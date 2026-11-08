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
    def test_foreign_files_and_empty_ranges(self):
        report=summarize({"Datasets":{"entries":{}}},
                         "[HULK/STREAM] file=MODEL.ARC offset=0 requested=10 read=10 seq=1")
        self.assertEqual(report["top_resource_hashes"],[])

if __name__=="__main__":unittest.main()
