#pragma once
// MIDI ports to a real S-550 (JUCE). Prepared, not yet tested against hardware.
#include "../Midi/SysEx.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <thread>

namespace s5
{
class MidiLink : private juce::MidiInputCallback
{
public:
    MidiLink();
    ~MidiLink() override;
    sysex::Session session;

    static juce::StringArray outputNames();
    static juce::StringArray inputNames();
    bool open (const juce::String& outName, const juce::String& inName, int deviceId, bool handshake);
    void close();
    bool isOpen() const { return out != nullptr; }
    std::string lastError;

    // background jobs (one at a time); done() runs on the message thread
    bool busy() const { return running; }
    void dumpBlock (const data::Block& b, sysex::PlanOptions o, std::function<void (bool, std::string)> done);
    void fetchBlock (data::Block& into, sysex::PlanOptions o, std::function<void (bool, std::string)> done);
    void job (std::function<bool (std::string&)> fn, std::function<void (bool, std::string)> done);   // any blocking transfer on the worker thread
    void sendShort (const uint8_t* m, int n);          // one channel message (notes, controllers, program change ...)
    void abort() { session.cancel(); }
    std::atomic<int> done { 0 }, total { 0 };

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    std::unique_ptr<juce::MidiOutput> out;
    std::unique_ptr<juce::MidiInput> in;
    std::thread worker;
    std::atomic<bool> running { false };
};
}
