# njd-siren-dpf

NJD-style dub siren generator (DPF, headless VST3 + LV2) for the nexus-preamp rig.

DSP recipe reverse-engineered from the Ableton Wavetable rack shown in
« le son de sirène analo avec un wavetable !!! » (youtube zmCO6T_ksno):

- Mono PolyBLEP **pulse oscillator**, pitch 60–1500 Hz (exponential `pitch` knob).
- Pitch modulated by **LFO1** (sine/tri/saw/square + shape morph, 1–15 Hz, retriggered
  at 180° on gate-on), **LFO2** (free), and an **attack envelope** (0–800 ms, lick shots).
  Modulation depths use the Ableton matrix scale (100 % = ±48 st): LFO1 = 15→20 %
  coupled to the pitch knob (NJD behavior), env = 18 %, LFO2 = 0–100 %.
  `amount` scales everything 0–200 % (lasers).
- **NJD Flavor**: phase-inverted copy of the oscillator delayed by 1–2 ms summed back
  in — the moving comb notches the real NJD's transistor-reset produces.
- Post: tube drive (tanh), resonant **12 kHz sparkle peak**, 100 Hz high-pass.

The plugin is a 2-in/2-out **insert**: input passes through untouched and the siren is
summed on top, so it drops anywhere in a sushi chain (before Dubwize / reverb sends).
No MIDI input — `trigger`/`hold` are bool params, mapped CC→param in sushi or driven
from the webapp (same pattern as Dubwize's tap button).

## Build (host)

```sh
cmake -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host -j8
ctest --test-dir build-host
```

## Cross-build (Bela)

Same flow as the other nexus plugins, target `Siren-vst3`:

```sh
docker run --rm -v "$(pwd):/workdir" -w /workdir/sushi-on-bela/build-arm64/njd-siren-dpf \
  elk-crossbuild-bookworm bash -c "cmake --build . --target Siren-vst3 -j4"
```

Deploy to `/usr/lib/vst3/Siren.vst3/Contents/aarch64-linux/Siren.so`.
