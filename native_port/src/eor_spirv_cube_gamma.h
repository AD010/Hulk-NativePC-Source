#pragma once
#include "eor_spirv_literal_string.h"
#include <initializer_list>
// Post-filter Xbox piecewise gamma conversion, specialized by authenticated live cube slots.
// The original bank remains immutable. No texture re-encoding or sRGB approximation.
#include <bit>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace hulk::eor::aot {
inline std::vector<uint32_t> LowerOriginalCubeGamma(std::span<const uint32_t> src,uint16_t mask) {
  if(!mask)return {src.begin(),src.end()};
  auto fail=[](){throw std::invalid_argument("Unsupported original cube gamma SPIR-V ABI");};
  if(src.size()<5 || src[0]!=0x07230203u || !src[3] || src[3]>0x1000000u)fail();
  std::map<uint32_t,uint32_t> cubes,loads,sampled,vector_types;
  uint32_t fp=0,boolean=0,glsl=0;size_t function=0;uint16_t declared=0,converted=0;
  for(size_t i=5;i<src.size();) {
    const uint32_t n=src[i]>>16,op=src[i]&65535u;if(!n || n>src.size()-i)fail();
    auto w=src.subspan(i,n);
    if(op==5 || op==11){
      if(n<3)fail();
      const auto literal=DecodeTerminalSpirvString(w.subspan(2));
      if(!literal)fail();
      if(op==11) {if(*literal=="GLSL.std.450")glsl=w[1];}
      else {const auto& s=*literal;constexpr auto prefix="HulkTextureCube_s";if(s.starts_with(prefix)){
      auto suffix=s.substr(17);if(suffix.empty() || suffix.size()>2 || suffix.find_first_not_of("0123456789")!=std::string::npos)fail();
      unsigned slot=std::stoul(suffix);if(slot>=16)fail();cubes[w[1]]=slot;declared|=uint16_t(1u<<slot);
    }}}
    else if(op==20 && n==2)boolean=w[1];
    else if(op==22 && n==3 && w[2]==32)fp=w[1];
    else if(op==23 && n==4 && w[3]==4)vector_types[w[1]]=w[2];
    else if(op==54 && !function)function=i;
    else if(op==61 && n>=4)loads[w[2]]=w[3];
    else if(op==86 && n==5)sampled[w[2]]=w[3];
    i+=n;
  }
  if(!fp || !boolean || !glsl || !function || (mask&~declared))fail();
  std::vector<uint32_t> out(src.begin(),src.begin()+function);uint32_t next=src[3];
  auto emit=[&](uint32_t op,std::initializer_list<uint32_t> args){out.push_back(uint32_t((args.size()+1)<<16)|op);out.insert(out.end(),args);};
  auto constant=[&](float v){uint32_t id=next++;emit(43,{fp,id,std::bit_cast<uint32_t>(v)});return id;};
  const auto zero=constant(0),one=constant(1),c255=constant(255),c64=constant(64),c96=constant(96),c192=constant(192),two=constant(2),four=constant(4),eight=constant(8),c256=constant(256),c1024=constant(1024),inv1024=constant(1.f/1024),inv1023=constant(1.f/1023);
  auto binary=[&](uint32_t op,uint32_t type,uint32_t a,uint32_t b){uint32_t id=next++;emit(op,{type,id,a,b});return id;};
  auto select=[&](uint32_t cond,uint32_t a,uint32_t b){uint32_t id=next++;emit(169,{fp,id,cond,a,b});return id;};
  for(size_t i=function;i<src.size();) {
    const auto n=src[i]>>16,op=src[i]&65535u;auto w=src.subspan(i,n);i+=n;
    bool gamma=false;uint32_t slot=0;
    if(op>=87 && op<=97 && n>=4){auto s=sampled.find(w[3]);if(s!=sampled.end()){auto l=loads.find(s->second);if(l!=loads.end()){auto c=cubes.find(l->second);if(c!=cubes.end()){slot=c->second;gamma=(mask&(1u<<slot))!=0;}}}}
    if(!gamma){out.insert(out.end(),w.begin(),w.end());continue;}
    if(op!=87 || !vector_types.contains(w[1]) || vector_types.at(w[1])!=fp)fail();
    converted|=uint16_t(1u<<slot);const uint32_t raw=next++,type=w[1],original=w[2];
    auto start=out.size();out.insert(out.end(),w.begin(),w.end());out[start+2]=raw;
    uint32_t components[4]{};
    for(unsigned c=0;c<4;++c){const uint32_t val=next++;emit(81,{fp,val,raw,c});if(c==3){components[c]=val;continue;}
      uint32_t clamp=next++;emit(12,{fp,clamp,glsl,43,val,zero,one});
      const auto x=binary(133,fp,clamp,c255);
      const auto p=binary(190,boolean,x,c64),q=binary(190,boolean,x,c96),r=binary(190,boolean,x,c192);
      const auto mul=select(r,eight,select(q,four,select(p,two,one)));
      const auto offset=select(r,c1024,select(q,c256,select(p,c64,zero)));
      const auto lin=binary(131,fp,binary(133,fp,x,mul),offset);
      const auto frac=binary(133,fp,binary(133,fp,lin,mul),inv1024);
      const uint32_t truncated=next++;emit(12,{fp,truncated,glsl,3,frac});
      components[c]=binary(133,fp,binary(129,fp,lin,truncated),inv1023);
    }
    emit(80,{type,original,components[0],components[1],components[2],components[3]});
  }
  if(converted!=mask)fail();out[3]=next;return out;
}
}
