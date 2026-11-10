#include "LivePage.h"
#include "Pages.h"
#include "App.h"
#include <algorithm>
#include <cmath>

namespace s5
{
static void drawStatic (Screen& s, const StaticText& t)
{
    CellOpts o; o.fg = t.fg; o.align = t.align; o.bg = t.bg;
    o.dy = t.bg >= 0 ? 0 : t.dy;
    s.cell (t.row, t.col, t.w, t.text, o);
}

std::vector<std::string> LivePage::frameNames() const
{
    std::vector<std::string> v = btnNames;
    if (v.size() < 2) v.resize (2);
    if (! subHook && ! showSub) v[0] = " ";                  // RULE: SUB / COM without a function stay white and empty
    if (! comHook && ! showCom) v[1] = " ";
    return v;
}

void LivePage::draw (Screen& s) const
{
    if (refresh) refresh (const_cast<LivePage&> (*this));
    { const auto fn = frameNames(); pages::frame (s, title, popupButtons, subRed, &fn, comRed); }
    s.setShift (shift);
    for (const auto& t : before) { s.setShift (shiftFor (t.row)); drawStatic (s, t); }
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const Field& f = fields[i];
        s.setShift (shiftFor (f.row));
        if (f.lw > 0)
        {
            CellOpts l; l.fg = Yellow;
            s.cell (f.row, f.lcol, f.lw, f.label, l);
        }
        if (f.hideBy) { const Field* o = byLabel (f.hideBy); if (o && o->p.value == f.hideWhen) continue; }
        CellOpts v; v.align = f.align; v.fg = f.fg;
        // cyan only under the mouse; the chosen value is lit while the mouse is over it (or over Dec / Inc, which act on it)
        const bool lit = activeBox < 0 && ((int) i == hoverField || ((int) i == selected && showSel));
        if (! lit && f.dy >= 0) v.dy = f.dy;
        if (lit)
        {
            v.bg = f.selBg >= 0 ? f.selBg : Cyan; v.fg = f.selFg;
            if (f.shadow) s.raw ((int) ((f.col - 1) * kCellW) + 4, f.row * kRowH + 3, (int) (f.w * kCellW), kRowH + 1, Black);
        }
        if (f.w > 0) s.cell (f.row, f.col, f.w, f.p.text(), v);
    }
    s.setShift (shift);
    for (const auto& t : after) { s.setShift (shiftFor (t.row)); drawStatic (s, t); }
    s.setShift (shift);
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        const auto& b = boxes[i];
        std::string t = b.get();
        t.resize ((size_t) b.len, ' ');
        pages::textBox (s, b.row, b.col, b.len, t, (int) i == activeBox ? b.cur : -1);
    }
    pages::tableDx = tableDx;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        std::string t = b.get(); t.resize ((size_t) (b.cols * b.rows), ' ');
        const bool act = (int) i == activeBlk;
        const int lo = std::min (bCur, bAnc), hi = std::max (bCur, bAnc);
        for (int l = 0; l < b.rows; ++l)
        {
            const std::string line = t.substr ((size_t) (l * b.cols), (size_t) b.cols);
            pages::textBox (s, b.row + l, b.col, b.cols, line, act && bCur == bAnc && bCur / b.cols == l ? bCur % b.cols : -1);
            if (! act || bCur == bAnc) continue;
            const auto sp = Screen::charSpans (line);
            const int sx = (int) std::lround ((b.col - 1) * 16) + 2;
            int c0 = -1, c1 = -1;
            for (int c = 0; c < b.cols && c < (int) sp.size(); ++c) { const int q = l * b.cols + c; if (q >= lo && q < hi) { if (c0 < 0) c0 = c; c1 = c; } }
            if (c0 < 0) continue;
            s.raw (sx + sp[(size_t) c0].x0, (b.row + l) * kRowH - 1, sp[(size_t) c1].x1 - sp[(size_t) c0].x0, kRowH + 1, Cyan);      // one continuous bar
            for (int c = c0; c <= c1; ++c)
                if (line[(size_t) c] != ' ') s.glyph (sx + sp[(size_t) c].gx, (b.row + l) * kRowH + 1, line[(size_t) c], Ink, true);
        }
    }
    if (editing()) pages::nameTable (s, tblHover);
    s.setShift (0);
    if (extras) extras (s, *this);
}

static bool inCell (int x, int y, int row, double col, double w, int shift)
{
    const int x0 = (int) ((col - 1) * kCellW + 0.5), x1 = x0 + (int) (w * kCellW + 0.5);
    const int y0 = row * kRowH - 1 + shift;
    return x >= x0 && x < x1 && y >= y0 && y < y0 + kRowH + 1;
}

bool LivePage::hit (int x, int y) const
{
    for (const Field& f : fields)
        if (inCell (x, y, f.row, f.col, f.w, shiftFor (f.row)) || (f.lw > 0 && f.lblHit && inCell (x, y, f.row, f.lcol, f.lw, shiftFor (f.row))))
            return true;
    return false;
}

bool LivePage::blockHit (int x, int y, int& idx, int& pos) const
{
    for (size_t n = 0; n < blocks.size(); ++n)
    {
        const auto& b = blocks[n];
        const int x0 = (int) ((b.col - 1) * kCellW + 0.5), y0 = b.row * kRowH - 1 + shift;
        if (x < x0 || x >= x0 + pages::textBoxWidth (b.cols) || y < y0 || y >= y0 + b.rows * kRowH + 1) continue;
        const int l = std::min (b.rows - 1, (y - y0) / kRowH);
        const auto sp = Screen::charSpans (std::string ((size_t) b.cols, 'W'));
        const int rel = x - (x0 + 2);
        int c = b.cols - 1;
        for (size_t i = 0; i < sp.size(); ++i)
        {
            const int end = i + 1 < sp.size() ? (sp[i].x1 + sp[i + 1].x0) / 2 : 1 << 20;
            if (rel < end) { c = (int) i; break; }
        }
        idx = (int) n; pos = l * b.cols + c;
        return true;
    }
    return false;
}

bool LivePage::blockDrag (int x, int y)
{
    if (! bDrag || activeBlk < 0) return false;
    const auto& b = blocks[(size_t) activeBlk];
    const int x0 = (int) ((b.col - 1) * kCellW + 0.5), y0 = b.row * kRowH - 1 + shift;
    const int l = std::max (0, std::min (b.rows - 1, (y - y0) / kRowH));
    const auto sp = Screen::charSpans (std::string ((size_t) b.cols, 'W'));
    const int rel = x - (x0 + 2);
    int c = rel < 0 ? 0 : b.cols - 1;
    for (size_t i = 0; i < sp.size() && rel >= 0; ++i)
    {
        const int end = i + 1 < sp.size() ? (sp[i].x1 + sp[i + 1].x0) / 2 : 1 << 20;
        if (rel < end) { c = (int) i; break; }
    }
    const int pos = l * b.cols + c;
    if (pos == bCur) return false;
    bCur = pos;
    return true;
}

bool LivePage::blockTable (int cell)
{
    if (activeBlk < 0 || cell < 0 || cell >= 100) return false;
    auto& b = blocks[(size_t) activeBlk];
    const int n = b.cols * b.rows;
    std::string t = b.get(); t.resize ((size_t) n, ' ');
    const int lo = std::min (bCur, bAnc), hi = std::max (bCur, bAnc);
    auto blankSel = [&]
    {
        for (int l = lo / b.cols; l <= (hi - 1) / b.cols && l < b.rows; ++l)
        {
            std::string line = t.substr ((size_t) (l * b.cols), (size_t) b.cols), keep;
            for (int c = 0; c < b.cols; ++c) { const int q = l * b.cols + c; if (q < lo || q >= hi) keep += line[(size_t) c]; }
            keep.resize ((size_t) b.cols, ' ');
            t.replace ((size_t) (l * b.cols), (size_t) b.cols, keep);
        }
    };
    if (cell == 98) { bCur = bAnc = std::max (0, (bCur != bAnc ? lo : bCur - 1)); return true; }
    if (cell == 99) { bCur = bAnc = std::min (n - 1, (bCur != bAnc ? hi : bCur + 1)); return true; }
    if (cell >= 95 && cell != 96 && cell != 97) return false;
    if (bCur != bAnc)
    {
        blankSel();
        bCur = bAnc = lo;
        if (cell == 97) { b.set (t); return true; }
    }
    const int l = bCur / b.cols, c = bCur % b.cols;
    std::string line = t.substr ((size_t) (l * b.cols), (size_t) b.cols);
    int cur = c;
    if (! pages::nameTableAction (cell, line, cur, b.cols)) return false;
    t.replace ((size_t) (l * b.cols), (size_t) b.cols, line);
    b.set (t);
    bCur = bAnc = std::min (n - 1, l * b.cols + cur);
    return true;
}

bool LivePage::blockKey (int code)
{
    if (activeBlk < 0 || activeBlk >= (int) blocks.size()) return false;
    auto& b = blocks[(size_t) activeBlk];
    const int n = b.cols * b.rows;
    std::string t = b.get(); t.resize ((size_t) n, ' ');
    const bool shiftK = (code & kKeyShift) != 0, ctrl = (code & kKeyCtrl) != 0;
    code &= 0xffff;
    const int lo = std::min (bCur, bAnc), hi = std::max (bCur, bAnc);
    auto& clip = App::get().clip;
    auto move = [&] (int to) { bCur = std::max (0, std::min (n - 1, to)); if (! shiftK) bAnc = bCur; return true; };
    auto blank = [&]                                    // delete the selection; the rest of each line moves left
    {
        for (int l = lo / b.cols; l <= (hi - 1) / b.cols && l < b.rows; ++l)
        {
            std::string line = t.substr ((size_t) (l * b.cols), (size_t) b.cols), keep;
            for (int c = 0; c < b.cols; ++c) { const int q = l * b.cols + c; if (q < lo || q >= hi) keep += line[(size_t) c]; }
            keep.resize ((size_t) b.cols, ' ');
            t.replace ((size_t) (l * b.cols), (size_t) b.cols, keep);
        }
    };
    if (ctrl)
    {
        const int ch = code >= 'a' && code <= 'z' ? code - 32 : code;
        if (ch == 'A') { bAnc = 0; bCur = n - 1; return true; }
        if (ch == 'C' || ch == 'X')
        {
            if (lo == hi) return true;
            clip.clear();
            for (int i = lo; i < hi; ++i) { if (i > lo && i % b.cols == 0) clip += '\n'; clip += t[(size_t) i]; }
            if (ch == 'X') { blank(); b.set (t); bCur = bAnc = lo; }
            return true;
        }
        if (ch == 'V')
        {
            if (clip.empty()) return true;
            int p = lo, ln = lo / b.cols;
            if (lo != hi) blank();
            for (char c : clip)
            {
                if (c == '\n' || c == '\r') { ++ln; p = std::min (n, ln * b.cols); continue; }
                if (p >= n) break;
                ln = p / b.cols;
                t[(size_t) p++] = c >= 32 && c < 127 ? c : ' ';
            }
            b.set (t);
            bCur = bAnc = std::min (n - 1, p);
            return true;
        }
        return true;
    }
    if (code == 27) { activeBlk = -1; bDrag = false; return true; }
    if (code == 13) { const int p = std::min (n - 1, (bCur / b.cols + 1) * b.cols); bCur = bAnc = p; return true; }
    if (code == 37) return move (! shiftK && lo != hi ? lo : bCur - 1);
    if (code == 39) return move (! shiftK && lo != hi ? hi : bCur + 1);
    if (code == 38) return move (bCur - b.cols);
    if (code == 40) return move (bCur + b.cols);
    if (code == 36) return move (bCur / b.cols * b.cols);
    if (code == 35) return move (bCur / b.cols * b.cols + b.cols - 1);
    if (code == 8 || code == 46)
    {
        if (lo != hi) { blank(); b.set (t); bCur = bAnc = lo; return true; }
        if (code == 8) { if (bCur > 0) --bCur; t[(size_t) bCur] = ' '; b.set (t); bAnc = bCur; return true; }
        t[(size_t) bCur] = ' '; b.set (t); return true;
    }
    if (code >= 32 && code < 127)
    {
        int p = bCur;
        if (lo != hi) { blank(); p = lo; }
        t[(size_t) p] = (char) code;
        b.set (t);
        bCur = bAnc = std::min (n - 1, p + 1);
        return true;
    }
    return false;
}

bool LivePage::boxChar (int x, int y, int& idx, int& ch) const
{
    for (size_t n = 0; n < boxes.size(); ++n)
    {
        const auto& box = boxes[n];
        const int x0 = (int) ((box.col - 1) * kCellW + 0.5), y0 = box.row * kRowH - 1 + shift;
        if (x < x0 || x >= x0 + pages::textBoxWidth (box.len) || y < y0 || y >= y0 + kRowH + 1) continue;
        std::string t = box.get();
        t.resize ((size_t) box.len, ' ');
        const auto sp = Screen::charSpans (t);
        const int rel = x - (x0 + 2);
        idx = (int) n;
        ch = (int) sp.size() - 1;
        for (size_t i = 0; i < sp.size(); ++i)
        {
            const int end = i + 1 < sp.size() ? (sp[i].x1 + sp[i + 1].x0) / 2 : 1 << 20;
            if (rel < end) { ch = (int) i; break; }
        }
        return true;
    }
    return false;
}

int LivePage::boxLen (int idx) const
{
    const auto& b = boxes[(size_t) idx];
    std::string t = b.get();
    t.resize ((size_t) b.len, ' ');
    int n = (int) t.size();
    while (n > 0 && t[(size_t) n - 1] == ' ') --n;
    return std::min (b.len - 1, n);
}

static bool boxChange (LivePage& p, int idx, int delta)
{
    auto& b = p.boxes[(size_t) idx];
    std::string t = b.get(); t.resize ((size_t) b.len, ' ');
    const std::string chars = pages::nameChars();
    const char c = t[(size_t) b.cur];
    int i = 0;
    for (size_t k = 0; k < chars.size(); ++k) if (chars[k] == c) { i = (int) k; break; }
    i = std::max (0, std::min ((int) chars.size() - 1, i + delta));
    if (chars[(size_t) i] == c) return false;
    t[(size_t) b.cur] = chars[(size_t) i];
    b.set (t);
    return true;
}

bool LivePage::boxKey (int code)
{
    if (activeBlk >= 0) return blockKey (code);
    if (activeBox < 0 || activeBox >= (int) boxes.size()) return false;
    auto& box = boxes[(size_t) activeBox];
    std::string t = box.get(); t.resize ((size_t) box.len, ' ');
    if (code == 27 || code == 13) { activeBox = -1; return true; }
    if (code == 37) { box.cur = std::max (0, box.cur - 1); return true; }
    if (code == 39) { box.cur = std::min (boxLen (activeBox), box.cur + 1); return true; }
    if (code == 8) { if (box.cur > 0) --box.cur; t[(size_t) box.cur] = ' '; box.set (t); return true; }
    if (code == 46) { t[(size_t) box.cur] = ' '; box.set (t); return true; }
    if (code >= 32 && code < 127)
    {
        t[(size_t) box.cur] = (char) code;
        box.set (t);
        box.cur = std::min (box.len - 1, box.cur + 1);
        return true;
    }
    return false;
}

bool LivePage::hitSelected (int x, int y) const
{
    int idx, ch;
    if (boxChar (x, y, idx, ch)) return activeBox == idx && std::min (ch, boxLen (idx)) == boxes[(size_t) idx].cur;
    if (activeBox >= 0 || activeBlk >= 0) return false;
    if (selected < 0 || selected >= (int) fields.size()) return false;
    const Field& f = fields[(size_t) selected];
    return inCell (x, y, f.row, f.col, f.w, shiftFor (f.row)) || (f.lw > 0 && f.lblHit && inCell (x, y, f.row, f.lcol, f.lw, shiftFor (f.row)));
}

bool LivePage::tableMove (int x, int y)
{
    const int h = editing() ? pages::nameTableHit (x, y) : -1;
    if (h == tblHover) return false;
    tblHover = h;
    return true;
}

bool LivePage::click (int x, int y, int dir)
{
    if (activeBlk >= 0)
    {
        const int cell = pages::nameTableHit (x, y);
        if (cell >= 0) return blockTable (cell);
    }
    {
        int bi = 0, bp = 0;
        if (blockHit (x, y, bi, bp))
        {
            const bool was = activeBlk == bi;
            activeBlk = bi;
            if (dir > 0 && was && bp == bCur && bCur == bAnc)               // right click on the cursor: next character
            {
                auto& b = blocks[(size_t) bi];
                std::string t = b.get(); t.resize ((size_t) (b.cols * b.rows), ' ');
                const std::string chars = pages::nameChars();
                const size_t k = chars.find (t[(size_t) bp]);
                t[(size_t) bp] = chars[((k == std::string::npos ? 0 : k) + 1) % chars.size()];
                b.set (t);
                return true;
            }
            if (bigStep && was) bCur = bp;                                      // Shift + click extends the selection
            else { bCur = bAnc = bp; bDrag = dir <= 0; }
            if (bigStep && was) bDrag = true;
            return true;
        }
    }
    if (activeBox >= 0 && activeBox < (int) boxes.size())
    {
        const int cell = pages::nameTableHit (x, y);
        if (cell >= 0)
        {
            auto& box = boxes[(size_t) activeBox];
            std::string t = box.get(); t.resize ((size_t) box.len, ' ');
            int cur = box.cur;
            if (! pages::nameTableAction (cell, t, cur, box.len)) return false;
            box.set (t); box.cur = cur;
            return true;
        }
    }
    int bi = 0, bc = 0;
    if (boxChar (x, y, bi, bc))
    {
        const bool was = activeBox == bi;
        activeBox = bi;
        auto& box = boxes[(size_t) bi];
        bc = std::min (bc, boxLen (bi));
        if (! was) { box.cur = bc; return true; }            // first click only selects the box
        if (box.cur != bc) { box.cur = bc; return true; }   // another character: only move the cursor
        return dir != 0 && boxChange (*this, bi, dir);
    }
    itemHit = false;
    if (clickHook) { const int r = clickHook (x, y, dir); if (r >= 0) { itemHit = r == 2; return r != 0; } }
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const Field& f = fields[i];
        if (inCell (x, y, f.row, f.col, f.w, shiftFor (f.row)) || (f.lw > 0 && f.lblHit && inCell (x, y, f.row, f.lcol, f.lw, shiftFor (f.row))))
        {
            const bool ch = selected != (int) i || activeBox >= 0;
            selected = (int) i;
            activeBox = -1; activeBlk = -1;
            if (dir != 0 && change (dir)) return true;   // hover is the confirmation: the click changes at once
            return ch;
        }
    }
    return false;
}

bool LivePage::change (int dir)
{
    if (activeBlk >= 0 && activeBlk < (int) blocks.size())              // Dec / Inc / wheel: the character at the cursor
    {
        auto& b = blocks[(size_t) activeBlk];
        std::string t = b.get(); t.resize ((size_t) (b.cols * b.rows), ' ');
        const std::string chars = pages::nameChars();
        const size_t k = chars.find (t[(size_t) bCur]);
        const int i = std::max (0, std::min ((int) chars.size() - 1, (k == std::string::npos ? 0 : (int) k) + dir));
        if (chars[(size_t) i] == t[(size_t) bCur]) return false;
        t[(size_t) bCur] = chars[(size_t) i];
        b.set (t); bAnc = bCur;
        return true;
    }
    if (activeBox >= 0 && activeBox < (int) boxes.size()) return boxChange (*this, activeBox, dir);
    if (changeHook) { const int r = changeHook (dir); if (r >= 0) return r != 0; }
    if (selected < 0 || selected >= (int) fields.size()) return false;
    if (preChange) preChange (*this);
    Param& q = fields[(size_t) selected].p;
    const bool ch = q.change (bigStep && q.fast > 1 ? dir * q.fast : dir);
    if (ch && postChange) postChange (*this);
    return ch;
}

bool LivePage::hoverMove (int x, int y)
{
    int hf = -1;
    if (activeBox < 0)
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const Field& f = fields[i];
            if (f.w <= 0) continue;
            if (f.hideBy) { const Field* o = byLabel (f.hideBy); if (o && o->p.value == f.hideWhen) continue; }
            if (inCell (x, y, f.row, f.col, f.w, shiftFor (f.row))) { hf = (int) i; break; }
        }
    if (hoverOut && ! inGraph (x, y)) hoverOut();
    const int hg = graphAt && inGraph (x, y) ? graphAt (x, y) : -1;
    const bool ch = hf != hoverField || hg != hoverGraph;
    hoverField = hf; hoverGraph = hg;
    return ch;
}

bool LivePage::setValue (const std::string& label, int v)
{
    Field* f = byLabel (label);
    if (! f) return false;
    v = std::max (f->p.min, std::min (f->p.max, v));
    if (v == f->p.value) return false;
    f->p.value = v;
    if (postChange) postChange (*this);
    return true;
}

Field* LivePage::byLabel (const std::string& l)
{
    for (auto& f : fields) if (f.p.label == l) return &f;
    return nullptr;
}
const Field* LivePage::byLabel (const std::string& l) const
{
    for (auto& f : fields) if (f.p.label == l) return &f;
    return nullptr;
}
}
