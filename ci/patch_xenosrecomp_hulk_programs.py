"""Adapt the existing XenosRecomp frontend to original Hulk program operands.

Input is the existing LOD/gradient-patched hedge-dev/XenosRecomp source.
This patch contains no game shader instructions or constant values.
"""
from __future__ import annotations

MARK = "HULK_EOR_SOURCE_INTERFACE_V2"


def patch_programs(source: str) -> str:
    # Keep the variable spelling used by XenosRecomp's ALU and fetch emitters.
    # Link stages through the container's interpolator register. Pixel inputs
    # and vertex outputs have independent declaration orders and D3D semantics.
    anchor = "    const auto shader = reinterpret_cast<const Shader*>(shaderData + shaderContainer->shaderOffset);"
    varyings = r"""
    struct HulkVarying { DeclUsage usage; uint32_t index, location; };
    std::vector<HulkVarying> hulkVaryings;
    const uint32_t hulkCount = (shader->interpolatorInfo >> 5) & 31;
    for (uint32_t i = 0; i < hulkCount; ++i) {
        const auto* vs = reinterpret_cast<const VertexShader*>(shader);
        const uint32_t word = isPixelShader
            ? reinterpret_cast<const PixelShader*>(shader)->interpolators[i].get()
            : vs->vertexElementsAndInterpolators[vs->field18 + vs->vertexElementCount + i].get();
        const auto usage = DeclUsage((word >> 4) & 15);
        const uint32_t index = word & 15;
        const uint32_t location = (word >> 8) & 15;
        if (uint32_t(usage) >= std::size(USAGE_VARIABLES) ||
            (!isPixelShader && location != i))
            throw std::runtime_error("Unrecognized original VS/PS interpolator allocation");
        for (const auto& previous : hulkVaryings)
            if (previous.location == location || (previous.usage == usage && previous.index == index))
                throw std::runtime_error("Duplicate original shader varying");
        hulkVaryings.push_back({usage, index, location});
    }
    out += "// HULK_EOR_SOURCE_INTERFACE_V2\n";
"""
    # Xenos export 63.x contains point size. Edge flag and kill vertex have
    # distinct hardware meanings; writing either requires separate lowering.
    point_case = r"""            case ExportRegister::VSPointSizeEdgeFlagKillVertex:
                if ((instr.vectorWriteMask | instr.scalarWriteMask) != 1u || instr.scalarDestRelative)
                    throw std::runtime_error("Unsupported Xenos edge/kill export");
                exportRegister = "hulkRaster.pointSize";
                break;
"""
    point_signature = r"""    if (!isPixelShader && out.find("hulkRaster.pointSize.x") != std::string::npos) {
        const auto at = out.find("\tout float4 oPos : SV_Position");
        if (at == std::string::npos) throw std::runtime_error("Missing vertex entry");
        out.insert(at, "\tout HulkRasterOutput hulkRaster,\n");
        out.insert(out.find("void main("),
            "struct HulkRasterOutput { [[vk::builtin(\"PointSize\")]] float pointSize : PSIZE; };\n");
    }
"""
    # CTAB Boolean ranges are stage-local. CF instructions carry the physical
    # bit index; PS b0 starts at 128. Honor array elements within CTAB counts.
    bool_old = r"""            println("\t#define {} (1 << {})", constantName, constantInfo->registerIndex + (isPixelShader ? 16 : 0));
            boolConstants.emplace(constantInfo->registerIndex, constantName);"""
    bool_new = r"""            const uint32_t start = constantInfo->registerIndex;
            const uint32_t count = constantInfo->registerCount;
            if (start + count > 16u)
                throw std::runtime_error("Original boolean constants exceed packed per-stage UBO");
            for (uint32_t n = 0; n < count; ++n) {
                const uint32_t physical = start + n + (isPixelShader ? 128u : 0u);
                const uint32_t bit = start + n + (isPixelShader ? 16u : 0u);
                hulkBoolBits.emplace(physical, bit);
            }"""
    one_d = r"""        if (!isPixelShader || instr.opcode != FetchOpcode::TextureFetch ||
            !instr.useCompLod || instr.useRegLod || instr.useRegGradients ||
            instr.texCoordDenorm || instr.offsetX || instr.lodBias || instr.sampleLocation ||
            instr.magFilter != 3 || instr.minFilter != 3 || instr.mipFilter != 3 ||
            instr.anisoFilter != 7)
            throw std::runtime_error("Unsupported original 1D sampling mode");
"""
    vertex_fetch = r"""    const bool vertexSample = !isPixelShader && instr.constIndex >= 16 && instr.constIndex < 20;
    if (vertexSample && (instr.dimension != TextureDimension::Texture2D ||
        instr.opcode != FetchOpcode::TextureFetch || instr.useCompLod || instr.useRegLod ||
        instr.useRegGradients || instr.texCoordDenorm || instr.lodBias || instr.sampleLocation ||
        instr.magFilter != 3 || instr.minFilter != 3 || instr.mipFilter != 3 || instr.anisoFilter != 7))
        throw std::runtime_error("Unsupported original vertex texture sampling mode");
    auto findResult = samplers.find(vertexSample ? instr.constIndex - 16 : instr.constIndex);"""
    # Address bits belong to the first and subsequent constant operands of
    # this ALU instruction. A relative sibling does not make a scalar relative.
    address = """            const bool priorConstant = operand == VECTOR_0 ? false :
                operand == VECTOR_1 ? !instr.src1Select : (!instr.src1Select || !instr.src2Select);
            const bool relative = priorConstant ? instr.const1Relative : instr.const0Relative;
"""
    edits = [
        ("    auto findResult = samplers.find(instr.constIndex);", vertex_fetch),
        ('    out += dimension;', '    out += dimension;\n    if (vertexSample) out += "LevelZero";'),
        ("            std::string regFormatted;", address + "            std::string regFormatted;"),
        ('instr.const0Relative ? (instr.constAddressRegisterRelative', 'relative ? (instr.constAddressRegisterRelative'),
        ('assert(!instr.const0Relative && !instr.const1Relative);',
         'if (relative) throw std::runtime_error("Relative source constant outside CTAB array");'),

        (anchor, anchor + varyings),
        ("for (auto& [usage, usageIndex] : INTERPOLATORS)",
         "for (const auto& [usage, usageIndex, location] : hulkVaryings)"),
        (r'println("\tin float4 i{0}{1} : {2}{1},",', r'println("\t[[vk::location({3})]] in float4 i{0}{1} : {2}{1},",'),
        (r'print(",\n\tout float4 o{0}{1} : {2}{1}",', r'print(",\n\t[[vk::location({3})]] out float4 o{0}{1} : {2}{1}",'),
        ('usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);',
         'usageIndex, USAGE_SEMANTICS[uint32_t(usage)], location);'),
        ("            case ExportRegister::VSPosition:", point_case + "            case ExportRegister::VSPosition:"),
        ('    out += "}";\n}', point_signature + '    out += "}";\n}'),
        (bool_old, bool_new),
        ('    out += include;', '    std::map<uint32_t,uint32_t> hulkBoolBits;\n    out += include;'),
        ('auto findResult = boolConstants.find(cfInstr.condJmp.boolAddress);',
         'auto findResult = hulkBoolBits.find(cfInstr.condJmp.boolAddress);'),
        ('if (findResult != boolConstants.end())','if (findResult != hulkBoolBits.end())'),
        ('if ((g_Booleans & {}) {}= 0)','if ((g_Booleans & (1u << {})) {}= 0)'),
        ('        dimension = "1D";', one_d + '        dimension = "1D";'),
    ]
    if MARK in source:
        if not all(new in source for _, new in edits):
            raise ValueError("Incomplete Hulk program patch")
        return source
    for old, new in edits:
        expected = 3 if old.startswith("for (auto&") else 2 if old.startswith(("usageIndex,", "assert(!instr.const0Relative")) else 1
        if source.count(old) != expected:
            raise ValueError("Unexpected XenosRecomp program source around " + old[:70])
        source = source.replace(old, new)
    return source
