#!/usr/bin/env python3
"""Make ReXGlue Xenos texture-fetch disassembly bounds-safe.

Log195: NativeXRenderer records two Shader::AnalyzeUcode access violations
with 27-dword vertex microcode. Shader analysis calls disassembly, and
the SDK indexed short diagnostic filter-name arrays without a range check.
This patch changes TEXT FORMATTING ONLY, not compiled GPU instructions,
samplers or shader semantics. Applied after the existing vertex format guard.
"""
from pathlib import Path
import sys

FILE="src/graphics/pipeline/shader/translator_disasm.cpp"
OLD = """  static const char* kAnisoFilterNames[] = {
      "disabled", "max1to1", "max2to1", "max4to1", "max8to1", "max16to1", "keep",
  };"""
NEW = OLD + """
  // HULK/TEXTURE-FETCH-DISASM-BOUNDS: malformed/unknown decoded shader
  // attributes must not index diagnostic C-string arrays out of bounds.
  // Return a printable fallback; shader filter bits remain unchanged.
  const auto hulk_texture_filter_name = [&](xenos::TextureFilter filter) {
    const auto index = static_cast<int>(filter);
    return index >= 0 &&
                   static_cast<unsigned>(index) <
                       sizeof(kTextureFilterNames) / sizeof(kTextureFilterNames[0])
               ? kTextureFilterNames[index]
               : "unknown";
  };
  const auto hulk_aniso_filter_name = [&](xenos::AnisoFilter filter) {
    const auto index = static_cast<int>(filter);
    return index >= 0 &&
                   static_cast<unsigned>(index) <
                       sizeof(kAnisoFilterNames) / sizeof(kAnisoFilterNames[0])
               ? kAnisoFilterNames[index]
               : "unknown";
  };"""
REPLACEMENTS = {
    "kTextureFilterNames[static_cast<int>(attributes.mag_filter)]": "hulk_texture_filter_name(attributes.mag_filter)",
    "kTextureFilterNames[static_cast<int>(attributes.min_filter)]": "hulk_texture_filter_name(attributes.min_filter)",
    "kTextureFilterNames[static_cast<int>(attributes.mip_filter)]": "hulk_texture_filter_name(attributes.mip_filter)",
    "kAnisoFilterNames[static_cast<int>(attributes.aniso_filter)]": "hulk_aniso_filter_name(attributes.aniso_filter)",
    "kTextureFilterNames[static_cast<int>(attributes.vol_mag_filter)]": "hulk_texture_filter_name(attributes.vol_mag_filter)",
    "kTextureFilterNames[static_cast<int>(attributes.vol_min_filter)]": "hulk_texture_filter_name(attributes.vol_min_filter)",
}

def patch_text(source: str) -> tuple[str, bool]:
    ready = NEW in source and all(
        new in source and old not in source for old, new in REPLACEMENTS.items()
    )
    if ready:
        return source, False
    if "HULK/TEXTURE-FETCH-DISASM-BOUNDS" in source or any(
        new in source for new in REPLACEMENTS.values()
    ):
        raise ValueError("Partial Hulk texture fetch disassembly guard")
    if source.count(OLD) != 1 or not all(
        source.count(old) == 1 for old in REPLACEMENTS
    ):
        raise ValueError("ReXGlue texture fetch disassembly anchors changed")
    if "DataFormat=unlisted" not in source:
        raise ValueError("Apply prior vertex format disassembly fix first")
    source = source.replace(OLD, NEW, 1)
    for old, new in REPLACEMENTS.items():
        source = source.replace(old, new, 1)
    return source, True

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_fetch_disasm_bounds.py SDK_ROOT")
    file = Path(sys.argv[1]) / FILE
    source = file.read_text(encoding="utf-8-sig")
    result, changed = patch_text(source)
    if changed:
        file.write_text(result, encoding="utf-8", newline="\n")
    print("Hulk texture fetch diagnostics bounds:",
          "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
