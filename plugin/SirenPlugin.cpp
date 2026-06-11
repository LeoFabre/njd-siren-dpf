#include "SirenPlugin.hpp"

START_NAMESPACE_DISTRHO

using siren::Param;
using siren::ParamInfo;
using siren::paramInfo;
using siren::kNumControlParams;
using siren::SirenEngine;

static inline int idx(Param p) { return static_cast<int>(p); }

SirenPlugin::SirenPlugin()
    : Plugin(kNumControlParams, 0, 0)
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
    return params_[index];
}

void SirenPlugin::setParameterValue(uint32_t index, float value)
{
    params_[index] = value;
    requiresUpdate_ = true;
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
        const SirenEngine::Params p {
            .gate           = params_[idx(Param::trigger)] > 0.5f
                           || params_[idx(Param::hold)] > 0.5f,
            .volume_dB      = params_[idx(Param::volume)],
            .pitch01        = params_[idx(Param::pitch)],
            .attack_ms      = params_[idx(Param::attack)],
            .amount_pct     = params_[idx(Param::amount)],
            .lfo1Wave       = (SirenEngine::Wave)(int) params_[idx(Param::lfo1Wave)],
            .lfo1Shape_pct  = params_[idx(Param::lfo1Shape)],
            .lfo1Rate_Hz    = params_[idx(Param::lfo1Rate)],
            .lfo2Wave       = (SirenEngine::Wave)(int) params_[idx(Param::lfo2Wave)],
            .lfo2Amount_pct = params_[idx(Param::lfo2Amount)],
            .lfo2Rate_Hz    = params_[idx(Param::lfo2Rate)],
            .flavor_pct     = params_[idx(Param::flavor)],
            .flavorTime_ms  = params_[idx(Param::flavorTime)],
            .drive_dB       = params_[idx(Param::drive)],
            .sparkle_pct    = params_[idx(Param::sparkle)],
        };
        engine_.setParameters(p);
        requiresUpdate_ = false;
    }

    engine_.process(outputs, 2, (int) frames);
}

Plugin* createPlugin() { return new SirenPlugin(); }

END_NAMESPACE_DISTRHO
