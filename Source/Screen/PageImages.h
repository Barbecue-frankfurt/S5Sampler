#pragma once
#include "PixelScreen.h"

namespace s5::pages
{
struct PageImage
{
    const char* id;
    const char* mode;      // PLAY, EDIT, UTIL, FUNC, MIDI, DISK
    const char* name;      // name shown in the page menu
    const uint16_t* rle;   // runs of (colour << 12 | length), rows in order
    int count;
};

extern const PageImage kPageImages[];
extern const int kNumPageImages;

const PageImage* findImage (const std::string& id);
void drawImage (Screen& s, const PageImage& p);
}
