# USA FMOD DSP evidence — 2026-11-28

Original XEX SHA256: 346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0.
Decoded memory-layout PE SHA256: aeb9ff9e6a16f5867b69c0cb626aa8e56d9bd96f820ddd34ab5b25f6faadde29.
Image base 0x82000000, PPC PE machine 0x01f2. The independently pinned audio menu body 0x82339D20 (108 bytes, FNV1a64 b94e5ad803a0e9c5) and thunk 0x8218CF70 (16 bytes, ebe8264ae35ae47e) both matched. No PE bytes, keys or SDK libraries are committed.

## DSP descriptor factories

| Name | Factory | Bytes | Descriptor | Parameters (48 bytes each) | Parameter count | Read callback |
|---|---|---:|---|---|---:|---|
| Distortion | 8279F198 | 208 | 82E352D0 | 82E352A0 | 1 | 8279F108 |
| Highpass | 827A1AA0 | 196 | 82E35678 | 82E35618 | 2 | 827A1918 |
| Lowpass Simple | 827A2260 | 196 | 82E35730 | 82E35700 | 1 | 827A20F0 |
| Lowpass | 827A38C8 | 184 | 82E35900 | 82E358A0 | 2 | 827A38B0 |

Factory lengths are corroborated by .pdata. Factories zero a 132-byte descriptor, set the DSP name and store callback pointers and parameter table/count. Parameter descriptor first three words are big-endian float minimum, maximum, default. Distortion Level 0, 1, .5; Highpass cutoff Hz 1, 22000, 5000; both lowpass cutoff Hz 10, 22000, 5000; second parameter of Highpass/Lowpass is Resonance 1, 10, 1. Do not equate legacy Resonance to biquad Q without additional proof.

Factory SHA256 in table order:
- 9bee92477c6e27d7954dfa2721c52392c048a619157382fe4381ba0db30ae7fa
- d661acbdc40e7cf1e92be456a3df6291c4ce470be07cb7d76da6296360bbedae
- 5a0b400496113cc7e0d95d3fe53a2cc94924643d4ee611d9a99becf821fb2667
- 0e93c82f4792082fdd47d53ae2beae4f0611d4f5789360692104b39137fde2bd

Distortion callback tail branches to 8279EF30. Scalar loop at 8279F054 computes y=x*(1+k)/(1+k*abs(x)), where k=2*level/(1-level) for level<1, otherwise 19994.681640625. Constants verified at 82000428 (1), 82001228 (2), 82085284 (19994.681640625). Native explicit one-shot PCM route uses this transfer function; PPC vector reciprocal-estimate rounding and end-to-end FMOD timing are not verified.

## Event ABI evidence, not callable hooks

Bounded lis/addi or lis/ori string-reference scan produced .pdata-contained candidate functions, not named API bindings: fmod_eventi.cpp 25; fmod_eventparameteri.cpp 4; fmod_eventgroupi.cpp 6; fmod_eventsystemi.cpp 12; fmod_eventprojecti.cpp 2; fmod_sounddef.cpp 2; fmod_eventcategoryi.cpp 5. One additional sounddef reference lacks a confirmed .pdata owner. Example anchors: event 8282E200, parameter 8283B148, group 8283CDD8, system 828413F0, project 82848970, sounddef 82846480, category 82847BA0. The fmodprojects string at 82063EF0 has a candidate owner 825D4510. No signature or runtime replacement is implied. Need complete caller/register/vtable evidence before installing hooks.

Corpus: 988 Distortion, 448 Lowpass Simple, 121 Highpass, 28 Lowpass and 2730 unnamed automation records. Named effect algorithms are available through explicit physical controls; unknown automation IDs, normalized-curve mappings, dynamic DSP scheduling and original event ABI remain unverified. Windows compilation and gameplay are separate gates.

## Lowpass Simple scalar algorithm

USA coefficient routine 0x827A1B98, mono loop 0x827A1D20, stereo loop 0x827A1D90. At cutoff >=22000 Hz the original copies input without updating filter state. Below rate*0.31830987334251404 the coefficient is dt/(1/(cutoff*6.2831854820251465)+dt), dt=1/rate. Above that transition it is 2/3+(cutoff-transition)/(3*(22000-transition)). Two cascaded one-pole states are updated per channel: first=alpha*x+(1-alpha)*first; second=alpha*first+(1-alpha)*second. Constants read from USA image at 82085628, 8200DBF0, 82002408, 820037EC, 82069ED8. Native one-shot implementation now uses these equations. The alternating ~1e-20 denormal bias at 82E357B4 is omitted; no bitwise equivalence is claimed. The state type can retain state across explicit cutoff changes; original event automation and Windows playback remain unverified. Highpass coefficient routine 827A0C68 and Lowpass cascade coefficient routine 827A2FD8 require separate validation; existing biquad approximations are unchanged.

## Highpass scalar algorithm

Coefficient routine 827A0C68, cutoff setter 827A1930, scalar tail 827A10B8. The latter explicitly accumulates the numerator from current and two previous samples, subtracts the two previous output terms, then divides by a0. Coefficients follow the highpass biquad with legacy resonance in sin(w)/(2*resonance). The setter clamps cutoff to rate*0.5-10. The read routine clears history on absolute cutoff changes >=1000 Hz (constant 82002AD8); cutoff <2 copies input; cutoff ==22000 zeros output and history. Native implementation now preserves this scalar recurrence and its edge behavior. The tiny alternating denormal offset and PPC floating-point rounding are not claimed bit-equivalent. Tests cover reset threshold, preserved state, cutoff clamping, bypass, silence, and response. The separate fourth-order FMOD Lowpass and game event ABI still need validation.

## Lowpass fixed-parameter scalar cascade

Revisited the saved USA PE (SHA256 aeb9ff9e6a16f5867b69c0cb626aa8e56d9bd96f820ddd34ab5b25f6faadde29). Prototype initializer at 827A3180 reads damping constants 0.7653669714927673 and 1.8477590084075928 from 82085770 and 8208576C. 827A2FD8 divides each damping by legacy resonance, calls 827A2DE8 twice and accumulates the gains. 827A2C80 uses atan(1)*4 and tan(pi*cutoff/rate) for prewarping, then 827A2DE8 applies a bilinear transform. Mono loop 827A32EC-827A3380 performs two direct-form-II sections, each with numerator 1,2,1 and independently retained history. Native static PCM now uses that fourth-order cascade instead of a single RBJ biquad. GCC and Clang tests cover impulse, DC response, frequency rejection and channel isolation. The admitted fixed-parameter range still excludes near/above Nyquist and resonance below 1. Dynamic coefficient-transition history rescaling at 827A30BC-827A30E8, denormal bias, exact PPC rounding, and in-game automation remain unverified; do not count this as bit-exact or complete event ABI.
