#include "Block.h"
#include <algorithm>
#include <cstring>

namespace s5::data
{
static std::string pad (std::string s, size_t n) { s.resize (n, ' '); return s; }

std::string Patch::name() const { return std::string ((const char*) raw, 12); }
void Patch::setName (const std::string& n) { const std::string p = pad (n, 12); std::memcpy (raw, p.data(), 12); }

Block::Block()
{
    image.assign ((size_t) kImageSize, 0);
    std::memcpy (&image[4], "S550", 4);
    wave[0].assign ((size_t) kWaveWords, 0);
    wave[1].assign ((size_t) kWaveWords, 0);
    for (auto& p : patches) p.setName ("");
    label = pad ("", kLabelSize);
}

void packWave (const std::vector<int16_t>& s, uint8_t* dst)
{
    for (size_t m = 0; m + 1 < s.size(); m += 2)
    {
        const unsigned a = (unsigned) s[m] & 0xfffu, b = (unsigned) s[m + 1] & 0xfffu;
        uint8_t* d = dst + (m / 2) * 3;
        d[0] = (uint8_t) (a >> 4);
        d[1] = (uint8_t) (((a & 15u) << 4) | (b & 15u));
        d[2] = (uint8_t) (b >> 4);
    }
}

void unpackWave (const uint8_t* src, std::vector<int16_t>& s)
{
    for (size_t m = 0; m + 1 < s.size(); m += 2)
    {
        const uint8_t* d = src + (m / 2) * 3;
        unsigned a = ((unsigned) d[0] << 4) | (d[1] >> 4), b = ((unsigned) d[2] << 4) | (d[1] & 15u);
        s[m]     = (int16_t) (a & 0x800u ? (int) a - 4096 : (int) a);
        s[m + 1] = (int16_t) (b & 0x800u ? (int) b - 4096 : (int) b);
    }
}

static uint32_t rd24 (const uint8_t* p) { return ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2]; }
static void wr24 (uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 16); p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) v; }

Block::Result Block::parse (const uint8_t* img, size_t size)
{
    if (size != (size_t) kImageSize) return Result::BadSize;
    if (img[0] || img[1] || img[2] || img[3] || (std::memcmp (img + 4, "S550", 4) != 0 && std::memcmp (img + 4, "S-50", 4) != 0)) return Result::NotS550;      // S-50 disks use the same layout
    image.assign (img, img + size);
    s50 = std::memcmp (img + 4, "S-50", 4) == 0;
    label = std::string ((const char*) img + kOffLabel, kLabelSize);
    for (int i = 0; i < kNumPatches; ++i) std::memcpy (patches[i].raw, img + kOffPatch + i * kPatchSize, kPatchSize);
    if (s50)                                // S-50 patch: 512 bytes = name, bend range ..., key map of 109 keys at 0xb4
        for (int i = 0; i < kNumPatches; ++i)
        {
            Patch& p = patches[i];
            std::memset (p.raw, 0, kPatchSize);
            if (i >= 8) { p.setName (""); p.raw[237] = 127; continue; }
            const uint8_t* s = img + kOffPatch + i * 512;
            std::memcpy (p.raw, s, 16);
            std::memcpy (p.raw + 17, s + 0xb4, 109);
            p.raw[237] = 127;
        }
    for (int i = 0; i < kNumTones; ++i)
    {
        const uint8_t* t = img + kOffTone + i * kToneSize;
        Tone& o = tones[i];
        o.name = std::string ((const char*) t, 8);
        o.f1 = t[8]; o.sourceTone = t[9]; o.subTone = t[10] != 0; o.khz15 = t[11] != 0; o.origKey = t[12];
        o.waveBank = t[13]; o.waveStart = t[14]; o.waveLen = t[15];
        o.startPoint = rd24 (t + 16); o.endPoint = rd24 (t + 19); o.loopPoint = rd24 (t + 22); o.loopMode = t[25];
        std::memcpy (o.rest, t + 26, 102);
    }
    unpackWave (img + kOffWaveA, wave[0]);
    unpackWave (img + kOffWaveB, wave[1]);
    return Result::Ok;
}

std::vector<uint8_t> Block::serialize() const
{
    std::vector<uint8_t> out = image;
    std::memcpy (&out[kOffLabel], pad (label, kLabelSize).data(), kLabelSize);
    if (s50)
        for (int i = 0; i < 8; ++i)
        {
            uint8_t* s = &out[(size_t) (kOffPatch + i * 512)];
            std::memcpy (s, patches[i].raw, 16);
            std::memcpy (s + 0xb4, patches[i].raw + 17, 109);
        }
    else
        for (int i = 0; i < kNumPatches; ++i) std::memcpy (&out[(size_t) (kOffPatch + i * kPatchSize)], patches[i].raw, kPatchSize);
    for (int i = 0; i < kNumTones; ++i)
    {
        uint8_t* t = &out[(size_t) (kOffTone + i * kToneSize)];
        const Tone& o = tones[i];
        std::memcpy (t, pad (o.name, 8).data(), 8);
        t[8] = o.f1; t[9] = o.sourceTone; t[10] = o.subTone ? 1 : 0; t[11] = o.khz15 ? 1 : 0; t[12] = o.origKey;
        t[13] = o.waveBank; t[14] = o.waveStart; t[15] = o.waveLen;
        wr24 (t + 16, o.startPoint); wr24 (t + 19, o.endPoint); wr24 (t + 22, o.loopPoint); t[25] = o.loopMode;
        std::memcpy (t + 26, o.rest, 102);
    }
    packWave (wave[0], &out[kOffWaveA]);
    packWave (wave[1], &out[kOffWaveB]);
    return out;
}

Block::Performance Block::performance() const
{
    Performance p;
    if (image.size() < 0x10c40) return p;
    const uint8_t* d = &image[0x10c00];
    if (d[0x10] < 24) p.voiceMode = d[0x10];
    for (int i = 0; i < 8; ++i)
    {
        p.ch[i] = d[0x11 + i] < 16 ? d[0x11 + i] + 1 : 0;           // 0x10 = part switched off
        p.patch[i] = d[0x19 + i] < 16 ? d[0x19 + i] : 0;
        p.level[i] = d[0x2b + i] < 128 ? d[0x2b + i] : 127;
    }
    return p;
}

std::string Block::systemLine() const
{
    std::string r;
    if (image.size() > 0x40)
        for (int i = 0x20; i < 0x3f && image[(size_t) i] != 0xfe; ++i) r += (image[(size_t) i] >= 32 && image[(size_t) i] < 127) ? (char) image[(size_t) i] : ' ';
    while (! r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

int Block::usedSegments (int bank) const
{
    bool used[kSegments] = {};
    for (const auto& t : tones)
        if (t.used() && t.waveBank == bank && ! t.subTone)
            for (int s = t.waveStart; s < t.waveStart + t.waveLen && s < kSegments; ++s) used[s] = true;
    return (int) std::count (used, used + kSegments, true);
}
}
