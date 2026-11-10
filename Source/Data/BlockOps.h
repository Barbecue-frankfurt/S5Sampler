#pragma once
// Moving tones and patches between two blocks (HD Load T / Load Patch, HD5-IF manual p.28-32)
#include "Block.h"
#include <string>
#include <vector>

namespace s5::data
{
// free wave segments of a bank (originals only own wave data)
int freeSegmentsOf (const Block& b, int bank);
double secondsFree (const Block& b, int bank);                 // 30 kHz: 0.4 s per segment
// Load Tone: tone `st` of `src` into tone `dt` of `dst`, wave data into `bank`. Empty string = ok.
std::string loadTone (const Block& src, int st, Block& dst, int dt, int bank);
// Load Patch: patch parameters and all tones the patch uses. Empty string = ok, else the message ("Memory full").
std::string loadPatch (const Block& src, int sp, Block& dst, int dp, std::vector<int>* notLoaded = nullptr);
void deletePatch (Block& b, int p);
// Sampling: a recording (12 bit values) becomes tone `tone` in `bank`. Empty string = ok, else the message.
std::string storeSample (Block& b, int tone, int bank, const std::vector<int16_t>& samples, bool khz15, int origKey, const std::string& name);
// Wave processing (D.Filter, DC-Cut): the wave of tone `st` of `src` is processed and written to tone `dt` of `dst` (bank `bank`),
// the tone parameters of the source are copied. Sub tones cannot be processed. Empty string = ok.
struct FilterPrm { bool hpf = false; double freqKHz = 1.0; int resonance = 0, level = 127; };
std::vector<int16_t> filterWave (const std::vector<int16_t>& in, double sampleRate, const FilterPrm& p);
std::vector<int16_t> dcCut (const std::vector<int16_t>& in);
std::string storeProcessed (const Block& src, int st, Block& dst, int dt, int bank, bool dc, const FilterPrm& p);

// Wave Loop: the source tone's parameters (Loop Mode = Forward) with a processed wave and the given points go to the destination
std::string storeLooped (const Block& src, int st, Block& dst, int dt, int bank, const std::vector<int16_t>& wave, uint32_t startPt, uint32_t loopPt, uint32_t endPt);
bool smoothLoop (std::vector<int16_t>& w, int startPt, int loopPt, int endPt);                              // false: less than 228 addresses Start -> Loop
bool autoLoop (const std::vector<int16_t>& w, int loopPt, int endPt, bool fromLoop, int& newLoop, int& newEnd);  // searches a Forward loop between Loop and End
// text of a tone line: wave bank and length ("A-1.6")
std::string toneInfo (const Tone& t);
}
