#include "SirenPlugin.hpp"
#include <algorithm>
#include <cstring>

START_NAMESPACE_DISTRHO

using siren::Param;
using siren::ParamInfo;
using siren::paramInfo;
using siren::kNumControlParams;
using siren::kNumOutputParams;
using siren::SirenEngine;

static inline int idx(Param p) { return static_cast<int>(p); }

// Rotary-switch detents, like the resistor ladders behind the real knobs.
static constexpr float kTonePitch01[3] = { 0.30f, 0.55f, 0.80f };
static constexpr float kSpeedHz[3]     = { 2.0f, 5.0f, 11.0f };
struct ModeDef { SirenEngine::Wave wave; float shape_pct; };
static constexpr ModeDef kModes[4] = {
    { SirenEngine::Wave::Tri,    0.0f },   // 1: classic rise/fall wail
    { SirenEngine::Wave::Saw,    0.0f },   // 2: falling sweep
    { SirenEngine::Wave::Square, 0.0f },   // 3: two-tone
    { SirenEngine::Wave::Tri,   70.0f },   // 4: hard wail (clipped tri)
};

SirenPlugin::SirenPlugin()
    : Plugin(kNumControlParams + kNumOutputParams, 0, 0)
{
    for (int i = 0; i < kNumControlParams; ++i)
        params_[i] = paramInfo(static_cast<Param>(i)).def;
}

void SirenPlugin::initParameter(uint32_t index, Parameter& parameter)
{
    const ParamInfo& pi = paramInfo(static_cast<Param>(index));

    parameter.name   = pi.name;
    parameter.symbol = pi.symbol;
    parameter.ranges.min = pi.min;
    parameter.ranges.def = pi.def;
    parameter.ranges.max = pi.max;

    parameter.hints = kParameterIsAutomatable;
    if (pi.isBool)        parameter.hints |= kParameterIsBoolean;
    if (pi.isInteger)     parameter.hints |= kParameterIsInteger;
    if (pi.isLogarithmic) parameter.hints |= kParameterIsLogarithmic;
    if (pi.isOutput)      parameter.hints |= kParameterIsOutput;

    if (pi.numChoices > 0)
    {
        parameter.enumValues.count = (uint32_t) pi.numChoices;
        parameter.enumValues.restrictedMode = true;
        auto* ev = new ParameterEnumerationValue[pi.numChoices];
        for (int i = 0; i < pi.numChoices; ++i)
        {
            ev[i].value = (float) i;
            ev[i].label = pi.choices[i];
        }
        parameter.enumValues.values = ev;
    }
}

float SirenPlugin::getParameterValue(uint32_t index) const
{
    return index < (uint32_t) kNumControlParams
        ? params_[index]
        : outLevel_;
}

void SirenPlugin::setParameterValue(uint32_t index, float value)
{
    if (index < (uint32_t) kNumControlParams)
    {
        params_[index] = value;
        requiresUpdate_ = true;
    }
}

void SirenPlugin::activate()
{
    engine_.prepare(getSampleRate(), (int) getBufferSize());
    requiresUpdate_ = true;
}

void SirenPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    for (int ch = 0; ch < 2; ++ch)
        if (outputs[ch] != inputs[ch])
            std::memcpy(outputs[ch], inputs[ch], frames * sizeof(float));

    if (requiresUpdate_)
    {
        const bool toneB  = params_[idx(Param::toneBtn)] > 0.5f;
        const bool sirenB = params_[idx(Param::sirenBtn)] > 0.5f;
        const bool trigB  = params_[idx(Param::trigger)] > 0.5f
                         || params_[idx(Param::hold)] > 0.5f;

        const int toneIdx  = std::clamp((int) params_[idx(Param::tone)],  0, 2);
        const int modeIdx  = std::clamp((int) params_[idx(Param::mode)],  0, 3);
        const int speedIdx = std::clamp((int) params_[idx(Param::speed)], 0, 3);

        // SIREN alone winds the pitch up like the real button's slow LFO
        // charge; TRIG and the rocker fire instantly.
        float attack = params_[idx(Param::attack)];
        if (sirenB && !trigB && !toneB)
            attack = std::max(attack, 700.0f);

        const SirenEngine::Params p {
            .gate           = toneB || sirenB || trigB,
            .volume_dB      = params_[idx(Param::volume)],
            .pitch01        = kTonePitch01[toneIdx],
            .attack_ms      = attack,
            .amount_pct     = params_[idx(Param::amount)],
            .lfo1Wave       = kModes[modeIdx].wave,
            .lfo1Shape_pct  = kModes[modeIdx].shape_pct,
            .lfo1Rate_Hz    = kSpeedHz[std::min(speedIdx, 2)],
            .lfo2Wave       = (SirenEngine::Wave)(int) params_[idx(Param::lfo2Wave)],
            .lfo2Amount_pct = params_[idx(Param::lfo2Amount)],
            .lfo2Rate_Hz    = params_[idx(Param::lfo2Rate)],
            .flavor_pct     = params_[idx(Param::flavor)],
            .flavorTime_ms  = params_[idx(Param::flavorTime)],
            .drive_dB       = params_[idx(Param::drive)],
            .sparkle_pct    = params_[idx(Param::sparkle)],
            .sweepEnabled   = !toneB && speedIdx < 3,
        };
        engine_.setParameters(p);
        requiresUpdate_ = false;
    }

    engine_.process(outputs, 2, (int) frames);
    outLevel_ = engine_.outputLevel();
}

Plugin* createPlugin() { return new SirenPlugin(); }

END_NAMESPACE_DISTRHO
