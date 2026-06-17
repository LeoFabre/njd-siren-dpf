// Offline checks for the NJD circuit model. The interesting behaviors are
// EMERGENT from the astable + C5 physics, so that's what we assert:
// stall-silence when C5 is empty, the discharge tail after a trigger while
// the rocker holds power, two-tone stall gaps, and the SIREN wind-up.
#include "SirenEngine.hpp"
#include <cstdio>
#include <cmath>

using siren::SirenEngine;

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
         else { std::printf("ok:   %s\n", msg); } } while (0)

static constexpr float kFs = 48000.0f;
static constexpr int kBlock = 64;

// Renders numBlocks, returns rms of the blocks after skipBlocks.
static float renderRms(SirenEngine& eng, int numBlocks, int skipBlocks)
{
    float bufL[kBlock], bufR[kBlock];
    float* bufs[2] = { bufL, bufR };
    double acc = 0.0;
    int n = 0;
    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
        eng.process(bufs, 2, kBlock);
        if (b < skipBlocks) continue;
        for (int i = 0; i < kBlock; ++i)
        {
            if (!std::isfinite(bufL[i]) || !std::isfinite(bufR[i]))
                return NAN;
            acc += (double) bufL[i] * bufL[i];
            ++n;
        }
    }
    return (float) std::sqrt(acc / n);
}

static int blocksFor(float seconds) { return (int)(seconds * kFs / kBlock); }

int main()
{
    // 1. Everything off: exact silence (power off, adds nothing).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        eng.setParameters(p);
        CHECK(renderRms(eng, 50, 0) == 0.0f, "all off -> silence");
    }

    // 2. Rocker on, auto siren (mode 1 wail): audible, finite, bounded.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        eng.setParameters(p);
        const float rms = renderRms(eng, blocksFor(3.0f), blocksFor(0.5f));
        CHECK(std::isfinite(rms), "auto siren -> finite output");
        CHECK(rms > 0.03f, "auto siren -> audible level");
        CHECK(rms < 1.5f, "auto siren -> bounded level");
    }

    // 3. TRIG with rocker OFF (auto LFO): TRIG is pure power — sound rises
    //    while held (C5 charges via the LFO), cuts right after release.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.trigBtn = true;
        eng.setParameters(p);
        const float held = renderRms(eng, blocksFor(0.4f), blocksFor(0.1f));
        CHECK(held > 0.03f, "TRIG held -> sound");
        p.trigBtn = false;
        eng.setParameters(p);
        const float after = renderRms(eng, blocksFor(0.1f), blocksFor(0.02f));
        CHECK(after < 5e-3f, "TRIG released (no rocker) -> cuts immediately");
    }

    // 4. Emergent envelope: rocker ON + SPEED OFF. Power alone = stalled
    //    (C5 empty, silence — TRIG/rocker are pure power now). The TONE
    //    button fast-charges C5 -> steady note; on release the sound
    //    persists and dies as C5 discharges through R23/R24.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        p.speed = 3;
        p.discharge_s = 0.5f;
        eng.setParameters(p);
        const float idle = renderRms(eng, blocksFor(1.0f), blocksFor(0.1f));
        CHECK(idle < 1e-3f, "rocker on, SPEED off -> stalled, silent (no kick)");

        p.toneBtn = true;
        eng.setParameters(p);
        const float note = renderRms(eng, blocksFor(0.5f), blocksFor(0.2f));
        CHECK(note > 0.03f, "TONE under rocker -> note sounds");
        p.toneBtn = false;
        eng.setParameters(p);
        const float tail = renderRms(eng, blocksFor(0.3f), blocksFor(0.05f));
        CHECK(tail > 0.03f, "TONE released under rocker -> discharge tail sounds");
        const float dead = renderRms(eng, blocksFor(4.0f), blocksFor(3.5f));
        CHECK(dead < 1e-3f, "discharge tail dies out (C5 drained)");
    }

    // 5. Mode 2 (square/fixed): square-low half stalls -> alternating
    //    sound/silence windows at the LFO rate.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        p.mode = 1;
        p.speed = 0; // 1.53 Hz: half = 0.326 s
        eng.setParameters(p);
        renderRms(eng, blocksFor(1.0f), 0); // settle
        float wMin = 1e9f, wMax = 0.0f;
        for (int w = 0; w < 12; ++w)
        {
            const float r = renderRms(eng, blocksFor(0.1f), 0);
            wMin = std::min(wMin, r);
            wMax = std::max(wMax, r);
        }
        CHECK(wMax > 0.05f, "two-tone: loud windows present");
        CHECK(wMin < wMax * 0.1f, "two-tone: stall gaps present");
    }

    // 5b. TONE or SIREN pressed alone (no power): silent, like the hardware
    //     (the SIREN ON switch sits in the V+ rail; S1/S2 only touch C5).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.toneBtn = true;
        eng.setParameters(p);
        const float tRms = renderRms(eng, blocksFor(0.5f), 0);
        p.toneBtn = false;
        p.sirenBtn = true;
        eng.setParameters(p);
        const float sRms = renderRms(eng, blocksFor(0.5f), 0);
        CHECK(tRms == 0.0f, "TONE alone (unpowered) -> silence");
        CHECK(sRms == 0.0f, "SIREN alone (unpowered) -> silence");
    }

    // 6. TONE button under power: steady fixed pitch (constant zc rate).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        p.toneBtn = true;
        eng.setParameters(p);
        float bufL[kBlock], bufR[kBlock];
        float* bufs[2] = { bufL, bufR };
        int zc[2] = { 0, 0 };
        float prev = 0.0f;
        const int settle = blocksFor(0.3f), win = blocksFor(0.5f);
        for (int b = 0; b < settle + 2 * win; ++b)
        {
            for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
            eng.process(bufs, 2, kBlock);
            if (b < settle) continue;
            const int w = (b - settle) / win;
            for (int i = 0; i < kBlock; ++i)
            {
                if (prev <= 0.0f && bufL[i] > 0.0f) ++zc[w];
                prev = bufL[i];
            }
        }
        std::printf("      tone zc: %d vs %d (~f0 = %d Hz)\n", zc[0], zc[1], zc[0] * 2);
        CHECK(zc[0] > 50, "tone button -> oscillating");
        CHECK(std::abs(zc[0] - zc[1]) <= 2, "tone button -> steady pitch");
    }

    // 7. SIREN wind-up under a latched rocker: the pitch climbs as C5
    //    charges through R25 (the wind-up is in frequency, not amplitude —
    //    the square is full level as soon as it oscillates). The rocker's
    //    initial kick is drained first.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        p.speed = 3;
        p.charge_s = 0.4f;
        p.discharge_s = 0.3f;
        eng.setParameters(p);
        renderRms(eng, blocksFor(2.0f), 0);  // kick + drain to stall
        p.sirenBtn = true;
        eng.setParameters(p);
        float bufL[kBlock], bufR[kBlock];
        float* bufs[2] = { bufL, bufR };
        const int win = blocksFor(0.2f);
        int zcEarly = 0, zcLate = 0;
        float prev = 0.0f;
        for (int b = 0; b < blocksFor(1.0f); ++b)
        {
            for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
            eng.process(bufs, 2, kBlock);
            int* zc = b < win ? &zcEarly : (b >= blocksFor(0.8f) ? &zcLate : nullptr);
            for (int i = 0; i < kBlock; ++i)
            {
                if (zc && prev <= 0.0f && bufL[i] > 0.0f) ++(*zc);
                prev = bufL[i];
            }
        }
        std::printf("      windup zc: early=%d late=%d (f0 ~%d -> ~%d Hz)\n",
                    zcEarly, zcLate, zcEarly * 5, zcLate * 5);
        CHECK(zcLate > zcEarly * 3 / 2, "SIREN wind-up: pitch climbs with C5 charge");
    }

    // 8. TRIG and the rocker are the same control: holding either from a
    //    fresh engine must produce bit-identical output.
    {
        SirenEngine engT, engR;
        engT.prepare(kFs, kBlock);
        engR.prepare(kFs, kBlock);
        SirenEngine::Params pT, pR;
        pT.trigBtn = true;
        pR.power = true;
        engT.setParameters(pT);
        engR.setParameters(pR);
        float tL[kBlock], tR[kBlock], rL[kBlock], rR[kBlock];
        float* bufsT[2] = { tL, tR };
        float* bufsR[2] = { rL, rR };
        float maxDiff = 0.0f;
        for (int b = 0; b < blocksFor(1.5f); ++b)
        {
            for (int i = 0; i < kBlock; ++i) { tL[i]=tR[i]=rL[i]=rR[i]=0.0f; }
            engT.process(bufsT, 2, kBlock);
            engR.process(bufsR, 2, kBlock);
            for (int i = 0; i < kBlock; ++i)
                maxDiff = std::max(maxDiff, std::fabs(tL[i] - rL[i]));
        }
        CHECK(maxDiff == 0.0f, "TRIG and rocker are bit-identical");
    }

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

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
