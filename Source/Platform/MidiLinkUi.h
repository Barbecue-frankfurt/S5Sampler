#pragma once
// Options > "S-550 MIDI link ...": ports, Device ID, bulk dump to / from a real S-550 and live control.
#include <juce_gui_basics/juce_gui_basics.h>
namespace s5
{
void initMidiLink();      // reads stored settings (call once); starts the live-sync timer when the link was open
void showMidiLink();      // opens the dialog
}
