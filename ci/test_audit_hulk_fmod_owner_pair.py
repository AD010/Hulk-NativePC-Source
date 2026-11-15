#!/usr/bin/env python3
"""Synthetic-only FMOD x86/Xbox index comparison regressions."""
import struct
import unittest
from audit_hulk_fmod_owner_pair import compare_rows,dll_arch

class Tests(unittest.TestCase):
    def test_archive_payload_identity_and_trailer_change(self):
        rows=[{"hash":"A","offset":4,"size":20,"reserved":0},
              {"hash":"B","offset":32,"size":20,"reserved":0}]
        a=bytes(90);b=bytearray(a);b[-3:]=b"ABC"
        r=compare_rows(rows,a,bytes(b))
        self.assertEqual(r["identical_indexed_records"],2)
        self.assertEqual(r["changed_bytes_inside_indexed_records"],0)
        self.assertEqual(r["changed_bytes_outside_indexed_records"],3)
        b[35]=1
        r=compare_rows(rows,a,bytes(b))
        self.assertEqual(r["different_indexed_records"],1)
        self.assertEqual(r["changed_bytes_inside_indexed_records"],1)
    def test_corrupt_index_and_arch_rejected(self):
        rows=[{"hash":"A","offset":4,"size":20,"reserved":0},
              {"hash":"B","offset":10,"size":20,"reserved":0}]
        with self.assertRaises(ValueError):compare_rows(rows,bytes(80),bytes(80))
        with self.assertRaises(ValueError):compare_rows(rows[:1],bytes(80),bytes(79))
        with self.assertRaises(ValueError):dll_arch(b"not a PE")
    def test_real_pe_machine_flag(self):
        image=bytearray(256);image[:2]=b"MZ"
        struct.pack_into("<I",image,0x3c,0x80)
        image[0x80:0x84]=b"PE\x00\x00"
        struct.pack_into("<H",image,0x84,0x014c)
        self.assertEqual(dll_arch(bytes(image))["arch"],"x86")
        struct.pack_into("<H",image,0x84,0x8664)
        self.assertEqual(dll_arch(bytes(image))["arch"],"x64")
if __name__=="__main__":unittest.main()
