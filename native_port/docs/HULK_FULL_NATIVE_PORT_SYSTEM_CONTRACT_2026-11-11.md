# The Incredible Hulk (2008) — full native Windows port contract

Date: 2026-11-11. Status: **user requirement / architecture and acceptance criteria**,
not a claim that these components are currently native or working.

## Final definition

The shipped project must be a self-contained native Windows x64 game
adaptation of the Xbox 360 USA release, not a wrapper around console
emulation. Both the game logic and the platform-facing subsystems must
execute as ordinary host code and integrate correctly with Windows.

**Native does not mean rewriting every line of game logic.** Ahead-of-time
PowerPC-to-C++/x64 recompilation is native CPU execution without a PPC
interpreter/JIT. A narrow, well-tested Xbox API *compatibility contract*
may still exist as a semantic adapter (e.g. guest save paths become host
file operations, guest threads become host scheduling, guest audio buffers
become native decoded/mixed audio). Such an adapter MUST NOT emulate a
virtual GPU/controller or conceal fundamentally incomplete functionality.
Original .arc game resources may remain in their authentic content
format, decoded by native host code. No requirement to reauthor the game.

Final release must NOT require:
- Xenia executable, GPU or EDRAM render emulation, or an Xenos graphics
  command interpreter to present frames;
- emulated Xbox 360 gamepads as the only way to use a mouse or keyboard;
- a synthetic 30 Hz cap to prevent physics/mission corruption;
- external batch launchers, unverified CRT DLL injections, downloaded
  proprietary Xbox system images or arbitrary guest-register patches;
- debug F4/F9/F11 interfaces for standard consumer operation.

## Subsystem migration and acceptance matrix

| Subsystem | Observed source baseline (11 Nov 2026) | Required final native host behavior and proof |
| --- | --- | --- |
| CPU / game logic | ReXGlue statically recompiles the USA game; guest PPC register/memory semantics remain needed | Host x64 generated code executes without PPC interpreter/JIT; all reachable game/mission paths resolve; ABI and endian correctness verified; no missing guest function or unregistered indirect dispatch |
| GPU / rendering | NativeXRenderer Vulkan path with historical Xenos plugin present in build output; currently severe RT/texture/shader issues | Host-native texture/buffer/render targets, shader translation, depth, resolves, swapchain, 16:10 scaling, lighting and complete frame parity; **no runtime rexgpu-xenos.dll or GPU ring/EDRAM raster dependency in final package** |
| Input / camera | native_input_win.cpp reads Win32 Raw Input, but native_input_map.cpp maps mouse into Xbox analog deflection, input_hook.cpp may synthesize XINPUT_STATE; native_camera_direct.cpp needs real validated camera layout | Direct Win32 raw relative mouse movement to verified EoR camera yaw/pitch and native key/button actions; no virtual-controller conversion required; remappable keys, separate game/UI focus, wheel, pause/cutscene camera, high DPI and cursor capture/release |
| Audio / video playback | No verified end-to-end Hulk native audio path was established by this audit; ReXGlue runtime dependency is present | Real host Windows audio backend with correct audio mixing, positional effects, speech/music and latency; Xbox asset compression, XMA or codecs decoded natively where necessary; correct cutscene playback and lip-sync; test with device switches/minimize |
| Filesystem / game archives | assets_locator.cpp uses std::filesystem and Win32 file picker, rex::filesystem; game still loads original user-owned archives | Native Windows Unicode paths and I/O, verified .arc/.ind discovery, correct mount priorities, streaming/caching and graceful missing-content messages, no writable files in Program Files unexpectedly; no hardcoded console path or drive prefix required |
| Save data / user profiles | ReXGlue guest runtime and game save semantics not yet audited to completion | Native per-user persistent save location and native atomic file writes with backups/validation; reproducible save/load, multi-slot support, language/settings persistence and robust recovery from interrupted write |
| Threading / synchronization | ReXGlue translates Xbox kernel/guest system semantics; earlier XInput lifetime/Alt-Tab issues have been diagnosed | Host threads, scheduler primitives, events, timers, mutexes, async I/O, GPU fences and deterministic teardown; no deadlocks, races or 0xc0000374 double-free; tested shutdown and minimize/restore |
| Timing / physics / animations | incredible_hulk.toml defaults gpu_guest_fps_limit=30; pc_menu.cpp explicitly labels 60 FPS experimental | Separate simulation delta and render/presentation clocks; stable 60 FPS baseline, higher FPS where safe; correct vehicle doors, physics, ragdolls, AI, attacks, animations and water interactions at variable frame rate; 30 FPS remains optional authenticity preset, not technical requirement |
| Windows window/display lifecycle | SDL host window + Win32 WndProc wrapper, current menu overlay; monitor 2560x1600/175% DPI | Per-monitor DPI awareness, stable exclusive/borderless/windowed, proper minimize/restore, Alt+Tab, cursor and HDR/SDR presentation where verified; correct frame pacing and native input focus |
| User interface and settings | pc_menu.cpp uses independent ImGui F10 overlay; original game still has Xbox UI assets; config is one TOML | Consumer-facing native PC Settings/Video/Controls/Audio/Language/Exit in game UI, Windows key glyphs, persistent validated settings and safe Apply/Reset; debug console not the consumer interface |
| Localization / Title Update | Pinned USA XEX; separately provided EU TU1 cannot be assumed binary compatible | Data-level localization loaded without changing USA code identity; port the actual TU1 behavior only after version/region/code compatibility proof; gameplay and water bugs fixed without injecting PAL addresses |
| Streaming / memory | Native runtime uses guest memory mirror and EoR archive/zone streaming; precise residency/LOD not proved here | Native host memory ownership, bounded allocation, real city asset residency, proper higher LOD/model streaming, no forced Xbox 360 memory cap where the engine can safely be expanded; robust GPU/CPU resource lifetime |
| Installation / dependencies | Builder delivers x64 EXE + rexruntime.dll + rexgpu-xenos.dll + one TOML; PE import audit in CI newer source | Single reproducible Windows x64 release with only genuinely required redistributable dependencies, no external build tools/launch scripts, legally deployable DLLs, deterministic asset setup; meaningful crash and dependency diagnostics |
| Optional platform services | Xbox-specific achievements/network/storage features have not been audited | No Xbox Live dependency for offline campaign. If a feature exists in the game, replace with native Windows/local functionality or document honestly if online services are discontinued; do not claim unavailable Xbox endpoints work |

## Measurable exit gates

- **N0 / Inventory:** identify every guest Xbox API that the final retail
  campaign uses (graphics, sound, kernel, filesystem, input, save, video).
  Mark observed, inferred, unverified and unsupported separately; log
  addresses only from the SHA-pinned USA XEX. No guessed hook installation.
- **N1 / I/O and input:** keyboard/mouse and camera work without gamepad
  emulation; correct focus, wheel and remapping; all basic game/menu actions
  complete. Validate actual game calls, not only a synthetic driver unit test.
- **N2 / CPU/kernel and storage:** complete missions, frame pacing, scheduler,
  content streaming, file access and save/load without unsafe guest
  stubs, guest CPU interpretation or stuck synchronization.
- **N3 / GPU independence:** native renderer alone provides all world/menu
  draws, shader variants, clears and scanout, plus GPU/CPU sync semantics.
  Remove Xenos runtime plugin dependency and ensure no hidden fallback;
  compare RenderDoc frames and original Xbox scene references.
- **N4 / audio and media:** full soundtrack/dialogue/effects/cutscenes,
  correct A/V timing after Alt+Tab and audio device changes; no missing
  external codec blobs or emulator audio bridge.
- **N5 / gameplay timing:** deterministic side-by-side 30/60/120 FPS
  comparisons; measure physics, player state, AI, timers, ragdolls and
  water; pass campaign-critical events at 60 FPS before unlocking.
- **N6 / official-quality UX:** boot → menu → missions → save/load → exit,
  native graphics/input/audio/language settings and 16:10 at 175% DPI,
  stable shutdown, no debug controls required.
- **N7 / release audit:** final Windows package has no Xenos GPU rendering
  plugin and no virtual-controller runtime requirement, only documented
  redistributables and user-owned original game assets. Full campaign and
  reproducible hardware tests pass.

A green GitHub Actions build is **not** completion of N0–N7. Keep both
native render and platform audits in the *existing* AD010/Hulk-NativePC-Source
repository and the one existing Windows Release workflow. Do not put user
assets/XEX/shaders/game logs into public Git.

## Next technical action

Use the currently added high-level D3D/XDK address-only call graph and
opt-in PPC ABI probe to identify genuine USA D3D/guest API boundaries.
In parallel, build an inventory of game-reachable imported Xbox API
families and the ReXGlue modules backing them. Prioritize deleting
controller mapping by validating real EoR camera functions; isolate and
replace GPU emulation only after visual correctness is demonstrated.
Investigate audio, save and kernel behavior with actual runtime traces
before claiming that source presence proves functionality.

The earlier HULK_NATIVE_D3D_LAYER_STRATEGY_2026-11-11.md remains the
graphics-focused technical plan. This contract expands the definition
of **native** to the whole product and overrides any interpretation that
a Vulkan-only graphics migration meets the final user requirement.
