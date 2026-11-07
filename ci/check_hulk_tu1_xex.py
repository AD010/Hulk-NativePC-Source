#!/usr/bin/env python3
"""Read-only XEXP compatibility preflight for CI-pinned Hulk default.xex.

This does NOT install or apply Title Update 1. XEXP requires an exact
RSA-signature match; matching Title ID and Media ID alone are insufficient.
"""
from __future__ import annotations

import hashlib
import pathlib
import sys
from audit_hulk_tu1 import xex_rsa_digest

# Source signature extracted from user's verified EU TU1 STFS/default.xexp.
TU1_XEXP_SOURCE_SHA1 = "8ea1670d19d66c322f50a4d1a6409466a86abada"


def check(path: pathlib.Path) -> bool:
    data = path.read_bytes()
    src = xex_rsa_digest(data).hex()
    actual = hashlib.sha256(data).hexdigest()
    print(f"HULK TU1 base XEX sha256={actual}")
    print(f"HULK TU1 base RSA signature sha1={src}")
    if src != TU1_XEXP_SOURCE_SHA1:
        print("::warning::Hulk TU1 XEXP signature does NOT match the CI XEX. "
              "TU1 is NOT installed. Do NOT apply EU patch to this binary.")
        return False
    print("Hulk TU1 XEXP source signature matches; TU1 remains NOT installed "
          "until pre-codegen patching and ARC/IND overlays are verified.")
    return True


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: check_hulk_tu1_xex.py PATH_TO_DEFAULT.XEX")
    check(pathlib.Path(sys.argv[1]))
