#pragma once
#include <cstdint>

namespace siren {

enum class Param : uint32_t {
    trigger,
    hold,
    volume,
    pitch,
    attack,
    amount,
    lfo1Wave,
    lfo1Shape,
    lfo1Rate,
    lfo2Wave,
    lfo2Amount,
    lfo2Rate,
    flavor,
    flavorTime,
    drive,
    sparkle,
};

constexpr int kNumControlParams = 16;

struct ParamInfo {
    const char* symbol;
    const char* name;
    float min;
    float max;
    float def;
    bool isBool;
    bool isInteger;
    bool isLogarithmic;
    int numChoices;
    const char* const* choices;
};

static const char* kChoicesLfoWave[] = { "SINE", "TRI", "SAW", "SQUARE" };

// Ordering must exactly match enum class Param above.
static constexpr ParamInfo kParams[kNumControlParams] = {
    // 0  trigger
    { "trigger",    "Trigger",      0.0f,    1.0f,    0.0f, true,  false, false, 0, nullptr },
    // 1  hold
    { "hold",       "Hold",         0.0f,    1.0f,    0.0f, true,  false, false, 0, nullptr },
    // 2  volume
    { "volume",     "Volume",     -60.0f,    6.0f,   -6.0f, false, false, false, 0, nullptr },
    // 3  pitch
    { "pitch",      "Pitch",        0.0f,    1.0f,    0.5f, false, false, false, 0, nullptr },
    // 4  attack
    { "attack",     "Attack",       0.0f,  800.0f,    0.0f, false, false, false, 0, nullptr },
    // 5  amount
    { "amount",     "Global Amount",0.0f,  200.0f,  100.0f, false, false, false, 0, nullptr },
    // 6  lfo1Wave
    { "lfo1Wave",   "LFO1 Wave",    0.0f,    3.0f,    1.0f, false, true,  false, 4, kChoicesLfoWave },
    // 7  lfo1Shape
    { "lfo1Shape",  "LFO1 Shape",   0.0f,  100.0f,    0.0f, false, false, false, 0, nullptr },
    // 8  lfo1Rate
    { "lfo1Rate",   "LFO1 Rate",    1.0f,   15.0f,    6.0f, false, false, false, 0, nullptr },
    // 9  lfo2Wave
    { "lfo2Wave",   "LFO2 Wave",    0.0f,    3.0f,    0.0f, false, true,  false, 4, kChoicesLfoWave },
    // 10 lfo2Amount
    { "lfo2Amount", "LFO2 Amount",  0.0f,  100.0f,    0.0f, false, false, false, 0, nullptr },
    // 11 lfo2Rate
    { "lfo2Rate",   "LFO2 Rate",    0.05f,  30.0f,    1.0f, false, false, true,  0, nullptr },
    // 12 flavor
    { "flavor",     "NJD Flavor",   0.0f,  100.0f,   75.0f, false, false, false, 0, nullptr },
    // 13 flavorTime
    { "flavorTime", "Flavor Time",  0.1f,    5.0f,    1.5f, false, false, false, 0, nullptr },
    // 14 drive
    { "drive",      "Drive",        0.0f,   12.0f,    4.0f, false, false, false, 0, nullptr },
    // 15 sparkle
    { "sparkle",    "Sparkle",      0.0f,  100.0f,   50.0f, false, false, false, 0, nullptr },
};

inline const ParamInfo& paramInfo(Param p) {
    return kParams[static_cast<uint32_t>(p)];
}

} // namespace siren
