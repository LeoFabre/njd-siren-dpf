# Collector-Waveform Oscillator Model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a circuit-faithful "Physical" oscillator mode to the NJD siren that models the transistor collector waveform (finite-slew exponentials), so the analog timbre and dying-tail vowel emerge from the model instead of an idealized square + polyBLEP — switchable against the current "Classic" path.

**Architecture:** A `Model` param selects, inside `genSample`, between the existing band-limited square (Classic, kept bit-identical) and a new collector-voltage state generator (Physical). In Physical the waveform is band-limited by physics (finite `τc` recovery and `τ_fall` turn-on time constants) + the existing oversampling/FIR-decimator, with no polyBLEP. Time constants are behavioral and adjustable.

**Tech Stack:** C++20 header-only DSP (`dsp/SirenEngine.hpp`), DPF plugin (`plugin/`), NanoVG UI, header-only offline tests built with `c++`, CMake host build (`build-host/`).

**Starting point:** The njd-siren-dpf submodule working tree already contains (uncommitted) the oversampling + windowed-sinc decimator in `SirenEngine.hpp` and the `oversample` param/UI knob, with the rejected formant prototype already rolled back. These tasks build on that state. The spec is committed at `docs/2026-06-17-collector-waveform-model-design.md`.

**Build/test commands (run from `plugins/njd-siren-dpf/`):**
- Engine test: `c++ -std=c++20 -O2 -I dsp -I plugin test/test_siren_engine.cpp -o /tmp/test_siren && /tmp/test_siren`
- VST3 + standalone: `cd build-host && cmake --build . --target Siren-vst3 Siren-jack -j4`

---

### Task 1: Engine — collector-voltage generator behind a `Model` param

Adds the Physical generator and its params to the engine, keeps Classic bit-identical, and enforces an anti-alias oversample floor in Physical. Tests use `SirenEngine::Params` directly (no plugin/UI needed yet).

**Files:**
- Modify: `dsp/SirenEngine.hpp`
- Test: `test/test_siren_engine.cpp`

- [ ] **Step 1: Add the failing Physical-mode tests**

Append these cases to `test/test_siren_engine.cpp`, just before the final `std::printf(failures...)` line:

```cpp
    // 9. Physical model: finite, bounded, audible under power.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true; p.model = 1; p.oversample = 4;
        eng.setParameters(p);
        const float rms = renderRms(eng, blocksFor(3.0f), blocksFor(0.5f));
        CHECK(std::isfinite(rms), "physical: finite output");
        CHECK(rms > 0.02f, "physical: audible level");
        CHECK(rms < 1.5f,  "physical: bounded level");
    }

    // 10. Physical model: no ideal discontinuity. The band-limited collector
    //     waveform must never jump a full square step (2.0) in one sample.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true; p.model = 1; p.oversample = 4; p.toneBtn = true;
        eng.setParameters(p);
        float bufL[kBlock], bufR[kBlock]; float* bufs[2] = { bufL, bufR };
        float prev = 0.0f, maxJump = 0.0f; bool first = true;
        for (int b = 0; b < blocksFor(1.0f); ++b)
        {
            for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
            eng.process(bufs, 2, kBlock);
            if (b < blocksFor(0.2f)) { prev = bufL[kBlock-1]; continue; } // settle
            for (int i = 0; i < kBlock; ++i)
            {
                if (!first) maxJump = std::max(maxJump, std::fabs(bufL[i] - prev));
                prev = bufL[i]; first = false;
            }
        }
        std::printf("      physical max |delta| = %.3f\n", maxJump);
        CHECK(maxJump < 1.5f, "physical: edge softened (no ideal square jump)");
    }

    // 11. Physical model: stall is still silent (C5 empty, SPEED off, no kick).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true; p.model = 1; p.oversample = 4; p.speed = 3;
        eng.setParameters(p);
        const float idle = renderRms(eng, blocksFor(1.0f), blocksFor(0.2f));
        CHECK(idle < 2e-3f, "physical: stalled -> silent");
    }

    // 12. Classic stays bit-identical to a fresh reference (model defaults to 0).
    {
        SirenEngine a, bb;
        a.prepare(kFs, kBlock); bb.prepare(kFs, kBlock);
        SirenEngine::Params pa, pb; pa.power = true; pb.power = true; pb.model = 0;
        a.setParameters(pa); bb.setParameters(pb);
        float aL[kBlock], aR[kBlock], bL[kBlock], bR[kBlock];
        float* ab[2] = { aL, aR }; float* bbf[2] = { bL, bR };
        float maxDiff = 0.0f;
        for (int b = 0; b < blocksFor(1.0f); ++b)
        {
            for (int i = 0; i < kBlock; ++i) { aL[i]=aR[i]=bL[i]=bR[i]=0.0f; }
            a.process(ab, 2, kBlock); bb.process(bbf, 2, kBlock);
            for (int i = 0; i < kBlock; ++i) maxDiff = std::max(maxDiff, std::fabs(aL[i]-bL[i]));
        }
        CHECK(maxDiff == 0.0f, "classic default unchanged");
    }
```

- [ ] **Step 2: Run the test to verify it fails to compile**

Run: `cd plugins/njd-siren-dpf && c++ -std=c++20 -O2 -I dsp -I plugin test/test_siren_engine.cpp -o /tmp/test_siren`
Expected: FAIL — `no member named 'model'` in `SirenEngine::Params`.

- [ ] **Step 3: Add the Physical params to `Params`**

In `dsp/SirenEngine.hpp`, in `struct Params`, after the `oversample` field:

```cpp
        int   oversample   = 1;      // 1/2/4/8 anti-alias factor for osc+tanh
        // Collector-waveform model (circuit-faithful generator).
        int   model        = 0;      // 0 = Classic (square+polyBLEP), 1 = Physical
        float collTau_us   = 45.0f;  // tau_c: collector recovery (Rc*Ccouple), us
        float fallTau_us   = 12.0f;  // tau_fall: turn-on fall time, us
```

- [ ] **Step 4: Extend oversample range to 8x and add the Physical floor**

In `process()`, replace the OS computation line:

```cpp
        const int OS = (p_.oversample >= 4) ? 4 : (p_.oversample >= 2 ? 2 : 1);
```

with:

```cpp
        int OS = (p_.oversample >= 8) ? 8 : (p_.oversample >= 4) ? 4
               : (p_.oversample >= 2) ? 2 : 1;
        if (p_.model == 1 && OS < 2) OS = 2;   // Physical anti-alias floor
```

Bump the FIR capacity for 8x. Change:

```cpp
    static constexpr int kMaxFir = 65;
```

to:

```cpp
    static constexpr int kMaxFir = 129;        // 16*8 + 1 for OS=8
```

- [ ] **Step 5: Add per-block Physical coefficients to `Blk` and compute them**

In `struct Blk`, add to the float members line group:

```cpp
        float lpCoef, hpCoef;
        float collCoef, fallCoef;   // Physical: 1-exp(-dtEff/tau)
        bool  physical;
        float amountTarget, volTarget, pre, driveNorm, bleed;
```

In `process()`, after the `c.hpCoef = ...;` line, add:

```cpp
        c.physical = (p_.model == 1);
        c.collCoef = 1.0f - std::exp(-dtEff / std::max(1e-6f, p_.collTau_us * 1e-6f));
        c.fallCoef = 1.0f - std::exp(-dtEff / std::max(1e-6f, p_.fallTau_us * 1e-6f));
```

- [ ] **Step 6: Branch the oscillator output in `genSample`**

In `genSample`, replace the whole block from `float prevCorr = 0.0f, nowCorr = 0.0f;` through `oscZ_ = naive + nowCorr;` and the following `y *= ramp_;` with:

```cpp
        float oscOut;
        if (c.physical)
        {
            // Collector voltage state: recovers toward Vcc (1) through Rc*Ccouple
            // during the OFF half, pulled to saturation (0) during the ON half.
            // Finite slew on both edges => band-limited by physics, no polyBLEP.
            ph_ += inc;
            if (ph_ >= 1.0f) { ph_ -= 1.0f; halfA_ = !halfA_; }
            const float target = halfA_ ? 1.0f : 0.0f;
            const float coef   = halfA_ ? c.collCoef : c.fallCoef;
            vColl_ += coef * (target - vColl_);
            oscOut = (vColl_ - 0.5f) * 2.0f;   // ~+/-1; HP removes residual DC
        }
        else
        {
            float prevCorr = 0.0f, nowCorr = 0.0f;
            ph_ += inc;
            if (ph_ >= 1.0f)
            {
                const float f = inc > 0.0f ? (1.0f - (ph_ - inc)) / inc : 0.0f;
                ph_ -= 1.0f;
                halfA_ = !halfA_;
                const float d = halfA_ ? 1.0f : -1.0f;  // polyBLEP step/2
                const float a = 1.0f - f;
                prevCorr = d * (1.0f - f) * (1.0f - f);
                nowCorr = d * (2.0f * a - a * a - 1.0f);
            }
            const float naive = halfA_ ? 1.0f : -1.0f;
            oscOut = oscZ_ + prevCorr;          // one-sample delayed + BLEP
            oscZ_ = naive + nowCorr;
        }

        float y = oscOut * ramp_;
```

Note: the `inc` computation and the `Vm`/`th` lines just above stay unchanged and are shared by both paths.

- [ ] **Step 7: Add and reset the `vColl_` state**

In the private member section (near `float ph_`, `oscZ_`), add:

```cpp
    float oscZ_ = 1.0f;
    float vColl_ = 0.0f;   // Physical: collector voltage state [0,1]
```

In `prepare()`, after `oscZ_ = 1.0f;`, add:

```cpp
        vColl_ = 0.0f;
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cd plugins/njd-siren-dpf && c++ -std=c++20 -O2 -I dsp -I plugin test/test_siren_engine.cpp -o /tmp/test_siren && /tmp/test_siren`
Expected: `ALL OK` — all existing cases plus cases 9–12 pass (including "classic default unchanged" and "physical: edge softened").

- [ ] **Step 9: Commit**

```bash
cd plugins/njd-siren-dpf
git add dsp/SirenEngine.hpp test/test_siren_engine.cpp
git commit -m "siren: collector-waveform Physical oscillator model behind Model param"
```

---

### Task 2: Engine — formant-emergence test (Physical vs Classic)

Proves the mechanism: during the dying tail the fixed `τc`/`τ_fall` corners make Physical concentrate **more** energy in the formant band than Classic. Robust because it is a Classic-vs-Physical relative comparison, not an absolute threshold.

**Files:**
- Test: `test/test_siren_engine.cpp`

- [ ] **Step 1: Add a small Goertzel band-energy helper**

Near the top of `test/test_siren_engine.cpp`, after the `blocksFor` helper, add:

```cpp
// Single-bin power via Goertzel, summed over a frequency band, on a window of
// `n` samples ending at the current render position.
static double bandPower(const float* x, int n, float f0, float f1, float step)
{
    double total = 0.0;
    for (float f = f0; f <= f1; f += step)
    {
        const double w = 2.0 * M_PI * f / kFs;
        const double c = 2.0 * std::cos(w);
        double s0 = 0, s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
        total += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return total;
}

// Render the death tail (SPEED off, hold TONE 0.6 s then release) into `out`.
static void renderDeath(SirenEngine::Params base, std::vector<float>& out, float seconds)
{
    SirenEngine eng; eng.prepare(kFs, kBlock);
    const int nb = (int)(seconds * kFs / kBlock);
    float bufL[kBlock], bufR[kBlock]; float* bufs[2] = { bufL, bufR };
    out.clear();
    for (int b = 0; b < nb; ++b)
    {
        SirenEngine::Params p = base;
        p.toneBtn = (b < blocksFor(0.6f));
        eng.setParameters(p);
        for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
        eng.process(bufs, 2, kBlock);
        for (int i = 0; i < kBlock; ++i) out.push_back(bufL[i]);
    }
}
```

Add `#include <vector>` to the includes at the top of the file.

- [ ] **Step 2: Add the failing formant test**

Append before the final `std::printf(failures...)`:

```cpp
    // 13. Formant emergence: in the late death, Physical concentrates a larger
    //     fraction of energy in the 400-1400 Hz formant band than Classic.
    {
        SirenEngine::Params base; base.power = true; base.speed = 3;
        base.tone = 1; base.discharge_s = 3.0f; base.oversample = 4;
        std::vector<float> classic, physical;
        base.model = 0; renderDeath(base, classic, 4.0f);
        base.model = 1; renderDeath(base, physical, 4.0f);

        const int N = 8192, st = (int)(2.6f * kFs);   // deep in the death
        auto frac = [&](const std::vector<float>& v) {
            const float* w = v.data() + st;
            double band = bandPower(w, N, 400.f, 1400.f, 50.f);
            double tot  = bandPower(w, N, 80.f, 8000.f, 50.f);
            return band / (tot + 1e-20);
        };
        const double fc = frac(classic), fp = frac(physical);
        std::printf("      death formant frac: classic=%.2f physical=%.2f\n", fc, fp);
        CHECK(fp > fc * 1.15, "physical: formant band emphasized in death vs classic");
    }
```

- [ ] **Step 3: Run to verify it fails (or passes) and inspect the printed fractions**

Run: `cd plugins/njd-siren-dpf && c++ -std=c++20 -O2 -I dsp -I plugin test/test_siren_engine.cpp -o /tmp/test_siren && /tmp/test_siren`
Expected: the `death formant frac` line prints two numbers. If `physical` is not at least 1.15× `classic`, the model needs tuning — adjust `collTau_us`/`fallTau_us` defaults in Task 1 Step 3 (try `collTau_us` 50–60, `fallTau_us` 8–10) and re-run until the assert passes. This is the design's validation gate.

- [ ] **Step 4: Commit**

```bash
cd plugins/njd-siren-dpf
git add test/test_siren_engine.cpp
git commit -m "siren: test formant emerges in Physical death vs Classic"
```

---

### Task 3: Plugin — expose Model / Coll Tau / Fall Tau params + 8x oversample

**Files:**
- Modify: `plugin/ParameterMetadata.hpp`
- Modify: `plugin/SirenPlugin.cpp`

- [ ] **Step 1: Add the enum entries**

In `plugin/ParameterMetadata.hpp`, in `enum class Param`, replace:

```cpp
    // Anti-alias, opt-in (default 1x = original behavior)
    oversample,  // anti-alias factor for osc+tanh: 0=1x, 1=2x, 2=4x
    // Output params
    ledLevel,    // T5/LED1: follows C5 (the sweep), drives the panel LED
};
```

with:

```cpp
    // Anti-alias + circuit-faithful generator
    oversample,  // 0=1x, 1=2x, 2=4x, 3=8x
    model,       // 0 = Classic (square+polyBLEP), 1 = Physical (collector)
    collTau,     // tau_c collector recovery (us), Physical only
    fallTau,     // tau_fall turn-on fall (us), Physical only
    // Output params
    ledLevel,    // T5/LED1: follows C5 (the sweep), drives the panel LED
};
```

- [ ] **Step 2: Update the param counts**

In `plugin/ParameterMetadata.hpp`, change:

```cpp
constexpr int kNumControlParams = 17;
```

to:

```cpp
constexpr int kNumControlParams = 20;
```

- [ ] **Step 3: Add the choice lists and table rows**

In `plugin/ParameterMetadata.hpp`, replace the oversample choices line:

```cpp
static const char* kChoicesOS[]    = { "1x", "2x", "4x" };
```

with:

```cpp
static const char* kChoicesOS[]    = { "1x", "2x", "4x", "8x" };
static const char* kChoicesModel[] = { "Classic", "Physical" };
```

Then in `kParams[]`, replace the oversample row and the ledLevel row:

```cpp
    // 16 oversample (0=1x, 1=2x, 2=4x)
    { "oversample","Oversample", 0.0f,  2.0f,    0.0f, false, true,  false, false, 3, kChoicesOS },
    // 17 ledLevel (output)
    { "ledLevel",  "LED Level",  0.0f,   1.0f,   0.0f, false, false, true,  false, 0, nullptr },
```

with:

```cpp
    // 16 oversample (0=1x, 1=2x, 2=4x, 3=8x)
    { "oversample","Oversample", 0.0f,  3.0f,    0.0f, false, true,  false, false, 4, kChoicesOS },
    // 17 model (0=Classic, 1=Physical)
    { "model",     "Model",      0.0f,  1.0f,    0.0f, false, true,  false, false, 2, kChoicesModel },
    // 18 collTau (us)
    { "collTau",   "Coll Tau",  10.0f, 120.0f,  45.0f, false, false, false, false, 0, nullptr },
    // 19 fallTau (us)
    { "fallTau",   "Fall Tau",   2.0f,  60.0f,  12.0f, false, false, false, false, 0, nullptr },
    // 20 ledLevel (output)
    { "ledLevel",  "LED Level",  0.0f,   1.0f,   0.0f, false, false, true,  false, 0, nullptr },
```

- [ ] **Step 4: Map the new params in the plugin**

In `plugin/SirenPlugin.cpp`, in the `SirenEngine::Params p { ... }` initializer, replace:

```cpp
            // 0=1x, 1=2x, 2=4x  ->  factor 1/2/4
            .oversample   = 1 << (int) params_[idx(Param::oversample)],
        };
```

with:

```cpp
            // 0=1x .. 3=8x  ->  factor 1/2/4/8
            .oversample   = 1 << (int) params_[idx(Param::oversample)],
            .model        = (int) params_[idx(Param::model)],
            .collTau_us   = params_[idx(Param::collTau)],
            .fallTau_us   = params_[idx(Param::fallTau)],
        };
```

- [ ] **Step 5: Build the VST3 to verify it compiles and exposes the params**

Run: `cd plugins/njd-siren-dpf/build-host && cmake --build . --target Siren-vst3 -j4`
Expected: `Built target Siren-vst3` with no errors.

- [ ] **Step 6: Commit**

```bash
cd plugins/njd-siren-dpf
git add plugin/ParameterMetadata.hpp plugin/SirenPlugin.cpp
git commit -m "siren: expose Model / Coll Tau / Fall Tau params + 8x oversample"
```

---

### Task 4: UI — MODEL / COLL / FALL dev knobs

Adds three dev-strip knobs. `Model` is detented (Classic/Physical); `Coll Tau`/`Fall Tau` are continuous. Relayout the dev strip for 10 knobs across the 740 px faceplate.

**Files:**
- Modify: `plugin/SirenUI.cpp`

- [ ] **Step 1: Add the knobs to the dev strip and relayout**

In `plugin/SirenUI.cpp`, replace:

```cpp
    { Param::amount,     "AMOUNT", "%"  },
    { Param::oversample, "OVSMP",  "x"  },   // anti-alias 1x/2x/4x
};
static constexpr int kNumDevKnobs = 7;
static constexpr float kDevY = 640.f;
static constexpr float kDevR = 16.f;
static inline float devKnobX(int i) { return 70.f + 100.f * (float) i; }
```

with:

```cpp
    { Param::amount,     "AMOUNT", "%"  },
    { Param::oversample, "OVSMP",  "x"  },   // anti-alias 1x/2x/4x/8x
    { Param::model,      "MODEL",  ""   },   // Classic / Physical
    { Param::collTau,    "COLL",   "us" },   // tau_c (Physical)
    { Param::fallTau,    "FALL",   "us" },   // tau_fall (Physical)
};
static constexpr int kNumDevKnobs = 10;
static constexpr float kDevY = 640.f;
static constexpr float kDevR = 14.f;
static inline float devKnobX(int i) { return 48.f + 72.f * (float) i; }
```

- [ ] **Step 2: Make `model` a detented knob**

In `plugin/SirenUI.cpp`, in `onMouse`, replace:

```cpp
        if (i == idx(Param::tone) || i == idx(Param::mode) || i == idx(Param::speed)
            || i == idx(Param::oversample))
        {
```

with:

```cpp
        if (i == idx(Param::tone) || i == idx(Param::mode) || i == idx(Param::speed)
            || i == idx(Param::oversample) || i == idx(Param::model))
        {
```

- [ ] **Step 3: Show the model label as text instead of a number**

In `plugin/SirenUI.cpp`, in `drawDevKnob`, replace:

```cpp
    char buf[32];
    if (paramIdx == idx(Param::oversample))
        std::snprintf(buf, sizeof(buf), "%dx", 1 << (int) values_[paramIdx]);
    else if (pi.max >= 1000.f)
```

with:

```cpp
    char buf[32];
    if (paramIdx == idx(Param::model))
        std::snprintf(buf, sizeof(buf), "%s",
                      (int) values_[paramIdx] == 0 ? "CLAS" : "PHYS");
    else if (paramIdx == idx(Param::oversample))
        std::snprintf(buf, sizeof(buf), "%dx", 1 << (int) values_[paramIdx]);
    else if (pi.max >= 1000.f)
```

- [ ] **Step 4: Build the standalone to verify the UI compiles**

Run: `cd plugins/njd-siren-dpf/build-host && cmake --build . --target Siren-jack -j4`
Expected: `Built target Siren-jack` with no errors.

- [ ] **Step 5: Launch and eyeball the dev strip**

Run: `cd plugins/njd-siren-dpf/build-host && ./bin/Siren`
Expected: the faceplate shows 10 dev knobs in a row; `MODEL` toggles CLAS/PHYS, `COLL`/`FALL` drag continuously. Close the window when done.

- [ ] **Step 6: Commit**

```bash
cd plugins/njd-siren-dpf
git add plugin/SirenUI.cpp
git commit -m "siren: MODEL / COLL / FALL dev knobs in the faceplate"
```

---

### Task 5: Validation — offline FFT A/B, aliasing check, ear + A53 bench

Throwaway tooling + manual gates. Confirms the design's claims before defaults are frozen. No production code; nothing here is committed except the updated `render_proto.cpp`.

**Files:**
- Modify: `test/render_proto.cpp` (add Classic-vs-Physical death A/B renders)

- [ ] **Step 1: Add Classic-vs-Physical death renders to `render_proto.cpp`**

In `test/render_proto.cpp`, inside `main()` after the existing `dying_tail.wav` block, add:

```cpp
    // ---- C. Classic vs Physical death A/B for listening + FFT.
    for (int model : {0, 1})
    {
        SirenEngine::Params p;
        p.power = true; p.speed = 3; p.tone = 1;
        p.discharge_s = 3.0f; p.oversample = 4; p.model = model;
        auto x = render(p, 4.5f, [](int b, SirenEngine::Params& pp){
            pp.toneBtn = (b < blk(0.6f));
        });
        writeWav(dir + (model ? "death_physical.wav" : "death_classic.wav"), x);
    }
```

- [ ] **Step 2: Build and run the renderer**

Run: `cd plugins/njd-siren-dpf && c++ -std=c++20 -O2 -I dsp -I plugin test/render_proto.cpp -o /tmp/render_proto && /tmp/render_proto`
Expected: writes `death_classic.wav` and `death_physical.wav` (plus the alias/dying WAVs) to `/tmp/siren-proto/`.

- [ ] **Step 3: FFT-inspect that fixed formants emerge and harmonics sweep beneath**

Reuse `/tmp/measure_alias.cpp` (built earlier in the session) or rebuild it:
`c++ -std=c++17 -O2 /tmp/measure_alias.cpp -o /tmp/measure_alias`
Run on a high note rendered in each model (set `p.toneBtn=true`, `p.tone=2` for a sustained note in a scratch render) and confirm Physical aliases no worse than Classic:
`/tmp/measure_alias /tmp/siren-proto/death_classic.wav /tmp/siren-proto/death_physical.wav`
Expected: inharmonic/total for Physical is ≤ Classic. Record the numbers in the commit message.

- [ ] **Step 4: Listen in the standalone (live A/B)**

Run: `cd plugins/njd-siren-dpf/build-host && ./bin/Siren`
Toggle `MODEL` CLAS↔PHYS during a SPEED-OFF death (HOLD on, hold TONE then release). Confirm: Physical sounds warmer/less digital and develops the vowel only as it dies; switching mid-wail is clean. Adjust `COLL`/`FALL` to taste and note good defaults.

- [ ] **Step 5: A53 bench (cost of structural oversampling)**

Cross-build and deploy per the repo's njd VST3 workflow, then measure the siren's per-block cost on the Bela at OS 4× Physical vs Classic. Bench natively on the A53 (the M1/clang masks NEON gains; GCC 12.2 on the Bela does not auto-vectorise). Record headroom; if 4× is too costly, evaluate 2× Physical or a slower `fallTau` (fewer images to resolve).

- [ ] **Step 6: Commit the renderer change**

```bash
cd plugins/njd-siren-dpf
git add test/render_proto.cpp
git commit -m "siren: Classic-vs-Physical death A/B render for validation"
```

---

## Notes for the implementer

- The engine is header-only; the offline tests are the fast inner loop. Build the VST3/standalone only at the Task 3/4 verification steps.
- Keep Classic strictly untouched — test 12 ("classic default unchanged") and the original tests 1–8 are the regression gate. If any change makes them fail, the Physical branch leaked into the Classic path.
- Time-constant defaults (`collTau_us=45`, `fallTau_us=12`) are starting points. Task 2 Step 3 and Task 5 Step 4 are where they get tuned. Do not freeze them as "correct" without the FFT + ear gates.
- Do not push to the submodule remote unless the user asks; commits stay local until validation passes.
- **`edge` (Edge LP) in Physical:** the spec says it is "ignored" in Physical. The plan leaves the output-chain LP active in both modes — in Physical it is largely redundant with `τc` and transparent at its default. A hard per-mode disable is deferred (YAGNI): it offers no audible benefit and adds a branch. Revisit only if `edge` audibly fights `τc` during tuning.
- **Physical oversample floor vs "default 4×":** the engine enforces a hard **2× floor** in Physical (anti-alias guard); the spec's "default 4×" is the *recommended* setting and is what the tests/validation use, not an auto-set param. Auto-bumping the `oversample` param to 4× when `Model` flips to Physical is deferred (would need param coupling and surprises the user). Recommend 4× via preset/UX instead.
