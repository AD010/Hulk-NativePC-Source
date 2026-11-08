# TU1 EU / Xbox 360 US resource-index compatibility audit

Source evidence from user-provided USA index(1).ind and EU TU1 index.ind.
Actual data (not committed) was parsed with strict BE signed counts.
Overlay category counts: Objects -273, QuickDatas -16, Models -2, Datasets 234.
All 525 TU1 delta records have in-bounds file offsets in extracted ARCs.
US base index contains 524 of the 525 changed resource hash keys.
QuickDatas has one patch-only hash absent from the unpatched US index.
Hashes do not prove object layout or payload equivalence across PAL/US.
EU Title Update 1 XEXP has a different base signature; never apply its
PowerPC delta to the US codegen. The water-jump behavior still requires
function-level reverse engineering and a separately validated guest patch.

Tool: python ci/audit_hulk_tu1_resource_index.py US_INDEX TU1_INDEX TU1_FOLDER.
It emits JSON metadata only; game files stay outside the public repository.

NativeX color Resolve fallback added in separate pinned-src patch
(ci/patch_native_x_hulk_front_fallback.py). It attempts picture
recovery only when the front-fetch lookup misses; not validated yet.
