#pragma once
// Roland S-550 System Exclusive (MIDI Implementation, Sep 1 1987): message builders/parser,
// address map, bulk dump / bulk request of a data block, live parameter access.
// No JUCE dependency: the MIDI ports are plugged in as std::function (see Platform/MidiLink).
#include "../Data/Block.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace s5::sysex
{
using Bytes = std::vector<uint8_t>;

constexpr uint8_t kRoland = 0x41, kModel = 0x1e;
enum Cmd : uint8_t { RQD = 0x41, WSD = 0x40, DAT = 0x42, ACK = 0x43, EOD = 0x45, ERR = 0x4e, RJC = 0x4f, RQ1 = 0x11, DT1 = 0x12 };

// ---- address map (7 bit bytes: address = a*2^21 + b*2^14 + c*2^7 + d) ----
constexpr uint32_t kPatchBase = 0x000000, kPatchSize = 512,  kNumPatches = 32;      // 00 00 00 00, 512 bytes each
constexpr uint32_t kFuncBase  = 0x004000, kFuncSize  = 1024;                        // 00 01 00 00 (4.2: Master Tune, Voice Mode, part table ...)
constexpr uint32_t kMidiBase  = 0x008000, kMidiSize  = 512;                         // 00 02 00 00
constexpr uint32_t kToneBase  = 0x00c000, kToneSize  = 256,  kNumTones = 64;        // 00 03 00 00
constexpr uint32_t kWaveBaseA = 0x200000, kWaveBaseB = 0x280000, kWaveSegSize = 24576;   // 01 00 00 00 / 01 20 00 00, 18 segments each
constexpr uint32_t kMaxData = 256;          // data bytes per DT1/DAT message

inline uint32_t patchAddr (int n)            { return kPatchBase + (uint32_t) n * kPatchSize; }
inline uint32_t toneAddr (int n)             { return kToneBase + (uint32_t) n * kToneSize; }
inline uint32_t waveAddr (int bank, int seg) { return kWaveBaseA + (uint32_t) bank * 0x80000u + (uint32_t) seg * kWaveSegSize; }   // banks A, B (Block I) and C, D (Block II)

Bytes   encodeAddr (uint32_t linear);              // -> 4 bytes of 7 bit
uint32_t decodeAddr (const uint8_t* p);
uint8_t checksum (const uint8_t* p, size_t n);     // makes the low 7 bits of the sum zero
Bytes   addrAdd (uint32_t linear, uint32_t off);   // helper for address + offset in 7 bit form

// ---- messages ----
struct Message
{
    uint8_t cmd = 0, dev = 0;
    uint32_t addr = 0, size = 0;        // RQ1/RQD/WSD: size;  DT1/DAT: size = data.size()
    Bytes data;
};
Bytes buildRQ1 (int dev, uint32_t addr, uint32_t size);
Bytes buildRQD (int dev, uint32_t addr, uint32_t size);
Bytes buildWSD (int dev, uint32_t addr, uint32_t size);
Bytes buildDT1 (int dev, uint32_t addr, const uint8_t* d, size_t n);   // n <= 256
Bytes buildDAT (int dev, uint32_t addr, const uint8_t* d, size_t n);
Bytes buildSimple (int dev, uint8_t cmd);                              // ACK, EOD, ERR, RJC
bool  parse (const uint8_t* p, size_t n, Message& out);                // false: not S-550 / bad checksum / malformed

// ---- parameter encoding: a value is two bytes 0000aaaa 0000bbbb ----
void nib (Bytes& out, uint8_t v);
Bytes toNibbles (const uint8_t* p, size_t n);
Bytes fromNibbles (const uint8_t* p, size_t n);        // n even
Bytes toneToSysex (const data::Tone&);                 // 128 byte tone record -> 256 bytes
void  sysexToTone (const uint8_t* p, data::Tone&);
void  toneRecord (const data::Tone&, uint8_t* rec128);       // disk record (= parameter order of the S-550)
void  recordTone (const uint8_t* rec128, data::Tone&);
Bytes patchToSysex (const data::Patch&);               // 256 -> 512
void  sysexToPatch (const uint8_t* p, data::Patch&);
Bytes waveToSysex (const int16_t* s, size_t samples);  // 12 bit two's complement: 0aaaaaaa 0bbbbb00
void  sysexToWave (const uint8_t* p, int16_t* s, size_t samples);

// ---- transfer plan ----
struct Chunk { uint32_t addr; Bytes data; std::string what; };
struct PlanOptions { bool patches = true, tones = true, waves = true, label = true; int toneOffset = 0, patchOffset = 0, waveBankOffset = 0; };   // Block II: 32 / 16 / 2 (wave banks C, D)   // offsets: 0 = Block I, 32/16 = Block II
std::vector<Chunk> planBlock (const data::Block& b, const PlanOptions& o);   // one chunk per memory area; split into <=256 byte messages when sending

// ---- session: sending, handshake, requests, live parameters ----
class Session
{
public:
    std::function<void (const uint8_t*, size_t)> send;               // MIDI out (one complete SysEx message)
    std::function<void (uint32_t addr, const Bytes& data)> onData;   // DT1/DAT received (e.g. panel edit of a tone parameter)
    std::function<void (int done, int total, const std::string& what)> progress;

    int  deviceId = 0;           // 0-15 (display 1-16): must match the S-550 MIDI mode setting
    int  gapMs = 25;             // >= 20 ms between messages (DT1)
    int  ackTimeoutMs = 1500;
    bool handshake = false;      // false: one way (DT1), true: WSD/ACK, DAT/ACK, EOD
    std::function<void (int ms)> sleepFn;      // test hook; default std::this_thread::sleep_for

    void receive (const uint8_t* p, size_t n);        // feed every incoming SysEx message here (MIDI-in thread)
    void cancel() { abort_ = true; }
    std::string logText();                            // last messages (TX/RX hex) for diagnosis
    std::atomic<long long> doneBytes { 0 }, totalBytes { 0 };   // progress of requestBlock (bytes)
    bool mergeRuns = false;                           // handshake: one WSD for a run of neighbouring chunks (patches 1-16, tones ...)

    // blocking calls (run them on a worker thread, not on the UI thread)
    bool sendChunks (const std::vector<Chunk>& c, std::string& err);
    bool sendBlock (const data::Block& b, const PlanOptions& o, std::string& err) { return sendChunks (planBlock (b, o), err); }
    bool request (uint32_t addr, uint32_t size, Bytes& out, std::string& err);   // RQ1, collects the DT1 replies
    bool requestBlock (data::Block& b, const PlanOptions& o, std::string& err);

    // live control (single parameter, DT1 with 1 value = 2 bytes)
    void setParam (uint32_t base, int index, uint8_t value);
    void setToneParam (int tone, int index, uint8_t v)  { setParam (toneAddr (tone), index, v); }
    void setPatchParam (int patch, int index, uint8_t v) { setParam (patchAddr (patch), index, v); }
    void setMidiParam (int index, uint8_t v)             { setParam (kMidiBase, index, v); }
    void setFuncParam (int index, uint8_t v)             { setParam (kFuncBase, index, v); }

private:
    void sleep (int ms);
    void tx (const Bytes& m);
    void note (const char* dir, const uint8_t* p, size_t n);
    std::mutex lg_;
    std::vector<std::string> log_;
    long long t0_ = 0;
    static const char* cmdName (uint8_t c);
    bool waitReply (uint8_t& cmd, int ms);
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<uint8_t> replies_;       // queue of handshake answers (ACK/RJC/ERR)
    std::map<uint32_t, Bytes> got_;      // DT1/DAT payloads while a request is running
    uint32_t gotBytes_ = 0;
    bool eod_ = false;
    std::atomic<bool> abort_ { false };
    std::atomic<bool> counting_ { false };
};
}
