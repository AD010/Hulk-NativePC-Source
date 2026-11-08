# Hulk NativeX black frame investigation — 079 log (8 November 2026)

**Do not mark fixed until a real Windows image/playtest.**

Original compilation #42423042903 used an outer Present(dev) without front-buffer r4:
black screen, 0 native completed frames, exhausted 256MiB dynamic heap.

PresentFix #42454997073 reclassified the nested Swap as Present:
- Real native frame events now flow: frame 31,800 at 09:18:34.896,
  1,082 draw calls and 1,082 drawn, 297 shaders, 353 pipelines.
- Heap exhaustion from 077 not present in 079; output still black.
- 31,800 native presents in approximately 76.2 seconds, ~417/s, **not**
  the 30 FPS target; the source of over-presentation remains unproven.
- Log contains invalid fetch constants (wrong texture dimensions in millions
  of pixels), unsupported primitive type 0/49 and index buffers of length 0.
  These are real issues; do not enable generic permissive Xenia flags.
- First nonzero draws are around 09:17:39; full render work begins about
  09:17:43, roughly 25 seconds after initialization, consistent with the
  user's impression of slower loading.

Reviewed actual European PPC guest instruction:
- USA outer Present 0x82880D10 (EU 0x8288C9A0) accepts **dev r3**.
- EU 0x8288CAB4 loads r4=*(dev+14828), EU 0x8288CAB8 calls
  EU nested Swap 0x8288C408 (USA 0x82880778).
- NativeXRenderer calls.inc declares Swap(dev,frontBuffer,params) and
  Present(dev,params), but old src/renderer.cpp OnEnter updated
  g_front_buffer from Present.r4 even though that is not frontBuffer in Hulk.
- Renderer::OnEnter() normally returns early on nested calls. New narrow
  pinned patch handles **Swap before the nested guard** to set
  g_front_buffer=ctx.r4; on **outer Present** it ends the frame and reads
  the proper front-buffer fetch. Guest ABI, draw addresses and mouse remain intact.
- Front buffer composition calls ResolvedTextures::FindNewest() by guest
  fetch address, otherwise clears the output black; sparse telemetry added
  to identify missing resolved front vs valid but empty pixel content.

The same stage also contains guarded MapBuffer writes based on 077 crash
dump; that is crash containment, not proof of stable GPU resource lifetime.

No change to Raw Input, F4 menu, 30/60FPS physics, EU TU1 codegen or
game-data archives. EU Title Update 1 cannot be applied to the USA XEX
because of the verified XEXP RSA signature mismatch.
