#!/usr/bin/env python3
"""Keep Xenos PM4 SET/LOAD register values from the validated pre-scan.

NativeX captures unhooked draw gaps by scanning the same guest command buffer
TWICE (prescan and individual draw replay). A ring reused between scans can
change fetch constants or vertex/index state, causing the black polygons in
Hulk #69. Previous shader snapshot fixed ONLY shader bytecode. Preserve all
chronologically ordered SET/LOAD values too, with fail-closed register/type
matching; preserve the Xenos order and all original values (no magic filters).
Apply after ci/patch_native_x_hulk_gap_shader_snapshot.py.
"""
from pathlib import Path
import os
import subprocess
import sys

PIN=os.environ.get("HULK_NATIVEX_SOURCE_SHA","bf965e952c98e050753e91d08f32685a9f754b12")
SUPPORTED={"bf965e952c98e050753e91d08f32685a9f754b12",
           "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
HEAD_OLD="""          const auto hulk_prescan_words = scan_.shader_ucode;
          size_t hulk_prescan_cursor = 0;"""
HEAD_NEW="""          const auto hulk_prescan_words = scan_.shader_ucode;
          // HULK/PM4-REGISTER-SNAPSHOT: validated first-pass SET/LOAD values
          // must not be silently replaced by the reused ring during replay.
          const auto hulk_prescan_writes = scan_.writes;
          const auto hulk_prescan_loads_reg = scan_.loads;
          const auto hulk_prescan_register_order = scan_.register_order;
          size_t hulk_register_cursor = 0;
          const auto hulk_restore_registers = [&]() -> bool {
            for (uint32_t newtag : scan_.register_order) {
              if (hulk_register_cursor >= hulk_prescan_register_order.size())
                return false;
              const uint32_t oldtag =
                  hulk_prescan_register_order[hulk_register_cursor++];
              if ((oldtag & 1u) != (newtag & 1u)) return false;
              if ((oldtag & 1u) != 0u) {
                if ((oldtag >> 1) >= hulk_prescan_loads_reg.size() ||
                    (newtag >> 1) >= scan_.loads.size()) return false;
                auto& now = scan_.loads[newtag >> 1];
                const auto& prior = hulk_prescan_loads_reg[oldtag >> 1];
                if (prior.first != now.first) return false;
                now.second = prior.second;
              } else {
                if ((oldtag >> 1) >= hulk_prescan_writes.size() ||
                    (newtag >> 1) >= scan_.writes.size()) return false;
                auto& now = scan_.writes[newtag >> 1];
                const auto& prior = hulk_prescan_writes[oldtag >> 1];
                if (prior.first != now.first) return false;
                now.second = prior.second;
              }
            }
            return true;
          };
          size_t hulk_prescan_cursor = 0;"""
DRAW_OLD="""            if (!hulk_restore_shader_words()) {
              hulk_shader_replay_ok = false;
              return;
            }
            // Gather without XDK shadow overwrite:"""
DRAW_NEW="""            if (!hulk_restore_shader_words() || !hulk_restore_registers()) {
              // Reject changed packet register/type sequences instead of
              // rendering with a mixed old/new texture or vertex descriptor.
              hulk_shader_replay_ok = false;
              return;
            }
            // Gather without XDK shadow overwrite:"""
TAIL_OLD="""            if (hulk_restore_shader_words()) {
              Gather(p, call, false, base);"""
TAIL_NEW="""            if (hulk_restore_shader_words() &&
                hulk_restore_registers() &&
                hulk_register_cursor == hulk_prescan_register_order.size()) {
              Gather(p, call, false, base);"""
DIAG_OLD='REXLOG_ERROR("[HULK/PM4-SHADER-SNAPSHOT] rejected changed post-draw shader payload");'
DIAG_NEW='REXLOG_ERROR("[HULK/PM4-REGISTER-SNAPSHOT] changed pre-scan register/LOAD sequence or shader payload");'

def change(src: str) -> tuple[str,bool]:
    parts=(HEAD_NEW,DRAW_NEW,TAIL_NEW,DIAG_NEW)
    if all(x in src for x in parts):
        return src,False
    if any(x in src for x in parts) or "HULK/PM4-REGISTER-SNAPSHOT" in src:
        raise ValueError("Partial Hulk validated PM4 register snapshot")
    if "HULK/PM4-PRESCAN-SHADER-SNAPSHOT" not in src:
        raise ValueError("Install pre-scan shader snapshot before register snapshot")
    pairs=((HEAD_OLD,HEAD_NEW),(DRAW_OLD,DRAW_NEW),(TAIL_OLD,TAIL_NEW),
           (DIAG_OLD,DIAG_NEW))
    for old,new in pairs:
        if src.count(old)!=1:
            raise ValueError("NativeX register snapshot anchor drift: "+old[:60])
        src=src.replace(old,new,1)
    return src,True

def main():
    if len(sys.argv)!=2:
        raise SystemExit("Usage: patch_native_x_hulk_gap_register_snapshot.py NativeXRoot")
    root=Path(sys.argv[1]).resolve()
    sha=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],
                                text=True).strip()
    if sha!=PIN or PIN not in SUPPORTED:
        raise ValueError("Unsupported NativeX revision "+sha)
    file=root/"src/state.cpp"
    dest,changed=change(file.read_text(encoding="utf-8"))
    if changed:
        file.write_text(dest,encoding="utf-8",newline="\n")
    print("Hulk PM4 prescan register snapshot: "+
          ("installed" if changed else "already installed"))

if __name__=="__main__":
    main()
