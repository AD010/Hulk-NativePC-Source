# Hulk XDK 6534: verify native GPU state against working Xenos

Observed log079: NativeX completed 31,800 frames; 1,082 draws per
sampled later frame, but 1D fetch constants decoded as millions of
pixels, primitive types 0/49, 0-sized indices, output remained black.
Those are signs of an invalid/missing GPU register snapshot or driver
state, not proof that a front-texture workaround produces correct pixels.

NativeXRenderer's OWN docs/integrating-a-game.md explicitly require
a trace (F9) and observer verification (originally F10) before
trusting a new XDK profile. POTCAWE/GRAW/Kameo attained 100% observer
match after correcting command buffer ranges, dirty masks, fetch rules.

New opt-in CVar hulk_native_x_verify_only=true works with
native_x_renderer=false. It initializes NativeX trace/observer and
installs PPC hooks, but leaves Xenos presenting the actual game!
This makes mismatch evidence obtainable during visually working
New York gameplay and preserves NativeX dormant by default.
The observer uses the same XDK 6534 profile / actual GPU DRAW observer;
auto capture at frame 900 and F11 manual, F9 D3D trace (PC menu F10).
Once logs show XDK 6534 100% comparison, revisit NativeX pixel output.

This is a verification facility, not a completed renderer fix.
No guest XEX changes, no copyrighted Xbox game assets committed.
NativeX pin SHA bf965e952c98e050753e91d08f32685a9f754b12.
