#!/usr/bin/env python3
"""Reject incomplete Xenos PM4 DRAW_INDX(_2) payloads before native Vulkan capture.

The original scanner incremented out.draws even if the packet had no draw
initiator, or a DMA initiator without both DMA registers, allowing stale
VGT_DMA_BASE/SIZE to be reused by a synthetic draw. This patch is deliberately
fail-closed: real packet data is required, never fabricated.
"""
import os
from pathlib import Path
import subprocess
import sys
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA","bf965e952c98e050753e91d08f32685a9f754b12")
OLD = """  uint32_t q = p + (opcode == 0x22 ? 4 : 0), q_end = p + 4 * count;
  if (q + 4 <= q_end) {
    uint32_t initiator = Load32(base, q);
    out.writes.emplace_back(kVgtDrawInitiator, initiator);
    if (((initiator >> 6) & 3) == 0 && q + 12 <= q_end) {  // source_select kDMA
      out.writes.emplace_back(kVgtDmaBase, Load32(base, q + 4));
      out.writes.emplace_back(kVgtDmaSize, Load32(base, q + 8));
    }
  }
  ++out.draws;"""
NEW = """  // HULK/PM4-PAYLOAD: fail closed on truncated packet data. The older
  // scanner incorrectly counted truncated draws and reused a previous DMA
  // index base/size, potentially drawing arbitrary/stale vertices.
  uint32_t q = p + (opcode == 0x22 ? 4 : 0), q_end = p + 4 * count;
  if (q + 4 > q_end) {
    out.complete = false;
    return;
  }
  const uint32_t initiator = Load32(base, q);
  const bool dma = ((initiator >> 6) & 3) == 0;
  if (dma && q + 12 > q_end) {
    out.complete = false;
    return;
  }
  out.writes.emplace_back(kVgtDrawInitiator, initiator);
  if (dma) {
    out.writes.emplace_back(kVgtDmaBase, Load32(base, q + 4));
    out.writes.emplace_back(kVgtDmaSize, Load32(base, q + 8));
  }
  ++out.draws;"""
CALLBACK_OLD = "          if (on_packet_draw) on_packet_draw();"
CALLBACK_NEW = "          if (on_packet_draw && out.complete) on_packet_draw();"
def patch_text(source):
    complete = NEW in source and CALLBACK_NEW in source and CALLBACK_OLD not in source
    if complete: return source,False
    if NEW in source or CALLBACK_NEW in source:
        raise ValueError("Partially installed PM4 payload bounds validation")
    if source.count(OLD)!=1 or source.count(CALLBACK_OLD)!=2:
        raise ValueError("Unexpected NativeX PM4 payload scanner anchor")
    return (source.replace(OLD,NEW,1).replace(CALLBACK_OLD,CALLBACK_NEW)),True
def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_pm4_draw_payload.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    revision=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if revision!=PIN:raise ValueError("NativeX SHA mismatch: "+revision)
    file=root/"src/state.cpp"
    result,changed=patch_text(file.read_text(encoding="utf-8"))
    if changed:file.write_text(result,encoding="utf-8",newline="\n")
    print("Hulk PM4 complete DMA packet validation:", "patched" if changed else "already installed")
if __name__=="__main__": main()
