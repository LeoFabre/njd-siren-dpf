#pragma once
#include "DistrhoUI.hpp"
#include "ParameterMetadata.hpp"

START_NAMESPACE_DISTRHO

// Faithful rendering of the NJD faceplate: TONE/MODE/SPEED rotary switches,
// signal LED, VOLUME knob, TONE/SIREN/TRIG pushbuttons, OFF/ON rocker (= hold).
class SirenUI : public UI
{
public:
    SirenUI();

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    void parameterChanged(uint32_t index, float value) override;

private:
    static constexpr float kWidth  = 740.f;
    static constexpr float kHeight = 700.f;

    void drawRotarySwitch(float cx, float cy, int pos, int numPos,
                          const char* const* labels, uint capColor, const char* name,
                          bool waveGlyphs = false);
    void drawWaveGlyph(float cx, float cy, int mode);
    void drawVolumeKnob(float cx, float cy, float norm);
    void drawDevKnob(float cx, float cy, int paramIdx, const char* label);
    void drawLed(float cx, float cy, float level);
    void drawPushButton(float cx, float cy, bool pressed, uint color, const char* name);
    void drawRocker(float cx, float cy, bool on);

    int hitTest(float mx, float my) const;   // returns a Param index or -1

    float values_[siren::kNumControlParams];
    float level_ = 0.f;
    int   pressedBtn_ = -1;   // param index of the held pushbutton
    int   draggingCont_ = -1; // param index of the dragged continuous knob
    int   draggingSel_ = -1;  // param index of the dragged rotary switch
    int   dragStartPos_ = 0;
    float dragStartY_ = 0.f;
    float dragStartN_ = 0.f;
    bool  resLoaded_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SirenUI)
};

END_NAMESPACE_DISTRHO
