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
  if (native_slot && file_size < header_size) {
    std::array<uint8_t,hulk::save::kNativeMetadataMaximum> bytes{};
    const bool read_ok = file_size <= bytes.size() &&
        fread(bytes.data(), 1, static_cast<size_t>(file_size), file) == file_size;
    const bool closed = fclose(file) == 0;
    if (!read_ok || !closed) return X_ERROR_FILE_NOT_FOUND;
    const auto name = hulk::save::DecodeNativeMetadata(bytes.data(), static_cast<size_t>(file_size));
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
  if (native_slot && file_size != header_size && file_size != header_size + sizeof(uint32_t)) {
    fclose(file);
    return X_ERROR_FILE_NOT_FOUND;
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
