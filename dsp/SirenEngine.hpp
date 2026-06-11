#pragma once
// NJD-style dub siren voice. Recipe reverse-engineered from the Ableton
// Wavetable rack in "le son de sirène analo avec un wavetable !!!" (zmCO6T_ksno):
// mono pulse osc, pitch driven by two LFOs + an attack envelope, and the analog
// grain recreated by summing a phase-inverted copy delayed by 1-2 ms (the comb
// the transistor-reset/capacitor-discharge produces in the real NJD circuit),
// then tube drive, a resonant 12 kHz "sparkle" peak and a 100 Hz high-pass.
#include <cmath>
#include <vector>
#include <algorithm>

namespace siren {

class SirenEngine {
public:
    enum class Wave : int { Sine = 0, Tri, Saw, Square };

    struct Params {
        bool  gate            = false;
        float volume_dB       = -6.0f;
        float pitch01         = 0.5f;   // 0..1 -> 60..1500 Hz exponential
        float attack_ms       = 0.0f;   // pitch envelope attack (lick shots)
        float amount_pct      = 100.0f; // 0..200, scales all pitch modulation
        Wave  lfo1Wave        = Wave::Tri;
        float lfo1Shape_pct   = 0.0f;
        float lfo1Rate_Hz     = 6.0f;
        Wave  lfo2Wave        = Wave::Sine;
        float lfo2Amount_pct  = 0.0f;
        float lfo2Rate_Hz     = 1.0f;
        float flavor_pct      = 75.0f;  // inverted-layer gain (comb depth)
        float flavorTime_ms   = 1.5f;   // comb delay
        float drive_dB        = 4.0f;
        float sparkle_pct     = 50.0f;  // 12 kHz peak gain, 0..+8 dB
    };

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        fs_ = (float) sampleRate;
        const int maxDelay = (int) std::ceil(0.006f * fs_) + 4;
        comb_.assign((size_t) maxDelay, 0.0f);
        combLen_ = maxDelay;
        combWrite_ = 0;

        smoothCoef_ = 1.0f - std::exp(-1.0f / (0.010f * fs_));
        ampAttCoef_ = 1.0f - std::exp(-1.0f / (0.003f * fs_));
        ampRelCoef_ = 1.0f - std::exp(-1.0f / (0.050f * fs_));

        oscPhase_ = 0.0f;
        lfo1Phase_ = 0.5f;
        lfo2Phase_ = 0.0f;
        ampEnv_ = 0.0f;
        pitchEnv_ = 0.0f;
        prevGate_ = false;

        peak_.reset();
        hpf_.reset();
        updateFilters(/*force=*/true);

        // Land the smoothers on their targets so the first block is in tune.
        pitch01S_ = p_.pitch01;
        amountS_ = p_.amount_pct / 100.0f;
        flavorS_ = p_.flavor_pct / 100.0f;
        delaySampS_ = p_.flavorTime_ms * 0.001f * fs_;
        volS_ = dB2lin(p_.volume_dB);
    }

    void setParameters(const Params& p)
    {
        const float prevSparkle = p_.sparkle_pct;
        p_ = p;
        if (p_.sparkle_pct != prevSparkle)
            updateFilters(false);
    }

    // Adds the mono siren to every channel; channel content is preserved.
    void process(float** buffers, int numChannels, int numFrames)
    {
        const bool gate = p_.gate;
        if (gate && !prevGate_)
        {
            lfo1Phase_ = 0.5f;   // 180° offset: start on the falling edge
            pitchEnv_ = 0.0f;
        }
        prevGate_ = gate;

        const float lfo1Inc = p_.lfo1Rate_Hz / fs_;
        const float lfo2Inc = p_.lfo2Rate_Hz / fs_;
        const float shape1 = p_.lfo1Shape_pct / 100.0f;

        // Ableton Wavetable mod-matrix full scale is +/-48 semitones. The video
        // uses LFO1 at 15->20 % (depth follows the pitch knob, as on the real
        // NJD), env2 at 18 %, LFO2 free 0..100 %.
        const float lfo2Depth = p_.lfo2Amount_pct * 0.48f;
        const float envDepth = 8.64f;

        const float pitchTarget = p_.pitch01;
        const float amountTarget = p_.amount_pct / 100.0f;
        const float flavorTarget = p_.flavor_pct / 100.0f;
        const float delayTarget = std::min(p_.flavorTime_ms * 0.001f * fs_,
                                           (float) (combLen_ - 2));
        const float volTarget = dB2lin(p_.volume_dB);

        const float pitchAttCoef = p_.attack_ms < 1.0f
            ? 1.0f
            : 1.0f - std::exp(-1.0f / (p_.attack_ms * 0.001f * fs_));

        const float pre = dB2lin(p_.drive_dB);
        const float driveNorm = 1.0f / std::tanh(pre);
        const float ampTarget = gate ? 1.0f : 0.0f;
        const float ampCoef = gate ? ampAttCoef_ : ampRelCoef_;
        const float nyqLimit = 0.45f * fs_;

        for (int i = 0; i < numFrames; ++i)
        {
            pitch01S_ += smoothCoef_ * (pitchTarget - pitch01S_);
            amountS_ += smoothCoef_ * (amountTarget - amountS_);
            flavorS_ += smoothCoef_ * (flavorTarget - flavorS_);
            delaySampS_ += smoothCoef_ * (delayTarget - delaySampS_);
            volS_ += smoothCoef_ * (volTarget - volS_);

            ampEnv_ += ampCoef * (ampTarget - ampEnv_);
            pitchEnv_ += pitchAttCoef * ((gate ? 1.0f : 0.0f) - pitchEnv_);

            const float l1 = lfoEval(p_.lfo1Wave, lfo1Phase_, shape1);
            const float l2 = lfoEval(p_.lfo2Wave, lfo2Phase_, 0.0f);
            lfo1Phase_ = wrap(lfo1Phase_ + lfo1Inc);
            lfo2Phase_ = wrap(lfo2Phase_ + lfo2Inc);

            const float lfo1Depth = 7.2f + 2.4f * pitch01S_;
            const float mod_st = amountS_ * (lfo1Depth * l1
                                             + lfo2Depth * l2
                                             + envDepth * pitchEnv_);

            // f = 60 * 25^pitch01 * 2^(mod/12), folded into a single exp2
            float f = 60.0f * std::exp2(pitch01S_ * 4.6438562f + mod_st * (1.0f / 12.0f));
            f = std::min(f, nyqLimit);

            const float inc = f / fs_;
            float x = oscPhase_ < 0.5f ? 1.0f : -1.0f;
            x += polyBlep(oscPhase_, inc);
            x -= polyBlep(wrap(oscPhase_ + 0.5f), inc);
            oscPhase_ = wrap(oscPhase_ + inc);

            // NJD flavor: inverted copy delayed 1-2 ms, summed at the source
            comb_[(size_t) combWrite_] = x;
            float rp = (float) combWrite_ - delaySampS_;
            if (rp < 0.0f) rp += (float) combLen_;
            const int r0 = (int) rp;
            const float frac = rp - (float) r0;
            const int r1 = (r0 + 1) % combLen_;
            const float delayed = comb_[(size_t) r0]
                                + frac * (comb_[(size_t) r1] - comb_[(size_t) r0]);
            combWrite_ = (combWrite_ + 1) % combLen_;
            float y = x - flavorS_ * delayed;

            y *= ampEnv_;
            y = std::tanh(pre * y) * driveNorm;
            y = peak_.process(y);
            y = hpf_.process(y);
            y *= volS_;

            for (int ch = 0; ch < numChannels; ++ch)
                buffers[ch][i] += y;
        }
    }

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        float process(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static float dB2lin(float dB) { return std::exp2(dB * 0.166096404f); }
    static float wrap(float p) { return p >= 1.0f ? p - 1.0f : p; }

    static float polyBlep(float t, float dt)
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // shape 0..1: sine/tri/saw clip progressively toward square (matching the
    // Wavetable "Shape" morphs in the video); square shifts its pulse width.
    static float lfoEval(Wave w, float p, float shape)
    {
        float v;
        switch (w)
        {
        case Wave::Sine:   v = std::sin(6.2831853f * p); break;
        case Wave::Tri:    v = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
        case Wave::Saw:    v = 1.0f - 2.0f * p; break;
        default:           return p < (0.5f - 0.45f * shape) ? 1.0f : -1.0f;
        }
        const float k = 1.0f + 9.0f * shape;
        return std::max(-1.0f, std::min(1.0f, v * k));
    }

    void updateFilters(bool force)
    {
        if (fs_ <= 0.0f) return;

        const float gainDb = 8.0f * p_.sparkle_pct / 100.0f;
        designPeak(peak_, std::min(12000.0f, 0.40f * fs_), 2.0f, gainDb);
        if (force)
            designHighpass(hpf_, 100.0f, 0.7071f);
    }

    void designPeak(Biquad& bq, float f0, float Q, float gainDb)
    {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 6.2831853f * f0 / fs_;
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float c = std::cos(w0);
        const float a0 = 1.0f + alpha / A;
        bq.b0 = (1.0f + alpha * A) / a0;
        bq.b1 = (-2.0f * c) / a0;
        bq.b2 = (1.0f - alpha * A) / a0;
        bq.a1 = (-2.0f * c) / a0;
        bq.a2 = (1.0f - alpha / A) / a0;
    }

    void designHighpass(Biquad& bq, float f0, float Q)
    {
        const float w0 = 6.2831853f * f0 / fs_;
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float c = std::cos(w0);
        const float a0 = 1.0f + alpha;
        bq.b0 = (1.0f + c) * 0.5f / a0;
        bq.b1 = -(1.0f + c) / a0;
        bq.b2 = (1.0f + c) * 0.5f / a0;
        bq.a1 = (-2.0f * c) / a0;
        bq.a2 = (1.0f - alpha) / a0;
    }

    Params p_;
    float fs_ = 0.0f;

    std::vector<float> comb_;
    int combLen_ = 0;
    int combWrite_ = 0;

    float oscPhase_ = 0.0f;
    float lfo1Phase_ = 0.5f;
    float lfo2Phase_ = 0.0f;
    float ampEnv_ = 0.0f;
    float pitchEnv_ = 0.0f;
    bool  prevGate_ = false;

    float smoothCoef_ = 0.0f, ampAttCoef_ = 0.0f, ampRelCoef_ = 0.0f;
    float pitch01S_ = 0.5f, amountS_ = 1.0f, flavorS_ = 0.75f;
    float delaySampS_ = 64.0f, volS_ = 0.5f;

    Biquad peak_, hpf_;
};

} // namespace siren
