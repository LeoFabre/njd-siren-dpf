#include "SirenUI.hpp"
#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using siren::Param;
using siren::paramInfo;
using siren::kNumControlParams;

static inline int idx(Param p) { return static_cast<int>(p); }
static inline float deg2rad(float d) { return d * 0.017453293f; }
static inline Color rgb(uint c)
{
    return Color((int)((c >> 16) & 0xff), (int)((c >> 8) & 0xff), (int)(c & 0xff));
}

// ---- Faceplate geometry ----------------------------------------------------
static constexpr float kToneX = 140.f,  kToneY = 185.f;
static constexpr float kModeX = 370.f,  kModeY = 185.f;
static constexpr float kSpeedX = 600.f, kSpeedY = 185.f;
static constexpr float kLedX = 370.f,   kLedY = 355.f;
static constexpr float kVolX = 140.f,   kVolY = 480.f;
static constexpr float kBtnY = 475.f;
static constexpr float kBtnToneX = 305.f, kBtnSirenX = 390.f, kBtnTrigX = 475.f;
static constexpr float kRockX = 612.f,  kRockY = 470.f;
static constexpr float kKnobR = 50.f;

// Dev strip: physics trims to hunt the real unit's character by ear.
struct DevKnobDef { Param p; const char* label; const char* unit; };
static const DevKnobDef kDevKnobs[] = {
    { Param::bleed,     "BLEED",  "%"  },
    { Param::capRatio,  "C2/C1",  "x"  },
    { Param::vbe,       "VBE",    "V"  },
    { Param::edge,      "EDGE",   "Hz" },
    { Param::discharge, "DISCHG", "s"  },
    { Param::amount,    "AMOUNT", "%"  },
};
static constexpr int kNumDevKnobs = 6;
static constexpr float kDevY = 640.f;
static constexpr float kDevR = 16.f;
static inline float devKnobX(int i) { return 95.f + 110.f * (float) i; }

static float toNorm(const siren::ParamInfo& pi, float v)
{
    if (pi.isLogarithmic)
        return std::log(v / pi.min) / std::log(pi.max / pi.min);
    return (v - pi.min) / (pi.max - pi.min);
}

static float fromNorm(const siren::ParamInfo& pi, float n)
{
    if (pi.isLogarithmic)
        return pi.min * std::pow(pi.max / pi.min, n);
    return pi.min + n * (pi.max - pi.min);
}

static const char* kPosTone[]  = { "1", "2", "3" };
static const char* kPosMode[]  = { "1", "2", "3", "4" };
static const char* kPosSpeed[] = { "1", "2", "3", "OFF" };

// Detent angles: 40 degree steps centered around 12 o'clock, OFF trailing right.
static float detentAngleDeg(int pos, int numPos)
{
    const float span = 40.f * (float)(numPos - 1);
    return -90.f - span * 0.5f + 40.f * (float)pos;
}

SirenUI::SirenUI() : UI((uint)kWidth, (uint)kHeight)
{
    // Resizable window with manual uniform scaling: DGL's automaticallyScale
    // mis-maps mouse events on retina standalone builds (events arrive in
    // physical pixels but are not divided), so we scale the drawing and
    // un-scale the mouse ourselves via uiScale(). 740x700 stays the logical
    // coordinate space for everything below.
    const double sf = getScaleFactor();
    setGeometryConstraints((uint)(kWidth * sf * 0.5), (uint)(kHeight * sf * 0.5),
                           /*keepAspectRatio=*/true, /*automaticallyScale=*/false);
    setSize((uint)(kWidth * sf), (uint)(kHeight * sf));

    for (int i = 0; i < kNumControlParams; ++i)
        values_[i] = paramInfo(static_cast<Param>(i)).def;
}

float SirenUI::uiScale() const
{
    return (float) getWidth() / kWidth;
}

void SirenUI::parameterChanged(uint32_t index, float value)
{
    if (index < (uint32_t) kNumControlParams)
        values_[index] = value;
    else if (index == (uint32_t) idx(Param::ledLevel))
        level_ = value;
    repaint();
}

void SirenUI::onNanoDisplay()
{
    if (!resLoaded_) { loadSharedResources(); resLoaded_ = true; }
    fontFaceId(findFont(NANOVG_DEJAVU_SANS_TTF));

    const float s = uiScale();
    scale(s, s);

    // Brushed grey faceplate with engraved border and corner screws.
    beginPath();
    rect(0.f, 0.f, kWidth, kHeight);
    fillColor(rgb(0x9a9489));
    fill();

    beginPath();
    rect(14.f, 14.f, kWidth - 28.f, kHeight - 28.f);
    strokeColor(rgb(0x55514a));
    strokeWidth(2.f);
    stroke();
    strokeWidth(1.f);

    for (const auto& s : { std::pair<float,float>{26.f, 26.f},
                           {kWidth - 26.f, 26.f},
                           {26.f, kHeight - 26.f},
                           {kWidth - 26.f, kHeight - 26.f} })
    {
        beginPath();
        circle(s.first, s.second, 7.f);
        fillColor(rgb(0x7d7870));
        fill();
        strokeColor(rgb(0x4a463f));
        stroke();
        beginPath();
        moveTo(s.first - 4.f, s.second - 3.f);
        lineTo(s.first + 4.f, s.second + 3.f);
        strokeColor(rgb(0x4a463f));
        stroke();
    }

    drawRotarySwitch(kToneX, kToneY, (int) values_[idx(Param::tone)], 3,
                     kPosTone, 0x2e6b34, "TONE");
    drawRotarySwitch(kModeX, kModeY, (int) values_[idx(Param::mode)], 4,
                     kPosMode, 0xd6c93e, "MODE", /*waveGlyphs=*/true);
    drawRotarySwitch(kSpeedX, kSpeedY, (int) values_[idx(Param::speed)], 4,
                     kPosSpeed, 0xc43028, "SPEED");

    drawLed(kLedX, kLedY, level_);

    {
        const auto& pi = paramInfo(Param::volume);
        const float n = std::clamp((values_[idx(Param::volume)] - pi.min)
                                   / (pi.max - pi.min), 0.f, 1.f);
        drawVolumeKnob(kVolX, kVolY, n);
    }

    drawPushButton(kBtnToneX,  kBtnY, values_[idx(Param::toneBtn)]  > 0.5f, 0x2faa55, "TONE");
    drawPushButton(kBtnSirenX, kBtnY, values_[idx(Param::sirenBtn)] > 0.5f, 0xe8d23c, "SIREN");
    drawPushButton(kBtnTrigX,  kBtnY, values_[idx(Param::trigger)]  > 0.5f, 0xd8362c, "TRIG");

    drawRocker(kRockX, kRockY, values_[idx(Param::hold)] > 0.5f);

    // Dev strip separator + trims.
    beginPath();
    moveTo(26.f, 602.f);
    lineTo(kWidth - 26.f, 602.f);
    strokeColor(rgb(0x6f6a61));
    stroke();
    fontSize(10.f);
    textAlign(ALIGN_LEFT | ALIGN_BASELINE);
    fillColor(rgb(0x55514a));
    text(28.f, 616.f, "DEV — physics trims", nullptr);
    for (int i = 0; i < kNumDevKnobs; ++i)
        drawDevKnob(devKnobX(i), kDevY, idx(kDevKnobs[i].p), kDevKnobs[i].label);

    // Hover/drag tooltip with a readable value.
    if (hoverDev_ >= 0 && hoverDev_ < kNumDevKnobs)
    {
        const DevKnobDef& k = kDevKnobs[hoverDev_];
        const float v = values_[idx(k.p)];
        char buf[48];
        if (v >= 1000.f)
            std::snprintf(buf, sizeof(buf), "%s  %.0f %s", k.label, (double) v, k.unit);
        else
            std::snprintf(buf, sizeof(buf), "%s  %.2f %s", k.label, (double) v, k.unit);

        fontSize(15.f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        DGL_NAMESPACE::Rectangle<float> bounds;
        textBounds(0.f, 0.f, buf, nullptr, bounds);
        const float tw = bounds.getWidth() + 16.f;
        const float thh = 26.f;
        float tx = devKnobX(hoverDev_);
        tx = std::clamp(tx, 14.f + tw * 0.5f, kWidth - 14.f - tw * 0.5f);
        const float ty = kDevY - kDevR - 24.f;

        beginPath();
        roundedRect(tx - tw * 0.5f, ty - thh * 0.5f, tw, thh, 5.f);
        fillColor(Color(20, 20, 18, 0.92f));
        fill();
        strokeColor(rgb(0x6f6a61));
        stroke();
        fillColor(rgb(0xf0ead9));
        text(tx, ty, buf, nullptr);
    }
}

void SirenUI::drawDevKnob(float cx, float cy, int paramIdx, const char* label)
{
    const auto& pi = paramInfo(static_cast<Param>(paramIdx));
    const float n = std::clamp(toNorm(pi, values_[paramIdx]), 0.f, 1.f);

    beginPath();
    circle(cx, cy, kDevR);
    fillColor(rgb(0x2c2a26));
    fill();
    strokeColor(rgb(0x55514a));
    stroke();

    const float a = deg2rad(-225.f + n * 270.f);
    beginPath();
    moveTo(cx + std::cos(a) * 5.f, cy + std::sin(a) * 5.f);
    lineTo(cx + std::cos(a) * (kDevR - 2.f), cy + std::sin(a) * (kDevR - 2.f));
    strokeColor(rgb(0xd8d2c6));
    strokeWidth(2.f);
    stroke();
    strokeWidth(1.f);

    char buf[32];
    if (pi.max >= 1000.f)
        std::snprintf(buf, sizeof(buf), "%.0f", (double) values_[paramIdx]);
    else
        std::snprintf(buf, sizeof(buf), "%.2f", (double) values_[paramIdx]);
    fontSize(10.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x3c3933));
    text(cx, cy + kDevR + 13.f, label, nullptr);
    fillColor(rgb(0x55514a));
    text(cx, cy + kDevR + 25.f, buf, nullptr);
}

void SirenUI::drawRotarySwitch(float cx, float cy, int pos, int numPos,
                               const char* const* labels, uint capColor, const char* name,
                               bool waveGlyphs)
{
    pos = std::clamp(pos, 0, numPos - 1);

    // Position ticks and numerals (or waveform glyphs for the MODE switch).
    fontSize(15.f);
    textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
    for (int i = 0; i < numPos; ++i)
    {
        const float a = deg2rad(detentAngleDeg(i, numPos));
        const float c = std::cos(a), s = std::sin(a);
        beginPath();
        moveTo(cx + c * (kKnobR + 6.f), cy + s * (kKnobR + 6.f));
        lineTo(cx + c * (kKnobR + 14.f), cy + s * (kKnobR + 14.f));
        strokeColor(rgb(0x2a2a26));
        strokeWidth(2.f);
        stroke();
        strokeWidth(1.f);
        if (waveGlyphs)
        {
            drawWaveGlyph(cx + c * (kKnobR + 29.f), cy + s * (kKnobR + 29.f), i);
        }
        else
        {
            fillColor(rgb(0x2a2a26));
            text(cx + c * (kKnobR + 27.f), cy + s * (kKnobR + 27.f), labels[i], nullptr);
        }
    }

    // Black knob body with colored cap and white pointer at the detent.
    beginPath();
    circle(cx, cy + 3.f, kKnobR);
    fillColor(Color(0, 0, 0, 0.35f));
    fill();
    beginPath();
    circle(cx, cy, kKnobR);
    fillColor(rgb(0x161616));
    fill();
    strokeColor(rgb(0x040404));
    stroke();
    beginPath();
    circle(cx, cy, 26.f);
    fillColor(rgb(capColor));
    fill();
    strokeColor(rgb(0x101010));
    stroke();

    const float a = deg2rad(detentAngleDeg(pos, numPos));
    beginPath();
    moveTo(cx + std::cos(a) * 26.f, cy + std::sin(a) * 26.f);
    lineTo(cx + std::cos(a) * (kKnobR - 4.f), cy + std::sin(a) * (kKnobR - 4.f));
    strokeColor(rgb(0xf2f2f2));
    strokeWidth(4.f);
    stroke();
    strokeWidth(1.f);

    fontSize(22.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x2a2a26));
    text(cx, cy + kKnobR + 45.f, name, nullptr);
}

// Small icons for the MODE positions, matching kRoute in the engine:
// 1 wail (shaped LFO), 2 two-tone square w/ stall gaps, 3 shaped vs square
// (complex), 4 fixed tone.
void SirenUI::drawWaveGlyph(float cx, float cy, int mode)
{
    const float w = 22.f, h = 12.f;
    const float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    const float yT = cy - h * 0.5f, yB = cy + h * 0.5f;

    beginPath();
    switch (mode)
    {
    case 0: // wail: fast charge, slow discharge (the shaped LFO on C5)
        moveTo(x0, yB);
        lineTo(x0 + w * 0.3f, yT);
        lineTo(x1, yB);
        break;
    case 1: // two-tone: square bursts with gaps
        moveTo(x0, yT);
        lineTo(x0 + w * 0.45f, yT);
        lineTo(x0 + w * 0.45f, yB);
        lineTo(x0 + w * 0.65f, yB);
        lineTo(x0 + w * 0.65f, yT);
        lineTo(x1, yT);
        break;
    case 2: // complex: ramp then square chunk
        moveTo(x0, yB);
        lineTo(x0 + w * 0.3f, yT);
        lineTo(x0 + w * 0.5f, yB);
        lineTo(x0 + w * 0.5f, yT);
        lineTo(x1, yT);
        break;
    default: // fixed tone: flat line
        moveTo(x0, cy);
        lineTo(x1, cy);
        break;
    }
    strokeColor(rgb(0x2a2a26));
    strokeWidth(2.f);
    stroke();
    strokeWidth(1.f);
}

void SirenUI::drawVolumeKnob(float cx, float cy, float norm)
{
    beginPath();
    circle(cx, cy + 3.f, kKnobR);
    fillColor(Color(0, 0, 0, 0.35f));
    fill();
    beginPath();
    circle(cx, cy, kKnobR);
    fillColor(rgb(0x161616));
    fill();
    strokeColor(rgb(0x040404));
    stroke();
    beginPath();
    circle(cx, cy, 26.f);
    fillColor(rgb(0x2f7fa8));
    fill();
    strokeColor(rgb(0x101010));
    stroke();

    const float a = deg2rad(-225.f + norm * 270.f);
    beginPath();
    moveTo(cx + std::cos(a) * 26.f, cy + std::sin(a) * 26.f);
    lineTo(cx + std::cos(a) * (kKnobR - 4.f), cy + std::sin(a) * (kKnobR - 4.f));
    strokeColor(rgb(0xf2f2f2));
    strokeWidth(4.f);
    stroke();
    strokeWidth(1.f);

    fontSize(22.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x2a2a26));
    text(cx, cy + kKnobR + 42.f, "VOLUME", nullptr);
}

void SirenUI::drawLed(float cx, float cy, float level)
{
    const float b = std::clamp(level * 2.5f, 0.f, 1.f);

    // Bezel.
    beginPath();
    circle(cx, cy, 14.f);
    fillColor(rgb(0x6d685f));
    fill();
    strokeColor(rgb(0x3c3933));
    stroke();

    if (b > 0.02f)
    {
        beginPath();
        circle(cx, cy, 14.f + 10.f * b);
        fillColor(Color(255, 216, 74, 0.35f * b));
        fill();
    }

    beginPath();
    circle(cx, cy, 8.f);
    fillColor(Color((int)(90 + 165 * b), (int)(81 + 135 * b), (int)(47 + 27 * b)));
    fill();
    strokeColor(rgb(0x2c2a24));
    stroke();

    fontSize(15.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x2a2a26));
    text(cx, cy + 38.f, "PWR", nullptr);
}

void SirenUI::drawPushButton(float cx, float cy, bool pressed, uint color, const char* name)
{
    // Chrome mounting nut, then the colored dome.
    beginPath();
    circle(cx, cy, 21.f);
    fillColor(rgb(0xb9b9b9));
    fill();
    strokeColor(rgb(0x5e5e5e));
    stroke();
    beginPath();
    circle(cx, cy, 16.f);
    fillColor(rgb(0x8e8e8e));
    fill();

    const float r = pressed ? 11.f : 13.f;
    Color dome = rgb(color);
    if (pressed)
        dome = Color(dome, Color(255, 255, 255), 0.35f);
    beginPath();
    circle(cx, cy, r);
    fillColor(dome);
    fill();
    strokeColor(rgb(0x222222));
    stroke();
    beginPath();
    circle(cx - r * 0.3f, cy - r * 0.35f, r * 0.3f);
    fillColor(Color(255, 255, 255, 0.45f));
    fill();

    fontSize(16.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x2a2a26));
    text(cx, cy + 48.f, name, nullptr);
}

void SirenUI::drawRocker(float cx, float cy, bool on)
{
    fontSize(18.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(rgb(0x2a2a26));
    text(cx, cy - 58.f, "OFF", nullptr);
    text(cx, cy + 72.f, "ON", nullptr);

    // Threaded collar and lever; lever throws DOWN for ON, like the photo.
    beginPath();
    circle(cx, cy, 15.f);
    fillColor(rgb(0x9c9c9c));
    fill();
    strokeColor(rgb(0x4f4f4f));
    stroke();
    beginPath();
    circle(cx, cy, 9.f);
    fillColor(rgb(0x6f6f6f));
    fill();

    const float dir = on ? 1.f : -1.f;
    beginPath();
    moveTo(cx, cy);
    lineTo(cx, cy + dir * 34.f);
    strokeColor(rgb(0xcfcfcf));
    strokeWidth(9.f);
    stroke();
    strokeWidth(1.f);
    beginPath();
    circle(cx, cy + dir * 34.f, 6.f);
    fillColor(rgb(0xe2e2e2));
    fill();
    strokeColor(rgb(0x6f6f6f));
    stroke();
}

// ---- Interaction ------------------------------------------------------------

int SirenUI::hitTest(float mx, float my) const
{
    const auto inCircle = [&](float cx, float cy, float r)
    { return (mx - cx) * (mx - cx) + (my - cy) * (my - cy) <= r * r; };

    if (inCircle(kToneX, kToneY, kKnobR + 14.f))  return idx(Param::tone);
    if (inCircle(kModeX, kModeY, kKnobR + 14.f))  return idx(Param::mode);
    if (inCircle(kSpeedX, kSpeedY, kKnobR + 14.f)) return idx(Param::speed);
    if (inCircle(kVolX, kVolY, kKnobR + 14.f))    return idx(Param::volume);
    if (inCircle(kBtnToneX, kBtnY, 24.f))         return idx(Param::toneBtn);
    if (inCircle(kBtnSirenX, kBtnY, 24.f))        return idx(Param::sirenBtn);
    if (inCircle(kBtnTrigX, kBtnY, 24.f))         return idx(Param::trigger);
    if (mx >= kRockX - 28.f && mx <= kRockX + 28.f
        && my >= kRockY - 48.f && my <= kRockY + 48.f) return idx(Param::hold);
    for (int i = 0; i < kNumDevKnobs; ++i)
        if (inCircle(devKnobX(i), kDevY, kDevR + 6.f))
            return idx(kDevKnobs[i].p);
    return -1;
}

bool SirenUI::onMouse(const MouseEvent& ev)
{
    if (ev.button != 1)
        return false;

    if (ev.press)
    {
        const float s = uiScale();
        const float mx = ev.pos.getX() / s;
        const float my = ev.pos.getY() / s;
        const int i = hitTest(mx, my);
        if (i < 0)
            return false;

        if (i == idx(Param::tone) || i == idx(Param::mode) || i == idx(Param::speed))
        {
            // Detented knob: drag up/down to step through the positions.
            draggingSel_ = i;
            dragStartY_ = my;
            dragStartPos_ = (int) values_[i];
            return true;
        }
        const auto& pi = paramInfo(static_cast<Param>(i));
        if (!pi.isBool && !pi.isInteger)
        {
            // Continuous knob (volume or a dev trim): vertical drag.
            draggingCont_ = i;
            dragStartY_ = my;
            dragStartN_ = std::clamp(toNorm(pi, values_[i]), 0.f, 1.f);
            return true;
        }
        if (i == idx(Param::hold))
        {
            const float nv = values_[i] > 0.5f ? 0.f : 1.f;
            values_[i] = nv;
            setParameterValue((uint32_t) i, nv);
            repaint();
            return true;
        }

        // Pushbuttons are momentary: down on press, up on release.
        pressedBtn_ = i;
        values_[i] = 1.f;
        setParameterValue((uint32_t) i, 1.f);
        repaint();
        return true;
    }

    draggingCont_ = -1;
    draggingSel_ = -1;
    if (pressedBtn_ >= 0)
    {
        values_[pressedBtn_] = 0.f;
        setParameterValue((uint32_t) pressedBtn_, 0.f);
        pressedBtn_ = -1;
        repaint();
    }
    return true;
}

bool SirenUI::onMotion(const MotionEvent& ev)
{
    const float s = uiScale();
    const float mx = ev.pos.getX() / s;
    const float my = ev.pos.getY() / s;

    if (draggingSel_ >= 0)
    {
        // One detent per 30 px, dragging up increases.
        const int n = paramInfo(static_cast<Param>(draggingSel_)).numChoices;
        const int steps = (int) std::lround((dragStartY_ - my) / 30.f);
        const int pos = std::clamp(dragStartPos_ + steps, 0, n - 1);
        if (pos != (int) values_[draggingSel_])
        {
            values_[draggingSel_] = (float) pos;
            setParameterValue((uint32_t) draggingSel_, (float) pos);
            repaint();
        }
        return true;
    }

    // Tooltip hover tracking (also while dragging a dev knob).
    {
        int hover = -1;
        for (int i = 0; i < kNumDevKnobs; ++i)
        {
            if (draggingCont_ == idx(kDevKnobs[i].p)
                || (draggingCont_ < 0 && draggingSel_ < 0
                    && (mx - devKnobX(i)) * (mx - devKnobX(i))
                       + (my - kDevY) * (my - kDevY) <= (kDevR + 6.f) * (kDevR + 6.f)))
            {
                hover = i;
                break;
            }
        }
        if (hover != hoverDev_)
        {
            hoverDev_ = hover;
            repaint();
        }
    }

    if (draggingCont_ < 0)
        return false;

    const auto& pi = paramInfo(static_cast<Param>(draggingCont_));
    const float dy = dragStartY_ - my;
    const float n = std::clamp(dragStartN_ + dy / 200.f, 0.f, 1.f);
    const float v = fromNorm(pi, n);
    values_[draggingCont_] = v;
    setParameterValue((uint32_t) draggingCont_, v);
    repaint();
    return true;
}

UI* createUI() { return new SirenUI(); }

END_NAMESPACE_DISTRHO
