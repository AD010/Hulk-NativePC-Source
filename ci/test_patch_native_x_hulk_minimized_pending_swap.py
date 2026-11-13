#!/usr/bin/env python3
"""No hidden-frame Vulkan presentation on NativeX D140's per-command pending queue."""
import unittest
from patch_native_x_hulk_minimized_pending_swap import (
    OLD, NEW, D140_HEAD_OLD, D140_HEAD_NEW, transform,
)


def d140_fixture():
    return (
        D140_HEAD_OLD + """
  int ready = -1;
  if (ready >= 0) {
    ++g_pacing.at_swap;
    g_pacing.skipped += uint32_t(ready);
    ShowPendingFrame(uint32_t(ready));
  } else if (room && g_pending_count == kMaxPendingFrames) {
    ++g_pacing.before_swap;
    ShowPendingFrame(0);
  } else if (check_time && std::chrono::steady_clock::now() - g_pending_frames[0].since > kSwapTimeout) {
    ++g_pacing.timed_out;
    ShowPendingFrame(0);
  }
}
void ExecutePresent(const Command& c) {
  if (!g_device->UpdateSize()) { /* D125: FinishHiddenFrame */ }
}"""
    )


class MinimizedD140Tests(unittest.TestCase):
    def test_original_d135_guard_is_preserved(self):
        old=OLD+"\nvoid ExecutePresent() { if (!g_device->UpdateSize()) {} }"
        patched=transform(old)
        self.assertIn(NEW,patched)
        self.assertEqual(transform(patched),patched)

    def test_d140_checks_visible_window_only_on_actual_show_path(self):
        original=d140_fixture()
        updated=transform(original)
        self.assertEqual(transform(updated),updated)
        self.assertIn(D140_HEAD_NEW,updated)
        self.assertEqual(updated.count("if (!hulk_pending_visible()) return;"),3)
        self.assertIn("HULK/RETAIL-PRESENT-FOCUS",updated)
        self.assertIn("if (!g_pending_count) return;",updated)
        self.assertLess(updated.index("if (!hulk_pending_visible()) return;"),
                        updated.index("++g_pacing.at_swap;"))
        self.assertLess(
            updated.index("if (!hulk_pending_visible()) return;",updated.index("else if (room")),
            updated.index("++g_pacing.before_swap;"))
        self.assertLess(
            updated.index("if (!hulk_pending_visible()) return;",updated.index("else if (check_time")),
            updated.index("++g_pacing.timed_out;"))
        self.assertEqual(updated.count("ShowPendingFrame("),3)

    def test_changed_d140_decisions_fail_closed(self):
        input0=d140_fixture()
        with self.assertRaisesRegex(ValueError,"decision structure"):
            transform(input0.replace(
                "++g_pacing.before_swap;", "++g_pacing.fake;"))
        partial=transform(input0).replace(
            "if (!hulk_pending_visible()) return;", "", 1)
        with self.assertRaisesRegex(ValueError,"Partial D140"):
            transform(partial)


if __name__=="__main__":
    unittest.main()
