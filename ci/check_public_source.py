#!/usr/bin/env python3
"""Refuse game data, generated guest code and credentials in public Git files."""
from pathlib import PurePosixPath
import re
import subprocess
import sys

BLOCKED_SUFFIXES = {'.xex', '.xexp', '.arc', '.ind', '.iso', '.stfs',
                    '.exe', '.dll', '.pdb', '.zip', '.7z', '.bin', '.pe',
                    '.xcp', '.pak', '.dat'}
BLOCKED_PREFIXES = ('native_port/game/', 'native_port/generated/default/',
                    'native_port/sdk-src/', 'native_port/tu1-overlay/',
                    'native_port/out/', 'native_port/runs/')
KEY = re.compile(rb'(?:github_pat_[A-Za-z0-9_]{25,}|ghp_[A-Za-z0-9]{30,}|-----BEGIN (?:RSA |OPENSSH )?PRIVATE KEY-----)')

def verify():
    paths = subprocess.check_output(['git', 'ls-files', '-z']).split(b'\x00')
    bad = []
    for path in paths:
        if not path:
            continue
        p = path.decode('utf-8', errors='replace').replace('\\', '/')
        if (p.lower().endswith(tuple(BLOCKED_SUFFIXES))
            or any(p.lower().startswith(prefix) for prefix in BLOCKED_PREFIXES)):
            bad.append(p + ' (proprietary/compiled input)')
            continue
        with open(path, 'rb') as source:
            contents = source.read()
        if b'\x00' in contents or KEY.search(contents):
            bad.append(p + ' (non-text or possible credential)')
    if bad:
        print('Public source guard refused:', '; '.join(bad), file=sys.stderr)
        return 1
    print('Source-only public tree verified:', len(paths) - 1, 'tracked entries')
    return 0

if __name__ == '__main__':
    sys.exit(verify())
