#!/usr/bin/env python3
"""Bound XContent names and reject Windows path escapes before file operations.

Protects existing saves. This does not change save payloads or migrate folders.
"""
from pathlib import Path
import re
import sys

HEADER = 'include/rex/system/xam/content_manager.h'
SOURCE = 'src/system/xam/content_manager.cpp'
OLD_NAME = '''    value.assign(file_name_raw, std::min(strlen(file_name_raw), countof(file_name_raw)));'''
NEW_NAME = '''    // HULK/SAVE-NAME-BOUND: a full 42-byte guest name has no terminator.
    // strlen before min still reads beyond the guest field and its padding.
    const auto end = std::find(file_name_raw, file_name_raw + countof(file_name_raw), '\\0');
    value.assign(file_name_raw, end);'''
HELPER = r'''// HULK/SAVE-LEAF: every package name is ONE Windows path component.
// Reject rather than sanitize: two distinct saves must never alias silently.
bool HulkSafeContentLeaf(std::string_view name) {
  if (name.empty() || name.size() > 42 || name == "." || name == ".." ||
      name.back() == '.' || name.back() == ' ') return false;
  for (unsigned char c : name) {
    if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
  }
  auto stem = name.substr(0, name.find('.'));
  // Win32 also treats spaces before a device-name extension as aliases.
  while (!stem.empty() && stem.back() == ' ') stem.remove_suffix(1);
  std::string upper(stem);
  for (char& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
      upper == "CONIN$" || upper == "CONOUT$") return false;
  if (upper.size() == 4 && (upper.substr(0,3) == "COM" || upper.substr(0,3) == "LPT") &&
      upper[3] >= '0' && upper[3] <= '9') return false;
  // Windows recognizes superscript 1/2/3 in COM/LPT device names too (UTF-8).
  if (upper.size() == 5 && (upper.substr(0,3) == "COM" || upper.substr(0,3) == "LPT") &&
      uint8_t(upper[3]) == 0xC2 && (uint8_t(upper[4]) == 0xB9 ||
      uint8_t(upper[4]) == 0xB2 || uint8_t(upper[4]) == 0xB3)) return false;
  return true;
}

'''
GUARDS = {
    'ResolvePackage': ('data.file_name()', 'nullptr'),
    'ContentExists': ('data.file_name()', 'false'),
    'WriteContentHeaderFile': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'ReadContentHeaderFile': ('file_name', 'X_ERROR_INVALID_NAME'),
    'CreateContent': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'OpenContent': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'GetContentThumbnail': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'SetContentThumbnail': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'DeleteContent': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
    'UnmountAndDeleteContent': ('data.file_name()', 'X_ERROR_INVALID_NAME'),
}

def patch(header, source):
    done = NEW_NAME in header and HELPER in source
    if done:
        for method, (name, error) in GUARDS.items():
            match = re.search(r'ContentManager::'+method+r'\([^\{]+\{\n', source)
            expected = f'  if (!HulkSafeContentLeaf({name})) return {error};\n'
            if not match or not source[match.end():].startswith(expected):
                raise ValueError('Partial content-name guard: '+method)
        return header, source, False
    if 'HULK/SAVE-NAME-BOUND' in header or 'HULK/SAVE-LEAF' in source:
        raise ValueError('Partial save name patch')
    if header.count(OLD_NAME) != 1 or source.count('static const char* kThumbnailFileName') != 1:
        raise ValueError('Content manager source drift')
    header = header.replace(OLD_NAME, NEW_NAME, 1)
    if '#include <algorithm>' not in header:
        header = '#include <algorithm>\n' + header
    source = source.replace('static const char* kThumbnailFileName', HELPER+'static const char* kThumbnailFileName', 1)
    for method, (name, error) in GUARDS.items():
        pattern = r'(ContentManager::'+method+r'\([^\{]+\{\n)'
        source, n = re.subn(pattern, lambda m:m[0]+f'  if (!HulkSafeContentLeaf({name})) return {error};\n', source)
        if n != 1: raise ValueError('Content method anchor drift: '+method)
    return header, source, True


COMMIT_OLD = """  rex::filesystem::CreateEmptyFile(header_path);

  auto file = rex::filesystem::OpenFile(header_path, "wb");
  if (!file) {
    return X_ERROR_FILE_NOT_FOUND;
  }
  fwrite(&data, 1, sizeof(XCONTENT_AGGREGATE_DATA), file);
  if (license_mask != 0) {
    fwrite(&license_mask, 1, sizeof(license_mask), file);
  }
  fclose(file);
  return X_ERROR_SUCCESS;"""
COMMIT_NEW = """  // HULK/SAVE-COMMIT: preserve the prior complete metadata on write failure.
  std::array<uint8_t, sizeof(XCONTENT_AGGREGATE_DATA) + sizeof(license_mask)> bytes{};
  std::memcpy(bytes.data(), &data, sizeof(data));
  size_t size = sizeof(data);
  if (license_mask != 0) {
    std::memcpy(bytes.data() + size, &license_mask, sizeof(license_mask));
    size += sizeof(license_mask);
  }
  return hulk::save::CommitMetadata(header_path, bytes.data(), size)
      ? X_ERROR_SUCCESS : X_ERROR_ACCESS_DENIED;"""
COMMIT_INCLUDE = '#include <rex/system/xam/hulk_save_commit.h>\n'

def patch_commit(source):
    if COMMIT_NEW in source and COMMIT_INCLUDE in source:
        return source, False
    if 'HULK/SAVE-COMMIT' in source or COMMIT_INCLUDE in source:
        raise ValueError('Partial atomic save metadata patch')
    if source.count(COMMIT_OLD) != 1:
        raise ValueError('Content header writer anchor drift')
    return COMMIT_INCLUDE + source.replace(COMMIT_OLD, COMMIT_NEW, 1), True


def main():
    root=Path(sys.argv[1]); hp=root/HEADER; sp=root/SOURCE
    h,s,changed=patch(hp.read_text(),sp.read_text())
    s,commit_changed=patch_commit(s)
    helper=Path(__file__).resolve().parents[1]/'native_port/src/native_save_commit.h'
    destination=root/'include/rex/system/xam/hulk_save_commit.h'
    destination.write_text(helper.read_text(),encoding='utf-8',newline='\n')
    changed = changed or commit_changed
    if changed:
        hp.write_text(h,encoding='utf-8',newline='\n')
        sp.write_text(s,encoding='utf-8',newline='\n')
    print('Hulk save-name bounds and Windows path guards:', 'patched' if changed else 'already patched')

if __name__=='__main__': main()
