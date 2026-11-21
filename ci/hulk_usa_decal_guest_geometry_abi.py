#!/usr/bin/env python3
"""Verify original USA EoR Decal per-Draw geometry ABI from PRIVATE PPC.

The original mapped PPC executable remains local to its owner. The
output contains only source addresses, register identities and offsets;
NEVER original PowerPC code, guest memory, shader bytes or model assets.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

USA_IMAGE_SHA256=(
 "fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386")
GUEST_BASE=0x82000000
DECAL_GUEST_METHOD=0x8265B420
DECAL_GUEST_VTABLE=0x82066504
SOURCE_MATERIAL_PREPARE=0x82642C68
ORIGINAL_XDK_SET_VERTEX_STREAM=0x8287E650
ORIGINAL_XDK_SET_INDEX_STREAM=0x8287E7F8
ORIGINAL_XDK_DRAW_INDEXED=0x8288E318
DECAL_DRAW_BL_SITE=0x8265B538


def audit(image:bytes,strict_source=True)->dict:
    if strict_source and hashlib.sha256(image).hexdigest()!=USA_IMAGE_SHA256:
        raise ValueError("Original mapped USA XEX identity changed")
    if image[:2]!=b"MZ" or len(image)<0x1400000:
        raise ValueError("Not the original owner-mapped PPC image")

    def word(address):
        off=address-GUEST_BASE
        if off<0 or off+4>len(image):
            raise ValueError("Original source PPC instruction out of range")
        return int.from_bytes(image[off:off+4],"big")

    def mr(address,src,dest):
        w=word(address)
        return ((w>>26)==31 and (w&0x7fe)==0x378 and
                ((w>>21)&31)==src and ((w>>16)&31)==dest and
                ((w>>11)&31)==src)

    def dform(address,opcode,target,base,disp):
        w=word(address)
        return ((w>>26)==opcode and
                ((w>>21)&31)==target and ((w>>16)&31)==base and
                (w&0xffff)==(disp&0xffff))

    def guest_bl(address,destination):
        w=word(address)
        if (w>>26)!=18 or (w&3)!=1:
            return False
        displacement=w&0x3fffffc
        if displacement&0x2000000:
            displacement-=0x4000000
        return address+displacement==destination

    checks=(
        ("shader this r3 -> r31",mr(0x8265B42C,3,31)),
        ("source collection r7 -> r27",mr(0x8265B448,7,27)),
        ("source item linked node -> r11",dform(0x8265B4CC,32,11,30,0)),
        ("source item owner r4 from node+12",dform(0x8265B4D8,32,4,11,12)),
        ("per-Draw item r4 -> r28",mr(0x8265B4DC,4,28)),
        ("original item material preparation",
            guest_bl(0x8265B4E0,SOURCE_MATERIAL_PREPARE)),
        ("original geometry = BE32(item+60)",
            dform(0x8265B4E4,32,11,28,60)),
        ("original XDK VB descriptor = geometry+24",
            dform(0x8265B4F8,14,5,11,24)),
        ("original XDK SetStreamSource",
            guest_bl(0x8265B500,ORIGINAL_XDK_SET_VERTEX_STREAM)),
        ("original XDK IB descriptor = geometry+56",
            dform(0x8265B50C,14,4,11,56)),
        ("original XDK SetIndices",
            guest_bl(0x8265B510,ORIGINAL_XDK_SET_INDEX_STREAM)),
        ("original XDK DrawIndexed triangle list",
            dform(0x8265B528,14,4,0,4)),
        ("original Xbox baseVertex=0",
            dform(0x8265B524,14,5,0,0)),
        ("original Xbox firstIndex=0",
            dform(0x8265B520,14,6,0,0)),
        ("genuine direct indexed guest Draw",
            guest_bl(DECAL_DRAW_BL_SITE,ORIGINAL_XDK_DRAW_INDEXED)),
        # Verify the *actual original XDK implementation* writes those
        # descriptor pointers into the captured device-shadow fields.
        # Geometry+24 and Geometry+56 are not inferred from one side of
        # the call boundary: this completes the original EoR->XDK->snapshot
        # identity chain with independent PowerPC instructions.
        ("XDK SetStreamSource original r5 -> stored buffer r30",
            mr(0x8287E65C,5,30)),
        ("XDK SetStreamSource original stream-slot r4 -> r29",
            mr(0x8287E664,4,29)),
        ("XDK VB slot (3115+stream_index)*4 from device origin",
            dform(0x8287E6C0,14,11,29,3115)),
        ("XDK SetIndices original r4 -> stored index r29",
            mr(0x8287E808,4,29)),
        ("XDK IB device-shadow original +12436 writes r29",
            dform(0x8287E87C,36,29,31,12436)),
        # Original Decal's 0x82642C68 per-item shader material setup:
        # source r4 is the material-bearing render-item. Its native
        # virtual slot+48 yields the 12-byte original descriptor, which
        # the real 0x825F8A50 binder reads at +0 and +8.
        ("material setup shader this r3 -> r31",
            mr(0x82642C78,3,31)),
        ("original render item r4 -> r30",
            mr(0x82642C7C,4,30)),
        ("shader context original this+48",
            dform(0x82642C88,32,3,31,48)),
        ("original default material binder before new item",
            guest_bl(0x82642C90,0x825F8A50)),
        ("original render item vtable pointer",
            dform(0x82642C94,32,11,30,0)),
        ("original render item virtual material slot+48",
            dform(0x82642C9C,32,11,11,48)),
        ("virtual return r3 becomes material descriptor r4",
            mr(0x82642CA8,3,4)),
        ("material shader context r3 from original this+48",
            dform(0x82642CAC,32,3,31,48)),
        ("original item-specific native material descriptor binder",
            guest_bl(0x82642CB0,0x825F8A50)),
        ("original native binder reads descriptor+8",
            dform(0x825F8A50,32,5,4,8)),
        ("original native binder reads descriptor+0",
            dform(0x825F8A54,32,4,4,0)),
    )
    bad=[name for name,valid in checks if not valid]
    if bad:
        raise ValueError("Original USA Decal PPC ABI differs: "+", ".join(bad))
    return {
        "schema":"hulk_original_usa_decal_render_geometry_abi_v1",
        "original_source_guest_image_sha256":USA_IMAGE_SHA256,
        "original_engine_shader_class":"Decal",
        "original_shader_guest_vtable":"0x%08X"%DECAL_GUEST_VTABLE,
        "original_engine_guest_method":"0x%08X"%DECAL_GUEST_METHOD,
        "original_source_shader_this_register_at_xdk_draw":"r31",
        "original_source_collection_register_at_xdk_draw":"r27",
        "original_source_render_item_register_at_xdk_draw":"r28",
        "original_geometry_guest_ptr_be32_offset_from_render_item":60,
        "original_geometry_vb_descriptor_offset":24,
        "original_geometry_ib_descriptor_offset":56,
        "xdk_device_shadow_original_vb0_guest_offset":12460,
        "xdk_device_shadow_original_ib_guest_offset":12436,
        "actual_original_xdk_setters_prove_device_shadow_binding":True,
        "original_material_prepare_guest_call":"0x%08X"%SOURCE_MATERIAL_PREPARE,
        "original_per_item_material_virtual_slot_bytes":48,
        "original_shader_material_context_source_offset":48,
        "original_material_binding_source_guest_lr":"0x82642CB4",
        "original_default_material_binding_source_guest_lr":"0x82642C94",
        "original_native_material_binding_guest_method":"0x825F8A50",
        "original_material_descriptor_word0_bytes":0,
        "original_material_descriptor_word8_bytes":8,
        "original_decal_item_to_native_material_descriptor_abi_verified":True,
        "original_xdk_set_vb_guest_call":"0x%08X"%ORIGINAL_XDK_SET_VERTEX_STREAM,
        "original_xdk_set_ib_guest_call":"0x%08X"%ORIGINAL_XDK_SET_INDEX_STREAM,
        "original_xdk_draw_guest_call":"0x%08X"%ORIGINAL_XDK_DRAW_INDEXED,
        "original_xdk_draw_ppc_bl_site":"0x%08X"%DECAL_DRAW_BL_SITE,
        "expected_original_guest_lr_at_draw":"0x%08X"%(DECAL_DRAW_BL_SITE+4),
        "source_ppc_register_and_branch_checks_passed":len(checks),
        "engine_decal_to_guest_vb_ib_abi_verified":True,
        "engine_material_effect_pass_shader_pair_verified":False,
        "all_game_code_and_source_assets_remain_private":True,
    }


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--private-usa-mapped-image",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    args=ap.parse_args()
    r=audit(args.private_usa_mapped_image.read_bytes())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(r,indent=2)+"\n",encoding="utf-8")
    print("[HULK/USA-DECAL] original shader->surface->geometry->XDK VB/IB "
          "verified: %d source PPC tests; ShaderEffects pass still unknown"
          %r["source_ppc_register_and_branch_checks_passed"])


if __name__=="__main__":
    main()
