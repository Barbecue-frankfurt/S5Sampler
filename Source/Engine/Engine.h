#pragma once
// S-550 sound engine: plays the tones of a disk image.  No JUCE dependency: the plugin only calls midi() and render().
//   patch (per part, keys 12-120)  ->  1st / 2nd tone  ->  wave of the tone (12 bit, 30 or 15 kHz)
//   wave: start / loop / end point, loop mode (Forward, Alter, 1 Shot, Reverse)
//   TVF: resonant low-pass, cut-off + key follow + 8 point envelope;  TVA: 8 point envelope, velocity
//   voices: 16, shared as the Voice Mode of the PLAY page says (VAL / VAF / V1..V22 groups A-H)
// Bytes of the tone record that are not mapped on the screen yet (LFO depth, tone level, output assign ...) use neutral values.
#include "../Data/Block.h"
#include <atomic>
#include <memory>
#include <vector>

namespace s5::engine
{
struct Snapshot
{
    // Block I = indices 0-31 / 0-15 / waves 0-1, Block II = 32-63 / 16-31 / waves 2-3
    data::Tone tones[2 * data::kNumTones];
    data::Patch patches[2 * data::kNumPatches];
    std::shared_ptr<const std::vector<int16_t>> wave[4];
};

class Engine
{
public:
    static constexpr int kVoices = 16, kParts = 8;

    Engine();
    void prepare (double sampleRate);

    // message thread: copy the disk contents (waves are shared, so only call setBlock when a disk was loaded or a wave changed)
    void setBlock (const data::Block& b);
    void setBlock2 (const data::Block& b);         // Block II (the second memory block): patches 16-31 of the parts
    void setParams2 (const data::Block& b);
    void setParams (const data::Block& b);          // tones and patches only (cheap)
    void setVoiceMode (int mode);                   // 0 VAL, 1 VAF, 2.. = V1..V22
    void setAudition (int tone, int patch);        // edit pages: every note plays this tone (or patch) on all channels; -1 = off
    void setPart (int part, int channel, int patch);   // part A-H (0-7): MIDI channel 0-15 (-1 = off), patch 0-15

    // audio thread
    void midi (const uint8_t* msg, int n);          // one complete MIDI message
    void render (float* left, float* right, int n,  // writes the stereo mix (overwrites)
                 float* const* outL = nullptr, float* const* outR = nullptr);   // Out 1-8 (null / entry null = bus not in use: that output goes into the mix)
    void floppyClick (int n = 1) { clickReq.fetch_add (n); }   // any thread: n Gotek head clicks (simulated floppy drive)
    void allNotesOff();
    int activeVoices() const;
    bool voiceActive (int i) const { return voices[i].on; }
    int stealCount() const { return steals.load (std::memory_order_relaxed); }     // notes that took over a sounding voice
    int dropCount() const  { return drops.load (std::memory_order_relaxed); }      // notes that found no voice at all
    void resetCounters() { steals = 0; drops = 0; }

private:
    struct Eg
    {
        int sus = 2, end = 3, rate[8] = {}, level[8] = {};
        int stage = 0;                  // stage being run (0-based); stage == end: finished
        bool hold = false;
        float cur = 0.0f;               // 0..127
        void start (const uint8_t* tone, int susByte, int levelBase, int rateBase, int velRate, int velocity, int keyRate, int key);
        void release();
        void run (int samples, double sr);   // advances the level
        bool done() const { return stage >= end; }
    };
    struct Voice
    {
        bool on = false, released = false, held = false;       // held: key is up but the sustain pedal keeps it
        int ch = 0, key = 0, vel = 0, part = 0, group = -1;
        uint64_t age = 0;
        data::Tone tone;
        const std::vector<int16_t>* wave = nullptr;
        std::shared_ptr<const std::vector<int16_t>> keep;
        int base = 0, words = 0;
        double pos = 0, step = 1;
        int dir = 1;
        bool finished = false;
        float gain = 1.0f, pan = 0.0f;
        Eg tva, tvf;
        float ic1 = 0, ic2 = 0;         // filter state
        float cutoffBase = 0, keyFollow = 0, res = 0, egDepth = 0;
        float fadeOut = 1.0f;
        float bendRange = 12.0f;
        int out = 0;                    // output jack 0-7 (Out 1-8)
    };
    struct Part { int channel = -1, patch = 0; };

    void noteOn (int ch, int key, int vel);
    void noteOff (int ch, int key);
    void setBlockN (int blk, const data::Block& b, bool waves);
    void startVoice (int ch, int key, int vel, int part, const Snapshot& s, int toneIdx, float gain);
    Voice* allocate (int part, int ch);
    void renderVoice (Voice& v, float* l, float* r, int n);
    void applyGroups();
    static float cutoffHz (float value127, float semitonesKey, float egOct);

    std::shared_ptr<const Snapshot> snap;
    std::shared_ptr<const Snapshot> retired[4];
    unsigned retIdx = 0;
    Part parts[kParts];
    std::atomic<int> audTone { -1 }, audPatch { -1 };
    int voiceMode = 12;
    int pendingOut = -2;                // output of the voices a note starts: 0-7 = patch Output Assign, -2 = per tone (Tone mode)
    int groupOf[kVoices];
    Voice voices[kVoices];
    uint64_t counter = 0;
    double sr = 44100.0;
    std::atomic<int> clickReq { 0 };
    std::atomic<int> steals { 0 }, drops { 0 };
    int clickPending = 0, clickDelay = 0;
    float clickLast = 0.f, clickTick = 0.f, clickThump = 0.f, clickPhase = 0.f;
    uint32_t clickRng = 12345u;
    bool sustain[16] = {};
    float bend[16] = {};
    std::vector<float> scratchL, scratchR;
};
}
