#include "Source/Data/WaveImport.h"
#include <cmath>
#include <cstdio>
using namespace s5::data;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main()
{
    std::vector<float> sine (44100);                       // 1 s, 441 Hz at 44.1 kHz, amplitude 0.25
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.25f * (float) std::sin (2 * 3.14159265358979 * 441.0 * (double) i / 44100.0);
    Block b; WaveImportResult r; std::string err;
    WaveImportOptions o; o.tone = 3; o.bank = 0; o.name = "Sine441Hz-long";
    CHECK (importWave (b, o, sine, 44100, r, err));
    CHECK (r.samples == 30000 && r.segLen == 3 && r.segStart == 0);
    const Tone& t = b.tones[3];
    CHECK (t.used() && t.waveLen == 3 && t.endPoint == 29999 && t.loopMode == 2 && t.name == "Sine441H");
    int16_t pk = 0; for (int i = 100; i < 29000; ++i) pk = std::max<int16_t> (pk, b.wave[0][(size_t) i]);
    CHECK (pk > 2000 && pk <= 2047);                       // normalized to about 0.98
    int zc = 0; for (int i = 1000; i < 29000; ++i) if ((b.wave[0][(size_t) i - 1] < 0) != (b.wave[0][(size_t) i] < 0)) ++zc;
    CHECK (std::fabs (zc / 2.0 / (28000.0 / 30000.0) - 441.0) < 3.0);      // pitch kept after 44.1 -> 30 kHz
    // a second file lands behind the first; replacing a tone reuses its own place
    o.tone = 4; CHECK (importWave (b, o, sine, 44100, r, err) && r.segStart == 3);
    o.tone = 3; o.khz15 = true; CHECK (importWave (b, o, sine, 44100, r, err) && r.segStart == 0 && r.segLen == 2 && b.tones[3].khz15);
    // too long / memory full
    std::vector<float> longv (44100 * 9, 0.1f); o.tone = 5; o.khz15 = false;
    CHECK (! importWave (b, o, longv, 44100, r, err) && err.compare (0, 8, "Too long") == 0);
    for (int i = 0; i < 6; ++i) { o.tone = 6 + i; std::vector<float> v (44100 * 2, 0.3f); (void) importWave (b, o, v, 44100, r, err); }
    o.tone = 20; std::vector<float> v2 (44100 * 2, 0.3f); CHECK (! importWave (b, o, v2, 44100, r, err) && err.compare (0, 11, "Memory full") == 0);
    std::printf (fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
}
