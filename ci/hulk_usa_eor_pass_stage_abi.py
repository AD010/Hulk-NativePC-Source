#!/usr/bin/env python3
"""Prove real original USA EoR material pass -> VS/PS XDK shader ABI.

Source verified from owner-private mapped PPC game image, SHA-pinned.
No source instructions, runtime guest bytes, shader code or assets are
published; output contains original addresses and record offsets ONLY.
This does not equate a 1-based runtime state ID to a ShaderEffects PDB pass.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

USA_SHA=(
    "fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386"
)
BASE=0x82000000


def inspect(image:bytes,strict=True)->dict:
    if strict and hashlib.sha256(image).hexdigest()!=USA_SHA:
        raise ValueError("Wrong original owner-mapped USA PPC source")
    if len(image)<0x1400000 or image[:2]!=b"MZ":
        raise ValueError("Original Xbox PPC guest image incomplete")
    def instruction(guest):
        at=guest-BASE
        if at<0 or at+4>len(image):
            raise ValueError("Original PPC instruction outside USA mapped image")
        return int.from_bytes(image[at:at+4],"big")
    def d(guest,opcode,target,base,displacement):
        w=instruction(guest)
        return ((w>>26)==opcode and
                ((w>>21)&31)==target and
                ((w>>16)&31)==base and
                (w&0xffff)==(displacement&0xffff))
    def bl(guest,target):
        w=instruction(guest)
        if w>>26!=18 or w&3!=1:
            return False
        disp=w&0x03fffffc
        if disp&0x02000000:
            disp-=0x04000000
        return guest+disp==target
    def add(guest,target,a,b):
        w=instruction(guest)
        return ((w>>26)==31 and
                ((w>>21)&31)==target and
                ((w>>16)&31)==a and
                ((w>>11)&31)==b and
                ((w>>1)&1023)==266)
    checks=(
        ("original one-based state slot",d(0x825F84D0,14,11,4,-1)),
        ("original 92-byte per-pass state record",d(0x825F84D4,7,9,11,92)),
        ("original pass table pointer at context+112",d(0x825F84D8,32,8,31,112)),
        ("original VS handle table pointer at context+124",d(0x825F84E0,32,10,31,124)),
        ("original VS index at state record+84",d(0x825F84F4,34,11,30,84)),
        ("VS handle entry array +8 times index",add(0x825F8500,4,11,10)),
        ("genuine VS program descriptor setter",
            bl(0x825F8504,0x82675C70)),
        ("original PS index at state record+85",d(0x825F8508,34,11,30,85)),
        ("original PS handle table pointer at context+136",d(0x825F850C,32,10,31,136)),
        ("PS handle entry array +8 times index",add(0x825F8518,4,11,10)),
        ("genuine PS program descriptor setter",
            bl(0x825F851C,0x82675D20)),
        ("original VS descriptor first object pointer",d(0x82675C90,32,4,30,0)),
        ("original renderer invokes XDK SetVertexShader",
            bl(0x82675C94,0x82874390)),
        ("original PS descriptor first object pointer",d(0x82675D40,32,4,30,0)),
        ("original renderer invokes XDK SetPixelShader",
            bl(0x82675D44,0x82874090)),
        ("original XDK VS shader device shadow +12696",
            d(0x82874430,36,29,30,12696)),
        ("original XDK PS shader device shadow +12692",
            d(0x82874114,36,29,30,12692)),
        ("original shared EoR shader instance material context +48",
            d(0x826432A4,32,3,30,48)),
        ("original shader class invokes state selector",
            bl(0x826432B0,0x825F84C0)),
    )
    failed=[name for name,valid in checks if not valid]
    if failed:
        raise ValueError("Original USA material state/VS/PS XDK source drift: "
                         +", ".join(failed))
    return {
        "schema":"hulk_usa_original_eor_dual_shader_pass_abi_v1",
        "original_mapped_usa_ppc_sha256":USA_SHA,
        "original_material_shader_state_setter":"0x825F84C0",
        "original_runtime_pass_state_is_one_based":True,
        "original_runtime_pass_record_stride":92,
        "original_pass_record_table_guest_offset":112,
        "original_vs_handle_table_guest_offset":124,
        "original_ps_handle_table_guest_offset":136,
        "original_vs_pass_record_index_byte_offset":84,
        "original_ps_pass_record_index_byte_offset":85,
        "original_stage_handle_descriptor_stride":8,
        "original_stage_handle_guest_object_offset":0,
        "original_eor_shader_material_context_guest_offset":48,
        "original_vs_setter_guest_method":"0x82675C70",
        "original_ps_setter_guest_method":"0x82675D20",
        "original_xdk_vs_guest_method":"0x82874390",
        "original_xdk_ps_guest_method":"0x82874090",
        "original_xdk_vs_device_shader_object_offset":12696,
        "original_xdk_ps_device_shader_object_offset":12692,
        "original_source_ppc_instruction_checks":len(checks),
        "engine_pass_vs_ps_xdk_shader_object_identity_verified":True,
        "source_xenos_shader_sha256_to_live_xdk_shader_object_verified":False,
        "runtime_state_index_to_source_pdb_pass_ordinal_verified":False,
        "original_shader_sampler_gpu_texture_binding_verified":False,
        "no_private_code_or_original_shader_data_published":True,
    }


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--private-mapped-usa-ppc",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    report=inspect(args.private_mapped_usa_ppc.read_bytes())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("[HULK/USA-PASS] original EoR state→VS/PS→XDK GPU shader objects; "
          "%d genuine PPC instructions, original source SHA still gated"
          %report["original_source_ppc_instruction_checks"])


if __name__=="__main__":
    main()
