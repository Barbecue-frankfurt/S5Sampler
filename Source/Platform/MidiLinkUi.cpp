#include "MidiLinkUi.h"
#include "MidiLink.h"
#include "../Screen/App.h"
#include <map>

namespace s5
{
namespace
{
juce::PropertiesFile* settings()
{
    static juce::ApplicationProperties props;
    static bool ready = false;
    if (! ready)
    {
        ready = true;
        juce::PropertiesFile::Options o;
        o.applicationName = "S5Sampler"; o.filenameSuffix = ".midi.settings"; o.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName = "~/.config";
       #else
        o.folderName = "";
       #endif
        props.setStorageParameters (o);
    }
    return props.getUserSettings();
}

bool gRecv = false;

struct Cfg { juce::String out, in; int dev = 0; bool handshake = false, liveOut = true, liveIn = true, merge = false, thru = true, mute = false, autoOpen = true, wasOpen = false; } cfg;

// Live control: edits made in the program are compared with the last state sent and go to the S-550 as single parameters;
// parameters the S-550 reports (panel edits) are written into the blocks.
class Live : private juce::Timer
{
public:
    MidiLink link;
    std::mutex qm;
    std::vector<std::pair<uint32_t, sysex::Bytes>> inQ;
    uint8_t snapTone[2][32][128] = {}, snapPatch[2][16][256] = {};
    int snapM[132] = {}, snapF[512] = {};
    bool snapValid = false;
    std::map<uint32_t, uint8_t> outQ;            // address -> value (merged)
    std::atomic<int> sent { 0 }, received { 0 };

    Live()
    {
        link.session.onData = [this] (uint32_t a, const sysex::Bytes& d) { std::lock_guard<std::mutex> l (qm); inQ.push_back ({ a, d }); };
        startTimerHz (30);
    }
    ~Live() override { stopTimer(); }
    void resync() { snapValid = false; }

private:
    static int toneOffset (int b) { return b ? 32 : 0; }
    static int patchOffset (int b) { return b ? 16 : 0; }

public:
    // function block: master tune, voice mode, part channels / patches / levels (= the PLAY pages)
    static const std::vector<int>& funcIdx()
    {
        static const std::vector<int> v = [] { std::vector<int> r { 0, 0x10 }; for (int i = 0x11; i <= 0x20; ++i) r.push_back (i); for (int i = 0x2b; i <= 0x32; ++i) r.push_back (i); return r; }();
        return v;
    }
    static int curFunc (int idx)                       // -1 = nothing to send (part switched off)
    {
        auto& a = App::get();
        if (idx == 0) return a.fpar[0] & 0xff;
        if (idx == 0x10) return a.voiceMode;
        if (idx >= 0x11 && idx <= 0x18) return a.partCh[idx - 0x11] > 0 ? a.partCh[idx - 0x11] - 1 : -1;
        if (idx >= 0x19 && idx <= 0x20) return a.partPatch[idx - 0x19] & 31;
        if (idx >= 0x2b && idx <= 0x32) return a.partLevel[idx - 0x2b] & 127;
        return a.fpar[idx];
    }
    static void setFunc (int idx, int v)
    {
        auto& a = App::get();
        if (idx == 0x10) a.voiceMode = std::min (23, v);
        else if (idx >= 0x11 && idx <= 0x18) a.partCh[idx - 0x11] = v < 16 ? v + 1 : 0;
        else if (idx >= 0x19 && idx <= 0x20) a.partPatch[idx - 0x19] = v & 31;
        else if (idx >= 0x2b && idx <= 0x32) a.partLevel[idx - 0x2b] = v & 127;
        else if (idx >= 0 && idx < 512) a.fpar[idx] = v;
    }

private:
    void takeSnap()
    {
        auto& a = App::get();
        for (int b = 0; b < 2; ++b)
        {
            for (int i = 0; i < 32; ++i) sysex::toneRecord (a.blk (b).tones[i], snapTone[b][i]);
            for (int i = 0; i < 16; ++i) std::copy (a.blk (b).patches[i].raw, a.blk (b).patches[i].raw + 256, snapPatch[b][i]);
        }
        for (int i = 0; i < 132; ++i) snapM[i] = a.mpar[i];
        for (int i : funcIdx()) snapF[i] = curFunc (i);
        snapValid = true;
    }

    void timerCallback() override
    {
        if (! link.isOpen()) { snapValid = false; return; }
        auto& a = App::get();
        if (link.busy())                                          // progress for the DISK-MIDI pages
        {
            const double tb = (double) link.session.totalBytes.load();
            a.xferFrac = gRecv ? (tb > 0 ? (double) link.session.doneBytes.load() / tb : 0.0) : (link.total.load() ? (double) link.done.load() / (double) link.total.load() : 0.0);
        }
        // S-550 -> program
        std::vector<std::pair<uint32_t, sysex::Bytes>> in;
        { std::lock_guard<std::mutex> l (qm); in.swap (inQ); }
        if (! snapValid) takeSnap();
        if (cfg.liveIn && ! link.busy())
            for (auto& m : in)
            {
                const uint32_t ad = m.first;
                if (ad >= sysex::kMidiBase && ad < sysex::kMidiBase + sysex::kMidiSize && ((ad - sysex::kMidiBase) & 1) == 0)
                {
                    for (size_t k = 0; k + 1 < m.second.size(); k += 2)
                    {
                        const uint32_t idx = (ad - sysex::kMidiBase) / 2 + (uint32_t) k / 2;
                        if (idx < 132) { a.mpar[idx] = ((m.second[k] & 15) << 4) | (m.second[k + 1] & 15); snapM[idx] = a.mpar[idx]; a.uiDirty = true; ++received; }
                    }
                }
                else if (ad >= sysex::kFuncBase && ad < sysex::kFuncBase + sysex::kFuncSize && ((ad - sysex::kFuncBase) & 1) == 0)
                {
                    for (size_t k = 0; k + 1 < m.second.size(); k += 2)
                    {
                        const uint32_t idx = (ad - sysex::kFuncBase) / 2 + (uint32_t) k / 2;
                        if (idx < 512) { setFunc ((int) idx, ((m.second[k] & 15) << 4) | (m.second[k + 1] & 15)); snapF[idx] = curFunc ((int) idx); a.uiDirty = true; ++received; }
                    }
                }
                for (int b = 0; b < 2; ++b)
                {
                    if (ad >= sysex::toneAddr (toneOffset (b)) && ad < sysex::toneAddr (toneOffset (b) + 32))
                    {
                        const uint32_t off = ad - sysex::toneAddr (toneOffset (b)); const int t = (int) (off / 256);
                        uint8_t rec[128]; sysex::toneRecord (a.blk (b).tones[t], rec);
                        for (size_t k = 0; k < m.second.size(); ++k)
                        {
                            const uint32_t o = (off % 256) + (uint32_t) k;
                            if (o >= 256) break;
                            uint8_t& r = rec[o / 2];
                            r = (o & 1) ? (uint8_t) ((r & 0xf0) | (m.second[k] & 15)) : (uint8_t) ((r & 0x0f) | ((m.second[k] & 15) << 4));
                        }
                        sysex::recordTone (rec, a.blk (b).tones[t]); std::copy (rec, rec + 128, snapTone[b][t]); a.uiDirty = true; ++received;
                    }
                    else if (ad >= sysex::patchAddr (patchOffset (b)) && ad < sysex::patchAddr (patchOffset (b) + 16))
                    {
                        const uint32_t off = ad - sysex::patchAddr (patchOffset (b)); const int p = (int) (off / 512);
                        uint8_t* r = a.blk (b).patches[p].raw;
                        for (size_t k = 0; k < m.second.size(); ++k)
                        {
                            const uint32_t o = (off % 512) + (uint32_t) k;
                            if (o >= 512) break;
                            r[o / 2] = (o & 1) ? (uint8_t) ((r[o / 2] & 0xf0) | (m.second[k] & 15)) : (uint8_t) ((r[o / 2] & 0x0f) | ((m.second[k] & 15) << 4));
                        }
                        std::copy (r, r + 256, snapPatch[b][p]); a.uiDirty = true; ++received;
                    }
                }
            }
        // program -> S-550
        if (cfg.liveOut && ! link.busy())
        {
            int changes = 0;
            std::vector<std::pair<uint32_t, uint8_t>> ch;
            for (int b = 0; b < 2; ++b)
            {
                for (int i = 0; i < 32; ++i)
                {
                    uint8_t rec[128]; sysex::toneRecord (a.blk (b).tones[i], rec);
                    for (int k = 0; k < 128; ++k) if (rec[k] != snapTone[b][i][k]) { ch.push_back ({ sysex::toneAddr (toneOffset (b) + i) + (uint32_t) k * 2, rec[k] }); ++changes; }
                    std::copy (rec, rec + 128, snapTone[b][i]);
                }
                for (int i = 0; i < 16; ++i)
                {
                    const uint8_t* r = a.blk (b).patches[i].raw;
                    for (int k = 0; k < 256; ++k) if (r[k] != snapPatch[b][i][k]) { ch.push_back ({ sysex::patchAddr (patchOffset (b) + i) + (uint32_t) k * 2, r[k] }); ++changes; }
                    std::copy (r, r + 256, snapPatch[b][i]);
                }
            }
            for (int i = 32; i < 132; ++i)                       // MIDI parameter block (not 97 / 98: exclusive switch and device ID are set at the S-550)
            {
                if (i == 97 || i == 98) { snapM[i] = a.mpar[i]; continue; }
                if (a.mpar[i] != snapM[i]) { ch.push_back ({ sysex::kMidiBase + (uint32_t) i * 2, (uint8_t) a.mpar[i] }); ++changes; snapM[i] = a.mpar[i]; }
            }
            for (int i : funcIdx())
            {
                const int v = curFunc (i);
                if (v >= 0 && v != snapF[i]) { ch.push_back ({ sysex::kFuncBase + (uint32_t) i * 2, (uint8_t) v }); ++changes; }
                snapF[i] = v;
            }
            if (changes > 400) ch.clear();               // a whole set was loaded: use "Send to S-550" instead of flooding the port
            for (auto& c : ch) outQ[c.first] = c.second;
            int n = 0;
            while (! outQ.empty() && n++ < 6)           // 6 messages per 33 ms = well within MIDI speed
            {
                const auto it = outQ.begin();
                const uint8_t d[2] = { (uint8_t) (it->second >> 4), (uint8_t) (it->second & 15) };
                const auto m = sysex::buildDT1 (link.session.deviceId, it->first, d, 2);
                link.session.send (m.data(), m.size());
                outQ.erase (it); ++sent;
            }
        }
        else if (! cfg.liveOut) { takeSnap(); outQ.clear(); }
    }
};

Live& live() { static Live l; return l; }

void saveCfg()
{
    if (auto* f = settings())
    {
        f->setValue ("out", cfg.out); f->setValue ("in", cfg.in); f->setValue ("dev", cfg.dev); f->setValue ("hs", cfg.handshake);
        f->setValue ("liveOut", cfg.liveOut); f->setValue ("liveIn", cfg.liveIn); f->setValue ("merge", cfg.merge); f->setValue ("thru", cfg.thru); f->setValue ("mute", cfg.mute); f->setValue ("auto", cfg.autoOpen); f->setValue ("wasOpen", cfg.wasOpen); f->saveIfNeeded();
    }
}

// One transfer (used by the dialog and by the pages DISK-MIDI Load / Save): block b (0 / 1), what to move, a result callback on the message thread.
// Receiving a block goes into a copy that replaces the memory when the transfer is complete (Set / Tone commands of the pages) or directly (dialog).
std::vector<sysex::Chunk> paramChunks (bool func, bool midi)
{
    auto& app = App::get();
    std::vector<sysex::Chunk> pc;
    if (midi)
    {
        sysex::Bytes mp; for (int i = 32; i < 96; ++i) sysex::nib (mp, (uint8_t) app.mpar[i]);
        pc.push_back ({ sysex::kMidiBase + 32 * 2, mp, "MIDI parameters" });
        sysex::Bytes pn; for (int i = 99; i < 131; ++i) sysex::nib (pn, (uint8_t) app.mpar[i]);
        pc.push_back ({ sysex::kMidiBase + 99 * 2, pn, "Program change numbers" });
    }
    if (func)
    {
        sysex::Bytes ft; sysex::nib (ft, (uint8_t) app.fpar[0]);
        pc.push_back ({ sysex::kFuncBase, ft, "Master tune" });
        sysex::Bytes pt;                                           // PLAY part table: voice mode, channels, patches
        sysex::nib (pt, (uint8_t) app.voiceMode);
        for (int i = 0; i < 8; ++i) sysex::nib (pt, (uint8_t) (app.partCh[i] > 0 ? app.partCh[i] - 1 : 0));
        for (int i = 0; i < 8; ++i) sysex::nib (pt, (uint8_t) (app.partPatch[i] & 31));
        pc.push_back ({ sysex::kFuncBase + 0x10 * 2, pt, "Part table" });
        sysex::Bytes lv; for (int i = 0; i < 8; ++i) sysex::nib (lv, (uint8_t) (app.partLevel[i] & 127));
        pc.push_back ({ sysex::kFuncBase + 0x2b * 2, lv, "Part levels" });
    }
    return pc;
}

void beginTransfer (bool toDevice, int b, sysex::PlanOptions o, bool func, bool midi, bool replaceBlock, std::function<void (bool, std::string)> userCb)
{
    auto& L = live().link;
    auto& app = App::get();
    L.session.deviceId = cfg.dev; L.session.handshake = cfg.handshake; L.session.mergeRuns = cfg.merge;
    o.toneOffset = b ? 32 : 0; o.patchOffset = b ? 16 : 0; o.waveBankOffset = b ? 2 : 0;
    const bool anyBlock = o.patches || o.tones || o.waves;
    gRecv = ! toDevice;
    auto cb = [userCb, b, replaceBlock, toDevice] (bool ok, std::string err)
    {
        live().resync();
        App::get().uiDirty = true;
        if (userCb) userCb (ok, err);
        (void) b; (void) replaceBlock; (void) toDevice;
    };
    if (toDevice)
    {
        const data::Block copy = app.blk (b);                       // the job outlives this call
        const auto pc = paramChunks (func, midi);
        L.job ([&L, copy, o, pc, anyBlock] (std::string& err) { return (! anyBlock || L.session.sendBlock (copy, o, err)) && (pc.empty() || L.session.sendChunks (pc, err)); }, cb);
    }
    else
    {
        auto into = std::make_shared<data::Block> (app.blk (b));
        L.job ([&L, into, o, func, midi, anyBlock] (std::string& err)
        {
            if (anyBlock && ! L.session.requestBlock (*into, o, err)) return false;
            auto& a = App::get();
            sysex::Bytes d;
            if (midi)
            {
                if (! L.session.request (sysex::kMidiBase, sysex::kMidiSize, d, err)) return false;
                const sysex::Bytes v = sysex::fromNibbles (d.data(), d.size());
                for (int i = 32; i < 132; ++i) a.mpar[i] = v[(size_t) i];
            }
            if (func)
            {
                if (! L.session.request (sysex::kFuncBase, sysex::kFuncSize, d, err)) return false;
                const sysex::Bytes fv = sysex::fromNibbles (d.data(), d.size());
                for (int i : Live::funcIdx()) if (i < (int) fv.size()) Live::setFunc (i, fv[(size_t) i]);
            }
            return true;
        }, [cb, into, b, anyBlock, replaceBlock] (bool ok, std::string err)
        {
            if (ok && anyBlock)
            {
                auto& a = App::get();
                if (replaceBlock) { if (b) a.newSet2 (*into); else a.newSet (*into); }
                else a.blk (b) = *into;
            }
            cb (ok, err);
        });
    }
}

class Panel : public juce::Component, private juce::Timer
{
public:
    Panel()
    {
        auto& L = live();
        auto lab = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); l.setColour (juce::Label::textColourId, juce::Colours::white); addAndMakeVisible (l); };
        lab (lOut, "MIDI out (to S-550 MIDI IN)"); lab (lIn, "MIDI in (from S-550 MIDI OUT)"); lab (lDev, "Device ID"); lab (lMode, "Transfer"); lab (lScope, "Block"); lab (lWhat, "Contents");
        outBox.addItemList (MidiLink::outputNames(), 1); inBox.addItem ("(none)", 1000); inBox.addItemList (MidiLink::inputNames(), 1); 
        outBox.setText (cfg.out, juce::dontSendNotification); inBox.setText (cfg.in.isEmpty() ? "(none)" : cfg.in, juce::dontSendNotification);
        for (int i = 1; i <= 16; ++i) devBox.addItem (juce::String (i), i);
        devBox.setSelectedId (cfg.dev + 1, juce::dontSendNotification);
        modeBox.addItem ("One way (DT1, 25 ms gaps)", 1); modeBox.addItem ("Handshake (WSD/DAT/ACK)", 2); modeBox.setSelectedId (cfg.handshake ? 2 : 1, juce::dontSendNotification);
        scopeBox.addItem ("Block I  (Patch 1-16, Tone 1-32)", 1); scopeBox.addItem ("Block II (Patch 17-32, Tone 33-64)", 2); scopeBox.setSelectedId (1, juce::dontSendNotification);
        wPatch.setButtonText ("Patches"); wTone.setButtonText ("Tones"); wWave.setButtonText ("Waves (about 6 min)");
        wPatch.setToggleState (true, juce::dontSendNotification); wTone.setToggleState (true, juce::dontSendNotification); wWave.setToggleState (true, juce::dontSendNotification);
        liveOut.setButtonText ("Live: program edits go to the S-550"); liveIn.setButtonText ("Live: S-550 panel edits come into the program");
        liveOut.setToggleState (cfg.liveOut, juce::dontSendNotification); liveIn.setToggleState (cfg.liveIn, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &outBox, &inBox, &devBox, &modeBox, &scopeBox, &wPatch, &wTone, &wWave, &wParams, &liveOut, &liveIn, &mergeBtn, &thruBtn, &muteBtn, &autoBtn, &panicBtn, &logBtn, &bar, &openBtn, &sendBtn, &recvBtn, &abortBtn, &status }) addAndMakeVisible (c);
        for (auto* c : std::initializer_list<juce::ToggleButton*> { &wPatch, &wTone, &wWave, &wParams, &liveOut, &liveIn, &mergeBtn, &thruBtn, &muteBtn, &autoBtn }) c->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        openBtn.setButtonText (L.link.isOpen() ? "Close ports" : "Open ports");
        sendBtn.setButtonText ("Send to S-550 (Bulk Dump)"); recvBtn.setButtonText ("Receive from S-550"); abortBtn.setButtonText ("Abort");
        status.setColour (juce::Label::textColourId, juce::Colours::yellow); status.setJustificationType (juce::Justification::topLeft);
        status.setText (L.link.isOpen() ? "Ports open." : "S-550: MIDI mode, Exclusive = On, same Device ID. Then open the ports.", juce::dontSendNotification);
        wParams.setButtonText ("Function / MIDI parameters (Master Tune, RX switches, Prog #; not Device ID)");
        thruBtn.setButtonText ("MIDI thru: keyboard, notes and controllers also play the S-550"); thruBtn.setToggleState (cfg.thru, juce::dontSendNotification);
        muteBtn.setButtonText ("Mute the internal sound while thru is on"); muteBtn.setToggleState (cfg.mute, juce::dontSendNotification);
        thruBtn.onClick = [this] { read(); applyThru(); }; muteBtn.onClick = [this] { read(); applyThru(); };
        autoBtn.setButtonText ("Open the ports automatically at start (if they were open)"); autoBtn.setToggleState (cfg.autoOpen, juce::dontSendNotification);
        autoBtn.onClick = [this] { read(); };
        panicBtn.setButtonText ("Panic (all notes off)");
        panicBtn.onClick = [] { auto& L = live().link; for (int c = 0; c < 16; ++c) { const uint8_t a[3] = { (uint8_t) (0xb0 | c), 123, 0 }, b[3] = { (uint8_t) (0xb0 | c), 120, 0 }; L.sendShort (a, 3); L.sendShort (b, 3); } };
        mergeBtn.setButtonText ("Handshake: one WSD for neighbouring data (fewer messages)"); mergeBtn.setToggleState (cfg.merge, juce::dontSendNotification);
        mergeBtn.onClick = [this] { read(); live().link.session.mergeRuns = cfg.merge; };
        logBtn.setButtonText ("Copy log");
        logBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (live().link.session.logText()); status.setText ("Log copied to the clipboard.", juce::dontSendNotification); };
        live().link.session.mergeRuns = cfg.merge;
        openBtn.onClick = [this] { toggle(); };
        sendBtn.onClick = [this] { run (true); };
        recvBtn.onClick = [this] { run (false); };
        abortBtn.onClick = [] { live().link.abort(); };
        liveOut.onClick = [this] { read(); if (cfg.liveOut) live().resync(); };
        liveIn.onClick = [this] { read(); };
        setSize (520, 644);
        bar.setPercentageDisplay (true);
        startTimerHz (10);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto row = [&] (juce::Component& l, juce::Component& c) { auto a = r.removeFromTop (22); l.setBounds (a); c.setBounds (r.removeFromTop (26)); r.removeFromTop (4); };
        row (lOut, outBox); row (lIn, inBox);
        auto a = r.removeFromTop (22); lDev.setBounds (a.removeFromLeft (180)); lMode.setBounds (a);
        auto b = r.removeFromTop (26); devBox.setBounds (b.removeFromLeft (170)); b.removeFromLeft (10); modeBox.setBounds (b); r.removeFromTop (4);
        row (lScope, scopeBox);
        lWhat.setBounds (r.removeFromTop (22));
        auto w = r.removeFromTop (24); wPatch.setBounds (w.removeFromLeft (100)); wTone.setBounds (w.removeFromLeft (100)); wWave.setBounds (w);
        wParams.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        liveOut.setBounds (r.removeFromTop (24)); liveIn.setBounds (r.removeFromTop (24)); mergeBtn.setBounds (r.removeFromTop (24)); thruBtn.setBounds (r.removeFromTop (24)); { auto m = r.removeFromTop (24); muteBtn.setBounds (m.removeFromLeft (300)); panicBtn.setBounds (m); } autoBtn.setBounds (r.removeFromTop (24)); r.removeFromTop (8);
        auto bt = r.removeFromTop (30); openBtn.setBounds (bt.removeFromLeft (120)); bt.removeFromLeft (8); abortBtn.setBounds (bt.removeFromRight (80)); bt.removeFromRight (8); logBtn.setBounds (bt.removeFromRight (100));
        r.removeFromTop (6); sendBtn.setBounds (r.removeFromTop (30)); r.removeFromTop (6); recvBtn.setBounds (r.removeFromTop (30)); r.removeFromTop (8);
        bar.setBounds (r.removeFromTop (22)); r.removeFromTop (6);
        status.setBounds (r);
    }
private:
    double prog = 0; juce::ProgressBar bar { prog };
    juce::int64 t0 = 0; bool recvMode = false;
    juce::Label lOut, lIn, lDev, lMode, lScope, lWhat, status;
    juce::ComboBox outBox, inBox, devBox, modeBox, scopeBox;
    juce::ToggleButton wPatch, wTone, wWave, wParams, liveOut, liveIn, mergeBtn, thruBtn, muteBtn, autoBtn;
    juce::TextButton openBtn, sendBtn, recvBtn, abortBtn, logBtn, panicBtn;

    void applyThru() { App::get().muteLocal = cfg.thru && cfg.mute && live().link.isOpen(); }
    void read()
    {
        cfg.out = outBox.getText(); cfg.in = inBox.getText() == "(none)" ? juce::String() : inBox.getText();
        cfg.dev = devBox.getSelectedId() - 1; cfg.handshake = modeBox.getSelectedId() == 2;
        cfg.merge = mergeBtn.getToggleState(); cfg.thru = thruBtn.getToggleState(); cfg.mute = muteBtn.getToggleState(); cfg.autoOpen = autoBtn.getToggleState(); cfg.liveOut = liveOut.getToggleState(); cfg.liveIn = liveIn.getToggleState();
        saveCfg();
    }
    void toggle()
    {
        auto& L = live().link;
        if (L.isOpen()) { L.close(); applyThru(); cfg.wasOpen = false; saveCfg(); openBtn.setButtonText ("Open ports"); status.setText ("Closed.", juce::dontSendNotification); return; }
        read();
        if (L.open (cfg.out, cfg.in, cfg.dev, cfg.handshake)) { live().resync(); applyThru(); cfg.wasOpen = true; saveCfg(); openBtn.setButtonText ("Close ports"); status.setText (cfg.in.isEmpty() ? "Output open (no input: dump works one way only, no receive / panel edits)." : "Ports open.", juce::dontSendNotification); }
        else status.setText (L.lastError, juce::dontSendNotification);
    }
    void run (bool toDevice)
    {
        auto& L = live().link;
        read();
        if (! L.isOpen()) { status.setText ("Open the ports first.", juce::dontSendNotification); return; }
        const int b = scopeBox.getSelectedId() - 1;
        sysex::PlanOptions o; o.patches = wPatch.getToggleState(); o.tones = wTone.getToggleState(); o.waves = wWave.getToggleState();
        auto cb = [this, alive = juce::Component::SafePointer<Panel> (this), toDevice] (bool ok, std::string err)
        {
            prog = ok ? 1.0 : prog;
            if (alive != nullptr) status.setText (ok ? (toDevice ? "Dump finished." : "Received. The data is in the program now.") : juce::String ("Stopped: ") + err, juce::dontSendNotification);
        };
        t0 = juce::Time::currentTimeMillis(); prog = 0; recvMode = ! toDevice;
        status.setText (toDevice ? "Sending ..." : "Requesting ...", juce::dontSendNotification);
        const bool withParams = wParams.getToggleState();
        beginTransfer (toDevice, b, o, withParams, withParams, false, cb);
    }
    void timerCallback() override
    {
        auto& L = live().link;
        if (L.busy())
        {
            const double f = recvMode ? (L.session.totalBytes.load() ? (double) L.session.doneBytes.load() / (double) L.session.totalBytes.load() : 0.0)
                                      : (L.total.load() ? (double) L.done.load() / (double) L.total.load() : 0.0);
            prog = juce::jlimit (0.0, 1.0, f);
            const double el = (double) (juce::Time::currentTimeMillis() - t0) / 1000.0;
            auto mmss = [] (double sec) { const int t = (int) (sec + 0.5); return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); };
            juce::String txt = juce::String (recvMode ? "Receiving" : "Sending") + "   elapsed " + mmss (el);
            if (f > 0.01) txt << "   remaining about " << mmss (el * (1.0 - f) / f);
            status.setText (txt, juce::dontSendNotification);
        }
        else if (L.isOpen() && status.getText().startsWith ("Ports"))
            status.setText ("Ports open.  Live: sent " + juce::String (live().sent.load()) + ", received " + juce::String (live().received.load()) + " parameters.", juce::dontSendNotification);
    }
};
}

void initMidiLink()
{
    if (auto* f = settings())
    {
        cfg.out = f->getValue ("out"); cfg.in = f->getValue ("in"); cfg.dev = f->getIntValue ("dev", 0) & 15;
        cfg.handshake = f->getBoolValue ("hs", false); cfg.liveOut = f->getBoolValue ("liveOut", true); cfg.liveIn = f->getBoolValue ("liveIn", true); cfg.merge = f->getBoolValue ("merge", false); cfg.thru = f->getBoolValue ("thru", true); cfg.mute = f->getBoolValue ("mute", false);
        cfg.autoOpen = f->getBoolValue ("auto", true); cfg.wasOpen = f->getBoolValue ("wasOpen", false);
    }
    if (cfg.autoOpen && cfg.wasOpen && cfg.out.isNotEmpty())          // the ports were open when the program was closed: open them again
    {
        auto& L = live().link;
        if (! L.isOpen() && L.open (cfg.out, cfg.in, cfg.dev, cfg.handshake)) { live().resync(); App::get().muteLocal = cfg.thru && cfg.mute; }
    }
    App::get().openMidiLinkDialog = [] { showMidiLink(); };
    App::get().midiXfer = [] (bool save, int cmd, std::string& err) -> bool
    {
        auto& a = App::get();
        auto& L = live().link;
        if (! L.isOpen()) { err = "MIDI Link not open"; return false; }
        if (L.busy()) { err = "Busy"; return false; }
        sysex::PlanOptions o; o.patches = o.tones = o.waves = false;
        bool func = false, midi = false; int b = 0;
        if (cmd == 0 || cmd == 1) { b = cmd; o.patches = o.tones = o.waves = true; }
        else if (cmd == 2 || cmd == 3) { b = cmd - 2; o.patches = o.tones = true; }
        else if (cmd == 4) func = true;
        else midi = true;
        a.xferState = 1; a.xferFrac = 0; a.xferErr.clear();
        beginTransfer (save, b, o, func, midi, true, [] (bool ok, std::string e) { auto& x = App::get(); x.xferErr = e; x.xferState = ok ? 2 : 3; x.uiDirty = true; });
        return true;
    };
    App::get().midiOut = [] (const uint8_t* m, int n)
    {
        auto& L = live().link;
        if (cfg.thru && L.isOpen() && ! L.busy()) L.sendShort (m, n);
    };
}

void showMidiLink()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Panel());
    o.dialogTitle = "S-550 MIDI link (SysEx)";
    o.dialogBackgroundColour = juce::Colour (0xff202428);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
}
