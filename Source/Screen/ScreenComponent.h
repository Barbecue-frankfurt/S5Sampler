#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PixelScreen.h"
#include <functional>

// Shows an s5::Screen (640x450) enlarged by a whole number (pixel doubling), no filtering.
class ScreenComponent : public juce::Component, private juce::Timer
{
public:
    ScreenComponent();

    s5::Screen& screen() { return scr; }
    void frameChanged();                       // call after drawing into screen()
    void setScale (int s);                     // 1, 2, 3 ...
    int getScale() const { return scale; }
    juce::Point<int> toScreenPixel (juce::Point<int> p) const;   // component -> 640x450 pixel

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // handlers get 640x450 screen pixels and return true when the picture must be redrawn
    std::function<bool (int, int, int)> onMouseDown;
    std::function<bool (int, int)> onMouseMove;
    std::function<bool()> onMouseExit;
    std::function<bool()> onMouseUp;             // button released: ends dragging of graph points / held keys
    std::function<bool (int, int, int)> onWheel;
    std::function<bool()> wantsKeys;             // a name box is being edited: the computer keyboard types into it
    std::function<bool (int)> onKey;             // code: 27 Esc, 13 Return, 37/39 arrows, 8 Backspace, 46 Delete, 32..126 characters
    std::function<void (bool)> onShift;          // Shift state, reported before every mouse action
    std::function<int (int, int, int)> onHold;   // x, y, dir -> direction to repeat while the button is held (0 = none)

private:
    void timerCallback() override;
    int holdDir = 0, holdX = 0, holdY = 0, holdTicks = 0;
    s5::Screen scr;
    juce::Image image { juce::Image::ARGB, s5::kW, s5::kH, false };
    int scale = 2;
};
