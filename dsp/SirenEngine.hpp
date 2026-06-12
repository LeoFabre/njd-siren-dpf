#pragma once
// Circuit model of the NJD dub siren, after the ZEN Instruments / Zack Nelson
// schematic redraw.
//
// Main oscillator: two-transistor astable (T3/T4, C=10n). Each half-period is
//   t = R*C * ln((Vm + Vcc - 1.3) / (Vm - Vbe))
// where Vm is the modulation voltage pulling that half's base resistor R (the
// Osc Rate ladder, S5). The two halves are pulled by DIFFERENT sources (S4
// routing), so pitch AND duty cycle move together: the pulse spectrum's
// |sin(pi*k*duty)| notches sweep continuously — the NJD grain. If Vm drops
// below Vbe a half never completes: the oscillator stalls and the output
// parks at a rail, i.e. silence after the DC-blocking output network. There
// is NO envelope anywhere in the circuit; sound starts/stops via power (S6
// rocker, here also the momentary buttons) and via this stall.
//
// LFO: same astable shape (T1/T2, C=10uF, rates from the S3 ladder). Its raw
// square charges C5 (100uF) through D4/R22 (tau ~0.1 s) and C5 discharges
// through R23/R24 — the "uneven triangle" shaped LFO. S1 (TRIG) fast-charges
// C5 and resets the square; S2 (SIREN) slow-charges it through R25 (tau ~1 s).
// The LED driver (T5) follows C5, so the panel LED breathes with the sweep.
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace siren {

class SirenEngine {
public:
    struct Params {
        bool  trigBtn  = false;  // momentary TRIG: power + S1 kick on press
        bool  sirenBtn = false;  // S2: slow-charge C5 through R25
        bool  toneBtn  = false;  // force fixed-pitch routing (D1/D2/R8/R9)
        bool  power    = false;  // rocker = latched TRIG (identical behavior)
        int   tone     = 1;      // S5 osc-rate ladder index (470k/220k/100k)
        int   mode     = 0;      // S4 modulation routing, see kRoute
        int   speed    = 1;      // S3 lfo-rate ladder (47k/22k/10k), 3 = OFF
        float volume_dB    = -6.0f;
        float charge_s     = 1.0f;   // R25*C5: SIREN wind-up time constant
        float discharge_s  = 2.0f;   // R23/R24*C5: manual / SPEED-OFF fall
        float amount_pct   = 100.0f; // mod scale about 4.5 V (not in circuit)
        float drive_dB     = 4.0f;   // post drive (not in circuit)
    };

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        fs_ = (float) sampleRate;
        dt_ = 1.0f / fs_;

        smoothCoef_ = 1.0f - std::exp(-dt_ / 0.010f);
        rampCoef_   = 1.0f - std::exp(-dt_ / 0.002f);  // 2 ms power de-click
        cAuto_      = 1.0f - std::exp(-dt_ / 0.100f);  // D4/R22 from the LFO
        lpCoef_     = 1.0f - std::exp(-6.2831853f * 6000.0f * dt_);
        hpCoef_     = kHpTau / (kHpTau + dt_);          // 106 Hz one-pole x2

        vC5_ = 0.0f;
        sqHigh_ = false;
        lfoT_ = 0.0f;
        halfA_ = true;
        ph_ = 0.0f;
        oscZ_ = 1.0f;
        ramp_ = 0.0f;
        led_ = 0.0f;
        prevPower_ = false;
        prevTrigBtn_ = false;
        prevPowerSw_ = false;
        hp1x_ = hp1y_ = hp2x_ = hp2y_ = lpY_ = 0.0f;

        amountS_ = p_.amount_pct / 100.0f;
        volS_ = dB2lin(p_.volume_dB);
    }

    void setParameters(const Params& p) { p_ = p; }

    // T5/LED1 brightness: follows the shaped LFO (C5), dark when unpowered.
    float ledLevel() const { return led_; }

    // Adds the mono siren to every channel; channel content is preserved.
    void process(float** buffers, int numChannels, int numFrames)
    {
        if (fs_ <= 0.0f)
            return;

        const bool effPower = p_.power || p_.trigBtn || p_.sirenBtn || p_.toneBtn;
        if (effPower && !prevPower_)
        {
            // Power-up: both astables restart from a known state.
            sqHigh_ = true;
            lfoT_ = 0.0f;
            halfA_ = true;
            ph_ = 0.0f;
        }
        prevPower_ = effPower;

        // TRIG and the rocker are the SAME control (the rocker is a latched
        // TRIG): on activation, S1 slams C5 to the rail and resets the LFO
        // square; afterwards the unit runs free. Tapping TRIG while the
        // rocker holds power re-fires the kick.
        if ((p_.trigBtn && !prevTrigBtn_) || (p_.power && !prevPowerSw_))
        {
            vC5_ = kVmax;
            sqHigh_ = true;
            lfoT_ = 0.0f;
        }
        prevTrigBtn_ = p_.trigBtn;
        prevPowerSw_ = p_.power;

        const bool speedOn = p_.speed < 3;
        const float tLfoHalf = kLfoHalf[std::clamp(p_.speed, 0, 2)];
        const float Rosc = kRosc[std::clamp(p_.tone, 0, 2)];

        const float cSlow = 1.0f - std::exp(-dt_ / std::max(0.05f, p_.charge_s));
        // Manual / power-off discharge uses the knob directly; while the LFO
        // runs, R24 is (partly) switched out, so scale the circuit's 0.22 s.
        const float cDisMan  = 1.0f - std::exp(-dt_ / std::max(0.05f, p_.discharge_s));
        const float cDisAuto = 1.0f - std::exp(-dt_ / std::max(0.02f, 0.22f * p_.discharge_s / 2.0f));

        const uint8_t* route = p_.toneBtn ? kRoute[3] : kRoute[std::clamp(p_.mode, 0, 3)];

        const float amountTarget = p_.amount_pct / 100.0f;
        const float volTarget = dB2lin(p_.volume_dB);
        const float pre = dB2lin(p_.drive_dB);
        const float driveNorm = 1.0f / std::tanh(std::max(0.1f, pre));

        for (int i = 0; i < numFrames; ++i)
        {
            amountS_ += smoothCoef_ * (amountTarget - amountS_);
            volS_ += smoothCoef_ * (volTarget - volS_);

            // ---- LFO square astable -------------------------------------
            if (speedOn && effPower)
            {
                lfoT_ += dt_;
                if (lfoT_ >= tLfoHalf) { lfoT_ -= tLfoHalf; sqHigh_ = !sqHigh_; }
            }
            else
                sqHigh_ = false;      // SPEED off / unpowered: no charge path

            // ---- C5 (shaped LFO) ----------------------------------------
            float target, coef;
            if (sqHigh_)                       { target = kVmax; coef = cAuto_; }
            else if (p_.sirenBtn && effPower)  { target = kVmax; coef = cSlow; }
            else { target = 0.0f; coef = (effPower && speedOn) ? cDisAuto : cDisMan; }
            vC5_ += coef * (target - vC5_);

            // ---- power ramp (2 ms de-click, the only "envelope") --------
            ramp_ += rampCoef_ * ((effPower ? 1.0f : 0.0f) - ramp_);

            // ---- main oscillator ----------------------------------------
            float Vm = srcVoltage(route[halfA_ ? 0 : 1]);
            Vm = 4.5f + amountS_ * (Vm - 4.5f);
            const float th = tHalf(Vm, Rosc);
            const float inc = th > 0.0f ? dt_ / th : 0.0f;

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
            float y = oscZ_ + prevCorr;          // one-sample delayed + BLEP
            oscZ_ = naive + nowCorr;

            y *= ramp_;

            // ---- output network: C9/R26 + C8/R27 (~106 Hz HP x2), edge
            // rounding, then the extra post drive and volume ---------------
            float h = hpCoef_ * (hp1y_ + y - hp1x_); hp1x_ = y; hp1y_ = h;
            float h2 = hpCoef_ * (hp2y_ + h - hp2x_); hp2x_ = h; hp2y_ = h2;
            lpY_ += lpCoef_ * (h2 - lpY_);
            float out = std::tanh(pre * lpY_) * driveNorm;
            out *= volS_;

            for (int ch = 0; ch < numChannels; ++ch)
                buffers[ch][i] += out;
        }

        led_ = ramp_ * (vC5_ / kVmax);
    }

private:
    static constexpr float kVcc  = 9.0f;
    static constexpr float kVbe  = 0.65f;
    static constexpr float kVmax = 8.3f;          // C5 ceiling (Vcc - diode)
    static constexpr float kCosc = 10e-9f;
    static constexpr float kHpTau = 15e3f * 100e-9f;  // R26/27 * C9/8

    // S5 osc-rate and S3 lfo-rate ladders. LFO half = 0.693*R*10uF.
    static constexpr float kRosc[3]    = { 470e3f, 220e3f, 100e3f };
    static constexpr float kLfoHalf[3] = { 0.326f, 0.152f, 0.069f };

    // S4 routing per MODE: modulation source for each oscillator half.
    // 0 = shaped LFO (C5), 1 = raw square, 2 = fixed +9V (D1/D2 path).
    // 1: wail  2: two-tone w/ stall gaps  3: shaped vs square  4: fixed tone
    static constexpr uint8_t kRoute[4][2] = { {0,2}, {1,2}, {0,1}, {2,2} };

    float srcVoltage(uint8_t src) const
    {
        switch (src)
        {
        case 0:  return vC5_;
        case 1:  return sqHigh_ ? kVcc : 0.0f;
        default: return kVcc;
        }
    }

    // Astable half-period for base resistor R pulled to Vm. Returns <= 0
    // when the transistor can never switch: the oscillator stalls (silence).
    float tHalf(float Vm, float R) const
    {
        if (Vm < kVbe + 0.02f)
            return -1.0f;
        return R * kCosc * std::log((Vm + kVcc - 1.3f) / (Vm - kVbe));
    }

    static float dB2lin(float dB) { return std::exp2(dB * 0.166096404f); }

    Params p_;
    float fs_ = 0.0f, dt_ = 0.0f;

    // LFO + shaper
    bool  sqHigh_ = false;
    float lfoT_ = 0.0f;
    float vC5_ = 0.0f;

    // Main oscillator
    bool  halfA_ = true;
    float ph_ = 0.0f;
    float oscZ_ = 1.0f;

    bool  prevPower_ = false;
    bool  prevTrigBtn_ = false, prevPowerSw_ = false;
    float ramp_ = 0.0f, led_ = 0.0f;

    float hp1x_ = 0, hp1y_ = 0, hp2x_ = 0, hp2y_ = 0, lpY_ = 0;
    float smoothCoef_ = 0, rampCoef_ = 0, cAuto_ = 0;
    float lpCoef_ = 0, hpCoef_ = 0;
    float amountS_ = 1.0f, volS_ = 0.5f;
};

} // namespace siren
