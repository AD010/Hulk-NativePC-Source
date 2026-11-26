---
name: hulk-render-replay
description: Diagnose Hulk EoR Vulkan frame failures, flicker, missing geometry, mip blur and Resolve using real RenderDoc replay evidence.
---
Read AGENTS.md and tools/renderdoc/README.md. Obtain a private capture from the
exact failing build and a comparable known-good capture; record build SHA,
scene, resolution and driver privately. Use the local exporter to inventory
draws and selected pipeline bindings. Inspect the earliest divergent event
in RenderDoc, including vertex/index range, actual VS/PS, descriptor resources,
mips, depth, attachments and Resolve order. Resource IDs are capture-local.
Trace the failing binding back to the actual original EoR producer; never
invent guest ABI or use PM4 fallback as proof. Patch the live runtime, add a
regression that fails before the fix, then capture again at the same scene.
Check 2560x1600, HUD, camera, destruction and frame time. No claim of visual
success without real replay and game evidence. Do not publish captures.
