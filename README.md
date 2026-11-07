# Hulk NativePC — source-only public build workspace

This public repository contains project source code and build scripts, not game data.

**The Incredible Hulk (2008)** is owned by its respective rights holders. No Xbox 360 `default.xex`, XEXP Title Update, ARC/IND files, game assets, decrypted PPC images, or generated guest-code output may be committed here.

The host-side launcher and integration work is based on the [TekRantGaming community project](https://github.com/TekRantGaming/incredible-hulk-recompiled) (MIT license). See `LICENSE` and `native_port/LICENSE`. ReXGlue and NativeXRenderer are fetched at build time; preserve their notices.

## Free Windows CI

The public Actions workflow can compile on a GitHub-hosted Windows runner **only after** its owner configures the required private secret. The original XEX comes from a separately private repository. Any compiled ZIP is uploaded back to that **private** repository, never published as a public Actions artifact.

This is an experimental recompilation: native Vulkan drawing, higher frame rates, TU1 and the water-jump physics correction are not yet verified as working. The European TU1 is incompatible with the currently pinned USA XEX and cannot just be placed next to it.

**Start here:** [SECURE_BUILD_SETUP_RU.md](SECURE_BUILD_SETUP_RU.md). Keep source-only contributions free of copyrighted game assets and credentials.
