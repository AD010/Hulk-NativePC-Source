#pragma once
// Platform-neutral indexed field storage. Opaque source sections are retained
// for lossless import/export; their semantic meanings are not yet established.
#include "native_pc_save_known_tables.h"
#include "native_save_commit.h"
#include <fstream>
#include <cstring>

namespace hulk::save::format {
inline constexpr size_t kProgressStoreMaximum=known_detail::maximum+128u;
inline std::vector<std::byte> EncodeNativeProgress(const KnownTables& data) {
  using namespace known_detail;
  // Reject a modified schema or inconsistent opaque layout before publishing.
  (void)EncodeKnownTables(data);
  std::vector<std::byte> out(24);
  constexpr char magic[]="HULKPRG1";
  std::memcpy(out.data(),magic,8);
  constexpr auto le=Kind::RetailPcLittleEndian;
  Put(out,8,1,le);
  Put(out,12,data.kind==Kind::XboxBigEndian?1u:2u,le);
  const auto append=[&](uint32_t v){const auto p=out.size();out.resize(p+4);Put(out,p,v,le);};
  for(size_t i=0;i<data.tables.size();++i) {
    append(static_cast<uint32_t>(data.tables[i].size()));
    for(const auto& field:data.tables[i]){append(field.id);append(field.raw_value);}
  }
  for(const auto& bytes:data.opaque){append(static_cast<uint32_t>(bytes.size()));out.insert(out.end(),bytes.begin(),bytes.end());}
  if(out.size()>kProgressStoreMaximum)throw std::invalid_argument("Native Hulk progress too large");
  Put(out,16,static_cast<uint32_t>(out.size()),le);
  // Checksum protects platform marker, payload size, and the entire body.
  // Its own word is excluded by computing with the word initially zero.
  Put(out,20,Crc(out),le);
  return out;
}
inline KnownTables DecodeNativeProgress(std::span<const std::byte> bytes) {
  using namespace known_detail;
  constexpr auto le=Kind::RetailPcLittleEndian;
  if(bytes.size()<24||bytes.size()>kProgressStoreMaximum||std::memcmp(bytes.data(),"HULKPRG1",8)!=0||
      Word(bytes,8,le)!=1||Word(bytes,16,le)!=bytes.size())
    throw std::invalid_argument("Unknown native Hulk progress format");
  const auto source=Word(bytes,12,le);
  if(source!=1&&source!=2)throw std::invalid_argument("Unknown Hulk progress origin");
  auto crc_copy=std::vector<std::byte>(bytes.begin(),bytes.end());
  const auto expected=Word(bytes,20,le);
  Put(crc_copy,20,0,le);
  if(Crc(crc_copy)!=expected)throw std::invalid_argument("Native Hulk progress CRC mismatch");
  KnownTables data;data.kind=source==1?Kind::XboxBigEndian:Kind::RetailPcLittleEndian;
  size_t pos=24;
  const auto next=[&](){const auto value=Word(bytes,pos,le);pos+=4;return value;};
  for(size_t i=0;i<data.tables.size();++i){
    const auto count=next();if(count!=counts[i])throw std::invalid_argument("Native Hulk progress table count mismatch");
    auto& table=data.tables[i];table.reserve(count);
    for(uint32_t j=0;j<count;++j){const auto id=next();const auto value=next();table.push_back({id,value});}
  }
  for(auto& opaque:data.opaque){const auto size=next();if(size>bytes.size()-pos)throw std::invalid_argument("Native Hulk progress section truncated");opaque.assign(bytes.begin()+pos,bytes.begin()+pos+size);pos+=size;}
  if(pos!=bytes.size())throw std::invalid_argument("Native Hulk progress trailing bytes");
  (void)EncodeKnownTables(data);
  return data;
}
// These host file APIs do not require a guest mount, XUID, XAM, or ReXGlue.
// Caller chooses a new NativePC path, never the original retail HULK.sav.
inline bool StoreNativeProgress(const std::filesystem::path& path,const KnownTables& data) {
  if(path.extension()!=".hpr")return false; // Never replace a retail or guest HULK.SAV.
  const auto bytes=EncodeNativeProgress(data);
  return hulk::save::CommitMetadata(path,bytes.data(),bytes.size());
}
inline KnownTables LoadNativeProgress(const std::filesystem::path& path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  if(!in)throw std::runtime_error("Cannot open native Hulk progress");
  const auto size=in.tellg();
  if(size<0||static_cast<uint64_t>(size)>kProgressStoreMaximum)throw std::invalid_argument("Native Hulk progress outside size bounds");
  std::vector<std::byte> bytes(static_cast<size_t>(size));
  in.seekg(0);
  if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))||in.peek()!=std::char_traits<char>::eof()||in.bad())
    throw std::runtime_error("Cannot read complete native Hulk progress");
  return DecodeNativeProgress(bytes);
}
enum class NativeMirrorResult { Unchanged, Stored, Unsupported, IoFailure };
// Use only after the source save has completed its normal host flush.
// Never substitute an older mirror for the live guest file automatically.
inline NativeMirrorResult MirrorOriginalProgress(const std::filesystem::path& source,
    const std::filesystem::path& target,Kind kind) noexcept {
  try {
    if(target.extension()!=".hpr"||source==target)return NativeMirrorResult::IoFailure;
    std::ifstream input(source,std::ios::binary|std::ios::ate);
    if(!input)return NativeMirrorResult::IoFailure;
    const auto length=input.tellg();
    if(length<0||static_cast<uint64_t>(length)>known_detail::maximum)
      return NativeMirrorResult::Unsupported;
    std::vector<std::byte> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(length))||
        input.peek()!=std::char_traits<char>::eof()||input.bad())
      return NativeMirrorResult::IoFailure;
    KnownTables data;
    try {data=DecodeKnownTables(bytes,kind);}
    catch(const std::invalid_argument&){return NativeMirrorResult::Unsupported;}
    // Avoid accumulating another durable transaction for unchanged saves.
    try {
      if(EncodeKnownTables(LoadNativeProgress(target))==bytes)
        return NativeMirrorResult::Unchanged;
    } catch(...) {} // Missing/invalid old mirror never authorizes overwriting the source.
    return StoreNativeProgress(target,data)?NativeMirrorResult::Stored:NativeMirrorResult::IoFailure;
  } catch(...) {return NativeMirrorResult::IoFailure;}
}
}
