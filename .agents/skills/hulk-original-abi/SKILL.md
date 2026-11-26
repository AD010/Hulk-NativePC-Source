---
name: hulk-original-abi
description: Recover original USA Xbox 360 EoR, Havok, input, UI and audio ABI using Ghidra, original archives and runtime evidence.
---
Read CLAUDE.md and tools/ghidra/README.md. Use its existing SHA-pinned headless
audit, not a second importer. For UI use ci/ghidra_hulk_ui_xrefs.py; for camera
use ci/ghidra_hulk_camera_projection.py. Validate original callsite, object
producer, field offset, lifetime and runtime consumer independently. A callsite
alone does not establish ABI. Keep USA XEX, PC PE32 and PAL evidence separate.
If a locally approved Ghidra MCP is available use it read-only first; otherwise
use existing headless helpers. Do not upload decompiled guest code. Implement
the proven path and test at the original menu or in gameplay.
