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

XAM_SOURCE='src/kernel/xam/xam_content.cpp'
XAM_INCLUDE="#include <rex/cvar.h>\n"
XAM_INCLUDE_NEW="#include <rex/cvar.h>\n#include <rex/system/xam/hulk_native_save_root.h>\n#include <rex/system/xam/hulk_native_save_metadata.h>\n"
XAM_ENUM="    // Also enumerate common content (xuid=0)\n    if (xuid != 0) {"
XAM_ENUM_NEW="    // HULK/NATIVE-SAVE-ENUM: the native Documents root is shared\n    // across the compatibility XUID arguments. Enumerate it just once;\n    // common DLC and other titles retain their existing behavior.\n    const bool hulk_single_root = !hulk::native_save::WindowsRoot(\n        REX_KERNEL_STATE()->title_id(), uint32_t(content_type),\n        rex::cvar::Query<bool>(\"hulk_native_pc_saves\")).empty();\n    if (xuid != 0 && !hulk_single_root) {"

XAM_CREATE="      if (result == X_ERROR_SUCCESS) {\n        content_manager->WriteContentHeaderFile(xuid, content_data);\n      }"
XAM_CREATE_NEW="      if (result == X_ERROR_SUCCESS) {\n        const auto header_result = content_manager->WriteContentHeaderFile(xuid, content_data);\n        // HULK/NATIVE-SAVE-CREATE-RESULT: a Windows save cannot be\n        // reported as created if metadata persistence failed. Unmount\n        // without deleting staged files or the previous recovery copies.\n        const uint32_t title = content_data.title_id == kCurrentlyRunningTitleId\n            ? REX_KERNEL_STATE()->title_id() : uint32_t(content_data.title_id);\n        if (header_result != X_ERROR_SUCCESS &&\n            hulk::save::IsHulkSaveSlot(content_data.file_name()) &&\n            !hulk::native_save::WindowsRoot(title, uint32_t(content_data.content_type),\n                rex::cvar::Query<bool>(\"hulk_native_pc_saves\")).empty()) {\n          content_manager->CloseContent(root_name);\n          result = header_result;\n          disposition = kDispositionState::Unknown;\n        }\n      }"

def patch_xam(source):
    if all(part in source for part in (XAM_INCLUDE_NEW,XAM_ENUM_NEW,XAM_CREATE_NEW)):
        return source,False
    if XAM_INCLUDE_NEW in source or 'HULK/NATIVE-SAVE-ENUM' in source or 'HULK/NATIVE-SAVE-CREATE-RESULT' in source:
        raise ValueError('Partial native save enumeration patch')
    for old,new in ((XAM_INCLUDE,XAM_INCLUDE_NEW),(XAM_ENUM,XAM_ENUM_NEW),(XAM_CREATE,XAM_CREATE_NEW)):
        if source.count(old)!=1:raise ValueError('Native save enumeration anchor drift')
        source=source.replace(old,new,1)
    return source,True

BACKUP_ANCHOR="static const char* kThumbnailFileName = \"__thumbnail.png\";"
BACKUP_HELPER="// HULK/NATIVE-SAVE-BACKUP: preserve the previous complete host file before\n// a destructive XContent disposition. This is a recovery copy, not a\n// conversion or an atomic replacement of the guest's live payload.\nstatic bool HulkBackupSaveFile(const std::filesystem::path& from,\n                               const std::filesystem::path& to,\n                               uintmax_t maximum, bool progress = false) {\n  namespace fs = std::filesystem;\n  try {\n    std::error_code ec;\n    const auto status = fs::symlink_status(from, ec);\n    if (ec && ec != std::errc::no_such_file_or_directory) return false;\n    if (!fs::exists(status)) return true;\n    if (!fs::is_regular_file(status)) return false;\n    const auto size = fs::file_size(from, ec);\n    if (ec || size > maximum) return false;\n    // Empty files carry no recoverable progress.\n    if (!size) return true;\n    std::ifstream in(from, std::ios::binary);\n    if (!in) return false;\n    std::vector<uint8_t> bytes(static_cast<size_t>(size));\n    if (!in.read(reinterpret_cast<char*>(bytes.data()),\n                 static_cast<std::streamsize>(bytes.size()))) return false;\n    if (in.peek() != std::char_traits<char>::eof() || in.bad()) return false;\n    if (progress && !hulk::save::ValidLegacyProgress(bytes.data(), bytes.size())) return false;\n    return hulk::save::CommitMetadata(to, bytes.data(), bytes.size());\n  } catch (...) {\n    return false;\n  }\n}\n\n"
DELETE_ANCHOR="  auto dir_removed = std::filesystem::remove_all(package_path, ec);"
DELETE_GUARD="  // HULK/NATIVE-SAVE-PRESERVE: CREATE_ALWAYS and TRUNCATE_EXISTING\n  // call DeleteContent before the guest writes replacement progress.\n  // Save a durable copy outside the package tree before either delete.\n  if (hulk::save::IsHulkSaveSlot(data.file_name()) &&\n      !hulk::native_save::WindowsRoot(\n          data.title_id == kCurrentlyRunningTitleId ? kernel_state_->title_id() : uint32_t(data.title_id),\n          uint32_t(data.content_type), REXCVAR_GET(hulk_native_pc_saves)).empty()) {\n    const auto backup_header = ResolvePackageHeaderPath(\n        data.file_name(), xuid, data.title_id, data.content_type);\n    auto payload_backup = backup_header;\n    payload_backup += \".payload-backup\";\n    auto metadata_backup = backup_header;\n    metadata_backup += \".metadata-backup\";\n    if (!HulkBackupSaveFile(package_path / \"HULK.SAV\", payload_backup, hulk::save::kLegacyProgressMaximum, true) ||\n        !HulkBackupSaveFile(backup_header, metadata_backup, 332u))\n      return X_ERROR_ACCESS_DENIED;\n  }\n"

def patch_backup(source):
    if BACKUP_HELPER in source and source.count(DELETE_GUARD+DELETE_ANCHOR)==2:
        return source,False
    if 'HULK/NATIVE-SAVE-BACKUP' in source or 'HULK/NATIVE-SAVE-PRESERVE' in source:
        raise ValueError('Partial native save backup patch')
    if source.count(BACKUP_ANCHOR)!=1 or source.count(DELETE_ANCHOR)!=2:
        raise ValueError('Native save destructive disposition anchor drift')
    return source.replace(BACKUP_ANCHOR,BACKUP_HELPER+BACKUP_ANCHOR,1).replace(
        DELETE_ANCHOR,DELETE_GUARD+DELETE_ANCHOR),True

FLUSH_HEADER='include/rex/system/xam/content_manager.h'
FLUSH_XAM="u32 XamContentFlush_entry(mapped_string root_name, mapped_void overlapped_ptr) {\n  X_RESULT result = X_ERROR_SUCCESS;"
FLUSH_XAM_NEW="u32 XamContentFlush_entry(mapped_string root_name, mapped_void overlapped_ptr) {\n  X_RESULT result = REX_KERNEL_STATE()->content_manager()->FlushNativeHulkContent(root_name.value());"
FLUSH_CPP="X_RESULT ContentManager::CloseContent(const std::string_view root_name) {"
FLUSH_CPP_NEW="// HULK/NATIVE-SAVE-FLUSH: the existing compatibility content API can\n// now flush the actual Windows file rather than report unconditional\n// success. Preserve the historical no-op for non-native content.\nX_RESULT ContentManager::FlushNativeHulkContent(const std::string_view root_name) {\n  try {\n    auto lock = global_critical_region_.Acquire();\n    const auto it = open_packages_.find(string::string_key_case(root_name));\n    if (it == open_packages_.end()) return X_ERROR_SUCCESS;\n    const auto& data = it->second->GetPackageContentData();\n    if (!hulk::save::IsHulkSaveSlot(data.file_name())) return X_ERROR_SUCCESS;\n    const uint32_t title = data.title_id == kCurrentlyRunningTitleId\n        ? kernel_state_->title_id() : uint32_t(data.title_id);\n    const auto root = hulk::native_save::WindowsRoot(\n        title, uint32_t(data.content_type), REXCVAR_GET(hulk_native_pc_saves));\n    if (root.empty()) return X_ERROR_SUCCESS;\n    const auto& package = it->second->package_path();\n    if (package.parent_path() != root ||\n        !hulk::save::IsHulkSaveSlot(rex::path_to_utf8(package.filename())))\n      return X_ERROR_SUCCESS;\n    const auto payload = package / \"HULK.SAV\";\n    std::error_code ec;\n    const auto status = std::filesystem::symlink_status(payload, ec);\n    if (ec || !std::filesystem::is_regular_file(status))\n      return X_ERROR_ACCESS_DENIED;\n    FILE* file = hulk::save::OpenCommitFile(payload, false);\n    if (!file) return X_ERROR_ACCESS_DENIED;\n    const bool flushed = hulk::save::FlushCommitFile(file);\n    const bool closed = std::fclose(file) == 0;\n    return flushed && closed ? X_ERROR_SUCCESS : X_ERROR_ACCESS_DENIED;\n  } catch (...) {\n    return X_ERROR_ACCESS_DENIED;\n  }\n}\n\nX_RESULT ContentManager::CloseContent(const std::string_view root_name) {"
FLUSH_DECL="  X_RESULT CloseContent(const std::string_view root_name);"
FLUSH_DECL_NEW="  X_RESULT FlushNativeHulkContent(const std::string_view root_name);\n  X_RESULT CloseContent(const std::string_view root_name);"

def patch_flush(header,source,xam):
    pairs=((header,FLUSH_DECL,FLUSH_DECL_NEW),(source,FLUSH_CPP,FLUSH_CPP_NEW),
           (xam,FLUSH_XAM,FLUSH_XAM_NEW))
    if all(s.count(new)==1 for s,old,new in pairs):return header,source,xam,False
    if any(new in s for s,old,new in pairs):raise ValueError('Partial native save flush patch')
    if any(s.count(old)!=1 for s,old,new in pairs):raise ValueError('Native save flush anchor drift')
    return (*[s.replace(old,new,1) for s,old,new in pairs],True)

def main():
    root=Path(sys.argv[1]);path=root/SOURCE
    before=path.read_text(encoding='utf-8');after,changed=patch_cpp(before)
    after,backup_changed=patch_backup(after)
    changed=changed or backup_changed
    xam_path=root/XAM_SOURCE
    xam_before=xam_path.read_text(encoding='utf-8')
    xam_after,xam_changed=patch_xam(xam_before)
    if patch_xam(xam_after)!=(xam_after,False):raise ValueError('Native save enum not idempotent')
    hp=root/FLUSH_HEADER
    header=hp.read_text(encoding='utf-8')
    new_header,after,xam_after,flush_changed=patch_flush(header,after,xam_after)
    if patch_flush(new_header,after,xam_after)!=(new_header,after,xam_after,False):
        raise ValueError('Native save flush not idempotent')
    changed=changed or flush_changed
    xam_changed=xam_changed or flush_changed
    helper=Path(__file__).resolve().parents[1]/'native_port/src/native_save_metadata.h'
    dest=root/'include/rex/system/xam/hulk_native_save_metadata.h'
    dest.write_text(helper.read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
    if flush_changed:hp.write_text(new_header,encoding='utf-8',newline='\n')
    if changed:path.write_text(after,encoding='utf-8',newline='\n')
    if xam_changed:xam_path.write_text(xam_after,encoding='utf-8',newline='\n')
    print('Hulk native save metadata:', 'installed' if changed else 'verified')
if __name__=='__main__':main()
