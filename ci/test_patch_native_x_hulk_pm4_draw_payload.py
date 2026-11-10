#!/usr/bin/env python3
"""Source-only regression tests for Hulk PM4 DMA payload boundaries."""
import unittest
from patch_native_x_hulk_pm4_draw_payload import OLD, NEW, CALLBACK_OLD, CALLBACK_NEW, patch_text

class PayloadTests(unittest.TestCase):
    def test_full_and_idempotent(self):
        raw=OLD + "\n" + CALLBACK_OLD + "\n" + CALLBACK_OLD
        after,changed=patch_text(raw)
        self.assertTrue(changed)
        self.assertEqual(patch_text(after),(after,False))
        with self.assertRaises(ValueError):
            patch_text("unknown scanner")
        with self.assertRaises(ValueError):
            patch_text(NEW+"\n"+CALLBACK_OLD+"\n"+CALLBACK_OLD)

    def test_incomplete_dma_never_captured(self):
        self.assertIn("if (q + 4 > q_end)",NEW)
        self.assertIn("if (dma && q + 12 > q_end)",NEW)
        self.assertIn("out.complete = false;",NEW)
        self.assertIn("if (on_packet_draw && out.complete)",CALLBACK_NEW)
        self.assertLess(NEW.index("if (dma && q + 12 > q_end)"),
                        NEW.index("out.writes.emplace_back(kVgtDrawInitiator"))
        self.assertLess(NEW.index("out.writes.emplace_back(kVgtDmaSize"),
                        NEW.index("++out.draws;"))

if __name__=="__main__":
    unittest.main()
