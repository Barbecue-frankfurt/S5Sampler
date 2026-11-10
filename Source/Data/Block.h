#pragma once
// S-550 data block (one disk): 16 patches, 32 tones, two wave banks, disk label.
// No JUCE dependency.  The disk image layout (offsets, tone entry fields, 12-bit wave packing)
// follows the publicly documented S-50/S-330/S-550 disk format; bytes not understood yet are kept
// as they are, so a loaded image can be saved again without losing them.
#include <cstdint>
#include <string>
#include <vector>

namespace s5::data
{
constexpr int kImageSize   = 737280;       // 80 tracks x 2 sides x 9 sectors x 512 bytes
constexpr int kNumTones    = 32;
constexpr int kNumPatches  = 16;
constexpr int kSegBytes    = 18432;
constexpr int kSegWords    = 12288;        // 12 bit samples per segment
constexpr int kSegments    = 18;           // per wave bank
constexpr int kWaveWords   = kSegments * kSegWords;   // 221184 samples per bank

constexpr int kOffPatch = 0xfc00, kOffLabel = 0x10c33, kOffTone = 0x10e00, kOffWaveA = 0x12000;
constexpr int kOffWaveB = kOffWaveA + kSegments * kSegBytes;
constexpr int kToneSize = 128, kPatchSize = 256, kLabelSize = 12;

struct Tone
{
    std::string name = "        ";          // 8 characters
    uint8_t f1 = 0;
    uint8_t sourceTone = 0;                 // 0-31
    bool subTone = false;
    bool khz15 = false;                     // false = 30 kHz
    uint8_t origKey = 0;                    // 11-120
    uint8_t waveBank = 0;                   // 0 = A, 1 = B
    uint8_t waveStart = 0, waveLen = 0;     // memory units (segments) 0-17 / 0-18
    uint32_t startPoint = 0, endPoint = 0, loopPoint = 0;
    uint8_t loopMode = 0;                   // 0 Fwd, 1 Alt, 2 1Shot, 3 Reverse
    uint8_t rest[102] = {};                 // parameters not mapped yet (kept)
    bool used() const { return waveLen > 0; }
    // byte of the 128 byte tone record (26-127 only: the rest block)
    uint8_t& at (int off) { return rest[off - 26]; }
    const uint8_t& at (int off) const { return rest[off - 26]; }
};

struct Patch
{
    uint8_t raw[kPatchSize] = {};           // first 12 bytes: name; the rest is kept as read
    std::string name() const;
    void setName (const std::string&);
};

struct Block
{
    Block();
    std::string label = "            ";    // 12 characters
    Tone tones[kNumTones];
    Patch patches[kNumPatches];
    std::vector<int16_t> wave[2];           // signed 12 bit samples, kWaveWords each
    bool s50 = false;                       // loaded from an S-50 disk: 8 patches of 512 bytes (converted to the 16 x 256 byte model, written back in the S-50 layout)

    enum class Result { Ok, BadSize, NotS550 };
    Result parse (const uint8_t* img, size_t size);
    std::vector<uint8_t> serialize() const;

    struct Performance { int voiceMode = 12; int ch[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; int patch[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }; int level[8] = { 127, 127, 127, 127, 127, 127, 127, 127 }; };
    Performance performance() const;                     // PLAY part table stored on the disk (voice mode, MIDI channel 0 = off, patch, level of parts A-H)
    std::string systemLine() const;                      // "S-550 System   Ver. 1.13" from the system area (line 1)
    int usedSegments (int bank) const;      // segments taken by tones
    int freeSegments (int bank) const { return kSegments - usedSegments (bank); }

private:
    std::vector<uint8_t> image;             // everything else on the disk (system area, unknown bytes)
};

// 12 bit packing: 2 samples in 3 bytes
void packWave (const std::vector<int16_t>& s, uint8_t* dst);
void unpackWave (const uint8_t* src, std::vector<int16_t>& s);
}
