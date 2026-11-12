#!/usr/bin/env python3
"""Prepare title-specific native Windows save roots without XUID folders.

Opt-in only (disabled until validated migration). Retail PC HULK.sav is never
opened/written/deleted by this feature. It preserves opaque Xbox-format
HULK.SAV package payloads in a separate Windows Documents/SEGA/.../NativePC
namespace and retains existing ReXGlue storage when disabled. DLC and other
titles are untouched. No new workflows, no original game/source bytes.
"""
from pathlib import Path
import sys

SOURCE = "src/system/xam/content_manager.cpp"
HEADER_PATH = "include/rex/system/xam/hulk_native_save_root.h"
INCLUDE_ANCHOR = "#include <rex/system/xam/content_manager.h>\n"
INCLUDE_NEW = "#include <rex/cvar.h>\n#include <rex/system/xam/hulk_native_save_root.h>\n" + INCLUDE_ANCHOR
NS_ANCHOR = "namespace rex {\nnamespace system {\nnamespace xam {\n"
NS_NEW = (
    'REXCVAR_DEFINE_BOOL(hulk_native_pc_saves, false, "Hulk/Storage",\n'
    '  "Opt in to native Windows Hulk save-package folders; does not convert Xbox data to PC 2008 format");\n\n'
    + NS_ANCHOR
)
ROOT_OLD = """std::filesystem::path ContentManager::ResolvePackageRoot(uint64_t xuid, XContentType content_type,
                                                         uint32_t title_id) {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }
  auto xuid_str = fmt::format("{:016X}", xuid);"""
ROOT_NEW = """std::filesystem::path ContentManager::ResolvePackageRoot(uint64_t xuid, XContentType content_type,
                                                         uint32_t title_id) {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }
  // HULK/NATIVE-PC-SAVE-ROOT: only saved games for this exact USA title.
  // Package name remains the original guest leaf; content files stay opaque.
  // No XUID/TITLE_ID/CONTENT_TYPE folders and no collisions with retail PC.
  if (auto native = hulk::native_save::WindowsRoot(
          title_id, uint32_t(content_type),
          REXCVAR_GET(hulk_native_pc_saves)); !native.empty()) {
    return native;
  }
  auto xuid_str = fmt::format("{:016X}", xuid);"""
HEADER_OLD = """std::filesystem::path ContentManager::ResolvePackageHeaderPath(const std::string_view file_name,
                                                               uint64_t xuid, uint32_t title_id,
                                                               XContentType content_type) const {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }

  if (content_type == XContentType::kMarketplaceContent) {"""
HEADER_NEW = """std::filesystem::path ContentManager::ResolvePackageHeaderPath(const std::string_view file_name,
                                                               uint64_t xuid, uint32_t title_id,
                                                               XContentType content_type) const {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }

  // HULK/NATIVE-PC-SAVE-HEADER: enumeration and reopen must resolve the
  // SAME isolated package namespace; never overwrite retail HULK.sav.
  if (auto native = hulk::native_save::WindowsRoot(
          title_id, uint32_t(content_type),
          REXCVAR_GET(hulk_native_pc_saves)); !native.empty()) {
    return native / "Headers" /
           rex::to_path(std::string(file_name) + ".header");
  }

  if (content_type == XContentType::kMarketplaceContent) {"""


def replace_once(s: str, old: str, new: str) -> str:
    count = s.count(old)
    if count != 1:
        raise ValueError(f"Unknown ReXGlue ContentManager source anchor ({count}): {old[:65]}")
    return s.replace(old, new, 1)


def patch_cpp(s: str) -> tuple[str, bool]:
    ready = all(part in s for part in
                (INCLUDE_NEW, NS_NEW, ROOT_NEW, HEADER_NEW))
    if ready:
        return s, False
    if any(part in s for part in
           (INCLUDE_NEW, NS_NEW, ROOT_NEW, HEADER_NEW)) or "HULK/NATIVE-PC-SAVE-" in s:
        raise ValueError("Partial native save root install")
    for old,new in ((INCLUDE_ANCHOR,INCLUDE_NEW),(NS_ANCHOR,NS_NEW),
                    (ROOT_OLD,ROOT_NEW),(HEADER_OLD,HEADER_NEW)):
        s = replace_once(s, old, new)
    return s, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdk_hulk_native_save_root.py SDK_ROOT")
    root = Path(sys.argv[1])
    source = root / SOURCE
    before = source.read_text(encoding="utf-8")
    after, changed = patch_cpp(before)
    helper = Path(__file__).resolve().parents[1]/"native_port/src/native_windows_save_root.h"
    output = root / HEADER_PATH
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(helper.read_text(encoding="utf-8"),encoding="utf-8",newline="\n")
    if changed:
        source.write_text(after,encoding="utf-8",newline="\n")
    print("Hulk title-scoped Windows saved-game storage:",
          "installed (disabled until explicitly enabled)" if changed
          else "already installed (disabled until explicitly enabled)")


if __name__=="__main__":
    main()
