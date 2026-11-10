#include "MidiLink.h"

namespace s5
{
MidiLink::MidiLink()
{
    session.send = [this] (const uint8_t* p, size_t n)
    {
        if (out) out->sendMessageNow (juce::MidiMessage::createSysExMessage (p + 1, (int) n - 2));   // JUCE adds F0/F7
    };
    session.progress = [this] (int d, int t, const std::string&) { done = d; if (t) total = t; };
}
MidiLink::~MidiLink() { close(); }

juce::StringArray MidiLink::outputNames() { juce::StringArray s; for (auto& d : juce::MidiOutput::getAvailableDevices()) s.add (d.name); return s; }
juce::StringArray MidiLink::inputNames()  { juce::StringArray s; for (auto& d : juce::MidiInput::getAvailableDevices()) s.add (d.name); return s; }

bool MidiLink::open (const juce::String& outName, const juce::String& inName, int deviceId, bool handshake)
{
    close();
    session.deviceId = deviceId & 15; session.handshake = handshake;
    for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name == outName) out = juce::MidiOutput::openDevice (d.identifier);
    for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == inName) { in = juce::MidiInput::openDevice (d.identifier, this); if (in) in->start(); }
    if (! out) { lastError = "MIDI output not found"; return false; }
    lastError.clear();
    return true;
}

void MidiLink::close()
{
    session.cancel();
    if (worker.joinable()) worker.join();
    if (in) in->stop();
    in.reset(); out.reset(); running = false;
}

void MidiLink::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isSysEx()) return;
    std::vector<uint8_t> v { 0xf0 };
    v.insert (v.end(), m.getSysExData(), m.getSysExData() + m.getSysExDataSize());
    v.push_back (0xf7);
    session.receive (v.data(), v.size());
}

void MidiLink::sendShort (const uint8_t* m, int n)
{
    if (out && n >= 1 && n <= 3 && m[0] >= 0x80 && m[0] < 0xf0) out->sendMessageNow (juce::MidiMessage (m, n));
}

void MidiLink::job (std::function<bool (std::string&)> fn, std::function<void (bool, std::string)> cb)
{
    if (running || ! isOpen()) { if (cb) cb (false, "not ready"); return; }
    if (worker.joinable()) worker.join();
    running = true; done = 0; total = 0;
    worker = std::thread ([this, fn, cb]
    {
        std::string err; const bool ok = fn (err);
        running = false;
        juce::MessageManager::callAsync ([cb, ok, err] { if (cb) cb (ok, err); });
    });
}

static void join (std::thread& t) { if (t.joinable()) t.join(); }

void MidiLink::dumpBlock (const data::Block& b, sysex::PlanOptions o, std::function<void (bool, std::string)> cb)
{
    if (running || ! isOpen()) { if (cb) cb (false, "not ready"); return; }
    join (worker); running = true; done = 0; total = 0;
    worker = std::thread ([this, b, o, cb]   // copy: the job outlives the call
    {
        std::string err; const bool ok = session.sendBlock (b, o, err);
        running = false;
        juce::MessageManager::callAsync ([cb, ok, err] { if (cb) cb (ok, err); });
    });
}

void MidiLink::fetchBlock (data::Block& into, sysex::PlanOptions o, std::function<void (bool, std::string)> cb)
{
    if (running || ! isOpen()) { if (cb) cb (false, "not ready"); return; }
    join (worker); running = true; done = 0; total = 0;
    worker = std::thread ([this, &into, o, cb]
    {
        std::string err; const bool ok = session.requestBlock (into, o, err);
        running = false;
        juce::MessageManager::callAsync ([cb, ok, err] { if (cb) cb (ok, err); });
    });
}
}
