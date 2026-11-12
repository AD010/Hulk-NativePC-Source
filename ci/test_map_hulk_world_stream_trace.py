#!/usr/bin/env python3
import unittest
from map_hulk_world_stream_trace import summarize

class TestHulkWorldMap(unittest.TestCase):
    def test_indexed_read_correlates_and_unmatched(self):
        idx={"Datasets":{"entries":{
           "721AAFDE":{"offset":4096,"size":500,"unknown":0},
           "CA54CBC0":{"offset":8192,"size":1000,"unknown":0}
        }}}
        log=("[HULK/STREAM] file=DATASETS.ARC offset=4096 requested=100 read=100 seq=1\n"
             "[HULK/STREAM] file=DATASETS.ARC offset=8192 requested=100 read=100 seq=2\n"
             "[HULK/STREAM] file=DATASETS.ARC offset=100 requested=4 read=4 seq=3")
        report=summarize(idx,log)
        self.assertEqual(report["reads_by_archive"]["Datasets"],3)
        self.assertEqual(report["unmatched_reads"]["Datasets"],1)
        self.assertEqual(len(report["top_resource_hashes"]),2)
        self.assertEqual({e["hash"] for e in report["top_resource_hashes"]},
                         {"721AAFDE","CA54CBC0"})
    def test_timed_stream_windows_and_failure_status(self):
        idx={"Models":{"entries":{
            "AAAA0001":{"offset":10,"size":4096,"unknown":0}}}}
        log=("[HULK/STREAM] file=MODELS.ARC offset=12 requested=100 read=100 seq=1 t_ms=4900 read_us=1500 ok=1\n"
             "[HULK/STREAM] file=MODELS.ARC offset=20 requested=100 read=0 seq=2 t_ms=5100 read_us=64000 ok=0\n"
             "[HULK/STREAM] file=MODELS.ARC offset=40 requested=100 read=60 seq=3 t_ms=5200 read_us=80000 ok=1")
        report=summarize(idx,log)
        self.assertEqual([x["start_ms"] for x in report["timeline_5s"]],[0,5000])
        self.assertEqual(report["timeline_5s"][1]["reads"],2)
        self.assertEqual(report["timeline_5s"][1]["failed"],1)
        self.assertEqual(report["timeline_5s"][1]["p95_read_us"],80000)
        self.assertEqual(report["slow_reads"][0]["read_us"],80000)
        self.assertEqual(report["reads_by_archive"]["Models"],2)

    def test_legacy_stream_lines_without_timing_remain_supported(self):
        report=summarize({"Models":{"entries":{}}},
                         "[HULK/STREAM] file=MODELS.ARC offset=1 requested=1 read=1 seq=3")
        self.assertEqual(report["timeline_5s"],[])
        self.assertEqual(report["reads_by_archive"]["Models"],1)

    def test_foreign_files_and_empty_ranges(self):
        report=summarize({"Datasets":{"entries":{}}},
                         "[HULK/STREAM] file=MODEL.ARC offset=0 requested=10 read=10 seq=1")
        self.assertEqual(report["top_resource_hashes"],[])

if __name__=="__main__":unittest.main()
