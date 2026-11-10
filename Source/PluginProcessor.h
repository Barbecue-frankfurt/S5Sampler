#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "Engine/Engine.h"
#include "Screen/App.h"

// Plugin / standalone shell around the S-550 engine.  The disk contents live in s5::App (edited by the screen);
// a timer on the message thread hands changes to the engine, MIDI and the audio input travel through lock-free FIFOs.
class S5Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    S5Processor();
    ~S5Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "S5 Sampler"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // load of the audio callback and note statistics, shown under the screen (for finding dropouts)
    float cpuPeak() const { return loadPeak.load(); }
    float cpuAvg() const { return loadAvg.load(); }
    int busyBlocks() const { return busyCount.load(); }
    int steals() const { return engine.stealCount(); }
    int drops() const { return engine.dropCount(); }
    void resetStats() { loadPeak = 0.f; busyCount = 0; engine.resetCounters(); }

private:
    std::atomic<float> loadPeak { 0.f }, loadAvg { 0.f };
    std::atomic<int> busyCount { 0 };
    void timerCallback() override;
    void pushUiMessage (const uint8_t* m, int n);
    uint64_t waveHash() const;
    uint64_t paramHash() const;
    uint64_t lastParamHash = 0;
    // a part sounds only when its MIDI channel is on and its patch exists on the disk (a patch without a name / level is empty)
    bool partActive (int i) const
    {
        const auto& a = s5::App::get();
        if (a.partCh[i] < 1) return false;
        const bool two = a.partPatch[i] >= 16;
        if (! (two ? a.linked2 : a.linked)) return ! two;
        const auto& pt = (two ? a.block2 : a.block).patches[a.partPatch[i] & 15];
        bool named = false;
        for (int k = 0; k < 12; ++k) if (pt.raw[k] > 32 && pt.raw[k] < 127) named = true;
        return named && pt.raw[237] != 0;
    }

    s5::engine::Engine engine;
    static constexpr int kFifo = 1024;
    struct Msg { uint8_t b[4]; };
    Msg toAudio[kFifo], toUi[kFifo];
    juce::AbstractFifo toAudioFifo { kFifo }, toUiFifo { kFifo };
    static constexpr int kRing = 4096;
    float inRing[kRing] = {};
    std::atomic<int> inPos { 0 };
    std::vector<float> scopeTmp;
    float lastVolume = 1.0f;                    // master volume of the previous block (ramp)
    std::vector<float> monoIn = std::vector<float> (8192, 0.0f);                  // sampling input (mono mix of the selected input bus)
    std::vector<float> auxTmp;                  // 8 stereo outputs x 8192 samples
    uint64_t lastWaveHash = 0;
    int ticks = 0;
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S5Processor)
};
