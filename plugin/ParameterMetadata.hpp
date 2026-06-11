#pragma once
#include <cstdint>

namespace siren {

enum class Param : uint32_t {
    // Front panel (mirrors the NJD faceplate; the UI shows exactly these)
    tone,        // rotary switch 1/2/3: base pitch
    mode,        // rotary switch 1/2/3/4: sweep character (LFO wave + shape)
    speed,       // rotary switch 1/2/3/OFF: sweep rate, OFF = fixed tone
    volume,
    toneBtn,     // pushbutton: fixed tone (no sweep)
    sirenBtn,    // pushbutton: siren with wind-up (slow LFO charge)
    trigger,     // pushbutton: instant siren
    hold,        // rocker switch: latched trigger
    // Fine-tune (not on the panel, host/webapp only)
    attack,
    amount,
    lfo2Wave,
    lfo2Amount,
    lfo2Rate,
    flavor,
    flavorTime,
    drive,
    sparkle,
    discharge,   // SPEED->OFF capacitor discharge time (s)
    // Output params
    outLevel,    // post-volume peak, drives the panel LED
};

constexpr int kNumPanelParams   = 8;
constexpr int kNumControlParams = 18;
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

static const char* kChoicesTone[]    = { "1", "2", "3" };
static const char* kChoicesMode[]    = { "1", "2", "3", "4" };
static const char* kChoicesSpeed[]   = { "1", "2", "3", "OFF" };
static const char* kChoicesLfoWave[] = { "SINE", "TRI", "SAW", "SQUARE" };

// Ordering must exactly match enum class Param above.
static constexpr ParamInfo kParams[kNumControlParams + kNumOutputParams] = {
    // 0  tone
    { "tone",       "Tone",         0.0f,    2.0f,    1.0f, false, true,  false, false, 3, kChoicesTone },
    // 1  mode
    { "mode",       "Mode",         0.0f,    3.0f,    0.0f, false, true,  false, false, 4, kChoicesMode },
    // 2  speed
    { "speed",      "Speed",        0.0f,    3.0f,    1.0f, false, true,  false, false, 4, kChoicesSpeed },
    // 3  volume
    { "volume",     "Volume",     -60.0f,    6.0f,   -6.0f, false, false, false, false, 0, nullptr },
    // 4  toneBtn
    { "toneBtn",    "Tone Button",  0.0f,    1.0f,    0.0f, true,  false, false, false, 0, nullptr },
    // 5  sirenBtn
    { "sirenBtn",   "Siren Button", 0.0f,    1.0f,    0.0f, true,  false, false, false, 0, nullptr },
    // 6  trigger
    { "trigger",    "Trigger",      0.0f,    1.0f,    0.0f, true,  false, false, false, 0, nullptr },
    // 7  hold
    { "hold",       "Hold",         0.0f,    1.0f,    0.0f, true,  false, false, false, 0, nullptr },
    // 8  attack
    { "attack",     "Attack",       0.0f,  800.0f,    0.0f, false, false, false, false, 0, nullptr },
    // 9  amount
    { "amount",     "Global Amount",0.0f,  200.0f,  100.0f, false, false, false, false, 0, nullptr },
    // 10 lfo2Wave
    { "lfo2Wave",   "LFO2 Wave",    0.0f,    3.0f,    0.0f, false, true,  false, false, 4, kChoicesLfoWave },
    // 11 lfo2Amount
    { "lfo2Amount", "LFO2 Amount",  0.0f,  100.0f,    0.0f, false, false, false, false, 0, nullptr },
    // 12 lfo2Rate
    { "lfo2Rate",   "LFO2 Rate",    0.05f,  30.0f,    1.0f, false, false, false, true,  0, nullptr },
    // 13 flavor
    { "flavor",     "NJD Flavor",   0.0f,  100.0f,   75.0f, false, false, false, false, 0, nullptr },
    // 14 flavorTime
    { "flavorTime", "Flavor Time",  0.1f,    5.0f,    1.5f, false, false, false, false, 0, nullptr },
    // 15 drive
    { "drive",      "Drive",        0.0f,   12.0f,    4.0f, false, false, false, false, 0, nullptr },
    // 16 sparkle
    { "sparkle",    "Sparkle",      0.0f,  100.0f,   50.0f, false, false, false, false, 0, nullptr },
    // 17 discharge
    { "discharge",  "Discharge",    0.2f,    8.0f,    2.0f, false, false, false, true,  0, nullptr },
    // 18 outLevel (output)
    { "outLevel",   "Out Level",    0.0f,    2.0f,    0.0f, false, false, true,  false, 0, nullptr },
};

inline const ParamInfo& paramInfo(Param p) {
    return kParams[static_cast<uint32_t>(p)];
}

} // namespace siren
