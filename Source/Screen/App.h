#pragma once
// Shared application state for the screen: the loaded data block plus the file and floppy services.
// No JUCE dependency.  The plugin installs real services (folder on disk, USB floppy drive);
// the defaults here are in-memory ones used by tests and the browser preview.
#include "../Data/Block.h"
#include "../Data/HdImage.h"
#include "../Data/BlockOps.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace s5
{
struct FileService
{
    virtual ~FileService() = default;
    virtual std::string folderName() const = 0;
    virtual std::vector<std::string> list() = 0;                    // image names without ".IMG"
    virtual int sizeOf (const std::string& name) = 0;               // bytes, -1 when missing
    virtual bool read (const std::string& name, std::vector<uint8_t>& out) = 0;
    virtual bool write (const std::string& name, const std::vector<uint8_t>& data) = 0;
    // Save dialog: the operating system (Finder / browser download) chooses where the .IMG goes.
    // done (ok, text) may be called later; text = file name or error / "Cancelled".
    // Load Image: the first list entry opens the Finder; the chosen image is copied into the folder.  done (ok, name or error)
    // Mac: no fixed folder at all - every load / save goes through the Finder dialogs (local disks and network volumes)
    virtual bool dialogs() const { return false; }
    // Load Wave: an audio file (wav, aiff, mp3 ...) from the Finder; done (ok, file name with extension or error, bytes)
    virtual void pickAudio (std::function<void (bool, const std::string&, std::vector<uint8_t>&)> done) { std::vector<uint8_t> none; done (false, "Not available", none); }
    // decodes the file to mono floats (-1..1) and its sample rate
    virtual bool decodeAudio (const std::string& name, const std::vector<uint8_t>& bytes, std::vector<float>& mono, double& rate, std::string& err)
    { (void) name; (void) bytes; (void) mono; (void) rate; err = "Not available"; return false; }
    virtual void pick (std::function<void (bool, const std::string&, std::vector<uint8_t>&)> done, const std::string& title = "Open disk image") { (void) title; std::vector<uint8_t> none; done (false, "Not available", none); }
    virtual void saveAs (const std::string& suggested, const std::vector<uint8_t>& data, std::function<void (bool, const std::string&)> done)
    { (void) suggested; (void) data; done (false, "Not available"); }
    // Save in two steps (IMG Save / Backup): first WHERE and under which name (the Finder's save dialog), later the data
    virtual void chooseSave (const std::string& suggested, std::function<void (bool, const std::string&)> done) { (void) suggested; done (false, "Not available"); }
    virtual bool writeChosen (const std::vector<uint8_t>& data, std::string& err) { (void) data; err = "Not available"; return false; }
};

struct FloppyDrive
{
    enum class State { NoDrive, NoDisk, Ready, Busy };
    struct Job
    {
        bool write = false, verifying = false, finished = false, ok = false;
        int track = 0, limit = 160;          // track: 0..limit (80 cylinders x 2 sides), counts again while verifying; limit < 160: only the first tracks are read
        std::string error;
        std::string label, sysLine;          // read job: disk name (12 chars) and system text line, known as soon as their tracks were read
        std::vector<uint8_t> data;           // result of a read
    };
    virtual ~FloppyDrive() = default;
    virtual State state() = 0;
    virtual std::string name() = 0;
    virtual bool startRead() = 0;
    virtual bool startRead (int tracks) { (void) tracks; return startRead(); }     // only the first tracks (names of patches / tones need 16 of 160)
    virtual bool startWrite (const std::vector<uint8_t>& image, bool verify) = 0;
    virtual void abort() = 0;
    virtual const Job& job() = 0;
    virtual bool tick (int ms) = 0;          // true when the screen has to be redrawn
};

class MemFiles : public FileService
{
public:
    std::string folderName() const override { return "FlashFloppy"; }
    std::vector<std::string> list() override { std::vector<std::string> v; for (auto& kv : files) v.push_back (kv.first); return v; }
    int sizeOf (const std::string& n) override { auto it = files.find (n); return it == files.end() ? -1 : (int) it->second.size(); }
    bool read (const std::string& n, std::vector<uint8_t>& out) override { auto it = files.find (n); if (it == files.end()) return false; out = it->second; return true; }
    bool write (const std::string& n, const std::vector<uint8_t>& d) override { files[n] = d; return true; }
    std::function<void (const std::string&, const std::vector<uint8_t>&)> download;   // browser: hands the file to the download
    void saveAs (const std::string& suggested, const std::vector<uint8_t>& d, std::function<void (bool, const std::string&)> done) override
    {
        if (download) download (suggested + ".IMG", d); else files[suggested] = d;
        done (true, suggested + ".IMG");
    }
    void chooseSave (const std::string& suggested, std::function<void (bool, const std::string&)> done) override { chosen = suggested; done (true, suggested + ".IMG"); }
    bool writeChosen (const std::vector<uint8_t>& d, std::string&) override { if (download) download (chosen + ".IMG", d); else files[chosen] = d; return true; }
private:
    std::string chosen;
    std::map<std::string, std::vector<uint8_t>> files;
};

// A pretend drive for tests and the browser: one inserted disk, one track every 25 ms.
class SimFloppy : public FloppyDrive
{
public:
    explicit SimFloppy (std::vector<uint8_t> inserted) : disk (std::move (inserted)) {}
    State state() override { return running ? State::Busy : State::Ready; }
    std::string name() override { return "USB Floppy"; }
    bool startRead() override { return begin (160); }
    bool startRead (int tracks) override { return begin (tracks); }
    bool begin (int tracks)
    {
        if (running) return false;
        lim = std::max (1, std::min (160, tracks));
        j = {}; j.limit = lim; running = true;
        data::Block t; simLabel.clear(); simSys.clear();
        if (t.parse (disk.data(), disk.size()) == data::Block::Result::Ok) { simLabel = t.label; simSys = t.systemLine(); }
        return true;
    }
    bool startWrite (const std::vector<uint8_t>& image, bool verify) override
    { if (running) return false; j = {}; j.write = true; pending = image; doVerify = verify; running = true; return true; }
    void abort() override { if (running) { running = false; j.finished = true; j.ok = false; j.error = "Aborted"; } }
    const Job& job() override { return j; }
    bool tick (int ms) override
    {
        if (! running) return false;
        acc += ms;
        while (acc >= 25 && running)
        {
            acc -= 25;
            if (j.track >= 1 && ! j.write) j.sysLine = simSys;            // the header tracks come first, the name a little later
            if (j.track >= 15 && ! j.write) j.label = simLabel;
            if (++j.track >= (j.write ? 160 : j.limit))
            {
                if (j.write && ! j.verifying && doVerify) { j.verifying = true; j.track = 0; continue; }
                running = false; j.finished = true; j.ok = true;
                if (j.write) disk = pending; else j.data = disk;
            }
        }
        return true;
    }
private:
    std::string simLabel, simSys;
    std::vector<uint8_t> disk, pending;
    Job j; bool running = false, doVerify = false; int acc = 0, lim = 160;
};

struct App
{
    App();
    data::Block block;
    data::Block block2;        // the second memory block (Block II): patches II11-II28 of the PLAY parts; loaded by "Set II" / "Block II"
    bool linked2 = false;      // true once something was loaded into Block II
    std::vector<std::string> labelText { "", "", "", "", "", "", "", "", "", "" };   // Label Set: 5 lines of 12 characters per block (60 letters, shown by Backup)
    std::string clip;                                      // text clipboard of the screen (copy / cut / paste in the label block)
    data::Block disk;          // contents of the inserted disk (Load Image / DD Read / Save / Format work on it)
    bool hasDisk = false;
    bool linked = false;       // true once a disk image is loaded: the parameter pages then edit `block`
    int curKey = 60;           // key cursor of the Split page (12-120)
    int curPatch = 0;          // patch the edit pages work on: 0-15 = Block I (I11-I28), 16-31 = Block II (II11-II28)
    int curTone = 0;           // tone the edit pages work on: 0-31 = Block I, 32-63 = Block II
    data::Block& blk (int b) { return b ? block2 : block; }
    data::Tone& toneAt (int i) { i = i < 0 ? 0 : (i > 63 ? 63 : i); return blk (i / 32).tones[i % 32]; }
    data::Patch& patchAt (int i) { i = i < 0 ? 0 : (i > 31 ? 31 : i); return blk (i / 16).patches[i % 16]; }
    // the edit pages work on the data once a disk is loaded; Block II is always real data
    bool boundTone (int i) const { return linked || i >= 32; }
    bool boundPatch (int i) const { return linked || i >= 16; }
    bool toneBound() const { return boundTone (curTone); }
    bool patchBound() const { return boundPatch (curPatch); }
    // Wave Scope: the last 480 input samples (-2048..2047), fed by the host (plugin audio input / browser microphone)
    std::vector<int16_t> scope = std::vector<int16_t> (480, 0);
    std::atomic<int> floppyClicks { 0 };       // UI -> audio: simulated floppy-drive clicks (Gotek)
    bool uiDirty = false;                      // set by asynchronous results (Finder dialog); the next tick redraws
    bool scopeRun = false;
    bool scopeAct = false;                     // Wave Scope switched on with COM (idle: Sel and ----- are inactive)
    // Sampling (UTIL-Sampling): the audio thread feeds recFeed(); the screen thread arms, polls and stores
    struct Rec
    {
        std::atomic<int> state { 0 };          // 0 idle, 1 waiting for the trigger, 2 recording, 3 finished (to be stored)
        std::atomic<float> level { 0.f };      // peak of the input, 0..1, decays
        bool manual = false, limiter = false, khz15 = false;
        float thr = 0.f; int pre = 0, maxN = 0, got = 0, w = 0, tone = 0, bank = 0, key = 60;
        double rate = 44100.0;
        std::vector<float> buf;
        bool review = false;                   // a recording was just stored: the Sampling page shows the whole bank with the new part in red
        std::string msg;                       // "Complete", "Memory full" ... shown instead of "Ready"
    } rec;
    double hostRate = 44100.0;                 // set by the plugin / the browser preview
    std::atomic<bool> recWatch { false };      // the Sampling page is on screen: redraw regularly (Level meter)
    void recFeed (const float* in, int n, double hostRate);                // audio thread
    // secs = real recording time, preMs = pre-trigger; returns "" or an error text
    std::string recArm (int tone, int bank, bool khz15, double secs, double preMs, int thr7, bool manual, bool limiter, int key, double hostRate);
    void recCancel();
    void recPoll();                                                        // screen thread: stores a finished recording
    static int recLevel7 (float peak);                                     // 0..127 (dB scale over 48 dB)
    void scopeFeed (const float* in, int n);   // takes a float block (-1..1); starts at a rising zero crossing
    // MIDI Monitor: the last 180 received bytes and the last note (fed by the host)
    std::vector<uint8_t> midiLog;
    int partCh[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };      // PLAY pages: MIDI channel (1-16), patch (0-15) and level of parts A-H
    int partPatch[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    int partLevel[8] = { 127, 127, 127, 127, 127, 127, 127, 127 };
    std::string imageName, fileMsg;                  // last image chosen in the Finder, last message of an asynchronous file action
    bool fileMsgErr = false, viaImg = false;         // viaImg: the Disk menu entry was an IMG one (file dialogs)
    data::HdImage hd;                                // hard disk image (ZIP / ZuluSCSI) opened through the Finder
    bool hasHd = false;
    std::string hdName, hdTarget = "LoadAll";        // hdTarget: page that follows the slot choice
    int hdSlot = 0, hdSlotI = 0, hdSlotII = 0, hdWhich = 0;   // hdSlot: cursor of the slot grid; I / II: the two lines of the load page
    bool hdOpenGrid = false, hdSync = false;                         // click on a slot value: UiState opens the grid
    int hdArea = 0;                                  // area of the HD Load P / T pages
    // SCSI bus (Options > Hard disk images): hard disks on the IDs 0-6, the S-550 itself is ID 7 (fixed), the CD-5 takes one of 0-6.
    // The S-550 works with the hard disk of the highest ID (CD-5 manual p.26); `hd` / `hasHd` always hold that device.
    struct HdDevice { std::string path, name; bool ok = false; int areas = 0; };
    HdDevice hdDev[7];
    int cdId = -1;                                   // SCSI ID of the CD-5, -1 = not connected
    int setupLoadI = 0, setupLoadII = 0, setupPchg = 0, setupCtrl = 1, hdSetupId = 0;   // DISK-Setup page (Off / area index+1 / channel / controller)
    int hdActiveId = -1;                             // device behind `hd`
    std::function<bool (const std::string&, std::vector<uint8_t>&)> readPath;    // platform: read a file by path
    std::function<void()> hdConfigChanged;           // platform: store the configuration
    bool hdAttach (int id, const std::string& path, std::string& err);          // read + check an image, put it on the ID
    void hdDetach (int id);
    int hdSaveI = 0, hdSaveII = 0, hdVerify = 1;      // HD Save page: target areas, Wave Verify
    std::function<bool(const std::string&, size_t, const uint8_t*, size_t)> writeRange;   // writes bytes into the image file at an offset
    bool hdSaveSlot (int slot, const data::Block& b, std::string& err, bool withParams = true);
    bool hdVerifySlot (int slot, const data::Block& b, std::string& err);   // Wave Verify: the file is read again and the waves of the area are compared with the block
    // The system of a hard disk in the sampler's memory: HD Load SYS reads it from a linked image, HD Save SYS writes it to the active one.
    // Content: system directory (sector 1) + sectors 256-5367.
    std::vector<uint8_t> hdSysMem; std::string hdSysLine;
    bool hdLoadSys (int id, std::string& err);
    bool hdSaveSys (int id, std::string& err);             // to the image on this ID
    bool hdActivate (int id);                        // load the image of a device into `hd`
    bool setCdId (int id, std::string& err);         // -1 = off
    data::Block hdCache; int hdCacheSlot = -1; bool hdCacheOk = false;
    const data::Block* hdBlockOf (int slot);         // block of a slot (cached), nullptr when empty
    void pickHd();                                   // Finder dialog -> hd
    bool hdLoadSlot();                               // chosen slot -> disk
    void newSet2 (const data::Block& b);             // Block II: replaced completely (the part table of Set I stays)
    void newSet (const data::Block& b);              // a Set / Block was loaded: the old memory (tones, patches, waves, sounding notes) is dropped completely
    void pickImage (bool intoMemory, const std::string& title = "Open disk image");
    bool pickDone = false;                               // set when a Finder pick delivered a disk (IMG Load opens its command window)                // Finder dialog -> disk (and memory); sets fileMsg
    uint32_t waveRev = 0;                            // incremented when a page edits wave samples in place (Wave Draw): the sound engine reloads
    int auditionTone = -1, auditionPatch = -1;       // edit pages: the keyboard / MIDI plays the displayed tone or patch
    std::atomic<int> recLevel { 100 }, volume { 100 };   // strip under the screen: recording level of the input, master volume (0-127, 100 = unity)
    int voiceMode = 12;                        // PLAY header: 0 VAL, 1 VAF, 2.. = V 1..V22 (manual p.28-29), default V11
    // notes currently held per MIDI channel (PLAY pages light the part letters and keys)
    uint8_t held[16][128] = {};
    bool playing (int ch) const { if (ch < 0 || ch > 15) return false; for (int k = 0; k < 128; ++k) if (held[ch][k]) return true; return false; }
    void noteSet (int ch, int key, bool on);   // mouse on a keyboard: held as long as the button is down
    int monCh = 0;                             // Display CH: 0-15 = channel 1-16, 16 = All
    bool monRt = false;                        // Real time MSG
    int lastKind = 0;                          // 0 nothing yet, 1 a note was received
    int lastKey = 0, lastVel = 0;
    int octView = 0;                                 // Oct.Shift of the shown patch: the edit keyboards move by that many octaves
    int pressSeq = 0, pressKey = 0;                  // every key press (mouse or MIDI, any channel): the Split page assigns tones with it
    long long pressMs = 0;
    void midiFeed (const uint8_t* msg, int n); // one complete MIDI message
    std::function<void()> openMidiLinkDialog;
    // S-550 parameters outside the tone / patch data (shared by the MIDI pages and the SysEx link). Values are the raw bytes of the device.
    // mpar = MIDI parameter block (parameter index = offset / 2), fpar = function block (0 = Master Tune, signed byte)
    int mpar[132] = {};
    int fpar[512] = {};                                     // function block (Play part table lives in voiceMode / partCh / partPatch / partLevel)
    std::function<void (const uint8_t*, int)> midiOut;     // MIDI thru to the real S-550 (set by the platform layer; channel messages only)
    std::atomic<bool> muteLocal { false };                 // internal sound off while the real S-550 plays
    void initDevParams();
    // transfer to / from a real S-550 (SysEx link): the page starts it, the platform layer runs it
    // cmd: 0 Set I, 1 Set II (patches, tones, waves), 2 Tone I, 3 Tone II (patches + tones), 4 Function, 5 MIDI parameters
    std::function<bool (bool save, int cmd, std::string& err)> midiXfer;
    std::atomic<int> xferState { 0 };                      // 0 idle, 1 running, 2 finished, 3 failed
    std::atomic<double> xferFrac { 0 };
    std::string xferErr;
    std::atomic<bool> waveDone { false };                  // Load Wave: the file dialog has finished
    std::string waveName;                                  // Load Wave: last file       // S-550 MIDI link dialog (set by the platform layer)
    std::function<void (const uint8_t*, int)> soundOut;   // set by the plugin: notes played with the mouse go to the sound engine
    std::unique_ptr<FileService> files;
    std::unique_ptr<FloppyDrive> drive;
    static App& get();
};

data::Block makeDemoBlock (int variant);       // variant 0: the factory sounds shown on the boards, 1: another bank
}
