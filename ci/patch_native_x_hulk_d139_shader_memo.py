#!/usr/bin/env python3
"""Backport upstream NativeX D139's address-aware shader memo to Hulk's reviewed D135 pin.

No altered shader bytes, GPU commands, cache ownership or title-specific heuristics:
a non-immediate IM_LOAD is reused ONLY if the previously owned program has exactly
the same address, size and byte sequence. Hulk's PS-load provenance still executes
before this fast path. Do not migrate to D140's changed queue/ABI by changing the pin.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e")
PINS = {"919371b5bdae0c9c4c7120ca321f5a4d72b7e78e",
        "bf965e952c98e050753e91d08f32685a9f754b12"}

HEADER_OLD = "  std::array<const Program*, 256> recent_programs_{};"
HEADER_NEW = HEADER_OLD + """
  // HULK/D139-OWNED-SHADER-MEMO: immutable program last loaded by address.
  // A byte-for-byte comparison is required: games can patch IM_LOAD in place.
  struct HulkLoadedAt {
    uint32_t address = 0, dwords = 0;
    const Program* program = nullptr;
  };
  std::array<HulkLoadedAt, 256> hulk_loaded_at_{};"""

RESET_OLD = """  hulk_ps_recent_serial_ = 0;
  record_.Clear();"""
RESET_NEW = """  hulk_ps_recent_serial_ = 0;
  hulk_loaded_at_ = {};
  record_.Clear();"""

HASH_OLD = "    uint64_t hash = XXH3_64bits(ucode, size_t(load.dwords) * sizeof(uint32_t));"
HASH_NEW = """    // HULK/D139-OWNED-SHADER-MEMO: the bytes are already owned by the
    // command capture (HULK/XDK-OWNED-IM-LOAD). A matching guest address
    // alone is NEVER proof of matching microcode; compare all dwords.
    HulkLoadedAt* hulk_at = load.immediate ? nullptr
        : &hulk_loaded_at_[((load.address >> 4) ^ (load.address >> 12)) & 255u];
    if (hulk_at && hulk_at->program && hulk_at->address == load.address &&
        hulk_at->dwords == load.dwords && load.dwords != 0 &&
        hulk_at->program->ucode.size() == load.dwords &&
        !std::memcmp(hulk_at->program->ucode.data(), ucode,
                     size_t(load.dwords) * sizeof(uint32_t))) {
      programs_[load.type] = hulk_at->program;
      continue;
    }
    uint64_t hash = XXH3_64bits(ucode, size_t(load.dwords) * sizeof(uint32_t));"""

HIT_OLD = """      programs_[load.type] = recent;
      continue;
    }
    std::shared_ptr<const Program>& cached = program_cache_[hash];"""
HIT_NEW = """      programs_[load.type] = recent;
      if (hulk_at) *hulk_at = {load.address, load.dwords, recent};
      continue;
    }
    std::shared_ptr<const Program>& cached = program_cache_[hash];"""

TAIL_OLD = """    programs_[load.type] = recent = cached.get();
  }

  if (draw) {"""
TAIL_NEW = """    programs_[load.type] = recent = cached.get();
    if (hulk_at) *hulk_at = {load.address, load.dwords, recent};
  }

  if (draw) {"""

PAIRS = ((HEADER_OLD, HEADER_NEW, "state.h: hash cache"),
         (RESET_OLD, RESET_NEW, "state.cpp: reset"),
         (HASH_OLD, HASH_NEW, "state.cpp: IM_LOAD owned microcode"),
         (HIT_OLD, HIT_NEW, "state.cpp: recent-program reuse"),
         (TAIL_OLD, TAIL_NEW, "state.cpp: program cache insertion"))


def patch(files: dict[str, str]) -> tuple[dict[str, str], bool]:
    if set(files) != {"src/state.h", "src/state.cpp"}:
        raise ValueError("Expected only NativeX src/state.h and src/state.cpp")
    header, cpp = files["src/state.h"], files["src/state.cpp"]
    # NativeX D139 is already implemented in the new D140 pin.
    # Do NOT install a second copy of the same 256-slot address memo.
    # Require upstream's byte comparison, cache update and reset together.
    upstream_markers = (
        "struct LoadedAt {" in header,
        "std::array<LoadedAt, 256> loaded_at_{};" in header,
        "\n  loaded_at_ = {};" in cpp,
        "!std::memcmp(at->program->ucode.data(), ucode," in cpp,
        "if (at) *at = {load.address, load.dwords, recent};" in cpp,
    )
    if all(upstream_markers):
        if "HULK/PS-RECENT-LOAD" not in cpp or "HULK/XDK-OWNED-IM-LOAD" not in cpp:
            raise ValueError("Upstream D139 memo lacks Hulk validated IM_LOAD protection")
        return files, False
    if any(upstream_markers):
        raise ValueError("Incomplete upstream D139 address memo; refusing duplicate")
    installed = [HEADER_NEW in header, RESET_NEW in cpp, HASH_NEW in cpp,
                 HIT_NEW in cpp, TAIL_NEW in cpp]
    if all(installed):
        return files, False
    if any(installed) or "HULK/D139-OWNED-SHADER-MEMO" in header + cpp:
        raise ValueError("Partial D139 shader memo installation: refusing mutation")
    if "HULK/PS-RECENT-LOAD" not in cpp or "HULK/XDK-OWNED-IM-LOAD" not in cpp:
        raise ValueError("Hulk PS provenance and owned external IM_LOAD required")
    for old, new, location in PAIRS:
        target = "src/state.h" if location.startswith("state.h") else "src/state.cpp"
        source = header if target.endswith(".h") else cpp
        if source.count(old) != 1:
            raise ValueError(f"D139 shader memo source drift {location}: found {source.count(old)}")
        if target.endswith(".h"):
            header = header.replace(old, new, 1)
        else:
            cpp = cpp.replace(old, new, 1)
    return {"src/state.h": header, "src/state.cpp": cpp}, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_d139_shader_memo.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN or PIN not in PINS:
        raise ValueError("Unreviewed NativeX shader-cache revision " + revision)
    paths = ("src/state.h", "src/state.cpp")
    before = {p: (root/p).read_text(encoding="utf-8") for p in paths}
    after, changed = patch(before)
    if changed:
        for p in paths:
            (root/p).write_text(after[p], encoding="utf-8", newline="\n")
    print("Hulk D139 immutable shader memo:", "installed" if changed else "already installed")


if __name__ == "__main__":
    main()
