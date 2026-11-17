#pragma once
// DiligentCore Vulkan-compatible shader-resource binding for verified EoR
// textured draws. The host renderer owns PSO creation, SRB lifetimes, and
// Submit/CommitShaderResources/DrawIndexed ordering.
#include "eor_diligent_xenos_textures.h"
#include "eor_verified_sampler_bindings.h"
#include <span>
#include <memory>
namespace Diligent { struct IShaderResourceBinding; }
namespace hulk::eor::gpu {
struct OriginalDiligentSampler {
  VerifiedEorSampler identity{};
  // Keep the physical host image/SRV alive after the residency cache is
  // cleared or LOD streaming replaces its entries. A raw borrowed pointer
  // used to dangle in queued original material passes across cache resets.
  std::shared_ptr<const UploadedEorTexture> uploaded;
  // NativeX fetch shaders use 2D arrays; legacy EoR shaders may use 2D.
  // The caller proves this by current, exact shader-variable reflection.
  OriginalTextureViewKind required_view=OriginalTextureViewKind::Native2D;
};
// Resolve all shader variables before mutating the binding. Never invent
// variable names, register numbers, or bind a fallback/magenta texture.
// An empty list is valid only when the calling shader uses no textures;
// verifying that is the caller's responsibility.
void BindVerifiedOriginalEorTextures(
    Diligent::IShaderResourceBinding* shader_resources,
    std::span<const OriginalDiligentSampler> samplers);
} // namespace hulk::eor::gpu
