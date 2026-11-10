#include "ScreenComponent.h"
#include "App.h"
#include <vector>

ScreenComponent::ScreenComponent()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setSize (s5::kW * scale, s5::kH * scale);
}

void ScreenComponent::setScale (int s)
{
    scale = juce::jmax (1, s);
    setSize (s5::kW * scale, s5::kH * scale);
}

void ScreenComponent::frameChanged()
{
    std::vector<uint32_t> rgb ((size_t) s5::kW * s5::kH);
    scr.toRgb (rgb.data());
    juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < s5::kH; ++y)
    {
        auto* line = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
        for (int x = 0; x < s5::kW; ++x)
        {
            const uint32_t c = rgb[(size_t) y * s5::kW + x];
            line[x].setARGB (255, (uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c);
        }
    }
    repaint();
}

juce::Point<int> ScreenComponent::toScreenPixel (juce::Point<int> p) const
{
    return { p.x / scale, p.y / scale };
}

void ScreenComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // nearest neighbour
    g.drawImage (image, 0, 0, s5::kW * scale, s5::kH * scale, 0, 0, s5::kW, s5::kH);
}

void ScreenComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = toScreenPixel (e.getPosition());
    const int dir = e.mods.isRightButtonDown() ? +1 : -1;   // left lowers, right raises
    if (onShift) onShift (e.mods.isShiftDown());
    holdDir = onHold ? onHold (p.x, p.y, dir) : 0;      // decide before the click changes anything
    if (onMouseDown && onMouseDown (p.x, p.y, dir)) frameChanged();
    holdX = p.x; holdY = p.y; holdTicks = 0;
    if (holdDir != 0) startTimer (15);
}

void ScreenComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto p = toScreenPixel (e.getPosition());
    if (onShift) onShift (e.mods.isShiftDown());
    if (onMouseMove && onMouseMove (p.x, p.y)) frameChanged();
}

void ScreenComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = toScreenPixel (e.getPosition());
    holdX = p.x; holdY = p.y;
    if (onMouseMove && onMouseMove (p.x, p.y)) frameChanged();
}

void ScreenComponent::mouseExit (const juce::MouseEvent&)
{
    if (onMouseExit && onMouseExit()) frameChanged();
}

void ScreenComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto p = toScreenPixel (e.getPosition());
    const int n = w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0);
    if (n == 0) return;
    if (onShift) onShift (e.mods.isShiftDown());
    if (onWheel && onWheel (p.x, p.y, n)) frameChanged();
}

void ScreenComponent::mouseUp (const juce::MouseEvent&)
{
    stopTimer(); holdDir = 0;
    if (onMouseUp && onMouseUp()) frameChanged();
}

// Holding the button repeats the change: quick start, then faster, then in bigger steps.
void ScreenComponent::timerCallback()
{
    ++holdTicks;                                       // 15 ms per tick
    const int ms = holdTicks * 15;
    if (ms < 300) return;                              // 0.3 s before it starts to run
    const int t = ms - 300;                            // time since the run started
    const int every = t < 300 ? 4 : (t < 700 ? 2 : 1); // every 60 ms, 30 ms, 15 ms
    if (holdTicks % every != 0) return;
    const int n = t < 300 ? 1 : (t < 700 ? 2 : (t < 1300 ? 4 : 8));
    if (onShift) onShift (juce::ModifierKeys::currentModifiers.isShiftDown());
    if (onWheel && onWheel (holdX, holdY, holdDir * n)) frameChanged();
}

bool ScreenComponent::keyPressed (const juce::KeyPress& k)
{
    if (! wantsKeys || ! wantsKeys() || ! onKey) return false;
    int code = 0;
    if (k.isKeyCode (juce::KeyPress::escapeKey)) code = 27;
    else if (k.isKeyCode (juce::KeyPress::returnKey)) code = 13;
    else if (k.isKeyCode (juce::KeyPress::leftKey)) code = 37;
    else if (k.isKeyCode (juce::KeyPress::rightKey)) code = 39;
    else if (k.isKeyCode (juce::KeyPress::backspaceKey)) code = 8;
    else if (k.isKeyCode (juce::KeyPress::deleteKey)) code = 46;
    else if (k.isKeyCode (juce::KeyPress::upKey)) code = 38;
    else if (k.isKeyCode (juce::KeyPress::downKey)) code = 40;
    else if (k.isKeyCode (juce::KeyPress::homeKey)) code = 36;
    else if (k.isKeyCode (juce::KeyPress::endKey)) code = 35;
    else if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown())         // Ctrl / Cmd + A C X V (label block)
    {
        const int c = (int) k.getKeyCode();
        if (c == 'A' || c == 'C' || c == 'X' || c == 'V' || c == 'a' || c == 'c' || c == 'x' || c == 'v')
        {
            code = (c & ~32) | 0x20000;
            if ((code & 0xffff) == 'V') s5::App::get().clip = juce::SystemClipboard::getTextFromClipboard().toStdString();
        }
    }
    else { const int c = (int) k.getTextCharacter(); if (c >= 32 && c < 127) code = c; }
    if (code == 0) return true;                      // swallow other keys while typing
    if (k.getModifiers().isShiftDown() && (code & 0xffff) != 0 && ! (code >= 32 && code < 127)) code |= 0x10000;
    if (onKey (code)) frameChanged();
    if ((code & 0xffff) == 'C' || (code & 0xffff) == 'X') juce::SystemClipboard::copyTextToClipboard (s5::App::get().clip);
    return true;
}
