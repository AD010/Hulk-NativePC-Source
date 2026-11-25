#pragma once
#include "eor_spirv_literal_string.h"
// Prove that the original scalar VS preserves the input clip-space X,Y,W.
// The shared half-pixel vector is exactly zero in the host native ABI.
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
namespace hulk::eor::aot {
inline bool ScalarPreservesClipXYW(std::span<const uint32_t> words) {
  if(words.size()<5 || words[0]!=0x07230203u)return false;
  enum Lane : uint8_t { Unknown=0, X,Y,Z,W,Zero,One };
  using Value=std::vector<Lane>;
  std::map<uint32_t,uint32_t> widths,constants,locations,storage,types;
  std::map<uint32_t,std::string> names;
  uint32_t position=0,input=0,shared=0,glsl=0,fp=0;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;
    if(!n || n>words.size()-i)return false;
    auto w=words.subspan(i,n);i+=n;
    if(op==5 || op==11) {
      if(n<3)return false;
      const auto name=DecodeTerminalSpirvString(w.subspan(2));
      if(!name)return false;
      if(op==5)names[w[1]]=*name;
      else if(*name=="GLSL.std.450")glsl=w[1];
    }
    else if(op==22 && n==3 && w[2]==32){fp=w[1];widths[fp]=1;}
    else if(op==23 && n==4 && w[2]==fp && w[3]<=4)widths[w[1]]=w[3];
    else if(op==32 && n==4)types[w[1]]=w[3];
    else if(op==43 && n==4)constants[w[2]]=w[3];
    else if(op==59 && n>=4){storage[w[2]]=w[3];types[w[2]]=w[1];}
    else if(op==71 && n==4 && w[2]==30)locations[w[1]]=w[3];
    else if(op==71 && n==4 && w[2]==11 && w[3]==0){if(position)return false;position=w[1];}
  }
  for(const auto& [id,slot]:locations)if(slot==0 && storage[id]==1 && names[id]=="in.var.ATTRIB0") {
    if(input)return false;
    input=id;
  }
  for(const auto& [id,nm]:names)if(nm=="HulkSharedConstants" && storage[id]==2){if(shared)return false;shared=id;}
  if(!input || !position || storage[position]!=3 || widths[types[types[input]]]!=4 || widths[types[types[position]]]!=4)return false;
  std::map<uint32_t,Value> values;
  std::map<uint32_t,uint32_t> shared_member;
  auto get=[&](uint32_t id)->Value {auto it=values.find(id);return it==values.end()?Value{}:it->second;};
  auto put=[&](uint32_t id,uint32_t type,Value v){const auto n=widths[type];if(n && n==v.size())values[id]=std::move(v);};
  unsigned position_stores=0;
  for(size_t i=5;i<words.size();) {
    const auto n=words[i]>>16,op=words[i]&65535u;auto w=words.subspan(i,n);i+=n;
    if(op==43 && n==4 && w[1]==fp)put(w[2],w[1],{w[3]==0u || w[3]==0x80000000u?Zero:w[3]==0x3f800000u?One:Unknown});
    else if(op==44 && n>=4) {Value v;for(size_t j=3;j<n;++j){auto a=get(w[j]);if(a.size()!=1){v.clear();break;}v.push_back(a[0]);}put(w[2],w[1],v);}
    else if(op==46 && n==3 && widths[w[1]])put(w[2],w[1],Value(widths[w[1]],Zero));
    else if((op==65 || op==66) && n==5 && w[3]==shared && shared && constants.contains(w[4]))shared_member[w[2]]=constants[w[4]];
    else if(op==61 && n>=4) {
      if(w[3]==input)put(w[2],w[1],{X,Y,Z,W});
      else if(shared_member.contains(w[3]) && shared_member[w[3]]==2 && widths[w[1]]==2)put(w[2],w[1],{Zero,Zero});
    } else if(op==12 && n==7 && w[3]==glsl && (w[4]==37 || w[4]==40 || w[4]==79 || w[4]==80) && w[5]==w[6])put(w[2],w[1],get(w[5]));
    else if(op==79 && n>=6){auto a=get(w[3]),b=get(w[4]);if(a.empty())continue;auto width=widths[w[1]];if(b.empty())b=Value(a.size(),Unknown);a.insert(a.end(),b.begin(),b.end());Value out;for(size_t j=5;j<n;++j)out.push_back(w[j]<a.size()?a[w[j]]:Unknown);if(out.size()==width)put(w[2],w[1],out);}
    else if(op==81 && n==5){auto a=get(w[3]);if(w[4]<a.size())put(w[2],w[1],{a[w[4]]});}
    else if(op==82 && n==6){auto a=get(w[3]),b=get(w[4]);if(a.size()==1 && w[5]<b.size()){b[w[5]]=a[0];put(w[2],w[1],b);}}
    else if(op==83 && n==4)put(w[2],w[1],get(w[3]));
    else if((op==129 || op==133 || op==142) && n==5){auto a=get(w[3]),b=get(w[4]);const auto width=widths[w[1]];if(a.empty())a=Value(width,Unknown);if(b.empty())b=Value(op==142?1:width,Unknown);if(op==142 && b.size()==1)b=Value(width,b[0]);if(a.size()!=width || b.size()!=width)continue;Value result(width,Unknown);for(size_t j=0;j<width;++j){if(op==129){if(a[j]==Zero)result[j]=b[j];else if(b[j]==Zero)result[j]=a[j];}else{if((a[j]==Zero && b[j]!=Unknown) || (b[j]==Zero && a[j]!=Unknown))result[j]=Zero;else if(a[j]==One)result[j]=b[j];else if(b[j]==One)result[j]=a[j];}}put(w[2],w[1],result);}
    else if(op==245 && n>=5 && (n&1u)){auto a=get(w[3]);if(a.empty())continue;for(size_t j=5;j<n;j+=2){auto b=get(w[j]);if(b.size()!=a.size()){a.clear();break;}for(size_t k=0;k<a.size();++k)if(a[k]!=b[k])a[k]=Unknown;}put(w[2],w[1],a);}
    else if(op==62 && n>=3 && w[1]==position){auto a=get(w[2]);if(++position_stores!=1 || a.size()!=4 || a[0]!=X || a[1]!=Y || a[3]!=W)return false;}
  }
  return position_stores==1;
}
}
