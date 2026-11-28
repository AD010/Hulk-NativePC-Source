#!/usr/bin/env python3
"""Apply all verified USA TU1 transfers as one validated codegen unit."""
import argparse
import hashlib
from pathlib import Path
import re
import patch_hulk_tu1_water_origin as origin
import patch_hulk_tu1_water_trajectory as trajectory
import patch_hulk_tu1_water_exit as exit_fix
import patch_hulk_tu1_ledge_region as ledge

FIXES=(origin,trajectory,exit_fix,ledge)

def plan(sources):
    result=dict(sources)
    for fix in FIXES:
        target=re.compile(r'DEFINE_REX_FUNC\(sub_'+f'{fix.OWNER:08x}'+r'\)',re.I)
        matches=[name for name,source in result.items() if target.search(source)]
        if len(matches)!=1:raise ValueError(f'{fix.MARKER}: expected exactly one function file')
        name=matches[0]
        result[name]=fix.patch(result[name])
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('generated',type=Path);ap.add_argument('xex',type=Path);args=ap.parse_args()
    with args.xex.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=origin.PIN:raise SystemExit('TU1 bundle rejected: unexpected USA XEX')
    owners={f'{fix.OWNER:08x}' for fix in FIXES}
    sources={}
    for path in args.generated.glob('*_recomp.*.cpp'):
        source=path.read_text(encoding='utf-8')
        if any(fix.MARKER in source for fix in FIXES) or any(m[1].lower() in owners for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9a-fA-F]{8})\)',source)):
            sources[path]=source
    try:updated=plan(sources)
    except ValueError as exc:
        # Do not publish a release with only a subset of the interdependent
        # water fixes. Fresh unknown generated input also fails explicitly.
        raise SystemExit(f'TU1 bundle rejected; no files modified: {exc}') from exc
    staged=[]
    try:
        for path,source in updated.items():
            if source!=sources[path]:
                tmp=path.with_name(path.name+'.tu1.tmp');staged.append((tmp,path));tmp.write_text(source,encoding='utf-8',newline='\n')
        for tmp,path in staged:tmp.replace(path)
    finally:
        for tmp,path in staged:tmp.unlink(missing_ok=True)
    print('All verified TU1 water and regional transition fixes installed/verified')

if __name__=='__main__':main()
