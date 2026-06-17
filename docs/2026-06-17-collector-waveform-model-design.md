# NJD Siren — collector-waveform oscillator model (design)

Date: 2026-06-17
Status: design approved; **revised 2026-06-18** (see Revision note)
Scope: `plugins/njd-siren-dpf` (DPF VST3/LV2/standalone), engine `dsp/SirenEngine.hpp`

## Revision 2026-06-18 — collector model is warmth/anti-alias, NOT the vowel

Empirically disproven during implementation: the collector-RC waveform does **not**
produce the dying vowel. A first-order RC charge/discharge has a **monotonic spectral
roll-off**, not a **resonant peak** — and a vowel requires resonant formant peaks.
Measured (Classic vs Physical across the descent, full τ sweep): Physical's spectral
centroid is uniformly **lower** (darker/warmer) and its 400–1400 Hz energy fraction is
always **≤** Classic's — never a formant. The original "two fixed corners → two
formants" reasoning conflated a roll-off knee with a resonant peak.

**Therefore:** the collector model is kept and finished for what it actually delivers —
**warmth + de-aliasing without polyBLEP** (the "digital in the highs" complaint). The
**vowel is deferred** to a separate effort: it most plausibly comes from the
**loudspeaker/cabinet resonance** of the real unit (an always-on physical formant
filter downstream of the PCB — the physically-correct version of the rejected gated
trick). The sections below describing the vowel emerging from the collector are
**superseded** by this note; read them as the (warmth-only) generator design.

## Problem

The real NJD dub siren develops a vowel-like ("o"→"a") coloration as it dies, and
sounds warm/analog where our model sounds "digital" in the highs. Our engine
computes the **right timing** (astable half-periods via `tHalf`) but emits an
**idealized `±1` square** band-limited with **polyBLEP**. That idealization is the
root cause of both shortcomings:

- A perfect square's spectrum is fixed-shape (1/k harmonics) regardless of pitch.
  Duty modulation only moves the `|sin(πk·duty)|` **notches** — never fixed
  **peaks**. So no formants can emerge → no vowel.
- polyBLEP is a non-physical band-limiting trick on an instantaneous edge. Its
  residual aliasing is part of the "digital" character, and it is inconsistent
  with the engine's electronic-modeling paradigm.

A previous prototype added gated formant band-pass filters. **Rejected**: it is a
DSP/sound-design trick (out of paradigm) and the gate leaked outside the death,
breaking authenticity. Rolled back 2026-06-17. The oversampling work was kept.

## Insight

A real circuit has **no instantaneous edges** — every transition has finite slew
(τ = Rc·Ccouple, transistor switching, parasitics), so the waveform is
**inherently band-limited**. Modeling the actual continuous collector waveform
(a) reproduces the warm/analog timbre, (b) makes anti-aliasing emerge from physics
+ oversampling instead of polyBLEP, and (c) produces the vowel **naturally and only
during the death**, with no gate.

## Approach (chosen)

Level-2 **behavioral state model** of the output transistor's collector voltage —
the waveform emerges from the circuit's state equations, without device-level SPICE
(too heavy for the Bela A53) and without idealized squares.

Decisions taken during brainstorming:
- **Time constants are behavioral & adjustable** (we don't have exact schematic Rc /
  coupling values). Anchored on typical orders of magnitude, tuned by ear + FFT.
- **A/B coexistence via a `Model` param** — keep the current square+polyBLEP path
  intact and switchable, zero risk to the working sound during dev.

### Engine model (Model = Physical)

Scope: only the **main oscillator** generator inside `genSample`. Timing (`tHalf`,
half-periods), LFO/C5 shaping, and the output chain (2× 106 Hz HP, drive, volume)
are unchanged.

Replace `naive = ±1` + polyBLEP with a collector-voltage state `Vcoll ∈ [0,1]`
(0 = saturated/ON, 1 = recovered toward Vcc/OFF), advanced per oversampled subsample
(`dtTick_`), driven by the phase boundaries we already compute:

- **OFF half** (transistor cut off) — collector recovers toward Vcc through Rc·Ccouple:
  `Vcoll += (1 - Vcoll) · (1 - exp(-dtTick_/τc))`
- **ON half** (conducting) — collector pulled to saturation:
  `Vcoll += (0 - Vcoll) · (1 - exp(-dtTick_/τ_fall))`

Output `y = Vcoll · ramp_`, then the existing AC chain (2× HP removes DC / recenters).
**No polyBLEP** — both transitions are continuous exponential approaches (finite
slew), band-limited by physics; oversampling resolves the fastest edge (τ_fall).

### Why the vowel emerges (and stays tied to the death)

τc and τ_fall are **fixed** → two **fixed spectral corners** (~1/2πτc, ~1/2πτ_fall).
While the siren is fast, the exponential fills the whole half-period (full timbre).
As it slows toward death, the half-periods (thA/thB) stretch but τc/τ_fall stay fixed
→ the waveform deforms, the corners become prominent, and the **descending harmonics
sweep through them → moving formants = the vowel**. The half-to-half asymmetry
(`capRatio`, `bleed`, already modeled) enriches the structure. Intrinsic to the
period/τ regime → no gate, no out-of-death leakage.

## Parameter surface

- **`Model`** (enum): `0 = Classic` (square + polyBLEP, current path, bit-identical),
  `1 = Physical` (collector). UI: `MODEL` in the dev strip.
- **`Coll Tau`** (τc, µs) and **`Fall Tau`** (τ_fall, µs): adjustable, **active only in
  Physical**. Defaults ~ τc 30–60 µs, τ_fall 5–15 µs (tune by ear + FFT).
- **`edge` (Edge LP)**: kept for Classic; ignored in Physical (τc recovery *is* the
  physical rounding).
- **`Oversample`**: **structural in Physical** — Physical forces a minimum factor
  (default 4×, adjustable 2/4/8) to resolve τ_fall; Classic keeps default 1×. Add 8×
  to the enum for fast τ_fall.
- `bleed`, `capRatio`, `vbe`, `amount`, `drive`, etc. unchanged — they still drive the
  timing, so still meaningful.

## Anti-aliasing

- Physical: no polyBLEP. Band-limiting = finite slew (τ_fall) + oversampling + the
  existing windowed-sinc FIR decimator. OS chosen so 1/2πτ_fall stays below the
  oversampled Nyquist.
- Classic: unchanged (polyBLEP + opt-in oversample), bit-identical to today.

## Validation (before freezing defaults)

1. **FFT offline**: render the death (SPEED OFF, hold TONE then release) in Physical;
   confirm **fixed peaks emerge** and harmonics **sweep beneath them** descending.
   Compare Classic vs Physical (vs a real-unit recording if available).
2. **Aliasing measurement**: high note; confirm Physical+OS aliases no worse than
   Classic+polyBLEP.
3. **By ear** in the standalone (live `Model` toggle).
4. **A53 bench** on the Bela for the structural-OS cost (bench natively on the A53,
   not the Mac).

## Testing

- All existing engine tests must pass in **Classic** mode (bit-identical unchanged).
- New Physical-mode asserts: finite/bounded output; silence at stall; **continuity**
  (no sample-to-sample jump above a threshold — proof there is no discontinuity left);
  a **formant assert**: mid-band spectral density rises during the descent.

## Deliverable

Code on the njd-siren-dpf submodule (uncommitted until validated); standalone rebuilt
for live A/B; throwaway FFT tools for the validation step.

## Out of scope / caveats

- Part of the real unit's vowel may come from the speaker/enclosure resonance (a
  transducer formant, not on the PCB). If so, the collector model won't capture 100%;
  it is the correct first step and likely the bulk.
- No device-level (Ebers-Moll/Gummel-Poon) modeling — too heavy for the A53.
- LFO waveform shape unchanged.
