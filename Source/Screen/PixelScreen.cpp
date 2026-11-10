#include <cctype>
#include "PixelScreen.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "FontData.inc"

namespace s5
{
static int hexv (char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void Screen::glyphBits (char ch, uint8_t out[16])
{
    int code = (unsigned char) ch;
    if (code == 127)                                   // the plus-minus sign of the device font (Search Mode)
    {
        static const uint8_t pm[16] = { 0x00, 0x00, 0x30, 0x30, 0xFC, 0xFC, 0x30, 0x30, 0x00, 0x00, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00 };
        std::copy (pm, pm + 16, out);
        return;
    }
    if (code == 3 || code == 4)                        // arrows of the Auto Loop commands: 3 = right (L->E), 4 = left (L<-E)
    {
        static const uint8_t ar[8] = { 0x20, 0x30, 0x38, 0x3C, 0x3C, 0x38, 0x30, 0x20 };
        std::fill (out, out + 16, 0);
        for (int r = 0; r < 8; ++r)
        {
            uint8_t b = ar[r];
            if (code == 4) { uint8_t m = 0; for (int k = 0; k < 8; ++k) if ((b >> k) & 1) m |= (uint8_t) (0x80 >> k); b = m; }
            out[4 + r] = b;
        }
        return;
    }
    if (code == 1 || code == 2)                        // PREVIEW: Roman numerals I (12 px serifs, 4 px stem) and II (12 px serifs, two 2 px stems)
    {
        for (int r = 0; r < 16; ++r)
        {
            const bool serif = r < 2 || (r >= 12 && r < 14), stem = r >= 2 && r < 12;
            out[r] = serif ? 0xFC : stem ? (code == 2 ? 0x30 : 0x48) : 0x00;
        }
        return;
    }
    if (code < 32 || code > 126) { std::fill (out, out + 16, 0); return; }
    const char* h = kFontHex[code - 32];
    for (int i = 0; i < 16; ++i) out[i] = (uint8_t) (hexv (h[2 * i]) * 16 + hexv (h[2 * i + 1]));
}

bool Screen::glyphInk (char ch, int& lo, int& hi)
{
    uint8_t b[16];
    glyphBits (ch, b);
    lo = 8; hi = -1;
    for (int r = 0; r < 16; ++r)
        for (int k = 0; k < 8; ++k)
            if ((b[r] >> (7 - k)) & 1) { lo = std::min (lo, k); hi = std::max (hi, k); }
    return hi >= 0;
}

void Screen::raw (int x, int y0, int w, int h, Col c)
{
    const int y = y0 + shift;
    for (int yy = std::max (0, y); yy < std::min (kH, y + h); ++yy)
        for (int xx = std::max (0, x); xx < std::min (kW, x + w); ++xx)
            fb[(size_t) yy * kW + xx] = (uint8_t) c;
}

void Screen::glyph (int x, int y, char ch, Col c, bool doubled)
{
    uint8_t b[16];
    glyphBits (ch, b);
    for (int r = 0; r < 16; ++r)
        for (int k = 0; k < 8; ++k)
            if ((b[r] >> (7 - k)) & 1)
            {
                if (doubled) raw (x + 2 * k, y + r, 2, 1, c);
                else raw (x + k, y + r, 1, 1, c);
            }
}

// Proportional spacing: 2 px between the ink of neighbouring glyphs, tabular digits,
// fixed-width dashes, word space 12 px, leading space 16 px.
// PREVIEW: an I that stands for the roman numeral (I11, IA-0.8s, a lone I, [I], "I >") becomes glyph 2; letters ("Int", "IMG") stay
static std::string romanize (const std::string& in)
{
    std::string s = in;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != 'I') continue;
        const char p = i ? s[i - 1] : ' ', n = i + 1 < s.size() ? s[i + 1] : ' ', n2 = i + 2 < s.size() ? s[i + 2] : ' ';
        if (std::isalpha ((unsigned char) p)) continue;
        size_t k = i + 1; while (k < s.size() && s[k] == ' ') ++k;
        const bool arrow = k < s.size() && s[k] == '>';
        if ((n >= '1' && n <= '4') || ((n == 'A' || n == 'B') && n2 == '-') || s.size() == 1 || (p == '[' && n == ']') || arrow) s[i] = '\002';
    }
    return s;
}

Screen::Layout Screen::layout (const std::string& s0) const
{
    const std::string s = romanize (s0);
    Layout L;
    int pen = 0, end = 0;
    bool lead = true;
    for (char ch : s)
    {
        int lo, hi;
        const bool has = glyphInk (ch, lo, hi);
        if (ch == ' ' || ! has) { pen += lead ? 16 : 12; continue; }

        int w = (hi - lo + 1) * 2, off = 0, adv;
        if (ch >= '0' && ch <= '9')      { off = (14 - w) / 2; adv = 16; }
        else if (ch == '-' || ch == '_') { off = lo * 2; adv = 16; w = 16 - lo * 2; }
        else                             { adv = w + 2; if (ch == '[' || ch == ']') adv += 4; }

        if (lead) { pen += lo * 2; lead = false; }
        L.g.push_back ({ ch, pen + off, lo, hi });
        end = (ch >= '0' && ch <= '9') ? pen + 14 : pen + off + w;     // digits end on their cell edge: numbers do not jitter
        pen += adv;
    }
    if (! L.g.empty()) L.w = end;
    return L;
}

std::vector<Screen::CharSpan> Screen::charSpans (const std::string& s0)
{
    const std::string s = romanize (s0);
    std::vector<CharSpan> v;
    int pen = 0;
    bool lead = true;
    for (char ch : s)
    {
        int lo, hi;
        const bool has = glyphInk (ch, lo, hi);
        if (ch == ' ' || ! has)
        {
            const int a = lead ? 16 : 12;
            v.push_back ({ pen - 1, pen + a - 3, pen });      // blank: a little narrower than the advance
            pen += a;
            continue;
        }
        int w = (hi - lo + 1) * 2, off = 0, adv;
        if (ch >= '0' && ch <= '9')      { off = (14 - w) / 2; adv = 16; }
        else if (ch == '-' || ch == '_') { off = lo * 2; adv = 16; w = 16 - lo * 2; }
        else                             { adv = w + 2; if (ch == '[' || ch == ']') adv += 4; }
        if (lead) { pen += lo * 2; lead = false; }
        const int ink = pen + off;                              // first ink pixel
        v.push_back ({ ink - 1, ink + w + 1, ink - lo * 2 });
        pen += adv;
    }
    return v;
}

int Screen::textWidth (const std::string& s) const { return layout (s).w; }

void Screen::text (int row, double col, double w, const std::string& s, Col c, Align a, int dy)
{
    const Layout L = layout (s);
    const int cx = (int) std::lround ((col - 1) * kCellW), cw = (int) std::lround (w * kCellW);
    int sx = cx + 2;
    if (a == Align::Right)       sx = cx + cw - L.w - 4;
    else if (a == Align::Center) sx = cx + (cw - L.w) / 2;
    {   // log every word separately (a gap of a blank or more ends a word)
        int w0 = -1, w1 = 0;
        auto flush = [&] { if (w0 >= 0) { const int yy = std::max (0, std::min (kH - 1, row * kRowH + shift + kRowH / 2)), xx = std::max (0, std::min (kW - 1, sx + w0 - 1));
                                    textLog.push_back ({ sx + w0 - 1, row * kRowH + shift, sx + w1 + 1, row * kRowH + shift + kRowH, fb[(size_t) yy * kW + (size_t) xx] }); } w0 = -1; };
        for (const auto& g : L.g)
        {
            const int gw = (g.ch >= '0' && g.ch <= '9') ? 14 : (g.hi - g.lo + 1) * 2;
            if (w0 >= 0 && g.x - w1 >= 8) flush();
            if (w0 < 0) w0 = g.x;
            w1 = g.x + gw;
        }
        flush();
    }
    for (const auto& g : L.g)
        glyph (sx + g.x - g.lo * 2, row * kRowH + 1 + dy, g.ch, c, true);
}

// Bars grow one pixel above the row so text keeps 1 px clearance on top.
void Screen::bar (int row, double col, double w, Col c, bool sep)
{
    raw ((int) std::lround ((col - 1) * kCellW), row * kRowH - 1,
         (int) std::lround (w * kCellW) - (sep ? 2 : 0), kRowH + 1, c);
}

void Screen::cell (double rowD, double col, double w, const std::string& str, const CellOpts& o)
{
    const int row = (int) rowD;
    if (o.bg >= 0) bar (row, col, w, (Col) o.bg, o.sep);
    if (! str.empty())
        text (row, col, w, str, o.fg, o.align, o.dy != -1000 ? o.dy : (o.bg < 0 ? 1 : 0));
}

// A pop-up never cuts a text in half: every text that reaches under it only partly is wiped completely.
void Screen::eraseCut (int rx, int ry, int rw, int rh)
{
    const int px1 = rx + rw + 4, py1 = ry + rh + 4;                  // incl. shadow
    for (const auto& t : textLog)
    {
        const bool hit = t.x0 < px1 && t.x1 > rx && t.y0 < py1 && t.y1 > ry;
        const bool inside = t.x0 >= rx && t.x1 <= px1 && t.y0 >= ry && t.y1 <= py1;
        if (! hit || inside) continue;
        const int x0 = std::max (0, t.x0), y0 = std::max (0, std::min (kH - 1, t.y0));
        const uint8_t bg = t.bg;
        for (int y = y0; y < std::min (kH, t.y1); ++y)
            for (int x = x0; x < std::min (kW, t.x1); ++x) fb[(size_t) y * kW + (size_t) x] = bg;
    }
}

void Screen::popupRect (int x, int row0, int n, int textW, int& rx, int& ry, int& rw, int& rh)
{
    const int cells = (textW + 8 + 15) / 16;       // whole character cells
    rx = x - 8; ry = row0 * kRowH - 1; rw = cells * kCellW + 16; rh = n * kRowH + 3;
}

void Screen::popup (int x, int row0, const std::vector<std::string>& items, int sel, bool center)
{
    int tw = 0;
    for (const auto& s : items) tw = std::max (tw, textWidth (s));
    int rx, ry, rw, rh;
    popupRect (x, row0, (int) items.size(), tw, rx, ry, rw, rh);
    const int save = shift; shift = 0;
    eraseCut (rx, ry, rw, rh);
    raw (rx + 4, ry + 4, rw, rh, Black);
    raw (rx, ry, rw, rh, White);
    const double col = x / 16.0 + 1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        CellOpts o; o.dy = 0; if (center) o.align = Align::Center;
        if ((int) i == sel) { bar (row0 + (int) i, rx / 16.0 + 1, rw / 16.0, Red); o.fg = White; }   // the red bar runs to the edge of the box
        else o.fg = Red;
        cell (row0 + (int) i, col, rw / 16.0 - 1, items[i], o);
    }
    shift = save;
}

void Screen::popup2 (int x, int row0, const std::vector<std::string>& items, int sel, int rows, int colCells, bool flush)
{
    const int ncol = ((int) items.size() + rows - 1) / rows;
    const int rx = x - 8, ry = row0 * kRowH - 1, rw = ncol * colCells * kCellW + 16, rh = rows * kRowH + 3;
    const int save = shift; shift = 0;
    eraseCut (rx, ry, rw, rh);
    raw (rx + 4, ry + 4, rw, rh, Black);
    raw (rx, ry, rw, rh, White);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int c = (int) i / rows, r = (int) i % rows;
        const double col = x / 16.0 + 1 + c * colCells;
        CellOpts o; o.dy = 0;
        if ((int) i == sel)
        {   // two columns: the bar runs to the edge of the box and to the edge of its own column
            const int cw = colCells * kCellW;
            const int l = c == 0 ? rx : rx + 8 + c * cw, r2 = c == ncol - 1 ? rx + rw : rx + 8 + (c + 1) * cw;
            bar (row0 + r, l / 16.0 + 1, (r2 - l) / 16.0, Red); o.fg = White;
            if (flush && r == rows - 1) raw (l, ry + rh - 3, r2 - l, 3, Red);                 // no white line under the last entry
        }
        else o.fg = Red;
        cell (row0 + r, col, colCells, items[i], o);
    }
    shift = save;
}

void Screen::toRgb (uint32_t* dst, uint32_t panelColour) const
{
    for (int i = 0; i < kW * kH; ++i)
        dst[i] = fb[(size_t) i] == Panel ? panelColour : kRgb[fb[(size_t) i]];
}
}
