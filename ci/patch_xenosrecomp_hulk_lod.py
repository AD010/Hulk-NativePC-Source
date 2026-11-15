#!/usr/bin/env python3
"""EoR XenosRecomp pixel 2D getCompTexLOD to native HLSL LOD query.

Xenia's QueryTextureLod extracts OpImageQueryLod component 1 (unclamped
LOD). Texture2D.CalculateLevelOfDetailUnclamped in a PIXEL shader is the
matching HLSL operation, not Sonic's max(0,computeMipLevel) approximation.
Unsupported 1D/3D/cube and vertex-stage derivatives remain fail-closed.
"""
from pathlib import Path
import argparse

CPP = 'XenosRecomp/shader_recompiler.cpp'
HEADER = 'XenosRecomp/shader_common.h'
STRICT = '''    if (instr.opcode != FetchOpcode::TextureFetch &&
        instr.opcode != FetchOpcode::GetTextureWeights) {'''
LODGATE = '''    // HULK/XENOS-COMP-LOD-PS2D: preserve original Xenos opcode 17.
    // Only fragment-stage 2D implicit gradients are legal here.
    // Xenia translation rejects GetTextureComputedLod when implicit LOD is
    // disabled or the instruction requests explicit LOD/register gradients.
    if (instr.opcode != FetchOpcode::TextureFetch &&
        instr.opcode != FetchOpcode::GetTextureWeights &&
        !(instr.opcode == FetchOpcode::GetTextureComputedLod &&
          isPixelShader && instr.dimension == TextureDimension::Texture2D &&
          instr.useCompLod && !instr.useRegLod && !instr.useRegGradients)) {'''
SWITCH = '''    case FetchOpcode::GetTextureWeights:
    {
        out += "getWeights";
        break;
    }'''
SWITCH_NEW = SWITCH + '''
    case FetchOpcode::GetTextureComputedLod:
    {
        out += "getCompTexLod";
        break;
    }'''
MARKER = 'HULK_EOR_XENOSRECOMP_STRICT_LOD2D_V1'
EMIT_OLD = '    out += include;'
EMIT_NEW = '    out += include;\n    out += "// ' + MARKER + '\\n";'
HELPER_ANCHOR = 'float w0(float a)'
HELPER = '''// HULK/XENOS-COMP-LOD-PS2D: use the UNCLAMPED pixel-stage sampler LOD.
// Xenia's SPIR-V translator selects OpImageQueryLod's 2nd component.
// The opcode does not apply the instruction's optional texel offset.
float4 getCompTexLod2D(uint resourceDescriptorIndex,
                       uint samplerDescriptorIndex,
                       float2 texCoord, float2 unusedOffset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    float lod = texture.CalculateLevelOfDetailUnclamped(
        g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord);
    return float4(lod, 0.0, 0.0, 0.0);
}

'''


def patch_sources(cpp: str, hdr: str):
    if LODGATE in cpp and SWITCH_NEW in cpp and HELPER in hdr and EMIT_NEW in cpp:
        return cpp, hdr, False
    if 'HULK/XENOS-COMP-LOD-PS2D' in cpp + hdr or MARKER in cpp:
        raise ValueError('Partial Hulk computed pixel LOD patch')
    if 'HULK/XENOSRECOMP-FETCH-STRICT' not in cpp:
        raise ValueError('Apply strict missing-fetch guard first')
    if cpp.count(STRICT) != 1 or cpp.count(SWITCH) != 1 or cpp.count(EMIT_OLD) != 1:
        raise ValueError('XenosRecomp fetch emitter ABI drift')
    if hdr.count(HELPER_ANCHOR) != 1:
        raise ValueError('XenosRecomp shader_common helper anchor drift')
    cpp = cpp.replace(STRICT, LODGATE, 1).replace(SWITCH, SWITCH_NEW, 1)\
             .replace(EMIT_OLD, EMIT_NEW, 1)
    hdr = hdr.replace(HELPER_ANCHOR, HELPER + HELPER_ANCHOR, 1)
    return cpp, hdr, True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('checkout', type=Path)
    root = p.parse_args().checkout
    cpp_path, hdr_path = root / CPP, root / HEADER
    cpp, hdr, changed = patch_sources(
        cpp_path.read_text(encoding='utf-8'),
        hdr_path.read_text(encoding='utf-8'))
    if changed:
        cpp_path.write_text(cpp, encoding='utf-8', newline='\n')
        hdr_path.write_text(hdr, encoding='utf-8', newline='\n')
    print('Hulk native pixel getCompTexLOD:', 'installed' if changed else 'already installed')


if __name__ == '__main__':
    main()
