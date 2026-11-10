#include "ScreenEditor.h"
#include "App.h"
#include "../Platform/JuceFiles.h"
#include "../Platform/MacFloppy.h"
#include "../Platform/HdOptions.h"
#include "../Version.h"
#include "../Platform/MidiLinkUi.h"

namespace
{
// Replace the in-memory services by the real ones the first time an editor opens.
void installServices()
{
    static bool done = false;
    if (done) return;
    done = true;
    auto& a = s5::App::get();
    a.files = std::make_unique<s5::JuceFiles>();
    a.drive = std::make_unique<s5::MacFloppy>();
    s5::initMidiLink();
    s5::initHdSettings();                      // hard disk images of the SCSI IDs (Options menu)
}
}

S5ScreenEditor::S5ScreenEditor (S5Processor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    installServices();
    ui.setPage ("Standard");
    addAndMakeVisible (screenView);
    screenView.setScale (s5::storedDisplayScale());
    inHost = processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
    if (inHost)                                              // plug-in in a host: no title bar of ours, so a thin strip under the screen carries the Options button
    {
        optionsBtn.setTriggeredOnMouseDown (true);
        optionsBtn.onClick = [this] { showOptionsMenu(); };
        addAndMakeVisible (optionsBtn);
        auto setup = [this] (juce::Slider& s, std::atomic<int>& v, juce::Colour c)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setRange (0, 127, 1);
            s.setDoubleClickReturnValue (true, 100);
            s.setValue (v.load(), juce::dontSendNotification);
            s.setColour (juce::Slider::trackColourId, c);
            s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff46464a));
            s.setColour (juce::Slider::thumbColourId, juce::Colour (0xfff5f5f5));
            s.onValueChange = [this, &s, &v] { v = (int) s.getValue(); repaint (0, screenView.getHeight(), getWidth(), stripHeight()); };
            addAndMakeVisible (s);
        };
        setup (recSlider, s5::App::get().recLevel, juce::Colour (0xffe63c32));
        setup (volSlider, s5::App::get().volume, juce::Colour (0xff46bee6));
    }
    setSize (screenView.getWidth(), screenView.getHeight() + stripHeight());
    setResizable (false, false);
    screenView.onMouseDown = [this] (int x, int y, int d) { const bool r = ui.mouseDown (x, y, d); if (r) ui.render (screenView.screen()); return r; };
    screenView.onMouseUp = [this] { const bool r = ui.mouseUp(); if (r) ui.render (screenView.screen()); return r; };
    screenView.onShift = [this] (bool b) { ui.setBig (b); };
    screenView.onHold = [this] (int x, int y, int d) { return ui.holdDir (x, y, d); };
    screenView.onMouseMove = [this] (int x, int y) { const bool r = ui.mouseMove (x, y); if (r) ui.render (screenView.screen()); return r; };
    screenView.onMouseExit = [this] { const bool r = ui.mouseLeave(); if (r) ui.render (screenView.screen()); return r; };
    screenView.wantsKeys = [this] { return ui.wantsKeys(); };
    screenView.onKey = [this] (int c) { const bool r = ui.key (c); if (r) ui.render (screenView.screen()); return r; };
    screenView.onWheel     = [this] (int x, int y, int n) { const bool r = ui.wheel (x, y, n); if (r) ui.render (screenView.screen()); return r; };
    redraw();
    startTimer (50);
}

S5ScreenEditor::~S5ScreenEditor()
{
    stopTimer();
    if (auto* par = optionsBtn.getParentComponent()) par->removeChildComponent (&optionsBtn);
    if (origOptions != nullptr) origOptions->setVisible (true);
}

// The JUCE standalone window has an "Options" button in its title bar (Audio/MIDI settings, state). Its menu cannot be extended,
// so an identical button is laid over it: its menu has the hard disk entry, and the original menu stays reachable below it.
void S5ScreenEditor::hookOptionsButton()
{
    if (hooked || ++hookTries > 60) return;
    auto* win = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());
    if (win == nullptr) return;                              // plug-in inside a host: no title bar of ours
    for (auto* c : win->getChildren())
        if (auto* tb = dynamic_cast<juce::Button*> (c))
            if (tb != &optionsBtn && tb->getButtonText() == "Options") { origOptions = tb; break; }
    hooked = true;
    if (origOptions == nullptr) return;
    optionsBtn.setBounds (origOptions->getBounds());
    optionsBtn.setTriggeredOnMouseDown (true);
    optionsBtn.onClick = [this] { showOptionsMenu(); };
    juce::Component& base = *win;                            // addAndMakeVisible is hidden in DocumentWindow
    base.addAndMakeVisible (optionsBtn);
    origOptions->setVisible (false);
}

void S5ScreenEditor::showOptionsMenu()
{
    {
        juce::PopupMenu m;
        m.addItem (100, juce::String ("S5 Sampler, Stand ") + S5_BUILD, false);       // version line (not selectable)
        m.addItem (102, juce::String::formatted ("CPU %d%% (Spitze %d%%, %d Bloecke >50%%), geklaut %d, verworfen %d",
                 (int) (processor.cpuAvg() * 100), (int) (processor.cpuPeak() * 100), processor.busyBlocks(), processor.steals(), processor.drops()), false);
        m.addItem (101, "Zaehler zuruecksetzen (CPU, geklaute Stimmen)");
        m.addSeparator();
        m.addItem (1, "Hard disk images (SCSI) ...");
        m.addItem (3, "Create Hard disk Image ...");
        m.addItem (4, "S-550 MIDI link (SysEx) ...");
        m.addSeparator();
        m.addItem (5, "Display 1x (small)", true, screenView.getScale() == 1);
        m.addItem (6, "Display 2x (large)", true, screenView.getScale() == 2);
        m.addSeparator();
        if (! inHost) m.addItem (2, "Audio/MIDI settings, state ...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsBtn),
                         [alive = juce::Component::SafePointer<juce::Component> (this), this] (int r)
                         {
                             if (alive == nullptr) return;
                             if (r == 101) { processor.resetStats(); repaint(); }
                             else if (r == 1) s5::showHdOptions();
                             else if (r == 3) s5::showCreateHdImage();
                             else if (r == 4) s5::showMidiLink();
                             else if (r == 5) setDisplayScale (1);
                             else if (r == 6) setDisplayScale (2);
                             else if (r == 2 && origOptions != nullptr) origOptions->triggerClick();
                         });
    }
}

void S5ScreenEditor::setDisplayScale (int sc)
{
    if (sc == screenView.getScale()) return;
    s5::storeDisplayScale (sc);
    screenView.setScale (sc);
    setSize (screenView.getWidth(), screenView.getHeight() + stripHeight());      // the standalone window follows the editor size
    redraw();
}

void S5ScreenEditor::timerCallback()
{
    if (! inHost && ! hooked) hookOptionsButton();
    else if (! inHost && origOptions != nullptr && optionsBtn.getBounds() != origOptions->getBounds()) optionsBtn.setBounds (origOptions->getBounds());
    if (ui.tick (50)) redraw();
    if (inHost && (++statTick % 10) == 0) repaint (0, screenView.getHeight(), screenView.getWidth(), stripHeight());     // statistics in the strip
}

void S5ScreenEditor::redraw()
{
    ui.render (screenView.screen());
    screenView.frameChanged();
}

void S5ScreenEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    if (inHost)                                                  // strip: REC LEVEL, VOLUME, CPU (aligned with the blue screen), the version is in the Options menu
    {
        const int y = screenView.getHeight(), h = stripHeight(), L = lx();
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffebebeb));
        g.drawText ("REC LEVEL", L, y, 70, h, juce::Justification::centredLeft);
        g.drawText ("VOLUME", L + 205, y, 70, h, juce::Justification::centredLeft);
        g.setFont (12.0f);
        g.setColour (juce::Colour (0xffc8c8c8));
        g.drawText (juce::String (s5::App::get().recLevel.load()), L + 72 + 84 + 8, y, 36, h, juce::Justification::centredLeft);
        g.drawText (juce::String (s5::App::get().volume.load()), L + 277 + 84 + 8, y, 36, h, juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String::formatted ("CPU %d%%", (int) (processor.cpuAvg() * 100)), L + 410, y, 80, h, juce::Justification::centredLeft);
    }
}

void S5ScreenEditor::resized()
{
    screenView.setBounds (0, 0, screenView.getWidth(), screenView.getHeight());
    if (inHost)
    {
        const int sc = screenView.getWidth() / 640, L = lx(), R = 624 * sc, y = screenView.getHeight();
        optionsBtn.setBounds (R - 96, y + 2, 96, stripHeight() - 4);
        recSlider.setBounds (L + 72, y + 2, 84, stripHeight() - 4);
        volSlider.setBounds (L + 277, y + 2, 84, stripHeight() - 4);
    }
}
