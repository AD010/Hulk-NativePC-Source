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
        self.assertIn('::hulk::eor::xdk::EndPpcHook(type); return;',hooks)
        self.assertIn('::__imp__sub_',hooks) if False else None
        self.assertIn('__imp__sub_##addr(ctx,base);',hooks)
if __name__ == "__main__":
    unittest.main()
