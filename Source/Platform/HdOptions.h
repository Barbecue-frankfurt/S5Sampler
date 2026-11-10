#pragma once
// Options > "Hard disk images (SCSI)": which hard disk image sits on which SCSI ID (0-6), and the ID of the CD-5.
// The S-550 itself is ID 7 (fixed). The configuration is stored in the user's settings folder.
#include <juce_gui_basics/juce_gui_basics.h>

namespace s5
{
void initHdSettings();     // reads the stored configuration, attaches the images (call once, before the first editor)
void showHdOptions();      // opens the dialog
int storedDisplayScale();          // 1 or 2 (Options > Display), default 2
void storeDisplayScale (int);
void showCreateHdImage();  // Options > Create Hard disk Image: 20 / 40 / 60 / 80 MB, formatted, optionally attached to an ID
}
