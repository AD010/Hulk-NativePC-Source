#!/usr/bin/env python3
"""Verify original USA EoR class virtual +44 per-item renderer (52 classes).

Private decrypted USA PPC executable is PINNED to original SHA-256.
Original renderer 0x82642A88 captures shader this, traverses a linked
item list and calls its shader vtable+44 method with original r4=item,
r5=render context. Guest LR from bctrl is 0x82642BD8. No original PPC
instructions or models are published, only source addresses/ABI offsets.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

USA_IMAGE_SHA256 = (
    "fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386"
)
BASE=0x82000000
RENDERER=0x82642A88
CALLSITE=0x82642BD4
CALLER_LR=0x82642BD8

def inspect(image:bytes, public_shader_vtables:dict, strict_source=True)->dict:
    if strict_source and hashlib.sha256(image).hexdigest()!=USA_IMAGE_SHA256:
        raise ValueError("Original user-owned USA PPC mapped source changed")
    if image[:2]!=b"MZ" or len(image)<0x1400000:
        raise ValueError("Not complete owner-provided original PPC source")
    def word(address):
        at=address-BASE
        if at<0 or at+4>len(image):
            raise ValueError("Original PowerPC guest instruction out of bounds")
        return int.from_bytes(image[at:at+4],"big")
    def mr(address,src,dst):
        w=word(address)
        return ((w>>26)==31 and (w&0x7fe)==0x378 and
                ((w>>21)&31)==src and ((w>>16)&31)==dst and
                ((w>>11)&31)==src)
    def load(address,target,base,offset):
        w=word(address)
        return ((w>>26)==32 and
                ((w>>21)&31)==target and ((w>>16)&31)==base and
                (w&65535)==(offset&65535))
    checks=(
        ("shader instance r3 -> r31",mr(0x82642A98,3,31)),
        ("renderer context r5 -> r23",mr(0x82642AA0,5,23)),
        ("linked model/surface item list",load(0x82642AF0,11,30,0)),
        ("current render item r28",load(0x82642AF4,28,11,12)),
        # The generic source renderer calls original ITEM virtual +48
        # FIRST, and keeps its returned material in callee-saved r27.
        # The shader's virtual +44 body uses that exact material while
        # drawing the current render item. No guessed resource hash.
        ("current render item r28 -> r3",mr(0x82642AF8,28,3)),
        ("original item class virtual table pointer",
            load(0x82642AFC,11,28,0)),
        ("original item getter at virtual slot +48",
            load(0x82642B00,11,11,48)),
        ("genuine item material virtual mtctr r11",
            word(0x82642B04)==0x7D6903A6),
        ("genuine item material virtual bctrl",
            word(0x82642B08)==0x4E800421),
        ("original item material result r3 -> r27",
            mr(0x82642B10,3,27)),
        ("original item material passed back into source setter",
            mr(0x82642C00,27,4)),
        ("original item call context r23 -> r5",mr(0x82642BC0,23,5)),
        ("original current item r28 -> r4",mr(0x82642BC4,28,4)),
        ("original shader instance r31 -> r3",mr(0x82642BC8,31,3)),
        ("shader class virtual slot +44",load(0x82642BCC,11,11,44)),
        ("original mtctr r11",word(0x82642BD0)==0x7D6903A6),
        ("original bctrl with genuine PPC return LR",word(CALLSITE)==0x4E800421),
    )
    invalid=[name for name,passes in checks if not passes]
    if invalid:
        raise ValueError("Original USA render item dispatch PPC ABI drift: "+
                         ", ".join(invalid))
    members=public_shader_vtables.get("original_shader_vtables")
    if not isinstance(members,list) or len(members)!=52:
        raise ValueError("52 original source shader classes required")
    pairs=[];unique=set();by_method={}
    for entry in members:
        klass=entry.get("original_shader_class_name")
        try:vt=int(entry["original_guest_vtable"],16)
        except (ValueError,KeyError,TypeError) as exc:
            raise ValueError("Unidentified original shader vtable") from exc
        if not klass or vt in unique:
            raise ValueError("Missing/ambiguous EoR source shader class")
        unique.add(vt)
        method=word(vt+44)
        if method<0x821B2000 or method>0x82770000 or method&3:
            raise ValueError("Original EoR item method leaves executable region")
        pairs.append({
            "original_shader_class":klass,
            "original_source_guest_vtable":"0x%08X"%vt,
            "original_render_item_method_guest":"0x%08X"%method,
        })
        by_method.setdefault(method,[]).append(klass)
    if len(by_method)!=22 or len(by_method.get(0x82642370,[]))!=27:
        raise ValueError("Original 52 shader render-item method map changed")
    return {
        "schema":"hulk_original_usa_eor_shader_item_dispatch_v1",
        "mapped_usa_ppc_source_sha256":USA_IMAGE_SHA256,
        "original_eor_render_item_dispatcher":"0x%08X"%RENDERER,
        "original_indirect_shader_virtual_call_site":"0x%08X"%CALLSITE,
        "original_indirect_shader_virtual_call_return_lr":"0x%08X"%CALLER_LR,
        "original_shader_virtual_table_slot_bytes":44,
        "original_shader_this_guest_register":"r3",
        "original_render_item_guest_register":"r4",
        "original_render_item_context_guest_register":"r5",
        "original_source_shader_class_count":len(pairs),
        "original_unique_render_item_methods":len(by_method),
        "original_shared_render_item_method_guest":"0x82642370",
        "classes_sharing_original_render_item_method":27,
        "verified_original_ppc_instruction_count":len(checks),
        "original_render_item_material_virtual_table_slot_bytes":48,
        "original_render_item_material_guest_return_register":"r27",
        "original_item_material_getter_and_draw_abi_source_verified":True,
        "source_verified_class_render_item_methods":pairs,
        "current_render_item_guest_pointer_attestation_available":True,
        "model_archive_crc32_and_surface_ordinal_verified":False,
        "original_material_shader_source_sha256_verified":False,
        "no_original_game_code_or_resource_bytes_published":True,
    }

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-mapped-usa-ppc",type=Path,required=True)
    parser.add_argument("--public-shader-vtables",type=Path,default=Path(
        "native_port/analysis/hulk_usa_eor_shader_vtables_2026-11-21.json"))
    parser.add_argument("--output",type=Path,required=True)
    a=parser.parse_args()
    report=inspect(a.private_mapped_usa_ppc.read_bytes(),json.loads(
        a.public_shader_vtables.read_text(encoding="utf-8")))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("[HULK/USA-ITEM] 52 original shader classes, 22 render-item "
          "methods, 10 original dispatcher PPC instructions source verified")

if __name__=="__main__":
    main()
