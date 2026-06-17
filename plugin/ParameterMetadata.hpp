#pragma once
#include <cstdint>

namespace siren {

enum class Param : uint32_t {
    // Front panel (mirrors the NJD faceplate; the UI shows exactly these)
    tone,        // S5 rotary 1/2/3: osc-rate ladder (470k/220k/100k)
    mode,        // S4 rotary 1/2/3/4: modulation routing per osc half
    speed,       // S3 rotary 1/2/3/OFF: LFO rate, OFF = LFO stopped
    volume,
    toneBtn,     // BP2/S1: fast-charge C5 + hold the LFO (steady note)
    sirenBtn,    // BP1/S2: slow-charge C5 through R25 (wind-up)
    trigger,     // momentary power, wired across SIREN ON
    hold,        // rocker: latched power (same path as trigger)
    // Fine-tune (host/webapp only, not on the panel)
    charge,      // R25*C5 wind-up time constant
    discharge,   // R23/R24*C5 fall time constant
    amount,      // modulation scale about 4.5 V (not in circuit)
    drive,       // post drive (not in circuit)
    // Dev/physics trims (component tolerances of the real unit)
    bleed,       // half-A source leaking into half B's pull-up
    capRatio,    // C2/C1 mismatch
    vbe,         // transistor threshold (stall point / dive shape)
    edge,        // collector edge rounding (output LP)
    // Anti-alias + circuit-faithful generator
    oversample,  // 0=1x, 1=2x, 2=4x, 3=8x
    model,       // 0 = Classic (square+polyBLEP), 1 = Physical (collector)
    collTau,     // tau_c collector recovery (us), Physical only
    fallTau,     // tau_fall turn-on fall (us), Physical only
    // Output params
    ledLevel,    // T5/LED1: follows C5 (the sweep), drives the panel LED
};

constexpr int kNumPanelParams   = 8;
constexpr int kNumControlParams = 20;
constexpr int kNumOutputParams  = 1;

struct ParamInfo {
    const char* symbol;
    const char* name;
    float min;
    float max;
    float def;
    bool isBool;
    bool isInteger;
    bool isOutput;
    bool isLogarithmic;
    int numChoices;
    const char* const* choices;
};

static const char* kChoicesTone[]  = { "1", "2", "3" };
static const char* kChoicesMode[]  = { "1", "2", "3", "4" };
static const char* kChoicesSpeed[] = { "1", "2", "3", "OFF" };
static const char* kChoicesOS[]    = { "1x", "2x", "4x", "8x" };
static const char* kChoicesModel[] = { "Classic", "Physical" };

// Ordering must exactly match enum class Param above.
static constexpr ParamInfo kParams[kNumControlParams + kNumOutputParams] = {
    // 0  tone
    { "tone",      "Tone",       0.0f,   2.0f,   1.0f, false, true,  false, false, 3, kChoicesTone },
    // 1  mode
    { "mode",      "Mode",       0.0f,   3.0f,   0.0f, false, true,  false, false, 4, kChoicesMode },
    // 2  speed
    { "speed",     "Speed",      0.0f,   3.0f,   1.0f, false, true,  false, false, 4, kChoicesSpeed },
    // 3  volume
    { "volume",    "Volume",   -60.0f,   6.0f,  -6.0f, false, false, false, false, 0, nullptr },
    // 4  toneBtn
    { "toneBtn",   "Tone Button", 0.0f,  1.0f,   0.0f, true,  false, false, false, 0, nullptr },
    // 5  sirenBtn
    { "sirenBtn",  "Siren Button",0.0f,  1.0f,   0.0f, true,  false, false, false, 0, nullptr },
    // 6  trigger
    { "trigger",   "Trigger",    0.0f,   1.0f,   0.0f, true,  false, false, false, 0, nullptr },
    // 7  hold
    { "hold",      "Hold",       0.0f,   1.0f,   0.0f, true,  false, false, false, 0, nullptr },
    // 8  charge
    { "charge",    "Charge",     0.1f,   4.0f,   1.0f, false, false, false, true,  0, nullptr },
    // 9  discharge
    { "discharge", "Discharge",  0.2f,   8.0f,   2.0f, false, false, false, true,  0, nullptr },
    // 10 amount
    { "amount",    "Mod Amount", 0.0f, 200.0f, 100.0f, false, false, false, false, 0, nullptr },
    // 11 drive
    { "drive",     "Drive",      0.0f,  12.0f,   4.0f, false, false, false, false, 0, nullptr },
    // 12 bleed
    { "bleed",     "Mod Bleed",  0.0f, 100.0f,  35.0f, false, false, false, false, 0, nullptr },
    // 13 capRatio
    { "capRatio",  "Cap Ratio",  0.5f,   2.0f,   1.0f, false, false, false, false, 0, nullptr },
    // 14 vbe
    { "vbe",       "Vbe",        0.3f,   1.2f,  0.65f, false, false, false, false, 0, nullptr },
    // 15 edge
    { "edge",      "Edge LP",  500.0f, 16000.0f, 10000.0f, false, false, false, true, 0, nullptr },
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
};

inline const ParamInfo& paramInfo(Param p) {
    return kParams[static_cast<uint32_t>(p)];
}

} // namespace siren
