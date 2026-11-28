#!/usr/bin/env python3
"""Adapt original Hulk registered texture gradients to XenosRecomp HLSL.

Verified original SHADEREF has two pixel programs that SET gradients H/V,
then perform registered-gradient 2D texture fetches. Preserve those via
Texture2D.SampleGrad. Unsupported predicated, vertex and non-2D cases fail.
"""
import argparse
from pathlib import Path
from patch_xenosrecomp_hulk_lod import LODGATE, EMIT_NEW

CPP = 'XenosRecomp/shader_recompiler.cpp'
HPP = 'XenosRecomp/shader_recompiler.h'
COMMON = 'XenosRecomp/shader_common.h'
MARK = 'HULK_EOR_XENOSRECOMP_GRAD2D_V1'
GATE = LODGATE.replace(
    '          instr.useCompLod && !instr.useRegLod && !instr.useRegGradients)) {',
    '          instr.useCompLod && !instr.useRegLod && !instr.useRegGradients) &&\n'
    '        instr.opcode != FetchOpcode::SetTextureGradientsHorz &&\n'
    '        instr.opcode != FetchOpcode::SetTextureGradientsVert) {')
HPP_OLD = '    bool isPixelShader = false;'
HPP_NEW = HPP_OLD + '\n    bool hulk_grad_h_set = false, hulk_grad_v_set = false;'
LAMBDA_END = '''        };

    std::string constName;'''
LAMBDA_NEW = '''        };

    // HULK/REG-GRAD-2D: maintain real shader-local Xenos gradient registers.
    if (instr.opcode == FetchOpcode::SetTextureGradientsHorz ||
        instr.opcode == FetchOpcode::SetTextureGradientsVert) {
        if (!isPixelShader || instr.isPredicated)
            throw std::runtime_error("Hulk gradients require an unpredicated pixel shader");
        const bool horizontal = instr.opcode == FetchOpcode::SetTextureGradientsHorz;
        if (horizontal) hulk_grad_h_set = true;
        else hulk_grad_v_set = true;
        indent();
        out += horizontal ? "hulkGradH = float3(" : "hulkGradV = float3(";
        printSrcRegister(3);
        out += ");\\n";
        return;
    }
    if (instr.useRegGradients &&
        !(isPixelShader && instr.opcode == FetchOpcode::TextureFetch &&
          instr.dimension == TextureDimension::Texture2D &&
          !instr.useRegLod && !bicubic && hulk_grad_h_set && hulk_grad_v_set))
        throw std::runtime_error("Unsupported Hulk registered-gradient fetch");

    std::string constName;'''
DIM_OLD = '''    out += dimension;

#ifdef UNLEASHED_RECOMP'''
DIM_NEW = '''    out += dimension;
    if (instr.opcode == FetchOpcode::TextureFetch && instr.useRegGradients)
        out += "Grad";

#ifdef UNLEASHED_RECOMP'''
OFFSET_OLD = '''    case TextureDimension::Texture2D:
        print(", float2({}, {})", instr.offsetX * 0.5f, instr.offsetY * 0.5f);
        break;'''
OFFSET_NEW = '''    case TextureDimension::Texture2D:
        print(", float2({}, {})", instr.offsetX * 0.5f, instr.offsetY * 0.5f);
        if (instr.opcode == FetchOpcode::TextureFetch && instr.useRegGradients)
            out += ", hulkGradH.xy, hulkGradV.xy";
        break;'''
DECLARE_OLD = '''        out += "\\tCubeMapData cubeMapData = (CubeMapData)0;\\n";'''
DECLARE_NEW = DECLARE_OLD + '''
        out += "\\tfloat3 hulkGradH = 0.0;\\n";
        out += "\\tfloat3 hulkGradV = 0.0;\\n";'''
EMIT_GRAD = EMIT_NEW + '\n    out += "// ' + MARK + '\\n";'
HELPER_ANCHOR = 'float2 getWeights2D(uint resourceDescriptorIndex'
HELPER = '''// HULK/REG-GRAD-2D: shader-local explicit Xenos gradients.
float4 tfetch2DGrad(uint resourceDescriptorIndex, uint samplerDescriptorIndex,
                    float2 texCoord, float2 offset,
                    float2 gradientH, float2 gradientV)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return texture.SampleGrad(g_SamplerDescriptorHeap[samplerDescriptorIndex],
                              texCoord + offset / getTexture2DDimensions(texture),
                              gradientH, gradientV);
}

'''

def patch_sources(cpp: str, hpp: str, common: str):
    if (GATE in cpp and LAMBDA_NEW in cpp and DIM_NEW in cpp and
        OFFSET_NEW in cpp and DECLARE_NEW in cpp and EMIT_GRAD in cpp and
        HPP_NEW in hpp and HELPER in common):
        return cpp, hpp, common, False
    if 'HULK/REG-GRAD-2D' in cpp + common or MARK in cpp or HPP_NEW in hpp:
        raise ValueError('Partially installed Hulk registered-gradient adapter')
    for label, src, anchor in (
        ('gate',cpp,LODGATE),('source',cpp,LAMBDA_END),('dimension',cpp,DIM_OLD),
        ('offset',cpp,OFFSET_OLD),('declare',cpp,DECLARE_OLD),('marker',cpp,EMIT_NEW),
        ('header',hpp,HPP_OLD),('helper',common,HELPER_ANCHOR)):
        if src.count(anchor) != 1:
            raise ValueError('XenosRecomp source drift at '+label)
    for before, after in ((LODGATE,GATE),(LAMBDA_END,LAMBDA_NEW),
                          (DIM_OLD,DIM_NEW),(OFFSET_OLD,OFFSET_NEW),
                          (DECLARE_OLD,DECLARE_NEW),(EMIT_NEW,EMIT_GRAD)):
        cpp = cpp.replace(before, after, 1)
    return (cpp, hpp.replace(HPP_OLD,HPP_NEW,1),
            common.replace(HELPER_ANCHOR,HELPER+HELPER_ANCHOR,1), True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout',type=Path)
    root=parser.parse_args().checkout
    c,h,s=root/CPP,root/HPP,root/COMMON
    from patch_xenosrecomp_hulk_programs import MARK as PROGRAMS_MARK, patch_programs
    cpp,hpp,common=c.read_text(),h.read_text(),s.read_text()
    if PROGRAMS_MARK in cpp:
        patched=patch_programs(cpp)  # verify and update the installed frontend
        if GATE not in cpp or HPP_NEW not in hpp or HELPER not in common:
            raise ValueError('Incomplete original gradient/compiler support')
        changed=patched!=cpp
        if changed:
            c.write_text(patched,encoding="utf-8",newline="\n")
    else:
        cpp,hpp,common,_=patch_sources(cpp,hpp,common)
        cpp=patch_programs(cpp)
        c.write_text(cpp,encoding='utf-8',newline='\n')
        h.write_text(hpp,encoding='utf-8',newline='\n')
        s.write_text(common,encoding='utf-8',newline='\n')
        changed=True
    print('Hulk native 2D SampleGrad:', 'installed' if changed else 'already installed')

if __name__ == '__main__':
    main()
