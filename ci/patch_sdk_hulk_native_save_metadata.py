#!/usr/bin/env python3
"""Install native metadata for the known Hulk save slot, after root/name patches."""
from pathlib import Path
import sys
SOURCE='src/system/xam/content_manager.cpp'
INCLUDE='#include <rex/system/xam/hulk_native_save_metadata.h>\n'
WRITE='  auto parent_path = header_path.parent_path();'
WRITE_NEW='''  // HULK/NATIVE-METADATA-WRITE: no persistent XUID or device identifier.
  if (hulk::save::IsHulkSaveSlot(data.file_name()) &&
      !hulk::native_save::WindowsRoot(data.title_id, uint32_t(data.content_type),
                                     REXCVAR_GET(hulk_native_pc_saves)).empty()) {
    if (license_mask != 0) return X_ERROR_ACCESS_DENIED;
    const auto native = hulk::save::EncodeNativeMetadata(data.display_name());
    return native && hulk::save::CommitMetadata(header_path, native->data(), native->size())
        ? X_ERROR_SUCCESS : X_ERROR_ACCESS_DENIED;
  }
''' + WRITE
READ='  if (file_size < header_size) {'
READ_NEW='''  // HULK/NATIVE-METADATA-READ: project native metadata into the legacy ABI
  // in memory only. Existing 328-byte imported headers remain readable.
  const bool native_slot = hulk::save::IsHulkSaveSlot(file_name) &&
      !hulk::native_save::WindowsRoot(
          title_id == kCurrentlyRunningTitleId ? kernel_state_->title_id() : title_id,
          uint32_t(content_type), REXCVAR_GET(hulk_native_pc_saves)).empty();
  if (native_slot) {
    // Read the known slot through a single bounded stream. Never return
    // persisted XUID/device/title values from a legacy header.
    static_assert(header_size == 328, "Unexpected Hulk legacy header layout");
    std::array<uint8_t,header_size> bytes{};
    if (file_size > bytes.size() || file_size < 14) {
      fclose(file);
      return X_ERROR_FILE_NOT_FOUND;
    }
    const auto n = static_cast<size_t>(file_size);
    const bool read_ok = fread(bytes.data(), 1, n, file) == n;
    const bool exact = fgetc(file) == EOF && !ferror(file);
    const bool closed = fclose(file) == 0;
    if (!read_ok || !exact || !closed) return X_ERROR_FILE_NOT_FOUND;
    std::optional<std::u16string> name;
    if (n == header_size) {
      // Legacy aggregate: content type at +4, UTF-16BE display name at
      // +8, 42-byte slot name at +264. Ignore old profile identifiers.
      if (bytes[4] || bytes[5] || bytes[6] || bytes[7] != 1)
        return X_ERROR_FILE_NOT_FOUND;
      const auto* leaf = reinterpret_cast<const char*>(bytes.data() + 264);
      size_t leaf_size = 0;
      while (leaf_size < 42 && leaf[leaf_size]) ++leaf_size;
      if (!hulk::save::IsHulkSaveSlot(std::string_view(leaf, leaf_size)))
        return X_ERROR_FILE_NOT_FOUND;
      std::u16string decoded;
      bool terminated = false;
      for (size_t i = 8; i < 264; i += 2) {
        const auto c = static_cast<char16_t>((uint16_t(bytes[i]) << 8) | bytes[i+1]);
        if (!c) { terminated = true; break; }
        decoded.push_back(c);
      }
      if (!terminated || !hulk::save::ValidMetadataName(decoded))
        return X_ERROR_FILE_NOT_FOUND;
      name = std::move(decoded);
    } else {
      name = hulk::save::DecodeNativeMetadata(bytes.data(), n);
    }
    if (!name) return X_ERROR_FILE_NOT_FOUND;
    data = XCONTENT_AGGREGATE_DATA{};
    data.device_id = static_cast<uint32_t>(DummyDeviceId::HDD);
    data.content_type = content_type;
    data.title_id = title_id == kCurrentlyRunningTitleId ? kernel_state_->title_id() : title_id;
    data.xuid = xuid;
    data.set_file_name(file_name);
    data.set_display_name(*name);
    return X_ERROR_SUCCESS;
  }
''' + READ
LIST='''      result.emplace_back(std::move(content_data));
    } else {
      content_data.device_id = device_id;'''
LIST_NEW='''      // HULK/NATIVE-METADATA-LIST: use the current caller's device selection.
      if (hulk_native_root && hulk::save::IsHulkSaveSlot(rex::path_to_utf8(file_info.name)))
        content_data.device_id = device_id;
      result.emplace_back(std::move(content_data));
    } else {
      if (hulk_native_root && hulk::save::IsHulkSaveSlot(rex::path_to_utf8(file_info.name))) {
        // Do not invent a save entry for corrupt metadata. A missing header
        // is still supported by the old directory-only fallback.
        std::error_code header_error;
        const auto header = ResolvePackageHeaderPath(rex::path_to_utf8(file_info.name),
                                                     xuid, title_id, content_type);
        if (std::filesystem::exists(header, header_error) || header_error) continue;
      }
      content_data.device_id = device_id;'''
PAIRS=((WRITE,WRITE_NEW),(READ,READ_NEW),(LIST,LIST_NEW))

def patch_cpp(source):
    if 'HULK/NATIVE-PC-SAVE-ROOT' not in source or 'HULK/SAVE-COMMIT' not in source:
        raise ValueError('Native root and transaction patches must be installed first')
    if INCLUDE in source and all(new in source for _,new in PAIRS):return source,False
    if INCLUDE in source or 'HULK/NATIVE-METADATA-' in source:raise ValueError('Partial native metadata patch')
    for old,new in PAIRS:
        if source.count(old)!=1:raise ValueError('Native metadata anchor drift: '+old[:80])
        source=source.replace(old,new,1)
    return INCLUDE+source,True

def main():
    root=Path(sys.argv[1]);path=root/SOURCE
    before=path.read_text(encoding='utf-8');after,changed=patch_cpp(before)
    helper=Path(__file__).resolve().parents[1]/'native_port/src/native_save_metadata.h'
    dest=root/'include/rex/system/xam/hulk_native_save_metadata.h'
    dest.write_text(helper.read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
    if changed:path.write_text(after,encoding='utf-8',newline='\n')
    print('Hulk native save metadata:', 'installed' if changed else 'verified')
if __name__=='__main__':main()
