#!/usr/bin/env python3
"""Guard one observed potential UI-thread detach race in ReXGlue input.

Experimental: the dump identifies an input-listener free on Shutdown,
but does not prove this race was the prior heap corruption's root cause.
"""
import pathlib
import sys

OLD = """void MnkInputDriver::DetachFromWindow() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  window->app_context().CallInUIThreadSynchronous([this, window] {
    // Detach first so nothing new is queued, then run out what already was.
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = nullptr;
    }
"""
NEW = """void MnkInputDriver::DetachFromWindow() {
  // Claim the detach under the mutex before switching to the UI thread.
  rex::ui::Window* window = nullptr;
  {
    std::lock_guard lock(state_mutex_);
    window = attached_window_;
    attached_window_ = nullptr;
  }
  if (!window) {
    return;
  }
  window->app_context().CallInUIThreadSynchronous([this, window] {
    // Drain queued work after the driver is marked detached.
"""
def main():
    if len(sys.argv) != 2:
        print("Usage: patch_sdk_input_lifetime.py SDK_ROOT", file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1]) / "src/input/mnk/mnk_input_driver.cpp"
    try:
        original = path.read_text(encoding="utf-8-sig")
        if NEW in original:
            print("Input lifetime patch already applied")
            return 0
        if original.count(OLD) != 1:
            raise ValueError("Unknown SDK version; refusing unsafe input patch")
        path.write_text(original.replace(OLD, NEW, 1),
                        encoding="utf-8", newline="\n")
        print("Applied experimental MnkInputDriver detach race guard")
    except (ValueError, OSError) as exc:
        print("Input lifetime guard failed:", exc, file=sys.stderr)
        return 1
    return 0
if __name__ == "__main__":
    sys.exit(main())
