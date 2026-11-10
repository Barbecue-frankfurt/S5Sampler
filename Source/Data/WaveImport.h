#pragma once
// Load Wave: an audio file (already decoded to mono floats) becomes a tone with its wave in a bank of a block.
// 12 bit samples, 30 kHz or 15 kHz, 12288 samples per memory segment, 18 segments per bank.
#include "Block.h"
#include <string>
#include <vector>

namespace s5::data
{
struct WaveImportOptions
{
    int tone = 0;                 // 0-31 in the block
    int bank = 0;                 // 0 = A, 1 = B
    bool khz15 = false;
    bool normalize = true;
    std::string name;             // tone name (file name), cut to 8 characters
    int origKey = 60;
};
struct WaveImportResult { int segStart = 0, segLen = 0, samples = 0; bool truncated = false; };

std::vector<float> resampleMono (const std::vector<float>& in, double rateIn, double rateOut);        // band limited (windowed sinc)
void setPlayableDefaults (Tone& t);                                                                    // filter open, envelopes 0 -> 127, sustain, release
bool importWave (Block& b, const WaveImportOptions& o, const std::vector<float>& mono, double rate, WaveImportResult& res, std::string& err);
}
