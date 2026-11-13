#!/usr/bin/env python3
"""Synthetic guard tests for minimal SDK patch: not a full Windows build."""
from __future__ import annotations
import importlib.util
import pathlib
import tempfile

p = pathlib.Path(__file__).with_name("patch_sdk_hulk_fps_probe.py")
spec = importlib.util.spec_from_file_location("hulk_fps_probe", p)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as temp:
    root = pathlib.Path(temp)
    source = root / "src/kernel/xboxkrnl/xboxkrnl_video.cpp"
    source.parent.mkdir(parents=True)
    text = ('#include <atomic>\n' +
            'void VdSwap_entry(mapped_void buffer_ptr,\n'
            '                   mapped_void fetch_ptr) {\n'
            '  // All of these parameters are REQUIRED.\n'
            '}\n')
    source.write_text(text, encoding="utf-8")
    module.patch_sdk(root)
    content = source.read_text()
    assert 'REXCVAR_DEFINE_BOOL(hulk_frame_probe' in content
    assert 'REXCVAR_DEFINE_INT32(gpu_guest_fps_limit, 30' in content
    assert 'HULK/GUEST-FPS-CAP' in content
    assert 'std::this_thread::sleep_until(next_submit)' in content
    assert 'std::clamp(REXCVAR_GET(gpu_guest_fps_limit), 0, 240)' in content
    assert '#include <thread>' in content
    assert 'REXKRNL_INFO("[HULK/FPS] VdSwap rate=' in content
    assert content.count("HULK_FRAME_PROBE_V1") >= 1
    module.patch_sdk(root)
    assert content == source.read_text()
    source.write_text(text.replace("  // All of these parameters are REQUIRED.", "  // different version"), encoding="utf-8")
    try:
        module.patch_sdk(root)
    except ValueError:
        assert "HULK_FRAME_PROBE_V1" not in source.read_text()
    else:
        raise AssertionError("Unknown SDK version was not rejected")
print("Hulk VdSwap probe patch tests passed")
