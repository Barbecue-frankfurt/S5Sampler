#include "../Source/Engine/Engine.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace s5;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static double zeroCrossFreq (const std::vector<float>& x, double sr, size_t a, size_t b)
{
    int n = 0; for (size_t i = a + 1; i < b; ++i) if (x[i-1] < 0 && x[i] >= 0) ++n;
    return n / ((b - a) / sr);
}

int main (int argc, char** argv)
{
    data::Block blk;
    // tone 0: sine, 100 samples per period at 30 kHz -> 300 Hz at origKey 60, looped
    auto& t = blk.tones[0];
    t.origKey = 60; t.waveBank = 0; t.waveStart = 0; t.waveLen = 1; t.khz15 = false;
    t.startPoint = 0; t.endPoint = 12000; t.loopPoint = 0; t.loopMode = 0;
    for (int i = 0; i < 12288; ++i) blk.wave[0][i] = (int16_t) std::lround (1500 * std::sin (2 * 3.14159265 * i / 100.0));
    t.at (38) = 127; t.at (39) = 0; t.at (40) = 0; t.at (43) = 0;
    t.at (51) = 1; t.at (52) = 2;                       // TVA: sustain at stage 1 (0-based), end stage 2
    t.at (53) = 127; t.at (54) = 127;                   // level/rate stage 1
    t.at (55) = 127; t.at (56) = 127;                   // stage 2 (sustain)
    t.at (57) = 0;   t.at (58) = 80;                    // stage 3: release to 0
    t.at (107) = 1; t.at (108) = 2;
    for (int k = 0; k < 8; ++k) { t.at (109 + 2*k) = 127; t.at (110 + 2*k) = 127; }
    for (int k = 0; k < 16; ++k) { blk.patches[k].raw[15] = 0; blk.patches[k].raw[237] = 127; for (int j = 0; j < 109; ++j) { blk.patches[k].raw[17+j] = 0; blk.patches[k].raw[126+j] = 0; } }

    engine::Engine e; e.prepare (44100); e.setBlock (blk); e.setPart (0, 0, 0);
    std::vector<float> L (44100), R (44100);
    uint8_t on[3] = { 0x90, 60, 100 };
    e.midi (on, 3);
    CHECK (e.activeVoices() == 1);
    e.render (L.data(), R.data(), 22050);
    double f = zeroCrossFreq (L, 44100, 4000, 22050);
    std::printf ("freq %.2f Hz (expect ~300)\n", f);
    CHECK (std::fabs (f - 300) < 3);
    float pk = 0; for (int i = 4000; i < 22050; ++i) pk = std::max (pk, std::fabs (L[i]));
    std::printf ("peak %.3f\n", pk);
    CHECK (pk > 0.05f);
    // octave up
    uint8_t on2[3] = { 0x90, 72, 100 }; e.midi (on2, 3);
    // release both
    uint8_t off[3] = { 0x80, 60, 0 }, off2[3] = { 0x80, 72, 0 }; e.midi (off, 3); e.midi (off2, 3);
    for (int b = 0; b < 40 && e.activeVoices(); ++b) e.render (L.data(), R.data(), 4410);
    CHECK (e.activeVoices() == 0);
    // key 72 pitch
    e.midi (on2, 3); e.render (L.data(), R.data(), 22050);
    f = zeroCrossFreq (L, 44100, 4000, 22050); std::printf ("freq %.2f (expect 600)\n", f); CHECK (std::fabs (f - 600) < 6);
    e.allNotesOff();
    // 1 shot ends by itself
    blk.tones[0].loopMode = 2; blk.tones[0].endPoint = 3000; e.setBlock (blk);
    e.midi (on, 3);
    for (int b = 0; b < 10; ++b) e.render (L.data(), R.data(), 4410);
    CHECK (e.activeVoices() == 0);
    {   // individual outputs: patch Output Assign (byte 243: 0-7 = Out 1-8, 8 = Tone) and tone Output Assign (byte 8)
        blk.tones[0].loopMode = 0; blk.tones[0].endPoint = 12000;
        blk.patches[0].raw[243] = 2;                                   // Out 3
        e.setBlock (blk); e.allNotesOff();
        std::vector<float> oL[8], oR[8]; float* pl[8]; float* pr[8];
        for (int o = 0; o < 8; ++o) { oL[o].assign (4410, 0.f); oR[o].assign (4410, 0.f); pl[o] = oL[o].data(); pr[o] = oR[o].data(); }
        auto peak = [] (const std::vector<float>& v) { float m = 0; for (float x : v) m = std::max (m, std::fabs (x)); return m; };
        e.midi (on, 3); e.render (L.data(), R.data(), 4410, pl, pr);
        CHECK (peak (oL[2]) > 0.05f && peak (oR[2]) > 0.05f);          // sounds on Out 3
        float others = 0; for (int o = 0; o < 8; ++o) if (o != 2) others = std::max (others, peak (oL[o]));
        float mix = 0; for (int i = 0; i < 4410; ++i) mix = std::max (mix, std::fabs (L[i]));
        CHECK (others == 0.f && mix == 0.f);                           // nothing elsewhere, the mix stays silent
        pl[2] = nullptr; pr[2] = nullptr;                              // Out 3 not switched on in the host: it plays in the mix
        e.render (L.data(), R.data(), 4410, pl, pr);
        mix = 0; for (int i = 0; i < 4410; ++i) mix = std::max (mix, std::fabs (L[i]));
        CHECK (mix > 0.05f);
        e.allNotesOff();
        blk.patches[0].raw[243] = 8; blk.tones[0].f1 = 5;              // Tone: the tone's own output (Out 6)
        e.setBlock (blk);
        for (int o = 0; o < 8; ++o) { pl[o] = oL[o].data(); pr[o] = oR[o].data(); }
        e.midi (on, 3); e.render (L.data(), R.data(), 4410, pl, pr);
        CHECK (peak (oL[5]) > 0.05f && peak (oL[2]) == 0.f);
        e.allNotesOff(); blk.patches[0].raw[243] = 0; blk.tones[0].f1 = 0; e.setBlock (blk);
    }
    // real disk
    if (argc > 1)
    {
        std::ifstream f2 (argv[1], std::ios::binary); std::vector<uint8_t> img ((std::istreambuf_iterator<char> (f2)), {});
        data::Block d; auto r = d.parse (img.data(), img.size());
        std::printf ("disk parse %d\n", (int) r);
        e.setBlock (d); e.allNotesOff();
        for (int p = 0; p < 8; ++p) e.setPart (p, p == 0 ? 0 : -1, p);
        for (int k : { 48, 60, 72 }) { uint8_t m[3] = { 0x90, (uint8_t) k, 100 }; e.midi (m, 3); }
        std::vector<float> big (88200), bigR (88200);
        e.render (big.data(), bigR.data(), 88200);
        float p2 = 0; for (float v : big) p2 = std::max (p2, std::fabs (v));
        std::printf ("real disk peak %.3f voices %d\n", p2, e.activeVoices());
        std::ofstream w ("/tmp/w/engine_out.raw", std::ios::binary); w.write ((char*) big.data(), big.size() * 4);
    }
    std::printf (fails ? "FAILED\n" : "engine: all checks passed\n");
    return fails;
}
