#include "BlockOps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace s5::data
{
static bool firstFit (const Block& b, int bank, int len, int ignoreTone, int& start)
{
    bool used[kSegments] = {};
    for (int i = 0; i < kNumTones; ++i)
    {
        const Tone& t = b.tones[i];
        if (i == ignoreTone || ! t.used() || t.subTone || t.waveBank != bank) continue;
        for (int s = t.waveStart; s < t.waveStart + t.waveLen && s < kSegments; ++s) used[s] = true;
    }
    for (int s = 0; s + len <= kSegments; ++s)
    {
        bool ok = true;
        for (int k = 0; k < len && ok; ++k) ok = ! used[s + k];
        if (ok) { start = s; return true; }
    }
    return false;
}

int freeSegmentsOf (const Block& b, int bank) { return b.freeSegments (bank); }
double secondsFree (const Block& b, int bank) { return b.freeSegments (bank) * 0.4; }

std::string toneInfo (const Tone& t)
{
    if (! t.used()) return "";
    char buf[24];
    std::snprintf (buf, sizeof buf, "%c-%.1f", t.waveBank ? 'B' : 'A', t.waveLen * (t.khz15 ? 0.8 : 0.4));
    return buf;
}

static void copyWave (const Block& src, const Tone& st, Block& dst, int bank, int start)
{
    const size_t n = (size_t) st.waveLen * kSegWords;
    std::copy (src.wave[st.waveBank].begin() + (long) st.waveStart * kSegWords,
               src.wave[st.waveBank].begin() + (long) st.waveStart * kSegWords + (long) n,
               dst.wave[bank].begin() + (long) start * kSegWords);
}

std::string loadTone (const Block& src, int st, Block& dst, int dt, int bank)
{
    if (st < 0 || st >= kNumTones || dt < 0 || dt >= kNumTones) return "Cannot Execute";
    const Tone s = src.tones[st];
    if (! s.used()) return "Cannot Execute";
    Tone d = s;
    if (! s.subTone)
    {
        int start = 0;
        if (! firstFit (dst, bank, s.waveLen, dt, start)) return "Cannot Execute";
        // a Sub Tone that borrowed the old wave data becomes an unused Sub Tone
        if (dst.tones[dt].used() && ! dst.tones[dt].subTone)
            for (int i = 0; i < kNumTones; ++i)
                if (i != dt && dst.tones[i].subTone && dst.tones[i].sourceTone == dt) { dst.tones[i] = Tone(); }
        copyWave (src, s, dst, bank, start);
        d.waveBank = (uint8_t) bank; d.waveStart = (uint8_t) start;
    }
    dst.tones[dt] = d;
    return "";
}

std::string storeSample (Block& b, int tone, int bank, const std::vector<int16_t>& smp, bool khz15, int origKey, const std::string& name)
{
    if (tone < 0 || tone >= kNumTones || smp.empty()) return "Cannot Execute";
    const int len = (int) ((smp.size() + kSegWords - 1) / kSegWords);
    int start = 0;
    if (len > kSegments || ! firstFit (b, bank, len, tone, start)) return "Memory full";
    if (b.tones[tone].used() && ! b.tones[tone].subTone)
        for (int i = 0; i < kNumTones; ++i)
            if (i != tone && b.tones[i].subTone && b.tones[i].sourceTone == tone) b.tones[i] = Tone();
    static const uint8_t tmpl[102] = { 0,0,88,1,0,0,0,0,0,0,0,241,42,0,0,0,0,127,0,2,0,0,2,1,1,1,2,127,67,127,127,0,65,0,127,0,127,0,127,0,127,0,127,0,0,127,0,10,1,0,0,0,0,0,0,0,0,0,0,4,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,107,20,1,2,1,2,127,123,72,20,0,28,0,127,0,127,0,127,0,127,0,127,1,0,0 };
    Tone t;
    std::memcpy (t.rest, tmpl, sizeof t.rest);
    t.name = (name + "        ").substr (0, 8);
    t.khz15 = khz15; t.origKey = (uint8_t) std::clamp (origKey, 11, 120);
    t.waveBank = (uint8_t) bank; t.waveStart = (uint8_t) start; t.waveLen = (uint8_t) len;
    t.startPoint = 0; t.endPoint = (uint32_t) (smp.size() - 1); t.loopPoint = 0; t.loopMode = 2;
    auto& w = b.wave[bank];
    if (w.size() < (size_t) kWaveWords) w.resize (kWaveWords, 0);
    const size_t o = (size_t) start * kSegWords;
    for (size_t i = 0; i < (size_t) len * kSegWords; ++i) w[o + i] = i < smp.size() ? smp[i] : 0;
    b.tones[tone] = t;
    return "";
}

std::vector<int16_t> filterWave (const std::vector<int16_t>& in, double fs, const FilterPrm& p)
{
    // 2 pole resonant filter (RBJ biquad); resonance 0-127 raises Q from 0.7 to about 9
    const double pi = 3.14159265358979323846;
    const double f0 = std::min (p.freqKHz * 1000.0, fs * 0.45);
    const double q = 0.7071 + p.resonance / 127.0 * 8.3;
    const double w0 = 2 * pi * f0 / fs, cw = std::cos (w0), alpha = std::sin (w0) / (2 * q);
    double b0, b1, b2;
    if (p.hpf) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    else       { b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; }
    const double a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    const double g = p.level / 127.0;
    std::vector<int16_t> out (in.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double x = in[i] * g;
        const double y = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        out[i] = (int16_t) std::clamp ((int) std::lround (y), -2048, 2047);
    }
    return out;
}

std::vector<int16_t> dcCut (const std::vector<int16_t>& in)
{
    if (in.empty()) return in;
    double sum = 0; for (auto v : in) sum += v;
    const int mean = (int) std::lround (sum / (double) in.size());
    std::vector<int16_t> out (in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = (int16_t) std::clamp ((int) in[i] - mean, -2048, 2047);
    return out;
}

std::string storeProcessed (const Block& src, int st, Block& dst, int dt, int bank, bool dc, const FilterPrm& p)
{
    if (st < 0 || st >= kNumTones || dt < 0 || dt >= kNumTones) return "Cannot Execute";
    const Tone s = src.tones[st];
    if (! s.used() || s.subTone) return "Cannot Execute";                         // a Sub Tone cannot be used
    const size_t n = (size_t) s.waveLen * kSegWords, o = (size_t) s.waveStart * kSegWords;
    if (o + n > src.wave[s.waveBank & 1].size()) return "Cannot Execute";
    std::vector<int16_t> in (src.wave[s.waveBank & 1].begin() + (long) o, src.wave[s.waveBank & 1].begin() + (long) (o + n));
    const auto out = dc ? dcCut (in) : filterWave (in, s.khz15 ? 15000.0 : 30000.0, p);
    int start = 0;
    if (! firstFit (dst, bank, s.waveLen, dt, start)) return "Memory full";
    Tone d = s; d.waveBank = (uint8_t) bank; d.waveStart = (uint8_t) start;
    if (dst.tones[dt].used() && ! dst.tones[dt].subTone)
        for (int i = 0; i < kNumTones; ++i)
            if (i != dt && dst.tones[i].subTone && dst.tones[i].sourceTone == dt) dst.tones[i] = Tone();
    auto& w = dst.wave[bank];
    if (w.size() < (size_t) kWaveWords) w.resize ((size_t) kWaveWords, 0);
    std::copy (out.begin(), out.end(), w.begin() + (long) ((size_t) start * kSegWords));
    dst.tones[dt] = d;
    return "";
}

std::string storeLooped (const Block& src, int st, Block& dst, int dt, int bank, const std::vector<int16_t>& wave, uint32_t startPt, uint32_t loopPt, uint32_t endPt)
{
    if (st < 0 || st >= kNumTones || dt < 0 || dt >= kNumTones) return "Cannot Execute";
    const Tone s = src.tones[st];
    if (! s.used() || s.subTone) return "Cannot Execute";
    if (wave.size() != (size_t) s.waveLen * kSegWords) return "Cannot Execute";
    const Tone& old = dst.tones[dt];
    if (old.used() && ! old.subTone && old.waveLen < s.waveLen) return "Not Execute";      // the space of the destination is shorter (manual p.57)
    int start = 0;
    if (! firstFit (dst, bank, s.waveLen, dt, start)) return "Memory full";
    Tone d = s; d.waveBank = (uint8_t) bank; d.waveStart = (uint8_t) start;
    d.startPoint = startPt; d.loopPoint = loopPt; d.endPoint = endPt; d.loopMode = 0;
    if (old.used() && ! old.subTone)
        for (int i = 0; i < kNumTones; ++i)
            if (i != dt && dst.tones[i].subTone && dst.tones[i].sourceTone == dt) dst.tones[i] = Tone();
    auto& w = dst.wave[bank];
    if (w.size() < (size_t) kWaveWords) w.resize ((size_t) kWaveWords, 0);
    std::copy (wave.begin(), wave.end(), w.begin() + (long) ((size_t) start * kSegWords));
    dst.tones[dt] = d;
    return "";
}

// Smoothing: the end of the loop is cross-faded into the wave just before the loop point, so that End -> Loop joins without a step
bool smoothLoop (std::vector<int16_t>& w, int startPt, int loopPt, int endPt)
{
    const int n = (int) w.size();
    if (loopPt - startPt < 228 || endPt <= loopPt || endPt >= n) return false;
    const int len = std::min ({ loopPt - startPt, endPt - loopPt, 2048 });
    for (int i = 0; i < len; ++i)
    {
        const double t = (i + 1.0) / len;
        const int e = endPt - len + 1 + i, l = loopPt - len + i;                // sample e is followed by sample loopPt
        w[(size_t) e] = (int16_t) std::lround ((1.0 - t) * w[(size_t) e] + t * w[(size_t) l]);
    }
    return true;
}

// Auto Loop: rising zero crossings; the neighbourhoods before the two points must look alike
bool autoLoop (const std::vector<int16_t>& w, int loopPt, int endPt, bool fromLoop, int& newLoop, int& newEnd)
{
    const int n = (int) w.size();
    if (endPt >= n || endPt - loopPt < 64) return false;
    std::vector<int> zc;
    for (int i = std::max (loopPt, 1); i <= endPt; ++i) if (w[(size_t) i - 1] < 0 && w[(size_t) i] >= 0) zc.push_back (i);
    if (zc.size() < 2) return false;
    const int W = 64, minLen = std::max (W, (endPt - loopPt) / 4);
    auto err = [&] (int a, int b)
    {
        double e = 0;
        for (int k = 1; k <= W; ++k)
        {
            const int ia = a - k, ib = b - k;
            const double da = ia >= 0 ? w[(size_t) ia] : 0.0, db = ib >= 0 ? w[(size_t) ib] : 0.0;
            e += (da - db) * (da - db);
        }
        return e;
    };
    double best = 1e300; int bl = -1, be = -1;
    if (fromLoop)
    {
        const int l = zc.front();
        for (int e : zc) if (e - l >= minLen) { const double x = err (l, e); if (x < best) { best = x; bl = l; be = e; } }
    }
    else
    {
        const int e = zc.back();
        for (int l : zc) if (e - l >= minLen) { const double x = err (l, e); if (x < best) { best = x; bl = l; be = e; } }
    }
    if (bl < 0) return false;
    newLoop = bl; newEnd = be;
    return true;
}

std::string loadPatch (const Block& src, int sp, Block& dst, int dp, std::vector<int>* notLoaded)
{
    if (sp < 0 || sp >= kNumPatches || dp < 0 || dp >= kNumPatches) return "Cannot Execute";
    const Patch& p = src.patches[sp];
    std::set<int> used;
    for (int k = 0; k < 109; ++k) { used.insert (p.raw[17 + k] & 31); used.insert (p.raw[126 + k] & 31); }
    std::set<int> need;                                            // used tones plus the originals of used sub tones
    for (int t : used)
        if (src.tones[t].used()) { need.insert (t); if (src.tones[t].subTone) need.insert (src.tones[t].sourceTone & 31); }
    int freeNums = 0;
    for (int i = 0; i < kNumTones; ++i) if (! dst.tones[i].used()) ++freeNums;
    if ((int) need.size() > freeNums) return "Memory full";

    std::map<int, int> to;                                         // source tone number -> destination number
    auto pick = [&] (int want)
    {
        if (! dst.tones[want].used()) { dst.tones[want].waveLen = 1; return want; }    // reserve
        for (int i = 0; i < kNumTones; ++i) if (! dst.tones[i].used()) { dst.tones[i].waveLen = 1; return i; }
        return -1;
    };
    std::vector<int> reserved;
    for (int t : need) { const int n = pick (t); to[t] = n; reserved.push_back (n); }
    for (int n : reserved) dst.tones[n].waveLen = 0;               // release the reservation again
    std::string err;
    std::vector<int> failed;
    for (int pass = 0; pass < 2; ++pass)                           // originals first, then the sub tones
        for (int t : need)
        {
            const Tone& s = src.tones[t];
            if ((pass == 0) == s.subTone) continue;
            Tone d = s;
            if (! s.subTone)
            {
                int start = 0, bank = s.waveBank;
                if (! firstFit (dst, bank, s.waveLen, to[t], start)) { failed.push_back (t); continue; }
                copyWave (src, s, dst, bank, start);
                d.waveBank = (uint8_t) bank; d.waveStart = (uint8_t) start;
            }
            else
            {
                if (std::find (failed.begin(), failed.end(), (int) (s.sourceTone & 31)) != failed.end()) { failed.push_back (t); continue; }
                d.sourceTone = (uint8_t) to[s.sourceTone & 31];
            }
            dst.tones[to[t]] = d;
        }
    Patch np = p;
    for (int k = 0; k < 109; ++k)
    {
        const int a = np.raw[17 + k] & 31, b = np.raw[126 + k] & 31;
        if (to.count (a)) np.raw[17 + k] = (uint8_t) to[a];
        if (to.count (b)) np.raw[126 + k] = (uint8_t) to[b];
    }
    dst.patches[dp] = np;
    if (! failed.empty()) { if (notLoaded) *notLoaded = failed; return "Memory full"; }
    return "";
}

void deletePatch (Block& b, int p)
{
    Patch e;
    e.setName ("");
    b.patches[p] = e;
}
}
