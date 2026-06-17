# NJD Siren — trim pots & fine-tune reference

How the fine-tune and "dev/physics" parameters of the `njd-siren-dpf` engine alter the
sound, and how they interact. Source of truth: `plugins/njd-siren-dpf/dsp/SirenEngine.hpp`
and `plugins/njd-siren-dpf/plugin/ParameterMetadata.hpp`.

The engine is **one** PolyBLEP square oscillator (the T3/T4 astable) modulated by a **shaped
LFO** (the T1/T2 astable charging C5), plus an output network (2× 106 Hz HP + an edge LP +
post drive). There is no second oscillator, no delay line, no envelope — only a 2 ms power
de-click ramp. Everything below shapes *that one oscillator* and the modulation feeding it.

## Parameter map

| Symbol | Display | Range | Def | In the circuit? | Models |
|---|---|---|---|---|---|
| `charge` | Charge | 0.1–4.0 s (log) | 1.0 | yes | R25·C5 — SIREN wind-up time |
| `discharge` | Discharge | 0.2–8.0 s (log) | 2.0 | yes | R23/R24·C5 — manual / SPEED-OFF fall |
| `amount` | Mod Amount | 0–200 % | 100 | no | mod depth about 4.5 V |
| `drive` | Drive | 0–12 dB | 4.0 | no | post `tanh` saturation |
| `bleed` | Mod Bleed | 0–100 % | 35 | yes | D1/D2 + R8/R9 — half-A source leaking into half-B's pull-up |
| `capRatio` | Cap Ratio | 0.5–2.0 | 1.0 | yes | C1/C2 tolerance — scales half-B period only |
| `vbe` | Vbe | 0.3–1.2 V | 0.65 | yes | transistor threshold — stall point + dive shape |
| `edge` | Edge LP | 500–16000 Hz (log) | 10000 | partial | collector edge rounding (output LP) |

`charge`/`discharge`/`amount`/`drive` are the "musical" fine-tunes; `bleed`/`capRatio`/`vbe`/
`edge` are the "physics trims" that model the tolerances of an individual real unit. All are
host/webapp-only — none are on the NJD faceplate.

---

## The physics trims (the ones that make it sound like a real unit)

### `bleed` — Mod Bleed (0–100 %, def 35)
The clone schematic shows each oscillator base mixing its rate-ladder source with a
220k + diode path (R8/D1, R9/D2). `bleed` is how hard half-A's source drags half-B's pull-up
down (`SirenEngine.hpp:176-177`). Because the diode only conducts when A sits ~0.6 V below B
(half-wave), the **two half-periods stretch at different rates** as the sweep moves → the duty
cycle sweeps continuously → the pulse spectrum's `|sin(π·k·duty)|` notches slide. **This is
"the grain."**
- **Low (0–15 %):** halves nearly independent → cleaner two-tone, less movement, more "buzzer".
- **Mid (~35–50 %):** the classic NJD interaction; notches sweep audibly with pitch.
- **High (70–100 %):** halves strongly coupled → dramatic duty collapse near the bottom of the
  sweep, more vocal/squelchy character, earlier asymmetric stall.
- **Only matters when the two halves have different sources** (MODE 1/2/3). In **MODE 4**
  (`kRoute[3] = {2,2}`, both halves fixed +9 V) `bleed` does nothing.

### `capRatio` — Cap Ratio (0.5–2.0, def 1.0)
C1/C2 mismatch. Scales **half-B's period only** (`SirenEngine.hpp:181-182`). Even with no
modulation this makes the square asymmetric.
- **= 1.0:** symmetric halves.
- **> 1.0:** half-B longer → square leans, adds even harmonics / a "thicker, dirtier" tone,
  slight perceived octave-down weight.
- **< 1.0:** half-B shorter → brighter, thinner, more nasal.
- Interacts with `bleed`: both push the duty off 50 %. `capRatio` is the *static* asymmetry,
  `bleed` is the *dynamic* (sweep-dependent) one. Stack them for an always-leaning square that
  *also* breathes.

### `vbe` — Vbe (0.3–1.2 V, def 0.65)
Transistor switching threshold. Sets **where the oscillator stalls** (dies to silence) and the
**shape of the dying dive** (`tHalf`, `SirenEngine.hpp:246-252`: returns "stalled" once
`Vm < vbe + 0.02`).
- **Low (0.3–0.5):** hangs on longer, dives deeper and slower before dying — long pitch tail.
- **Default (0.65):** silicon-typical.
- **High (0.9–1.2):** stalls early / at higher Vm → the siren cuts out sooner and more
  abruptly; less low-pitch tail.
- This is the main knob for the **death character** on SPEED-OFF / button release.

### `edge` — Edge LP (500–16000 Hz log, def 10000)
The real output chain has no LP (only 2× 106 Hz HP), but the collector edges are slew-limited
in hardware; `edge` is a one-pole standing in for that rounding (`SirenEngine.hpp:128-129,207`).
- **High (10–16 kHz):** sharp edges → bright, buzzy, **and more digital/aliased** in the highs.
- **Low (2–6 kHz):** softer edges → warmer, more analog, **less aliasing**, but loses bite.
- This is your first lever against the "digital in the highs" complaint — at the cost of
  brightness. (A proper fix is oversampling the osc + drive; see notes below.)

---

## The musical fine-tunes

### `charge` (0.1–4 s) — SIREN wind-up
R25·C5 time constant used **only while the SIREN button is held** (`cSlow`,
`SirenEngine.hpp:115,157`). Longer = slower, more dramatic wind-up to the top note.

### `discharge` (0.2–8 s) — fall time
C5 fall toward 0. Two regimes (`SirenEngine.hpp:117-119,158`):
- **Manual / SPEED-OFF / unpowered:** uses the knob directly (`cDisMan`) — this is the speed of
  the "dying" glide you asked about.
- **While the LFO runs:** R24 is partly switched out, so the circuit's 0.22 s is scaled by
  `discharge/2` (`cDisAuto`). So `discharge` also subtly changes the LFO contour's down-slope.

### `amount` (0–200 %) — modulation depth
Scales Vm about 4.5 V (`Vm = 4.5 + amount·(Vm−4.5)`, `SirenEngine.hpp:179`). Not in the
circuit — a depth control.
- **< 100 %:** keeps Vm near 4.5 V → narrower pitch sweep, and **less likely to stall** (the
  siren may never fully die because Vm never reaches `vbe`).
- **> 100 %:** exaggerated sweep, pushes deeper toward the stall → more dramatic death.

### `drive` (0–12 dB) — post saturation
`tanh(pre·x)·driveNorm` after the HP/LP (`SirenEngine.hpp:208`), gain-compensated so loudness
stays roughly constant. Adds harmonics / grit. Interacts with `edge`: high `drive` + high
`edge` = harsh & aliased; high `drive` + low `edge` = warm overdrive.

---

## Synergies cheat-sheet

- **The grain** = `bleed` × `capRatio` × `MODE`. Bleed needs differing half sources (MODE 1/2/3);
  capRatio works in any mode. MODE 4 is a static tone where only capRatio/edge/drive matter.
- **The death** = `vbe` × `discharge` × `amount`. `amount` decides *whether* it dies (Vm reach),
  `vbe` decides *where*, `discharge` decides *how fast*. Tune these three together for the
  SPEED-OFF tail.
- **Brightness vs aliasing** = `edge` × `drive`. Lower `edge` to tame digital highs; raise `drive`
  to put bite back without re-introducing as much alias as `edge` would.
- **Static fatness** = `capRatio` (always-on asymmetry) vs **breathing** = `bleed` (sweep-driven
  asymmetry). Combine for a square that leans *and* moves.

## Tuning recipes

- **Less "digital" in the highs (quick):** `edge` 4000–6000, `drive` 5–7 dB to compensate, and
  prefer `tone` 1/2 over 3 for sustained high notes. (Real fix = 2×–4× oversample the osc + tanh;
  needs a Bela bench — see [[mac-cannot-bench-neon-a53]] — not a trim-pot change.)
- **The dying "o/a" vowel on SPEED-OFF:** the ingredients are present (duty collapse + stall) but
  there are no formant peaks in the path. Push `bleed` 60–80 % and `amount` 120–160 % to make the
  duty collapse harder into narrow vocal pulses, slow `discharge` to 4–6 s and drop `vbe` to
  0.45–0.55 so the dive lingers in the formant region. This *suggests* the vowel; a convincing
  morph likely needs a pair of light fixed formant band-passes (F1/F2) added to the output network
  (out-of-circuit, prototype first — modifies DSP behaviour, not a trim).
- **Classic NJD grain:** `bleed` 40 %, `capRatio` 1.0–1.1, MODE 1 or 2, `edge` 9–11 kHz.
- **Foghorn / static:** MODE 4, `capRatio` 1.3, `edge` 6 kHz, `drive` 6 dB.

## Addressing (sushi / webapp)

Track `siren`, processor **`siren_dsp`** (uid `DBT NJD Siren-1`). Params by **display name**:
`Tone`, `Mode`, `Speed`, `Volume`, `Tone Button`, `Siren Button`, `Trigger`, `Hold`, `Charge`,
`Discharge`, `Mod Amount`, `Drive`, `Mod Bleed`, `Cap Ratio`, `Vbe`, `Edge LP`, output `LED Level`.
Sushi values are normalized 0..1 over the ranges in the table above.
