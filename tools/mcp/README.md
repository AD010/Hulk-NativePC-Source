# Local Hulk MCP registration (Windows Codex)

This opt-in setup uses two real third-party stdio servers. It is not an
installed ChatGPT web plugin. No server is started in GitHub Actions;
no game bytes or captures should be committed. Keep the Ghidra HTTP
listener on loopback. Do not forward ports or expose either service.

## Prerequisites

Install local Codex CLI, uv, Git, RenderDoc (upstream bridge baseline
1.43), and Ghidra 12.1.4 with matching JDK and XEX loader. Review and
clone these two upstream projects outside the Hulk source tree:

- https://github.com/scarletfantasy/renderdoc-mcp (GUI bridge; avoids
  requiring a standalone renderdoc.pyd)
- https://github.com/LaurieWired/GhidraMCP (live Ghidra extension)

Use audited/pinned revisions rather than automatically downloading
unreviewed code on each session. Record each checkout's git rev-parse HEAD
locally. Follow upstream installation instructions; in RenderDoc MCP run
uv sync; in Ghidra MCP run uv sync and install the matching released
Ghidra extension from its Releases page via File > Install Extensions.
Restart Ghidra, enable GhidraMCPPlugin, verify the listener is local only,
and open the private USA XEX project with the existing XEX loader.
Check the existing SHA-pinned USA audit in tools/ghidra/README.md.

From the Hulk checkout, run (replace these example paths):

    pwsh -NoProfile -File tools/mcp/Connect-HulkMcp.ps1 -RenderDocMcpRoot "C:/Tools/renderdoc-mcp" -QRenderDoc "C:/Program Files/RenderDoc/qrenderdoc.exe" -GhidraMcpRoot "C:/Tools/GhidraMCP"

The script verifies local paths, checks the loopback port, refuses to
replace existing named entries, installs the RenderDoc bridge extension
(which modifies the user's RenderDoc extension configuration), and adds
two Codex stdio registrations. It does not install Ghidra or Codex itself.
If the second registration fails, inspect the first with codex mcp get.
Restart Codex. To unregister use codex mcp remove hulk_renderdoc and
codex mcp remove hulk_ghidra (this does not uninstall the GUI extensions).

## Acceptance (not performed by a registration)

1. In local Codex ask for tools from both named servers. Call
   renderdoc_get_server_status. Open a private .rdc capture with
   renderdoc_open_capture and inspect overview and actual pipeline bindings.
2. Query Ghidra's currently open program and list/decompile a known original
   USA game function; cross-check with the SHA-pinned audit. Start read-only;
   approve any renaming or changes to the local Ghidra database explicitly.
3. Follow the existing Hulk skills under .agents/skills. Use real draw
   evidence to fix the original EoR runtime, not fake textures or PM4 fallback.
   Keep raw capture resources and original game code out of public Git.
4. Record server versions, actual call results and failures privately.
   Listing configured servers is not a successful replay or decompilation.

This setup has not been run on the owner's Windows machine. It requires
local consent and installed dependencies. No additional Release workflow.
