#include "WaveImport.h"
#include <algorithm>
#include <cmath>

namespace s5::data
{
std::vector<float> resampleMono (const std::vector<float>& in, double rin, double rout)
{
    if (in.empty() || rin <= 0 || rout <= 0) return {};
    if (std::fabs (rin - rout) < 0.5) return in;
    const double ratio = rout / rin;
    const size_t n = (size_t) std::floor ((double) in.size() * ratio);
    std::vector<float> out (n);
    const double fc = std::min (1.0, ratio) * 0.94;            // cut-off relative to the input Nyquist (down sampling: low pass first)
    const int half = 24;                                       // taps each side (in input samples, scaled when down sampling)
    const double scale = std::min (1.0, ratio);
    const int w = (int) std::ceil (half / scale);
    const double PI = 3.14159265358979323846;
    for (size_t i = 0; i < n; ++i)
    {
        const double pos = (double) i / ratio;
        const long c = (long) std::floor (pos);
        double acc = 0, wsum = 0;
        for (long k = c - w + 1; k <= c + w; ++k)
        {
            if (k < 0 || k >= (long) in.size()) continue;
            const double x = pos - (double) k;
            const double t = x * fc;
            const double sinc = std::fabs (t) < 1e-9 ? 1.0 : std::sin (PI * t) / (PI * t);
            const double win = 0.5 + 0.5 * std::cos (PI * x / (double) w);          // Hann window
            const double h = sinc * win * fc;
            acc += h * in[(size_t) k]; wsum += h;
        }
        out[i] = (float) (wsum > 0.2 ? acc / wsum : 0.0);
    }
    return out;
}

void setPlayableDefaults (Tone& o)
{
    o.at (38) = 127; o.at (51) = 1; o.at (52) = 2; o.at (107) = 1; o.at (108) = 2;
    for (int e = 0; e < 8; ++e) { o.at (53 + 2 * e) = e == 2 ? 0 : 127; o.at (54 + 2 * e) = e == 2 ? 70 : 127;
                                  o.at (109 + 2 * e) = 127; o.at (110 + 2 * e) = 127; }
}

bool importWave (Block& b, const WaveImportOptions& o, const std::vector<float>& mono, double rate, WaveImportResult& res, std::string& err)
{
    if (mono.empty() || rate <= 0) { err = "Empty file"; return false; }
    if (o.tone < 0 || o.tone >= kNumTones || o.bank < 0 || o.bank > 1) { err = "Bad target"; return false; }
    std::vector<float> s = resampleMono (mono, rate, o.khz15 ? 15000.0 : 30000.0);
    if (s.size() < 8) { err = "File too short"; return false; }
    const int maxWords = kSegments * kSegWords;
    if ((int) s.size() > maxWords) { s.resize ((size_t) maxWords); res.truncated = true; }          // too long: cut to the bank size
    if (o.normalize)
    {
        float pk = 0; for (float v : s) pk = std::max (pk, std::fabs (v));
        if (pk > 1e-6f) { const float g = 0.98f / pk; for (float& v : s) v *= g; }
    }
    const int need = ((int) s.size() + kSegWords - 1) / kSegWords;
    // free segments of the bank: the old wave of the target tone counts as free
    bool used[kSegments] = {};
    for (int t = 0; t < kNumTones; ++t)
    {
        const Tone& x = b.tones[t];
        if (t == o.tone || ! x.used() || x.waveBank != o.bank || x.subTone) continue;
        for (int k = x.waveStart; k < x.waveStart + x.waveLen && k < kSegments; ++k) used[k] = true;
    }
    int start = -1;
    for (int i = 0; i + need <= kSegments && start < 0; ++i)
    {
        bool ok = true;
        for (int k = i; k < i + need; ++k) if (used[k]) { ok = false; break; }
        if (ok) start = i;
    }
    if (start < 0) { err = "Memory full (" + std::to_string (need) + " segments needed)"; return false; }
    auto& w = b.wave[o.bank];
    std::fill (w.begin() + (long) start * kSegWords, w.begin() + (long) (start + need) * kSegWords, (int16_t) 0);
    for (size_t i = 0; i < s.size(); ++i)
        w[(size_t) start * kSegWords + i] = (int16_t) std::max (-2048L, std::min (2047L, std::lround (s[i] * 2047.0f)));
    Tone& t = b.tones[o.tone];
    const bool fresh = ! t.used() || t.at (38) == 0;
    std::string nm = o.name; for (auto& c : nm) if (c < 32 || c > 126) c = ' ';
    nm.resize (8, ' ');
    t.name = nm;
    t.sourceTone = (uint8_t) o.tone; t.subTone = false; t.khz15 = o.khz15; t.origKey = (uint8_t) o.origKey;
    t.waveBank = (uint8_t) o.bank; t.waveStart = (uint8_t) start; t.waveLen = (uint8_t) need;
    t.startPoint = 0; t.endPoint = (uint32_t) s.size() - 1; t.loopPoint = 0; t.loopMode = 2;          // 1 Shot
    if (fresh) setPlayableDefaults (t);
    res.segStart = start; res.segLen = need; res.samples = (int) s.size();
    return true;
}
}
