#include "PluginProcessor.h"
#include "Screen/ScreenEditor.h"
#include "Screen/App.h"

S5Processor::S5Processor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Audio In", juce::AudioChannelSet::stereo(), true)
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)    // sampling input: route any track / channel here in the host
                                .withOutput ("Out", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Out 1", juce::AudioChannelSet::stereo(), false)       // the eight jacks of the S-550: off until the host switches them on
                                .withOutput ("Out 2", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 3", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 4", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 5", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 6", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 7", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Out 8", juce::AudioChannelSet::stereo(), false)),
      scopeTmp (1024)
{
    auto& a = s5::App::get();
    a.soundOut = [this] (const uint8_t* m, int n) { pushUiMessage (m, n); };
    engine.setBlock (a.block);
    engine.setVoiceMode (a.voiceMode);
    for (int i = 0; i < 8; ++i) engine.setPart (i, partActive (i) ? a.partCh[i] - 1 : -1, a.partPatch[i]);
    startTimer (25);
}

S5Processor::~S5Processor()
{
    stopTimer();
    s5::App::get().soundOut = nullptr;
}

void S5Processor::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate;
    s5::App::get().hostRate = sampleRate;
    engine.prepare (sampleRate);
}

bool S5Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    const auto in = l.getMainInputChannelSet();
    if (l.inputBuses.size() > 1 && ! (l.inputBuses[1].isDisabled() || l.inputBuses[1] == juce::AudioChannelSet::stereo() || l.inputBuses[1] == juce::AudioChannelSet::mono())) return false;
    for (int b = 1; b < l.outputBuses.size(); ++b)                      // Out 1-8: stereo or off
        if (! (l.outputBuses[b].isDisabled() || l.outputBuses[b] == juce::AudioChannelSet::stereo())) return false;
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void S5Processor::pushUiMessage (const uint8_t* m, int n)       // message thread -> audio thread
{
    if (n < 1 || n > 3) return;
    int s1, z1, s2, z2;
    toAudioFifo.prepareToWrite (1, s1, z1, s2, z2);
    if (z1 < 1) return;
    Msg& g = toAudio[s1];
    g.b[0] = (uint8_t) n; g.b[1] = m[0]; g.b[2] = n > 1 ? m[1] : 0; g.b[3] = n > 2 ? m[2] : 0;
    toAudioFifo.finishedWrite (1);
}

void S5Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const auto t0 = juce::Time::getHighResolutionTicks();

    // sampling input: the Sidechain bus when the host has switched it on (any track / channel), otherwise the plain input
    {
        const float* in = nullptr;
        if (monoIn.size() < 8192) monoIn.assign (8192, 0.0f);
        for (int bi = 1; bi >= 0 && ! in; --bi)
        {
            auto* bus = getBus (true, bi);
            if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 1 || n > 8192) continue;
            auto ib = getBusBuffer (buffer, true, bi);
            const float* l = ib.getReadPointer (0);
            const float* r = ib.getNumChannels() > 1 ? ib.getReadPointer (1) : l;
            const float rg = (float) s5::App::get().recLevel.load() / 100.0f;
            for (int i = 0; i < n; ++i) monoIn[(size_t) i] = 0.5f * rg * (l[i] + r[i]);
            in = monoIn.data();
        }
        if (in != nullptr)
        {
            int p = inPos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) { inRing[p] = in[i]; p = (p + 1) & (kRing - 1); }
            inPos.store (p, std::memory_order_release);
            s5::App::get().recFeed (in, n, rate);
        }
    }

    { const int fc = s5::App::get().floppyClicks.exchange (0); if (fc > 0) engine.floppyClick (fc); }

    // notes played on the screen keyboard
    {
        int s1, z1, s2, z2;
        toAudioFifo.prepareToRead (toAudioFifo.getNumReady(), s1, z1, s2, z2);
        const bool mute = s5::App::get().muteLocal.load();
        for (int i = 0; i < z1 && ! mute; ++i) engine.midi (toAudio[s1 + i].b + 1, toAudio[s1 + i].b[0]);
        for (int i = 0; i < z2 && ! mute; ++i) engine.midi (toAudio[s2 + i].b + 1, toAudio[s2 + i].b[0]);
        toAudioFifo.finishedRead (z1 + z2);
    }

    // MIDI from the host, sample accurate: render up to each event
    int done = 0;
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float>* dummy = nullptr; (void) dummy;
    static thread_local float tmpL[8192], tmpR[8192];
    if (auxTmp.size() < 16 * 8192) auxTmp.assign (16 * 8192, 0.0f);
    float* auxL[8] = {}; float* auxR[8] = {};                        // Out 1-8 that the host has switched on
    for (int b = 1; b < getBusCount (false) && b <= 8; ++b)
        if (auto* bus = getBus (false, b))
            if (bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                auto ab = getBusBuffer (buffer, false, b);
                auxL[b - 1] = ab.getWritePointer (0); auxR[b - 1] = ab.getWritePointer (1);
            }
    auto renderTo = [&] (int from, int count)
    {
        while (count > 0)
        {
            const int c = juce::jmin (count, 8192);
            float* oL[8] = {}; float* oR[8] = {};
            for (int o = 0; o < 8; ++o) if (auxL[o]) { oL[o] = &auxTmp[(size_t) (o * 2) * 8192]; oR[o] = &auxTmp[(size_t) (o * 2 + 1) * 8192]; }
            engine.render (tmpL, tmpR, c, oL, oR);
            for (int o = 0; o < 8; ++o) if (auxL[o]) { std::memcpy (auxL[o] + from, oL[o], sizeof (float) * (size_t) c); std::memcpy (auxR[o] + from, oR[o], sizeof (float) * (size_t) c); }
            if (r) { std::memcpy (l + from, tmpL, sizeof (float) * (size_t) c); std::memcpy (r + from, tmpR, sizeof (float) * (size_t) c); }
            else for (int i = 0; i < c; ++i) l[from + i] = 0.5f * (tmpL[i] + tmpR[i]);
            from += c; count -= c;
        }
    };
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (done, n, meta.samplePosition);
        if (pos > done) { renderTo (done, pos - done); done = pos; }
        const auto* d = meta.data;
        const int len = meta.numBytes;
        if (len >= 1 && len <= 3)
        {
            if (! s5::App::get().muteLocal.load()) engine.midi (d, len);
            int s1, z1, s2, z2;                                                  // copy to the screen (MIDI Monitor, key lights)
            toUiFifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 > 0)
            {
                Msg& g = toUi[s1];
                g.b[0] = (uint8_t) len; g.b[1] = d[0]; g.b[2] = len > 1 ? d[1] : 0; g.b[3] = len > 2 ? d[2] : 0;
                toUiFifo.finishedWrite (1);
            }
        }
    }
    if (done < n) renderTo (done, n - done);
    {   // master volume on the main output and on all individual outputs that the host uses
        const float g = (float) s5::App::get().volume.load() / 100.0f;
        for (int b = 0; b < getBusCount (false); ++b)
            if (auto* bus = getBus (false, b))
                if (bus->isEnabled() && bus->getNumberOfChannels() > 0)
                {
                    auto ob = getBusBuffer (buffer, false, b);
                    if (b == 0) ob.applyGainRamp (0, n, lastVolume, g); else ob.applyGain (g);
                }
        lastVolume = g;
    }
    {   // how much of the time available for this block was used
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        const float load = (float) (secs / ((double) n / juce::jmax (1.0, rate)));
        loadAvg.store (loadAvg.load() * 0.98f + load * 0.02f);
        if (load > loadPeak.load()) loadPeak.store (load);
        if (load > 0.5f) busyCount.fetch_add (1);
    }
}

void S5Processor::timerCallback()
{
    auto& a = s5::App::get();
    // MIDI -> screen
    {
        int s1, z1, s2, z2;
        toUiFifo.prepareToRead (toUiFifo.getNumReady(), s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) { a.midiFeed (toUi[s1 + i].b + 1, toUi[s1 + i].b[0]); if (a.midiOut) a.midiOut (toUi[s1 + i].b + 1, toUi[s1 + i].b[0]); }
        for (int i = 0; i < z2; ++i) { a.midiFeed (toUi[s2 + i].b + 1, toUi[s2 + i].b[0]); if (a.midiOut) a.midiOut (toUi[s2 + i].b + 1, toUi[s2 + i].b[0]); }
        toUiFifo.finishedRead (z1 + z2);
        if (z1 + z2 > 0) a.uiDirty = true;
    }
    if (a.recWatch.exchange (false) || a.rec.state.load() != 0) { a.recPoll(); a.uiDirty = true; }
    // audio input -> Wave Scope
    if (a.scopeRun)
    {
        const int p = inPos.load (std::memory_order_acquire);
        for (int i = 0; i < 1024; ++i) scopeTmp[(size_t) i] = inRing[(p - 1024 + i + kRing) & (kRing - 1)];
        a.scopeFeed (scopeTmp.data(), 1024);
        a.uiDirty = true;
    }
    // edits on the screen -> engine
    engine.setVoiceMode (a.voiceMode);
    for (int i = 0; i < 8; ++i) engine.setPart (i, partActive (i) ? a.partCh[i] - 1 : -1, a.partPatch[i]);
    engine.setAudition (a.auditionTone, a.auditionPatch);
    {   // a new snapshot only when a tone or patch really changed (it was rebuilt 40 times a second before)
        const auto h = paramHash();
        if (h != lastParamHash) { lastParamHash = h; engine.setParams (a.block); engine.setParams2 (a.block2); }
    }
    if ((++ticks & 3) == 0)
    {
        const auto h = waveHash();
        if (h != lastWaveHash) { lastWaveHash = h; engine.setBlock (a.block); engine.setBlock2 (a.block2); }
    }
}

uint64_t S5Processor::paramHash() const
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const auto* b : { &s5::App::get().block, &s5::App::get().block2 })
    {
        for (const auto& t : b->tones)
        {
            for (char c : t.name) mix ((uint8_t) c);
            mix (t.f1); mix (t.sourceTone); mix (t.subTone); mix (t.khz15); mix (t.origKey); mix (t.waveBank); mix (t.waveStart); mix (t.waveLen);
            mix (t.startPoint); mix (t.endPoint); mix (t.loopPoint); mix (t.loopMode);
            for (uint8_t v : t.rest) mix (v);
        }
        for (const auto& p : b->patches) for (uint8_t v : p.raw) mix (v);
    }
    return h;
}

uint64_t S5Processor::waveHash() const
{
    uint64_t h = 1469598103934665603ull ^ ((uint64_t) s5::App::get().waveRev * 0x9e3779b97f4a7c15ull);
    for (const auto* b : { &s5::App::get().block, &s5::App::get().block2 })
        for (const auto& w : b->wave)
            for (size_t i = 0; i < w.size(); i += 7) h = (h ^ (uint16_t) w[i]) * 1099511628211ull;
    return h;
}

juce::AudioProcessorEditor* S5Processor::createEditor() { return new S5ScreenEditor (*this); }

void S5Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (wrapperType == wrapperType_Standalone) return;      // standalone always starts empty; only the plugin keeps its state in the project
    auto& a = s5::App::get();
    const auto img = a.block.serialize();
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (0x53354d31);                     // "S5M1"
    out.writeInt (a.voiceMode);
    out.writeInt ((int) img.size());
    out.write (img.data(), img.size());
    out.writeInt (a.recLevel.load());
    out.writeInt (a.volume.load());
}

void S5Processor::setStateInformation (const void* data, int size)
{
    if (wrapperType == wrapperType_Standalone) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (size < 12 || in.readInt() != 0x53354d31) return;
    auto& a = s5::App::get();
    a.voiceMode = in.readInt();
    const int len = in.readInt();
    if (len <= 0 || len > size) return;
    std::vector<uint8_t> img ((size_t) len);
    in.read (img.data(), len);
    if (in.getNumBytesRemaining() >= 8) { a.recLevel = juce::jlimit (0, 127, in.readInt()); a.volume = juce::jlimit (0, 127, in.readInt()); }
    s5::data::Block b;
    if (b.parse (img.data(), img.size()) == s5::data::Block::Result::Ok) { a.block = std::move (b); engine.setBlock (a.block); lastWaveHash = waveHash(); a.uiDirty = true; }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new S5Processor(); }
