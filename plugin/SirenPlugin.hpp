#pragma once
#include "DistrhoPlugin.hpp"
#include "ParameterMetadata.hpp"
#include "SirenEngine.hpp"

START_NAMESPACE_DISTRHO
class SirenPlugin : public Plugin {
public:
    SirenPlugin();
protected:
    const char* getLabel() const override { return "Siren"; }
    const char* getDescription() const override { return "NJD-style dub siren generator"; }
    const char* getMaker() const override { return "Nexus"; }
    const char* getLicense() const override { return "Apache-2.0"; }
    uint32_t getVersion() const override { return d_version(0,1,0); }
    int64_t getUniqueId() const override { return d_cconst('N','j','S','r'); }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void  setParameterValue(uint32_t index, float value) override;
    void  activate() override;
    void  run(const float** inputs, float** outputs, uint32_t frames) override;
private:
    siren::SirenEngine engine_;
    float params_[siren::kNumControlParams] = {};
    float outLevel_ = 0.0f;
    bool  requiresUpdate_ = true;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SirenPlugin)
};
END_NAMESPACE_DISTRHO
