# Private RenderDoc replay helper

This source-only helper uses RenderDoc's own Python API, not an emulator
and not a connected MCP service. It exports draw metadata and, for selected
events, pipeline shader IDs and attachment IDs. It never exports textures,
buffer contents, shader code or screenshots. The report is still private.

1. Install RenderDoc from https://renderdoc.org/ on the Windows test machine.
2. Capture a failing Vulkan frame using RenderDoc's normal UI. Save outside
   the source checkout, e.g. E:/HulkAudit/bad.rdc. Open that capture.
3. Open Window > Python Shell. Run the following using the actual checkout
   path (keep the report outside the checkout):

    exec(open(r"E:/HulkSource/tools/renderdoc/export_frame.py", encoding="utf-8").read())
    pyrenderdoc.Replay().BlockInvoke(lambda controller: export_frame(controller, rd, r"E:/HulkAudit/bad.json", [123, 456]))

   Use [] to inventory all draw events without querying individual pipelines.
   Replace example event IDs with real draw IDs from the first inventory.
   The RenderDoc shell normally exposes rd; if not, run import renderdoc as rd.
4. Repeat for a known-good build and same scene. Resource IDs are local to
   a capture and cannot be directly compared across builds. Use event order,
   counts, markers in RenderDoc and source-traced material identity.
5. Inspect selected events in the Pipeline State and Mesh Viewer. Use Pixel
   History where supported. Record the build SHA, driver, resolution and scene
   separately. Never commit the RDC or JSON. No proof of correct pixels is
   claimed by this inventory.

The helper is intentionally offline; no network listener, no third-party
MCP execution and no automatic modifications to the game. For an approved
local MCP bridge, verify its upstream source/version and bind locally; use
read-only replay tools first. Ghidra has its own existing headless workflow
in tools/ghidra/README.md.

API reference: https://github.com/baldurk/renderdoc/tree/v1.x/docs/python_api/examples
