#!/usr/bin/env python3
"""Replay PM4 register SET and memory LOAD in their ORIGINAL Xenos order.

NativeX originally collected PacketScan.writes and .loads separately, then
Gather always applied ALL writes first and ALL loads second. This is wrong
when SET_CONSTANT and LOAD_ALU_CONSTANT interleave, particularly for FETCH
descriptor slots: a stale overridden value can replace a later valid load.
Maintain original legacy arrays for diagnostics, while recording a compact
ordered index. Source-only patch: apply AFTER other Hulk NativeX source
patches (including frame_path_probe and bin_predication), before compilation.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")

HELPERS = """// HULK/PM4-REGISTER-ORDER: append to the source-specific lists AND the
// chronological stream. Even nested INDIRECT_BUFFER writes stay in order.
void HulkOrderedWrite(PacketScan& out, uint32_t reg, uint32_t value) {
  if (out.writes.size() >= 0x7fffffffu) { out.complete = false; return; }
  out.register_order.push_back(uint32_t(out.writes.size()) << 1);
  out.writes.emplace_back(reg, value);
}
void HulkOrderedLoad(PacketScan& out, uint32_t reg, uint32_t value) {
  if (out.loads.size() >= 0x7fffffffu) { out.complete = false; return; }
  out.register_order.push_back((uint32_t(out.loads.size()) << 1) | 1u);
  out.loads.emplace_back(reg, value);
}

"""

GATHER_HEAD = """  for (uint32_t hulk_tag : scan_.register_order) {
    const bool loaded = (hulk_tag & 1u) != 0;
    const auto& [reg, value] = loaded
        ? scan_.loads[hulk_tag >> 1] : scan_.writes[hulk_tag >> 1];"""

LAST_WRITE_OLD = """  for (auto it = scan_.writes.rbegin(); it != scan_.writes.rend(); ++it) {
    if (it->first == reg) {
      value = it->second;
      return true;
    }
  }"""
LAST_WRITE_NEW = """  // HULK/PM4-REGISTER-ORDER: LOAD_ALU_CONSTANT can overwrite a later or
  // earlier SET_CONSTANT. Query the last GPU update, not just SET packets.
  for (auto it = scan_.register_order.rbegin(); it != scan_.register_order.rend(); ++it) {
    const uint32_t tag = *it;
    const auto& entry = (tag & 1u) ? scan_.loads[tag >> 1] : scan_.writes[tag >> 1];
    if (entry.first == reg) {
      value = entry.second;
      return true;
    }
  }"""

OLD_LOAD_LOOP = """  for (const auto& [reg, value] : scan_.loads) {
    if (reg < rex::graphics::RegisterFile::kRegisterCount) SendValue(reg, value);
  }
"""

def exactly(s: str, old: str, new: str, description: str) -> str:
    n = s.count(old)
    if n != 1:
        raise ValueError(f"PM4 ordered state {description}: expected exactly once, found {n}")
    return s.replace(old, new, 1)

def transform(files: dict[str, str]) -> dict[str, str]:
    if set(files) != {"src/state.cpp", "src/state.h"}:
        raise ValueError("Expected NativeXRenderer src/state.cpp and src/state.h")
    cpp, hdr = files["src/state.cpp"], files["src/state.h"]
    if "HULK/PM4-REGISTER-ORDER" in cpp:
        if ("std::vector<uint32_t> register_order;" not in hdr or
            "HulkOrderedWrite(PacketScan&" not in cpp or
            "for (uint32_t hulk_tag : scan_.register_order)" not in cpp):
            raise ValueError("Partial ordered PM4 register patch")
        return files
    if "HULK/PFP-PREDICATE" not in cpp or "HULK/PM4-PAYLOAD" not in cpp:
        raise ValueError("Must apply BIN and PM4 payload patches first")
    hdr = exactly(hdr,
        "  std::vector<std::pair<uint32_t, uint32_t>> loads;\n",
        "  std::vector<std::pair<uint32_t, uint32_t>> loads;\n"
        "  // Chronological SET/LOAD updates, low bit=load, high bits=array index.\n"
        "  std::vector<uint32_t> register_order;\n", "PacketScan header")
    cpp = exactly(cpp, "  loads.clear();\n  shader_loads.clear();",
                  "  loads.clear();\n  register_order.clear();\n  shader_loads.clear();",
                  "Clear ordered updates")
    # Replace only SCANNER writes and loads. Do not touch Gather, which
    # operates on the separate vectors. 8 SET call sites, one LOAD call site
    # exist in both pinned NativeX state.cpp revisions.
    set_count = cpp.count("out.writes.emplace_back(")
    load_count = cpp.count("out.loads.emplace_back(")
    if not (7 <= set_count <= 16 and load_count == 1):
        raise ValueError(f"NativeX PM4 scan writer anchors drift: {set_count} writes, {load_count} loads")
    cpp = cpp.replace("out.writes.emplace_back(", "HulkOrderedWrite(out, ")
    cpp = cpp.replace("out.loads.emplace_back(", "HulkOrderedLoad(out, ")
    cpp = exactly(cpp, "// DRAW_INDX (viz query token, then as DRAW_INDX_2)",
                  HELPERS + "// DRAW_INDX (viz query token, then as DRAW_INDX_2)",
                  "helper insertion before Draw")
    cpp = exactly(cpp, LAST_WRITE_OLD, LAST_WRITE_NEW, "LastWrite chronology")
    cpp = exactly(cpp,
        "  for (const auto& [reg, value] : scan_.writes) {",
        GATHER_HEAD, "Gather ordered register operations")
    cpp = exactly(cpp,
        "      if (!draw) SendValue(reg, value);",
        "      if (!draw || loaded) SendValue(reg, value);",
        "load float constants even during Draw")
    cpp = exactly(cpp, OLD_LOAD_LOOP, "", "remove delayed LOAD application")
    # Existing frame_path_probe logs SET writes in Gather. The new ordered
    # loop includes LOAD_ALU_CONSTANT, so log provenance without rewriting the
    # existing diagnostic, and never alter the actual register values.
    return {"src/state.cpp": cpp, "src/state.h": hdr}

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_register_order.py <patched NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                  text=True).strip()
    if sha != PIN:
        raise ValueError("Unsupported pinned NativeXRenderer revision: " + sha)
    paths = ("src/state.cpp", "src/state.h")
    before = {name:(root / name).read_text(encoding="utf-8") for name in paths}
    after = transform(before)
    for name in paths:
        if after[name] != before[name]:
            (root / name).write_text(after[name], encoding="utf-8", newline="\n")
    print("Hulk chronological Xenos PM4 register SET/LOAD tracking:",
          "patched" if before != after else "already patched")

if __name__ == "__main__":
    main()
