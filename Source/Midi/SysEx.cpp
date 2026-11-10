#include "SysEx.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace s5::sysex
{
Bytes encodeAddr (uint32_t a) { return { (uint8_t) ((a >> 21) & 127), (uint8_t) ((a >> 14) & 127), (uint8_t) ((a >> 7) & 127), (uint8_t) (a & 127) }; }
uint32_t decodeAddr (const uint8_t* p) { return ((uint32_t) p[0] << 21) | ((uint32_t) p[1] << 14) | ((uint32_t) p[2] << 7) | p[3]; }
Bytes addrAdd (uint32_t a, uint32_t off) { return encodeAddr (a + off); }
uint8_t checksum (const uint8_t* p, size_t n) { unsigned s = 0; for (size_t i = 0; i < n; ++i) s += p[i]; return (uint8_t) ((128 - (s & 127)) & 127); }

static Bytes head (int dev, uint8_t cmd) { return { 0xf0, kRoland, (uint8_t) (dev & 15), kModel, cmd }; }
static Bytes withBody (int dev, uint8_t cmd, uint32_t addr, const uint8_t* d, size_t n)
{
    Bytes m = head (dev, cmd);
    const Bytes a = encodeAddr (addr);
    m.insert (m.end(), a.begin(), a.end());
    m.insert (m.end(), d, d + n);
    m.push_back (checksum (m.data() + 5, m.size() - 5));
    m.push_back (0xf7);
    return m;
}
static Bytes withSize (int dev, uint8_t cmd, uint32_t addr, uint32_t size) { const Bytes s = encodeAddr (size); return withBody (dev, cmd, addr, s.data(), 4); }

Bytes buildRQ1 (int dev, uint32_t a, uint32_t s) { return withSize (dev, RQ1, a, s); }
Bytes buildRQD (int dev, uint32_t a, uint32_t s) { return withSize (dev, RQD, a, s); }
Bytes buildWSD (int dev, uint32_t a, uint32_t s) { return withSize (dev, WSD, a, s); }
Bytes buildDT1 (int dev, uint32_t a, const uint8_t* d, size_t n) { return withBody (dev, DT1, a, d, n); }
Bytes buildDAT (int dev, uint32_t a, const uint8_t* d, size_t n) { return withBody (dev, DAT, a, d, n); }
Bytes buildSimple (int dev, uint8_t cmd) { Bytes m = head (dev, cmd); m.push_back (0xf7); return m; }

bool parse (const uint8_t* p, size_t n, Message& o)
{
    if (n < 6 || p[0] != 0xf0 || p[1] != kRoland || p[3] != kModel || p[n - 1] != 0xf7) return false;
    o = Message();
    o.dev = p[2]; o.cmd = p[4];
    switch (o.cmd)
    {
        case ACK: case EOD: case ERR: case RJC: return n == 6;
        case RQ1: case RQD: case WSD:
            if (n != 5 + 4 + 4 + 1 + 1 || checksum (p + 5, 8) != p[13]) return false;
            o.addr = decodeAddr (p + 5); o.size = decodeAddr (p + 9); return true;
        case DT1: case DAT:
            if (n < 5 + 4 + 1 + 1 + 1 || checksum (p + 5, n - 7) != p[n - 2]) return false;
            o.addr = decodeAddr (p + 5); o.data.assign (p + 9, p + n - 2); o.size = (uint32_t) o.data.size(); return true;
        default: return false;
    }
}

void nib (Bytes& o, uint8_t v) { o.push_back ((uint8_t) (v >> 4)); o.push_back ((uint8_t) (v & 15)); }
Bytes toNibbles (const uint8_t* p, size_t n) { Bytes o; o.reserve (n * 2); for (size_t i = 0; i < n; ++i) nib (o, p[i]); return o; }
Bytes fromNibbles (const uint8_t* p, size_t n) { Bytes o; for (size_t i = 0; i + 1 < n; i += 2) o.push_back ((uint8_t) (((p[i] & 15) << 4) | (p[i + 1] & 15))); return o; }

void toneRecord (const data::Tone& o, uint8_t* t)
{
    std::string nm = o.name; nm.resize (8, ' ');
    std::copy (nm.begin(), nm.end(), t);
    t[8] = o.f1; t[9] = o.sourceTone; t[10] = o.subTone ? 1 : 0; t[11] = o.khz15 ? 1 : 0; t[12] = o.origKey;
    t[13] = o.waveBank; t[14] = o.waveStart; t[15] = o.waveLen;
    auto w24 = [] (uint8_t* d, uint32_t v) { d[0] = (uint8_t) (v >> 16); d[1] = (uint8_t) (v >> 8); d[2] = (uint8_t) v; };
    w24 (t + 16, o.startPoint); w24 (t + 19, o.endPoint); w24 (t + 22, o.loopPoint); t[25] = o.loopMode;
    std::copy (o.rest, o.rest + 102, t + 26);
}
Bytes toneToSysex (const data::Tone& o) { uint8_t t[128]; toneRecord (o, t); return toNibbles (t, 128); }
void sysexToTone (const uint8_t* p, data::Tone& o) { const Bytes t = fromNibbles (p, 256); recordTone (t.data(), o); }
void recordTone (const uint8_t* t, data::Tone& o)
{
    auto r24 = [] (const uint8_t* d) { return ((uint32_t) d[0] << 16) | ((uint32_t) d[1] << 8) | d[2]; };
    o.name.assign ((const char*) t, 8);
    o.f1 = t[8]; o.sourceTone = t[9]; o.subTone = t[10] != 0; o.khz15 = t[11] != 0; o.origKey = t[12];
    o.waveBank = t[13]; o.waveStart = t[14]; o.waveLen = t[15];
    o.startPoint = r24 (t + 16); o.endPoint = r24 (t + 19); o.loopPoint = r24 (t + 22); o.loopMode = t[25];
    std::copy (t + 26, t + 128, o.rest);
}
Bytes patchToSysex (const data::Patch& p) { return toNibbles (p.raw, data::kPatchSize); }
void sysexToPatch (const uint8_t* p, data::Patch& o) { const Bytes t = fromNibbles (p, 512); std::copy (t.begin(), t.begin() + data::kPatchSize, o.raw); }

Bytes waveToSysex (const int16_t* s, size_t n)
{
    Bytes o; o.reserve (n * 2);
    for (size_t i = 0; i < n; ++i) { const unsigned v = (unsigned) s[i] & 0xfffu; o.push_back ((uint8_t) (v >> 5)); o.push_back ((uint8_t) ((v & 31u) << 2)); }
    return o;
}
void sysexToWave (const uint8_t* p, int16_t* s, size_t n)
{
    for (size_t i = 0; i < n; ++i) { const unsigned v = ((unsigned) (p[2 * i] & 127) << 5) | ((p[2 * i + 1] & 127) >> 2); s[i] = (int16_t) (v & 0x800u ? (int) v - 4096 : (int) v); }
}

std::vector<Chunk> planBlock (const data::Block& b, const PlanOptions& o)
{
    std::vector<Chunk> c;
    if (o.patches)
        for (int i = 0; i < data::kNumPatches; ++i) c.push_back ({ patchAddr (o.patchOffset + i), patchToSysex (b.patches[i]), "Patch " + std::to_string (i + 1) });
    if (o.tones)
        for (int i = 0; i < data::kNumTones; ++i) c.push_back ({ toneAddr (o.toneOffset + i), toneToSysex (b.tones[i]), "Tone " + std::to_string (i + 1) });
    if (o.waves)
        for (int bank = 0; bank < 2; ++bank)
            for (int s = 0; s < data::kSegments; ++s)
                c.push_back ({ waveAddr (bank + o.waveBankOffset, s), waveToSysex (&b.wave[bank][(size_t) s * data::kSegWords], data::kSegWords), std::string ("Wave ") + (bank ? "B" : "A") + std::to_string (s + 1) });
    return c;
}

// ---------------- Session ----------------
const char* Session::cmdName (uint8_t c)
{
    switch (c) { case RQ1: return "RQ1"; case DT1: return "DT1"; case WSD: return "WSD"; case RQD: return "RQD"; case DAT: return "DAT"; case ACK: return "ACK"; case EOD: return "EOD"; case ERR: return "ERR"; case RJC: return "RJC"; default: return "???"; }
}
void Session::note (const char* dir, const uint8_t* p, size_t n)
{
    static const char* hx = "0123456789ABCDEF";
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
    std::string s;
    { std::lock_guard<std::mutex> l (lg_); if (! t0_) t0_ = now; s = std::to_string (now - t0_) + " ms " + dir + " "; }
    s += n > 4 ? cmdName (p[4]) : "?";
    s += " (" + std::to_string (n) + " bytes) ";
    for (size_t i = 0; i < n && i < 22; ++i) { s += hx[p[i] >> 4]; s += hx[p[i] & 15]; s += ' '; }
    if (n > 22) s += "...";
    std::lock_guard<std::mutex> l (lg_);
    log_.push_back (s);
    if (log_.size() > 400) log_.erase (log_.begin(), log_.begin() + 100);
}
std::string Session::logText()
{
    std::lock_guard<std::mutex> l (lg_);
    std::string r = "S-550 SysEx log (Device ID " + std::to_string (deviceId + 1) + ", " + (handshake ? "handshake" : "one way") + ")\n";
    for (auto& s : log_) r += s + "\n";
    return r;
}
void Session::tx (const Bytes& m) { note ("TX", m.data(), m.size()); if (send) send (m.data(), m.size()); }

void Session::sleep (int ms) { if (sleepFn) sleepFn (ms); else std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

void Session::receive (const uint8_t* p, size_t n)
{
    Message m;
    note ("RX", p, n);
    if (! parse (p, n, m) || m.dev != (deviceId & 15)) return;
    if (m.cmd == DT1 || m.cmd == DAT)
    {
        { std::lock_guard<std::mutex> l (m_); got_[m.addr] = m.data; gotBytes_ += (uint32_t) m.data.size(); if (counting_) doneBytes += (long long) m.data.size(); }
        if (onData) onData (m.addr, m.data);
        if (m.cmd == DAT) tx (buildSimple (deviceId, ACK));
        cv_.notify_all();
    }
    else if (m.cmd == ACK || m.cmd == RJC || m.cmd == ERR) { { std::lock_guard<std::mutex> l (m_); replies_.push_back (m.cmd); } cv_.notify_all(); }
    else if (m.cmd == EOD) { { std::lock_guard<std::mutex> l (m_); eod_ = true; } cv_.notify_all(); }
}

bool Session::waitReply (uint8_t& cmd, int ms)
{
    std::unique_lock<std::mutex> l (m_);
    if (! cv_.wait_for (l, std::chrono::milliseconds (ms), [&] { return ! replies_.empty() || abort_; }) || replies_.empty()) return false;
    cmd = replies_.front(); replies_.erase (replies_.begin());
    return true;
}

bool Session::sendChunks (const std::vector<Chunk>& in, std::string& err)
{
    if (! send) { err = "No MIDI output"; return false; }
    abort_ = false;
    std::vector<Chunk> chunks;
    for (auto& c : in)                                    // handshake: optionally one WSD for neighbouring chunks
    {
        if (handshake && mergeRuns && ! chunks.empty() && chunks.back().addr + chunks.back().data.size() == c.addr)
        { chunks.back().data.insert (chunks.back().data.end(), c.data.begin(), c.data.end()); chunks.back().what += " - " + c.what; }
        else chunks.push_back (c);
    }
    int total = 0, done = 0;
    for (auto& c : chunks) total += (int) ((c.data.size() + (handshake ? 128 : kMaxData) - 1) / (handshake ? 128 : kMaxData));
    auto why = [] (uint8_t r) -> std::string { return r == RJC ? "RJC (S-550 refuses address/size or is not ready)" : r == ERR ? "ERR (checksum error)" : "unexpected answer"; };
    for (auto& c : chunks)
    {
        bool ok = false; std::string last;
        for (int attempt = 0; attempt < (handshake ? 2 : 1) && ! ok; ++attempt)
        {
            if (attempt) { sleep (400); last += " - retry: "; }
            { std::lock_guard<std::mutex> l (m_); replies_.clear(); }
            bool fail = false;
            if (handshake)
            {
                tx (buildWSD (deviceId, c.addr, (uint32_t) c.data.size()));
                uint8_t r = 0;
                if (! waitReply (r, ackTimeoutMs)) { last += "no answer to WSD at " + c.what; fail = true; }
                else if (r != ACK) { last += why (r) + " to WSD at " + c.what; fail = true; }
            }
            const size_t pk = handshake ? 128 : kMaxData;
            for (size_t off = 0; ! fail && off < c.data.size(); off += pk)
            {
                if (abort_) { err = "Aborted"; return false; }
                const size_t n = std::min<size_t> (pk, c.data.size() - off);
                tx (handshake ? buildDAT (deviceId, c.addr + (uint32_t) off, &c.data[off], n) : buildDT1 (deviceId, c.addr + (uint32_t) off, &c.data[off], n));
                if (handshake)
                {
                    uint8_t r = 0;
                    if (! waitReply (r, ackTimeoutMs)) { last += "no ACK at " + c.what; fail = true; }
                    else if (r != ACK) { last += why (r) + " at " + c.what; fail = true; }
                }
                else sleep (gapMs);
                if (! fail && progress) progress (++done, total, c.what);
            }
            if (handshake)
            {
                tx (buildSimple (deviceId, EOD));
                sleep (std::max (gapMs, 60));              // the S-550 stores the data now; late ACKs are dropped before the next WSD
            }
            ok = ! fail;
            if (fail && handshake) { tx (buildSimple (deviceId, EOD)); sleep (150); }
        }
        if (! ok) { err = last; return false; }
    }
    return true;
}

bool Session::request (uint32_t addr, uint32_t size, Bytes& out, std::string& err)
{
    if (! send) { err = "No MIDI output"; return false; }
    abort_ = false;
    const uint8_t order[2] = { (uint8_t) (handshake ? RQD : RQ1), (uint8_t) (handshake ? RQ1 : RQD) };
    std::string last;
    for (int t = 0; t < 2; ++t)
    {
        { std::lock_guard<std::mutex> l (m_); got_.clear(); gotBytes_ = 0; eod_ = false; replies_.clear(); }
        tx (order[t] == RQ1 ? buildRQ1 (deviceId, addr, size) : buildRQD (deviceId, addr, size));
        uint8_t rej = 0;
        bool timeout = false;
        {
            // the S-550 answers with many messages (RQD: DAT of 128 bytes each, RQ1: DT1): wait while data keeps coming
            std::unique_lock<std::mutex> l (m_);
            uint32_t seen = 0;
            while (gotBytes_ < size && ! abort_ && replies_.empty())
            {
                const int wait = seen == 0 ? ackTimeoutMs * 2 : ackTimeoutMs;
                seen = gotBytes_;
                if (! cv_.wait_for (l, std::chrono::milliseconds (wait), [&] { return gotBytes_ != seen || abort_ || ! replies_.empty(); })) { timeout = true; break; }
            }
            if (gotBytes_ >= size)
            {
                if (order[t] == RQD)                   // let the S-550 finish: it ends with EOD, a new request before that is refused (RJC)
                    cv_.wait_for (l, std::chrono::milliseconds (600), [&] { return eod_ || abort_; });
                out.assign (size, 0);
                for (auto& kv : got_) if (kv.first >= addr && kv.first - addr < size) std::copy (kv.second.begin(), kv.second.begin() + std::min<size_t> (kv.second.size(), size - (kv.first - addr)), out.begin() + (kv.first - addr));
                return true;
            }
            if (! replies_.empty()) rej = replies_.front();
            if (gotBytes_ > 0 && ! abort_) { err = std::string (order[t] == RQ1 ? "RQ1" : "RQD") + ": transfer stopped after " + std::to_string (gotBytes_) + " of " + std::to_string (size) + " bytes" + (rej == RJC ? " (RJC)" : timeout ? " (timeout)" : ""); return false; }
        }
        if (abort_) { err = "Aborted"; return false; }
        const std::string nm = order[t] == RQ1 ? "RQ1" : "RQD";
        last += nm + ": " + (rej == RJC ? "rejected (RJC)" : rej == ERR ? "ERR" : rej ? "unexpected answer" : "no answer (cable, Device ID, Exclusive On?)") + (t == 0 ? "; " : "");
        sleep (300);
    }
    err = last;
    return false;
}

bool Session::requestBlock (data::Block& b, const PlanOptions& o, std::string& err)
{
    Bytes d;
    int step = 0;
    long long tot = 0;
    if (o.patches) tot += (long long) data::kNumPatches * kPatchSize;
    if (o.tones)   tot += (long long) data::kNumTones * kToneSize;
    if (o.waves)   tot += 2LL * data::kSegments * kWaveSegSize;
    doneBytes = 0; totalBytes = tot; counting_ = true;
    struct Off { std::atomic<bool>& f; ~Off() { f = false; } } off { counting_ };
    auto prog = [&] (const std::string& w) { if (progress) progress (++step, 0, w); };
    if (o.patches) for (int i = 0; i < data::kNumPatches; ++i) { if (! request (patchAddr (o.patchOffset + i), kPatchSize, d, err)) return false; sysexToPatch (d.data(), b.patches[i]); prog ("Patch " + std::to_string (i + 1)); }
    if (o.tones)   for (int i = 0; i < data::kNumTones; ++i)   { if (! request (toneAddr (o.toneOffset + i), kToneSize, d, err)) return false; sysexToTone (d.data(), b.tones[i]); prog ("Tone " + std::to_string (i + 1)); }
    if (o.waves)
        for (int bank = 0; bank < 2; ++bank)
            for (int s = 0; s < data::kSegments; ++s)
            { if (! request (waveAddr (bank + o.waveBankOffset, s), kWaveSegSize, d, err)) return false; sysexToWave (d.data(), &b.wave[bank][(size_t) s * data::kSegWords], data::kSegWords); prog (std::string ("Wave ") + (bank ? "B" : "A") + std::to_string (s + 1)); }
    return true;
}

void Session::setParam (uint32_t base, int index, uint8_t v)
{
    if (! send) return;
    const Bytes d = { (uint8_t) (v >> 4), (uint8_t) (v & 15) };
    const Bytes m = buildDT1 (deviceId, base + (uint32_t) index * 2, d.data(), 2);
    tx (m);
}
}
