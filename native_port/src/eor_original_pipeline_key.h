#pragma once
// Compare the effective immutable graphics state, independently of the
// source material owner and its mutable descriptors. Own every input-layout
// field so temporary Draw staging cannot leave dangling semantic pointers.
#include "PipelineState.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace hulk::eor::gpu {
class OriginalGraphicsPipelineKey {
 public:
  explicit OriginalGraphicsPipelineKey(const Diligent::GraphicsPipelineDesc& desc)
      : state_(desc) {
    if(desc.pRenderPass || !desc.InputLayout.NumElements ||
       desc.InputLayout.NumElements>DILIGENT_MAX_LAYOUT_ELEMENTS ||
       !desc.InputLayout.LayoutElements)
      throw std::invalid_argument("Unsupported original graphics cache key");
    state_.InputLayout={};
    elements_.reserve(desc.InputLayout.NumElements);
    for(Diligent::Uint32 i=0;i<desc.InputLayout.NumElements;++i) {
      const auto& source=desc.InputLayout.LayoutElements[i];
      Element element{source,std::nullopt};
      if(source.HLSLSemantic)element.semantic=source.HLSLSemantic;
      element.layout.HLSLSemantic=nullptr;
      elements_.push_back(std::move(element));
    }
  }
  bool Matches(const Diligent::GraphicsPipelineDesc& desc) const noexcept {
    if(desc.pRenderPass || desc.InputLayout.NumElements!=elements_.size() ||
       !desc.InputLayout.LayoutElements)return false;
    auto state=desc;
    state.InputLayout={};
    if(state_!=state)return false;
    for(size_t i=0;i<elements_.size();++i) {
      auto layout=desc.InputLayout.LayoutElements[i];
      const auto* semantic=layout.HLSLSemantic;
      layout.HLSLSemantic=nullptr;
      if(layout!=elements_[i].layout ||
         bool(semantic)!=elements_[i].semantic.has_value() ||
         (semantic && *elements_[i].semantic!=semantic))return false;
    }
    return true;
  }
 private:
  struct Element {
    Diligent::LayoutElement layout;
    std::optional<std::string> semantic;
  };
  Diligent::GraphicsPipelineDesc state_;
  std::vector<Element> elements_;
};
} // namespace hulk::eor::gpu
