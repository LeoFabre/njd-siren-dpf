#include "SirenPlugin.hpp"
#include "../dsp/DenormalGuard.hpp"
#include <cstring>

START_NAMESPACE_DISTRHO

using siren::Param;
using siren::ParamInfo;
using siren::paramInfo;
using siren::kNumControlParams;
using siren::kNumOutputParams;
using siren::SirenEngine;

static inline int idx(Param p) { return static_cast<int>(p); }

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
        : ledLevel_;
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
    // Unconditional re-arm: FPCR is per-thread and the write is a couple of
    // system-register ops — cheaper than a thread_local lookup, and avoids
    // dynamic-TLS allocation on the out-of-band audio thread.
    ftz::enable();

    for (int ch = 0; ch < 2; ++ch)
        if (outputs[ch] != inputs[ch])
            std::memcpy(outputs[ch], inputs[ch], frames * sizeof(float));

    if (requiresUpdate_)
    {
        const SirenEngine::Params p {
            .trigBtn      = params_[idx(Param::trigger)]  > 0.5f,
            .sirenBtn     = params_[idx(Param::sirenBtn)] > 0.5f,
            .toneBtn      = params_[idx(Param::toneBtn)]  > 0.5f,
            .power        = params_[idx(Param::hold)]     > 0.5f,
            .tone         = (int) params_[idx(Param::tone)],
            .mode         = (int) params_[idx(Param::mode)],
            .speed        = (int) params_[idx(Param::speed)],
            .volume_dB    = params_[idx(Param::volume)],
            .charge_s     = params_[idx(Param::charge)],
            .discharge_s  = params_[idx(Param::discharge)],
            .amount_pct   = params_[idx(Param::amount)],
            .drive_dB     = params_[idx(Param::drive)],
            .bleed_pct    = params_[idx(Param::bleed)],
            .capRatio     = params_[idx(Param::capRatio)],
            .vbe_V        = params_[idx(Param::vbe)],
            .edgeHz       = params_[idx(Param::edge)],
        };
        engine_.setParameters(p);
        requiresUpdate_ = false;
    }

    engine_.process(outputs, 2, (int) frames);
    ledLevel_ = engine_.ledLevel();
}

Plugin* createPlugin() { return new SirenPlugin(); }

END_NAMESPACE_DISTRHO
