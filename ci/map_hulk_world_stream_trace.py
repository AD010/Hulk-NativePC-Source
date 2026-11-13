#!/usr/bin/env python3
"""Map Hulk [HULK/STREAM] reads to indexed resource hashes (read-only).

Reads only game log text and the owner's original US INDEX.IND.
Do not infer the original asset filename or its high/low LOD class from a
hash; compare captures in low-altitude vs rooftop gameplay instead.

Usage: python ci/map_hulk_world_stream_trace.py USA_INDEX.IND HULK.log
"""
import argparse
import bisect
from collections import Counter
import json
from pathlib import Path
import re
from audit_hulk_tu1_resource_index import parse_index

LOG = re.compile(r"\[HULK/STREAM\] file=([^\s]+) offset=(\d+) requested=(\d+) read=(\d+) seq=(\d+)")
TIMED = re.compile(r"\bt_ms=(\d+) read_us=(\d+) ok=([01])\b")
CATEGORIES = {
    "DATASETS.ARC":"Datasets", "QUICKDAT.ARC":"QuickDatas",
    "QUICKDATAS.ARC":"QuickDatas", "MODELS.ARC":"Models",
    "OBJECTS.ARC":"Objects", "SHADERS.ARC":"Shaders",
    "TEXTURES.ARC":"Textures", "QDMETADATA.ARC":"QDMetadatas",
}


def summarize(index_data: dict, log: str) -> dict:
    by_category = {}
    for group, info in index_data.items():
        ranges = sorted(
            (v["offset"],v["offset"]+v["size"],h)
            for h,v in info["entries"].items() if v["size"] > 0
        )
        # The predecessor alone misses nested/shared archive extents.
        # Prefix maximum ends delimit every interval that can overlap a read.
        max_ends = []
        furthest = 0
        for _, end, _ in ranges:
            furthest = max(furthest, end)
            max_ends.append(furthest)
        by_category[group] = ([r[0] for r in ranges], max_ends, ranges)
    reads=Counter()
    hit=Counter()
    missing=Counter()
    timeline={}
    stalled_reads=[]
    for line in log.splitlines():
        match=LOG.search(line)
        if not match: continue
        file_name, offset, _, byte_count, _ = match.groups()
        group=CATEGORIES.get(file_name.upper())
        if group not in by_category: continue
        offset=int(offset);size=int(byte_count)
        timed=TIMED.search(line)
        if timed:
            t_ms, read_us, ok = map(int, timed.groups())
            if read_us > 10_000_000 or t_ms > 86_400_000:
                # Corrupt/mixed logs must not invent multi-day stalls.
                continue
            bucket=5000*(t_ms//5000)
            row=timeline.setdefault(bucket, {"start_ms":bucket,"reads":0,
                   "bytes":0,"failed":0,"read_us":[],
                   "resource_hits":Counter()})
            row["reads"]+=1
            row["bytes"]+=max(0,size)
            row["failed"]+=1 if not ok else 0
            row["read_us"].append(read_us)
            if read_us >= 50_000:
                stalled_reads.append({"file":file_name.upper(),"t_ms":t_ms,
                                      "read_us":read_us,"bytes":size})
        if size <= 0: continue
        reads[group]+=1
        starts,max_ends,ranges=by_category[group]
        first=bisect.bisect_right(max_ends,offset)
        stop=bisect.bisect_left(starts,offset+size)
        matched=False
        for i in range(first,stop):
            if ranges[i][1]>offset:
                hit[(group,ranges[i][2])]+=1
                if timed and ok:
                    row["resource_hits"][(group,ranges[i][2])]+=1
                matched=True
        if not matched: missing[group]+=1
    windows=[]
    for bucket,row in sorted(timeline.items()):
        latencies=sorted(row["read_us"])
        n=len(latencies)
        windows.append({"start_ms":bucket,"reads":row["reads"],
                        "bytes":row["bytes"],"failed":row["failed"],
                        "p95_read_us":latencies[(95*n+99)//100-1],
                        "slowest_read_us":latencies[-1],
                        "distinct_indexed_resources":len(row["resource_hits"]),
                        "top_resource_hashes":[
                            {"category":g,"hash":h,"reads":count}
                            for (g,h),count in row["resource_hits"].most_common(12)
                        ]})
    return {
        "reads_by_archive":dict(reads),
        "unmatched_reads":dict(missing),
        "top_resource_hashes":[{"category":g,"hash":h,"reads":n}
                               for (g,h),n in hit.most_common(100)],
        "timeline_5s":windows[:17280],
        "slow_reads":sorted(stalled_reads, key=lambda x:-x["read_us"])[:50],
        "sampling":"Logged read samples only; counts and p95 are not complete I/O totals or population latency percentiles. Overlapping resource extents may produce multiple hash matches for one read.",
        "warning":"Reads indicate file I/O only. Missing LODs may instead be game frustum/distance culling, predecoded resources, or CPU/GPU budgets; hashes do not identify asset names.",
    }


def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("usa_index",type=Path)
    p.add_argument("log",type=Path)
    p.add_argument("--output",type=Path)
    args=p.parse_args()
    indexes=parse_index(args.usa_index.read_bytes())
    content=args.log.read_text(encoding="utf-8",errors="replace")
    txt=json.dumps(summarize(indexes,content),indent=2)+"\n"
    if args.output:args.output.write_text(txt,encoding="utf-8")
    print(txt)

if __name__=="__main__":main()
