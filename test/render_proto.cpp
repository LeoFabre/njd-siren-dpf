// Offline renderer for the prototype A/B: oversampling (anti-alias) and the
// dying-tail formant vowel. Writes mono 48 kHz 16-bit WAVs to listen to.
//   c++ -std=c++20 -O2 -I ../dsp -I ../plugin render_proto.cpp -o /tmp/render_proto
#include "SirenEngine.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>

using siren::SirenEngine;

static constexpr float kFs = 48000.0f;
static constexpr int   kBlock = 64;

static void writeWav(const std::string& path, const std::vector<float>& x)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot open %s\n", path.c_str()); return; }
    const uint32_t n = (uint32_t) x.size();
    const uint32_t rate = (uint32_t) kFs;
    const uint16_t bits = 16, ch = 1;
    const uint32_t byteRate = rate * ch * bits / 8;
    const uint16_t blockAlign = ch * bits / 8;
    const uint32_t dataBytes = n * blockAlign;
    auto u32 = [&](uint32_t v){ std::fwrite(&v,4,1,f); };
    auto u16 = [&](uint16_t v){ std::fwrite(&v,2,1,f); };
    std::fwrite("RIFF",1,4,f); u32(36+dataBytes); std::fwrite("WAVE",1,4,f);
    std::fwrite("fmt ",1,4,f); u32(16); u16(1); u16(ch); u32(rate);
    u32(byteRate); u16(blockAlign); u16(bits);
    std::fwrite("data",1,4,f); u32(dataBytes);
    for (float s : x)
    {
        if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t) std::lround(s * 32767.0f);
        std::fwrite(&v,2,1,f);
    }
    std::fclose(f);
    std::printf("wrote %s (%.2f s, peak check done)\n", path.c_str(), n / kFs);
}

// Renders `seconds` of audio, calling `tweak(blockIndex, params)` before each
// block so a scenario can press/release buttons over time.
template <class Tweak>
static std::vector<float> render(SirenEngine::Params base, float seconds, Tweak tweak)
{
    SirenEngine eng;
    eng.prepare(kFs, kBlock);
    const int nblocks = (int) (seconds * kFs / kBlock);
    std::vector<float> out;
    out.reserve(nblocks * kBlock);
    float bufL[kBlock], bufR[kBlock];
    float* bufs[2] = { bufL, bufR };
    for (int b = 0; b < nblocks; ++b)
    {
        SirenEngine::Params p = base;
        tweak(b, p);
        eng.setParameters(p);
        for (int i = 0; i < kBlock; ++i) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
        eng.process(bufs, 2, kBlock);
        for (int i = 0; i < kBlock; ++i) out.push_back(bufL[i]);
    }
    return out;
}

static int blk(float s) { return (int)(s * kFs / kBlock); }

int main()
{
    const std::string dir = "/tmp/siren-proto/";
    std::system("mkdir -p /tmp/siren-proto");

    // ---- A. Aliasing: a sustained HIGH note (fastest osc ladder, TONE held).
    //        Compare OS 1 / 2 / 4. Listen for the gritty "digital" buzz vs a
    //        smoother tone in the highs.
    for (int os : {1, 2, 4})
    {
        SirenEngine::Params p;
        p.power = true; p.toneBtn = true; p.speed = 3;
        p.tone = 2;            // 100k = fastest = most aliasing
        p.volume_dB = -6.0f;
        p.oversample = os;
        auto x = render(p, 2.0f, [](int,SirenEngine::Params&){});
        writeWav(dir + "alias_highnote_os" + std::to_string(os) + ".wav", x);
    }

    // ---- B. Dying tail: SPEED OFF, hold TONE 0.6 s, release, let C5 drain.
    //        Reference render to study the death timbre in the circuit model.
    {
        SirenEngine::Params p;
        p.power = true; p.speed = 3; p.tone = 1;
        p.discharge_s = 3.0f;
        p.oversample = 2;
        auto x = render(p, 4.5f, [](int b, SirenEngine::Params& pp){
            pp.toneBtn = (b < blk(0.6f));   // press, then release
        });
        writeWav(dir + "dying_tail.wav", x);
    }

    // ---- C. Classic vs Physical death A/B for listening + FFT (warmth check).
    for (int model : {0, 1})
    {
        SirenEngine::Params p;
        p.power = true; p.speed = 3; p.tone = 1;
        p.discharge_s = 3.0f; p.oversample = 4; p.model = model;
        auto x = render(p, 4.5f, [](int b, SirenEngine::Params& pp){
            pp.toneBtn = (b < blk(0.6f));
        });
        writeWav(dir + (model ? "death_physical.wav" : "death_classic.wav"), x);
    }

    // ---- D. Classic vs Physical sustained HIGH note (de-digital A/B).
    for (int model : {0, 1})
    {
        SirenEngine::Params p;
        p.power = true; p.toneBtn = true; p.speed = 3; p.tone = 2;
        p.oversample = 4; p.model = model;
        auto x = render(p, 2.0f, [](int,SirenEngine::Params&){});
        writeWav(dir + (model ? "highnote_physical.wav" : "highnote_classic.wav"), x);
    }

    std::printf("done. WAVs in %s\n", dir.c_str());
    return 0;
}
