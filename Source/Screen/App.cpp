#include <chrono>
#include "App.h"
#include "../Data/WaveImport.h"
#ifndef S5_EMPTY_START
#define S5_EMPTY_START 0
#endif
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

namespace s5
{
void App::midiFeed (const uint8_t* m, int n)
{
    if (n <= 0 || m[0] < 0x80) return;
    const int st = m[0];
    if (st < 0xF0)
    {
        const int type0 = st & 0xF0, ch0 = st & 15;
        if ((type0 == 0x90 || type0 == 0x80) && n >= 3)
        {
            const uint8_t v = (type0 == 0x90 && m[2] > 0) ? 1 : 0;
            if (held[ch0][m[1] & 127] != v) { held[ch0][m[1] & 127] = v; uiDirty = true; }
            if (v) { ++pressSeq; pressKey = m[1] & 127; pressMs = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
        }
        else if (type0 == 0xB0 && n >= 3 && (m[1] == 123 || m[1] == 120))
        {
            for (int k = 0; k < 128; ++k) held[ch0][k] = 0;
            uiDirty = true;
        }
    }
    if (st >= 0xF8) { if (! monRt) return; }
    else if (st < 0xF0 && monCh < 16 && (st & 15) != monCh) return;
    for (int i = 0; i < n; ++i) midiLog.push_back (m[i]);
    if (midiLog.size() > 180) midiLog.erase (midiLog.begin(), midiLog.begin() + (long) (midiLog.size() - 180));
    const int type = st & 0xF0;
    if (st >= 0xF0) return;
    if ((type == 0x90 || type == 0x80) && n >= 3) { lastKind = 1; lastKey = m[1]; lastVel = m[2]; }
}

void App::initDevParams()
{
    static const int rx[8] = { 3, 10, 1, 2, 5, 6, 7, 8 };
    for (int i = 0; i < 8; ++i)
    {
        mpar[0x20 + i] = rx[i] - 1;                      // RX channel (16 = off)
        mpar[0x28 + i] = 1; mpar[0x30 + i] = 1; mpar[0x38 + i] = 1;   // P.Chg, Bender, Modulation
        mpar[64 + i] = 1;                                // Hold
        mpar[72 + i] = 0; mpar[80 + i] = 0; mpar[88 + i] = 0;         // A.Touch, Volume, Bend range
    }
    mpar[97] = 0; mpar[98] = 0;                          // exclusive off, device ID 1
    for (int i = 0; i < 32; ++i) mpar[99 + i] = i;       // program change numbers 1-32
    fpar[0] = 0;
}

void App::noteSet (int ch, int key, bool on)
{
    const uint8_t m[3] = { (uint8_t) ((on ? 0x90 : 0x80) | (ch & 15)), (uint8_t) (key & 127), (uint8_t) (on ? 100 : 0) };
    midiFeed (m, 3);
    if (midiOut) midiOut (m, 3);
    if (soundOut) soundOut (m, 3);
}

const data::Block* App::hdBlockOf (int slot)
{
    if (! hasHd || slot < 0 || slot >= data::kHdSlots) return nullptr;
    if (hdCacheSlot != slot) { hdCacheSlot = slot; hdCacheOk = hd.loadBlock (slot, hdCache); }
    return hdCacheOk ? &hdCache : nullptr;
}

void App::pickHd()
{
    if (! files || ! files->dialogs()) return;
    fileMsg = "Waiting for the Finder ..."; fileMsgErr = false;
    files->pick ([this] (bool ok, const std::string& name, std::vector<uint8_t>& bytes)
    {
        if (! ok) { fileMsg = name == "Cancelled" ? "Cancelled" : name; fileMsgErr = name != "Cancelled"; }
        else if (! hd.open (bytes.data(), bytes.size())) { hasHd = false; fileMsg = "Not an S-550 hard disk image"; fileMsgErr = true; }
        else
        {
            hasHd = true; hdName = name;
            int n = 0; for (auto& s : hd.slots) if (s.valid) ++n;
            fileMsg = "Loaded  " + name + "  " + std::to_string (n) + " Blocks"; fileMsgErr = false;
            hdSlot = 0; for (int i = 0; i < data::kHdSlots; ++i) if (hd.slots[i].valid) { hdSlot = i; break; }
            hdSlotI = hdSlotII = hdArea = hdSlot; hdWhich = 0; hdSync = true; hdCacheSlot = -1;
        }
        uiDirty = true;
    });
}

static std::string hdBaseName (const std::string& path)
{
    const size_t sl = path.find_last_of ("/\\");
    std::string n = sl == std::string::npos ? path : path.substr (sl + 1);
    const size_t dot = n.find_last_of ('.');
    if (dot != std::string::npos && dot > 0) n = n.substr (0, dot);
    for (auto& c : n) c = (char) std::toupper ((unsigned char) c);
    return n;
}

bool App::hdActivate (int id)
{
    if (id < 0 || id > 6 || ! hdDev[id].ok || ! readPath) return false;
    std::vector<uint8_t> bytes;
    if (! readPath (hdDev[id].path, bytes) || ! hd.open (bytes.data(), bytes.size())) { hdDev[id].ok = false; return false; }
    hasHd = true; hdName = hdDev[id].name; hdActiveId = id;
    hdSlot = 0; for (int i = 0; i < data::kHdSlots; ++i) if (hd.slots[i].valid) { hdSlot = i; break; }
    hdSlotI = hdSlotII = hdArea = hdSlot; hdWhich = 0; hdSync = true; hdCacheSlot = -1;
    uiDirty = true;
    return true;
}

bool App::hdAttach (int id, const std::string& path, std::string& err)
{
    err.clear();
    if (id == 7) { err = "ID 7 is the S-550 itself"; return false; }
    if (id < 0 || id > 6) { err = "ID must be 0 - 6"; return false; }
    if (id == cdId) { err = "ID " + std::to_string (id) + " is used by the CD-5"; return false; }
    std::vector<uint8_t> bytes;
    if (! readPath || ! readPath (path, bytes)) { err = "Cannot read file"; return false; }
    data::HdImage probe;
    if (! probe.open (bytes.data(), bytes.size())) { err = "Not an S-550 hard disk image"; return false; }
    const int n = probe.areas;
    hdDev[id].path = path; hdDev[id].name = hdBaseName (path); hdDev[id].ok = true; hdDev[id].areas = n;
    if (hdActiveId < 0 || id >= hdActiveId) hdActivate (id);          // the highest ID boots
    if (hdConfigChanged) hdConfigChanged();
    uiDirty = true;
    return true;
}

void App::hdDetach (int id)
{
    if (id < 0 || id > 6) return;
    hdDev[id] = HdDevice();
    if (hdActiveId == id)
    {
        hdActiveId = -1; hasHd = false; hdName.clear(); hdCacheSlot = -1;
        for (int i = 6; i >= 0; --i) if (hdDev[i].ok && hdActivate (i)) break;
    }
    if (hdConfigChanged) hdConfigChanged();
    uiDirty = true;
}

bool App::setCdId (int id, std::string& err)
{
    err.clear();
    if (id < -1 || id > 6) { err = "ID must be 0 - 6"; return false; }
    if (id >= 0 && hdDev[id].ok) { err = "ID " + std::to_string (id) + " is used by a hard disk"; return false; }
    cdId = id;
    if (hdConfigChanged) hdConfigChanged();
    uiDirty = true;
    return true;
}

bool App::hdSaveSlot (int slot, const data::Block& b, std::string& err, bool withParams)
{
    err.clear();
    if (! hasHd || hdActiveId < 0) { err = "HD Connection Error"; return false; }
    std::vector<std::pair<size_t, size_t>> dirty;
    if (! hd.saveBlock (slot, b, dirty, err, withParams)) return false;
    if (! writeRange) { err = "HD Write Error"; return false; }
    for (const auto& d : dirty)
        if (! writeRange (hdDev[hdActiveId].path, d.first, hd.bytes.data() + d.first, d.second)) { err = "HD Write Error"; return false; }
    hdCacheSlot = -1; uiDirty = true;
    return true;
}

bool App::hdVerifySlot (int slot, const data::Block& b, std::string& err)
{
    err.clear();
    if (! hasHd || hdActiveId < 0) { err = "HD Connection Error"; return false; }
    std::vector<uint8_t> bytes;
    if (! readPath || ! readPath (hdDev[hdActiveId].path, bytes)) { err = "HD Read Error"; return false; }
    data::HdImage img; data::Block back;
    if (! img.open (bytes.data(), bytes.size()) || slot < 0 || slot >= data::kHdSlots || ! img.loadBlock (slot, back)) { err = "Verify Error"; return false; }
    for (int i = 0; i < 32; ++i)                                  // every wave that a tone of the block owns
    {
        const auto& t = b.tones[(size_t) i];
        if (! t.used() || t.subTone) continue;
        const size_t from = (size_t) t.waveStart * data::kSegWords, to = std::min ((size_t) data::kWaveWords, from + (size_t) t.waveLen * data::kSegWords);
        const auto& w1 = b.wave[t.waveBank & 1]; const auto& w2 = back.wave[t.waveBank & 1];
        if (w1.size() < to || w2.size() < to || ! std::equal (w1.begin() + (long) from, w1.begin() + (long) to, w2.begin() + (long) from)) { err = "Verify Error"; return false; }
    }
    return true;
}

bool App::hdLoadSys (int id, std::string& err)
{
    err.clear();
    constexpr size_t kSec = 512, kFirst = 256, kEnd = 5368;
    if (id < 0 || id > 6 || ! hdDev[id].ok) { err = "HD Connection Error"; return false; }
    std::vector<uint8_t> ref;
    if (! readPath || ! readPath (hdDev[id].path, ref)) { err = "Cannot read file"; return false; }
    if (ref.size() < kEnd * kSec || std::memcmp (ref.data(), "* ROLAND S-550 *", 16) != 0
        || std::memcmp (ref.data() + kFirst * kSec + 4, "S550 MT25B", 10) != 0 || std::memcmp (ref.data() + kSec, "S-550", 5) != 0)
    { err = "HD No System"; return false; }
    hdSysMem.assign (ref.begin() + kSec, ref.begin() + 2 * kSec);
    hdSysMem.insert (hdSysMem.end(), ref.begin() + kFirst * kSec, ref.begin() + kEnd * kSec);
    std::string line (reinterpret_cast<const char*> (ref.data() + kFirst * kSec + 32), 31);
    while (! line.empty() && (line.back() == ' ' || line.back() == 0)) line.pop_back();
    hdSysLine = line;
    return true;
}

bool App::hdSaveSys (int id, std::string& err)
{
    err.clear();
    constexpr size_t kSec = 512, kFirst = 256, kEnd = 5368;
    if (hdSysMem.size() != kSec + (kEnd - kFirst) * kSec) { err = "No System loaded"; return false; }
    if (id < 0 || id > 6 || ! hdDev[id].ok) { err = "HD Connection Error"; return false; }
    if (! writeRange) { err = "HD Write Error"; return false; }
    const std::string& path = hdDev[id].path;
    if (! writeRange (path, kSec, hdSysMem.data(), kSec)
        || ! writeRange (path, kFirst * kSec, hdSysMem.data() + kSec, (kEnd - kFirst) * kSec)) { err = "HD Write Error"; return false; }
    if (hasHd && id == hdActiveId && hd.bytes.size() >= kEnd * kSec)                 // the image held in memory follows
    {
        std::memcpy (hd.bytes.data() + kSec, hdSysMem.data(), kSec);
        std::memcpy (hd.bytes.data() + kFirst * kSec, hdSysMem.data() + kSec, (kEnd - kFirst) * kSec);
        hdCacheSlot = -1;
    }
    uiDirty = true;
    return true;
}

bool App::hdLoadSlot()
{
    data::Block nb;
    if (! hasHd || ! hd.loadBlock (hdSlot, nb)) { fileMsg = "Empty slot"; fileMsgErr = true; return false; }
    disk = nb; hasDisk = true; imageName = data::HdImage::slotId (hdSlot);
    return true;
}

void App::newSet (const data::Block& b)
{
    block = b; linked = true;
    curTone = 0; curPatch = 0; curKey = 60;
    auditionTone = auditionPatch = -1;
    const auto pf = b.performance();                  // the part table is part of the Set
    voiceMode = pf.voiceMode;
    for (int i = 0; i < 8; ++i) { partCh[i] = pf.ch[i]; partPatch[i] = pf.patch[i]; partLevel[i] = pf.level[i]; }
    for (int c = 0; c < 16; ++c)
    {
        for (int k = 0; k < 128; ++k) held[c][k] = 0;
        if (soundOut) { const uint8_t m[3] = { (uint8_t) (0xb0 | c), 123, 0 }; soundOut (m, 3); }      // all notes off: nothing of the old set keeps sounding
    }
}

void App::newSet2 (const data::Block& b)
{
    block2 = b; linked2 = true;
    for (int c = 0; c < 16; ++c)
        if (soundOut) { const uint8_t m[3] = { (uint8_t) (0xb0 | c), 123, 0 }; soundOut (m, 3); }
}

void App::pickImage (bool intoMemory, const std::string& title)
{
    if (! files || ! files->dialogs()) return;
    fileMsg = "Waiting for the Finder ..."; fileMsgErr = false;
    files->pick ([this, intoMemory] (bool ok, const std::string& name, std::vector<uint8_t>& bytes)
    {
        data::Block nb;
        if (! ok) { fileMsg = name == "Cancelled" ? "Cancelled" : name; fileMsgErr = name != "Cancelled"; }
        else if (nb.parse (bytes.data(), bytes.size()) != data::Block::Result::Ok) { fileMsg = "Not an S-550 or S-50 disk image"; fileMsgErr = true; }
        else
        {
            disk = nb; hasDisk = true; imageName = name; pickDone = true;
            if (intoMemory) newSet (nb);
            int used = 0; for (auto& t : nb.tones) if (t.used()) ++used;
            fileMsg = "Loaded  " + name + ".IMG  " + std::to_string (used) + " Tones"; fileMsgErr = false;
        }
        uiDirty = true;
    }, title);
}

int App::recLevel7 (float p)
{
    if (p <= 0.f) return 0;
    const float db = 20.f * std::log10 (p);
    return std::max (0, std::min (127, (int) std::lround (127.f * (1.f + db / 48.f))));
}

void App::recFeed (const float* in, int n, double hostRate)
{
    float pk = rec.level.load (std::memory_order_relaxed) * std::exp (-(float) n / (float) (hostRate * 0.18));
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::fabs (in[i]));
    rec.level.store (pk, std::memory_order_relaxed);
    int st = rec.state.load (std::memory_order_acquire);
    if (st != 1 && st != 2) return;
    const int total = rec.pre + rec.maxN;
    for (int i = 0; i < n; ++i)
    {
        if (st == 1)
        {
            if (rec.manual || std::fabs (in[i]) >= rec.thr)
            {
                if (rec.pre > 0 && rec.got >= rec.pre)                    // ring is full: oldest sample first
                    std::rotate (rec.buf.begin(), rec.buf.begin() + rec.w, rec.buf.begin() + rec.pre);
                rec.got = rec.pre > 0 ? std::min (rec.got, rec.pre) : 0;
                st = 2; rec.state.store (2, std::memory_order_release);
            }
            else if (rec.pre > 0) { rec.buf[(size_t) rec.w] = in[i]; rec.w = (rec.w + 1) % rec.pre; if (rec.got < rec.pre) ++rec.got; continue; }
            else continue;
        }
        if (rec.got < total) rec.buf[(size_t) rec.got++] = in[i];
        if (rec.got >= total) { rec.state.store (3, std::memory_order_release); return; }
    }
}

std::string App::recArm (int tone, int bank, bool khz15, double secs, double preMs, int thr7, bool manual, bool limiter, int key, double hostRate)
{
    if (hostRate < 8000.0) return "Cannot Execute";
    if (secs <= 0.0) return "Cannot Execute";
    data::Block& b = blk (tone / 32);
    const double cap = b.freeSegments (bank) * 0.4 * (khz15 ? 2.0 : 1.0);
    if (secs > cap + 1e-9) return "Memory full";
    rec.state.store (0, std::memory_order_release);
    rec.rate = hostRate; rec.manual = manual; rec.limiter = limiter; rec.khz15 = khz15;
    rec.tone = tone; rec.bank = bank; rec.key = key;
    rec.pre = manual ? 0 : (int) (preMs * 0.001 * hostRate);
    rec.maxN = (int) (secs * hostRate);
    rec.thr = thr7 <= 0 ? 0.f : std::pow (10.f, (float) ((thr7 / 127.0 - 1.0) * 48.0 / 20.0));
    rec.got = 0; rec.w = 0; rec.msg.clear(); rec.review = false;
    rec.buf.assign ((size_t) (rec.pre + rec.maxN + 1), 0.f);
    rec.state.store (1, std::memory_order_release);
    return "";
}

void App::recCancel() { rec.state.store (0, std::memory_order_release); rec.msg.clear(); }

void App::recPoll()
{
    if (rec.state.load (std::memory_order_acquire) != 3) return;
    const double R = rec.khz15 ? 15000.0 : 30000.0;
    const int total = rec.got;
    const int nOut = (int) (total / rec.rate * R);
    std::vector<int16_t> out ((size_t) std::max (nOut, 1), 0);
    const double ratio = rec.rate / R;
    for (int i = 0; i < nOut; ++i)
    {
        const double pos = i * ratio;
        double v;
        if (ratio > 1.0)
        {
            const int a = (int) pos, e = std::min (total, std::max (a + 1, (int) (pos + ratio)));
            double sum = 0; for (int k = a; k < e; ++k) sum += rec.buf[(size_t) k];
            v = sum / (e - a);
        }
        else
        {
            const int a = (int) pos; const double fr = pos - a;
            const float x0 = rec.buf[(size_t) std::min (a, total - 1)], x1 = rec.buf[(size_t) std::min (a + 1, total - 1)];
            v = x0 + (x1 - x0) * fr;
        }
        if (rec.limiter) v = std::tanh (v * 1.2) / std::tanh (1.2);        // soft peak limiter
        out[(size_t) i] = (int16_t) std::max (-2048, std::min (2047, (int) std::lround (v * 2047.0)));
    }
    rec.state.store (0, std::memory_order_release);
    const std::string err = data::storeSample (blk (rec.tone / 32), rec.tone % 32, rec.bank, out, rec.khz15, rec.key, "Sample");
    rec.msg = err.empty() ? "Complete" : err;
    if (err.empty() && rec.tone < 32) linked = true;
    rec.review = err.empty();
    uiDirty = true;
}

void App::scopeFeed (const float* in, int n)
{
    if (n <= 0) return;
    int start = 0;
    for (int i = 1; i + 480 < n; ++i)
        if (in[i - 1] < 0.f && in[i] >= 0.f) { start = i; break; }
    for (int i = 0; i < 480; ++i)
    {
        const int k = start + i;
        const float v = k < n ? in[k] : 0.f;
        scope[(size_t) i] = (int16_t) std::max (-2048.f, std::min (2047.f, v * 2048.f));
    }
}

data::Block makeDemoBlock (int variant)
{
    static const char* pn[8] = { "Drums/Flute", "SBass/Xylo", "E.Piano", "Trumpet", "FLBass/SBell", "Strings", "Poly Synth", "Synth Bell" };
    static const char* pb[8] = { "Brass Hit", "Fat Bass", "Choir", "Organ", "Slap Bass", "Pad Warm", "Pluck", "Glass Bell" };
    static const char* tn[8] = { "Kick", "Snare", "HiHat", "Crash", "Cowbell", "HandClap", "Pan Shot", "Bell" };
    static const char* tb[8] = { "Brass 1", "Brass 2", "Tuba", "Horn", "Sax", "Flute", "Oboe", "Clarinet" };
    data::Block b;
    b.label = variant == 0 ? "S-550 Demo  " : "Brass Bank  ";
    for (int i = 0; i < data::kNumPatches; ++i) b.patches[i].setName (variant == 0 ? pn[i % 8] : pb[i % 8]);
    for (int t = 0; t < data::kNumTones; ++t)
    {
        auto& o = b.tones[t];
        const int k = t < 16 ? t % 8 : (t - 16 + 3) % 8;
        o.name = variant == 0 ? tn[k] : tb[k];
        o.name.resize (8, ' ');
        o.sourceTone = (uint8_t) t; o.origKey = 60; o.khz15 = false;
        if (t < 8)
        {
            o.waveBank = 0; o.waveStart = (uint8_t) (t * 2); o.waveLen = 2;
            o.startPoint = 0; o.endPoint = 2 * data::kSegWords - 1; o.loopPoint = o.endPoint / 2;
        }
        data::setPlayableDefaults (o);
    }
    for (int i = 0; i < data::kNumPatches; ++i)
        for (int ki = 0; ki < 109; ++ki) { b.patches[i].raw[17 + ki] = (uint8_t) std::min (7, ki * 8 / 109); b.patches[i].raw[126 + ki] = b.patches[i].raw[17 + ki]; b.patches[i].raw[237] = 127; }
    for (int t = 0; t < 8; ++t)
        for (int i = 0; i < 2 * data::kSegWords; ++i)
        {
            const double env = std::exp (-i / (3000.0 + 800.0 * t));
            b.wave[0][(size_t) (t * 2 * data::kSegWords + i)] = (int16_t) std::lround (1800.0 * env * std::sin (i * (0.02 + 0.01 * t) * (variant + 1)));
        }
    return b;
}

App::App()
{
    initDevParams();
    auto mem = std::make_unique<MemFiles>();
    mem->write ("FACTORY1", makeDemoBlock (0).serialize());
    mem->write ("BRASS", makeDemoBlock (1).serialize());
    files = std::move (mem);
    drive = std::make_unique<SimFloppy> (makeDemoBlock (1).serialize());
    block = makeDemoBlock (0);
#if S5_EMPTY_START
    block = data::Block();                // plugin / standalone: an empty sampler without names and waves
    linked = true;
#endif
}

App& App::get() { static App a; return a; }
}
