#pragma once
#include "../PluginProcessor.h"
#include "ScreenComponent.h"
#include "UiState.h"

// Editor that shows the sampler's S-550 style monitor screen.
class S5ScreenEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit S5ScreenEditor (S5Processor&);
    ~S5ScreenEditor() override;
    void resized() override;

private:
    void redraw();
    void setDisplayScale (int s);                   // 1 = small (640x450), 2 = large (pixel doubling)
    void hookOptionsButton();                       // Standalone: the title bar's Options menu gets the entry for the hard disk images
    void showOptionsMenu();                         // the same menu from the title bar (Standalone) or the strip under the screen (plug-in)
    int lx() const { return 16 * (screenView.getWidth() / 640); }     // left edge of the blue screen
    int stripHeight() const { return inHost ? 24 : 0; }
    void timerCallback() override;
    void paint (juce::Graphics& g) override;

    S5Processor& processor;
    ScreenComponent screenView;
    s5::UiState ui;
    juce::TextButton optionsBtn { "Options" };
    juce::Slider recSlider, volSlider;               // strip under the screen: REC LEVEL and VOLUME (0-127)
    juce::Component::SafePointer<juce::Button> origOptions;
    int hookTries = 0, statTick = 0;
    bool hooked = false, inHost = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S5ScreenEditor)
};
