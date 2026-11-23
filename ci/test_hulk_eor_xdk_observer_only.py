#!/usr/bin/env python3
"""Observer-only must never accidentally initialize a second Vulkan renderer."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class TestEorObserverOnly(unittest.TestCase):
    def test_independent_xenos_observer_without_gpu_takeover(self):
        renderer=(ROOT/"native_port/src/native_x_renderer_hulk.cpp").read_text()
        hooks=(ROOT/"native_port/tools/gen_native_x_hooks.py").read_text()
        conf=(ROOT/"native_port/incredible_hulk.toml").read_text()
        self.assertIn('hulk_eor_xdk_capture = false',conf)
        self.assertIn('hulk_eor_vulkan_replay = false',conf)
        self.assertIn('const bool eor_observer_only =',renderer)
        self.assertIn('rex::cvar::Query<bool>("hulk_eor_xdk_capture")',renderer)
        self.assertIn('if (eor_observer_only) {',renderer)
        observer=renderer.split('if (eor_observer_only) {',1)[1].split('  std::error_code error;',1)[0]
        self.assertIn('nx::Install(g_profile.get());',observer)
        self.assertIn('return false;',observer)
        self.assertNotIn('native_x_renderer::Initialize',observer)
        self.assertNotIn('native_x_renderer::Renders()',observer)
        self.assertLess(renderer.index('if (eor_observer_only) {'),
                        renderer.index('native_x_renderer::Initialize('))
        self.assertIn('CapturePpcDraw',hooks)
        self.assertIn('CapturePpcCommand',hooks)
        self.assertIn('CommandKind::Present',hooks)
        self.assertIn('::hulk::eor::xdk::EndPpcHook(type); return;',hooks)
        self.assertIn('__imp__sub_##addr(ctx,base);',hooks)
    def test_live_replay_refuses_unowned_frame_before_mailbox_take(self):
        source=(ROOT/"native_port/src/eor_xdk_hook_bridge.cpp").read_text()
        replay=source.split("bool ReplayNextVerifiedPpcFrame(",1)[1].split(
            "void CapturePpcDraw(",1)[0]
        preflight=replay.split("std::shared_ptr<const Frame> preview;",1)[0]
        self.assertIn("!CaptureEnabled()",preflight)
        self.assertIn("!REXCVAR_GET(hulk_eor_vulkan_replay)",preflight)
        self.assertLess(preflight.index("!REXCVAR_GET(hulk_eor_vulkan_replay)"),
                        replay.index("std::shared_ptr<const Frame> preview;"))
        self.assertIn("REXCVAR_DEFINE_BOOL(hulk_eor_vulkan_replay, false",source)
        self.assertIn("!output.direct_engine_path_ready",preflight)
        self.assertIn("native::HasFullNativeCompositePixels(",preflight)
        self.assertIn("witnesses.empty()",preflight)
        self.assertIn("residents.empty()",preflight)
        self.assertIn("reflection.size()!=shaders.expected()",preflight)
        self.assertIn("delivered->overwritten_since_take",replay)
        self.assertIn("ValidateOriginalCommandTimeline(*preview)",replay)
        self.assertIn("gpu.CanReplayOriginalXdkTimeline(*preview)",replay)
        # A stale epoch is discarded by sequence before eligibility. Only
        # the successful take after preflight may authorize submission.
        handoff=replay.rindex("g_frames_for_native.TakeIfSequence(sequence)")
        eligibility=replay.index("gpu.CanReplayOriginalXdkTimeline(*preview)")
        self.assertLess(eligibility,handoff)
        self.assertLess(replay.index("preview.reset();",eligibility),handoff)
        self.assertLess(handoff,replay.index("native::ReplayDeliveredResourceScene("))
        discard=replay.index("g_frames_for_native.DiscardIfSequence(sequence)")
        self.assertLess(replay.index("preview.reset();", replay.index("if(missed)")),discard)
        self.assertLess(discard,eligibility)
        self.assertIn("catch (...)",replay)
        self.assertNotIn("native_x_renderer::Initialize(",replay)
        self.assertNotIn("device_->Present(",replay)
if __name__ == "__main__":
    unittest.main()
