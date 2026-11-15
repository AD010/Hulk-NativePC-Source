#!/usr/bin/env python3
import unittest
from audit_hulk_gpu_load import audit

class TestGpuTrace(unittest.TestCase):
    SAMPLE = """[2026-11-14] [native_x_renderer] renderer: frame 600: 4 draw calls, 4 drawn; GPU 1.00 ms average, 3.00 ms max over 600 frames
[2026-11-14] [native_x_renderer] renderer: frame 1200: 1975 draw calls, 1975 drawn; GPU 22.00 ms average, 47.00 ms max over 600 frames
[2026-11-14] [native_x_renderer] renderer: frame 1200: antialiasing (SMAA) applied 1 times in 600 frames
[2026-11-14] [native_x_renderer] renderer: frame 1200: device memory 678 MB used in 736 MB of pages
"""
    def test_timing_windows_and_careful_inference(self):
        r = audit(self.SAMPLE)
        self.assertEqual(r["window_count_over_gpu_budget"], 1)
        self.assertAlmostEqual(r["gpu_average_weighted_ms"], 11.5)
        self.assertEqual(r["max_recorded_gpu_ms"], 47)
        self.assertEqual(r["largest_draw_calls_in_last_frame_of_window"], 1975)
        self.assertEqual(r["aa_applied_windows"][0]["applications"], 1)
        self.assertFalse(r["gpu_utilization_percent_measured"])
        self.assertFalse(r["full_gameplay_fps_measured"])
    def test_invalid_or_missing_windows(self):
        for text in ("", "GPU: 96% (made up)",
                     "[native_x_renderer] renderer: frame 3: 1 draw calls, GPU 22.00 ms average, 12.00 ms max over 600 frames"):
            with self.assertRaises(ValueError): audit(text)
        with self.assertRaises(ValueError): audit(self.SAMPLE, 300)
    def test_ignore_early_unmeasured_gpu(self):
        with_zero = "[native_x_renderer] renderer: frame 1: 0 draw calls, GPU 0.00 ms average, 0.00 ms max over 0 frames\n"
        self.assertEqual(audit(with_zero + self.SAMPLE)["total_sampled_frames"], 1200)

if __name__ == "__main__":
    unittest.main()
