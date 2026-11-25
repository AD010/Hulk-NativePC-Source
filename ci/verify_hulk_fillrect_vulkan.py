#!/usr/bin/env python3
"""Create the genuine original FillRect pass pipeline using raw Vulkan.
Independent driver smoke test; does not execute a Draw or prove game Present.
"""
import ctypes as C
import struct
from verify_hulk_vulkan_module_bank import U32, Ptr, ShaderInfo, check

F32 = C.c_float
I32 = C.c_int32

def layout(name, fields):
    return type(name, (C.Structure,), {'_fields_': fields})

def info(name, fields):
    return layout(name, [('sType', U32), ('pNext', Ptr), ('flags', U32)] + fields)

Binding = layout('Binding', [('binding', U32), ('type', U32), ('count', U32), ('stages', U32), ('samplers', Ptr)])
SetInfo = info('SetInfo', [('count', U32), ('bindings', Ptr)])
LayoutInfo = info('LayoutInfo', [('count', U32), ('sets', Ptr), ('pushCount', U32), ('pushRanges', Ptr)])
Attachment = layout('Attachment', [(x, U32) for x in ('flags','format','samples','load','store','stencilLoad','stencilStore','initial','final')])
Reference = layout('Reference', [('attachment', U32), ('layout', U32)])
Subpass = layout('Subpass', [('flags', U32), ('bindPoint', U32), ('inputCount', U32), ('inputs', Ptr), ('colorCount', U32), ('colors', Ptr), ('resolves', Ptr), ('depth', Ptr), ('preserveCount', U32), ('preserves', Ptr)])
RenderInfo = info('RenderInfo', [('count', U32), ('attachments', Ptr), ('subpassCount', U32), ('subpasses', Ptr), ('dependencyCount', U32), ('dependencies', Ptr)])
Stage = info('Stage', [('stage', U32), ('module', Ptr), ('entry', C.c_char_p), ('specialization', Ptr)])
VertexBinding = layout('VertexBinding', [('binding', U32), ('stride', U32), ('rate', U32)])
Attribute = layout('Attribute', [('location', U32), ('binding', U32), ('format', U32), ('offset', U32)])
VertexInput = info('VertexInput', [('bindingCount', U32), ('bindings', Ptr), ('attributeCount', U32), ('attributes', Ptr)])
Assembly = info('Assembly', [('topology', U32), ('restart', U32)])
Viewport = layout('Viewport', [(x, F32) for x in ('x','y','width','height','minDepth','maxDepth')])
Rect = layout('Rect', [('x', I32), ('y', I32), ('width', U32), ('height', U32)])
ViewportInfo = info('ViewportInfo', [('viewportCount', U32), ('viewports', Ptr), ('scissorCount', U32), ('scissors', Ptr)])
Raster = info('Raster', [('depthClamp', U32), ('discard', U32), ('polygon', U32), ('cull', U32), ('front', U32), ('depthBias', U32), ('biasConstant', F32), ('biasClamp', F32), ('biasSlope', F32), ('lineWidth', F32)])
MSAA = info('MSAA', [('samples', U32), ('sampleShading', U32), ('minSampleShading', F32), ('sampleMask', Ptr), ('alphaCoverage', U32), ('alphaOne', U32)])
BlendAttachment = layout('BlendAttachment', [(x, U32) for x in ('enable','srcColor','dstColor','colorOp','srcAlpha','dstAlpha','alphaOp','mask')])
Blend = info('Blend', [('logicEnable', U32), ('logic', U32), ('count', U32), ('attachments', Ptr), ('constants', F32*4)])
PipelineInfo = info('PipelineInfo', [('stageCount', U32), ('stages', Ptr), ('vertex', Ptr), ('assembly', Ptr), ('tessellation', Ptr), ('viewport', Ptr), ('raster', Ptr), ('msaa', Ptr), ('depth', Ptr), ('blend', Ptr), ('dynamic', Ptr), ('layout', Ptr), ('renderPass', Ptr), ('subpass', U32), ('base', Ptr), ('baseIndex', I32)])

def addr(obj):
    return C.cast(C.pointer(obj), Ptr)

def verify(vk, device, z):
    def bind(name, result, *args):
        fn = getattr(vk, name)
        fn.restype, fn.argtypes = result, args
        return fn
    def create(name, obj):
        fn = bind('vkCreate'+name, I32, Ptr, C.POINTER(type(obj)), Ptr, C.POINTER(Ptr))
        handle = Ptr()
        check(fn(device, C.byref(obj), None, C.byref(handle)), 'vkCreate'+name)
        if not handle:
            raise RuntimeError('Null Vulkan '+name)
        owned.append((name,handle))
        return handle
    owned=[]
    try:
        ledger=z.read('HEORMAT2.bin')
        count, version=struct.unpack_from('<II',ledger,8)
        if ledger[:8]!=b'HEORMAT2' or version!=2 or len(ledger)!=16+72*count:
            raise ValueError('Invalid private original effect ledger')
        pairs=[(ledger[o+8:o+40].hex(),ledger[o+40:o+72].hex()) for o in range(16,len(ledger),72) if struct.unpack_from('<II',ledger,o)==(0x1edd5adf,2)]
        if len(pairs)!=1:
            raise ValueError('Missing original FillRect pass 2')
        vs,ps=pairs[0]
        if (vs,ps)!=('f884518386e364ce02f3028676ccded8ce4da08b49ba3174603c1c57aa81884e','fbdf6c11549d1782b12b72ef0364dd74c02c42436bd65255087f6eb351f09e81'):
            raise ValueError('Original FillRect shader identity differs')
        modules=[]
        for digest,stage in ((vs,'vs'),(ps,'ps')):
            data=z.read(f'aot/{digest}.{stage}.spv')
            words=(U32*(len(data)//4)).from_buffer_copy(data)
            modules.append(create('ShaderModule',ShaderInfo(sType=16,size=len(data),code=words)))
        # Original scalar ABI: set 2, binding 0 shared uniform, binding 2 PS float4 registers.
        bindings=(Binding*2)(Binding(0,6,1,0x11,None),Binding(2,6,1,0x10,None))
        empty=create('DescriptorSetLayout',SetInfo(sType=32))
        actual=create('DescriptorSetLayout',SetInfo(sType=32,count=2,bindings=addr(bindings)))
        sets=(Ptr*3)(empty.value,empty.value,actual.value)
        pl=create('PipelineLayout',LayoutInfo(sType=30,count=3,sets=addr(sets)))
        attachment=Attachment(0,37,1,1,0,2,1,0,2) # RGBA8, clear/store, color attachment
        ref=Reference(0,2)
        sub=Subpass(colorCount=1,colors=addr(ref))
        render=create('RenderPass',RenderInfo(sType=38,count=1,attachments=addr(attachment),subpassCount=1,subpasses=addr(sub)))
        stages=(Stage*2)(Stage(sType=18,stage=1,module=modules[0],entry=b'main'),Stage(sType=18,stage=0x10,module=modules[1],entry=b'main'))
        vb=VertexBinding(0,16,0)
        attr=Attribute(0,0,109,0)
        vi=VertexInput(sType=19,bindingCount=1,bindings=addr(vb),attributeCount=1,attributes=addr(attr))
        ia=Assembly(sType=20,topology=3)
        viewport=Viewport(0,0,64,32,0,1)
        rect=Rect(0,0,64,32)
        vp=ViewportInfo(sType=22,viewportCount=1,viewports=addr(viewport),scissorCount=1,scissors=addr(rect))
        raster=Raster(sType=23,lineWidth=1)
        msaa=MSAA(sType=24,samples=1)
        ba=BlendAttachment(mask=15)
        blend=Blend(sType=26,count=1,attachments=addr(ba))
        pi=PipelineInfo(sType=28,stageCount=2,stages=addr(stages),vertex=addr(vi),assembly=addr(ia),viewport=addr(vp),raster=addr(raster),msaa=addr(msaa),blend=addr(blend),layout=pl,renderPass=render,baseIndex=-1)
        pipeline=Ptr()
        create_pipeline=bind('vkCreateGraphicsPipelines',I32,Ptr,Ptr,U32,C.POINTER(PipelineInfo),Ptr,C.POINTER(Ptr))
        check(create_pipeline(device,None,1,C.byref(pi),None,C.byref(pipeline)),'vkCreateGraphicsPipelines FillRect')
        if not pipeline:
            raise RuntimeError('Null original FillRect pipeline')
        owned.append(('Pipeline',pipeline))
        print('PASS: authentic EoR FillRect pass 2 graphics pipeline created',flush=True)
    finally:
        for name,handle in reversed(owned):
            bind('vkDestroy'+name,None,Ptr,Ptr,Ptr)(device,handle,None)
