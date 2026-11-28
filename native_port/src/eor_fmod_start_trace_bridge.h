#pragma once
#include "eor_fmod_start_trace.h"
namespace hulk::eor::xdk {
void TraceFmodSelectionBegin(uint8_t* base,uint32_t child) noexcept;
void TraceFmodSelectionStore(uint8_t* base,uint32_t child,uint32_t parent,uint32_t ordinal) noexcept;
void TraceFmodChannelStart(uint8_t* base,uint32_t child) noexcept;
}
