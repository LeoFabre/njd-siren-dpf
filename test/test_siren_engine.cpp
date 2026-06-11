// Offline checks for SirenEngine: gate behavior, signal sanity, and the
// "NJD flavor" comb (a delay of exactly one oscillator period must cancel
// every harmonic of the pulse, so output collapses toward silence).
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

int main()
{
    // 1. Gate off from a fresh engine: exact passthrough (adds nothing).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.gate = false;
        eng.setParameters(p);
        const float rms = renderRms(eng, 20, 0);
        CHECK(rms == 0.0f, "gate off -> silence");
    }

    // 2. Gate on: signal present, finite, bounded.
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.gate = true;
        eng.setParameters(p);
        const float rms = renderRms(eng, 200, 50);
        CHECK(std::isfinite(rms), "gate on -> finite output");
        CHECK(rms > 0.05f, "gate on -> audible level");
        CHECK(rms < 1.5f, "gate on -> bounded level");
    }

    // 3. Gate on then off: the sound cuts immediately (2 ms de-click ramp).
    {
        SirenEngine eng;
        eng.prepare(kFs, kBlock);
        SirenEngine::Params p;
        p.gate = true;
        eng.setParameters(p);
        renderRms(eng, 100, 0);
        p.gate = false;
        eng.setParameters(p);
        const float rmsEarly = renderRms(eng, 15, 8);  // 10..20 ms after release
        CHECK(rmsEarly < 5e-3f, "gate off -> cuts within ~10 ms");
        const float rmsLate = renderRms(eng, 50, 40);  // ~70 ms after release
        CHECK(rmsLate < 1e-5f, "gate off -> fully silent shortly after");
    }

    // 4. Flavor comb: delay = one period of a fixed-pitch osc cancels all
    //    harmonics. f0 = 500 Hz, delay = 2 ms, modulation amount = 0.
    {
        SirenEngine::Params p;
        p.gate = true;
        p.amount_pct = 0.0f;
        p.drive_dB = 0.0f;
        p.sparkle_pct = 0.0f;
        // pitch01 such that 60 * 25^p = 500 Hz
        p.pitch01 = std::log(500.0f / 60.0f) / std::log(25.0f);
        p.flavorTime_ms = 2.0f;

        SirenEngine engDry, engComb;
        engDry.prepare(kFs, kBlock);
        engComb.prepare(kFs, kBlock);
        p.flavor_pct = 0.0f;
        engDry.setParameters(p);
        p.flavor_pct = 100.0f;
        engComb.setParameters(p);

        const float rmsDry = renderRms(engDry, 300, 100);
        const float rmsComb = renderRms(engComb, 300, 100);
        std::printf("      comb cancellation: dry rms=%.4f comb rms=%.4f\n", rmsDry, rmsComb);
        CHECK(rmsDry > 0.1f, "comb test: dry reference is loud");
        CHECK(rmsComb < rmsDry * 0.12f, "flavor comb cancels harmonics at 1/period delay");
    }

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
