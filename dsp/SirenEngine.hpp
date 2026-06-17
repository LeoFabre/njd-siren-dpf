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
// square charges C5 (100uF) through R23/D4/R22 (tau ~0.32 s) and C5 discharges
// through R23/R24 — the "uneven triangle" shaped LFO. BP2/S1 (TONE button)
// fast-charges C5 and holds the square: a steady top note while held. BP1/S2
// (SIREN) slow-charges it through R25 (tau ~1 s): the wind-up. TRIG and the
// rocker are wired across the SIREN ON header in the V+ rail: pure power,
// momentary vs latched.
// The LED driver (T5) follows C5, so the panel LED breathes with the sweep.
//
// --- Anti-alias (OPT-IN, default-off) ---------------------------------------
// oversample (1/2/4): the whole osc+filter+tanh chain runs at OS*fs and a
// windowed-sinc FIR decimates back. The polyBLEP square + the tanh drive alias
// in the highs; running them hot and decimating pushes the images above the
// audible band. oversample==1 is the original code path, bit-identical.
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace siren {

class SirenEngine {
public:
    struct Params {
        bool  trigBtn  = false;  // momentary power (wired across SIREN ON)
        bool  sirenBtn = false;  // BP1/S2: slow-charge C5 through R25
        bool  toneBtn  = false;  // BP2/S1: fast-charge C5 + hold the LFO
                                 // square -> steady top note while held
        bool  power    = false;  // rocker = latched TRIG (identical behavior)
        int   tone     = 1;      // S5 osc-rate ladder index (470k/220k/100k)
        int   mode     = 0;      // S4 modulation routing, see kRoute
        int   speed    = 1;      // S3 lfo-rate ladder (47k/22k/10k), 3 = OFF
        float volume_dB    = -6.0f;
        float charge_s     = 1.0f;   // R25*C5: SIREN wind-up time constant
        float discharge_s  = 2.0f;   // R23/R24*C5: manual / SPEED-OFF fall
        float amount_pct   = 100.0f; // mod scale about 4.5 V (not in circuit)
        float drive_dB     = 4.0f;   // post drive (not in circuit)
        // Dev/physics trims: component tolerances of the real unit.
        float bleed_pct    = 35.0f;  // half-A source leaking into half B's
                                     // pull-up: the clone schematic shows each
                                     // base mixes its ladder with a 220k+diode
                                     // path (R8/D1, R9/D2) — ~50/50 at TONE 2
        float capRatio     = 1.0f;   // C2/C1 mismatch: scales half-B period
        float vbe_V        = 0.65f;  // transistor threshold: stall point and
                                     // the shape of the dying dive
        float edgeHz       = 10000.0f;// collector edge rounding (no LP in the
                                      // real output chain, only 2x HP 106 Hz)
        int   oversample   = 1;      // 1/2/4/8 anti-alias factor for osc+tanh
        // Collector-waveform model (circuit-faithful generator).
        int   model        = 0;      // 0 = Classic (square+polyBLEP), 1 = Physical
        float collTau_us   = 45.0f;  // tau_c: collector recovery (Rc*Ccouple), us
        float fallTau_us   = 12.0f;  // tau_fall: turn-on fall time, us
    };

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        fs_ = (float) sampleRate;
        dt_ = 1.0f / fs_;

        vC5_ = 0.0f;
        sqHigh_ = false;
        lfoT_ = 0.0f;
        halfA_ = true;
        ph_ = 0.0f;
        oscZ_ = 1.0f;
        vColl_ = 0.0f;
        ramp_ = 0.0f;
        led_ = 0.0f;
        prevPower_ = false;
        hp1x_ = hp1y_ = hp2x_ = hp2y_ = lpY_ = 0.0f;

        amountS_ = p_.amount_pct / 100.0f;
        volS_ = dB2lin(p_.volume_dB);

        lastOS_ = 0;          // force decimator (re)design on first block
        decimReset();
    }

    void setParameters(const Params& p) { p_ = p; }

    // T5/LED1 brightness: follows the shaped LFO (C5), dark when unpowered.
    float ledLevel() const { return led_; }

    // Adds the mono siren to every channel; channel content is preserved.
    void process(float** buffers, int numChannels, int numFrames)
    {
        if (fs_ <= 0.0f)
            return;

        int OS = (p_.oversample >= 8) ? 8 : (p_.oversample >= 4) ? 4
               : (p_.oversample >= 2) ? 2 : 1;
        if (p_.model == 1 && OS < 2) OS = 2;   // Physical anti-alias floor
        const float dtEff = dt_ / (float) OS;
        if (OS != lastOS_) { decimDesign(OS); decimReset(); lastOS_ = OS; }

        // Only TRIG and the rocker provide power (they ARE the power path,
        // like the SIREN ON switch in the V+ rail). TONE and SIREN only
        // manipulate the routing / C5 while the unit is already powered —
        // pressed alone they are silent, like the hardware.
        const bool effPower = p_.power || p_.trigBtn;
        if (effPower && !prevPower_)
        {
            // Power-up: both astables restart from a known state.
            sqHigh_ = true;
            lfoT_ = 0.0f;
            halfA_ = true;
            ph_ = 0.0f;
        }
        prevPower_ = effPower;

        // Per-block constants. All dt-dependent coefs are derived from dtEff so
        // the chain runs identically at any oversample factor (and exactly the
        // original values when OS==1).
        Blk c;
        c.effPower   = effPower;
        c.speedOn    = p_.speed < 3;
        c.tLfoHalf   = kLfoHalf[std::clamp(p_.speed, 0, 2)];
        c.Rosc       = kRosc[std::clamp(p_.tone, 0, 2)];
        c.route      = kRoute[std::clamp(p_.mode, 0, 3)];

        c.smoothCoef = 1.0f - std::exp(-dtEff / 0.010f);
        c.rampCoef   = 1.0f - std::exp(-dtEff / 0.002f);  // 2 ms power de-click
        c.cFast      = 1.0f - std::exp(-dtEff / 0.100f);  // BP2 direct: D4+R22*C5
        c.cAuto      = 1.0f - std::exp(-dtEff / 0.320f);  // R23+R22 (2k2+1k)*C5
        c.cSlow      = 1.0f - std::exp(-dtEff / std::max(0.05f, p_.charge_s));
        // Manual / power-off discharge uses the knob directly; while the LFO
        // runs, R24 is (partly) switched out, so scale the circuit's 0.22 s.
        c.cDisMan    = 1.0f - std::exp(-dtEff / std::max(0.05f, p_.discharge_s));
        c.cDisAuto   = 1.0f - std::exp(-dtEff / std::max(0.02f, 0.22f * p_.discharge_s / 2.0f));
        c.lpCoef     = 1.0f - std::exp(-6.2831853f
                              * std::clamp(p_.edgeHz, 200.0f, 0.45f * fs_ * OS) * dtEff);
        c.hpCoef     = kHpTau / (kHpTau + dtEff);
        c.physical = (p_.model == 1);
        c.collCoef = 1.0f - std::exp(-dtEff / std::max(1e-6f, p_.collTau_us * 1e-6f));
        c.fallCoef = 1.0f - std::exp(-dtEff / std::max(1e-6f, p_.fallTau_us * 1e-6f));

        c.amountTarget = p_.amount_pct / 100.0f;
        c.volTarget    = dB2lin(p_.volume_dB);
        c.pre          = dB2lin(p_.drive_dB);
        c.driveNorm    = 1.0f / std::tanh(std::max(0.1f, c.pre));
        c.bleed        = p_.bleed_pct / 100.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            float sample;
            if (OS == 1)
                sample = genSample(c);
            else
            {
                for (int k = 0; k < OS; ++k)
                    decimPush(genSample(c));
                sample = decimRead();
            }

            for (int ch = 0; ch < numChannels; ++ch)
                buffers[ch][i] += sample;
        }

        led_ = ramp_ * (vC5_ / kVmax);
    }

private:
    // ---- per-block constant bundle ------------------------------------------
    struct Blk {
        bool  effPower, speedOn;
        float tLfoHalf, Rosc;
        const uint8_t* route;
        float smoothCoef, rampCoef, cFast, cAuto, cSlow, cDisMan, cDisAuto;
        float lpCoef, hpCoef;
        float collCoef, fallCoef;   // Physical: 1-exp(-dtEff/tau)
        bool  physical;
        float amountTarget, volTarget, pre, driveNorm, bleed;
    };

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

    // ---- one oversampled audio sample (mutates engine state) ----------------
    inline float genSample(const Blk& c)
    {
        amountS_ += c.smoothCoef * (c.amountTarget - amountS_);
        volS_    += c.smoothCoef * (c.volTarget - volS_);

        // ---- LFO square astable ---------------------------------------------
        if (c.speedOn && c.effPower)
        {
            if (p_.toneBtn)
            {
                sqHigh_ = true;   // BP2/S1 holds the astable via D3
                lfoT_ = 0.0f;
            }
            else
            {
                lfoT_ += dtTick_;
                if (lfoT_ >= c.tLfoHalf) { lfoT_ -= c.tLfoHalf; sqHigh_ = !sqHigh_; }
            }
        }
        else
            sqHigh_ = false;      // SPEED off / unpowered: no charge path

        // ---- C5 (shaped LFO) ------------------------------------------------
        float target, coef;
        if (p_.toneBtn && c.effPower)        { target = kVmax; coef = c.cFast; }
        else if (sqHigh_)                    { target = kVmax; coef = c.cAuto; }
        else if (p_.sirenBtn && c.effPower)  { target = kVmax; coef = c.cSlow; }
        else { target = 0.0f; coef = (c.effPower && c.speedOn) ? c.cDisAuto : c.cDisMan; }
        vC5_ += coef * (target - vC5_);

        // ---- power ramp (2 ms de-click, the only "envelope") ----------------
        ramp_ += c.rampCoef * ((c.effPower ? 1.0f : 0.0f) - ramp_);

        // ---- main oscillator ------------------------------------------------
        float Vm;
        if (halfA_)
            Vm = srcVoltage(c.route[0]);
        else
        {
            // D1/D2 + R8/R9: the 220k diode path drags half B's pull-up DOWN
            // toward half A's source, conducting only when that source sits a
            // diode drop below (half-wave). During the discharge fall both
            // half-periods stretch at different rates, so the duty keeps
            // sweeping the spectral notches all the way down — until half A
            // stalls and the unit goes quiet.
            const float vB = srcVoltage(c.route[1]);
            Vm = vB - c.bleed * std::max(0.0f, vB - (srcVoltage(c.route[0]) + 0.6f));
        }
        Vm = 4.5f + amountS_ * (Vm - 4.5f);
        float th = tHalf(Vm, c.Rosc);
        if (!halfA_ && th > 0.0f)
            th *= p_.capRatio;          // C1/C2 tolerance mismatch
        const float inc = th > 0.0f ? dtTick_ / th : 0.0f;

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

        // ---- output network: C9/R26 + C8/R27 (~106 Hz HP x2), edge rounding,
        // then the extra post drive and volume -------------------------------
        float h = c.hpCoef * (hp1y_ + y - hp1x_); hp1x_ = y; hp1y_ = h;
        float h2 = c.hpCoef * (hp2y_ + h - hp2x_); hp2x_ = h; hp2y_ = h2;
        lpY_ += c.lpCoef * (h2 - lpY_);
        float out = std::tanh(c.pre * lpY_) * c.driveNorm;
        out *= volS_;
        return out;
    }

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
        const float vbe = p_.vbe_V;
        if (Vm < vbe + 0.02f)
            return -1.0f;
        return R * kCosc * std::log((Vm + kVcc - 2.0f * vbe) / (Vm - vbe));
    }

    static float dB2lin(float dB) { return std::exp2(dB * 0.166096404f); }

    // ---- decimator (windowed-sinc FIR, used only when OS > 1) ---------------
    void decimDesign(int OS)
    {
        decimOS_ = OS;
        if (OS <= 1) { decimN_ = 1; return; }
        decimN_ = std::min(kMaxFir, 16 * OS + 1);
        const float fc = 0.45f / (float) OS;        // cycles/sample at OS rate
        const int M = decimN_ - 1;
        float sum = 0.0f;
        for (int n = 0; n < decimN_; ++n)
        {
            const float x = (float) n - 0.5f * (float) M;
            float s = (x == 0.0f) ? 2.0f * fc
                                  : std::sin(6.2831853f * fc * x) / (3.1415927f * x);
            // Blackman window
            const float w = 0.42f
                - 0.5f * std::cos(6.2831853f * (float) n / (float) M)
                + 0.08f * std::cos(12.566371f * (float) n / (float) M);
            decimH_[n] = s * w;
            sum += decimH_[n];
        }
        for (int n = 0; n < decimN_; ++n) decimH_[n] /= sum;  // unity DC gain
    }
    void decimReset()
    {
        for (int n = 0; n < kMaxFir; ++n) decimRing_[n] = 0.0f;
        decimPos_ = 0;
        dtTick_ = dt_ / (float) std::max(1, decimOS_ ? decimOS_ : 1);
    }
    inline void decimPush(float x)
    {
        decimRing_[decimPos_] = x;
        decimPos_ = (decimPos_ + 1 == decimN_) ? 0 : decimPos_ + 1;
    }
    inline float decimRead() const
    {
        float acc = 0.0f;
        int idx = decimPos_;            // oldest sample
        for (int n = 0; n < decimN_; ++n)
        {
            acc += decimH_[n] * decimRing_[idx];
            idx = (idx + 1 == decimN_) ? 0 : idx + 1;
        }
        return acc;
    }

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
    float vColl_ = 0.0f;   // Physical: collector voltage state [0,1]

    bool  prevPower_ = false;
    float ramp_ = 0.0f, led_ = 0.0f;

    float hp1x_ = 0, hp1y_ = 0, hp2x_ = 0, hp2y_ = 0, lpY_ = 0;
    float amountS_ = 1.0f, volS_ = 0.5f;

    // Oversampling state
    float dtTick_ = 0.0f;          // per-subsample dt (= dt_/OS)
    int   lastOS_ = 0;

    static constexpr int kMaxFir = 129;        // 16*8 + 1 for OS=8
    float decimH_[kMaxFir] = {0};
    float decimRing_[kMaxFir] = {0};
    int   decimN_ = 1, decimPos_ = 0, decimOS_ = 1;
};

} // namespace siren
