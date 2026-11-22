"""Lower XenosRecomp's private HLSL to scalar Vulkan resources for EoR.

The translator supplies a separate compile-time XDK register / name for each
fetch. Preserve its generated fetch arithmetic and pass typed resource objects
to the same helpers. No original shader source is stored in this module.
"""
import re


def native_resources(hlsl: str, stage: str) -> str:
    prefix, entry, body = hlsl.partition('void main(')
    if not entry or stage not in ('VS', 'PS'):
        raise ValueError('Missing original AOT shader entry or stage')
    if 'g_Texture2DDescriptorHeap' not in prefix:
        if 'DescriptorIndex' in body or 'RawBufferLoad' in hlsl:
            raise ValueError('Unrecognized original texture/constant ABI')
        return hlsl

    declared = {}
    # Offsets are emitted by XenosRecomp from the original CTAB register.
    macro = re.compile(r'^#define (\w+)_(Texture2D|Texture3D|TextureCube|Sampler)DescriptorIndex '
                       r'vk::RawBufferLoad<uint>\(g_PushConstants.SharedConstants \+ (\d+)\)$', re.M)
    bases = {'Texture2D': 0, 'Texture3D': 64, 'TextureCube': 128, 'Sampler': 192}
    for name, kind, offset in macro.findall(prefix):
        delta = int(offset) - bases[kind]
        if not 0 <= delta < 64 or delta % 4 or (name, kind) in declared:
            raise ValueError('Ambiguous original XDK texture declaration')
        declared[name, kind] = delta // 4
    prefix = macro.sub('', prefix)
    used = {}
    def resource(match):
        name, kind = match.groups()
        slot = declared.get((name, kind))
        if slot is None:
            raise ValueError('Original fetch has no CTAB descriptor ' + name)
        symbol = f'Hulk{kind}_s{slot}'
        if (kind, slot) in used and used[kind, slot] != name:
            raise ValueError('Two original texture names share one XDK register')
        used[kind, slot] = name
        return symbol

    body = re.sub(r'\b(\w+)_(Texture2D|Texture3D|TextureCube|Sampler)DescriptorIndex\b', resource, body)
    if 'DescriptorIndex' in body:
        raise ValueError('Unsupported original texture fetch reference')
    # Diligent maps HLSL vertex semantics to the explicit numeric input
    # location. Preserve the original translator location and value type.
    if stage == 'VS':
        body = re.sub(r'(\[\[vk::location\((\d+)\)\]\]\s+in\s+\w+\s+\w+\s*:\s*)\w+',
                      lambda match: match[1] + 'ATTRIB' + match[2], body)
    # The swizzle mask is a uint32. Widening it to 64 bits makes this
    # simple vertex fetch require shaderInt64 on otherwise compatible GPUs.
    prefix = prefix.replace('(swappedTexcoords & (1ull << semanticIndex)) != 0',
                            '(semanticIndex < 32u && ((swappedTexcoords >> (semanticIndex & 31u)) & 1u) != 0u)')
    declarations = []
    for kind, slot in sorted(used):
        var_type = 'SamplerState' if kind == 'Sampler' else kind + '<float4>'
        # Each stage has its own descriptor namespace in the native PSO.
        binding = bases[kind] // 4 + slot
        set_index = 1 if stage == 'PS' else 0
        declarations.append(f'[[vk::binding({binding}, {set_index})]] {var_type} Hulk{kind}_s{slot};')

    prefix = re.sub(r'^(?:Texture(?:2D|3D|Cube)<float4>|SamplerState) g_\w+DescriptorHeap\[\] : register\([^;]+;\n',
                    '', prefix, flags=re.M)
    # Helper signatures specify the dimension. getPixelCoord is 2D.
    helper = re.compile(r'(\w+\s+\w+\s*\()uint resourceDescriptorIndex([^{}]+)\{([^{}]*?)\}', re.S)
    def typed(match):
        start, params, code = match.groups()
        kinds = set(re.findall(r'g_(Texture(?:2D|3D|Cube))DescriptorHeap\[resourceDescriptorIndex\]', code))
        if len(kinds) != 1:
            raise ValueError('Unsupported HLSL resource helper ' + start.strip())
        kind, = kinds
        params = params.replace('uint samplerDescriptorIndex', 'SamplerState nativeSampler')
        code = code.replace('g_' + kind + 'DescriptorHeap[resourceDescriptorIndex]', 'nativeImage')
        code = code.replace('g_SamplerDescriptorHeap[samplerDescriptorIndex]', 'nativeSampler')
        return start + kind + '<float4> nativeImage' + params + '{' + code + '}'
    prefix = helper.sub(typed, prefix)
    if 'DescriptorHeap[' in prefix:
        raise ValueError('Unsupported HLSL descriptor heap access')

    # Native Vulkan binds UBOs directly. Keep the original c# index even for
    # relative addressing. The compiler still owns the original bounds checks.
    shared = ('cbuffer HulkSharedConstants : register(b0, space2) {\n'
              '    uint g_Booleans; uint g_SwappedTexcoords; float2 g_HalfPixelOffset;\n'
              '    float g_AlphaThreshold; uint g_SpecFlags;\n};\n'
              '#define g_SpecConstants() g_SpecFlags\n')
    prefix, occurrences = re.subn(r'struct PushConstants\s*\{[^}]*\};.*?'
        r'#define g_SpecConstants\(\) g_SpecConstants\n', shared, prefix, count=1, flags=re.S)
    if occurrences != 1:
        raise ValueError('Unrecognized shader host constant layout')
    stage_name = 'Vertex' if stage == 'VS' else 'Pixel'
    bank = 'Hulk' + stage_name + 'Floats'
    address = r'vk::RawBufferLoad<float4>\(g_PushConstants\.' + stage_name + r'ShaderConstants \+ '
    def fixed(match):
        offset = int(match.group(1))
        if offset % 16 or offset >= 4096:
            raise ValueError('Unaligned or out-of-range source float4')
        return f'{bank}[{offset // 16}]'
    prefix = re.sub(address + r'(\d+), 0x10\)', fixed, prefix)
    prefix = re.sub(address + r'\((\d+) \+ min\(INDEX, (\d+)\)\) \* 16, 0x10\)',
                    lambda m: f'{bank}[{m[1]} + min(INDEX, {m[2]})]', prefix)
    if 'g_PushConstants' in prefix or 'RawBufferLoad' in body:
        raise ValueError('Unresolved original shader constant address')
    if bank + '[' in prefix:
        declarations.append(f'cbuffer Hulk{stage_name}Constants : register(b{1 if stage=="VS" else 2}, space2) '
                            f'{{ float4 {bank}[256]; }};')
    return '\n'.join(declarations) + '\n' + prefix + entry + body
