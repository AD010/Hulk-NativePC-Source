#!/usr/bin/env python3
"""Create every private original AOT shader module with a real Vulkan device.
No extraction or public upload of proprietary code. Module acceptance is
not SPIRV-Tools validation, graphics pipeline creation, or an in-game test.
"""
import argparse
import ctypes as C
import sys
import zipfile
from pathlib import Path

U32 = C.c_uint32
Ptr = C.c_void_p
Str = C.c_char_p

class InstanceInfo(C.Structure):
    _fields_ = [('sType', U32), ('pNext', Ptr), ('flags', U32),
                ('app', Ptr), ('layerCount', U32), ('layers', Ptr),
                ('extCount', U32), ('extensions', Ptr)]

class QueueProps(C.Structure):
    _fields_ = [('flags', U32), ('count', U32), ('timestampBits', U32),
                ('minTransferGranularity', U32 * 3)]

class QueueInfo(C.Structure):
    _fields_ = [('sType', U32), ('pNext', Ptr), ('flags', U32),
                ('family', U32), ('count', U32), ('priority', C.POINTER(C.c_float))]

class DeviceInfo(C.Structure):
    _fields_ = [('sType', U32), ('pNext', Ptr), ('flags', U32),
                ('queueCount', U32), ('queues', C.POINTER(QueueInfo)),
                ('layerCount', U32), ('layers', Ptr), ('extCount', U32),
                ('extensions', Ptr), ('features', Ptr)]

class ShaderInfo(C.Structure):
    _fields_ = [('sType', U32), ('pNext', Ptr), ('flags', U32),
                ('size', C.c_size_t), ('code', C.POINTER(U32))]

def check(result, operation):
    if result != 0:
        raise RuntimeError(f'{operation}: VkResult {result}')

def run(archive: Path):
    if C.sizeof(Ptr) != 8:
        raise RuntimeError('This Vulkan probe requires a 64-bit Python process')
    vk = C.WinDLL('vulkan-1.dll') if sys.platform == 'win32' else C.CDLL('libvulkan.so.1')
    def bind(name, result, *args):
        fn = getattr(vk, name)
        fn.restype, fn.argtypes = result, args
        return fn
    create_instance = bind('vkCreateInstance', C.c_int32, C.POINTER(InstanceInfo), Ptr, C.POINTER(Ptr))
    destroy_instance = bind('vkDestroyInstance', None, Ptr, Ptr)
    enumerate_devices = bind('vkEnumeratePhysicalDevices', C.c_int32, Ptr, C.POINTER(U32), C.POINTER(Ptr))
    queues = bind('vkGetPhysicalDeviceQueueFamilyProperties', None, Ptr, C.POINTER(U32), C.POINTER(QueueProps))
    properties = bind('vkGetPhysicalDeviceProperties', None, Ptr, Ptr)
    create_device = bind('vkCreateDevice', C.c_int32, Ptr, C.POINTER(DeviceInfo), Ptr, C.POINTER(Ptr))
    destroy_device = bind('vkDestroyDevice', None, Ptr, Ptr)
    create_module = bind('vkCreateShaderModule', C.c_int32, Ptr, C.POINTER(ShaderInfo), Ptr, C.POINTER(Ptr))
    destroy_module = bind('vkDestroyShaderModule', None, Ptr, Ptr, Ptr)
    instance, device = Ptr(), Ptr()
    counts = {'vs': 0, 'ps': 0}
    try:
        check(create_instance(C.byref(InstanceInfo(sType=1)), None, C.byref(instance)), 'vkCreateInstance')
        n = U32()
        check(enumerate_devices(instance, C.byref(n), None), 'enumerate devices')
        if not n.value:
            raise RuntimeError('No Vulkan physical devices')
        devices = (Ptr * n.value)()
        check(enumerate_devices(instance, C.byref(n), devices), 'enumerate device handles')
        chosen = None
        for physical in devices:
            qn = U32()
            queues(physical, C.byref(qn), None)
            qp = (QueueProps * qn.value)()
            queues(physical, C.byref(qn), qp)
            for i, prop in enumerate(qp):
                if prop.count and prop.flags & 1:  # VK_QUEUE_GRAPHICS_BIT
                    chosen = (physical, i)
                    break
            if chosen:
                break
        if chosen is None:
            raise RuntimeError('No graphics-capable Vulkan queue')
        physical, family = chosen
        # VkPhysicalDeviceProperties begins with five uint32 fields and the
        # 256-byte device name. The rest is deliberately opaque, generously
        # sized and aligned; we only report device name/type.
        raw = (C.c_uint64 * 512)()
        properties(physical, raw)
        prop = bytes(raw)
        name = prop[20:276].split(b'\0', 1)[0].decode('utf-8', 'replace')
        kind = int.from_bytes(prop[16:20], 'little')
        priority = C.c_float(1.0)
        queue = QueueInfo(sType=2, family=family, count=1, priority=C.pointer(priority))
        info = DeviceInfo(sType=3, queueCount=1, queues=C.pointer(queue))
        check(create_device(physical, C.byref(info), None, C.byref(device)), 'vkCreateDevice')
        print(f'Vulkan device: {name}, type={kind}', flush=True)
        with zipfile.ZipFile(archive) as z:
            names = [n for n in z.namelist() if n.endswith(('.vs.spv', '.ps.spv'))]
            if len(names) != 5194 or len(set(names)) != len(names):
                raise ValueError('Expected 5,194 unique original shader entries')
            for filename in names:
                data = z.read(filename)
                if len(data) < 20 or len(data) % 4 or data[:4] != b'\x03\x02\x23\x07':
                    raise ValueError('Invalid SPIR-V module header')
                words = (U32 * (len(data) // 4)).from_buffer_copy(data)
                info = ShaderInfo(sType=16, size=len(data), code=words)
                module = Ptr()
                result = create_module(device, C.byref(info), None, C.byref(module))
                if result != 0 or not module:
                    raise RuntimeError(f'Shader module creation failed at entry {sum(counts.values())}: {result}')
                destroy_module(device, module, None)
                counts[filename.split('.')[-2]] += 1
        if counts != {'vs': 2597, 'ps': 2597}:
            raise ValueError('Original shader stage counts differ')
        print(f'PASS: {counts["vs"]} vertex + {counts["ps"]} pixel shader modules created/destroyed')
    finally:
        if device:
            destroy_device(device, None)
        if instance:
            destroy_instance(instance, None)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive', type=Path, help='private original AOT ZIP')
    run(p.parse_args().archive)
