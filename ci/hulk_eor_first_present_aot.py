#!/usr/bin/env python3
"""Install ONLY genuinely compiled, source-matched Hulk Xbox HEORSPV1 VS/PS.

The input ZIP and outputs stay PRIVATE. The original public GitHub checkout
contains no proprietary Xenos programs, translated HLSL or host SPIR-V.
Every shader must satisfy the existing XenosRecomp/DXC + SHA source contract.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import zipfile
import time
from hulk_xenos_aot import (_entries, _private, _source_shaders_for_effects,
                            _source_shaders_for_passes, compile_bank)

META=("HEORMAT2.bin","HEORREF1.bin","HEORSMP1.bin","HEORVRT1.bin")


def prepare_original_bank(source_zip:Path,private_bank:Path,public_repo:Path):
    bank=_private(private_bank,public_repo)
    with zipfile.ZipFile(source_zip) as archive:
        entries=archive.namelist()
        if len(entries)!=len(set(entries)):
            raise ValueError("Duplicate original Xbox source ZIP member")
        manifest=json.loads(archive.read("manifest.json"))
        source=manifest.get("shader_containers")
        if (manifest.get("platform")!="Xbox360" or
            manifest.get("bank_version")!=1 or
            manifest.get("original_effects")!=233 or
            not isinstance(source,list) or len(source)!=5194):
            raise ValueError("Original USA ShaderEffects corpus is incomplete")
        required={"manifest.json",*META}
        for shader in source:
            sha,stage,path=(shader["sha256"],shader["stage"],shader["private_file"])
            if (not isinstance(sha,str) or len(sha)!=64 or
                stage not in ("VS","PS") or path!=f"Xenos/{sha}.xenos.bin"):
                raise ValueError("Unproven original Xenos file identity")
            required.add(path)
        if not required.issubset(set(entries)):
            raise ValueError("Original Xbox private shader or ledger absent")
        bank.mkdir(parents=True,exist_ok=True)
        for name in sorted(required):
            data=archive.read(name)
            if (name.startswith("Xenos/") and
                hashlib.sha256(data).hexdigest()!=Path(name).name.split(".")[0]):
                raise ValueError("Original Xenos shader SHA does not match manifest")
            target=bank/name
            if target.is_file() and target.read_bytes()==data:
                continue
            target.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.NamedTemporaryFile("wb",dir=target.parent,
                  prefix=".hulk_source_",delete=False) as out:
                staged=Path(out.name)
                out.write(data)
            try:
                os.replace(staged,target)
            finally:
                staged.unlink(missing_ok=True)
    _entries(bank) # all 5194 SHA, stages, lengths are reverified independently
    return bank


def _observed_original_passes(path:Path,max_passes:int=0) -> tuple[tuple[int,int],...]:
    """Read ONLY live XDK ShaderContainer-backed effect:pass identities."""
    if not path.is_file() or path.stat().st_size>4*1024*1024:
        raise ValueError("Original private observed-pass demand file missing or oversized")
    if max_passes<0 or max_passes>16384:
        raise ValueError("Invalid maximum original shader pass count")
    observed=set()
    ordered=[]
    for line in path.read_text(encoding="ascii").splitlines():
        if not line.strip():continue
        pieces=line.strip().split(":")
        if len(pieces)!=2:
            raise ValueError("Live original shader request requires effect:pass")
        # The original worker writes unsigned decimal identifiers. CLI also
        # accepts 0x-prefixed hexadecimal when explicitly supplied.
        def u32(raw):
            value=int(raw,16 if raw.lower().startswith("0x") else 10)
            if not 0<=value<2**32:raise ValueError("Invalid original shader identity")
            return value
        key=(u32(pieces[0]),u32(pieces[1]))
        if not key[0] or key[1]>16384:
            raise ValueError("Unverified live original ShaderEffects demand")
        if key not in observed:
            observed.add(key)
            ordered.append(key)
            if max_passes and len(ordered)>=max_passes:
                break
    if not ordered:raise ValueError("No live original ShaderEffects VS/PS demand")
    return tuple(ordered)


def compile_prepared_bank(original:Path,work:Path,install:Path,public_repo:Path,
                          hashes:tuple[int,...],xenos:Path,dxc:Path,common:Path,
                          workers:int=0,passes:tuple[tuple[int,int],...]=()):
    if (not hashes and not passes or
        any(not 0<hash_value<2**32 for hash_value in hashes) or
        any(not 0<effect<2**32 or not 0<=ordinal<=16384
            for effect,ordinal in passes)):
        raise ValueError("Select genuinely OBSERVED ShaderEffects hashes/passes")
    work=_private(work,public_repo)
    install=_private(install,public_repo)
    original=_private(original,public_repo)
    effect_bank=original/"HEORMAT2.bin"
    required=(_source_shaders_for_effects(effect_bank,set(hashes))
              if hashes else set())
    if passes:
        required.update(_source_shaders_for_passes(effect_bank,set(passes)))
    report=compile_bank(original,work/"verified_spirv",xenos,dxc,common,
        public_repo,workers=workers,effect_catalog=effect_bank,
        effect_hashes=hashes,effect_passes=passes)
    completed={row["source_sha256"] for row in report["compiled"]}
    if not required.issubset(completed):
        raise RuntimeError(
            f"Original shader compilation incomplete: {len(required-completed)} "
            "needed original VS/PS programs not compiled")
    # The private bank has the COMPLETE original expected_count but may have
    # compiled only the original VS+PS for the first verified technique.
    # The live renderer still rejects every Draw with a missing real shader.
    result_dir=work/"verified_spirv"
    install.mkdir(parents=True,exist_ok=True)
    to_install={name:original/name for name in META}
    to_install["hulk_shader_bank.bin"]=result_dir/"hulk_shader_bank.bin"
    for name,source in to_install.items():
        data=source.read_bytes()
        target=install/name
        with tempfile.NamedTemporaryFile("wb",dir=install,
              prefix=".hulk_private_install_",delete=False) as out:
            staged=Path(out.name)
            out.write(data)
        try:
            os.replace(staged,target)
        finally:
            staged.unlink(missing_ok=True)
    return {
        "source_only_effects":list(hashes),
        "source_observed_original_passes":sorted(set(passes)),
        "required_original_programs":len(required),
        "compiled_original_programs":len(required),
        "cumulative_authentic_spirv":report["compiled_count"],
        "HEORSPV1_sha256":hashlib.sha256(
            to_install["hulk_shader_bank.bin"].read_bytes()).hexdigest(),
        "private_runtime_directory":str(install),
        "first_onscreen_game_frame_verified":False,
    }


def compile_and_install(source_zip:Path,work:Path,install:Path,public_repo:Path,
                        hashes:tuple[int,...],xenos:Path,dxc:Path,common:Path,
                        workers:int=0,passes:tuple[tuple[int,int],...]=()):
    original=prepare_original_bank(
        source_zip,_private(work,public_repo)/"original_source_bank",public_repo)
    return compile_prepared_bank(original,work,install,public_repo,
                                 hashes,xenos,dxc,common,workers,passes)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    source=p.add_mutually_exclusive_group(required=True)
    source.add_argument("--source-zip",type=Path,
                        help="Original private shader ZIP, extracted once")
    source.add_argument("--bank",type=Path,
                        help="ALREADY EXTRACTED 5194-original-Xenos private bank; no re-extraction")
    p.add_argument("--work",type=Path,required=True)
    p.add_argument("--install",type=Path,required=True)
    p.add_argument("--public-repository",type=Path,required=True)
    p.add_argument("--effect-hash",type=lambda s:int(s,0),action="append",default=[])
    p.add_argument("--effect-pass",action="append",default=[],
                   help="Exact original live Xbox effect:pass (e.g. 0x1234:2)")
    p.add_argument("--observed-passes",type=Path,
                   help="Private HEORREQ1.txt emitted by genuine XDK Draw worker")
    p.add_argument("--max-observed-passes",type=int,default=0,
                   help="Process first N live-observed unique ShaderEffects passes in order; "
                        "0 means all, preserving AOT checkpoints across calls")
    p.add_argument("--xenosrecomp",type=Path,required=True)
    p.add_argument("--dxc",type=Path,required=True)
    p.add_argument("--common",type=Path,required=True)
    p.add_argument("--workers",type=int,default=0)
    p.add_argument("--watch",action="store_true",
                   help="Keep compiling newly observed original XDK passes; "
                        "game/Vulkan worker hot-loads the cumulative HEORSPV1")
    p.add_argument("--watch-interval",type=float,default=2.0,
                   help="Seconds between private original shader requests, 1-60")
    a=p.parse_args()
    if not 1.0<=a.watch_interval<=60.0:
        p.error("Private shader watch interval must be 1-60 seconds")
    if a.watch and not a.observed_passes:
        p.error("--watch requires the private HEORREQ1.txt from the live XDK worker")
    def parse_pair(item:str)->tuple[int,int]:
        raw=item.split(":")
        if len(raw)!=2:raise ValueError("Pass must be effect:ordinal")
        return (int(raw[0],0),int(raw[1],0))
    selected={parse_pair(item) for item in a.effect_pass}
    # Unpack original source at MOST ONCE per invocation. Every later
    # private worker request is compiled against this identical manifest,
    # with strict SHA-256 reuse of the existing HEORSPV1 checkpoints.
    original=( _private(a.bank,a.public_repository) if a.bank else
        prepare_original_bank(a.source_zip,
            _private(a.work,a.public_repository)/"original_source_bank",
            a.public_repository) )
    if a.bank:
        manifest,all_sources=_entries(original)
        if (manifest.get("original_effects")!=233 or
            len(all_sources)!=5194 or
            manifest.get("xenos_vertex_shaders")!=2597 or
            manifest.get("xenos_pixel_shaders")!=2597):
            raise ValueError("Existing original Xbox ShaderEffects bank incomplete")
    previously_seen:tuple[tuple[int,int],...]=()
    last_failure_message=None
    while True:
        requested=set(selected)
        try:
            if a.observed_passes:
                requested.update(_observed_original_passes(
                    a.observed_passes,a.max_observed_passes))
            if not requested:
                if not a.watch:
                    raise ValueError("No authentic live XDK ShaderEffects passes supplied")
            elif not a.watch or tuple(sorted(requested))!=previously_seen:
                result=compile_prepared_bank(
                    original,a.work,a.install,a.public_repository,
                    tuple(a.effect_hash),a.xenosrecomp,a.dxc,a.common,
                    a.workers,tuple(sorted(requested)))
                previously_seen=tuple(sorted(requested))
                last_failure_message=None
                print(json.dumps(result,indent=2),flush=True)
        except (ValueError,RuntimeError,OSError,KeyError) as error:
            # The live guest can start BEFORE its first ShaderEffects Draw
            # or write a temporary incomplete request file. Never install a
            # synthetic shader or discard previously completed real SPIR-V.
            if not a.watch:
                raise
            message=f"{type(error).__name__}: {error}"
            if message!=last_failure_message:
                print(f"[HEORSPV1] {message}",flush=True)
                last_failure_message=message
        if not a.watch:
            break
        try:
            time.sleep(a.watch_interval)
        except KeyboardInterrupt:
            return


if __name__=="__main__":
    main()
