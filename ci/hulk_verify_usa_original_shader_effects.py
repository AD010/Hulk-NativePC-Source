#!/usr/bin/env python3
"""Source-verify entire genuine Hulk USA ShaderEffects archive and samplers.

No game assets, original shaders, disassembled code, model files or original
PDB names are output. The shader source archive remains private. This
proves PDB-authored VS/PS technique pairing and sampler declarations;
it does NOT prove which game material/Draw selected a technique.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import zlib

from audit_hulk_pc_x360_ui_assets import parse_index
from hulk_shader_effect_bank import scan_xenos_containers
from hulk_eor_effect_ledger import _shader_technique
from hulk_eor_shader_samplers import inspect_original_samplers

USA_INDEX_SHA256 = (
    "46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820"
)
USA_EFFECTS_SHA256 = (
    "c335e0ad39be455e34dcf6e02dfbcac74d17184c6a39ceb38411cf9be42e89dd"
)
SAFE_NAME=re.compile(rb"^[A-Za-z][A-Za-z0-9_]{0,95}$")


def inspect(index: bytes, effects: bytes, class_witness: dict) -> dict:
    if hashlib.sha256(index).hexdigest() != USA_INDEX_SHA256 or (
        hashlib.sha256(effects).hexdigest() != USA_EFFECTS_SHA256
    ):
        raise ValueError("Not the owner-supplied source-pinned USA ShaderEffects")
    if len(effects) > 64 << 20:
        raise ValueError("Unsafe original shader archive size")
    directory = parse_index(index)
    if directory["endianness"] != "big":
        raise ValueError("Original Xbox360 index is not big endian")
    source = directory["groups"].get("ShaderEffects", ())
    if len(source)!=233:
        raise ValueError("Wrong original ShaderEffects source count")
    names={}
    parsed_shaders={}
    pairs={}
    techniques=Counter()
    sampler_total=0
    max_sampler=-1
    no_samplers=0
    shaders_with_samplers=0
    for entry in source:
        off,length,reserved=entry["offset"],entry["size"],entry["reserved"]
        if reserved or off<4 or not 0<length<=(2<<20) or (
            off+length>len(effects)
        ):
            raise ValueError("Invalid original effect resource range")
        key=int(entry["hash"],16)
        blob=effects[off:off+length]
        end=blob.find(b"\0",0,97)
        if end<=0 or not SAFE_NAME.fullmatch(blob[:end]):
            raise ValueError("Original EoR effect resource lacks named source")
        name=blob[:end].decode("ascii")
        if (zlib.crc32(name.upper().encode("ascii"))&0xffffffff)!=key:
            raise ValueError("Original EoR ShaderEffects name/CRC mismatch")
        if name in names:
            raise ValueError("Duplicate original EoR source effect name")
        names[name]=key
        stages={"VS":[],"PS":[]}
        for at,stage,source_shader in scan_xenos_containers(blob):
            digest=hashlib.sha256(source_shader).hexdigest()
            technique=_shader_technique(source_shader,stage,name)
            stages[stage].append((at,digest,technique))
            previously=parsed_shaders.get(digest)
            if previously and previously[0]!=stage:
                raise ValueError("Original shader source changed its stage")
            if previously is None:
                slots=inspect_original_samplers(source_shader,stage)
                parsed_shaders[digest]=(stage,slots)
                if slots:
                    shaders_with_samplers+=1
                    sampler_total+=len(slots)
                    max_sampler=max(max_sampler,max(reg for reg,_ in slots))
                else:
                    no_samplers+=1
        if len(stages["VS"])!=len(stages["PS"]):
            raise ValueError("Original effect stage count differs")
        stages["VS"].sort();stages["PS"].sort()
        used=set()
        for ordinal, (v,p) in enumerate(zip(stages["VS"],stages["PS"])):
            if v[2]!=p[2] or v[2] in used:
                raise ValueError("Original source PDB technique pairing mismatch")
            used.add(v[2])
            pair=(v[1],p[1])
            if pair in pairs:
                raise ValueError("Original VS/PS source pair is ambiguous")
            pairs[pair]=(key,ordinal)
            techniques[v[2]]+=1

    entries=class_witness.get("original_shader_vtables")
    if (class_witness.get("schema") !=
        "hulk_usa_original_eor_shader_dispatch_v1" or
        not isinstance(entries,list) or len(entries)!=52):
        raise ValueError("Original USA named ShaderClass vtable proof missing")
    mapped=[];missing=[]
    for shader in entries:
        klass=shader["original_shader_class_name"]
        guesses={klass,klass.removeprefix("E").removesuffix("Shader"),
                 klass.removesuffix("Shader")}
        found=sorted(guesses & names.keys())
        if len(found)==1:
            mapped.append({
                "original_engine_shader_class":klass,
                "original_shader_class_guest_vtable":
                    shader["original_guest_vtable"],
                "same_name_source_effect_crc32":"0x%08X"%names[found[0]],
                "runtime_class_to_effect_binding_verified":False,
            })
        else:
            missing.append(klass)
    return {
        "schema":"hulk_usa_original_shader_source_contract_v1",
        "original_usa_index_sha256":USA_INDEX_SHA256,
        "original_usa_shader_effects_sha256":USA_EFFECTS_SHA256,
        "original_source_effect_records":len(source),
        "effects_with_programs":sum(1 for n in names if
                                    n not in ("ToneMap","Shared")),
        "original_unique_effect_pass_vs_ps_pairs":len(pairs),
        "original_unique_source_shader_containers":len(parsed_shaders),
        "original_unique_authored_techniques":len(techniques),
        "shader_sources_with_sampler_declarations":shaders_with_samplers,
        "shader_sources_with_no_sampler_declarations":no_samplers,
        "total_original_sampler_declarations":sampler_total,
        "largest_original_sampler_register":max_sampler,
        "source_shader_class_name_effect_matches":mapped,
        "source_shader_class_name_effect_matches_count":len(mapped),
        "unmatched_original_shader_classes":missing,
        "all_authored_pairs_and_sampler_slots_source_verified":True,
        "runtime_xdk_program_to_original_source_digest_verified":False,
        "runtime_shader_class_to_effect_hash_verified":False,
        "runtime_effect_pass_to_material_verified":False,
        "runtime_sampler_to_guest_texture_verified":False,
        "no_original_shader_or_other_game_bytes_published":True,
    }


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-usa-index",type=Path,required=True)
    parser.add_argument("--private-shader-effects",type=Path,required=True)
    parser.add_argument("--public-class-vtables",type=Path,default=Path(
        "native_port/analysis/hulk_usa_eor_shader_vtables_2026-11-21.json"))
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    result=inspect(args.private_usa_index.read_bytes(),
                   args.private_shader_effects.read_bytes(),
                   json.loads(args.public_class_vtables.read_text(
                       encoding="utf-8")))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("[HULK/EOR] original authentic VS/PS program pairs: "
          f"{result['original_unique_effect_pass_vs_ps_pairs']}; "
          f"source sampler declarations: "
          f"{result['total_original_sampler_declarations']}; "
          "actual runtime material and menu binding NOT inferred")


if __name__=="__main__":
    main()
