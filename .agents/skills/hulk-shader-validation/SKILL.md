---
name: hulk-shader-validation
description: Validate original EoR shader banks and Vulkan pipeline contracts; diagnose invalid SPIR-V, layout, sampler, mip and vertex mismatches.
---
Reuse ci/hulk_xenos_to_spirv.py and ci/hulk_spirv_resource_contract.py and
their existing tests. Preserve cumulative AOT bank checkpoints; do not rebuild
all shaders without cause. Validate changed modules using installed spirv-val
with the target environment matching the actual Vulkan runtime. Record validator
version, target environment and module IDs privately. Compare reflected stages,
descriptor sets, bindings, vertex formats and actual live PSO. Structural checks
are not spirv-val, and passing spirv-val is not proof of pixels. Fix the
producer rather than suppressing errors or substituting dummy shaders.
