#pragma once
// Pixel-exact 640x450 character screen in the style of a 1980s sampler monitor.
// Pure C++ (no JUCE dependency) so it can be unit-tested and compared pixel by pixel
// against the layout boards.  Own bitmap font (derived from the public-domain
// font8x8_basic), own drawing code.  No scaling or anti-aliasing here: the host
// enlarges the finished frame by integer pixel doubling only.
#include <cstdint>
#include <string>
#include <vector>

namespace s5
{
constexpr int kW = 640, kH = 450, kRowH = 18, kCellW = 16;

// One palette entry per role.  Panel (0) is "transparent" and is shown as the panel blue.
enum Col : uint8_t { Panel = 0, Yellow, Red, Cyan, White, Black, Ink, Blue, Dim, Violet, NumColours };

constexpr uint32_t kRgb[NumColours] = {
    0x1a3cff,  // Panel
    0xffe629,  // Yellow  (labels, units)
    0xe2262a,  // Red     (titles, menu text, selection bars)
    0x3de0ff,  // Cyan    (names, selection boxes, buttons)
    0xffffff,  // White   (values, light surfaces)
    0x000000,  // Black   (outer frame, name field)
    0x1f2fb0,  // Ink     (text on light surfaces, button text)
    0x1a3cff,  // Blue    (keyboard black keys)
    0x9fb2ff,  // Dim     (voice counts on the Split display)
    0xe64be6   // Violet  (arrows beside the tones that sound, Split / Info; photo)
};

enum class Align { Left, Right, Center };

struct CellOpts
{
    int bg = -1;                 // -1: no background bar
    Col fg = White;
    Align align = Align::Left;
    int dy = -1000;              // -1000: default (1 for plain text, 0 inside bars)
    bool sep = false;
};

class Screen
{
public:
    static int leadInk (char ch) { int lo, hi; return glyphInk (ch, lo, hi) ? lo * 2 : 0; }   // blank pixels left of the first ink of a glyph
    Screen() { clear(); }

    void clear() { fb.assign ((size_t) kW * kH, (uint8_t) Panel); shift = 0; textLog.clear(); }
    const uint8_t* pixels() const { return fb.data(); }
    uint8_t at (int x, int y) const { return fb[(size_t) y * kW + x]; }

    // vertical offset applied to all following raw() calls (used for text blocks)
    void setShift (int s) { shift = s; }
    void forgetText() { textLog.clear(); }          // after a big area was painted over: the texts below it are gone

    void raw (int x, int y0, int w, int h, Col c);
    // col is 1-based and may be fractional as long as (col-1)*16 is a whole number
    void cell (double row, double col, double w, const std::string& str, const CellOpts& o = {});
    void bar (int row, double col, double w, Col c, bool sep = false);
    // white pop-up list with black shadow; items start at the given text row, sel = red bar (-1: none)
    void popup (int x, int row0, const std::vector<std::string>& items, int sel, bool center = false);
    // two columns, filled top to bottom (rows per column), every column colCells character cells wide
    void popup2 (int x, int row0, const std::vector<std::string>& items, int sel, int rows, int colCells, bool flush = false);   // flush: the red bar also covers the white edge of the box
    void eraseCut (int rx, int ry, int rw, int rh);
    static void popupRect (int x, int row0, int n, int textW, int& rx, int& ry, int& rw, int& rh);

    // text helpers
    int textWidth (const std::string& s) const;
    void glyph (int x, int y, char ch, Col c, bool doubled);

    // Where every character of a left aligned text sits, relative to the text origin (cell x + 2):
    // [x0, x1) is the box a text cursor has to cover (ink plus one pixel on each side), gx the glyph origin.
    // Characters without ink (blanks) get a box of the width the text engine advances for them.
    struct CharSpan { int x0, x1, gx; };
    static std::vector<CharSpan> charSpans (const std::string& s);

    // RGB (0xRRGGBB) output of the whole frame, panel colour selectable
    void toRgb (uint32_t* dst, uint32_t panelColour = kRgb[Panel]) const;

private:
    struct Glyph { char ch; int x, lo, hi; };
    struct Layout { std::vector<Glyph> g; int w = 0; };
    static bool glyphInk (char ch, int& lo, int& hi);
    static void glyphBits (char ch, uint8_t out[16]);
    Layout layout (const std::string& s) const;
    void text (int row, double col, double w, const std::string& s, Col c, Align a, int dy);

    std::vector<uint8_t> fb;
    int shift = 0;
    struct TextRect { int x0, y0, x1, y1; uint8_t bg; };
    std::vector<TextRect> textLog;                 // every text drawn so far, so a pop-up can wipe the texts it would cut in half
};
}
