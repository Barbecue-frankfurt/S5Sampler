// Loopback test: a simulated S-550 memory answers RQ1 / accepts DT1 and WSD/DAT.
#include "Source/Midi/SysEx.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace s5; using namespace s5::sysex;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL %d: %s\n", __LINE__, #c); ++fails; } } while (0)

struct FakeS550
{
    std::map<uint32_t, uint8_t> mem; Session* host = nullptr; int dev = 0; bool rejectRq1 = false; int wsd = 0;
    void onMidi (const uint8_t* p, size_t n)
    {
        Message m; if (! parse (p, n, m) || m.dev != dev) return;
        if (m.cmd == DT1 || m.cmd == DAT) { for (size_t i = 0; i < m.data.size(); ++i) mem[m.addr + (uint32_t) i] = m.data[i]; if (m.cmd == DAT) { auto a = buildSimple (dev, ACK); host->receive (a.data(), a.size()); } }
        else if (m.cmd == WSD) { ++wsd; auto a = buildSimple (dev, ACK); host->receive (a.data(), a.size()); }
        else if (m.cmd == RQ1 && rejectRq1) { auto a = buildSimple (dev, RJC); host->receive (a.data(), a.size()); }
        else if (m.cmd == RQ1 || m.cmd == RQD)
        {
            const uint32_t st = m.cmd == RQD ? 128 : 256;
            for (uint32_t off = 0; off < m.size; off += st)
            {
                Bytes d; for (uint32_t i = off; i < std::min (m.size, off + st); ++i) d.push_back (mem[m.addr + i]);
                auto r = m.cmd == RQD ? buildDAT (dev, m.addr + off, d.data(), d.size()) : buildDT1 (dev, m.addr + off, d.data(), d.size()); host->receive (r.data(), r.size());
            }
        if (m.cmd == RQD) { auto e = buildSimple (dev, EOD); host->receive (e.data(), e.size()); }
        }
    }
};

int main()
{
    // address / checksum from the manual: 00 03 00 00 = tone 1, 00 03 02 00 -> tone 3? (256 bytes = 2*128)
    CHECK (encodeAddr (toneAddr (0)) == (Bytes { 0, 3, 0, 0 }));
    CHECK (encodeAddr (toneAddr (1)) == (Bytes { 0, 3, 2, 0 }));
    CHECK (encodeAddr (toneAddr (64)) == (Bytes { 0, 4, 0, 0 }));
    CHECK (encodeAddr (patchAddr (32)) == (Bytes { 0, 1, 0, 0 }));
    CHECK (encodeAddr (waveAddr (0, 0)) == (Bytes { 1, 0, 0, 0 }));
    CHECK (encodeAddr (waveAddr (1, 0)) == (Bytes { 1, 0x20, 0, 0 }));
    CHECK (encodeAddr (waveAddr (0, 1)) == (Bytes { 1, 1, 0x40, 0 }));
    CHECK (encodeAddr (waveAddr (0, 18)) == encodeAddr (0x200000 + 18 * 24576));
    CHECK (decodeAddr (encodeAddr (0x123456).data()) == 0x123456);
    { const uint8_t d[] = { 0x41, 0x1e, 0x12 }; (void) d; }
    Bytes m = buildRQ1 (2, 0x00c000, 256);
    CHECK (m.size() == 15 && m[0] == 0xf0 && m[1] == 0x41 && m[2] == 2 && m[3] == 0x1e && m[4] == 0x11 && m.back() == 0xf7);
    { unsigned s = 0; for (size_t i = 5; i < m.size() - 1; ++i) s += m[i]; CHECK ((s & 127) == 0); }
    Message pm; CHECK (parse (m.data(), m.size(), pm) && pm.cmd == RQ1 && pm.addr == 0xc000 && pm.size == 256 && pm.dev == 2);
    m[13] ^= 1; CHECK (! parse (m.data(), m.size(), pm));

    // wave coding
    int16_t w[4] = { 0, -1, 2047, -2048 }, r[4];
    Bytes wb = waveToSysex (w, 4); CHECK (wb.size() == 8); for (uint8_t b : wb) CHECK (b < 128);
    sysexToWave (wb.data(), r, 4); for (int i = 0; i < 4; ++i) CHECK (r[i] == w[i]);

    // block round trip, one way and handshake
    data::Block b;
    for (int i = 0; i < 32; ++i) { b.tones[i].name = "TONE" + std::to_string (10 + i) + "  "; b.tones[i].waveLen = (uint8_t) (i % 3); b.tones[i].startPoint = 1000u * i; b.tones[i].rest[5] = (uint8_t) (i * 7); }
    for (int i = 0; i < 16; ++i) { b.patches[i].setName ("PATCH" + std::to_string (i)); b.patches[i].raw[100] = (uint8_t) (200 + i); }
    for (int k = 0; k < 2; ++k) for (size_t i = 0; i < b.wave[k].size(); ++i) b.wave[k][i] = (int16_t) ((int) ((i * 31 + k * 7) % 4096) - 2048);
    for (int hs = 0; hs < 2; ++hs)
    {
        FakeS550 dev; Session s; dev.host = &s; s.handshake = hs; s.sleepFn = [] (int) {};
        s.send = [&] (const uint8_t* p, size_t n) { for (size_t i = 0; i < n; ++i) CHECK (i == 0 ? p[0] == 0xf0 : i == n - 1 ? p[i] == 0xf7 : p[i] < 128); CHECK (n <= 5 + 4 + 256 + 2); dev.onMidi (p, n); };
        std::string err; PlanOptions o; o.toneOffset = 32; o.patchOffset = 16;
        int last = 0; s.progress = [&] (int d, int, const std::string&) { last = d; };
        CHECK (s.sendBlock (b, o, err));
        CHECK (last > 3000);
        data::Block c; CHECK (s.requestBlock (c, o, err));
        CHECK (c.tones[5].name == b.tones[5].name && c.tones[31].startPoint == 31000u && c.tones[7].rest[5] == 49);
        CHECK (c.patches[9].raw[100] == 209 && c.patches[3].name() == b.patches[3].name());
        CHECK (c.wave[0] == b.wave[0] && c.wave[1] == b.wave[1]);
        CHECK (dev.mem.count (toneAddr (32)) == 1 && dev.mem.count (toneAddr (0)) == 0);
    }
    // handshake request when RQ1 is refused (RJC): falls back to RQD, merged WSD runs
    { FakeS550 dev; Session s; dev.host = &s; dev.rejectRq1 = true; s.sleepFn = [] (int) {}; s.handshake = false;
      s.send = [&] (const uint8_t* p, size_t n) { dev.onMidi (p, n); };
      Bytes d; std::string e; Bytes tone (256, 5); dev.mem[toneAddr (1)] = 7;
      CHECK (s.request (toneAddr (1), 256, d, e) && d[0] == 7 && ! s.logText().empty());
      s.handshake = true; s.mergeRuns = true; dev.wsd = 0; std::string e2;
      CHECK (s.sendBlock (b, PlanOptions { true, false, false, false, 0, 0 }, e2) && dev.wsd == 1); }
    // live parameter
    { Session s; Bytes got; s.send = [&] (const uint8_t* p, size_t n) { got.assign (p, p + n); }; s.setToneParam (3, 0x4c / 2, 99);
      Message q; CHECK (parse (got.data(), got.size(), q) && q.addr == toneAddr (3) + 0x4c && q.data == (Bytes { 6, 3 })); }
    // no answer -> clean error
    { Session s; s.ackTimeoutMs = 30; s.send = [] (const uint8_t*, size_t) {}; Bytes d; std::string e; CHECK (! s.request (0, 8, d, e) && ! e.empty()); }
    std::printf (fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
}
