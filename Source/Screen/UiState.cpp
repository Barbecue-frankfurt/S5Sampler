#include "UiState.h"
#include "App.h"
#include <algorithm>
#include <cstring>

namespace s5
{
static const char* const kModes[] = { "PLAY", "EDIT", "DISK", "FUNC", "MIDI", "UTIL" };
static const char* const kModeKeys[] = { "PLAY", "EDIT", "DISK", "FUNC", "MIDI", "UTIL" };
static constexpr int kPopupRow = 4;

static bool inButton (int x, int y, int x0) { return x >= x0 && x < x0 + 80 && y >= 17 && y < 36; }

UiState::UiState()
{
    auto P = [] (const char* l, int v, int lo, int hi, std::vector<std::string> n = {})
    { Param p; p.label = l; p.value = v; p.min = lo; p.max = hi; p.names = std::move (n); return p; };
    patch = {
        P ("Key Mode", 0, 0, 4, { "Normal", "V-Sw", "X-Fade", "V-Mix", "Unison" }),
        P ("Key Assign", 0, 0, 1, { "Rotary", "Fix" }),
        P ("Unison Detune", 4, 0, 127),
        P ("V-Sw Thresh.", 64, 0, 127),
        P ("V-Mix Ratio", 0, 0, 127),
        P ("P.Bend Range", 12, 0, 12),
        P ("A.T Assign", 0, 0, 3, { "Bend +", "Bend -", "P.Mod", "Vol" }),
        P ("A.T Sense", 127, 0, 127),
        P ("Oct.Shift", 0, -2, 2),
        P ("Output Assign", 0, 0, 8, { "Out1", "Out2", "Out3", "Out4", "Out5", "Out6", "Out7", "Out8", "Tone" }),
        P ("Level", 127, 0, 127) };
}

LivePage* UiState::liveFor (const std::string& id) const
{
    auto it = live.find (id);
    if (it == live.end())
    {
        auto p = makeLivePage (id);
        if (! p) return nullptr;
        p->bigStep = big;
        if (p->refresh) p->refresh (*p);
        it = live.emplace (id, std::move (p)).first;
    }
    return it->second.get();
}

void UiState::setPage (const std::string& id) { pageId = id; overlay = Overlay::None; hover = -1; }

std::string UiState::mode() const
{
    const auto* p = pages::findImage (pageId);
    return p ? p->mode : "PLAY";
}

// Menu order follows the owner's manual.
static const char* const kOrder[] = {
    "Standard", "Main", "EightKey",
    "Patch", "Split", "PatchMap", "ToneEdit", "Loop", "Lfo", "Tvf", "Tva", "ToneMap", "Delete", "CopyMove", "DispWave",
    "WaveScope", "Sampling", "Truncate", "Mix", "Combine", "DFilter", "WaveLoop", "WaveDraw", "SysBackup",
    "Master", "Initialize",
    "Message", "ProgNo", "Monitor",
    "Disk", "LoadAll", "LoadPRM", "LoadTone", "DirPatch", "DirTone", "LabelSet", "SaveAll", "SavePRM", "Format", "Backup", "FdSave", "SaveSys", "ChangeSys", "ImgLoad", "FdLoad", "HdLoad", "HdSet", "HdSave", "DiskSetup", "MidiLoad", "MidiSave", "LoadWave", "HdLoadP", "HdLoadT", "LoadImage", "DdRead", "DdWrite" };

std::vector<const pages::PageImage*> UiState::pagesOfMode (const std::string& m, bool menuOnly) const
{
    std::vector<const pages::PageImage*> v;
    for (const char* id : kOrder)
        if (const auto* p = pages::findImage (id))
            if (m == p->mode) v.push_back (p);
    if (menuOnly && m == "DISK" && v.size() > 13) v.resize (13);       // the pull-down lists the original disk pages; IMG / FD / HD are in the Disk menu
    return v;
}

std::vector<std::string> UiState::overlayItems() const
{
    std::vector<std::string> r;
    if (overlay == Overlay::ModeMenu) for (auto* m : kModes) r.push_back (m);
    else for (const auto& e : menuEntries()) r.push_back (e.first);
    return r;
}

bool UiState::twoCols() const { return overlay == Overlay::PageMenu && (mode() == "EDIT" || mode() == "DISK" || mode() == "UTIL"); }
int UiState::menuRows() const { return mode() == "DISK" ? 7 : mode() == "UTIL" ? 8 : 6; }
static int menuColCells (const std::string& m) { return m == "UTIL" ? 12 : 11; }

std::vector<std::pair<std::string, std::string>> UiState::menuEntries() const
{
    std::vector<std::pair<std::string, std::string>> r;
    if (mode() == "UTIL")
    {   // as on the device: 8 + 1 entries; the pages that do not exist yet only close the menu
        static const char* const names[9] = { "Wave Scope", "Sampling", "Truncate", "Mix", "Combine", "D.Filter", "Wave Loop", "Wave Draw",
                                               "SYS.Backup" };
        static const char* const ids[9] = { "WaveScope", "Sampling", "Truncate", "Mix", "Combine", "DFilter", "WaveLoop", "WaveDraw", "SysBackup" };
        for (int i = 0; i < 9; ++i) r.push_back ({ names[i], ids[i] });
        return r;
    }
    for (auto* p : pagesOfMode (mode(), true)) r.push_back ({ p->name, p->id });
    return r;
}

bool UiState::overlayHit (int x, int y, int& item) const
{
    const auto items = overlayItems();
    int tw = 0; Screen tmp;
    for (const auto& s : items) tw = std::max (tw, tmp.textWidth (s));
    int rx, ry, rw, rh;
    if (twoCols())
    {
        const int top2 = kPopupRow * kRowH - 1, cw = menuColCells (mode()) * kCellW, x0 = overlayX();
        if (x < x0 - 8 || x >= x0 - 8 + 2 * cw + 16 || y < top2 || y >= top2 + menuRows() * kRowH) { item = -1; return false; }
        item = std::min ((int) items.size() - 1, std::min (1, std::max (0, (x - x0 + 8) / cw)) * menuRows() + (y - top2) / kRowH);
        return true;
    }
    Screen::popupRect (overlayX(), kPopupRow, (int) items.size(), tw, rx, ry, rw, rh);
    const int top = kPopupRow * kRowH - 1;
    if (x < rx || x >= rx + rw || y < top || y >= top + (int) items.size() * kRowH) { item = -1; return false; }
    item = (y - top) / kRowH;
    return true;
}

struct PByte { const char* name; int off; bool sgn; };
static const PByte kPB[] = { { "Key Mode", 15, false }, { "Unison Detune", 239, false }, { "V-Sw Thresh.", 16, false },
                             { "P.Bend Range", 12, false }, { "Oct.Shift", 236, true }, { "Level", 237, false },
                             { "Output Assign", 243, false } };

void UiState::pullPatch() const
{
    auto& a = App::get();
    if (! a.patchBound()) return;
    const auto& pt = a.patchAt (a.curPatch);
    for (auto& p : patch)
        for (auto& e : kPB)
            if (p.label == e.name)
            {
                const int v = e.sgn ? (int) (int8_t) pt.raw[e.off] : (int) pt.raw[e.off];
                p.value = std::max (p.min, std::min (p.max, v));
            }
}

void UiState::pushPatch()
{
    auto& a = App::get();
    if (! a.patchBound() || selectedRow < 0 || selectedRow >= (int) patch.size()) return;
    auto& pt = a.patchAt (a.curPatch);
    for (auto& e : kPB)
        if (patch[(size_t) selectedRow].label == e.name) pt.raw[e.off] = (uint8_t) patch[(size_t) selectedRow].value;
}

std::string UiState::patchId() const
{
    const int n = App::get().curPatch;                            // 0-15 = I11-I28, 16-31 = II11-II28
    return std::string (n >= 16 ? "\001" : "I") + (char) ('1' + (n / 8) % 2) + (char) ('1' + n % 8);
}

bool UiState::idChange (int delta)
{
    auto& a = App::get();
    int& n = a.curPatch;
    const int nn = std::max (0, std::min (2 * data::kNumPatches - 1, n + delta));
    if (nn == n) return false;
    n = nn;
    return true;
}

std::vector<std::string> UiState::pItems() const
{
    if (pMenu == 1) return { "E.Source", "C.Source" };
    return { "Copy All PRM", "Copy Page PRM", "Init All PRM", "Init Page PRM", "Swap All PRM", "Swap Page PRM" };
}

bool UiState::pHit (int x, int y, int& item) const
{
    if (! pMenu) return false;
    const auto it = pItems();
    int tw = 0; Screen tmp;
    for (const auto& t : it) tw = std::max (tw, tmp.textWidth (t));
    int rx, ry, rw, rh;
    Screen::popupRect (pMenu == 1 ? 240 : 336, 4, (int) it.size(), tw, rx, ry, rw, rh);
    const int top = 4 * kRowH - 1;
    if (x < rx || x >= rx + rw || y < top || y >= top + (int) it.size() * kRowH) return false;
    item = (y - top) / kRowH;
    return true;
}

std::string UiState::srcId() const
{
    const int n = pSrc == 1 ? pC : App::get().curPatch;
    return std::string (n >= 16 ? "\001" : "I") + (char) ('1' + (n / 8) % 2) + (char) ('1' + n % 8);
}

// cmd: 0 Copy All, 1 Copy Page, 2 Init All, 3 Init Page, 4 Swap All, 5 Swap Page.
// Only bytes that are known (kPB) and the name are handled as a page; the rest of the 256 bytes belongs to the other pages.
void UiState::pExecute (int cmd)
{
    auto& a = App::get();
    if (! a.patchBound()) return;
    auto& e = a.patchAt (a.curPatch);
    const bool all = cmd % 2 == 0, init = cmd == 2 || cmd == 3;
    auto page = [&] (data::Patch& x, data::Patch& y, bool swap)
    {
        for (int i = 0; i < 12; ++i) { if (swap) std::swap (x.raw[i], y.raw[i]); else x.raw[i] = y.raw[i]; }
        for (auto& b : kPB) { if (swap) std::swap (x.raw[b.off], y.raw[b.off]); else x.raw[b.off] = y.raw[b.off]; }
    };
    if (init)
    {
        e.setName ("");
        e.raw[15] = 0; e.raw[239] = 0; e.raw[16] = 64; e.raw[12] = 2; e.raw[236] = 0; e.raw[237] = 127; e.raw[243] = 0;      // manual p.114
        for (auto& p : patch)                                // values that are not stored in the block yet
        {
            if (p.label == "Key Assign" || p.label == "V-Mix Ratio" || p.label == "A.T Sense") p.value = 0;
            if (p.label == "A.T Assign") p.value = 2;
        }
        return;
    }
    if (pSrc != 1 || pC == a.curPatch || ! a.boundPatch (pC)) return;
    auto& c = a.patchAt (pC);
    if (cmd < 2) { if (all) std::memcpy (e.raw, c.raw, sizeof e.raw); else page (e, c, false); }
    else { if (all) { for (int i = 0; i < data::kPatchSize; ++i) std::swap (e.raw[i], c.raw[i]); } else page (e, c, true); }
}

void UiState::render (Screen& s) const
{
    if (pageId != enteredId)       // RULE: every load / save page opens its COM (command) window as soon as it is entered
    {
        if (! enteredId.empty()) if (auto* lpOld = liveFor (enteredId)) if (lpOld->onLeave) lpOld->onLeave();       // e.g. Wave Draw gives the tone its own loop points back
        enteredId = pageId;
        if (auto* lp0 = liveFor (pageId)) if (lp0->onEnter) lp0->onEnter();
        static const char* const ids[] = { "FdSave", "Backup", "FdLoad", "ImgLoad", "LoadTone", "LoadPRM",
                                           "MidiLoad", "MidiSave" };
        for (auto* id : ids)
            if (pageId == id)
            {
                if ((pageId == "LoadTone" || pageId == "LoadPRM" || pageId == "FdSave" || pageId == "Backup") && App::get().viaImg) break;     // IMG: file first, then the destination, COM only afterwards
                if (auto* lp = liveFor (pageId))
                    if (lp->menuOpen && lp->comHook && ! lp->menuOpen()) lp->comHook();
                break;
            }
    }
    {   // on the edit pages the keyboard plays the tone / patch that is shown
        auto& a = App::get();
        const bool toneP = pageId == "ToneEdit" || pageId == "Loop" || pageId == "Lfo" || pageId == "Tvf" || pageId == "Tva" || pageId == "ToneMap" || pageId == "Truncate";
        const bool patchP = pageId == "Patch" || pageId == "Split" || pageId == "PatchMap";
        a.auditionTone = toneP ? a.curTone : (pageId == "Sampling" && a.rec.review ? a.rec.tone : -1);
        a.auditionPatch = patchP ? a.curPatch : -1;
    }
    if (utilMenu())
    {   // UTIL start menu: empty page, title with the red stars, Sel / exit
        const std::vector<std::string> blank { " ", " " };
        pages::frame (s, "UTIL-******", true, false, &blank);
    }
    else if (pageId == "Patch")
    {
        pullPatch();
        pages::PatchPrm d;
        d.id = patchId(); d.idSelected = idSel; d.showTable = nameSel; d.tableHover = tblHover;
        d.nameField = nameText(); d.name = nameText();
        while (! d.name.empty() && d.name.back() == ' ') d.name.pop_back();
        d.nameCursor = nameSel ? nameCur : -1;
        d.selected = nameSel || idSel ? -1 : (overStep || hoverRow == selectedRow ? selectedRow : -1);
        d.hoverRow = hoverRow;
        for (const auto& p : patch) d.rows.push_back ({ p.label, p.text() });
        d.menu = pMenu; d.menuSel = pSel; d.src = pSrc; d.srcId = srcId(); d.srcSelected = hdrSel && pSrc >= 0; d.menuItems = pItems();
        d.menuX = pMenu == 1 ? 240 : 336;
        d.oct = 0;
        for (const auto& pr : patch) if (pr.label == "Oct.Shift") d.oct = std::max (-2, std::min (2, pr.value));
        App::get().octView = d.oct;
        pages::patchPrm (s, d);
        drawEditHeld (s, d.oct);
    }
    else if (auto* lp = liveFor (pageId))
    {
        if (overlay != Overlay::None && lp->popupButtons)       // never two menus on top of each other: the page's own menu makes room
        { const auto fn = lp->frameNames(); pages::frame (s, lp->title, lp->popupButtons, lp->subRed, &fn, lp->comRed); }
        else lp->draw (s);
    }
    else if (const auto* img = pages::findImage (pageId))
        pages::drawImage (s, *img);

    {
        static const char* names[4] = { "MODE", "MENU", "SUB", "COM" };
        static const double cols[4] = { 3.5, 9.5, 15.5, 21.5 };
        const auto* lp = liveFor (pageId);
        const auto fn = lp ? lp->frameNames() : std::vector<std::string>();
        bool blankHover = false;
        for (int i = 0; i < 4; ++i)
        {
            const bool open = (i == 0 && overlay == Overlay::ModeMenu) || (i == 1 && overlay == Overlay::PageMenu);
            if (i != hoverTab && ! open) continue;
            std::string t = names[i];
            const int bi = i - 2;                             // btnNames: SUB, COM, left, right
            if (lp && bi >= 0 && bi < (int) fn.size() && ! fn[(size_t) bi].empty()) t = fn[(size_t) bi];
            if (t == " ")                                     // RULE: a blank SUB / COM shows a red line under the mouse: nothing here
            {
                const int bx = (int) ((cols[i] - 1) * 16);
                s.raw (bx + 14, kRowH + 8, 5 * 16 - 28, 2, Red);
                blankHover = true;
                continue;
            }
            CellOpts o; o.bg = Red; o.fg = White; o.align = Align::Center;
            s.cell (1, cols[i], 5, t, o);
        }
        if (blankHover)                                       // the right buttons while the mouse is over a blank button (photos)
        {
            CellOpts o; o.bg = Cyan; o.fg = Black; o.align = Align::Center;
            s.cell (1, 27.5, 5, "Sel", o); s.cell (1, 33.5, 5, "----", o);
        }
    }
    if (overlay != Overlay::None)
    {
        int sel = hover;
        if (sel < 0 && overlay == Overlay::ModeMenu)
        {
            const auto m = mode();
            for (int i = 0; i < 6; ++i) if (m == kModeKeys[i]) sel = i;
        }
        if (sel < 0 && overlay == Overlay::PageMenu)
        {
            const auto v = menuEntries();
            for (size_t i = 0; i < v.size(); ++i) if (pageId == v[i].second) sel = (int) i;
        }
        if (twoCols()) s.popup2 (overlayX(), kPopupRow, overlayItems(), sel, menuRows(), menuColCells (mode()));   // EDIT / DISK / UTIL menus: two columns
        else s.popup (overlayX(), kPopupRow, overlayItems(), sel, overlay == Overlay::ModeMenu);
    }
}

#define kNameChars (pages::nameChars())

bool UiState::nameBox (int x, int y, int& ch) const
{
    const int x0 = 13 * kCellW, y0 = 16 * kRowH - 1 + 4;
    if (x < x0 || x >= x0 + pages::textBoxWidth (12) || y < y0 || y >= y0 + kRowH + 1) return false;
    // find the character whose box contains the pixel (the same boxes the cursor is drawn with)
    std::string t = nameText();
    const auto sp = Screen::charSpans (t);
    const int rel = x - (x0 + 2);
    ch = (int) sp.size() - 1;
    for (size_t i = 0; i < sp.size(); ++i)
    {
        const int end = i + 1 < sp.size() ? (sp[i].x1 + sp[i + 1].x0) / 2 : 1 << 20;
        if (rel < end) { ch = (int) i; break; }
    }
    return true;
}

int UiState::nameLen() const
{
    std::string t = nameText();
    int n = (int) t.size();
    while (n > 0 && t[(size_t) n - 1] == ' ') --n;
    return std::min (11, n);                                // index of the first free character, at most 11
}

std::string UiState::nameText() const
{
    std::string t = patchName;
    auto& a = App::get();
    if (a.patchBound()) t = a.patchAt (a.curPatch).name();
    t.resize (12, ' ');
    return t;
}

void UiState::storeName (const std::string& n)
{
    patchName = n;
    auto& a = App::get();
    if (a.patchBound()) a.patchAt (a.curPatch).setName (n);
}

bool UiState::nameChange (int delta)
{
    std::string t = nameText();
    const char c = t[(size_t) nameCur];
    const int n = (int) std::string (kNameChars).size();
    int i = 0;
    for (int k = 0; k < n; ++k) if (kNameChars[k] == c) { i = k; break; }
    i = std::max (0, std::min (n - 1, i + delta));
    if (kNameChars[i] == c) return false;
    t[(size_t) nameCur] = kNameChars[i];
    storeName (t);
    return true;
}

bool UiState::key (int code)
{
    if (pageId != "Patch")
    {
        auto* lp = liveFor (pageId);
        return lp && lp->boxKey (lp->activeBlk >= 0 ? code : code & 0xffff);
    }
    code &= 0xffff;
    if (! wantsKeys()) return false;
    std::string t = nameText();
    if (code == 27 || code == 13) { nameSel = false; return true; }
    if (code == 37) { nameCur = std::max (0, nameCur - 1); return true; }
    if (code == 39) { nameCur = std::min (nameLen(), nameCur + 1); return true; }
    if (code == 8) { if (nameCur > 0) --nameCur; t[(size_t) nameCur] = ' '; storeName (t); return true; }
    if (code == 46) { t[(size_t) nameCur] = ' '; storeName (t); return true; }
    if (code >= 32 && code < 127)
    {
        t[(size_t) nameCur] = (char) code;
        storeName (t);
        nameCur = std::min (11, nameCur + 1);
        return true;
    }
    return false;
}

bool UiState::changeSelected (int delta)
{
    if (nameSel && pageId == "Patch") return nameChange (delta);
    if (hdrSel && pageId == "Patch" && pSrc >= 0)           // header patch: E.Source = the patch that is edited, C.Source = the source
    {
        if (pSrc == 1) { const int n = std::max (0, std::min (2 * data::kNumPatches - 1, pC + delta)); const bool ch = n != pC; pC = n; return ch; }
        return idChange (delta);
    }
    if (idSel && pageId == "Patch") return idChange (delta);
    if (auto* lp = liveFor (pageId)) return lp->change (delta);
    if (pageId != "Patch" || selectedRow < 0 || selectedRow >= (int) patch.size()) return false;
    pullPatch();
    const bool ch = patch[(size_t) selectedRow].change (delta);
    if (ch) pushPatch();
    return ch;
}

// the keyboard at the bottom of the edit pages (36 white keys from C2): the mouse plays the tone / patch shown
static bool editKeyAt (const std::string& pageId, int x, int y, int& ch, int& note)
{
    if (pageId != "Patch" && pageId != "Split" && pageId != "ToneMap") return false;
    if (y < 366 || y >= 416 || x < 45 || x >= 585) return false;
    static const int semi[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int base = pageId == "ToneMap" ? 36 : 36 + 12 * App::get().octView;          // the keyboard moves with Oct.Shift
    const int t = x - 45 + 4, b = t / 15 - 1, w = (x - 45) / 15;
    note = base + w / 7 * 12 + semi[w % 7];
    if (y < 366 + 30 && b >= 0 && t % 15 < 9 && b < 35 && (b % 7 == 0 || b % 7 == 1 || b % 7 == 3 || b % 7 == 4 || b % 7 == 5))
        note = base + b / 7 * 12 + semi[b % 7] + 1;
    ch = 0;
    return true;
}

void UiState::dragTo (int ch, int note)
{
    if (ch == dragCh && note == dragNote) return;
    auto& a = App::get();
    if (dragNote >= 0) a.noteSet (dragCh, dragNote, false);
    dragCh = ch; dragNote = note;
    if (dragNote >= 0) a.noteSet (dragCh, dragNote, true);
}

bool UiState::mouseUp()
{
    if (auto* lp = liveFor (pageId)) lp->bDrag = false;
    if (gripId >= 0) { gripId = -1; if (auto* lp = liveFor (pageId)) lp->graphScale = 0; return true; }
    if (dragNote < 0) return false;
    dragTo (-1, -1);
    return true;
}

bool UiState::mouseDown (int x, int y, int dir)
{
    const std::string before = pageId;
    const bool r = mouseDown0 (x, y, dir);
    if (App::get().hdOpenGrid) { App::get().hdOpenGrid = false; setPage ("HdLoad"); }
    if (before != pageId)                                       // IMG entries of the Disk menu open the Finder at once
    {
        auto& a = App::get();
        if (pageId == "HdLoad" || pageId == "HdSet" || pageId == "HdSave" || pageId == "HdLoadP" || pageId == "HdLoadT") { if (! a.hasHd) a.pickHd(); }
        else if (pageId == "FdLoad") live.erase (pageId);                    // always a fresh page: the command window is open at once
        else if (pageId == "ImgLoad") { live.erase (pageId); a.hasDisk = false; a.pickDone = false; a.pickImage (false); }
        else if (pageId == "LoadImage") a.pickImage (true);
        else if (((pageId == "LoadPRM" || pageId == "LoadTone") && a.viaImg) || (pageId == "LoadAll" && (a.viaImg || ! a.hasDisk))) a.pickImage (false);   // FD: the floppy is read by the page itself
    }
    if (overlay != Overlay::None || pageId != before)       // a menu opened or the page changed: nothing stays lit behind it
    {
        hoverRow = -1;
        if (auto* lp = liveFor (pageId)) lp->hoverField = lp->hoverGraph = -1;
        if (auto* lp = liveFor (before)) lp->hoverField = lp->hoverGraph = -1;
    }
    return r;
}

bool UiState::mouseDown0 (int x, int y, int dir)
{
    if (pageId == "Sampling" && App::get().rec.review) { App::get().rec.review = false; App::get().rec.msg.clear(); return true; }   // any click leaves the review of the new sample
    if (pageId == "Patch" && pMenu)                          // SUB / COM pull-down of the Patch page: it waits for its answer
    {
        if (dir > 0) { pMenu = 0; return true; }             // right click closes it without choosing
        int it;
        const bool tab = inButton (x, y, 424) || inButton (x, y, 520) || inButton (x, y, 232) || inButton (x, y, 328);
        if (! tab)
        {
            if (! pHit (x, y, it)) return false;
            pSel = it;
            return true;
        }
        if (inButton (x, y, 232)) { if (pMenu == 1) pMenu = 0; else { pMenu = 1; pSel = pSrc == 1 ? 1 : 0; } return true; }
        if (inButton (x, y, 328)) { if (pMenu == 2) pMenu = 0; else { pMenu = 2; pSel = 0; } return true; }
        const int m = pMenu, sel = pSel;                     // Sel / EXE and exit
        pMenu = 0;
        if (inButton (x, y, 520)) return true;
        if (m == 1) { pSrc = sel; hdrSel = false; }
        else pExecute (sel);
        return true;
    }
    if (pageId == "Patch" && overlay == Overlay::None && dir <= 0)
    {
        if (inButton (x, y, 232)) { pMenu = 1; pSel = pSrc == 1 ? 1 : 0; if (pSrc < 0) pSrc = 0; nameSel = false; idSel = false; return true; }
        if (inButton (x, y, 328)) { pMenu = 2; pSel = 0; nameSel = false; idSel = false; return true; }
        if (pSrc >= 0 && x >= 208 && x < 256 && y >= 2 * kRowH && y < 3 * kRowH)       // the patch id in the header
        {
            const bool was = hdrSel;
            hdrSel = true; idSel = false; nameSel = false;
            return ! was;
        }
        if (! (inButton (x, y, 424) || inButton (x, y, 520))) hdrSel = false;
    }
    if (overlay == Overlay::None && dir <= 0)                // graphic fields: grab a point
        if (auto* lp = liveFor (pageId))
            if (lp->graphGrab && lp->inGraph (x, y))
            {
                const int id = lp->graphGrab (x, y);
                if (id >= 0) { gripId = id; lp->hoverGraph = id; return true; }
            }
    if (overlay == Overlay::None && dir <= 0 && ! (pageId == "Patch" && nameSel && pages::nameTableHit (x, y) >= 0))
    {   // edit-page keyboards: the note sounds while the button is down (Split also moves its key cursor)
        int ch, note;
        if (editKeyAt (pageId, x, y, ch, note)) { dragTo (ch, note); return true; }
    }
    if (overlay == Overlay::None && dir <= 0)                // PLAY keyboards: the note sounds while the button is down
        if (auto* lp = liveFor (pageId))
            if (lp->keyAt)
            {
                int ch, note;
                if (lp->keyAt (x, y, ch, note)) { dragTo (ch, note); return true; }
            }
    if (overlay != Overlay::None && dir > 0)             // right click closes a pull-down without choosing
    {
        overlay = Overlay::None; hover = -1;
        return true;
    }
    if (overlay == Overlay::None && dir > 0)
        if (auto* lp = liveFor (pageId))
            if (lp->buttonHook && (lp->popupButtons || (lp->menuOpen && lp->menuOpen())))
            {
                const std::string go = lp->buttonHook (1);          // same as the exit button
                if (! go.empty()) pageId = go;
                return true;
            }
    if (overlay != Overlay::None && ! (utilMenu() && (inButton (x, y, 424) || inButton (x, y, 520))))
    {
        int item;
        if (overlayHit (x, y, item))
        {
            if (overlay == Overlay::ModeMenu)
            {
                const std::string key = kModeKeys[item];
                if (key != mode())
                {
                    const auto v = pagesOfMode (key);
                    if (! v.empty()) pageId = v.front()->id;
                }
                if (key == "UTIL") { overlay = Overlay::PageMenu; utilBlank = true; hover = 0; return true; }      // UTIL always starts with the list of all utilities
            }
            else
            {
                const auto v = menuEntries();
                if (item >= (int) v.size() || v[(size_t) item].second.empty()) return false;       // entries without a page do nothing
                pageId = v[(size_t) item].second;
            }
        }
        else return false;                                   // a click beside the entries does nothing
        overlay = Overlay::None; hover = -1;
        return true;
    }

    if (auto* mlp = liveFor (pageId))                         // a question of the page: the tabs do not react
        if (mlp->menuOpen && mlp->menuOpen() && (inButton (x, y, 40) || inButton (x, y, 136) || inButton (x, y, 232) || inButton (x, y, 328))) return false;
    if (inButton (x, y, 40))  { overlay = Overlay::ModeMenu; hover = -1; return true; }
    if (inButton (x, y, 136))
    {
        if (mode() == "DISK") { pageId = "Disk"; hover = -1; return true; }        // MENU in the disk mode = the start menu of DISK
        overlay = Overlay::PageMenu; utilBlank = false; hover = -1; return true;
    }
    if (inButton (x, y, 232))
        if (auto* lp = liveFor (pageId))
            if (lp->subHook) return lp->subHook();
    if (inButton (x, y, 328))
        if (auto* lp = liveFor (pageId))
            if (lp->comHook) return lp->comHook();
    if (inButton (x, y, 424) || inButton (x, y, 520))
    {
        const int which = inButton (x, y, 424) ? 0 : 1;      // Dec/Sel, Inc/exit
        if (utilMenu())                                       // Sel opens the lit entry, exit closes the menu
        {
            if (which == 0)
            {
                const auto v = menuEntries();
                int sel = hover;
                if (sel < 0) for (size_t i = 0; i < v.size(); ++i) if (pageId == v[i].second) sel = (int) i;
                if (sel >= 0 && sel < (int) v.size() && ! v[(size_t) sel].second.empty()) pageId = v[(size_t) sel].second;
            }
            overlay = Overlay::None; hover = -1;
            return true;
        }
        if (pageId == "Split" && ! (liveFor (pageId)->menuOpen && liveFor (pageId)->menuOpen()))      // Play sounds the key cursor while the button is down, exit leaves
        {
            if (which == 0) dragTo (0, App::get().curKey);
            else pageId = "Patch";
            return true;
        }
        auto* lp = liveFor (pageId);
        if (lp && lp->buttonHook)
        {
            const std::string go = lp->buttonHook (which);
            if (! go.empty()) { pageId = go; return true; }
            return true;
        }
        return changeSelected (which == 0 ? -1 : +1);
    }

    int nc = 0;
    if (pageId == "Patch" && nameSel)
    {
        const int cell = pages::nameTableHit (x, y);
        if (cell >= 0)
        {
            std::string t = nameText();
            int cur = nameCur;
            if (! pages::nameTableAction (cell, t, cur, 12)) return false;
            storeName (t); nameCur = cur;
            return true;
        }
    }
    if (pageId == "Patch" && idHit (x, y))
    {
        const bool was = idSel;
        idSel = true; nameSel = false;
        if (! was) return true;                              // first click only selects the field
        return dir != 0 && idChange (dir);
    }
    if (pageId == "Patch" && nameBox (x, y, nc))
    {
        const bool was = nameSel;
        nameSel = true; idSel = false;
        nc = std::min (nc, nameLen());                       // the cursor never leaves a gap behind the text
        if (! was) { nameCur = nc; return true; }          // first click only selects the box
        if (nameCur != nc) { nameCur = nc; return true; }  // a click on another character only moves the cursor
        return dir != 0 && nameChange (dir);
    }
    if (pageId == "Patch" && x >= 16 && x < 624)
        for (size_t i = 0; i < patch.size(); ++i)
        {
            const int top = (4 + (int) i) * kRowH + 3;
            if (y >= top && y < top + kRowH + 1) { const bool wasName = nameSel || idSel; nameSel = false; idSel = false; const bool ch = selectedRow != (int) i; selectedRow = (int) i; if (wasName) return true; if (dir != 0 && changeSelected (dir)) return true; return ch; }
        }
    if (auto* lp = liveFor (pageId))
    {
        const bool ch = lp->click (x, y, dir);
        if (lp->itemHit && lp->buttonHook)                    // a click on a menu entry activates it
        {
            lp->itemHit = false;
            const std::string go = lp->buttonHook (0);
            if (! go.empty()) pageId = go;
            return true;
        }
        return ch;
    }
    return false;
}

int UiState::holdDir (int x, int y, int dir) const
{
    if (overlay != Overlay::None) return 0;
    if (pageId == "Patch" && pMenu) return 0;
    if (inButton (x, y, 424) || inButton (x, y, 520))
    {
        auto* lp = liveFor (pageId);
        if (lp && lp->buttonHook && (! lp->buttonsActive || lp->buttonsActive())) return 0;   // Sel / exit do not repeat
        return inButton (x, y, 424) ? -1 : +1;
    }
    int nc = 0;
    if (pageId == "Patch" && idHit (x, y)) return idSel ? dir : 0;
    if (pageId == "Patch" && nameSel && pages::nameTableHit (x, y) >= 0) return 0;
    if (pageId == "Patch" && nameBox (x, y, nc)) return nameSel && std::min (nc, nameLen()) == nameCur ? dir : 0;
    if (pageId == "Patch" && x >= 16 && x < 624)
        for (size_t i = 0; i < patch.size(); ++i)
        {
            const int top = (4 + (int) i) * kRowH + 3;
            if (y >= top && y < top + kRowH + 1) return ! nameSel && ! idSel ? dir : 0;
        }
    if (auto* lp = liveFor (pageId))
    {
        return lp->hitSelected (x, y) || (lp->activeBox < 0 && lp->hit (x, y)) ? dir : 0;
    }
    return 0;
}

bool UiState::tick (int ms)
{
    bool d = App::get().drive && App::get().drive->tick (ms);
    if (App::get().uiDirty) { App::get().uiDirty = false; d = true; }
    return d;
}

int UiState::tabAt (int x, int y) const
{
    static const int x0[4] = { 40, 136, 232, 328 };
    for (int i = 0; i < 4; ++i) if (inButton (x, y, x0[i])) return i;
    return -1;
}

bool UiState::mouseMove (int x, int y)
{
    if (auto* lp = liveFor (pageId)) if (lp->bDrag) return lp->blockDrag (x, y);
    if (gripId >= 0)
    {
        if (auto* lp = liveFor (pageId)) if (lp->graphDrag) { lp->graphDrag (*lp, gripId, x, y); lp->hoverGraph = gripId; }
        return true;
    }
    if (dragNote >= 0)                                       // wiping over the keys with the button down
    {
        int ch = -1, note = -1;
        auto* lp = liveFor (pageId);
        if (! ((lp && lp->keyAt && lp->keyAt (x, y, ch, note)) || editKeyAt (pageId, x, y, ch, note))) { ch = -1; note = -1; }
        dragTo (ch, note);
        return true;
    }
    // the top tabs react to the mouse: they light up, and while a pull-down is open the other tab takes over
    if (pageId == "Patch" && pMenu)
    {
        int it;
        bool ch = hoverTab >= 0 || hoverRow >= 0;
        hoverTab = -1; hoverRow = -1;
        if (pHit (x, y, it) && it != pSel) { pSel = it; ch = true; }
        return ch;
    }
    bool modal = overlay != Overlay::None;                   // a menu or a COM / SUB question waits for its answer: the tabs stay dark
    if (auto* mlp = liveFor (pageId)) if (mlp->menuOpen && mlp->menuOpen()) modal = true;
    const int t = modal ? -1 : tabAt (x, y);
    bool changed = t != hoverTab;
    {
        const bool os = inButton (x, y, 424) || inButton (x, y, 520);
        if (os != overStep) { overStep = os; changed = true; }
        if (auto* lp = liveFor (pageId)) lp->showSel = os;
    }
    {
        const int th = pageId == "Patch" && nameSel ? pages::nameTableHit (x, y) : -1;
        if (th != tblHover) { tblHover = th; changed = true; }
        if (pageId != "Patch") if (auto* lp = liveFor (pageId)) if (lp->tableMove (x, y)) changed = true;
    }
    hoverTab = t;
    if (false && overlay != Overlay::None && t >= 0 && t <= 1)       // an open menu stays: only an entry or a right click ends it
    {
        const Overlay want = t == 0 ? Overlay::ModeMenu : Overlay::PageMenu;
        if (overlay != want) { overlay = want; hover = -1; changed = true; }
    }
    if (overlay == Overlay::None)
    {
        if (auto* lp = liveFor (pageId))
        {
            if (lp->menuOpen && lp->menuOpen())                // a pull-down asks for an answer: nothing behind it can be hovered
            {
                if (lp->hoverField >= 0 || lp->hoverGraph >= 0) changed = true;
                lp->hoverField = lp->hoverGraph = -1;
            }
            else if (lp->hoverMove (x, y)) changed = true;
        }
        else if (pageId == "Patch")                          // mouse over a value row
        {
            int hr = -1;
            if (x >= 315 && x < 315 + 8 * kCellW)            // only over the value itself
                for (size_t i = 0; i < patch.size(); ++i)
                {
                    const int top = (4 + (int) i) * kRowH + 3;
                    if (y >= top && y < top + kRowH + 1) hr = (int) i;
                }
            if (hr != hoverRow) { hoverRow = hr; changed = true; }
        }
    }
    if (overlay == Overlay::None)
    {
        // pull-down menus on pages (Disk menu, Load, Save, Format, ...): moving over an entry selects it
        auto* lp = liveFor (pageId);
        if (! lp || ! lp->buttonHook || ! lp->clickHook) return changed;
        const int r = lp->clickHook (x, y, 0);
        return changed || r == 2 || r == 1;
    }
    if (auto* lp = liveFor (pageId)) { if (lp->hoverField >= 0 || lp->hoverGraph >= 0) changed = true; lp->hoverField = lp->hoverGraph = -1; }
    if (hoverRow >= 0) { hoverRow = -1; changed = true; }
    int item;
    overlayHit (x, y, item);
    if (item == hover) return changed;
    hover = item;
    return true;
}

bool UiState::mouseLeave()
{
    if (dragNote >= 0 || gripId >= 0) return false;          // pointer is captured while a key or a point is held
    bool tab = hoverTab >= 0 || tblHover >= 0 || hoverRow >= 0;
    hoverTab = -1; tblHover = -1; hoverRow = -1;
    if (overStep) tab = true;
    overStep = false;
    if (auto* lp = liveFor (pageId)) { lp->tblHover = -1; if (lp->hoverField >= 0 || lp->hoverGraph >= 0) tab = true; lp->hoverField = lp->hoverGraph = -1; lp->showSel = false; }
    if (hover < 0) return tab;
    hover = -1;
    return tab || overlay != Overlay::None;
}

bool UiState::wheel (int, int, int steps)
{
    if (overlay != Overlay::None || steps == 0) return false;
    if (auto* lp = liveFor (pageId)) if (lp->menuOpen && lp->menuOpen() && ! lp->changeHook) return false;
    return changeSelected (steps);
}
}
