#!/usr/bin/env python3
"""Read-only Hulk ReXGlue forced-function boundaries audit. No gameplay edits."""
from __future__ import annotations
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import tomllib

LO, HI = 0x82110000, 0x82E0985C
HELPER_LO, HELPER_HI = 0x827377B0, 0x82738000

def entries(path: Path) -> dict[int, int | None]:
    funcs = tomllib.loads(path.read_text(encoding="utf-8-sig")).get("functions")
    if not isinstance(funcs, dict) or not funcs:
        raise ValueError(f"Invalid or empty [functions] in {path}")
    found = {}
    for k, val in funcs.items():
        start = int(k, 16)
        if start % 4 or not LO <= start < HI:
            raise ValueError(f"Invalid function start {k}")
        if not isinstance(val, dict):
            raise ValueError(f"Invalid function metadata {k}")
        end = val.get("end")
        if end is not None and (not isinstance(end, int) or end % 4 or not start < end <= HI):
            raise ValueError(f"Invalid function bounds {k}")
        found[start] = end
    return found

def audit(missing: dict[int,int|None], indirect: dict[int,int|None]) -> dict:
    common = sorted(set(missing)&set(indirect))
    spans = sorted((start,end) for start,end in indirect.items() if end is not None)
    overlaps = [[f"0x{a:08X}",f"0x{c:08X}"] for (a,b),(c,d) in zip(spans,spans[1:]) if b>c]
    starts = sorted(missing)
    inside = []
    for begin,end in spans:
        i = bisect.bisect_left(starts,begin)
        if i<len(starts) and starts[i]<end:
            inside.append([f"0x{begin:08X}",f"0x{starts[i]:08X}"])
    lens=sorted(end-start for start,end in spans)
    largest=sorted(((end-start,start,end) for start,end in spans),reverse=True)
    helpers=sorted(x for x in set(missing)|set(indirect) if HELPER_LO<=x<HELPER_HI)
    return {
        "schema":"hulk_rexglue_function_inventory_v1",
        "interpretation":"Both tables FORCE guest function starts; they do not skip code.",
        "missing":len(missing),"indirect":len(indirect),
        "duplicate_starts":[f"0x{x:08X}" for x in common[:50]],
        "indirect_overlaps":overlaps[:50],
        "missing_start_inside_indirect":inside[:50],
        "helper_region_starts":[f"0x{x:08X}" for x in helpers[:50]],
        "large_spans":[{"start":f"0x{a:08X}","end":f"0x{b:08X}","bytes":n}
                       for n,a,b in largest if n>=4096][:50],
        "span_percentiles_bytes":{str(p):lens[int((len(lens)-1)*p/100)]
                                  for p in (50,90,95,99,100)} if lens else {},
        "warnings":["Guest function boundaries are candidates, not PPC ABI proof.",
                    "Do not remove long spans without disassembly and caller validation.",
                    "Do not split save/restore helper fall-through chains.",
                    "Inspect bctrl indirect targets, shader/fetch state, and GPU packet order."]
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("missing",type=Path)
    p.add_argument("indirect",type=Path)
    p.add_argument("--output",type=Path)
    args=p.parse_args()
    report=audit(entries(args.missing),entries(args.indirect))
    report["input_sha256"]={
        "missing":hashlib.sha256(args.missing.read_bytes()).hexdigest(),
        "indirect":hashlib.sha256(args.indirect.read_bytes()).hexdigest()}
    out=json.dumps(report,indent=2)+"\n"
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(out,encoding="utf8")
    print(out)

if __name__=="__main__":
    main()
