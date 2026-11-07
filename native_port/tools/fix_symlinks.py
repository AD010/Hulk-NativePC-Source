"""Replaces symbolic links that git checked out as small text files with copies of their targets.

Git on Windows without symlink support writes each link as a file holding the target's path, which then
fails to compile (libmspack's cabextract folder is one). Run once after cloning sdk-src with its submodules.

Usage: python tools/fix_symlinks.py sdk-src
"""
import shutil
import subprocess
import sys
from pathlib import Path


def links(repo):
    out = subprocess.run(["git", "-C", str(repo), "ls-files", "-s"], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        meta, path = line.split("\t", 1)
        if meta.startswith("120000 "):
            yield path


def main():
    root = Path(sys.argv[1]).resolve()
    repos = [root]
    sub = subprocess.run(["git", "-C", str(root), "submodule", "foreach", "--quiet", "--recursive", "echo $displaypath"],
                         capture_output=True, text=True, check=True).stdout
    repos += [root / p for p in sub.split()]
    fixed = 0
    for repo in repos:
        for rel in links(repo):
            link = repo / rel
            if not link.is_file() or link.stat().st_size > 512:
                continue
            target = (link.parent / link.read_text().strip()).resolve()
            if not target.exists() or root not in target.parents:
                print(f"skipped {link} -> {target}")
                continue
            link.unlink()
            if target.is_dir():
                shutil.copytree(target, link)
            else:
                shutil.copyfile(target, link)
            fixed += 1
    print(f"{fixed} links replaced")


if __name__ == "__main__":
    main()
