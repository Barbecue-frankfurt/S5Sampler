// Disk image: sizes, offsets, 12 bit packing and a full round trip.
#include "../Source/Data/Block.h"
#include <cstdio>
#include <cstring>
using namespace s5::data;

int main()
{
    int fails = 0;
    auto expect = [&] (bool c, const char* w) { if (! c) { std::printf ("FAIL: %s\n", w); ++fails; } };

    expect (kOffWaveB + kSegments * kSegBytes == kImageSize - 0 + 0 || kOffWaveB + kSegments * kSegBytes == kImageSize, "wave B ends at the end of the image");
    expect (kWaveWords == 221184, "221184 samples per bank");
    expect (kSegBytes * 2 / 3 == 12288, "segment = 12288 words");

    Block b;
    b.label = "My Bank";
    b.patches[0].setName ("Piano");
    b.patches[15].setName ("Last");
    Tone& t = b.tones[3];
    t.name = "Cowbell"; t.sourceTone = 3; t.khz15 = true; t.origKey = 60; t.waveBank = 1; t.waveStart = 2; t.waveLen = 5;
    t.startPoint = 123; t.endPoint = 0x012345; t.loopPoint = 0xABCDE; t.loopMode = 2; t.rest[0] = 7; t.rest[101] = 9;
    for (size_t i = 0; i < b.wave[1].size(); ++i) b.wave[1][i] = (int16_t) (((int) (i * 37) % 4096) - 2048);
    for (size_t i = 0; i < b.wave[0].size(); ++i) b.wave[0][i] = (int16_t) (i % 2 ? -2048 : 2047);

    const auto img = b.serialize();
    expect (img.size() == (size_t) kImageSize, "image has 737280 bytes");
    expect (img[0] == 0 && img[3] == 0 && std::memcmp (&img[4], "S550", 4) == 0, "S550 header");
    expect (std::memcmp (&img[kOffLabel], "My Bank     ", 12) == 0, "label at its offset");
    expect (std::memcmp (&img[kOffTone + 3 * kToneSize], "Cowbell ", 8) == 0, "tone name at its offset");
    expect (img[kOffTone + 3 * kToneSize + 11] == 1, "15 kHz flag");
    expect (img[kOffTone + 3 * kToneSize + 19] == 0x01 && img[kOffTone + 3 * kToneSize + 20] == 0x23 && img[kOffTone + 3 * kToneSize + 21] == 0x45, "end point 24 bit big endian");
    expect (std::memcmp (&img[kOffPatch], "Piano       ", 12) == 0, "patch 1 name");

    Block c;
    expect (c.parse (img.data(), img.size()) == Block::Result::Ok, "parse ok");
    expect (c.label == "My Bank     ", "label");
    expect (c.patches[0].name() == "Piano       " && c.patches[15].name() == "Last        ", "patch names");
    const Tone& u = c.tones[3];
    expect (u.name == "Cowbell " && u.khz15 && u.origKey == 60 && u.waveBank == 1 && u.waveStart == 2 && u.waveLen == 5, "tone fields");
    expect (u.startPoint == 123 && u.endPoint == 0x012345 && u.loopPoint == 0xABCDE && u.loopMode == 2, "tone points");
    expect (u.rest[0] == 7 && u.rest[101] == 9, "unknown tone bytes kept");
    expect (c.wave[1] == b.wave[1] && c.wave[0] == b.wave[0], "wave data round trip");
    expect (c.serialize() == img, "byte identical after load and save");
    expect (c.usedSegments (1) == 5 && c.usedSegments (0) == 0 && c.freeSegments (1) == 13, "segment usage");

    std::vector<uint8_t> bad (1000);
    expect (Block().parse (bad.data(), bad.size()) == Block::Result::BadSize, "wrong size rejected");
    std::vector<uint8_t> other ((size_t) kImageSize); std::memcpy (&other[4], "XXXX", 4);
    expect (Block().parse (other.data(), other.size()) == Block::Result::NotS550, "foreign image rejected");
    std::memcpy (&other[4], "S-50", 4);
    Block s50;
    expect (s50.parse (other.data(), other.size()) == Block::Result::Ok && s50.s50, "S-50 image accepted");
    expect (s50.serialize() == other, "S-50 image: byte identical after load and save");

    std::printf ("%s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
