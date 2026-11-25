#!/usr/bin/env python3
"""Create a real Vulkan graphics pipeline for every authenticated original pair.
Private bank only. Fixed single RGBA8 render pass is a driver smoke fixture;
not a substitute for actual per-Draw state, shader execution or Present.
"""
import ctypes as C
import struct
from verify_hulk_fillrect_vulkan import (
    U32, Ptr, I32, ShaderInfo, Binding, SetInfo, LayoutInfo, Attachment,
    Reference, Subpass, RenderInfo, Stage, VertexBinding, Attribute, VertexInput,
    Assembly, Viewport, Rect, ViewportInfo, Raster, MSAA, BlendAttachment, Blend,
    PipelineInfo, addr, check,
)

def reflect(data, stage):
    if len(data)<20 or len(data)%4:
        raise ValueError('Malformed SPIR-V size')
    w=struct.unpack('<%dI'%(len(data)//4),data)
    if w[0]!=0x07230203:
        raise ValueError('Invalid SPIR-V magic')
    dec, types, variables = {}, {}, []
    i=5
    while i<len(w):
        n, op = w[i]>>16, w[i]&65535
        if not n or i+n>len(w):
            raise ValueError('Malformed SPIR-V instruction')
        a=w[i+1:i+n]; i+=n
        if op==71:
            dec.setdefault(a[0],{})[a[1]]=a[2:]
        elif op in (21,22,23,25,26,27,28,29,30,32):
            types[a[0]]=(op,a[1:])
        elif op==59:
            variables.append(a[:3])
    descriptors, inputs = {}, {}
    for ty, ident, storage in variables:
        d=dec.get(ident,{})
        if 33 in d:
            if 34 not in d:
                raise ValueError('Missing descriptor set')
            op, args=types[types[ty][1][1]]
            kind={(2,30):6,(0,25):2,(0,26):0}.get((storage,op))
            if kind is None:
                raise ValueError('Unsupported descriptor ABI')
            key=(d[34][0],d[33][0])
            if key in descriptors:
                raise ValueError('Duplicate descriptor binding')
            descriptors[key]=(kind,stage)
        if stage==1 and storage==1 and 30 in d:
            op,args=types[types[ty][1][1]]
            if op!=23 or not 1<=args[1]<=4:
                raise ValueError('Unsupported vertex type')
            scalar,sa=types[args[0]]
            if scalar==22 and sa==(32,): base=100
            elif scalar==21 and sa==(32,0): base=98
            else: raise ValueError('Unsupported vertex scalar')
            loc=d[30][0]
            if loc in inputs: raise ValueError('Duplicate vertex input')
            inputs[loc]=(base+3*(args[1]-1),args[1]*4)
        if stage==0x10 and storage==3 and 30 in d and d[30][0]!=0:
            raise ValueError('Multiple render targets not covered by fixture')
    return descriptors,inputs

def verify_all(vk, device, z, start=0, stop=2597):
    def bind(name, result, *args):
        fn=getattr(vk,name);fn.restype,fn.argtypes=result,args
        return fn
    def create(owned,name,obj):
        fn=bind('vkCreate'+name,I32,Ptr,C.POINTER(type(obj)),Ptr,C.POINTER(Ptr))
        handle=Ptr()
        check(fn(device,C.byref(obj),None,C.byref(handle)),'vkCreate'+name)
        if not handle:raise RuntimeError('Null Vulkan '+name)
        owned.append((name,handle));return handle
    def release(owned):
        for name,handle in reversed(owned):
            bind('vkDestroy'+name,None,Ptr,Ptr,Ptr)(device,handle,None)
    ledger=z.read('HEORMAT2.bin')
    count,version=struct.unpack_from('<II',ledger,8)
    if ledger[:8]!=b'HEORMAT2' or version!=2 or count!=2597 or len(ledger)!=16+72*count or not 0<=start<stop<=count:
        raise ValueError('Invalid original effect ledger/range')
    outer=[]
    passed=0
    try:
        attachment=Attachment(0,37,1,1,0,2,1,0,2)
        ref=Reference(0,2);sub=Subpass(colorCount=1,colors=addr(ref))
        render=create(outer,'RenderPass',RenderInfo(sType=38,count=1,attachments=addr(attachment),subpassCount=1,subpasses=addr(sub)))
        ia=Assembly(sType=20,topology=3)
        viewport=Viewport(0,0,64,32,0,1);rect=Rect(0,0,64,32)
        vp=ViewportInfo(sType=22,viewportCount=1,viewports=addr(viewport),scissorCount=1,scissors=addr(rect))
        raster=Raster(sType=23,lineWidth=1);msaa=MSAA(sType=24,samples=1)
        ba=BlendAttachment(mask=15);blend=Blend(sType=26,count=1,attachments=addr(ba))
        create_pipeline=bind('vkCreateGraphicsPipelines',I32,Ptr,Ptr,U32,C.POINTER(PipelineInfo),Ptr,C.POINTER(Ptr))
        for index in range(start,stop):
            offset=16+72*index;effect,pass_index=struct.unpack_from('<II',ledger,offset)
            owned=[]
            try:
                descriptors={};inputs={};modules=[]
                for digest,stage,ext in ((ledger[offset+8:offset+40],1,'vs'),(ledger[offset+40:offset+72],0x10,'ps')):
                    data=z.read(f'aot/{digest.hex()}.{ext}.spv')
                    desc,ins=reflect(data,stage)
                    for key,(kind,flags) in desc.items():
                        old=descriptors.get(key,(kind,0))
                        if old[0]!=kind:raise ValueError('Conflicting cross-stage descriptor type')
                        descriptors[key]=(kind,flags|old[1])
                    inputs.update(ins)
                    words=(U32*(len(data)//4)).from_buffer_copy(data)
                    modules.append(create(owned,'ShaderModule',ShaderInfo(sType=16,size=len(data),code=words)))
                if any(s>2 for s,b in descriptors):raise ValueError('Unexpected descriptor set')
                sets=(Ptr*3)()
                for s in range(3):
                    entries=[Binding(b,kind,1,flags,None) for (ds,b),(kind,flags) in sorted(descriptors.items()) if ds==s]
                    bindings=(Binding*len(entries))(*entries)
                    sets[s]=create(owned,'DescriptorSetLayout',SetInfo(sType=32,count=len(entries),bindings=addr(bindings) if entries else None)).value
                pl=create(owned,'PipelineLayout',LayoutInfo(sType=30,count=3,sets=addr(sets)))
                stages=(Stage*2)(Stage(sType=18,stage=1,module=modules[0],entry=b'main'),Stage(sType=18,stage=0x10,module=modules[1],entry=b'main'))
                items=sorted(inputs.items())
                vbs=(VertexBinding*len(items))(*(VertexBinding(i,stride,0) for i,(loc,(fmt,stride)) in enumerate(items)))
                attrs=(Attribute*len(items))(*(Attribute(loc,i,fmt,0) for i,(loc,(fmt,stride)) in enumerate(items)))
                vi=VertexInput(sType=19,bindingCount=len(items),bindings=addr(vbs) if items else None,attributeCount=len(items),attributes=addr(attrs) if items else None)
                pi=PipelineInfo(sType=28,stageCount=2,stages=addr(stages),vertex=addr(vi),assembly=addr(ia),viewport=addr(vp),raster=addr(raster),msaa=addr(msaa),blend=addr(blend),layout=pl,renderPass=render,baseIndex=-1)
                pipeline=Ptr()
                check(create_pipeline(device,None,1,C.byref(pi),None,C.byref(pipeline)),'vkCreateGraphicsPipelines')
                if not pipeline:raise RuntimeError('Null graphics pipeline')
                owned.append(('Pipeline',pipeline));passed+=1
            except Exception as exc:
                raise RuntimeError(f'Original pair {index}, effect {effect:08x}, pass {pass_index}: {exc}') from exc
            finally:
                release(owned)
            if passed%100==0:print(f'Vulkan pairs {start}..{index}: {passed} passed',flush=True)
    finally:
        release(outer)
    print(f'PASS: original Vulkan graphics pipeline pairs [{start},{stop}): {passed}',flush=True)
