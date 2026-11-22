#pragma once
// Exact ShaderEffects source program pair for Font_Main in the SHA-pinned
// original USA Xbox 360 archive. This proves ONLY the immutable archived
// VS/PS digest relationship; current guest EFontShader, vertex declaration,
// material sampler and GPU atlas are independently checked at each Draw.
// No original shader instructions or proprietary source bytes are embedded.
#include "eor_spirv_bank.h"
#include <cstdint>
namespace hulk::eor::ui {
inline constexpr uint32_t kOriginalFontMainEffectHash=0xE601E566u;
inline constexpr uint32_t kOriginalFontMainPassIndex=0u;
inline constexpr aot::Digest kOriginalFontMainVsSha{
  0xf4u,0xa3u,0xcbu,0xaeu,0x7du,0x8eu,0x9cu,0x28u,
  0x14u,0xdfu,0x61u,0x6cu,0xa8u,0x50u,0x70u,0x1du,
  0x93u,0x87u,0x47u,0x7fu,0x4au,0xaeu,0x06u,0x06u,
  0x1eu,0x61u,0x46u,0xdeu,0xe2u,0xfdu,0x79u,0xcbu
};
inline constexpr aot::Digest kOriginalFontMainPsSha{
  0x2fu,0x35u,0x05u,0xb3u,0x32u,0x8cu,0xdeu,0xe6u,
  0xfcu,0x59u,0xc0u,0x5fu,0x27u,0x27u,0x17u,0x3du,
  0x57u,0xa8u,0xd7u,0x5fu,0xe5u,0x6au,0x01u,0xb5u,
  0x76u,0x25u,0x5bu,0x02u,0xe4u,0x6cu,0x47u,0x68u
};
constexpr bool MatchesOriginalUsaFontMainProgram(
    uint32_t effect,uint32_t pass,
    const aot::Digest& original_vs,const aot::Digest& original_ps) noexcept {
  return effect==kOriginalFontMainEffectHash &&
         pass==kOriginalFontMainPassIndex &&
         original_vs==kOriginalFontMainVsSha &&
         original_ps==kOriginalFontMainPsSha;
}
} // namespace hulk::eor::ui
