# Hulk NativePC — source-only public build workspace

This public repository contains project source code and build scripts, not game data.

**The Incredible Hulk (2008)** is owned by its respective rights holders. No Xbox 360 `default.xex`, XEXP Title Update, ARC/IND files, game assets, decrypted PPC images, or generated guest-code output may be committed here.

The host-side launcher and integration work is based on the [TekRantGaming community project](https://github.com/TekRantGaming/incredible-hulk-recompiled) (MIT license). See `LICENSE` and `native_port/LICENSE`. ReXGlue and NativeXRenderer are fetched at build time; preserve their notices.

## Manual Windows Release

The **single** Windows GitHub Actions workflow is manual-only: a source edit
cannot start a build. Its owner must configure private input access and
explicitly dispatch a Release. The original USA XEX stays in a separate
private repository, and any validated ZIP is delivered privately, never
attached as a public Actions artifact.

The November 17, 2026 experimental Release attempts failed their source-only
or MSVC regression/link checks. No ZIP from those attempts is being called
ready, and no visible render improvement was verified. See
[`native_port/README.md`](native_port/README.md) for the consolidated
NativeX patch dispatcher, cleanup and required pre-release gates.

This is an experimental recompilation: native Vulkan drawing, higher frame rates, TU1 and the water-jump physics correction are not yet verified as working. The European TU1 is incompatible with the currently pinned USA XEX and cannot just be placed next to it.

**Start here:** [SECURE_BUILD_SETUP_RU.md](SECURE_BUILD_SETUP_RU.md). Keep source-only contributions free of copyrighted game assets and credentials.
