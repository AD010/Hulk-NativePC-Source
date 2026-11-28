#!/usr/bin/env python3
"""Install a USA city-only runtime view of original QUICKDAT.ARC Levels.

Apply AFTER patch_sdk_hulk_world_stream_trace.py. No archive modification;
validate the complete original Levels record on every intersecting read.
"""
from pathlib import Path
import sys

INCLUDE = '#include <rex/filesystem/devices/host_path_file.h>\n'
INCLUDE_NEW = INCLUDE + '#include <rex/filesystem/hulk_city_distance.h>\n'
CVAR_ANCHOR = 'REXCVAR_DEFINE_BOOL(hulk_world_stream_trace, false, "Hulk",'
CVAR_NEW = '''REXCVAR_DEFINE_DOUBLE(hulk_city_far_plane, 6000.0, "Hulk/World",
                     "Original USA city camera far clip (2000 = retail)").range(2000.0, 10000.0);
REXCVAR_DEFINE_DOUBLE(hulk_city_zone_buffer, 300.0, "Hulk/World",
                     "Original USA city streaming buffer (50 = retail)").range(50.0, 750.0);
REXCVAR_DEFINE_DOUBLE(hulk_city_lod_multiplier, 3.0, "Hulk/World",
                     "Original city block and rooftop LODDistance multiplier").range(1.0, 5.0);

''' + CVAR_ANCHOR
READ = '  const bool ok = file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read);\n'
READ_NEW = READ + '''  // HULK/CITY-DISTANCE: the original indexed Levels record, not
  // shader fog guesses or global magic-number replacement.
  const auto far=static_cast<float>(REXCVAR_GET(hulk_city_far_plane));
  const auto zone=static_cast<float>(REXCVAR_GET(hulk_city_zone_buffer));
  const auto lod=static_cast<float>(REXCVAR_GET(hulk_city_lod_multiplier));
  if(ok && entry_ && out_bytes_read && *out_bytes_read<=buffer.size() &&
     (far!=hulk::city::kOriginalFarPlane || zone!=hulk::city::kOriginalZoneBuffer || lod!=1.0f) &&
     (hulk::city::IsTargetRead(byte_offset,*out_bytes_read) ||
      hulk::city::IsSkyFogRead(byte_offset,*out_bytes_read) ||
      hulk::city::FindLodRead(byte_offset,*out_bytes_read))) {
    std::string name=entry_->name();
    std::transform(name.begin(),name.end(),name.begin(),
                   [](unsigned char c){return static_cast<char>(std::toupper(c));});
    if(name=="QUICKDAT.ARC" && hulk::city::IsTargetRead(byte_offset,*out_bytes_read)) {
      std::array<uint8_t,hulk::city::kLevelsBytes> original{};
      size_t read=0;
      if(file_handle_->Read(hulk::city::kLevelsOffset,original.data(),original.size(),&read) &&
         read==original.size() &&
         hulk::city::ApplyToRead(buffer.first(*out_bytes_read),byte_offset,original,far,zone)) {
        static std::atomic<bool> logged{false};
        if(!logged.exchange(true,std::memory_order_relaxed))
          REXLOG_INFO("[HULK/CITY-DISTANCE] original USA Levels city far={} zone_buffer={} (runtime only)",far,zone);
      }
    } else if(name=="OBJECTS.ARC") {
      if(far!=hulk::city::kOriginalFarPlane &&
         hulk::city::IsSkyFogRead(byte_offset,*out_bytes_read)) {
        std::array<uint8_t,hulk::city::kSkyBytes> original{};
        size_t read=0;
        if(file_handle_->Read(hulk::city::kSkyOffset,original.data(),original.size(),&read) &&
           read==original.size() &&
           hulk::city::ApplySkyFogToRead(buffer.first(*out_bytes_read),byte_offset,original,far)) {
          static std::atomic<bool> logged{false};
          if(!logged.exchange(true,std::memory_order_relaxed))
            REXLOG_INFO("[HULK/CITY-DISTANCE] original SkyManager day/night FogFar={} (runtime only)",far);
        }
      }
      // A bulk read can contain more than one real prototype; patch each
      // separately rather than stopping at the first match.
      if(lod!=1.0f)for(size_t index=0;index<hulk::city::kLodRecords.size();++index) {
        const auto& record=hulk::city::kLodRecords[index];
        if(!hulk::city::Intersects(byte_offset,*out_bytes_read,record.offset+record.field))continue;
        std::array<uint8_t,hulk::city::kMaxLodRecordBytes> original{};
        size_t read=0;
        if(file_handle_->Read(record.offset,original.data(),record.size,&read) &&
           read==record.size &&
           hulk::city::ApplyLodToRead(buffer.first(*out_bytes_read),byte_offset,
                std::span<const uint8_t>(original.data(),record.size),record,lod)) {
          // Repeated streaming reads must not flood the game/render threads
          // with identical synchronous log work. Emit on first successful
          // patch of each original record, or on a changed multiplier.
          static std::array<std::atomic<uint32_t>,hulk::city::kLodRecords.size()> logged{};
          const auto scale_bits=std::bit_cast<uint32_t>(lod);
          if(logged[index].exchange(scale_bits,std::memory_order_relaxed)!=scale_bits)
            REXLOG_INFO("[HULK/CITY-LOD] original resource offset={} LODDistance={} -> {}",
                         record.offset,record.original_distance,record.original_distance*lod);
        }
      }
    }
  }
'''

def patch(source: str) -> tuple[str, bool]:
    if all(x in source for x in (INCLUDE_NEW, CVAR_NEW, READ_NEW)):
        return source, False
    if 'HULK/CITY-DISTANCE' in source or INCLUDE_NEW in source or CVAR_NEW in source:
        raise ValueError("Partial Hulk city distance patch")
    if any(source.count(x)!=1 for x in (INCLUDE, CVAR_ANCHOR, READ)):
        raise ValueError("Unknown Hulk host archive read path; apply world stream trace first")
    return source.replace(INCLUDE,INCLUDE_NEW).replace(CVAR_ANCHOR,CVAR_NEW).replace(READ,READ_NEW), True

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_sdk_hulk_city_distance.py SDK_ROOT")
    root=Path(sys.argv[1])
    cpp=root/"src/filesystem/devices/host_path_file.cpp"
    source=cpp.read_text(encoding="utf-8")
    patched,changed=patch(source)
    header=root/"include/rex/filesystem/hulk_city_distance.h"
    header.parent.mkdir(parents=True,exist_ok=True)
    template=Path(__file__).resolve().parents[1]/"native_port/src/native_city_distance.h"
    header.write_text(template.read_text(encoding="utf-8"),encoding="utf-8",newline="\n")
    if changed:cpp.write_text(patched,encoding="utf-8",newline="\n")
    print("Hulk original city camera/streaming distance:", "installed" if changed else "already installed")

if __name__=="__main__":
    main()
