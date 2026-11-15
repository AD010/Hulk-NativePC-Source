#!/usr/bin/env python3
"""Opt-in read-only Hulk guest thread creation and affinity evidence.

No game scheduling, guest timing, PPC or Havok state is modified. Logs actual
ExCreateThread and KeSetAffinityThread imports for later USA physics analysis.
Fail closed if the pinned recomp branch's kernel source layout changed.
"""
from __future__ import annotations
import argparse
from pathlib import Path

SOURCE = 'src/kernel/xboxkrnl/xboxkrnl_threading.cpp'
MARKER = 'HULK/CPU-THREAD-PROBE'
INCLUDE = '#include <rex/hook.h>\n'
NAMESPACE = 'namespace rex::kernel::xboxkrnl {\n'
CREATE = '  REXKRNL_IMPORT_RESULT("ExCreateThread", "{:#x} handle={:#x} tid={}", result,'
AFFINITY = '  thread->SetAffinity(affinity);\n  REXKRNL_IMPORT_RESULT("KeSetAffinityThread", "0x0");'
DECL = """// HULK/CPU-THREAD-PROBE: diagnostics only, disabled by default.
REXCVAR_DEFINE_BOOL(hulk_guest_thread_probe, false, "Hulk/CPU",
  "Read-only guest ExCreateThread/KeSetAffinityThread evidence; no physics scheduler changes");

"""
CREATE_REPLACEMENT = """  // HULK/CPU-THREAD-PROBE: register only real guest-created threads.
  // Thread scheduling and Havok timestep remain completely unchanged.
  if (XSUCCEEDED(result) && REXCVAR_GET(hulk_guest_thread_probe)) {
    static std::atomic<uint32_t> thread_count{0};
    const auto n = thread_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 64 || n % 64 == 0) {
      REXKRNL_INFO("[HULK/CPU-XTHREAD] guest_created={} guest_tid={} "
                   "entry={:#x} stack={} flags={:#x} "
                   "ignore_host_affinity={}",
                   n, thread->thread_id(), start_address.guest_address(),
                   actual_stack_size, (uint32_t)creation_flags,
                   rex::cvar::Query<bool>("ignore_thread_affinities"));
    }
  }
""" + CREATE
AFFINITY_REPLACEMENT = """  // HULK/CPU-THREAD-AFFINITY: observed Xbox affinity is not host affinity.
  if (REXCVAR_GET(hulk_guest_thread_probe)) {
    static std::atomic<uint32_t> affinity_count{0};
    const auto n = affinity_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 64 || n % 64 == 0)
      REXKRNL_INFO("[HULK/CPU-AFFINITY] guest_requests={} "
                   "guest_tid={} xbox_mask={:#x} ignore_host_affinity={}",
                   n, thread->thread_id(), (uint32_t)affinity,
                   rex::cvar::Query<bool>("ignore_thread_affinities"));
  }
  thread->SetAffinity(affinity);
  REXKRNL_IMPORT_RESULT("KeSetAffinityThread", "0x0");"""


def patch_text(src: str) -> tuple[str, bool]:
    if MARKER in src:
        required = [DECL, CREATE_REPLACEMENT, AFFINITY_REPLACEMENT, '#include <rex/cvar.h>\n']
        if any(src.count(item) != 1 for item in required):
            raise ValueError('Partial Hulk CPU thread observer installation')
        return src, False
    for anchor in (INCLUDE, NAMESPACE, CREATE, AFFINITY):
        if src.count(anchor) != 1:
            raise ValueError('Unknown XDK kernel thread ABI/source layout')
    src = src.replace(INCLUDE, '#include <rex/cvar.h>\n' + INCLUDE, 1)
    src = src.replace(NAMESPACE, DECL + NAMESPACE, 1)
    src = src.replace(CREATE, CREATE_REPLACEMENT, 1)
    src = src.replace(AFFINITY, AFFINITY_REPLACEMENT, 1)
    return src, True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sdk', type=Path)
    a = p.parse_args()
    path = a.sdk / SOURCE
    result, changed = patch_text(path.read_text(encoding='utf-8-sig'))
    if changed:
        path.write_text(result, encoding='utf-8', newline='\n')
    print('[HULK/CPU] guest thread/affinity observer:',
          'installed' if changed else 'already installed',
          '(default OFF, no game physics modifications)')

if __name__ == '__main__':
    main()
