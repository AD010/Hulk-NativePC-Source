#!/usr/bin/env python3
"""Identify original USA EoR state_r VM native callback for Pause scripts.

Inputs are the user's PRIVATE original Xbox/PC OBJECTS archives and
fully mapped USA PPC guest image. No original code/VM bytecode is output.
This proves a GLOBAL VM state_r method, not the Pause Settings dispatcher.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import zlib

USA_IMAGE_SHA256 = (
    "fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386"
)
XBOX_OBJECTS_SHA256 = (
    "35c9a1d502f62a0fd4e3ef73da204195ead4598f132f21763f66d168d7801815"
)
PC_OBJECTS_SHA256 = (
    "2d080575a4608f6ebf0fb04f1005a3f8b2ec96233fb4cb6e5e790af7d0d4f7b5"
)
GUEST_BASE=0x82000000
STATE_NATIVE_NAME_GUEST=0x8206D25C
STATE_NATIVE_REGISTRATION_GUEST=0x82E2EB8C
STATE_NATIVE_HANDLER_GUEST=0x8264C798
STATE_GETTER_HASH=zlib.crc32(b"STATE_R") & 0xffffffff
METHOD_HEADER_NAME=b"ChangeState_r\x00"
PAUSE_PAGE_NAME=b"UIPage_Pause_Settings\x00"


def original_pause_state(data: bytes,endian: str) -> dict:
    if endian not in ("big","little"):
        raise ValueError("Unexpected original EoR UI VM source endian")
    anchor=data.find(PAUSE_PAGE_NAME)
    if anchor<0 or data.find(PAUSE_PAGE_NAME,anchor+1)>=0:
        raise ValueError("Missing or ambiguous original settings page")
    method=data.find(METHOD_HEADER_NAME,anchor,anchor+1024)
    if method<0:
        raise ValueError("Original Pause ChangeState method absent")
    head=method+len(METHOD_HEADER_NAME)
    if head+21>len(data):
        raise ValueError("Original Pause method truncated")
    size=int.from_bytes(data[head:head+4],endian)
    if size!=13:
        raise ValueError("Original Pause state VM bytecode length changed")
    body=data[head+4:head+4+size]
    crc=int.from_bytes(data[head+4+size:head+8+size],endian)
    if crc!=(zlib.crc32(b"CHANGESTATE_R")&0xffffffff):
        raise ValueError("Original Pause source method CRC invalid")
    if body[4]!=0x28 or int.from_bytes(body[5:9],endian)!=STATE_GETTER_HASH:
        raise ValueError("Unexpected original Pause state VM call identifier")
    return {
        "page_symbol":"UIPage_Pause_Settings",
        "method_symbol":"ChangeState_r",
        "method_source_payload_bytes":size,
        "global_vm_invocation_opcode":"0x28",
        "invoked_original_global_name":"state_r",
        "invoked_global_crc32_upper":"%08X"%STATE_GETTER_HASH,
        "state_r_is_unique_pause_callback":False,
    }


def registered_native_state(image:bytes, strict_source=True)->dict:
    if strict_source and hashlib.sha256(image).hexdigest()!=USA_IMAGE_SHA256:
        raise ValueError("Unknown private mapped USA guest executable")
    if image[:2]!=b"MZ" or len(image)<0x1400000:
        raise ValueError("Not the owner-provided mapped PPC executable")
    def be32(guest):
        at=guest-GUEST_BASE
        if at<0 or at+4>len(image):
            raise ValueError("Original PPC method registry pointer invalid")
        return int.from_bytes(image[at:at+4],"big")
    if image[STATE_NATIVE_NAME_GUEST-GUEST_BASE:
             STATE_NATIVE_NAME_GUEST-GUEST_BASE+8]!=b"state_r\x00":
        raise ValueError("Original registered global VM method name changed")
    if be32(STATE_NATIVE_REGISTRATION_GUEST)!=STATE_NATIVE_NAME_GUEST:
        raise ValueError("Original registered VM entry name pointer drift")
    if be32(STATE_NATIVE_REGISTRATION_GUEST+4)!=STATE_NATIVE_HANDLER_GUEST:
        raise ValueError("Original registered native PPC VM callback drift")
    if (be32(STATE_NATIVE_REGISTRATION_GUEST+8)!=0x82002E4C or
        be32(STATE_NATIVE_REGISTRATION_GUEST+12)!=0x82002E4C):
        raise ValueError("Original VM native type metadata changed")
    # Decode actual PowerPC mr r31,r3 instruction fields at callback+0xC.
    # Only the genuine r3 context register is proven, not a C++ signature.
    word=be32(STATE_NATIVE_HANDLER_GUEST+12)
    if ((word>>26)!=31 or (word&0x7FE)!=0x378 or
        ((word>>21)&31)!=3 or ((word>>16)&31)!=31 or
        ((word>>11)&31)!=3):
        raise ValueError("Original PPC state_r first argument register differs")
    return {
        "native_global_vm_method":"state_r",
        "original_guest_method_registration":"0x%08X"%STATE_NATIVE_REGISTRATION_GUEST,
        "registered_guest_name_pointer":"0x%08X"%STATE_NATIVE_NAME_GUEST,
        "registered_original_native_ppc_handler":"0x%08X"%STATE_NATIVE_HANDLER_GUEST,
        "first_native_ppc_context_arg":"r3",
        "vm_native_parameter_and_return_types_fully_verified":False,
        "actual_pause_page_event_dispatcher_abi_verified":False,
    }


def inspect(xbox:bytes,pc:bytes,image:bytes,strict_source=True)->dict:
    if strict_source:
        if hashlib.sha256(xbox).hexdigest()!=XBOX_OBJECTS_SHA256:
            raise ValueError("Wrong private Xbox OBJECTS source revision")
        if hashlib.sha256(pc).hexdigest()!=PC_OBJECTS_SHA256:
            raise ValueError("Wrong original PC comparator OBJECTS revision")
    xbox_script=original_pause_state(xbox,"big")
    pc_script=original_pause_state(pc,"little")
    if xbox_script!=pc_script:
        raise ValueError("Original Xbox/PC pause state global method mismatch")
    return {
        "schema":"hulk_usa_original_pause_state_r_vm_native_witness_v1",
        "usa_guest_image_sha256":USA_IMAGE_SHA256,
        "original_xbox_objects_sha256":XBOX_OBJECTS_SHA256,
        "original_pc_objects_sha256":PC_OBJECTS_SHA256,
        "verified_original_xbox_pc_pause_method":xbox_script,
        "verified_native_engine_state_getter":registered_native_state(
            image,strict_source),
        "global_vm_getter_is_not_unique_pause_ui_dispatcher":True,
        "no_original_game_instruction_or_script_bytes_published":True,
        "original_ui_page_pause_settings_native_windows_binding_installed":False,
    }


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--private-usa-mapped-image",type=Path,required=True)
    ap.add_argument("--original-xbox-objects",type=Path,required=True)
    ap.add_argument("--original-pc-objects",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    args=ap.parse_args()
    report=inspect(args.original_xbox_objects.read_bytes(),
                   args.original_pc_objects.read_bytes(),
                   args.private_usa_mapped_image.read_bytes())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("[HULK/EOR-PAUSE-VM] source ChangeState_r opcode28 -> "
          "global state_r -> guest PPC 0x8264C798 (r3 context); "
          "not a proven PauseSettings event dispatcher")


if __name__=="__main__":
    main()
