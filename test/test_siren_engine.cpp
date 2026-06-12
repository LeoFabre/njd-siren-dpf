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

    // 3. TRIG with rocker OFF: sound while held, cuts right after release.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.speed = 3; // manual mode
        p.trigBtn = true;
        eng.setParameters(p);
        const float held = renderRms(eng, blocksFor(0.4f), blocksFor(0.1f));
        CHECK(held > 0.03f, "TRIG held -> sound");
        p.trigBtn = false;
        eng.setParameters(p);
        const float after = renderRms(eng, blocksFor(0.1f), blocksFor(0.02f));
        CHECK(after < 5e-3f, "TRIG released (no rocker) -> cuts immediately");
    }

    // 4. Emergent envelope: rocker ON + SPEED OFF. Idle = stalled (C5 empty,
    //    silence). TRIG charges C5 -> sound persists after release and dies
    //    as C5 discharges through R23/R24.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.power = true;
        p.speed = 3;
        p.discharge_s = 0.5f;
        eng.setParameters(p);
        const float idle = renderRms(eng, blocksFor(1.0f), blocksFor(0.3f));
        CHECK(idle < 1e-3f, "rocker on, C5 empty -> stalled, silent");

        p.trigBtn = true;
        eng.setParameters(p);
        renderRms(eng, blocksFor(0.2f), 0);
        p.trigBtn = false;
        eng.setParameters(p);
        const float tail = renderRms(eng, blocksFor(0.3f), blocksFor(0.05f));
        CHECK(tail > 0.03f, "TRIG released under rocker -> discharge tail sounds");
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

    // 6. TONE button: steady fixed pitch (constant zero-crossing rate).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
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

    // 7. SIREN wind-up: the pitch climbs as C5 charges through R25 (the
    //    wind-up is in frequency, not amplitude — the square is full level
    //    as soon as it oscillates).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.speed = 3;
        p.charge_s = 0.4f;
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

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
