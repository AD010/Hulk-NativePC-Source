#!/usr/bin/env python3
"""Append only the six verified USA EoR banks to a private Windows ZIP.

The flat executable ZIP is verified separately first. No source bank is
added to a public artifact or accepted from an unverified archive.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import zipfile

BANKS = {
    'eor_offline/hulk_shader_bank.bin': (32662388, '994cda2e7027b158de579476657354338a706e0b3fbe61414a2c69b13112ac20'),
    'eor_offline/HEORMAT2.bin': (187000, 'a0ca20fce21a1eb9a5159a40205e0d7f92c970fdf7506a0834432e2d4e20b03d'),
    'eor_offline/HEORREF1.bin': (353208, '1e41834cbf8047fa69cac92a65db5bc1ba157956905d18041f85a473d0a896a1'),
    'eor_offline/HEORSMP1.bin': (598192, '59e304eb096793fc26c2af77a1e8f59ea78b8217405e6f0fb533491690cb72c1'),
    'eor_offline/HEORVRT1.bin': (373984, '82064e54dd6ca98796b08cc92b1f7063321d8e4ca4386bfe861e4569d54f22cd'),
    'eor_offline/HEORCND1.bin': (501060, 'f58d3346f23816d67b40b68e1a770febc96ea85a55be14d35e78e7803bcee123'),
}


def verified_pack(path):
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        if len(entries) != len(BANKS) or set(z.namelist()) != set(BANKS):
            raise ValueError('Unexpected private EoR pack entries')
        data = {}
        for info in entries:
            size, sha = BANKS[info.filename]
            if info.is_dir() or info.file_size != size:
                raise ValueError('Private EoR bank size mismatch: ' + info.filename)
            payload = z.read(info)
            if len(payload) != size or hashlib.sha256(payload).hexdigest() != sha:
                raise ValueError('Private EoR bank fingerprint mismatch: ' + info.filename)
            data[info.filename] = payload
        return data


def append_private_pack(release, pack):
    data = verified_pack(pack)
    release = Path(release)
    with zipfile.ZipFile(release) as z:
        names = z.namelist()
        if len(names) != len({n.casefold() for n in names}) or any('/' in n or '\\' in n for n in names):
            raise ValueError('Expected already-verified flat Windows release ZIP')
        if not {'incredible_hulk.exe', 'incredible_hulk.toml'}.issubset(names):
            raise ValueError('Windows game files absent')
    fd, name = tempfile.mkstemp(prefix='hulk-private-', suffix='.zip', dir=release.parent)
    os.close(fd)
    tmp = Path(name)
    try:
        shutil.copyfile(release, tmp)
        with zipfile.ZipFile(tmp, 'a', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for name, payload in sorted(data.items()):
                z.writestr(name, payload)
        with zipfile.ZipFile(tmp) as z:
            if set(z.namelist()) != set(names) | set(data) or z.testzip() is not None:
                raise ValueError('Combined private release ZIP failed verification')
            for name, payload in data.items():
                if z.read(name) != payload:
                    raise ValueError('Combined private bank changed')
        os.replace(tmp, release)
    finally:
        tmp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    parser.add_argument('pack', type=Path)
    args = parser.parse_args()
    append_private_pack(args.release, args.pack)
    print('[HULK/PRIVATE-PACK] All six authenticated EoR banks included in private release')


if __name__ == '__main__':
    main()
