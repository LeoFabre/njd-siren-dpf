#pragma once
#include <cstdint>

namespace siren {

enum class Param : uint32_t {
    // Front panel (mirrors the NJD faceplate; the UI shows exactly these)
    tone,        // S5 rotary 1/2/3: osc-rate ladder (470k/220k/100k)
    mode,        // S4 rotary 1/2/3/4: modulation routing per osc half
    speed,       // S3 rotary 1/2/3/OFF: LFO rate, OFF = LFO stopped
    volume,
    toneBtn,     // pushbutton: force fixed pitch (D1/D2/R8/R9)
    sirenBtn,    // pushbutton S2: slow-charge C5 through R25 (wind-up)
    trigger,     // pushbutton S1: fast-charge C5 + reset the LFO square
    hold,        // rocker S6: latched power (buttons power while held)
    // Fine-tune (host/webapp only, not on the panel)
    charge,      // R25*C5 wind-up time constant
    discharge,   // R23/R24*C5 fall time constant
    amount,      // modulation scale about 4.5 V (not in circuit)
    drive,       // post drive (not in circuit)
    // Output params
    ledLevel,    // T5/LED1: follows C5 (the sweep), drives the panel LED
};

constexpr int kNumPanelParams   = 8;
constexpr int kNumControlParams = 12;
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
    // 12 ledLevel (output)
    { "ledLevel",  "LED Level",  0.0f,   1.0f,   0.0f, false, false, true,  false, 0, nullptr },
};

inline const ParamInfo& paramInfo(Param p) {
    return kParams[static_cast<uint32_t>(p)];
}

} // namespace siren
