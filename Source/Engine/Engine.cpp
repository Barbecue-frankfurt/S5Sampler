#include "Engine.h"
#include "EnvModel.h"
#include <algorithm>
#include <cmath>

namespace s5::engine
{
namespace
{
const int kVoiceSplit[22][8] = {
    { 16, 0, 0, 0, 0, 0, 0, 0 }, { 14, 2, 0, 0, 0, 0, 0, 0 }, { 12, 4, 0, 0, 0, 0, 0, 0 }, { 12, 2, 2, 0, 0, 0, 0, 0 },
    { 10, 6, 0, 0, 0, 0, 0, 0 }, { 10, 4, 2, 0, 0, 0, 0, 0 }, { 10, 2, 2, 2, 0, 0, 0, 0 }, { 8, 8, 0, 0, 0, 0, 0, 0 },
    { 8, 6, 2, 0, 0, 0, 0, 0 },  { 8, 4, 4, 0, 0, 0, 0, 0 },  { 8, 4, 2, 2, 0, 0, 0, 0 },  { 8, 2, 2, 2, 2, 0, 0, 0 },
    { 6, 6, 4, 0, 0, 0, 0, 0 },  { 6, 6, 2, 2, 0, 0, 0, 0 },  { 6, 4, 4, 2, 0, 0, 0, 0 },  { 6, 4, 2, 2, 2, 0, 0, 0 },
    { 6, 2, 2, 2, 2, 2, 0, 0 },  { 4, 4, 4, 4, 0, 0, 0, 0 },  { 4, 4, 4, 2, 2, 0, 0, 0 },  { 4, 4, 2, 2, 2, 2, 0, 0 },
    { 4, 2, 2, 2, 2, 2, 2, 0 },  { 2, 2, 2, 2, 2, 2, 2, 2 } };

// tone record offsets (see LivePages.cpp): TVA envelope sus 51 end 52 levels 53+2k rates 54+2k, vel-rate 72;
// TVF cut-off 38, resonance 39, key follow 40 (signed), EG depth 43, key-rate 46, vel-rate 47, envelope sus 107 end 108 levels 109+2k rates 110+2k
inline uint8_t at (const data::Tone& t, int off) { return t.at (off); }
}

// ---------------------------------------------------------------- envelope
void Engine::Eg::start (const uint8_t* rec, int susByte, int levelBase, int rateBase, int velRate, int velocity, int keyRate, int key)
{
    const int base = 26;                                    // rec = Tone::rest
    end = std::max (2, std::min (8, rec[susByte + 1 - base] + 1));
    sus = std::max (1, std::min (7, rec[susByte - base] + 1));
    for (int k = 0; k < 8; ++k)
    {
        rate[k] = std::min (127, (int) rec[rateBase + 2 * k - base]);
        level[k] = std::min (127, (int) rec[levelBase + 2 * k - base]);
        // Vel-Rate / Key-Rate: faster envelopes for hard / high notes (a few Rate steps)
        const int shift = (velRate * (velocity - 64)) / 64 / 8 + (keyRate * (key - 60)) / 60 / 8;
        if (rate[k] < 127) rate[k] = std::max (0, std::min (127, rate[k] + shift));
    }
    stage = 0; hold = false; cur = 0.0f;
}

void Engine::Eg::release()
{
    if (hold || stage < sus) { hold = false; stage = std::max (stage, sus); }
}

void Engine::Eg::run (int samples, double sr)
{
    while (samples > 0 && stage < end)
    {
        if (hold) return;
        const float target = (float) level[stage];
        const double spl = env::secondsPerLevel (rate[stage]);           // seconds for one level step
        if (spl <= 0.0 || cur == target) { cur = target; }
        else
        {
            const float perSample = (float) (1.0 / (spl * sr));
            const float d = target - cur;
            const float need = std::fabs (d) / perSample;                 // samples until the target
            if (need > (float) samples) { cur += (d > 0 ? perSample : -perSample) * (float) samples; return; }
            cur = target;
            samples -= (int) std::ceil (need);
        }
        // target reached
        if (stage + 1 == sus) { hold = true; ++stage; return; }          // hold at the sustain point until key off
        ++stage;
    }
}

// ---------------------------------------------------------------- engine
Engine::Engine()
{
    snap = std::make_shared<Snapshot>();
    for (int i = 0; i < kParts; ++i) { parts[i].channel = i; parts[i].patch = i; }
    applyGroups();
}

void Engine::prepare (double rate)
{
    sr = rate > 0 ? rate : 44100.0;
    allNotesOff();
}

static void fillBlock (Snapshot& s, int blk, const data::Block& b, bool waves)
{
    for (int i = 0; i < data::kNumTones; ++i) s.tones[blk * data::kNumTones + i] = b.tones[i];
    for (int i = 0; i < data::kNumPatches; ++i) s.patches[blk * data::kNumPatches + i] = b.patches[i];
    if (waves)
        for (int k = 0; k < 2; ++k) s.wave[blk * 2 + k] = std::make_shared<std::vector<int16_t>> (b.wave[k]);
}

static void copyBlock (Snapshot& s, const Snapshot& old, int blk, bool waves)
{
    for (int i = 0; i < data::kNumTones; ++i) s.tones[blk * data::kNumTones + i] = old.tones[blk * data::kNumTones + i];
    for (int i = 0; i < data::kNumPatches; ++i) s.patches[blk * data::kNumPatches + i] = old.patches[blk * data::kNumPatches + i];
    if (waves) for (int k = 0; k < 2; ++k) s.wave[blk * 2 + k] = old.wave[blk * 2 + k];
}

// blk: block to replace; waves: replace the wave data as well (otherwise the old waves stay)
void Engine::setBlockN (int blk, const data::Block& b, bool waves)
{
    auto old = std::atomic_load (&snap);
    auto s = std::make_shared<Snapshot>();
    copyBlock (*s, *old, 1 - blk, true);
    copyBlock (*s, *old, blk, ! waves);
    fillBlock (*s, blk, b, waves);
    retired[retIdx++ & 3] = old;                    // the last reference to an old snapshot is dropped here (message thread), never on the audio thread
    std::atomic_store (&snap, std::shared_ptr<const Snapshot> (std::move (s)));
}

void Engine::setBlock (const data::Block& b)   { setBlockN (0, b, true); }
void Engine::setParams (const data::Block& b)  { setBlockN (0, b, false); }
void Engine::setBlock2 (const data::Block& b)  { setBlockN (1, b, true); }
void Engine::setParams2 (const data::Block& b) { setBlockN (1, b, false); }

void Engine::setAudition (int tone, int patch) { audTone = tone; audPatch = patch; }
void Engine::setVoiceMode (int mode)
{
    mode = std::max (0, std::min (23, mode));
    if (mode == voiceMode) return;                  // the screen calls this every 25 ms: the audio thread must never see the groups half rebuilt
    voiceMode = mode; applyGroups();
}
void Engine::setPart (int part, int channel, int patch)
{
    if (part < 0 || part >= kParts) return;
    parts[part].channel = channel; parts[part].patch = std::max (0, std::min (31, patch));
}

void Engine::applyGroups()
{
    int tmp[kVoices];
    for (int i = 0; i < kVoices; ++i) tmp[i] = -1;
    if (voiceMode >= 2)
    {
        int v = 0;
        for (int g = 0; g < 8; ++g)
            for (int k = 0; k < kVoiceSplit[voiceMode - 2][g] && v < kVoices; ++k) tmp[v++] = g;
    }
    for (int i = 0; i < kVoices; ++i) groupOf[i] = tmp[i];
}

int Engine::activeVoices() const { int n = 0; for (const auto& v : voices) if (v.on) ++n; return n; }

void Engine::allNotesOff()
{
    for (auto& v : voices) { v.on = false; v.keep.reset(); }
    for (int c = 0; c < 16; ++c) { sustain[c] = false; bend[c] = 0; }
}

float Engine::cutoffHz (float v, float semitones, float egOct)
{
    // cut-off 0..127: about 30 Hz .. 20 kHz (9.4 octaves); key follow and envelope move it in octaves
    const float oct = v / 127.0f * 9.4f + semitones / 12.0f + egOct;
    return 30.0f * std::pow (2.0f, std::max (-1.0f, std::min (9.7f, oct)));
}

void Engine::midi (const uint8_t* m, int n)
{
    if (n < 1) return;
    const int st = m[0] & 0xf0, ch = m[0] & 0x0f;
    if (st == 0x90 && n >= 3) { if (m[2] > 0) noteOn (ch, m[1] & 0x7f, m[2] & 0x7f); else noteOff (ch, m[1] & 0x7f); }
    else if (st == 0x80 && n >= 3) noteOff (ch, m[1] & 0x7f);
    else if (st == 0xe0 && n >= 3) bend[ch] = (float) (((m[2] & 0x7f) << 7 | (m[1] & 0x7f)) - 8192) / 8192.0f;
    else if (st == 0xb0 && n >= 3)
    {
        if (m[1] == 64)
        {
            const bool on = m[2] >= 64;
            sustain[ch] = on;
            if (! on) for (auto& v : voices) if (v.on && v.ch == ch && v.held) { v.held = false; v.tva.release(); v.tvf.release(); v.released = true; }
        }
        else if (m[1] == 120 || m[1] == 123)
            for (auto& v : voices) if (v.on && v.ch == ch) { if (m[1] == 120) v.on = false; else { v.tva.release(); v.tvf.release(); v.released = true; } }
    }
}

Engine::Voice* Engine::allocate (int part, int ch)
{
    (void) ch;
    Voice* best = nullptr;
    const bool pool = voiceMode < 2;
    for (int i = 0; i < kVoices; ++i)
    {
        if (! pool && groupOf[i] != part) continue;
        if (! voices[i].on) return &voices[i];
        if (! best || (voices[i].released && ! best->released) || (voices[i].released == best->released && voices[i].age < best->age)) best = &voices[i];
    }
    if (voiceMode == 1 && best && ! best->released) { drops.fetch_add (1, std::memory_order_relaxed); return nullptr; }      // VAF: the first notes keep their voices
    if (! best) drops.fetch_add (1, std::memory_order_relaxed);                                                         // the group of this part has no voice at all
    else steals.fetch_add (1, std::memory_order_relaxed);
    return best;
}

void Engine::startVoice (int ch, int key, int vel, int part, const Snapshot& s, int toneIdx, float gain)
{
    if (toneIdx < 0 || toneIdx >= 2 * data::kNumTones) return;          // 0-31 Block I, 32-63 Block II
    const int blkBase = toneIdx / data::kNumTones * data::kNumTones;
    const data::Tone* src = &s.tones[toneIdx];
    const data::Tone& t = s.tones[toneIdx];
    if (t.subTone) src = &s.tones[blkBase + std::min (31, (int) t.sourceTone)];
    if (! src->used()) return;
    const auto& w = s.wave[blkBase / data::kNumTones * 2 + (src->waveBank & 1)];
    if (! w) return;
    Voice* v = allocate (part, ch);
    if (! v) return;
    *v = Voice();
    v->on = true; v->ch = ch; v->key = key; v->vel = vel; v->part = part; v->group = groupOf[0];
    v->age = ++counter;
    v->tone = t;
    v->keep = w; v->wave = w.get();
    v->base = src->waveStart * data::kSegWords;
    v->words = std::min ((int) src->waveLen * data::kSegWords, (int) w->size() - v->base);
    if (v->words < 2) { v->on = false; return; }
    const double waveRate = t.khz15 ? 15000.0 : 30000.0;
    const double semis = key - (int) t.origKey;
    v->step = waveRate / sr * std::pow (2.0, semis / 12.0);
    const int end = std::min ((int) t.endPoint, v->words - 1);
    if (t.loopMode == 3) { v->pos = end; v->dir = -1; } else { v->pos = (double) std::min ((int) t.startPoint, v->words - 1); v->dir = 1; }
    v->tva.start (t.rest, 51, 53, 54, t.at (72), vel, 0, key);
    v->tvf.start (t.rest, 107, 109, 110, t.at (47), vel, t.at (46), key);
    v->cutoffBase = t.at (38); v->res = t.at (39); v->keyFollow = (float) (int8_t) t.at (40); v->egDepth = t.at (43);
    v->gain = gain * 0.5f * (float) (vel + 1) / 128.0f;
    v->out = pendingOut >= 0 && pendingOut < 8 ? pendingOut : (int) (t.f1 & 7);     // patch Output Assign Out 1-8, or Tone: the output of the tone itself
    v->bendRange = (float) s.patches[std::min (31, parts[part].patch)].raw[12];
    if (v->bendRange > 12) v->bendRange = 12;
}

void Engine::noteOn (int ch, int key, int vel)
{
    auto s = std::atomic_load (&snap);
    if (! s) return;
    const int at = audTone.load(), ap = audPatch.load();
    if (at >= 0) { pendingOut = -2; startVoice (ch, std::max (0, std::min (127, key)), vel, 0, *s, at, 1.0f); return; }
    for (int p = 0; p < kParts; ++p)
    {
        if (ap < 0 && parts[p].channel != ch) continue;
        if (ap >= 0 && p > 0) break;
        const int gp = std::max (0, std::min (31, ap >= 0 ? ap : parts[p].patch));
        const int tb = gp / data::kNumPatches * data::kNumTones;            // tone numbers of a patch belong to its own block
        const data::Patch& pt = s->patches[gp];
        const int ki = std::max (0, std::min (108, key - 12));
        const int t1 = tb + pt.raw[17 + ki], t2 = tb + pt.raw[126 + ki];
        const int mode = pt.raw[15] & 3, thr = pt.raw[16];
        if (pt.raw[237] == 0) continue;                                  // empty patch: silent
        const float level = pt.raw[237] / 127.0f;
        pendingOut = pt.raw[243] < 8 ? (int) pt.raw[243] : -2;           // byte 243: 0-7 = Out 1-8, 8 = Tone
        const int oct = (int) (int8_t) pt.raw[236];
        const int k = std::max (0, std::min (127, key + 12 * std::max (-2, std::min (2, oct))));
        switch (mode)
        {
            case 0:  startVoice (ch, k, vel, p, *s, t1, level); break;                                   // Normal: 1st tone
            case 1:  startVoice (ch, k, vel, p, *s, t1, level * 0.7f); startVoice (ch, k, vel, p, *s, t2, level * 0.7f); break;   // V-Mix
            case 2:  startVoice (ch, k, vel, p, *s, vel < thr ? t1 : t2, level); break;                  // V-Sw
            default: startVoice (ch, k, vel, p, *s, t1, level * (1.0f - vel / 127.0f)); startVoice (ch, k, vel, p, *s, t2, level * (vel / 127.0f)); break;   // X-Fade
        }
    }
}

void Engine::noteOff (int ch, int key)
{
    for (auto& v : voices)
        if (v.on && v.ch == ch && v.key == key && ! v.released && ! v.held)
        {
            if (sustain[ch]) { v.held = true; continue; }
            v.tva.release(); v.tvf.release(); v.released = true;
        }
}

void Engine::renderVoice (Voice& v, float* l, float* r, int n)
{
    const std::vector<int16_t>& w = *v.wave;
    const int start = std::min ((int) v.tone.startPoint, v.words - 1);
    const int end = std::max (start + 1, std::min ((int) v.tone.endPoint, v.words - 1));
    const int loop = std::max (start, std::min ((int) v.tone.loopPoint, end - 1));
    const int mode = v.tone.loopMode;
    const float bendSemis = bend[v.ch] * v.bendRange;
    const double step = v.step * std::pow (2.0, bendSemis / 12.0);
    int i = 0;
    while (i < n && v.on)
    {
        const int blk = std::min (n - i, 16);
        v.tva.run (blk, sr);
        v.tvf.run (blk, sr);
        // filter coefficients (control rate)
        const float egOct = v.egDepth / 127.0f * (v.tvf.cur / 127.0f) * 6.0f;
        const float fc = std::min (0.45f * (float) sr, cutoffHz (v.cutoffBase, v.keyFollow / 63.0f * (float) (v.key - 60), egOct));
        const float g = std::tan (3.14159265f * fc / (float) sr);
        const float k = 2.0f - 1.9f * v.res / 127.0f;                       // damping: 2 (flat) .. 0.1 (resonant)
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float lv = v.tva.cur / 127.0f;
        const float amp = v.gain * lv * lv;
        for (int j = 0; j < blk; ++j)
        {
            // wave position
            int ip = (int) v.pos; ip = std::max (0, std::min (v.words - 2, ip));
            const float fr = (float) (v.pos - ip);
            const float s0 = w[(size_t) (v.base + ip)], s1 = w[(size_t) (v.base + ip + 1)];
            const float x = (s0 + (s1 - s0) * fr) / 2048.0f;
            // TVF low-pass (TPT state variable filter)
            const float v3 = x - v.ic2;
            const float v1 = a1 * v.ic1 + a2 * v3;
            const float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2 * v1 - v.ic1; v.ic2 = 2 * v2 - v.ic2;
            const float y = v2 * amp * v.fadeOut;
            l[i + j] += y; r[i + j] += y;
            // advance
            v.pos += v.dir * step;
            if (mode == 3)
            {
                if (v.pos <= start) { v.finished = true; }
            }
            else if (mode == 2)
            {
                if (v.pos >= end) v.finished = true;
            }
            else if (v.dir > 0 && v.pos >= end)
            {
                if (loop >= end - 1) v.finished = true;
                else if (mode == 1) { v.dir = -1; v.pos = 2.0 * end - v.pos; }
                else v.pos = loop + (v.pos - end);
                if (v.pos >= end) v.pos = loop;
            }
            else if (v.dir < 0 && v.pos <= loop) { v.dir = 1; v.pos = 2.0 * loop - v.pos; }
            if (v.finished) { v.fadeOut -= 1.0f / 64.0f; if (v.fadeOut <= 0.0f) { v.on = false; v.keep.reset(); break; } }
        }
        i += blk;
        if (v.on && v.tva.done() && v.tva.cur <= 0.5f) { v.on = false; v.keep.reset(); }
    }
}

void Engine::render (float* left, float* right, int n, float* const* outL, float* const* outR)
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    if (outL && outR)
        for (int o = 0; o < 8; ++o)
            if (outL[o] && outR[o]) { std::fill (outL[o], outL[o] + n, 0.0f); std::fill (outR[o], outR[o] + n, 0.0f); }
    for (auto& v : voices)
        if (v.on)
        {
            const int o = v.out & 7;
            if (outL && outR && outL[o] && outR[o]) renderVoice (v, outL[o], outR[o], n);     // its own output is in use
            else renderVoice (v, left, right, n);                                              // otherwise it plays in the mix
        }

    clickPending += clickReq.exchange (0);
    if (clickPending > 0 || clickTick > 1e-4f || clickThump > 1e-4f)
    {
        const float dTick = std::exp (-1.0f / (0.0007f * (float) sr)), dThump = std::exp (-1.0f / (0.0035f * (float) sr));
        const float w = 6.2831853f * 1400.0f / (float) sr;
        for (int i = 0; i < n; ++i)
        {
            if (clickPending > 0 && --clickDelay <= 0)
            {
                clickTick = 1.0f; clickThump = 0.9f; clickPhase = 0.0f; --clickPending;
                clickRng = clickRng * 1664525u + 1013904223u;
                clickDelay = (int) (0.012 * sr);
            }
            clickRng = clickRng * 1664525u + 1013904223u;
            const float noise = ((int) (clickRng >> 16) - 32768) / 32768.0f;
            const float hp = noise - clickLast; clickLast = noise;            // high-passed noise: a bright tick
            const float s = 0.09f * (0.5f * hp * clickTick + 0.9f * std::sin (clickPhase) * clickThump);
            clickTick *= dTick; clickThump *= dThump; clickPhase += w;
            left[i] += s; right[i] += s;
        }
    }
}
}
