# Hulk NativePC — Save system parity with official Windows PC edition

Date: 2026-11-11. This is a **required behavior**, not a claim that
saving/loading already works in the current Vulkan #57 build.

## Confirmed official location

The original The Incredible Hulk (2008) Windows PC README states that
saved games are located at:

`C:/Documents and Settings/USER/My Documents/Sega/The Incredible Hulk`
(on Windows XP/2000).

On Windows 10/11 this corresponds to the shell's actual Documents known
folder, then `SEGA/The Incredible Hulk`. The end-user target is:

`<FOLDERID_Documents>/SEGA/The Incredible Hulk/`

Do NOT assume Documents is always `%USERPROFILE%/Documents`, since
Windows can redirect it (OneDrive and other user-selected locations).
Use Unicode Win32 SHGetKnownFolderPath(FOLDERID_Documents).

The manual is more authoritative than third-party references that list
`Documents/The Incredible Hulk` (a path reported for some 1.0 variants).
If an existing official PC install uses a case difference such as
`Sega` instead of `SEGA`, normal Windows case-insensitive filesystem
resolution treats them as the same folder. Do not create two folders
with different case on a case-sensitive volume.

Original README:
https://oldgamesdownload.com/manual/the-incredible-hulk-windows-readme-english/

An independent community comparison reports 1.0 vs 1.1 save path/version
differences. Treat those compatibility notes as reports, not proof of
the Xbox 360/PC binary layout:
https://www.speedrun.com/the_incredible_hulk/forums/v5tmk

Example original PC saves are named `HULK.SAV` or
`HULK_<identifier>.SAV` in some publicly available user reports.
That is **not proof** that a current Xbox-save payload can be written as
that file or loaded by the retail PC version.

## Current ReXGlue dependency — why reconfiguring a folder is insufficient

Inspected `ElGuslml/rexglue-sdk`, branch `recomp`:
- `src/ui/rex_app.cpp` passes `PathConfig.user_data_root` to runtime
  construction; Hulk can change this value in `OnConfigurePaths` or
  `OnFinalizePaths`, but that changes only the root directory.
- `src/system/kernel_state.cpp` passes the root into
  `xam::ContentManager`.
- `src/system/xam/content_manager.cpp` currently maps save-content
  packages into a console layout:
  `root/XUID/TITLE_ID/CONTENT_TYPE/package_name/`.
- `src/kernel/xam/xam_content.cpp` dispatches XamContentCreate/Close,
  enumeration and delete through that Xbox content manager.
- Thus setting `user_data_root = Documents/SEGA/The Incredible Hulk`
  would still put `XUID/TITLE_ID/...` inside the *official PC save
  directory*, risking collisions/confusion and NOT meeting the request.
  We must NOT do that.
- Hulk currently has no verified game-specific XDK save-format adapter
  or proof of PC/USA Xbox progress serialization equivalence.

## Implementation already prepared

`native_port/src/native_pc_save_path.h/.cpp` implements Windows KnownFolder
path resolution and a pure append function. The source is safe to compile
and uses no files or writes. `native_port/tests/native_save_path_test.cpp`
tests correct SEGA folder and OneDrive-style relocated Documents paths.
The existing one CMake input-tests target can run it.

**Not yet connected to guest storage:** this is intentional to prevent
silent overwrites or false success. Existing game saves and current ReXGlue
storage behavior are unchanged.

## Native save implementation gates

**S0** Audit Hulk's exact USA XEX guest XamContent, NtCreateFile and save
function callsites, payload type, package/file name, write commit order,
resume/load behavior and user profiles; no guessed addresses or serializers.

**S1** Separate native PC save root from read-only guest input data,
shader cache and existing ReXGlue/Xbox profile directories. Create the
folder only on an actual successful save operation. Do not delete,
rename, truncate, or overwrite files written by the original PC game.

**S2** Implement a title-specific storage adapter: guest save
operations are translated into native Win32 file operations and
serialized in an understood file format. Validate the exact content
bytes, checksums, region dependencies and loads before selecting any
retail-PC-compatible `HULK*.SAV` filename. Never simply rename STFS
or raw Xbox 360 memory as `.SAV`.

**S3** Atomic temp-write/flush/rename, backup and existing-file
detection; multi-slot continuity and failure recovery. No partial
overwrite of saves after a crash or power cut.

**S4** Verify New Game > mission progress > Save > Close > Relaunch >
Load > next mission across game restarts, independent of Xbox
account/XUID, no emulator save-folder structure, with real Windows
username, OneDrive and redirected Documents path. Cross-compatibility
with original PC 1.0/1.1 should be tested before claiming support.

The full native-port contract
`HULK_FULL_NATIVE_PORT_SYSTEM_CONTRACT_2026-11-11.md`
includes this nonnegotiable save acceptance goal.

Never distribute copyrighted user save data or raw XEX bytes in the
public source repository.

## Save-name safety audit, source main c5576b8

Audited actual SDK recomp revision
`9982a899cad666104c6ec38bd4b2dfacda28e8e4`.
`XCONTENT_DATA::file_name()` called `strlen` on a fixed 42-byte field before
applying `min`. A nonterminated field therefore read beyond its bounds.
The new existing-builder patch uses a bounded search and protects ten content
entry points before open/create/header/thumbnail/delete operations. Names
must be a single Windows component: separators, traversal, alternate data
streams, device aliases and trailing dot/space are rejected, not sanitized.
This prevents distinct names being rewritten to the same target by the patch.

The actual C++ validator and bounded accessor run against full-length fields,
all terminator positions, valid slot names and invalid Windows components.
The transform is tested against the real SDK before and after patching;
unknown/partial layouts are refused. No game build is started.

This is a concrete save-protection change, NOT a native save-backend
implementation. Existing XUID layout is still in use; the Windows Documents
resolver remains unconnected. No save bytes are migrated or rewritten.
Filesystem links, transaction durability, PC-format compatibility and real
save/load continuity still need separate implementation/validation.

## Transactional metadata replacement

The real SDK `WriteContentHeaderFile` previously truncated the live header,
ignored both `fwrite` results and `fclose`, then returned success. The existing
save-name patch now installs the project-owned `native_save_commit.h` and
routes that writer through it. Guest metadata layout and optional license
bytes remain unchanged.

The helper writes into an exclusively created sibling transaction directory,
checks byte count, flush and close, preserves the previous complete header
as `previous`, then replaces the live name. Windows uses Unicode paths,
`_commit` and `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`; POSIX uses fsync
and same-filesystem rename. It never overwrites or prunes older backups.
A failed pre-replacement write leaves the live file unchanged. A failure
after replacement may mean the new file is visible but durability is unknown;
returning an error must not be interpreted as a guaranteed rollback.

Real host filesystem tests passed for first creation, replacement with exact
embedded-NUL bytes, previous-version retention, Unicode parent directory,
invalid targets and symlink rejection. A POSIX child with RLIMIT_FSIZE forces
an actual short-write/flush failure: the call fails and the original complete
header stays byte-identical. All three save regression tests passed against
the real SDK before and after patching. Windows calls still need Windows
execution validation; no game/Actions build was performed.

Scope: metadata only, not the guest's in-place payload writes. One writer
process is required (threads within that process are serialized). No claim
of power-loss safety across every disk/cloud provider, native Documents
routing, retail-PC save compatibility or campaign save/load parity is made.
Those remain blockers to calling the save system release-ready.

## 12 November 2026 — comparative save samples and opt-in Windows package path

The user provided two private RAR5 archives: original recomp XContent saved-game
package (`HULK.SAV` payload 24,544 bytes; header 328 bytes), and official
Windows PC 2008 `HULK.sav` (10,176 bytes). Both share the first 242 hashed
field identifiers after adjusting for BE Xbox and LE Windows record order,
but only 52 of those first 242 record VALUES agree; record layouts diverge
after the initial table. No PC-format conversion or compatible checksum
regeneration has been demonstrated. **Never rename or directly overwrite
retail `HULK.sav` with the XContent payload.**

New `ci/patch_sdk_hulk_native_save_root.py` injects a title-specific,
Windows-only `ContentManager` save directory provider and tests it
against the real SDK source. When opted in via `hulk_native_pc_saves=true`:
`FOLDERID_Documents/SEGA/The Incredible Hulk/NativePC/SavedGames/HULK.SAV/HULK.SAV`
and its XContent metadata under `NativePC/SavedGames/Headers/HULK.SAV.header`.
For Hulk USA title 0x534507F1, saved games type 1 only, paths contain NO XUID
or title ID hierarchy. Other titles, marketplace TU1/DLC, achievements,
caches and game assets keep their existing paths. No file or directory is
created by calculating the root; only actual existing save operations create
files.

**Safety:** This option defaults to **false** to keep existing recomp save
progress visible until a non-destructive import/copy-back procedure is
implemented and verified on both user-provided packages. Enabling this
prematurely would make the old Xbox-path save invisible to the game. The
native root still stores Xbox-format *opaque data*, not the proprietary 2008
Windows serializer. Runtime save/reload, Xbox→PC format conversion and
atomic content payload commits remain separately unverified.

## Exact observed integrity envelope, 12 November

The two owner-provided save payloads were extracted offline from separate RAR5
archives (they are never committed). A byte-level experiment establishes that
**the first 32-bit word in BOTH payloads is CRC32 of all bytes starting at
offset 12**. Xbox stores that checksum in big-endian order; the original
Windows edition stores it in little-endian order. Both actual samples PASS.

The next 32-bit word is different for each sample and is **not yet proven**
to equal the complete file length; don't rewrite it as an assumed length.
The 8-byte hashed-parameter tables start at Xbox byte offset 20 vs Windows
offset 16. The first 242 **field IDs** match in sequence after endian
conversion, while only 52 of the corresponding **field values** match.
The divergence includes object references/pointer-like serialized data.
Thus a cross-platform converter must interpret records, version changes,
object/graph references and unknown checksum/length fields; re-endianing
the entire file or replacing only CRC32 would be unsafe.

Use `python ci/audit_hulk_save_pair.py xbox_HULK.SAV windows_HULK.sav
--output save_pair_audit.json` for a read-only repeatable comparison.
`python ci/test_audit_hulk_save_pair.py` runs four synthetic regressions
without any private game save bytes. The result reports `conversion_ready:
false` until an independently verified serializer and load test exist.

## One-time lossless import, source-only and explicitly authorized

`ci/import_hulk_legacy_save.py` finds a single existing original recomp
Xbox-format XContent package under the known 16-hex-XUID directory and
`534507F1/00000001/HULK.SAV` path. It requires the matching 328-byte
header and the observed Xbox big-endian CRC32; does not invent a PC save.
It dry-runs by default. `--apply` writes a new, private staging directory
containing package and header, fsyncs files, then publishes the package tree
inside `Documents/SEGA/The Incredible Hulk/NativePC/SavedGames` only when
that complete target path does not exist. Existing targets are refused.
The legacy source and the original retail `HULK.sav` are never changed.

```powershell
python ci/import_hulk_legacy_save.py "PATH_TO_LEGACY_incredible_hulk" "PATH_TO_Documents"
# Only after checking the proposal and taking your own backup:
python ci/import_hulk_legacy_save.py "PATH_TO_LEGACY_incredible_hulk" "PATH_TO_Documents" --apply
```

**Verified offline on the owner's actual save archive in a sandbox test
destination:** 24,544-byte payload and 328-byte metadata copied exactly;
both original and destination SHA-256 digests match. Duplicate import and
corrupted-CRC test cases correctly rejected. This is *not* yet proof of
successful in-game reopen or recovery from interrupted writes on Windows.
The `hulk_native_pc_saves` runtime switch remains false until in-game
load/save tests confirm continuity. User is not required to run this manually
until there is a validated opt-in migration path in the future launcher.


## 13 November 2026 — exclude metadata from native package enumeration

Source inspection found a concrete native-path defect: `ResolvePackageRoot`
places `Headers` under `SavedGames`, while `ContentManager::ListContent`
enumerates every subdirectory and invents fallback package metadata whenever
its header cannot be read. Thus the metadata directory itself becomes a bogus
save entry. The SDK adapter now excludes the exact `Headers` directory,
case-insensitively, only when the native Windows root is selected. Legacy
storage and other title/content types retain their original behavior.
The change does not enable native saves, migrate, delete or overwrite files.

Validation: two native-root tests pass against the actual SDK source, including
compiled C++ path/title/metadata-name checks; three import tests also pass.
Windows in-game save/reload and atomic payload writes remain unverified.
