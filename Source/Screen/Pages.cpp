#include "Pages.h"
#include <algorithm>
#include <cmath>

namespace s5::pages
{
void frame (Screen& s, const std::string& title, bool popupButtons, bool subRed, const std::vector<std::string>* names, bool comRed)
{
    s.clear();
    s.raw (0, 0, kW, kH, Black);
    s.raw (16, 3 * kRowH + 1, 608, 385, Panel);

    struct B { const char* t; double col; bool light; };
    const B btn[] = { { "MODE", 3.5, true }, { "MENU", 9.5, true }, { "SUB", 15.5, true }, { "COM", 21.5, true },
                      { popupButtons ? "Sel" : "Dec", 27.5, false }, { popupButtons ? "exit" : "Inc", 33.5, false } };
    int bi = 0;
    for (const auto& b : btn)
    {
        CellOpts o;
        o.bg = b.light ? White : Cyan;
        o.fg = b.light ? ((subRed && std::string (b.t) == "SUB") || (comRed && std::string (b.t) == "COM") ? Red : Ink) : Black;
        o.align = Align::Center;
        std::string t = b.t;
        if (names && bi >= 2 && bi - 2 < (int) names->size() && ! (*names)[(size_t) bi - 2].empty()) t = (*names)[(size_t) bi - 2];
        if (t == " ") t.clear();
        s.cell (1, b.col, 5, t, o);
        ++bi;
    }
    CellOpts t; t.fg = Red; t.align = Align::Right; t.dy = 1;
    s.cell (2, 22, 16.75, title, t);
}

void keyboard (Screen& s, bool endTicksOnly, int oct)
{
    s.setShift (0);
    s.raw (16, 332, 608, 108, White);
    const int kx = 45, ky = 366, kh = 50, kbh = 30, KW = 15, N = 36;
    s.raw (kx - 1, ky - 1, N * KW + 3, kh + 2, Ink);
    s.raw (kx, ky, N * KW + 1, kh, White);
    for (int w = 1; w < N; ++w) s.raw (kx + w * KW, ky, 1, kh, Ink);
    for (int w = 0; w < N - 1; ++w)
    {
        static const int blackAt[] = { 0, 1, 3, 4, 5 };
        for (int m : blackAt)
            if (w % 7 == m) s.raw (kx + (w + 1) * KW - 4, ky, 9, kbh, Blue);
    }
    auto label = [&] (int w, const char* str, Col c)
    {
        for (int i = 0; str[i]; ++i) s.glyph (kx + w * KW + 1 + i * 8, 418, str[i], c, false);
    };
    for (int i = 0; i < 6; ++i) { const std::string n = "C" + std::to_string (2 + oct + i); label (i * 7, n.c_str(), Ink); }
    { const std::string n = "A" + std::to_string (4 + oct); label (19, n.c_str(), Red); }
    s.raw (kx, 352, N * KW, 1, Ink);
    if (endTicksOnly) { s.raw (kx, 346, 1, 13, Ink); s.raw (kx + N * KW, 346, 1, 13, Ink); }
    const char* second = "2nd"; const char* first = "1st";
    for (int i = 0; i < 3; ++i)
    {
        s.glyph (593 + i * 8, 336, second[i], Red, false);
        s.glyph (593 + i * 8, 351, first[i], Ink, false);
    }
}

PatchPrm defaultPatchPrm()
{
    PatchPrm d;
    d.rows = { { "Key Mode", "Normal" }, { "Key Assign", "Rotary" }, { "Unison Detune", "4" },
               { "V-Sw Thresh.", "64" }, { "V-Mix Ratio", "0" }, { "P.Bend Range", "12" },
               { "A.T Assign", "Bend +" }, { "A.T Sense", "127" }, { "Oct.Shift", "0" },
               { "Output Assign", "Out1" }, { "Level", "127" } };
    return d;
}

const char* nameChars() { return " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-+.,&#/'()!?:;*_=<>@"; }

int textBoxWidth (int len)
{
    const auto sp = Screen::charSpans (std::string ((size_t) len, 'W'));
    return sp.empty() ? 4 : sp.back().x1 + 4;
}

void textBox (Screen& s, int row, double col, int len, const std::string& text, int cursor)
{
    const int bx = (int) std::lround ((col - 1) * 16);
    s.raw (bx, row * kRowH - 1, textBoxWidth (len), kRowH + 1, Black);
    CellOpts f; f.fg = White; f.dy = 0;
    s.cell (row, col, 40, text, f);
    if (cursor < 0) return;
    std::string t = text;
    if ((int) t.size() <= cursor) t.resize ((size_t) cursor + 1, ' ');
    const auto sp = Screen::charSpans (t);
    const int i = std::min (cursor, (int) sp.size() - 1);
    const int sx = (int) ((col - 1) * 16) + 2;
    s.raw (sx + sp[(size_t) i].x0, row * kRowH - 1, sp[(size_t) i].x1 - sp[(size_t) i].x0, kRowH + 1, Cyan);
    if (t[(size_t) i] != ' ') s.glyph (sx + sp[(size_t) i].gx, row * kRowH + 1, t[(size_t) i], Ink, true);
}

int tableDx = 0;                                                    // columns the character table is moved (Disk Label: one column further left)
static constexpr int kTabX0 = 464, kTabY = 5 * kRowH - 1 + 4;       // top left of the table (shift 4 included)

int nameTableHit (int x, int y)
{
    const int kTabX = kTabX0 + tableDx * 16;
    if (x < kTabX || x >= kTabX + 160 || y < kTabY || y >= kTabY + 10 * kRowH) return -1;
    return ((y - kTabY) / kRowH) * 10 + (x - kTabX) / 16;
}

void nameTable (Screen& s, int hoverCell)
{
    const int save = 4;
    (void) save;
    s.setShift (4);
    for (int c = 0; c < 100; ++c)
    {
        const int row = 5 + c / 10, col = 30 + tableDx + c % 10;
        const bool hov = c == hoverCell;
        CellOpts o; o.bg = hov ? Red : White; o.fg = hov ? White : Blue; o.align = Align::Center; o.dy = 0;
        if (c < 95)
            s.cell (row, col, 1, std::string (1, (char) (32 + c)), o);
        else
        {
            s.cell (row, col, 1, "", o);
            const int x = (col - 1) * 16, y = row * kRowH - 1;
            const Col fg = hov ? White : Red;
            if (c == 95)
                for (int yy = 0; yy < 8; ++yy)
                    for (int xx = 0; xx < 8; ++xx)
                        if (((xx + yy) & 1) == 0) s.raw (x + 4 + xx, y + 5 + yy, 1, 1, hov ? White : Blue);
            if (c == 96 || c == 97) s.glyph (x + 1, y + 2, c == 96 ? 'I' : 'D', fg, true);
            if (c == 98) for (int k = 0; k < 6; ++k) s.raw (x + 3 + k * 2, y + 9 - k, 2, 2 * k + 1, fg);
            if (c == 99) for (int k = 0; k < 6; ++k) s.raw (x + 3 + (5 - k) * 2, y + 9 - k, 2, 2 * k + 1, fg);
        }
    }
    s.setShift (0);
}

bool nameTableAction (int cell, std::string& text, int& cur, int len)
{
    text.resize ((size_t) len, ' ');
    if (cell < 0 || cell >= 100) return false;
    if (cell < 95)
    {
        text[(size_t) cur] = (char) (32 + cell);
        cur = std::min (len - 1, cur + 1);
        return true;
    }
    if (cell == 96) { text.insert ((size_t) cur, " "); text.resize ((size_t) len, ' '); return true; }
    if (cell == 97) { text.erase ((size_t) cur, 1); text.resize ((size_t) len, ' '); return true; }
    if (cell == 98) { if (cur == 0) return false; --cur; return true; }
    if (cell == 99)
    {
        int n = len;
        while (n > 0 && text[(size_t) n - 1] == ' ') --n;
        const int lim = std::min (len - 1, n);
        if (cur >= lim) return false;
        ++cur; return true;
    }
    return false;                                              // shaded block: no function known
}

void patchPrm (Screen& s, const PatchPrm& d)
{
    {
        std::vector<std::string> nm { "", "", d.menu == 2 ? "EXE" : "", "" };
        frame (s, "EDIT-Patch PRM", d.menu != 0, d.menu == 1, &nm, d.menu == 2);
    }
    if (d.src >= 0)                      // header while the SUB / COM menus are in use: which patch is edited (E.) or taken as source (C.)
    {
        CellOpts h; h.fg = Yellow; h.dy = 1;
        s.cell (2, 3.5, 9, d.src == 0 ? "E.Source" : "C.Source", h);
        CellOpts hi; hi.fg = Yellow; hi.dy = 1;
        if (d.srcSelected) { hi.bg = Cyan; hi.fg = Ink; }
        s.cell (2, 13, 3, d.srcId, hi);
    }
    s.setShift (4);
    if (d.idSelected) s.cell (3, 3, 3, d.id, { Cyan, Ink, Align::Left, 0 });
    else s.cell (3, 3, 3, d.id, { -1, White, Align::Left, 1 });
    CellOpts nm; nm.fg = Cyan; nm.dy = 1;
    s.cell (3, 6.6875, 12, d.name, nm);

    for (size_t i = 0; i < d.rows.size(); ++i)
    {
        const int r = 4 + (int) i;
        CellOpts lab; lab.fg = Yellow;
        s.cell (r, 6.6875, 15, d.rows[i].first, lab);
        CellOpts v; v.align = Align::Right;
        if ((int) i == d.selected) { v.bg = Cyan; v.fg = Ink; }
        else if ((int) i == d.hoverRow) { v.bg = Cyan; v.fg = Ink; }
        s.cell (r, 20.6875, 8, d.rows[i].second, v);
    }
    CellOpts lab; lab.fg = Yellow;
    s.cell (16, 6.6875, 5, "Name", lab);
    textBox (s, 16, 14, 12, d.nameField, d.nameCursor);
    s.setShift (0);
    keyboard (s, false, d.oct);
    if (d.showTable) nameTable (s, d.tableHover);
    if (d.menu) s.popup (d.menuX, 4, d.menuItems, d.menuSel);       // the pull-down hides everything beneath it
}
}
