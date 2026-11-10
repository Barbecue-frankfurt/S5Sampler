#include <algorithm>
#include <set>
#include "LivePage.h"
#include "../Data/WaveImport.h"
#include "Pages.h"
#include "App.h"
#include "../Engine/EnvModel.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>

namespace s5
{
namespace
{
using Names = std::vector<std::string>;

Names toneIds()
{
    Names n;
    for (const char* blk : { "I", "\001" })
        for (int g = 1; g <= 4; ++g)
            for (int k = 1; k <= 8; ++k) n.push_back (std::string (blk) + std::to_string (g) + std::to_string (k));
    return n;
}
int indexOf (const Names& n, const std::string& s)
{
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == s) return (int) i;
    return 0;
}
Names toneNames()          // "I11 Kick" style entries for the Split page
{
    static const char* nick[] = { "Kick", "Snare", "HiHat", "Crash", "Cowbell", "HandClap", "Pan Shot", "Bell" };
    Names n; const Names ids = toneIds();
    for (size_t i = 0; i < ids.size(); ++i) n.push_back (ids[i] + " " + nick[i % 8]);
    return n;
}
Names noteNames()          // C 0 ... B 9, "#" instead of the blank for sharps
{
    static const char* nn[] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };
    Names n;
    for (int o = 0; o <= 9; ++o)
        for (int k = 0; k < 12; ++k) n.push_back (std::string (nn[k]) + std::to_string (o));
    return n;
}
Param mk (const char* label, int v, int lo, int hi, Names names = {}, int step = 1, int pad = 0)
{
    Param p; p.label = label; p.value = v; p.min = lo; p.max = hi; p.names = std::move (names); p.step = step; p.pad = pad;
    return p;
}
Param fastp (Param q) { q.fast = 10; return q; }   // Shift = x10
const Names kOnOff { "Off", "On" };

void addRow (LivePage& pg, Param p, int row, double lcol = 6.6875, double lw = 15, double vcol = 20.6875, double vw = 8)
{
    Field f; f.label = p.label; f.p = std::move (p); f.row = row; f.lcol = lcol; f.lw = lw; f.col = vcol; f.w = vw;
    pg.fields.push_back (std::move (f));
}
void addCell (LivePage& pg, Param p, int row, double col, double w, Align a)
{
    Field f; f.p = std::move (p); f.row = row; f.col = col; f.w = w; f.align = a;
    pg.fields.push_back (std::move (f));
}
void header (LivePage& pg, const char* id, const char* name, const char* extra)
{
    pg.before.push_back ({ 3, 3, 3, id });
    pg.before.push_back ({ 3, 6.6875, 12, name, Cyan });
    if (*extra) pg.before.push_back ({ 3, 20.6875, 8, extra, Cyan, Align::Right });
}
// header of the LFO / TVF / TVA pages: "I46  Wave 5  Sub 35" (the sub text follows the name, left aligned)
void toneHeader (LivePage& pg, const char* id, const char* name, const char* sub)
{
    pg.before.push_back ({ 3, 3, 3, id });
    pg.before.push_back ({ 3, 6.6875, 12, name, Cyan });
    pg.before.push_back ({ 3, 15.75, 8, sub, Cyan, Align::Left });
}
void label (std::vector<StaticText>& v, int row, double col, double w, const char* t, Align a = Align::Left)
{
    v.push_back ({ row, col, w, t, Yellow, a });
}

// JavaScript-compatible pseudo random numbers used by the sample waveform drawing
struct JsRnd
{
    long long seed;
    double next()
    {
        const double d = (double) seed * 1103515245.0 + 12345.0;
        const double m = std::fmod (d, 4294967296.0);
        const uint32_t u = (uint32_t) m;
        seed = (long long) (u & 0x7fffffffu);
        return (double) seed / 2147483647.0;
    }
};
int jsRound (double v) { return (int) std::floor (v + 0.5); }

void line (Screen& s, int x0, int y0, int x1, int y1, Col c)
{
    const int n = std::max (std::abs (x1 - x0), std::abs (y1 - y0));
    for (int i = 0; i <= n; ++i)
    {
        const int x = jsRound (x0 + (double) (x1 - x0) * i / (n ? n : 1));
        const int y = jsRound (y0 + (double) (y1 - y0) * i / (n ? n : 1));
        s.raw (x, y, 2, 2, c);
    }
}

// ---------------------------------------------------------------- data binding
// Once a disk image is loaded (App::linked) the parameter pages show and edit App::block.
// Without one they keep the fixed demo values of the layout boards.
std::string trimSp (std::string s) { while (! s.empty() && s.back() == ' ') s.pop_back(); return s; }
data::Tone& curTone() { return App::get().toneAt (App::get().curTone); }
// key numbers on the disk run 11-120; the note list starts at "C 0" = key 12 (assumption, not verified on a real disk)
int keyToIndex (int key) { return std::max (0, std::min (119, key - 12)); }
int indexToKey (int idx) { return std::max (11, std::min (120, idx + 12)); }

Field* srcField (LivePage& pg) { auto* f = pg.byLabel ("Source"); return f ? f : pg.byLabel ("Tone"); }

// header cell "I23" as a tone selector (added last, so field numbers of the page do not change)
void addToneSelect (LivePage& p, const char* id)
{
    Field f; f.p = mk ("Tone", indexOf (toneIds(), id), 0, 63, toneIds()); f.row = 3; f.col = 3; f.w = 3; f.align = Align::Left; f.label = "Tone"; f.dy = 1;
    p.fields.push_back (std::move (f));
}

void bindTone (LivePage& p, std::function<void (LivePage&, data::Tone&)> load, std::function<void (LivePage&, data::Tone&)> store)
{
    p.refresh = [load] (LivePage& pg)
    {
        auto& a = App::get();
        if (! a.toneBound()) return;
        auto& t = curTone();
        if (auto* f = srcField (pg)) f->p.value = a.curTone;
        load (pg, t);
    };
    p.postChange = [store] (LivePage& pg)
    {
        auto& a = App::get();
        if (auto* f = srcField (pg)) { if (f->p.value != a.curTone) { a.curTone = std::min (f->p.value, 63); return; } }
        if (! a.toneBound()) return;
        store (pg, curTone());
    };
}
void setHeader (LivePage& pg, const data::Tone& t, bool withExtra)
{
    const Names ids = toneIds();
    pg.before[0].text = ids[(size_t) App::get().curTone];
    pg.before[1].text = trimSp (t.name);
    if (withExtra && pg.before.size() > 2)
    {
        std::string e = std::string (1, (char) ('A' + t.waveBank)) + "-" + std::to_string (t.waveStart + 1);
        if (t.waveLen > 1) e += "." + std::to_string (t.waveStart + t.waveLen);
        pg.before[2].text = e;
    }
}

// patch bytes (see README): 12 pitch bend range, 15 key mode, 16 V-Sw threshold, 236 octave shift (signed), 237 level
data::Patch& curPatch() { return App::get().patchAt (App::get().curPatch); }
int patchGet (const data::Patch& p, int off, bool sgn) { return sgn ? (int) (int8_t) p.raw[off] : (int) p.raw[off]; }
void patchSet (data::Patch& p, int off, int v) { p.raw[off] = (uint8_t) v; }
struct PatchByte { const char* name; int off; bool sgn; int plus = 0; };
const PatchByte* patchByte (const std::string& n)
{
    static const PatchByte t[] = { { "P.Bend Range", 12, false }, { "V-Sw Thresh.", 16, false }, { "Oct.Shift", 236, true }, { "Level", 237, false }, { "Unison Detune", 239, false }, { "Output Assign", 243, false, 1 } };   // 243: 0-7 = Out 1-8, 8 = Tone (CLUB_EP)
    for (auto& e : t) if (n == e.name) return &e;
    return nullptr;
}

// "Sub 35": the sub tone shown right of the tone name
std::string subText (const data::Tone& t) { return t.subTone ? "Sub " + toneIds()[(size_t) std::min (31, (int) t.sourceTone)].substr (1) : ""; }

// ---- tone parameter bytes confirmed against device photos (CLUB_EP I46 "Wave 5"); offsets in the 128 byte tone record
struct Bind { std::string label; int off; bool sgn; int plus; int def; };
int bindDecode (const Bind& b, const data::Tone& t) { return (b.sgn ? (int) (int8_t) t.at (b.off) : (int) t.at (b.off)) + b.plus; }
void bindEncode (const Bind& b, data::Tone& t, int v) { t.at (b.off) = (uint8_t) (v - b.plus); }

std::vector<Bind> envBinds (int sus, int levelBase, int rateBase)
{
    std::vector<Bind> v { { "Sus", sus, false, 1, 2 }, { "End", sus + 1, false, 1, 3 } };
    for (int k = 0; k < 8; ++k)
    {
        v.push_back ({ "Rate" + std::to_string (k + 1), rateBase + 2 * k, false, 0, 127 });
        v.push_back ({ "Level" + std::to_string (k + 1), levelBase + 2 * k, false, 0, k == 0 ? 127 : 0 });
    }
    return v;
}

void bindBytes (LivePage& p, std::vector<Bind> binds)
{
    bindTone (p,
        [binds] (LivePage& pg, data::Tone& t)
        {
            for (const auto& b : binds)
                if (auto* f = pg.byLabel (b.label)) f->p.value = std::max (f->p.min, std::min (f->p.max, bindDecode (b, t)));
            setHeader (pg, t, false);
            pg.before[2].text = subText (t);
        },
        [binds] (LivePage& pg, data::Tone& t)
        {
            for (const auto& b : binds)                                    // only bytes whose value was really changed are written
                if (auto* f = pg.byLabel (b.label))
                    if (f->p.value != std::max (f->p.min, std::min (f->p.max, bindDecode (b, t)))) bindEncode (b, t, f->p.value);
        });
}

// SUB (E.Source / C.Source) and COM (Copy / Init / Swap Page PRM) pull-downs of the tone edit pages
struct ToneMenu { int menu = 0, sel = 0, src = -1, c = 0; };

void toneMenus (LivePage& p, std::vector<Bind> binds)
{
    auto st = std::make_shared<ToneMenu>();
    LivePage* self = &p;
    p.before.push_back ({ 2, 3.5, 9, "", Yellow });
    const size_t hdr = p.before.size() - 1;
    { Field m; m.p = mk ("SrcMode", -1, -1, 1); m.w = 0; m.label = "SrcMode"; p.fields.push_back (std::move (m)); }
    { Field c; c.p = mk ("CSrc", 0, 0, 63, toneIds()); c.row = 2; c.col = 13; c.w = 3; c.align = Align::Left; c.fg = Yellow; c.dy = 1; c.label = "CSrc";
      c.hideBy = "SrcMode"; c.hideWhen = -1; p.fields.push_back (std::move (c)); }
    auto base = p.refresh;
    p.refresh = [base, st, hdr] (LivePage& pg)
    {
        if (base) base (pg);
        auto& a = App::get();
        pg.byLabel ("SrcMode")->p.value = st->src;
        if (st->src == 1) pg.byLabel ("CSrc")->p.value = st->c;
        else if (a.toneBound()) pg.byLabel ("CSrc")->p.value = a.curTone;
        pg.before[hdr].text = st->src < 0 ? "" : st->src == 0 ? "E.Source" : "C.Source";
        pg.subRed = st->menu == 1; pg.comRed = st->menu == 2;
        pg.btnNames = st->menu == 1 ? std::vector<std::string> { "", "", "Sel", "exit" } : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> {};
    };
    auto basePost = p.postChange;
    p.postChange = [basePost, st] (LivePage& pg)
    {
        auto& a = App::get();
        auto* f = pg.byLabel ("CSrc");
        if (st->src == 1) { if (f->p.value != st->c) { st->c = f->p.value; return; } }
        else if (f->p.value != a.curTone) { a.curTone = std::min (f->p.value, 63); return; }
        if (basePost) basePost (pg);
    };
    auto items = [st] () -> std::vector<std::string>
    {
        if (st->menu == 1) return { "E.Source", "C.Source" };
        return { "Copy Page PRM", "Init Page PRM", "Swap Page PRM" };
    };
    auto baseEx = p.extras;
    p.extras = [baseEx, st, items] (Screen& s, const LivePage& pg)
    {
        if (baseEx) baseEx (s, pg);
        if (st->menu) s.popup (st->menu == 1 ? 240 : 336, 4, items(), st->sel);
    };
    p.menuOpen = [st] { return st->menu != 0; };
    p.buttonsActive = p.menuOpen;
    p.subHook = [st] { st->menu = st->menu == 1 ? 0 : 1; st->sel = st->src == 1 ? 1 : 0; if (st->src < 0) st->src = 0; return true; };
    p.comHook = [st] { st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; return true; };
    p.clickHook = [st, items] (int x, int y, int) -> int
    {
        if (! st->menu) return -1;
        const auto it = items();
        int tw = 0; Screen tmp;
        for (const auto& t : it) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (st->menu == 1 ? 240 : 336, 4, (int) it.size(), tw, rx, ry, rw, rh);
        const int top = 4 * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) it.size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p.buttonHook = [st, self, binds] (int which) -> std::string
    {
        if (! st->menu) { self->change (which == 0 ? -1 : +1); return ""; }
        const int m = st->menu, sel = st->sel;
        st->menu = 0;
        if (which == 1) return "";
        if (m == 1) { st->src = sel; return ""; }
        auto& a = App::get();
        if (! a.toneBound()) return "";
        auto& e = curTone();
        auto& c = a.toneAt (st->c);
        for (const auto& b : binds)
        {
            if (sel == 1) bindEncode (b, e, b.def);
            else if (st->src == 1 && &c != &e)
            {
                if (sel == 0) e.at (b.off) = c.at (b.off);
                else std::swap (e.at (b.off), c.at (b.off));
            }
        }
        return "";
    };
}
// ---------------------------------------------------------------- pages

std::unique_ptr<LivePage> toneEdit()
{
    auto p = std::make_unique<LivePage>();
    p->id = "ToneEdit"; p->title = "EDIT-Tone PRM";
    header (*p, "I23", "Cowbell", "Sub 22");
    Names ids = toneIds(); ids.push_back ("***");           // *** = no source tone (the tone is not a Sub tone)
    const Names notes = noteNames();
    int r = 4;
    addRow (*p, mk ("Original Tone", indexOf (ids, "I22"), 0, 64, ids), r++);
    addRow (*p, mk ("Original Key", indexOf (notes, "G#3"), 0, 119, notes), r++);
    addRow (*p, mk ("Pitch Follow", 1, 0, 1, kOnOff), r++);
    addRow (*p, mk ("Shift", 0, -24, 24), r++, 12.6875, 9);
    addRow (*p, mk ("Fine Tune", 0, -64, 63), r++);
    addRow (*p, mk ("P.LFO Depth", 0, 0, 127), r++);
    addRow (*p, mk ("Pitch Bender", 0, 0, 1, kOnOff), r++);
    addRow (*p, mk ("After Touch", 0, 0, 1, kOnOff), r++);
    addRow (*p, mk ("TVF", 0, 0, 1, kOnOff), r++);
    addRow (*p, mk ("Output Assign", 1, 1, 8), r++);
    addRow (*p, mk ("Level", 114, 0, 127), r++);
    p->selected = 1;
    label (p->after, 16, 6.6875, 5, "Name");
    addToneSelect (*p, "I23");
    bindTone (*p,
        [] (LivePage& pg, data::Tone& t)
        {
            pg.byLabel ("Original Tone")->p.value = t.subTone ? std::min (31, (int) t.sourceTone) : 64;
            pg.byLabel ("Original Key")->p.value = keyToIndex (t.origKey);
            pg.byLabel ("Output Assign")->p.value = 1 + std::min (7, (int) t.f1);        // tone byte 8: 0-7 = Out 1-8
            setHeader (pg, t, false);
            pg.before[2].text = subText (t);
        },
        [] (LivePage& pg, data::Tone& t)
        {
            t.f1 = (uint8_t) std::max (0, std::min (7, pg.byLabel ("Output Assign")->p.value - 1));
            if (pg.byLabel ("Original Tone")->p.value < 64) t.sourceTone = (uint8_t) std::min (31, pg.byLabel ("Original Tone")->p.value);
            t.origKey = (uint8_t) indexToKey (pg.byLabel ("Original Key")->p.value);
        });
    {
        // the black name box
        auto local = std::make_shared<std::string> ("Cowbell");
        TextBox tb; tb.row = 16; tb.col = 14; tb.len = 8;
        tb.get = [local] () { auto& a = App::get(); return a.toneBound() ? curTone().name : *local; };
        tb.set = [local] (const std::string& t)
        {
            auto& a = App::get();
            if (a.toneBound()) { curTone().name = t; curTone().name.resize (8, ' '); } else *local = t;
        };
        p->boxes.push_back (std::move (tb));
        auto r = p->refresh;
        p->refresh = [r, local] (LivePage& pg)
        {
            r (pg);
            auto& a = App::get();
            pg.before[1].text = trimSp (a.toneBound() ? curTone().name : *local);
        };
    }
    return p;
}

std::unique_ptr<LivePage> lfo()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Lfo"; p->title = "EDIT-LFO";
    toneHeader (*p, "I46", "Wave 5", "Sub 35");
    int r = 4;
    addRow (*p, mk ("Rate", 88, 0, 127), r++, 6.6875, 15, 20.5, 3);
    addRow (*p, mk ("Sync", 1, 0, 1, kOnOff), r++, 6.6875, 15, 20.5, 3);
    addRow (*p, mk ("Mode", 0, 0, 1, { "Sin", "P.H" }), r++, 6.6875, 15, 20.5, 3);
    addRow (*p, mk ("Delay", 86, 0, 127), r++, 6.6875, 15, 20.5, 3);
    addRow (*p, mk ("Offset", 0, -64, 63), r++, 6.6875, 15, 20.5, 3);
    addRow (*p, mk ("Polarity", 1, 0, 1, { "-", "+" }), r++, 6.6875, 15, 20.5, 3);
    p->selected = 0;
    addToneSelect (*p, "I46");
    // confirmed with CLUB_EP I46 and ORCHESTR I28 "Gong3": Rate (28), Sync (29), Delay (30), Mode (32: 0 Sin, 1 P.H), Offset (35, signed); Polarity stays local
    const std::vector<Bind> binds { { "Rate", 28, false, 0, 88 }, { "Sync", 29, false, 0, 1 }, { "Delay", 30, false, 0, 0 },
                                 { "Mode", 32, false, 0, 0 }, { "Offset", 35, true, 0, 0 } };
    bindBytes (*p, binds);
    toneMenus (*p, binds);
    return p;
}

// length of the wave in words: the scale of the Loop / Truncate graphs
int gLoopTone = -1;                                    // Wave Loop: the graph code shows this tone instead of the current one
struct LoopToneScope { int old; explicit LoopToneScope (int t) : old (gLoopTone) { gLoopTone = t; } ~LoopToneScope() { gLoopTone = old; } };
int loopToneIdx() { return gLoopTone >= 0 ? gLoopTone : App::get().curTone; }
double loopTotal()
{
    auto& a = App::get();
    if (a.toneBound()) { auto& t = a.toneAt (loopToneIdx()); if (t.used()) return (double) t.waveLen * data::kSegWords; }
    return 36864.0;
}

// One sample (-1..1) of the wave of the current tone (a Sub tone shows the wave of its source tone); false without a disk.
bool loopSample (int w, double& v)
{
    auto& a = App::get();
    if (! a.toneBound()) return false;
    const int tb = loopToneIdx() / 32;
    const data::Tone* t = &a.toneAt (loopToneIdx());
    if (t->subTone) t = &a.blk (tb).tones[std::min (31, (int) t->sourceTone)];
    if (! t->used()) return false;
    const int idx = t->waveStart * data::kSegWords + w;
    v = w < 0 || w >= t->waveLen * data::kSegWords || idx >= data::kWaveWords ? 0.0 : a.blk (tb).wave[t->waveBank & 1][(size_t) idx] / 2048.0;
    return true;
}

constexpr int kLoopX0 = 103, kLoopW = 420;                  // the wave on the Loop graphs (device photos)

// The Loop graph (Edit-Loop and UTIL-Wave Loop): screen types 1-3, zoom time / level, ticks of Start / Loop / End
void drawLoopGraph (Screen& s, const LivePage& pg, int st, int lp, int en)
{
        if (App::get().scopeRun) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 4.5, 12, "Start", w); }
        s.raw (16, 278, 608, 162, White);
        s.raw (77, 284, 488, 150, Ink);
        s.raw (79, 286, 484, 146, White);
        s.raw (79, 286, 484, 8, Ink);
        const int type = pg.byLabel ("Screen Type")->p.value;
        const int zt = pg.byLabel ("Zoom Time")->p.value, zl = pg.byLabel ("Zoom Level")->p.value;
        const double total = loopTotal();
        // Device photos: the whole wave sits on x 103 .. 523; start, loop and end are the white ticks on the top bar (all three
        // screen types); the wave itself is drawn on the zero line
        auto tickX = [&] (int w) { return kLoopX0 + (int) std::floor (std::max (0.0, std::min (1.0, w / total)) * kLoopW); };
        if (type == 1)
        {
            for (int k = 0; k < 4; ++k) s.raw (kLoopX0 + (kLoopW * k) / 3, 294, 1, 138, Cyan);      // the wave in thirds
            double probe;
            if (loopSample (0, probe))                                   // the real wave: lowest and highest sample of each column
            {
                const int nw = (int) total;
                for (int x = kLoopX0; x < kLoopX0 + kLoopW; ++x)
                {
                    const int a = (int) ((double) (x - kLoopX0) * nw / kLoopW), b = (int) ((double) (x - kLoopX0 + 1) * nw / kLoopW);
                    const int step = std::max (1, (b - a) / 16);
                    double lo = 0, hi = 0, v;
                    for (int w = a; w < b; w += step) if (loopSample (w, v)) { lo = std::min (lo, v); hi = std::max (hi, v); }
                    const int up = std::min (68, (int) std::floor (hi * 68.0 * zl)), dn = std::min (68, (int) std::floor (-lo * 68.0 * zl));
                    s.raw (x, 363 - up, 1, up + dn + 1, Ink);
                }
            }
            // no data (empty tone / no disk): nothing is drawn - a wave that cannot be played must not be shown
        }
        else
        {
            // Screen Type 2 / 3: the samples (one dot per word) around the Loop Point / Start Point; the cyan cross is the centre.
            // The window is 48 words wide per Zoom Time step (device photos), Zoom Level multiplies the height.
            const double pxw = 484.0 / (48.0 * zt);
            const int cx = 79 + 242, L = en - lp, centre = type == 2 ? lp : st;

            s.raw (79, 363, 484, 1, Cyan);
            const int w0 = (int) std::floor (centre - 242 / pxw) - 1, w1 = (int) std::floor (centre + 242 / pxw) + 1;
            for (int w = w0; w <= w1; ++w)
            {
                const int x = cx + (int) std::floor ((w - centre) * pxw);
                double v = 0;
                if (! loopSample (w, v)) break;                       // no wave: no dots
                const int y = 363 - (int) std::floor (68.0 * zl * v);
                if (x >= 80 && x <= 560 && y >= 294 && y < 431) s.raw (x, y, 2, 2, Ink);         // dots outside the picture are not shown
            }
            s.raw (cx, 294, 1, 138, Cyan);
        }
        const int mk[3] = { st, lp, en };
        for (int i = 0; i < 3; ++i)
            s.raw (tickX (mk[i]), 287, 1, 6, pg.hoverGraph == i ? Cyan : White);
        if (pg.hoverGraph >= 0) s.raw (tickX (mk[pg.hoverGraph]), 294, 1, 138, Cyan);          // mouse over a marker: its position
}

std::unique_ptr<LivePage> loop()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Loop"; p->title = "EDIT-Loop";
    header (*p, "I21", "Crash", "A-1.2");
    int r = 4;
    addRow (*p, mk ("Loop Mode", 1, 0, 3, { "Foward", "Alter", "1 Shot", "Reverse" }), r++);
    addRow (*p, mk ("Loop Tune", 0, -64, 63), r++);
    addRow (*p, fastp (mk ("Start Point", 0, 0, 221184, {}, 1, 6)), r++);
    addRow (*p, fastp (mk ("Loop Point", 32923, 0, 221184, {}, 1, 6)), r++);
    addRow (*p, fastp (mk ("End Point", 36864, 0, 221184, {}, 1, 6)), r++);
    addRow (*p, mk ("Search Mode", 0, 0, 2, { "\x7f 1", "\x7f 114", "Peak" }), r++);
    addRow (*p, mk ("Loop Edit", 0, 0, 1, { "Point", "Length" }), r++);
    addRow (*p, mk ("Screen Type", 1, 1, 3), r++);
    addRow (*p, mk ("Zoom Time", 1, 1, 7), r++);
    addRow (*p, mk ("Zoom Level", 1, 1, 7), r++);
    p->selected = 2;
    p->preChange = [] (LivePage& pg)
    {
        const int mode = pg.byLabel ("Search Mode")->p.value;
        const int step = mode == 1 ? 114 : 1;
        for (const char* l : { "Start Point", "Loop Point", "End Point" }) pg.byLabel (l)->p.step = step;
    };
    {
        auto pre = p->preChange;
        addToneSelect (*p, "I21");
        bindTone (*p,
            [] (LivePage& pg, data::Tone& t)
            {
                pg.byLabel ("Loop Mode")->p.value = t.loopMode;
                pg.byLabel ("Start Point")->p.value = (int) t.startPoint;
                pg.byLabel ("Loop Point")->p.value = (int) t.loopPoint;
                pg.byLabel ("End Point")->p.value = (int) t.endPoint;
                setHeader (pg, t, true);
            },
            [] (LivePage& pg, data::Tone& t)
            {
                t.loopMode = (uint8_t) pg.byLabel ("Loop Mode")->p.value;
                t.startPoint = (uint32_t) pg.byLabel ("Start Point")->p.value;
                t.loopPoint = (uint32_t) pg.byLabel ("Loop Point")->p.value;
                t.endPoint = (uint32_t) pg.byLabel ("End Point")->p.value;
            });
        p->preChange = pre;
    }
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        const int st = pg.byLabel ("Start Point")->p.value, lp = pg.byLabel ("Loop Point")->p.value,
                  en = pg.byLabel ("End Point")->p.value;
        // length read-outs on the right
        s.setShift (4);
        CellOpts y; y.fg = Yellow;
        s.cell (6, 29.6875, 12, "Loop Length", y);
        CellOpts v; v.align = Align::Right;
        CellOpts vc = v; vc.fg = Cyan;                    // the loop length is shown in cyan
        auto pad6 = [] (int n) { std::string t = std::to_string (std::max (0, n)); while (t.size() < 6) t = "0" + t; return t; };
        s.cell (7, 29.6875, 9, pad6 (en - lp), vc);
        s.cell (9, 29.6875, 12, "Wave Length", y);
        s.cell (10, 29.6875, 9, pad6 (en), v);
        s.setShift (0);

        drawLoopGraph (s, pg, st, lp, en);
    };
    p->gx = 77; p->gy = 284; p->gw = 488; p->gh = 150;
    LivePage* self = p.get();
    p->graphGrab = [self] (int x, int y) { return self->graphAt (x, y); };
    p->graphAt = [self] (int x, int y) -> int          // the ticks on the top bar: 0 start, 1 loop, 2 end
    {
        if (y < 286 || y > 300) return -1;
        int best = -1, bd = 7;
        const char* nm[3] = { "Start Point", "Loop Point", "End Point" };
        for (int i = 0; i < 3; ++i)
        {
            const int mxp = kLoopX0 + (int) std::floor (std::max (0.0, std::min (1.0, self->byLabel (nm[i])->p.value / loopTotal())) * kLoopW);
            if (std::abs (x - mxp) < bd) { bd = std::abs (x - mxp); best = i; }
        }
        return best;
    };
    p->graphDrag = [] (LivePage& pg, int id, int x, int) 
    {
        const int v = (int) std::floor (std::max (0, std::min (kLoopW, x - kLoopX0)) * loopTotal() / kLoopW);
        const int st = pg.byLabel ("Start Point")->p.value, lp = pg.byLabel ("Loop Point")->p.value, en = pg.byLabel ("End Point")->p.value;
        if (id == 0) pg.setValue ("Start Point", std::min (v, std::min (lp, en)));
        else if (id == 1) pg.setValue ("Loop Point", std::max (st, std::min (v, en)));
        else pg.setValue ("End Point", std::max (v, std::max (st, lp)));
    };
    return p;
}

// Envelope graph geometry (device photo): Rate 127 = jump (vertical line), lower rates take longer (exponential feel);
// at the Sustain point the level is held for a fixed width (red line); points are small black squares.
constexpr int kEnvX0 = 85, kEnvXMax = 563, kEnvY0 = 428, kEnvHold = 40;
// Time axis measured on device photos (Disp.Zoom 1..4, same envelope): each zoom step doubles the horizontal scale; a segment
// takes (level change) / speed(rate), the speed grows about 3.6 % per Rate step; Rate 127 = jump (vertical line).
double envPx (int rate, int dl, int zoom)
{
    if (rate >= 127 || dl <= 0) return 0;
    return env::pxPerLevel (rate) * env::zoomFactor (zoom) * dl;
}
int envRateFor (double px, int dl, int zoom)             // the Rate whose segment is closest to px wide
{
    int best = 127; double bd = 1e9;
    for (int r = 127; r >= 0; --r) { const double d = std::abs (envPx (r, dl, zoom) - px); if (d < bd) { bd = d; best = r; } }
    return best;
}
struct EnvPt { int x, y, id; };                  // id = stage 1..End, 0 = end of the sustain hold
struct EnvGeo
{
    int end = 2, sus = 1, zoom = 3; int rt[8] = {}, lv[8] = {};
    std::vector<EnvPt> pts; int holdSeg = -1;    // segment pts[holdSeg] -> pts[holdSeg + 1] is the sustain hold
    int dl (int i) const { return std::abs (lv[i] - (i > 0 ? lv[i - 1] : 0)); }          // level change of stage i + 1
    int stageX0 (int i) const { for (size_t k = 0; k < pts.size(); ++k) if (pts[k].id == i - 1) return pts[k].x + (i - 1 == sus && i > 1 ? kEnvHold : 0); return kEnvX0; }
};
EnvGeo envGeo (const LivePage& pg)
{
    EnvGeo g;
    g.end = pg.byLabel ("End")->p.value;
    g.sus = pg.byLabel ("Sus")->p.value;
    g.zoom = pg.byLabel ("Disp.Zoom")->p.value;
    for (int i = 0; i < 8; ++i)
    {
        g.rt[i] = pg.byLabel (("Rate" + std::to_string (i + 1)).c_str())->p.value;
        g.lv[i] = pg.byLabel (("Level" + std::to_string (i + 1)).c_str())->p.value;
    }
    const bool hold = g.sus >= 1 && g.sus <= g.end;
    double px = kEnvX0;
    g.pts.push_back ({ kEnvX0, kEnvY0, 0 });
    for (int i = 0; i < g.end; ++i)
    {
        px += envPx (g.rt[i], g.dl (i), g.zoom);
        const int y = kEnvY0 - jsRound (g.lv[i] * 1.15);
        g.pts.push_back ({ jsRound (px), y, i + 1 });
        if (hold && i + 1 == g.sus) { g.holdSeg = (int) g.pts.size() - 1; px += kEnvHold; g.pts.push_back ({ jsRound (px), y, 0 }); }
    }
    return g;
}

// TVF and TVA share the break point table and the envelope picture
void envelopePage (LivePage& p, const int* rate, const int* level, int sus, int end)
{
    label (p.before, 10, 10.9, 3, "Sus");
    label (p.before, 10, 26.5, 3, "End");
    p.before.push_back ({ 11, 3, 6, "Point", Yellow });
    p.before.push_back ({ 12, 3, 6, "Rate", Yellow });
    p.before.push_back ({ 13, 3, 6, "Level", Yellow });
    for (int i = 0; i < 8; ++i)
        p.before.push_back ({ 11, 8.875 + 3.859 * i, 3, std::to_string (i + 1), White, Align::Center });
    addCell (p, mk ("Sus", sus, 1, 7), 10, 14.75, 3, Align::Right);
    addCell (p, mk ("End", end, 2, 8), 10, 30.25, 3, Align::Right);
    for (int i = 0; i < 8; ++i) addCell (p, mk (("Rate" + std::to_string (i + 1)).c_str(), rate[i], 0, 127), 12, 8.875 + 3.859 * i, 3, Align::Right);
    for (int i = 0; i < 8; ++i) addCell (p, mk (("Level" + std::to_string (i + 1)).c_str(), level[i], 0, 127), 13, 8.875 + 3.859 * i, 3, Align::Right);
    p.extras = [] (Screen& s, const LivePage& pg)
    {
        s.raw (16, 268, 608, 172, White);
        s.raw (77, 274, 488, 160, Black);
        s.raw (79, 276, 484, 156, White);
        const auto g = envGeo (pg);
        for (size_t i = 1; i < g.pts.size(); ++i)                  // the picture is cut at the right edge of the frame
        {
            const auto &a = g.pts[i - 1]; auto b = g.pts[i];
            if (a.x >= kEnvXMax) break;
            if (b.x > kEnvXMax) { b.y = a.y + (int) ((double) (b.y - a.y) * (kEnvXMax - a.x) / (b.x - a.x) + (b.y > a.y ? 0.5 : -0.5)); b.x = kEnvXMax; }
            line (s, a.x, a.y, b.x, b.y, (int) i - 1 == g.holdSeg ? Red : Black);
        }
        for (const auto& q : g.pts)
        {
            if (q.id == 0 || q.x > kEnvXMax) continue;
            if (q.id == pg.hoverGraph) s.raw (q.x - 3, q.y - 3, 7, 7, Cyan);            // mouse over: this point can be grabbed
            else s.raw (q.x - 2, q.y - 2, 5, 5, Black);
        }
    };
    p.gx = 77; p.gy = 274; p.gw = 488; p.gh = 160;
    LivePage* self = &p;
    p.graphAt = [self] (int x, int y) -> int            // nearest break point (the start point stays at level 0)
    {
        const auto g = envGeo (*self);
        int best = -1, bd = 9 * 9 + 1;
        for (const auto& q : g.pts)
        {
            if (q.id == 0 || q.x > kEnvXMax) continue;
            const int dx = x - q.x, dy = y - q.y;
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = q.id; }
        }
        return best;
    };
    p.graphGrab = [self] (int x, int y) -> int { return self->graphAt (x, y); };
    // The held point follows the mouse in every direction.  Up/down = Level; left/right = the Rate of its segment.  The next
    // segment gets the Rate that keeps the following points where they are.
    p.graphDrag = [] (LivePage& pg, int i, int x, int y)
    {
        const auto g = envGeo (pg);
        const std::string n = std::to_string (i);
        const int lvl = std::max (0, std::min (127, jsRound ((kEnvY0 - y) / 1.15)));
        int nextX = 0;
        for (size_t k = 0; k < g.pts.size(); ++k) if (g.pts[k].id == i + 1) nextX = g.pts[k].x;
        pg.setValue ("Level" + n, lvl);
        const int prev = i > 1 ? g.lv[i - 2] : 0;
        const int px = std::min (kEnvXMax, std::max (kEnvX0, x)) - g.stageX0 (i);
        const int r = envRateFor (px, std::abs (lvl - prev), g.zoom);
        pg.setValue ("Rate" + n, r);
        if (i < g.end)
        {
            const auto g2 = envGeo (pg);
            const int nx = g2.stageX0 (i + 1);
            if (nextX > nx) pg.setValue ("Rate" + std::to_string (i + 1), envRateFor (nextX - nx, g2.dl (i), g2.zoom));
        }
    };
}

std::unique_ptr<LivePage> tvf()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Tvf"; p->title = "EDIT-TVF";
    toneHeader (*p, "I46", "Wave 5", "Sub 35");
    int r = 4;
    for (auto& q : { mk ("Cut-off", 98, 0, 127), mk ("Resonance", 0, 0, 127), mk ("Key Follow", 20, -64, 63),
                     mk ("LFO Depth", 0, 0, 127), mk ("L.Curve", 4, 0, 5) })
        addRow (*p, q, r++, 3, 12, 14.75, 3);
    r = 4;
    for (auto& q : { mk ("EG Depth", 92, 0, 127), mk ("EG Pol.", 1, 0, 1, { "-", "+" }), mk ("Key-Rate", 0, 0, 127),
                     mk ("Vel-Rate", 0, 0, 127), mk ("Disp.Zoom", 3, 1, 7) })
        addRow (*p, q, r++, 19.8, 11, 30.25, 3);
    const int rate[8] = { 60, 60, 16, 58, 127, 127, 127, 127 }, level[8] = { 127, 0, 119, 0, 0, 0, 0, 0 };
    envelopePage (*p, rate, level, 3, 4);
    addToneSelect (*p, "I46");
    p->selected = (int) p->fields.size() - 1;
    // confirmed: Cut-off (38), Key Follow (40, signed), EG Depth (43), L.Curve (45), envelope (levels 109.., rates 110.., Sus 107, End 108)
    std::vector<Bind> binds { { "Cut-off", 38, false, 0, 127 }, { "Key Follow", 40, true, 0, 0 }, { "EG Depth", 43, false, 0, 0 }, { "L.Curve", 45, false, 0, 2 },
                              { "Resonance", 39, false, 0, 0 }, { "Key-Rate", 46, false, 0, 0 }, { "Vel-Rate", 47, false, 0, 0 } };
    for (auto& b : envBinds (107, 109, 110)) binds.push_back (b);
    bindBytes (*p, binds);
    toneMenus (*p, binds);
    return p;
}

std::unique_ptr<LivePage> tva()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Tva"; p->title = "EDIT-TVA";
    toneHeader (*p, "I46", "Wave 5", "Sub 35");
    int r = 4;
    for (auto& q : { mk ("LFO Depth", 0, 0, 127), mk ("L.Curve", 1, 0, 5), mk ("Key-Rate", 0, 0, 127),
                     mk ("Vel-Rate", 127, 0, 127), mk ("Disp.Zoom", 3, 1, 7) })
        addRow (*p, q, r++, 3, 12, 14.75, 3);
    const int rate[8] = { 78, 127, 58, 127, 127, 127, 127, 127 }, level[8] = { 127, 127, 0, 0, 0, 0, 0, 0 };
    envelopePage (*p, rate, level, 2, 3);
    addToneSelect (*p, "I46");
    p->selected = (int) p->fields.size() - 1;
    // confirmed: envelope (levels 53.., rates 54.., Sus 51, End 52) and Vel-Rate (72); LFO Depth, L.Curve and Key-Rate stay local
    auto binds = envBinds (51, 53, 54);
    binds.push_back ({ "Vel-Rate", 72, false, 0, 0 });
    bindBytes (*p, binds);
    toneMenus (*p, binds);
    return p;
}

// SUB (E.Source / C.Source) and COM (Copy / Init / Swap Page PRM) of the Split page (photos); the pages of a patch are the key maps, Key Mode and Oct.Shift
struct SplitMenu { int menu = 0, sel = 0, src = -1, c = 0; };

void splitMenus (LivePage& p)
{
    auto st = std::make_shared<SplitMenu>();
    p.before.push_back ({ 2, 3.5, 9, "", Yellow });
    const size_t hdr = p.before.size() - 1;
    { Field m; m.p = mk ("SrcMode", -1, -1, 1); m.w = 0; m.label = "SrcMode"; p.fields.push_back (std::move (m)); }
    { Field c; c.p = mk ("CSrc", 0, 0, 31, toneIds()); c.p.names.resize (32); c.row = 2; c.col = 13; c.w = 3; c.align = Align::Left; c.fg = Yellow; c.dy = 1; c.label = "CSrc";
      c.hideBy = "SrcMode"; c.hideWhen = -1; p.fields.push_back (std::move (c)); }
    auto base = p.refresh;
    p.refresh = [base, st, hdr] (LivePage& pg)
    {
        if (base) base (pg);
        auto& a = App::get();
        pg.byLabel ("SrcMode")->p.value = st->src;
        pg.byLabel ("CSrc")->p.value = st->src == 1 ? st->c : a.curPatch;
        pg.before[hdr].text = st->src < 0 ? "" : st->src == 0 ? "E.Source" : "C.Source";
        pg.subRed = st->menu == 1; pg.comRed = st->menu == 2;
        pg.btnNames = st->menu == 1 ? std::vector<std::string> { "", "", "Sel", "exit" } : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> { "", "", "Play", "exit" };
    };
    auto basePost = p.postChange;
    p.postChange = [basePost, st] (LivePage& pg)
    {
        auto& a = App::get();
        auto* f = pg.byLabel ("CSrc");
        if (st->src == 1) { if (f->p.value != st->c) { st->c = f->p.value; return; } }
        else if (f->p.value != a.curPatch) { a.curPatch = std::min (f->p.value, 31); return; }
        if (basePost) basePost (pg);
    };
    auto items = [st] () -> std::vector<std::string>
    {
        if (st->menu == 1) return { "E.Source", "C.Source" };
        return { "Copy Page PRM", "Init Page PRM", "Swap Page PRM" };
    };
    auto baseEx = p.extras;
    p.extras = [baseEx, st, items] (Screen& s, const LivePage& pg)
    {
        if (baseEx) baseEx (s, pg);
        if (st->menu) s.popup (st->menu == 1 ? 240 : 336, 4, items(), st->sel);
    };
    p.menuOpen = [st] { return st->menu != 0; };
    p.buttonsActive = p.menuOpen;
    p.subHook = [st] { st->menu = st->menu == 1 ? 0 : 1; st->sel = st->src == 1 ? 1 : 0; if (st->src < 0) st->src = 0; return true; };
    p.comHook = [st] { st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; return true; };
    p.clickHook = [st, items] (int x, int y, int) -> int
    {
        if (! st->menu) return -1;
        const auto it = items();
        int tw = 0; Screen tmp;
        for (const auto& t : it) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (st->menu == 1 ? 240 : 336, 4, (int) it.size(), tw, rx, ry, rw, rh);
        const int top = 4 * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) it.size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p.buttonHook = [st] (int which) -> std::string
    {
        if (! st->menu) return "";
        const int m = st->menu, sel = st->sel;
        st->menu = 0;
        if (which == 1) return "";
        if (m == 1) { st->src = sel; return ""; }
        auto& a = App::get();
        if (! a.patchBound()) return "";
        auto& e = a.patchAt (a.curPatch);
        auto pageBytes = [] (auto fn)
        {
            fn (15);
            for (int k = 0; k < 109; ++k) { fn (17 + k); fn (126 + k); }
            fn (236);
        };
        if (sel == 1) pageBytes ([&] (int o) { e.raw[o] = 0; });                    // manual p.114: Key Mode Normal, both tones I11, Oct.Shift 0
        else if (st->src == 1 && st->c != a.curPatch && a.boundPatch (st->c))
        {
            auto& c = a.patchAt (st->c);
            if (sel == 0) pageBytes ([&] (int o) { e.raw[o] = c.raw[o]; });
            else pageBytes ([&] (int o) { std::swap (e.raw[o], c.raw[o]); });
        }
        return "";
    };
}

std::unique_ptr<LivePage> split()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Split"; p->title = "EDIT-Split";
    header (*p, "I11", "Drums/Flute", "");
    const Names tn = toneNames();
    int r = 4;
    // Type Select (photos): Info / Off = only monitor the tones of the key (Info also marks the tones that sound), otherwise a key press on the keyboard or from MIDI writes the chosen tone(s)
    for (auto& q : { mk ("Type Select", 0, 0, 4, { "Info", "1st&2nd", "1st", "2nd", "Off" }), mk ("1st Tone", 0, 0, 63, tn),
                     mk ("2nd Tone", 0, 0, 63, tn), mk ("Key Mode", 0, 0, 4, { "Normal", "V-Sw", "X-Fade", "V-Mix", "Unison" }),
                     mk ("Oct.Shift", -1, -2, 2) })
        addRow (*p, q, r++, 6.6875, 15, 20.6875, 7);
    // cyan names of the two tones (the tones of the key under the cursor, or the chosen ones while a map is written)
    p->before.push_back ({ 5, 28.4, 8, "", Cyan });          // nb + 0: name of the 1st tone
    p->before.push_back ({ 6, 28.4, 8, "", Cyan });          // nb + 1: name of the 2nd tone
    p->btnNames = { "", "", "Play", "exit" };
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        {   // list of all 32 tones of the patch's block: white = used by the patch, black = available but unused, empty slot = nothing
            const int tb = 32 * (a.curPatch / 16);
            bool used[32] = {};
            if (a.patchBound())
            {
                auto& pt = curPatch();
                const int km = patchGet (pt, 15, false);
                for (int k = 0; k < 109; ++k)
                {
                    used[pt.raw[17 + k] % 32] = true;
                    if (km >= 1 && km <= 3) used[pt.raw[126 + k] % 32] = true;       // the 2nd Tone only plays in V-Sw, X-Fade, V-Mix
                }
            }
            s.setShift (4);
            static const double nameCol[4] = { 4.4, 13.6, 22.8, 32.0 };
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            const bool b2 = a.curPatch >= 16;
            for (int c = 0; c < 4; ++c)
            {
                s.cell (9, nameCol[c] + 3.2, 3, std::string (b2 ? "\001" : "I") + (char) ('1' + c), y);
                for (int r = 0; r < 8; ++r)
                {
                    const int ti = tb + c * 8 + r;
                    if (! a.boundTone (ti)) continue;
                    const std::string nm = trimSp (a.toneAt (ti).name);
                    if (nm.empty()) continue;
                    CellOpts o; o.fg = used[c * 8 + r] ? White : Black; o.align = Align::Left;
                    s.cell (10 + r, nameCol[c], 9, nm, o);
                }
            }
            for (int r = 0; r < 8; ++r) s.cell (10 + r, 2.5, 1, std::to_string (r + 1), y);
            if (pg.byLabel ("Type Select")->p.value == 0 && a.patchBound())
            {   // Info: a violet arrow beside every tone that sounds for a key that is held now
                auto& pt = curPatch();
                const int km = patchGet (pt, 15, false);
                bool arrow[32] = {};
                for (int ch = 0; ch < 16; ++ch)
                    for (int k = 12; k < 12 + 109; ++k)
                        if (a.held[ch][k])
                        {
                            arrow[pt.raw[17 + k - 12] % 32] = true;
                            if (km >= 1 && km <= 3) arrow[pt.raw[126 + k - 12] % 32] = true;
                        }
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 8; ++r)
                        if (arrow[c * 8 + r]) { CellOpts o; o.fg = Violet; o.align = Align::Left; s.cell (10 + r, nameCol[c] - 0.9, 1, "\003", o); }
            }
            s.setShift (0);
        }
        pages::keyboard (s, false, a.octView); drawEditHeld (s, a.octView);
        if (! a.linked) return;
        const char* second = "2nd"; const char* first = "1st";
        for (int i = 0; i < 3; ++i) { s.glyph (593 + i * 8, 336, second[i], Red, false); s.glyph (593 + i * 8, 351, first[i], Ink, false); }
    };
    {
        Field k; k.p = mk ("Key", 0, 0, 119, noteNames()); k.row = 10; k.col = 23; k.w = 0; k.lcol = 6.6875; k.lw = 0; k.label = "Key"; k.lblHit = false;
        p->fields.push_back (std::move (k));
    }
    {
        Field f; f.p = mk ("Patch", 0, 0, 31, toneIds()); f.row = 3; f.col = 3; f.w = 3; f.align = Align::Left; f.label = "Patch"; f.dy = 1;
        f.p.names.resize (32);
        p->fields.push_back (std::move (f));
    }
    p->refresh = [] (LivePage& pg)
    {
        auto& a = App::get();
        pg.byLabel ("Patch")->p.value = a.curPatch;
        pg.before[0].text = toneIds()[(size_t) a.curPatch];
        auto& pt = curPatch();                                    // the key maps work on the patch even before a disk is loaded
        if (a.patchBound())
        {
            pg.before[1].text = trimSp (pt.name());
            pg.byLabel ("Key Mode")->p.value = std::min (4, patchGet (pt, 15, false));
            pg.byLabel ("Oct.Shift")->p.value = std::max (-2, std::min (2, patchGet (pt, 236, true)));
        }
        const Names tn = toneIds();
        // a key pressed on the keyboard (mouse or MIDI) becomes the key under the cursor; unless Type Select is Off it also receives the chosen tone(s)
        static int seen = -1;
        if (seen < 0) seen = a.pressSeq;
        const int sw = pg.byLabel ("Type Select")->p.value;                  // 0 Info, 1 1st&2nd, 2 1st, 3 2nd, 4 Off
        const bool setMode = sw >= 1 && sw <= 3;
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
        if (a.pressSeq != seen)
        {
            seen = a.pressSeq;
            if (nowMs - a.pressMs < 600 && a.pressKey >= 12 && a.pressKey <= 120)
            {
                a.curKey = a.pressKey;
                if (setMode)
                {
                    const int ki2 = a.pressKey - 12;
                    if (sw == 1 || sw == 2) pt.raw[17 + ki2] = (uint8_t) (pg.byLabel ("1st Tone")->p.value & 31);
                    if (sw == 1 || sw == 3) pt.raw[126 + ki2] = (uint8_t) (pg.byLabel ("2nd Tone")->p.value & 31);
                }
            }
        }
        const int tb = 32 * (a.curPatch / 16), kix = std::max (0, std::min (108, a.curKey - 12));
        const size_t nb = pg.before.size() - 3;                   // before[nb], before[nb + 1]: tone names (the last entry is the header of the menus)
        for (int m = 0; m < 2; ++m)
        {
            Param& q = pg.byLabel (m ? "2nd Tone" : "1st Tone")->p;
            q.names = tn;
            const int mapTone = std::min (63, tb + (int) pt.raw[(m ? 126 : 17) + kix] % 32);
            if (! setMode) q.value = mapTone;                                   // Off: the tones of the last key
            const std::string nm = a.boundTone (q.value) ? trimSp (a.toneAt (q.value).name) : std::string();
            pg.before[nb + (size_t) m].text = nm;
        }
        pg.byLabel ("Key")->p.value = keyToIndex (a.curKey);
        a.octView = pg.byLabel ("Oct.Shift")->p.value;
    };
    p->postChange = [] (LivePage& pg)
    {
        auto& a = App::get();
        const int sel = pg.byLabel ("Patch")->p.value;
        if (sel != a.curPatch) { a.curPatch = sel; return; }
        if (! a.patchBound()) return;
        auto& pt = curPatch();
        const int kv = indexToKey (pg.byLabel ("Key")->p.value);
        if (kv != a.curKey) { a.curKey = kv; return; }
        patchSet (pt, 15, pg.byLabel ("Key Mode")->p.value);
        patchSet (pt, 236, pg.byLabel ("Oct.Shift")->p.value);
    };
    splitMenus (*p);
    return p;
}

std::unique_ptr<LivePage> master()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Master"; p->title = "FUNC-Master";
    addRow (*p, mk ("Master Tune", 0, -64, 63), 5);
    p->refresh = [] (LivePage& pg) { const int v = (int8_t) App::get().fpar[0]; pg.fields[0].p.value = std::max (-64, std::min (63, v)); };
    p->postChange = [] (LivePage& pg) { App::get().fpar[0] = pg.fields[0].p.value & 0xff; };      // signed byte, like the tone data
    return p;
}

// MIDI Monitor (idle picture confirmed by a device photo): Display CH, Real time MSG, hex dump of the last bytes
std::unique_ptr<LivePage> midiMonitor()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Monitor"; p->title = "MIDI-Monitor";
    p->btnNames = { " ", " " };
    Names ch; for (int i = 1; i <= 16; ++i) ch.push_back (std::to_string (i)); ch.push_back ("All");
    auto row = [&] (Param q, int r, const char* lab)
    {
        Field f; f.label = lab; f.p = std::move (q); f.row = r; f.col = 21; f.w = 3; f.align = Align::Right; f.lcol = 7.4; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    row (mk ("Display CH", 0, 0, 16, ch), 4, "Display CH");
    row (mk ("Real time MSG", 0, 0, 1, kOnOff), 5, "Real time MSG");
    p->postChange = [] (LivePage& pg)
    {
        auto& a = App::get();
        a.monCh = pg.byLabel ("Display CH")->p.value; a.monRt = pg.byLabel ("Real time MSG")->p.value != 0;
    };
    p->extras = [] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        s.setShift (11);                                       // header and values sit a little lower than the labels
        CellOpts y; y.fg = Yellow; y.align = Align::Right;
        s.cell (4, 30, 4, "Key.", y); s.cell (4, 34.75, 4, "Vel.", y);
        CellOpts v; v.fg = White; v.align = Align::Right;
        if (a.lastKind != 0) { s.cell (5, 30.25, 3, std::to_string (a.lastKey), v); s.cell (5, 34.9, 3, std::to_string (a.lastVel), v); }
        s.setShift (0);
        // hex dump: 18 bytes per row, 10 rows; status bytes (80-FF) are red
        static const char* hx = "0123456789ABCDEF";
        for (size_t i = 0; i < a.midiLog.size(); ++i)
        {
            const uint8_t b = a.midiLog[i];
            const Col c = b >= 0x80 ? Red : White;
            const int x = 44 + 31 * (int) (i % 18), y = (7 + (int) (i / 18)) * kRowH + 1;
            s.glyph (x, y, hx[b >> 4], c, false);
            s.glyph (x + 9, y, hx[b & 15], c, false);
        }
    };
    return p;
}

std::unique_ptr<LivePage> message()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Message"; p->title = "MIDI-Message"; p->showCom = true;
    const Names ch = [] { Names n; for (int i = 1; i <= 16; ++i) n.push_back (std::to_string (i)); n.push_back ("Off"); return n; }();
    constexpr double kC0 = 8.69, kPitch = 3.9;                    // values right-aligned: photo of the original (pitch 62 px)
    for (int i = 0; i < 8; ++i)
        p->before.push_back ({ 4, kC0 + kPitch * i, 3, std::string (1, (char) ('A' + i)) + "-*", Yellow, Align::Right });
    const int rx[8] = { 3, 10, 1, 2, 5, 6, 7, 8 };
    struct Row { const char* l; const char* shown; int on; };
    const Row rows[] = { { "P.Chg", "P.Chg", 1 }, { "Bender", "Bend", 1 }, { "Bend Range", "B.Rng", 0 }, { "Mod.", "Mod", 1 }, { "Hold", "Hold", 1 }, { "A.Touch", "A.T", 0 }, { "Volume", "Vol", 0 } };
    label (p->before, 5, 3, 6, "RX-CH");
    for (int i = 0; i < 8; ++i)
    {
        Field f; f.p = mk ("RX-CH", rx[i] - 1, 0, 16, ch); f.p.label = "RX-CH" + std::to_string (i);
        f.row = 5; f.col = kC0 + kPitch * i; f.w = 3; f.align = Align::Right;
        p->fields.push_back (f);
    }
    int r = 6;
    for (const auto& row : rows)
    {
        label (p->before, r, 3, 6, row.shown);
        for (int i = 0; i < 8; ++i)
        {
            Field f; f.p = mk (row.l, row.on, 0, 1, kOnOff); f.row = r; f.col = kC0 + kPitch * i; f.w = 3; f.align = Align::Right;
            p->fields.push_back (f);
        }
        ++r;
    }
    label (p->before, 15, 3, 10, "Exclusive");
    label (p->before, 16, 3, 10, "Device ID");
    addCell (*p, mk ("Exclusive", 0, 0, 1, kOnOff), 15, 12.5, 3, Align::Right);
    addCell (*p, mk ("Device ID", 1, 1, 16), 16, 12.5, 3, Align::Right);
    // values live in App::mpar (MIDI parameter block of the S-550): rows RX-CH, P.Chg, Bender, Bend Range, Mod., Hold, A.Touch, Volume
    static const int base[8] = { 0x20, 0x28, 0x30, 88, 0x38, 64, 72, 80 };
    p->refresh = [] (LivePage& pg)
    {
        auto& a = App::get();
        for (int r = 0; r < 8; ++r) for (int i = 0; i < 8; ++i) pg.fields[(size_t) (r * 8 + i)].p.value = std::max (0, std::min (r == 0 ? 16 : 1, a.mpar[base[r] + i]));
        pg.fields[64].p.value = a.mpar[97] ? 1 : 0;
        pg.fields[65].p.value = std::max (1, std::min (16, a.mpar[98] + 1));
        pg.btnNames = { " " };                                  // the SUB button is blank on the MIDI pages (photo)
    };
    p->extras = [] (Screen& s, const LivePage&)                   // the S/Load (P.CHG) setup of DISK-Setup is shown beside Exclusive / Device ID (photo)
    {
        const int c = App::get().setupPchg;
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        s.cell (15, 19.4, 22, "HD Load Set (P.Chg)", y);
        s.cell (16, 21.4, 10, c == 0 ? std::string ("CH Off") : "CH " + std::to_string (c), y);
    };
    p->postChange = [] (LivePage& pg)
    {
        auto& a = App::get();
        for (int r = 0; r < 8; ++r) for (int i = 0; i < 8; ++i) a.mpar[base[r] + i] = pg.fields[(size_t) (r * 8 + i)].p.value;
        a.mpar[97] = pg.fields[64].p.value; a.mpar[98] = pg.fields[65].p.value - 1;
    };
    return p;
}

std::unique_ptr<LivePage> progNo()
{
    auto p = std::make_unique<LivePage>();
    p->id = "ProgNo"; p->title = "MIDI-Prog #"; p->showCom = true;
    const char* labs[] = { "I1", "I2", "\0011", "\0012" };
    constexpr double kC0 = 7.6, kPitch = 3.9;                     // values right-aligned, the head numbers one cell further left (photo)
    for (int i = 0; i < 8; ++i)
        p->before.push_back ({ 4, kC0 - 1 + kPitch * i, 3, std::to_string (i + 1), Yellow, Align::Right });
    for (int j = 0; j < 4; ++j)
    {
        label (p->before, 5 + 3 * j, 4, 4, labs[j]);
        for (int i = 0; i < 8; ++i)
        {
            Field f; f.p = mk ("Prog", 8 * j + i + 1, 1, 128); f.row = 5 + 3 * j; f.col = kC0 + kPitch * i; f.w = 3; f.align = Align::Right;
            p->fields.push_back (f);
        }
    }
    p->refresh = [] (LivePage& pg) { auto& a = App::get(); for (int n = 0; n < 32; ++n) pg.fields[(size_t) n].p.value = std::max (1, std::min (128, a.mpar[99 + n] + 1)); pg.btnNames = { " " }; };
    p->postChange = [] (LivePage& pg) { auto& a = App::get(); for (int n = 0; n < 32; ++n) a.mpar[99 + n] = pg.fields[(size_t) n].p.value - 1; };
    return p;
}

// ---------------------------------------------------------------- UTIL / EDIT pages on the "Mix" grid
// label col 6.6875, value right edge at col 23.5625, tone name (cyan) at col 24.3125
Names toneNameTable()
{
    static const char* nick[] = { "Kick", "Snare", "HiHat", "Crash", "Cowbell", "HandClap", "Pan Shot", "Bell" };
    Names n;
    for (int i = 0; i < 64; ++i) n.push_back (nick[i % 8]);
    n[indexOf (toneIds(), "I23")] = "Cowbell";  n[indexOf (toneIds(), "I24")] = "HandClap";
    n[indexOf (toneIds(), "I11")] = "Kick";     n[indexOf (toneIds(), "I12")] = "Snare";
    n[indexOf (toneIds(), "I22")] = "Pan Shot";
    return n;
}


// a tone selector: label, "I23" cell, tone name read-out (drawn in extras, follows the value)
void mixTone (LivePage& p, const char* lab, const char* id, int row, const char* extra)
{
    Field f; f.p = mk (lab, indexOf (toneIds(), id), 0, 63, toneIds()); f.row = row; f.col = 20.375; f.w = 3.1875;
    f.label = lab; f.lcol = 6.6875; f.lw = 14;
    p.fields.push_back (std::move (f));
    if (*extra) p.before.push_back ({ row, 33.0625, 7, extra, Cyan });
}
void mixValue (LivePage& p, Param q, int row, double w = 3.1875)
{
    Field f; f.label = q.label; f.p = std::move (q); f.row = row; f.col = 23.5625 - w; f.w = w; f.lcol = 6.6875; f.lw = 14;
    p.fields.push_back (std::move (f));
}

// Remaining Time like the HD pages: IA IB / IIA IIB (free time at 30 kHz; Block II is always empty here)
void remTime (Screen& s, int r)
{
    CellOpts y; y.fg = Yellow;
    s.setShift (4);
    s.cell (r, 8.5, 15, "Remaining Time", y);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
        {
            char buf[12];
            const int bank = col;
            std::snprintf (buf, sizeof buf, "%c-%.1fs", bank ? 'B' : 'A', row == 0 ? data::secondsFree (App::get().block, bank) : data::secondsFree (App::get().block2, bank));
            const double c0 = 24.3 + col * 7.6;
            if (row == 0) s.cell (r, c0, 8, std::string ("I") + buf, y);
            else
            {
                s.cell (r + 1, c0, 9, std::string ("\001") + buf, y);          // same advance as the I: the A stands under the A above
            }
        }
    s.setShift (0);
}

void remBlock (Screen& s, int r, double lcol = 24.375, double c0 = 27, int shift = 4, double lw = 15, bool same = false, int firstRow = 1)
{
    CellOpts y; y.fg = Yellow;
    s.setShift (shift);
    s.cell (r, lcol, lw, "Remaining Time", y);
    static const char* a[] = { "I", "I", "II", "II" };
    static const char* b[] = { "A", "B", "A", "B" };
    std::string t[4];
    for (int i = 0; i < 4; ++i)                                  // free time per bank (30 kHz); Block II is always empty here
    {
        char buf[12]; std::snprintf (buf, sizeof buf, "-%.1fs", i < 2 ? data::secondsFree (App::get().block, i) : data::secondsFree (App::get().block2, i - 2));
        t[i] = buf;
    }
    for (int i = 0; i < 4; ++i)
    {
        const int row = r + firstRow + i;
        CellOpts c; c.fg = Yellow; c.align = Align::Center;
        if (std::string (a[i]) == "II")
        {
            s.cell (row, c0, 1, "\001", c);
        }
        else s.cell (row, c0, 1, "I", c);
        s.cell (row, c0 + 1, 1, b[i], c);
        for (int k = 0; k < 5; ++k) s.cell (row, c0 + 2 + k, 1, std::string (1, t[same ? 2 : i][(size_t) k]), c);
    }
    s.setShift (0);
}

void toneNameAt (Screen& s, const LivePage& pg, const char* lab, int row)
{
    const Field* f = pg.byLabel (lab);
    CellOpts c; c.fg = Cyan;
    s.setShift (4);
    auto& app = App::get();
    s.cell (row, 24.3125, 8, app.boundTone (f->p.value) && f->p.value < 64 ? trimSp (app.toneAt (f->p.value).name) : toneNameTable()[(size_t) f->p.value], c);
    s.setShift (0);
}

void jsPopup (Screen& s, double col, int row0, double wc, const Names& items, int sel);
void tightPopup (Screen& s, double col, int row0, double wc, const Names& items, int sel);

// Remaining Time in two columns (IA IB / IIA IIB), label on row r, the values from row r + 1
void remTwoCol (Screen& s, int r, double lcol, double c0, double c1)
{
    CellOpts y; y.fg = Yellow; y.align = Align::Left;
    s.cell (r, lcol, 14, "Remaining Time", y);
    static const char* bk[4] = { "A", "B", "A", "B" };
    for (int i = 0; i < 4; ++i)
    {
        const int row = r + 1 + i / 2; const double c = i % 2 ? c1 : c0;
        char buf[12]; std::snprintf (buf, sizeof buf, "%s-%.1fs", bk[i], data::secondsFree (App::get().block2, i % 2));
        if (i < 2) s.cell (row, c, 1, "I", y);
        else
        {
            s.cell (row, c, 1, "\001", y);
        }
        s.cell (row, c + 1, 6, buf, y);
    }
}

// COM pull-down with a single red entry (Truncate, Mix): COM opens it, EXE runs ("Working" for a moment), exit closes.
// Optional SUB pull-down (Mix): entries {text, field label}; Sel selects that field (it drives header and graph).
struct ComState { int menu = 0, sel = 0, csel = 0; long long until = 0; std::string msg; };
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

void comBar (LivePage& p, const char* name, int row, double col, double wc,
             std::vector<std::pair<std::string, std::string>> subItems = {},
             std::function<std::string (LivePage&)> exec = nullptr, std::shared_ptr<ComState> st = nullptr,
             double popCol = 13.7, int popRow = -1, double popW = 11, int eraseExtra = 0, bool clipDest = false, Names comItems = {})
{
    if (popRow < 0) popRow = row;
    if (! st) st = std::make_shared<ComState>();
    LivePage* self = &p;
    auto baseRef = p.refresh;
    p.refresh = [baseRef, st] (LivePage& pg)
    {
        if (baseRef) baseRef (pg);
        if (st->until && nowMs() >= st->until) { st->until = 0; st->menu = 0; }
        const bool work = st->until != 0;
        pg.popupButtons = st->menu != 0; pg.comRed = st->menu == 2; pg.subRed = st->menu == 1;
        pg.btnNames = work ? std::vector<std::string> { "", "", "----", "----" }
                  : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" }
                  : st->menu == 1 ? std::vector<std::string> { "", "", "Sel", "exit" } : std::vector<std::string> {};
    };
    auto baseEx = p.extras;
    const int n = (int) subItems.size();
    p.extras = [baseEx, st, name, row, col, wc, subItems, popCol, popRow, popW, eraseExtra, clipDest, comItems] (Screen& s, const LivePage& pg)
    {
        if (baseEx) baseEx (s, pg);
        if (! st->msg.empty() && ! st->until && st->menu == 0) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 3.5, 14, st->msg, w); }
        if (st->menu == 2 && comItems.size() > 1)                // several commands: white box, the chosen one red (device photos)
        {
            s.eraseCut ((int) ((col + 0.5 - 1) * 16 - 8), row * kRowH + 3, (int) (wc * 16) + 20, (int) comItems.size() * kRowH - 3);
            tightPopup (s, col + 0.5, row, wc - 0.7, comItems, st->csel);
            if (st->until) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 3.5, 8, "Working", w); }
        }
        else if (st->menu == 2)
        {
            const int x = (int) ((col - 1) * 16);
            s.eraseCut (x, row * kRowH + 4, (int) (wc * 16) + 4, 24);
            s.raw (x + 4, row * kRowH + 7, (int) (wc * 16), 20, Black);
            s.setShift (4);
            CellOpts r; r.bg = Red; r.fg = White; r.dy = 0;
            s.cell (row, col, wc, name, r);
            s.setShift (0);
            if (st->until) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 3.5, 8, "Working", w); }
        }
        else if (st->menu == 1)
        {
            const double pc = popCol, pw = popW;
            Names items; for (const auto& it : subItems) items.push_back (clipDest && it.first == "Destination" ? "Destin" : it.first);
            if (clipDest)
            {
                s.eraseCut ((int) ((pc - 1) * 16 - 8), popRow * kRowH + 3, (int) (pw * 16 + 16) + 4 + eraseExtra, (int) items.size() * kRowH - 3);
                tightPopup (s, pc, popRow, pw, items, st->sel);
            }
            else
            {
                s.eraseCut ((int) ((pc - 1) * 16 - 8), popRow * kRowH - 8, (int) (pw * 16 + 16) + 4 + eraseExtra, (int) items.size() * kRowH + 20);
                jsPopup (s, pc, popRow, pw, items, st->sel);
            }
            const auto* f = pg.byLabel (subItems[(size_t) st->sel].second);
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            s.cell (2, 3, 12, subItems[(size_t) st->sel].first, y);
            if (f) s.cell (2, clipDest ? (subItems[(size_t) st->sel].first.size() > 8 ? 15.8 : 14.5) : 15.8, 4, f->p.text(), y);
        }
    };
    p.menuOpen = [st] { return st->menu != 0; };
    p.buttonsActive = p.menuOpen;
    p.comHook = [st] { if (st->until) return false; st->msg.clear(); st->menu = st->menu == 2 ? 0 : 2; return true; };
    if (n > 0)
        p.subHook = [st, self, subItems] {
            if (st->until) return false;
            st->msg.clear();
            st->menu = st->menu == 1 ? 0 : 1; st->sel = 0;
            if (st->menu == 1 && self->selected >= 0)
                for (size_t i = 0; i < subItems.size(); ++i)
                    if (self->fields[(size_t) self->selected].label.rfind (subItems[i].second, 0) == 0) st->sel = (int) i;
            return true; };
    p.clickHook = [st, n, pcw = popW, row, col, wc, popCol, popRow, comItems] (int x, int y, int) -> int
    {
        if (st->menu == 2 && comItems.size() > 1)            // several commands: a click on an entry chooses it and executes
        {
            const int bx = (int) ((col + 0.5 - 1) * 16 - 8), bw = (int) ((wc - 0.7) * 16 + 16), top = row * kRowH;
            if (x < bx || x >= bx + bw || y < top || y >= top + (int) comItems.size() * kRowH) return 0;
            st->csel = (y - top) / kRowH;
            return 2;
        }
        if (st->menu == 2)                                   // COM question: only the red bar (click = EXE) answers it
        {
            const int bx = (int) ((col - 1) * 16);
            return x >= bx && x < bx + (int) (wc * 16) && y >= row * kRowH + 4 && y < row * kRowH + 24 ? 2 : 0;
        }
        if (st->menu != 1) return -1;
        const int x0 = (int) ((popCol - 1) * 16 - 8), w = (int) (pcw * 16 + 16), top = popRow * kRowH;
        if (x < x0 || x >= x0 + w || y < top || y >= top + n * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p.buttonHook = [st, self, subItems, exec] (int which) -> std::string
    {
        if (st->until) return "";
        if (st->menu == 0) { self->change (which == 0 ? -1 : +1); return ""; }
        if (which == 1) { st->menu = 0; return ""; }
        if (st->menu == 1)
        {
            for (size_t i = 0; i < self->fields.size(); ++i)
                if (self->fields[i].label == subItems[(size_t) st->sel].second) self->selected = (int) i;
            st->menu = 0;
            if (self->openToneMap) self->openToneMap (subItems[(size_t) st->sel].second);
            return "";
        }
        if (exec)
        {
            const std::string r = exec (*self);                // EXE: runs the operation; an error text replaces "Complete"
            st->msg = r.empty() ? "Complete" : r;
            if (! r.empty()) { st->menu = 0; return ""; }
        }
        st->until = nowMs() + 1200;
        return "";
    };
}

void waveBox (Screen& s, int seed, double k, int toneIdx = -1, int bx0 = 130, int bw = 300)
{
    s.raw (16, 278, 608, 162, White);
    s.raw (77, 284, 488, 150, Ink);
    s.raw (79, 286, 484, 146, White);
    s.raw (79, 286, 484, 8, Ink);
    auto& app = App::get();
    if (app.toneBound())
    {
        // real waveform of the selected tone: min/max per column over the whole tone (full scale = +-2048)
        const int ti = toneIdx < 0 ? app.curTone : toneIdx;
        const auto& t = app.toneAt (ti);
        const auto& w = app.blk (ti / 32).wave[t.waveBank & 1];
        const int n = t.used() ? std::min ((int) t.waveLen * data::kSegWords, (int) w.size() - (int) t.waveStart * data::kSegWords) : 0;
        const int base = t.waveStart * data::kSegWords;
        if (t.used() && toneIdx >= 0 || bx0 != 130)           // cyan lines at the segment borders (Mix / Truncate)
            for (int k2 = 0; k2 <= (int) t.waveLen && t.used(); ++k2) s.raw (bx0 + (int) ((long long) bw * k2 / std::max (1, (int) t.waveLen)), 294, 1, 138, Cyan);
        for (int x = bx0; x < bx0 + bw && n > 0; ++x)    // same scale as the Start / End markers: bw px = whole tone
        {
            const int i0 = (int) ((long long) (x - bx0) * n / bw), i1 = std::max (i0 + 1, (int) ((long long) (x - bx0 + 1) * n / bw));
            int lo = 0, hi = 0;
            for (int i = i0; i < i1 && i < n; ++i) { const int v = w[(size_t) (base + i)]; lo = std::min (lo, v); hi = std::max (hi, v); }
            const int top = 363 - hi * 58 / 2048, bot = 363 - lo * 58 / 2048;
            s.raw (x, top, 1, std::max (1, bot - top + 1), Ink);
        }
        s.raw (90, 363, 450, 1, Ink);
        return;
    }
    (void) seed; (void) k;                          // nothing loaded: an empty picture (the device starts without a waveform)
    s.raw (90, 363, 450, 1, Ink);
}

std::unique_ptr<LivePage> mixPage (const char* id, const char* title)
{
    auto p = std::make_unique<LivePage>();
    p->id = id; p->title = title;
    return p;
}

std::unique_ptr<LivePage> truncate()
{
    auto p = mixPage ("Truncate", "UTIL-Truncate");
    p->shift = 0;
    {
        Field f; f.p = mk ("Source", 0, 0, 63, toneIds()); f.row = 4; f.col = 23.5625 - 6.6; f.w = 6.6; f.align = Align::Right;
        f.label = "Source"; f.lcol = 4.9; f.lw = 14; p->fields.push_back (std::move (f));
    }
    auto val = [&] (Param q, int row)
    {
        Field f; f.label = q.label; f.p = std::move (q); f.row = row; f.col = 23.5625 - 6.1875; f.w = 6.1875; f.lcol = 4.9; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    val (fastp (mk ("Start Point", 0, 0, 999999, {}, 1, 6)), 5);
    val (fastp (mk ("End Point", 36864, 0, 999999, {}, 1, 6)), 6);
    val (fastp (mk ("Shift", 0, 0, 999999, {}, 1, 6)), 7);
    val (mk ("Search Mode", 0, 0, 2, { "\x7f 1", "\x7f 114", "Peak" }), 8);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        const int ti = pg.byLabel ("Source")->p.value;
        double total = 36864.0;
        if (a.toneBound() && a.toneAt (ti).used())
        {
            const auto& t = a.toneAt (ti);
            char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1f", 'A' + (t.waveBank & 1), t.waveLen * 0.4);
            s.cell (4, 24.3125, 8, trimSp (t.name), c);
            s.cell (4, 33.0625, 6, buf, c);
            total = (double) t.waveLen * data::kSegWords;
        }
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        s.cell (6, 25.6, 12, "Wave Length", y);
        char wl[12]; std::snprintf (wl, sizeof wl, "%06d", (int) (a.toneBound() && a.toneAt (ti).used() ? total : 0));
        s.cell (7, 27.4, 6, wl, y);
        remTwoCol (s, 11, 4.8, 5.5, 13.5);
        waveBox (s, 11, 5.5, ti, 102, 424);
        const int st = pg.byLabel ("Start Point")->p.value, en = pg.byLabel ("End Point")->p.value;
        auto mx = [total] (int v) { return 102 + (int) std::min (424.0, std::floor (std::max (0, v) * 424.0 / total)); };
        for (int i = 0; i < 2; ++i)                              // markers: small ticks in the black bar of the graph
        {
            const int x = mx (i == 0 ? st : en);
            const bool hot = pg.hoverGraph == i;
            s.raw (x, 287, hot ? 3 : 1, 7, hot ? Cyan : White);
        }
    };
    bindTone (*p,
        [] (LivePage& pg, data::Tone& t)
        {
            pg.byLabel ("Start Point")->p.value = (int) t.startPoint;
            pg.byLabel ("End Point")->p.value = (int) t.endPoint;
        },
        [] (LivePage& pg, data::Tone& t)
        {
            t.startPoint = (uint32_t) pg.byLabel ("Start Point")->p.value;
            t.endPoint = (uint32_t) pg.byLabel ("End Point")->p.value;
        });
    p->gx = 77; p->gy = 284; p->gw = 488; p->gh = 150;
    LivePage* self = p.get();
    p->graphGrab = [self] (int x, int y) { return self->graphAt (x, y); };
    p->graphAt = [self] (int x, int y) -> int          // markers: 0 start, 1 end (x mapping 102..526 = whole tone)
    {
        if (y < 294) return -1;
        int best = -1, bd = 7;
        const char* nm[2] = { "Start Point", "End Point" };
        for (int i = 0; i < 2; ++i)
        {
            const int mxp = 102 + (int) std::min (424.0, std::floor (std::max (0, self->byLabel (nm[i])->p.value) * 424.0 / loopTotal()));
            if (std::abs (x - mxp) < bd) { bd = std::abs (x - mxp); best = i; }
        }
        return best;
    };
    p->graphDrag = [] (LivePage& pg, int id, int x, int)
    {
        const int v = (int) std::floor (std::max (0, std::min (424, x - 102)) * loopTotal() / 424.0);
        const int st = pg.byLabel ("Start Point")->p.value, en = pg.byLabel ("End Point")->p.value;
        if (id == 0) pg.setValue ("Start Point", std::min (v, en));
        else pg.setValue ("End Point", std::max (v, st));
    };
    comBar (*p, "Truncate", 5, 18.1, 15);
    return p;
}

std::unique_ptr<LivePage> combine()
{
    auto p = mixPage ("Combine", "UTIL-Combine");
    p->shift = 0;
    auto tsel = [&] (const char* lab, const char* id, int row)
    {
        Field f; f.p = mk (lab, indexOf (toneIds(), id), 0, 63, toneIds()); f.row = row; f.col = 23.5625 - 7.2; f.w = 7.2; f.align = Align::Right;
        f.label = lab; f.lcol = 6.6875; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    tsel ("Source 1", "I11", 4); tsel ("Source 2", "I11", 5); tsel ("Destination", "\00115", 6);
    mixValue (*p, mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 7);
    auto val = [&] (const char* lab, int row)
    {
        Field f; f.label = lab; f.p = fastp (mk (lab, 0, 0, 999999, {}, 1, 6)); f.row = row; f.col = 23.5625 - 6.1875; f.w = 6.1875; f.lcol = 6.6875; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    val ("S1 Start", 8); val ("S1 End", 9); val ("S2 Start", 10); val ("S2 End", 11);
    mixValue (*p, mk ("Search Mode", 0, 0, 2, { "\x7f 1", "\x7f 114", "Peak" }), 12, 6.1875);
    for (auto& f : p->fields) f.lcol -= 2;                       // the yellow labels stand two cells further left (device photos)

    auto st = std::make_shared<ComState>();
    auto last = std::make_shared<std::array<int, 2>> (std::array<int, 2> { -1, -1 });     // tones whose points are shown
    auto toneOf = [] (const LivePage& pg, const char* lab) -> data::Tone* { auto& a = App::get(); const int i = pg.byLabel (lab)->p.value; return a.boundTone (i) ? &a.toneAt (i) : nullptr; };

    // the points are those of the Loop set: a tone selected here loads its Start / End, a change here is written back
    p->refresh = [last, toneOf] (LivePage& pg)
    {
        const char* tl[2] = { "Source 1", "Source 2" }; const char* sl[2] = { "S1 Start", "S2 Start" }; const char* el[2] = { "S1 End", "S2 End" };
        for (int k = 0; k < 2; ++k)
        {
            const int v = pg.byLabel (tl[k])->p.value;
            const auto* t = toneOf (pg, tl[k]);
            if (v == (*last)[(size_t) k]) continue;
            (*last)[(size_t) k] = v;
            pg.byLabel (sl[k])->p.value = t && t->used() ? (int) t->startPoint : 0;
            pg.byLabel (el[k])->p.value = t && t->used() ? (int) t->endPoint : 0;
        }
    };
    p->postChange = [toneOf, st, last] (LivePage& pg)
    {
        st->msg.clear();
        const char* tl[2] = { "Source 1", "Source 2" }; const char* sl[2] = { "S1 Start", "S2 Start" }; const char* el[2] = { "S1 End", "S2 End" };
        for (int k = 0; k < 2; ++k)
            if (auto* t = toneOf (pg, tl[k]); t && t->used() && (*last)[(size_t) k] == pg.byLabel (tl[k])->p.value)     // a just changed tone loads its points first
            {
                t->startPoint = (uint32_t) pg.byLabel (sl[k])->p.value;
                t->endPoint = (uint32_t) pg.byLabel (el[k])->p.value;
            }
    };

    p->extras = [toneOf] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        static const char* lab[3] = { "Source 1", "Source 2", "Destination" };
        for (int i = 0; i < 3; ++i)
        {
            const auto* t = toneOf (pg, lab[i]);
            if (! t || ! t->used()) { if (i == 2) s.cell (6, 33.0625, 7, "Sub --", c); continue; }
            s.cell (4 + i, 24.3125, 8, trimSp (t->name), c);
            char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1f", 'A' + (t->waveBank & 1), t->waveLen * 0.4);
            s.cell (4 + i, 33.0625, 6, buf, c);
            if (i == 2 && t->subTone) s.cell (6, 33.0625, 7, subText (*t), c);
        }
        // wave length of the two tones (whole wave, in words), at the S Start / S End rows
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        for (int k = 0; k < 2; ++k)
        {
            const auto* t = toneOf (pg, lab[k]);
            char wl[24]; std::snprintf (wl, sizeof wl, "%06d", t && t->used() ? (int) t->waveLen * data::kSegWords : 0);
            s.cell (8 + 2 * k, 25.5, 15, k ? "Wave Length S2" : "Wave Length S1", y);
            s.cell (9 + 2 * k, 27.5, 6, wl, y);
        }
        // which wave is shown follows the cursor
        const auto* sf = pg.selected >= 0 ? &pg.fields[(size_t) pg.selected] : nullptr;
        if (pg.hoverField >= 0 && pg.hoverField < (int) pg.fields.size())          // mouse over a tone number: its wave at once
        {
            const auto& hf = pg.fields[(size_t) pg.hoverField];
            if (hf.label == "Source 1" || hf.label == "Source 2" || hf.label == "Destination") sf = &hf;
        }
        int which = 0;
        if (sf && (sf->label == "Source 2" || sf->label.rfind ("S2", 0) == 0)) which = 1;
        else if (sf && (sf->label == "Destination" || sf->label == "Wave Bank")) which = 2;
        const char* hdr[3] = { "S1", "S2", "D" };
        s.cell (14, 2.0, 3, hdr[which], c);
        {   // info line: remaining time of the four banks (Block I full / II free)
            CellOpts r; r.fg = Yellow; r.align = Align::Left;
            s.cell (14, 4.6875, 4, "R.T", r);
            const char* nm[4] = { "IA", "IB", "\001A", "\001B" };
            const double cx[4] = { 8.2, 16.2, 24.1, 32.1 };
            for (int i = 0; i < 4; ++i)
            {
                char b[16]; std::snprintf (b, sizeof b, "-%.1fs", data::secondsFree (a.blk (i / 2), i % 2));
                s.cell (14, cx[i], 9, std::string (nm[i]) + b, r);
            }
        }
        const auto* t = toneOf (pg, lab[which]);
        a.auditionTone = t ? pg.byLabel (lab[which])->p.value : -1;      // every wave that is shown can be played (keyboard / MIDI)
        waveBox (s, 5, 4.0, pg.byLabel (lab[which])->p.value, 102, 424);
        if (t && t->used())
        {
            const double total = (double) t->waveLen * data::kSegWords;
            auto mx = [total] (int v) { return 102 + (int) std::min (424.0, std::floor (std::max (0, v) * 424.0 / total)); };
            int pts[3];                                          // Start, Loop, End ticks in the black bar
            if (which == 2) { pts[0] = (int) t->startPoint; pts[1] = (int) t->loopPoint; pts[2] = (int) t->endPoint; }
            else
            {
                pts[0] = pg.byLabel (which ? "S2 Start" : "S1 Start")->p.value; pts[1] = (int) t->loopPoint; pts[2] = pg.byLabel (which ? "S2 End" : "S1 End")->p.value;
            }
            for (int i = 0; i < 3; ++i) s.raw (mx (pts[i]), 287, 1, i == 1 ? 4 : 7, White);
        }
        else s.raw (102, 287, 1, 7, White);
    };

    auto exec = [toneOf, st] (LivePage& pg) -> std::string
    {
        auto& a = App::get();
        const int di = pg.byLabel ("Destination")->p.value;
        const auto* t1 = toneOf (pg, "Source 1"); const auto* t2 = toneOf (pg, "Source 2");
        if (! t1 || ! t2 || ! a.boundTone (di)) return "Cannot Execute";
        if (! t1->used() || ! t2->used() || t1->subTone || t2->subTone) return "Cannot Execute";     // Sub Tones cannot be combined
        std::vector<int16_t> smp;
        auto add = [&] (int tone, const data::Tone& t, int st0, int en)
        {
            const auto& w = a.blk (tone / 32).wave[t.waveBank & 1];
            const int base = t.waveStart * data::kSegWords, n = t.waveLen * data::kSegWords;
            for (int i = std::max (0, st0); i <= en && i < n && base + i < (int) w.size(); ++i) smp.push_back (w[(size_t) (base + i)]);
        };
        add (pg.byLabel ("Source 1")->p.value, *t1, pg.byLabel ("S1 Start")->p.value, pg.byLabel ("S1 End")->p.value);
        add (pg.byLabel ("Source 2")->p.value, *t2, pg.byLabel ("S2 Start")->p.value, pg.byLabel ("S2 End")->p.value);
        if (smp.empty()) return "Cannot Execute";
        const int key = t1->origKey;
        const bool k15 = t1->khz15;
        const int bank = pg.byLabel ("Wave Bank")->p.value;
        const std::string r = data::storeSample (a.blk (di / 32), di % 32, bank, smp, k15, key, "");
        if (r.empty())
        {
            for (size_t i = 0; i < pg.fields.size(); ++i) if (pg.fields[i].label == "Destination") pg.selected = (int) i;     // the cursor jumps to the new tone
            a.uiDirty = true;
        }
        (void) st;
        return r;
    };
    comBar (*p, "Combine", 4, 21.1, 15,
            { { "Source 1", "Source 1" }, { "Source 2", "Source 2" }, { "Destination", "Destination" } }, exec, st, 15.4, 4, 11.3, 4, true);
    return p;
}

std::unique_ptr<LivePage> dFilter()
{
    auto p = mixPage ("DFilter", "UTIL-D.Filter");
    p->shift = 0;
    mixTone (*p, "Source", "I13", 5, "");
    mixTone (*p, "Destination", "\00113", 6, "");
    Names hz;                                    // 0.1 ... 10.0 kHz (manual p. 55)
    for (int i = 1; i <= 100; ++i) hz.push_back (std::to_string (i / 10) + "." + std::to_string (i % 10));
    mixValue (*p, mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 7);
    mixValue (*p, mk ("Mode", 0, 0, 1, { "LPF", "HPF" }), 8);
    mixValue (*p, mk ("Frequency", 9, 0, 99, hz), 9);       // index 9 = "1.0"
    mixValue (*p, mk ("Resonance", 0, 0, 127), 10);
    mixValue (*p, mk ("Level Adjust", 127, 0, 127), 11);
    auto st = std::make_shared<ComState>();
    auto toneOf = [] (const LivePage& pg, const char* lab) -> data::Tone* { auto& a = App::get(); const int i = pg.byLabel (lab)->p.value; return a.boundTone (i) ? &a.toneAt (i) : nullptr; };
    p->postChange = [st] (LivePage&) { st->msg.clear(); };

    p->extras = [toneOf] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        static const char* lab[2] = { "Source", "Destination" };
        for (int i = 0; i < 2; ++i)
        {
            const auto* t = toneOf (pg, lab[i]);
            if (! t || ! t->used()) { if (i == 1) s.cell (6, 33.0625, 7, "Sub --", c); continue; }
            s.cell (5 + i, 24.3125, 8, trimSp (t->name), c);
            char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1f", 'A' + (t->waveBank & 1), t->waveLen * 0.4);
            s.cell (5 + i, 33.0625, 6, buf, c);
        }
        remBlock (s, 9, 25.4, 27.5, 0, 15, false);
        // the wave that is shown (and played) follows the cursor, the mouse over a tone number shows it at once
        const auto* sf = pg.selected >= 0 ? &pg.fields[(size_t) pg.selected] : nullptr;
        if (pg.hoverField >= 0 && pg.hoverField < (int) pg.fields.size())
        {
            const auto& hf = pg.fields[(size_t) pg.hoverField];
            if (hf.label == "Source" || hf.label == "Destination") sf = &hf;
        }
        const int which = sf && (sf->label == "Destination" || sf->label == "Wave Bank") ? 1 : 0;
        s.cell (14, 2.0, 14, which ? "Destination" : "Source", c);
        const int ti = pg.byLabel (lab[which])->p.value;
        a.auditionTone = a.boundTone (ti) ? ti : -1;
        waveBox (s, 17, 6.5, ti, 102, 424);
    };

    auto exec = [toneOf, st] (LivePage& pg) -> std::string
    {
        auto& a = App::get();
        const int si = pg.byLabel ("Source")->p.value, di = pg.byLabel ("Destination")->p.value;
        if (! a.boundTone (si) || ! a.boundTone (di)) return "Cannot Execute";
        data::FilterPrm fp;
        fp.hpf = pg.byLabel ("Mode")->p.value == 1;
        fp.freqKHz = (pg.byLabel ("Frequency")->p.value + 1) / 10.0;
        fp.resonance = pg.byLabel ("Resonance")->p.value; fp.level = pg.byLabel ("Level Adjust")->p.value;
        const std::string r = data::storeProcessed (a.blk (si / 32), si % 32, a.blk (di / 32), di % 32, pg.byLabel ("Wave Bank")->p.value, st->csel == 1, fp);
        if (r.empty())
        {
            for (size_t i = 0; i < pg.fields.size(); ++i) if (pg.fields[i].label == "Destination") pg.selected = (int) i;     // the cursor jumps to the new tone
            a.uiDirty = true;
        }
        return r;
    };
    comBar (*p, "D.Filter", 5, 21.1, 15, { { "Source", "Source" }, { "Destination", "Destination" } }, exec, st, 15.4, 5, 11.3, 4, true, { "D.Filter", "DC-Cut" });
    return p;
}

// UTIL Wave Loop (manual p.57-58): Source tone, Destination tone, Loop points of the source; COM = Auto Loop / Smoothing
std::unique_ptr<LivePage> waveLoop()
{
    auto p = mixPage ("WaveLoop", "UTIL-Wave Loop");
    p->shift = 0;
    auto tsel = [&] (const char* lab, const char* id, int row)
    {
        Field f; f.p = mk (lab, indexOf (toneIds(), id), 0, 63, toneIds()); f.row = row; f.col = 23.5625 - 7.2; f.w = 7.2; f.align = Align::Right;
        f.label = lab; f.lcol = 6.6875; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    auto val = [&] (Param q, int row, double w)
    {
        Field f; f.label = q.label; f.p = std::move (q); f.row = row; f.col = 23.5625 - w; f.w = w; f.lcol = 6.6875; f.lw = 14;
        p->fields.push_back (std::move (f));
    };
    tsel ("Source", "I21", 4); tsel ("Destination", "\00121", 5);
    val (mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 6, 3.1875);
    val (fastp (mk ("Start Point", 0, 0, 221184, {}, 1, 6)), 7, 6.1875);
    val (fastp (mk ("Loop Point", 0, 0, 221184, {}, 1, 6)), 8, 6.1875);
    val (fastp (mk ("End Point", 0, 0, 221184, {}, 1, 6)), 9, 6.1875);
    val (mk ("Search Mode", 0, 0, 2, { "\x7f 1", "\x7f 114", "Peak" }), 10, 6.1875);
    val (mk ("Loop Edit", 0, 0, 1, { "Point", "Length" }), 11, 6.1875);
    val (mk ("Screen Type", 1, 1, 3), 12, 3.1875);
    val (mk ("Zoom Time", 1, 1, 7), 13, 3.1875);
    val (mk ("Zoom Level", 1, 1, 7), 14, 3.1875);
    for (auto& f : p->fields) f.lcol -= 2;                       // labels at col 4.6875 (device photos)

    auto st = std::make_shared<ComState>();
    auto last = std::make_shared<int> (-1);                      // the source tone whose points are shown
    auto toneOf = [] (const LivePage& pg, const char* lab) -> data::Tone* { auto& a = App::get(); const int i = pg.byLabel (lab)->p.value; return a.boundTone (i) ? &a.toneAt (i) : nullptr; };
    p->preChange = [] (LivePage& pg)
    {
        const int step = pg.byLabel ("Search Mode")->p.value == 1 ? 114 : 1;
        for (const char* l : { "Start Point", "Loop Point", "End Point" }) pg.byLabel (l)->p.step = step;
    };
    // the points are those of the Loop set of the source tone: a tone selected here loads them, a change here is written back
    p->refresh = [last, toneOf] (LivePage& pg)
    {
        const int v = pg.byLabel ("Source")->p.value;
        if (v == *last) return;
        *last = v;
        const auto* t = toneOf (pg, "Source");
        pg.byLabel ("Start Point")->p.value = t && t->used() ? (int) t->startPoint : 0;
        pg.byLabel ("Loop Point")->p.value = t && t->used() ? (int) t->loopPoint : 0;
        pg.byLabel ("End Point")->p.value = t && t->used() ? (int) t->endPoint : 0;
    };
    p->postChange = [toneOf, st, last] (LivePage& pg)
    {
        st->msg.clear();
        if (auto* t = toneOf (pg, "Source"); t && t->used() && *last == pg.byLabel ("Source")->p.value)
        {
            t->startPoint = (uint32_t) pg.byLabel ("Start Point")->p.value;
            t->loopPoint = (uint32_t) pg.byLabel ("Loop Point")->p.value;
            t->endPoint = (uint32_t) pg.byLabel ("End Point")->p.value;
        }
    };
    // which wave is shown (and played): the cursor / the mouse over a tone number decides
    auto which = [] (const LivePage& pg)
    {
        const auto* sf = pg.selected >= 0 ? &pg.fields[(size_t) pg.selected] : nullptr;
        if (pg.hoverField >= 0 && pg.hoverField < (int) pg.fields.size())
        {
            const auto& hf = pg.fields[(size_t) pg.hoverField];
            if (hf.label == "Source" || hf.label == "Destination") sf = &hf;
        }
        return sf && (sf->label == "Destination" || sf->label == "Wave Bank") ? 1 : 0;
    };

    p->extras = [toneOf, which, st] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        static const char* lab[2] = { "Source", "Destination" };
        for (int i = 0; i < 2; ++i)
        {
            const auto* t = toneOf (pg, lab[i]);
            if (! t || ! t->used()) { if (i == 1) s.cell (5, 33.0625, 7, "Sub --", c); continue; }
            s.cell (4 + i, 24.3125, 8, trimSp (t->name), c);
            char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1f", 'A' + (t->waveBank & 1), t->waveLen * 0.4);
            s.cell (4 + i, 33.0625, 6, buf, c);
            if (i == 1 && t->subTone) s.cell (5, 33.0625, 7, subText (*t), c);
        }
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        {   // Loop Length (cyan) and Wave Length (yellow) as on the device: labels on the Start / End Point rows (device photos)
            const auto* t = toneOf (pg, "Source");
            const int en = pg.byLabel ("End Point")->p.value, lp = pg.byLabel ("Loop Point")->p.value;
            char ll[24], wl[24];
            std::snprintf (ll, sizeof ll, "%06d", std::max (0, en - lp));
            std::snprintf (wl, sizeof wl, "%06d", t && t->used() ? (int) t->waveLen * data::kSegWords : 0);
            s.cell (7, 25.4, 15, "Loop Length", y);
            if (st->menu != 2) s.cell (8, 27.3, 6, ll, c);                // the COM list hides it
            s.cell (9, 25.4, 15, "Wave Length", y);
            s.cell (10, 27.3, 6, wl, y);
        }
        s.cell (12, 25.4, 15, "Remaining Time", y);                      // 2 x 2, without the s
        for (int i = 0; i < 4; ++i)
        {
            char b[16]; std::snprintf (b, sizeof b, "-%.1f", data::secondsFree (a.blk (i / 2), i % 2));
            s.cell (13 + i / 2, 26.3 + (i % 2) * 6.9, 9, std::string (i < 2 ? "I" : "\001") + (i % 2 ? "B" : "A") + b, y);
        }
        const int w = which (pg);
        const int ti = pg.byLabel (lab[w])->p.value;
        s.cell (14, 3.2, 2, w ? "D" : "S", c);
        const auto* t = toneOf (pg, lab[w]);
        a.auditionTone = t ? ti : -1;                                    // every wave that is shown can be played
        if (t && t->used())
        {
            LoopToneScope scope (ti);
            const bool dest = w == 1;
            drawLoopGraph (s, pg, dest ? (int) t->startPoint : pg.byLabel ("Start Point")->p.value,
                           dest ? (int) t->loopPoint : pg.byLabel ("Loop Point")->p.value, dest ? (int) t->endPoint : pg.byLabel ("End Point")->p.value);
        }
        else
        {
            s.raw (16, 278, 608, 162, White); s.raw (77, 284, 488, 150, Ink); s.raw (79, 286, 484, 146, White); s.raw (79, 286, 484, 8, Ink);
            s.raw (90, 363, 450, 1, Ink);
        }
    };
    p->gx = 77; p->gy = 284; p->gw = 488; p->gh = 150;
    LivePage* self = p.get();
    p->graphGrab = [self] (int x, int y) { return self->graphAt (x, y); };
    p->graphAt = [self, which, toneOf] (int x, int y) -> int      // the ticks of the source: 0 start, 1 loop, 2 end
    {
        if (y < 286 || y > 300 || which (*self) != 0 || ! toneOf (*self, "Source")) return -1;
        LoopToneScope scope (self->byLabel ("Source")->p.value);
        int best = -1, bd = 7;
        const char* nm[3] = { "Start Point", "Loop Point", "End Point" };
        for (int i = 0; i < 3; ++i)
        {
            const int mxp = kLoopX0 + (int) std::floor (std::max (0.0, std::min (1.0, self->byLabel (nm[i])->p.value / loopTotal())) * kLoopW);
            if (std::abs (x - mxp) < bd) { bd = std::abs (x - mxp); best = i; }
        }
        return best;
    };
    p->graphDrag = [] (LivePage& pg, int id, int x, int)
    {
        LoopToneScope scope (pg.byLabel ("Source")->p.value);
        const int v = (int) std::floor (std::max (0, std::min (kLoopW, x - kLoopX0)) * loopTotal() / kLoopW);
        const int s0 = pg.byLabel ("Start Point")->p.value, lp = pg.byLabel ("Loop Point")->p.value, en = pg.byLabel ("End Point")->p.value;
        if (id == 0) pg.setValue ("Start Point", std::min (v, std::min (lp, en)));
        else if (id == 1) pg.setValue ("Loop Point", std::max (s0, std::min (v, en)));
        else pg.setValue ("End Point", std::max (v, std::max (s0, lp)));
    };

    auto exec = [toneOf, st] (LivePage& pg) -> std::string
    {
        auto& a = App::get();
        const int si = pg.byLabel ("Source")->p.value, di = pg.byLabel ("Destination")->p.value;
        const auto* t = toneOf (pg, "Source");
        if (! t || ! t->used() || t->subTone || ! a.boundTone (di)) return "Cannot Execute";      // a Sub Tone cannot be selected
        const auto& src = a.blk (si / 32).wave[t->waveBank & 1];
        const size_t base = (size_t) t->waveStart * data::kSegWords, n = (size_t) t->waveLen * data::kSegWords;
        if (base + n > src.size()) return "Cannot Execute";
        std::vector<int16_t> w (src.begin() + (long) base, src.begin() + (long) (base + n));
        const int s0 = pg.byLabel ("Start Point")->p.value;
        int lp = pg.byLabel ("Loop Point")->p.value, en = pg.byLabel ("End Point")->p.value;
        if (st->csel == 2) { if (! data::smoothLoop (w, s0, lp, en)) return "Cannot Execute"; }
        else
        {
            int nl = lp, ne = en;
            if (! data::autoLoop (w, lp, en, st->csel == 0, nl, ne)) return "Cannot Execute";
            lp = nl; en = ne;
        }
        const std::string r = data::storeLooped (a.blk (si / 32), si % 32, a.blk (di / 32), di % 32, pg.byLabel ("Wave Bank")->p.value, w,
                                                 (uint32_t) s0, (uint32_t) lp, (uint32_t) en);
        if (r.empty())
        {
            for (size_t i = 0; i < pg.fields.size(); ++i) if (pg.fields[i].label == "Destination") pg.selected = (int) i;     // the cursor jumps to the new tone
            a.uiDirty = true;
        }
        return r;
    };
    comBar (*p, "Wave Loop", 5, 21.1, 16.2, { { "Source", "Source" }, { "Destination", "Destination" } }, exec, st, 15.4, 5, 11.3, 4, true,
            { "A.Loop L\003E", "A.Loop L\004E", "Smoothing" });
    return p;
}

std::unique_ptr<LivePage> deletePage()
{
    auto p = mixPage ("Delete", "EDIT-Delete");
    mixTone (*p, "Source", "I23", 4, "Sub 22");
    p->extras = [] (Screen& s, const LivePage& pg) { toneNameAt (s, pg, "Source", 4); remBlock (s, 8); };
    return p;
}

std::unique_ptr<LivePage> copyMove()
{
    auto p = mixPage ("CopyMove", "EDIT-Copy/Move");
    mixTone (*p, "Source", "I23", 4, "Sub 22");
    mixTone (*p, "Destination", "I12", 5, "Sub 11");
    mixValue (*p, mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 6);
    mixValue (*p, mk ("Mode", 0, 0, 1, { "Normal", "Reverse" }), 7, 6.1875);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        toneNameAt (s, pg, "Source", 4); toneNameAt (s, pg, "Destination", 5);
        remBlock (s, 9);
        waveBox (s, 23, 5.0);
    };
    return p;
}

std::unique_ptr<LivePage> dispWave()
{
    auto p = mixPage ("DispWave", "EDIT-Disp.Wave");
    mixTone (*p, "Source", "I23", 4, "Sub 22");
    p->extras = [] (Screen& s, const LivePage& pg) { toneNameAt (s, pg, "Source", 4); remBlock (s, 8); waveBox (s, 29, 4.5); };
    return p;
}

// ---------------------------------------------------------------- PLAY pages
const Names& patchIds()          // I11-I18, I21-I28
{
    static Names n;
    if (n.empty())
        for (const char* blk : { "I", "\001" }) for (int g = 1; g <= 2; ++g) for (int k = 1; k <= 8; ++k) n.push_back (blk + std::to_string (g) + std::to_string (k));
    return n;
}
std::string patchName (int i)       // from the disk when one is linked (empty patch = blank), else demo names
{
    if (i >= 16) return App::get().linked2 ? trimSp (App::get().block2.patches[i - 16].name()) : std::string();     // Block II
    if (App::get().linked) return trimSp (App::get().block.patches[i].name());
    static const char* const n[16] = { "Drums/Flute", "SBass/Xylo", "E.Piano", "Trumpet", "FLBass/SBell", "Strings", "Poly Synth", "Synth Bell",
                                       "Organ", "Harpsichord", "Vibes", "Choir", "Brass", "Pad", "Clav", "Bass 2" };
    return n[i];
}
const Names& patchNames()
{
    static const Names n { "Drums/Flute", "SBass/Xylo", "E.Piano", "Trumpet", "FLBass/SBell", "Strings", "Poly Synth", "Synth Bell",
                           "Organ", "Harpsichord", "Vibes", "Choir", "Brass", "Pad", "Clav", "Bass 2" };
    return n;
}

// Voice Mode (manual p.28-29): VAL = Last Note Priority, VAF = First Note Priority, V 1..V22 = 16 voices split over groups A-H
const Names& voiceModeNames()
{
    static Names n;
    if (n.empty())
    {
        n = { "VAL", "VAF" };
        for (int i = 1; i <= 22; ++i) n.push_back (i < 10 ? "V " + std::to_string (i) : "V" + std::to_string (i));
    }
    return n;
}
const int kVoiceSplit[22][8] = {
    { 16, 0, 0, 0, 0, 0, 0, 0 }, { 14, 2, 0, 0, 0, 0, 0, 0 }, { 12, 4, 0, 0, 0, 0, 0, 0 }, { 12, 2, 2, 0, 0, 0, 0, 0 },
    { 10, 6, 0, 0, 0, 0, 0, 0 }, { 10, 4, 2, 0, 0, 0, 0, 0 }, { 10, 2, 2, 2, 0, 0, 0, 0 }, { 8, 8, 0, 0, 0, 0, 0, 0 },
    { 8, 6, 2, 0, 0, 0, 0, 0 },  { 8, 4, 4, 0, 0, 0, 0, 0 },  { 8, 4, 2, 2, 0, 0, 0, 0 },  { 8, 2, 2, 2, 2, 0, 0, 0 },
    { 6, 6, 4, 0, 0, 0, 0, 0 },  { 6, 6, 2, 2, 0, 0, 0, 0 },  { 6, 4, 4, 2, 0, 0, 0, 0 },  { 6, 4, 2, 2, 2, 0, 0, 0 },
    { 6, 2, 2, 2, 2, 2, 0, 0 },  { 4, 4, 4, 4, 0, 0, 0, 0 },  { 4, 4, 4, 2, 2, 0, 0, 0 },  { 4, 4, 2, 2, 2, 2, 0, 0 },
    { 4, 2, 2, 2, 2, 2, 2, 0 },  { 2, 2, 2, 2, 2, 2, 2, 2 } };
const Names& chNames() { static Names n; if (n.empty()) { n.push_back ("OFF"); for (int i = 1; i <= 16; ++i) n.push_back (std::to_string (i)); } return n; }
int partCh (const LivePage& pg, int i) { return pg.byLabel (("CH " + std::string (1, (char) ('A' + i))).c_str())->p.value - 1; }
const int kSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
bool isBlackSemi (int r) { return r == 1 || r == 3 || r == 6 || r == 8 || r == 10; }
int whiteIndexOf (int semi) { int w = 0; for (int i = 0; i < 7; ++i) if (kSemi[i] <= semi) w = i; return w; }   // white key at or left of the note
// Counts (or "*" in the auto modes) next to the parts, red letter box of the sounding parts, and the message line
void voiceCounts (Screen& s, const LivePage& pg)
{
    const int m = pg.byLabel ("Voice Mode")->p.value;
    s.setShift (pg.shift + pg.bodyShift);
    CellOpts o; o.fg = Cyan; o.align = Align::Right;
    for (int i = 0; i < 8; ++i)
        s.cell (5 + i, 4.8125, 2, m < 2 ? std::string ("*") : std::to_string (kVoiceSplit[m - 2][i]), o);
    for (int i = 0; i < 8; ++i)
        if (App::get().playing (partCh (pg, i)))
        {
            CellOpts l; l.bg = Red; l.fg = White;
            s.cell (5 + i, 3, 1.1875, std::string (1, (char) ('A' + i)), l);
        }
    s.setShift (0);
    if (m < 2)
    {
        CellOpts t; t.fg = Cyan; t.dy = 1;
        s.cell (2, 3.5, 20, m == 0 ? "Last Note Priority" : "First Note Priority", t);
    }
}

// Part table of the three PLAY pages (identical on all of them); values = device photos
struct PartDef { const char* pid[8]; };
const PartDef kStandardParts { { "I11", "I12", "I13", "I14", "I15", "I16", "I17", "I18" } };
void partTable (LivePage& p, const PartDef& d)
{
    { Field f; f.p = mk ("Voice Mode", App::get().voiceMode, 0, 23, voiceModeNames()); f.row = 4; f.col = 3; f.w = 3; f.align = Align::Left; f.label = "Voice Mode"; p.fields.push_back (std::move (f)); }
    {   // the part table is shared by the three PLAY pages and read by the sound engine
        static bool first = true;
        if (first) { first = false; if (! App::get().linked) for (int i = 0; i < 8; ++i) App::get().partPatch[i] = indexOf (patchIds(), kStandardParts.pid[i]); }
    }
    p.refresh = [] (LivePage& pg)
    {
        auto& a = App::get();
        pg.byLabel ("Voice Mode")->p.value = a.voiceMode;
        for (int i = 0; i < 8; ++i)
        {
            const std::string L (1, (char) ('A' + i));
            pg.byLabel (("CH " + L).c_str())->p.value = a.partCh[i];
            pg.byLabel (("Patch " + L).c_str())->p.value = a.partPatch[i];
            pg.byLabel (("Level " + L).c_str())->p.value = a.partLevel[i];
        }
    };
    p.postChange = [] (LivePage& pg)
    {
        auto& a = App::get();
        a.voiceMode = pg.byLabel ("Voice Mode")->p.value;
        for (int i = 0; i < 8; ++i)
        {
            const std::string L (1, (char) ('A' + i));
            const int oldPatch = a.partPatch[i];
            a.partCh[i] = pg.byLabel (("CH " + L).c_str())->p.value;
            a.partPatch[i] = pg.byLabel (("Patch " + L).c_str())->p.value;
            a.partLevel[i] = pg.byLabel (("Level " + L).c_str())->p.value;
            if (a.partPatch[i] != oldPatch && a.partCh[i] > 0 && a.midiOut)      // real S-550: the part gets its patch by Program Change, too
            {
                const uint8_t m[2] = { (uint8_t) (0xc0 | (a.partCh[i] - 1)), (uint8_t) (a.mpar[99 + a.partPatch[i]] & 127) };
                a.midiOut (m, 2);
            }
        }
    };
    label (p.before, 4, 7.6875, 3, "CH", Align::Right);
    label (p.before, 4, 12, 5, "Patch");
    label (p.before, 4, 28, 5, "Out", Align::Right);
    label (p.before, 4, 34, 5, "Level", Align::Right);
    for (int i = 0; i < 8; ++i)
    {
        const int r = 5 + i;
        const std::string L (1, (char) ('A' + i));
        label (p.before, r, 3, 2, L.c_str());
        addCell (p, mk (("CH " + L).c_str(), i + 1, 0, 16, chNames()), r, 7.6875, 3, Align::Right);
        addCell (p, mk (("Patch " + L).c_str(), indexOf (patchIds(), d.pid[i]), 0, 31, patchIds()), r, 12, 3, Align::Left);
        addCell (p, mk (("Out " + L).c_str(), 1, 1, 8), r, 28, 5, Align::Right);
        addCell (p, mk (("Level " + L).c_str(), 127, 0, 127), r, 34, 5, Align::Right);
    }
    p.bodyRow = 5; p.bodyShift = 3;                 // table body a little lower so the header row does not touch the red part boxes
    p.selected = 1;                                   // CH A (field 0 is the Voice Mode header)
}
const PartDef kSplitParts    { { "I14", "I12", "I13", "I14", "I15", "I16", "I17", "I18" } };
void partNames (Screen& s, const LivePage& pg)
{
    voiceCounts (s, pg);
    s.setShift (pg.shift + pg.bodyShift);
    for (int i = 0; i < 8; ++i)
    {
        CellOpts c;
        const int pv = pg.byLabel (("Patch " + std::string (1, (char) ('A' + i))).c_str())->p.value;
        s.cell (5 + i, 15.5, 12, patchName (pv), c);
    }
    s.setShift (0);
}

std::unique_ptr<LivePage> standard()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Standard"; p->title = "PLAY-Standard";
    p->btnNames = { " ", " " };
    partTable (*p, kStandardParts);
    label (p->before, 14, 3, 7, "Display");
    addCell (*p, mk ("Display", 0, 0, 3, { "I1", "I2", "\0011", "\0012" }), 14, 12, 3, Align::Left);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        partNames (s, pg);
        s.setShift (pg.shift + pg.bodyShift);
        const int g = pg.byLabel ("Display")->p.value;
        for (int i = 0; i < 8; ++i)
        {
            const double col = i < 4 ? 3 : 21, ncol = i < 4 ? 6 : 24;
            const int r = 16 + i % 4;
            CellOpts y; y.fg = Cyan; CellOpts c; c.fg = Cyan;
            s.cell (r, col, 3, patchIds()[(size_t) (g * 8 + i)], y);
            s.cell (r, ncol, 12, patchName (g * 8 + i), c);
        }
        s.setShift (0);
    };
    return p;
}

std::unique_ptr<LivePage> eightKey()
{
    auto p = std::make_unique<LivePage>();
    p->id = "EightKey"; p->title = "PLAY-8 Key Disp";
    p->btnNames = { " ", " " };
    partTable (*p, kSplitParts);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        partNames (s, pg);
        s.raw (16, 264, 608, 176, White);                 // photo: panel starts below the table and runs to the screen bottom, rows A-H
        const int kx0 = 62, kw = 540, nw = 64;
        auto wx = [&] (int w) { return kx0 + w * kw / nw; };
        for (int k = 0; k < 8; ++k)
        {
            const int ky8 = 268 + k * 21;
            // 109 keys (9 octaves + 1, manual p.33) = 64 white keys over 540 px
            s.raw (kx0, ky8, kw + 1, 19, Black);
            s.raw (kx0 + 1, ky8 + 1, kw - 1, 17, White);
            for (int w = 1; w < nw; ++w) s.raw (wx (w), ky8 + 1, 1, 17, Black);
            for (int w = 0; w < nw - 1; ++w)
                if (w % 7 == 0 || w % 7 == 1 || w % 7 == 3 || w % 7 == 4 || w % 7 == 5) s.raw (wx (w + 1) - 2, ky8 + 1, 5, 10, Black);
            const int pch = partCh (pg, k);
            if (pch >= 0 && App::get().playing (pch))
            {
                s.raw (32, ky8 + 1, 14, 17, Red);
                s.glyph (34, ky8 + 2, (char) ('A' + k), White, false);
            }
            else s.glyph (34, ky8 + 2, (char) ('A' + k), Black, false);
            // pressed keys: small red dashes (black keys near the top, white keys below them)
            for (int n = 0; n < 109; ++n)
            {
                if (pch < 0 || ! App::get().held[pch][12 + n]) continue;
                const int w = n / 12 * 7 + whiteIndexOf (n % 12);
                if (isBlackSemi (n % 12)) s.raw (wx (w + 1) - 2, ky8 + 5, 5, 4, Red);       // 5 px wide on black and white keys
                else s.raw ((wx (w) + wx (w + 1)) / 2 - 2, ky8 + 12, 5, 4, Red);       // as wide as the black-key mark
            }
        }
    };
    LivePage* self = p.get();
    p->keyAt = [self] (int x, int y, int& ch, int& note) -> bool        // row = part, 109 keys from note 12
    {
        if (y < 268 || y >= 268 + 8 * 21 || x < 62 || x >= 602) return false;
        const int k = (y - 268) / 21;
        if (y - 268 - k * 21 >= 19) return false;
        auto wx = [] (int w) { return 62 + w * 540 / 64; };
        int w = 0; while (w < 63 && wx (w + 1) <= x) ++w;
        note = 12 + w / 7 * 12 + kSemi[w % 7];
        if (y - 268 - k * 21 < 11)
            for (int b = 0; b < 63; ++b)
                if ((b % 7 == 0 || b % 7 == 1 || b % 7 == 3 || b % 7 == 4 || b % 7 == 5) && x >= wx (b + 1) - 2 && x < wx (b + 1) + 3)
                    note = 12 + b / 7 * 12 + kSemi[b % 7] + 1;
        if (note > 120) return false;
        ch = partCh (*self, k);
        if (ch < 0) return false;
        return true;
    };
    return p;
}

std::unique_ptr<LivePage> splitDisp()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Main"; p->title = "PLAY-Split Disp";
    p->btnNames = { " ", " " };
    partTable (*p, kSplitParts);
    label (p->before, 14, 3, 7, "Display");
    addCell (*p, mk ("Display", 0, 0, 7, { "A", "B", "C", "D", "E", "F", "G", "H" }), 14, 14.375, 2, Align::Left);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        partNames (s, pg);
        pages::keyboard (s, true);
        // pressed keys of the displayed part (device photo): white key = filled blue box low in the key, black key = filled white box
        const int ch = partCh (pg, pg.byLabel ("Display")->p.value);
        for (int n = 36; n <= 96; ++n)
        {
            if (ch < 0 || ! App::get().held[ch][n]) continue;
            const int r = (n - 36) % 12, w = (n - 36) / 12 * 7 + whiteIndexOf (r);
            if (isBlackSemi (r))
            {
                const int bx = 45 + (w + 1) * 15 - 4;
                s.raw (bx + 2, 366 + 19, 5, 9, White);                 // 2 px to the key edge left, right and below
            }
            else
            {
                const int bx = 45 + w * 15 + 5;
                s.raw (bx, 366 + 34, 6, 11, Blue);                  // 4 px to the key edge left, right and below
            }
        }
    };
    LivePage* self = p.get();
    p->keyAt = [self] (int x, int y, int& ch, int& note) -> bool
    {
        if (y < 366 || y >= 416 || x < 45 || x >= 585) return false;
        const int t = x - 45 + 4, b = t / 15 - 1;
        const int w = (x - 45) / 15;
        note = 36 + w / 7 * 12 + kSemi[w % 7];
        if (y < 366 + 30 && b >= 0 && t % 15 < 9 && b < 35 && (b % 7 == 0 || b % 7 == 1 || b % 7 == 3 || b % 7 == 4 || b % 7 == 5))
            note = 36 + b / 7 * 12 + kSemi[b % 7] + 1;
        ch = partCh (*self, self->byLabel ("Display")->p.value);
        if (ch < 0) return false;
        return true;
    };
    return p;
}

// ---------------------------------------------------------------- Mix, Sampling, Wave Draw
Names mixNameTable()
{
    Names n = toneNameTable();
    n[indexOf (toneIds(), "I12")] = "BUBBLES1";
    n[indexOf (toneIds(), "I13")] = "Charge";
    return n;
}

std::unique_ptr<LivePage> mix()
{
    auto p = mixPage ("Mix", "UTIL-Mix");
    p->shift = 0;
    auto tone = [&] (const char* lab, const char* id, int row)
    {
        Field f; f.p = mk (lab, indexOf (toneIds(), id), 0, 63, toneIds()); f.row = row; f.col = 20.375; f.w = 3.1875;
        f.label = lab; f.lcol = 6.6875; f.lw = 14; p->fields.push_back (std::move (f));
    };
    tone ("Source1", "I11", 4); tone ("Source2", "I12", 5); tone ("Destination", "II11", 6);
    mixValue (*p, mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 7);
    mixValue (*p, mk ("Source1 Level", 127, 0, 127), 8);
    mixValue (*p, mk ("Source2 Level", 127, 0, 127), 9);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        static const char* lab[3] = { "Source1", "Source2", "Destination" };
        for (int i = 0; i < 3; ++i)
        {
            const int ti = pg.byLabel (lab[i])->p.value;
            const bool used = a.toneBound() && a.toneAt (ti).used();
            if (used)
            {
                const auto& t = a.toneAt (ti);
                char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1f", 'A' + (t.waveBank & 1), t.waveLen * 0.4);
                s.cell (4 + i, 24.3125, 8, trimSp (t.name), c);
                s.cell (4 + i, 33.0625, 6, buf, c);
                if (i == 2 && t.subTone) s.cell (6, 33.0625, 7, subText (t), c);
            }
            else if (i == 2) s.cell (6, 33.0625, 7, "Sub --", c);
        }
        remBlock (s, 8, 24.375, 27, 0, 15, true);
        const auto* sf = pg.selected >= 0 ? &pg.fields[(size_t) pg.selected] : nullptr;
        int which = 0;
        const char* hdr = "Source 1";
        if (sf && (sf->label == "Source2" || sf->label == "Source2 Level")) { which = 1; hdr = "Source 2"; }
        else if (sf && sf->label == "Destination") { which = 2; hdr = "Destination"; }
        s.setShift (6);
        s.cell (14, 3, 12, hdr, c);
        s.setShift (0);
        a.auditionTone = a.boundTone (pg.byLabel (lab[which])->p.value) ? pg.byLabel (lab[which])->p.value : -1;
        waveBox (s, 7, 5.0, pg.byLabel (lab[which])->p.value, 102, 424);
    };
    comBar (*p, "Mix", 4, 20.4, 16);
    return p;
}

// Wave Draw (manual p.59-62): the wave data of an Original tone is drawn with the mouse.
// Display: blue belt on top (whole length, white square = Window Address), 228 addresses (2 px each) below it, edge zones left / right
// (left button: scroll 114 addresses), left button in the wave draws (red), right button above / below the middle rolls the display (Zoom 2-7).
struct WdSt
{
    int off = 0;                          // vertical roll (sample units at the middle of the display)
    int lastAdr = -1, lastVal = 0, grab = -1;
    int curAdr = 0; bool curOn = false, curUp = true, edge = false;
    std::vector<char> red = std::vector<char> (228, 0);
    int menu = 0, sel = 0;
    int redWin = -1;
    bool wlActive = false; int wlTone = -1; uint32_t svStart = 0, svLoop = 0, svEnd = 0; uint8_t svMode = 0;      // Window Loop: the points the tone had
    bool onBelt = false;
};
constexpr int kWdN = 228, kWdX0 = 101, kWdW = 440, kWdY0 = 294, kWdYM = 363, kWdH = 138;       // 228 addresses over 440 px between the cyan lines
inline int wdX (int i) { return kWdX0 + i * kWdW / kWdN; }

std::unique_ptr<LivePage> waveDraw()
{
    auto p = mixPage ("WaveDraw", "UTIL-Wave Draw");
    mixTone (*p, "Source", "I22", 4, "");
    mixValue (*p, mk ("Window Adrs", 65536, 0, 999999, {}, 114, 6), 5, 6.1875);
    mixValue (*p, mk ("Zoom Level", 1, 1, 7), 6);
    mixValue (*p, mk ("Window Loop", 0, 0, 1, kOnOff), 7, 6.1875);
    mixValue (*p, mk ("Cursor Adrs", 0, 0, 999999, {}, 1, 6), 11, 6.1875);
    mixValue (*p, mk ("Data", 0, 0, 4095, {}, 1, 4), 12, 6.1875);
    for (auto& f : p->fields) if (f.label == "Data") f.lw = 0;                      // its label is drawn right-aligned in extras
    for (auto& f : p->fields) if (f.label == "Window Adrs" || f.label == "Cursor Adrs") f.p.fast = 10;
    auto st = std::make_shared<WdSt>();
    auto pgOf = [p = p.get()] { return p; };
    static const Names cmds = { "COPY", "CLR WINDOW", "CLR ALL" };
    static const double zf[7] = { 1, 2, 3, 4, 6, 8, 16 };

    // the wave of the selected tone: only an Original tone can be drawn on
    struct Wv { std::vector<int16_t>* w = nullptr; int base = 0, n = 0; };
    auto wave = [] (const LivePage& pg) -> Wv
    {
        auto& a = App::get(); Wv r;
        const int ti = pg.byLabel ("Source")->p.value;
        if (! a.boundTone (ti) || ti > 63) return r;
        auto& t = a.toneAt (ti);
        if (! t.used() || t.subTone) return r;
        auto& w = a.blk (ti / 32).wave[t.waveBank & 1];
        r.base = t.waveStart * data::kSegWords; r.n = std::min ((int) t.waveLen * data::kSegWords, (int) w.size() - r.base);
        if (r.n <= 0) return Wv();
        r.w = &w; return r;
    };
    auto yOf = [st] (int z, int v) { return kWdYM - (int) std::lround ((v - st->off) * 66.0 * zf[z - 1] / 2048.0); };
    auto vOf = [st] (int z, int y) { return std::max (-2048, std::min (2047, st->off + (int) std::lround ((kWdYM - y) * 2048.0 / (66.0 * zf[z - 1])))); };
    auto zoom = [] (const LivePage& pg) { return pg.byLabel ("Zoom Level")->p.value; };
    auto win = [] (const LivePage& pg) { return pg.byLabel ("Window Adrs")->p.value; };
    // Window Loop (manual p.62): On = Start 0, Loop point = Window Address, End = Window Address + 228, forward loop; Off / leaving the page = the old points
    auto wlRestore = [st] ()
    {
        if (! st->wlActive) return;
        st->wlActive = false;
        auto& a = App::get();
        if (st->wlTone < 0 || ! a.boundTone (st->wlTone)) return;
        auto& t = a.toneAt (st->wlTone);
        t.startPoint = st->svStart; t.loopPoint = st->svLoop; t.endPoint = st->svEnd; t.loopMode = st->svMode;
        a.uiDirty = true;
    };
    auto wlApply = [st, wave] (LivePage& pg)
    {
        auto& a = App::get();
        const Wv v = wave (pg);
        const int ti = pg.byLabel ("Source")->p.value;
        if (! v.w) return false;
        auto& t = a.toneAt (ti);
        if (! st->wlActive) { st->svStart = t.startPoint; st->svLoop = t.loopPoint; st->svEnd = t.endPoint; st->svMode = t.loopMode; st->wlTone = ti; st->wlActive = true; }
        const int w0 = pg.byLabel ("Window Adrs")->p.value;
        const uint32_t ne = (uint32_t) std::min (v.n - 1, w0 + kWdN);
        if (t.startPoint != 0 || t.loopPoint != (uint32_t) w0 || t.endPoint != ne || t.loopMode != 0) a.uiDirty = true;
        t.startPoint = 0; t.loopPoint = (uint32_t) w0; t.endPoint = ne; t.loopMode = 0;
        return true;
    };

    p->refresh = [=] (LivePage& pg)
    {
        auto& a = App::get();
        const Wv v = wave (pg);
        if (st->wlActive && st->wlTone != pg.byLabel ("Source")->p.value) { wlRestore(); pg.byLabel ("Window Loop")->p.value = 0; }      // another tone: the old one gets its points back
        auto* wf = pg.byLabel ("Window Adrs");
        wf->p.max = v.w ? std::max (0, v.n - kWdN) : 0;
        if (wf->p.value > wf->p.max) wf->p.value = wf->p.max;
        if (zoom (pg) == 1) st->off = 0;
        const int lim = 2048 - (int) (2048 / zf[zoom (pg) - 1]);
        st->off = std::max (-lim, std::min (lim, st->off));
        if (st->redWin != wf->p.value) { std::fill (st->red.begin(), st->red.end(), 0); st->redWin = wf->p.value; }
        auto* cf = pg.byLabel ("Cursor Adrs"); cf->p.max = v.w ? v.n - 1 : 0;
        if (pg.byLabel ("Window Loop")->p.value && v.w) wlApply (pg); else if (st->wlActive) wlRestore();
        st->curAdr = std::max (0, std::min (st->curAdr, cf->p.max));
        cf->p.value = st->curAdr;
        pg.byLabel ("Data")->p.value = v.w ? (*v.w)[(size_t) (v.base + st->curAdr)] + 2048 : 0;
        a.auditionTone = v.w ? pg.byLabel ("Source")->p.value : -1;                  // the drawn wave can be played
        pg.comRed = st->menu == 2;
        pg.btnNames = st->menu == 2 ? std::vector<std::string> { " ", "", "EXE", "exit" }
                    : st->curOn && ! st->onBelt && ! pg.popupButtons ? st->edge ? std::vector<std::string> { "", "", "Adrs", "exit" } : std::vector<std::string> { "", "", "Draw", st->curUp ? "Roll D" : "Roll U" } : std::vector<std::string> {};   // the mouse buttons while the mouse is over the wave
        const bool live = st->curOn && v.w;
        pg.byLabel ("Cursor Adrs")->fg = live ? Cyan : White; pg.byLabel ("Data")->fg = live ? Cyan : White;
        pg.gx = 79; pg.gy = 286; pg.gw = 484; pg.gh = 146;
    };
    p->postChange = [=] (LivePage& pg)
    {
        auto& a = App::get();
        a.fileMsg.clear();
        const Wv v = wave (pg);
        if (! v.w) { pg.byLabel ("Window Loop")->p.value = 0; return; }
        if (pg.byLabel ("Window Loop")->p.value) wlApply (pg); else wlRestore();
        st->curAdr = pg.byLabel ("Cursor Adrs")->p.value;
        const int d = pg.byLabel ("Data")->p.value - 2048;
        int16_t& cur = (*v.w)[(size_t) (v.base + st->curAdr)];
        if (cur != d) { cur = (int16_t) d; ++a.waveRev; a.uiDirty = true; }          // Data edited by hand
    };
    p->onLeave = [wlRestore] { wlRestore(); };
    p->onEnter = [] { App::get().fileMsg.clear(); };                       // the 'Loaded ...' line of the last page is not shown here
    p->hoverOut = [st] { if (st->curOn) { st->curOn = false; st->onBelt = false; App::get().uiDirty = true; } };
    p->extras = [=] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        if (App::get().scopeRun) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 4.5, 12, "Start", w); }
        s.raw (16, 278, 608, 162, White);
        s.raw (77, 284, 488, 150, Ink);
        s.raw (79, 286, 484, 146, White);
        s.raw (79, 286, 484, 8, Ink);                                                // the belt (whole length of the wave)
        const Wv v = wave (pg);
        const int z = zoom (pg), w0 = win (pg);
        if (v.w)
        {
            auto& t = a.toneAt (pg.byLabel ("Source")->p.value);
            int k = 0;
            for (const uint32_t pt : { t.startPoint, t.loopPoint, t.endPoint })                // Start / End point tall, Loop point short (device photos)
            { if ((int) pt <= v.n) s.raw (79 + (int) ((long long) pt * 484 / std::max (1, v.n)), 287, 1, k == 1 ? 3 : 6, White); ++k; }
            const int bx = 79 + (int) ((long long) w0 * 484 / std::max (1, v.n));
            s.raw (bx - 2, 287, 5, 6, White);                                        // Window Address on the belt
            int prevY = 0;
            for (int i = 0; i < kWdN; ++i)
            {
                const int ad = w0 + i; if (ad >= v.n) break;
                int y = yOf (z, (*v.w)[(size_t) (v.base + ad)]);
                y = std::max (kWdY0, std::min (kWdY0 + kWdH - 1, y));
                int y0 = y, y1 = y;
                if (i > 0) { y0 = std::min (y, prevY); y1 = std::max (y, prevY); }
                s.raw (wdX (i), y0, std::max (1, wdX (i + 1) - wdX (i)), std::max (1, y1 - y0 + 1), st->red[(size_t) i] ? Red : Ink);
                prevY = y;
            }
            for (const int i : { 0, kWdN / 2, kWdN }) s.raw (wdX (i), kWdY0, 1, kWdH, Cyan);       // Window Address, +114, +228
        }
        toneNameAt (s, pg, "Source", 4);
        CellOpts yl; yl.fg = Yellow; yl.align = Align::Left;
        s.setShift (4);
        {
            const int ti = pg.byLabel ("Source")->p.value;
            if (v.w)                                                                 // wave data of the tone (bank - time) and its length
            {
                const auto& t = a.toneAt (ti);
                char buf[24]; std::snprintf (buf, sizeof buf, "%c-%.1f%s", t.waveBank ? 'B' : 'A', t.waveLen * 0.4, t.khz15 ? "x2" : "");
                CellOpts cy; cy.fg = Cyan; cy.align = Align::Left; s.cell (4, 33.0625, 7, buf, cy);
                s.cell (6, 25.0, 12, "Wave Length", yl);
                char nb[12]; std::snprintf (nb, sizeof nb, "%06d", v.n);
                s.cell (7, 27.1, 6, nb, yl);
            }
            CellOpts rl = yl; rl.align = Align::Right; s.cell (12, 6.6875, 11, "Data", rl);
        }
        s.setShift (0);
        if (! a.fileMsg.empty()) { CellOpts m; m.fg = a.fileMsgErr ? Red : White; m.align = Align::Left; s.cell (2, 3.5, 24, a.fileMsg, m); }
        if (st->menu == 2) s.popup (336, 4, cmds, st->sel);
    };
    p->graphAt = [=] (int x, int y) -> int          // hover: the cursor address follows the mouse (no grab)
    {
        if (pgOf()->popupButtons || st->menu) { st->curOn = false; return -1; }
        if (y >= kWdY0 && y < kWdY0 + kWdH && x >= kWdX0 && x < kWdX0 + kWdW) { const int i = (x - kWdX0) * kWdN / kWdW; st->curOn = true; st->onBelt = false; st->edge = false; st->curUp = y < kWdYM; st->curAdr = pgOf()->byLabel ("Window Adrs")->p.value + i; App::get().uiDirty = true; return 100 + i + (st->curUp ? 0 : 1000); }
        if (y >= kWdY0 && y < kWdY0 + kWdH && x >= 79 && x < 563)                  // scroll zones left / right of the cyan lines: the device shows the address of the window edge
        {
            st->curOn = true; st->onBelt = false; st->edge = true; st->curUp = y < kWdYM;
            st->curAdr = pgOf()->byLabel ("Window Adrs")->p.value + (x < kWdX0 ? 0 : kWdN + 1); App::get().uiDirty = true; return 200 + (x < kWdX0 ? 0 : 1);
        }
        if (y >= 286 && y < kWdY0 && x >= 79 && x < 563)                           // over the belt: the cursor address is the place on the whole wave
        {
            const Wv v = wave (*pgOf());
            if (v.w) { st->curOn = true; st->onBelt = true; st->curAdr = std::max (0, std::min (v.n - 1, (int) ((long long) std::max (0, std::min (483, x - 79)) * v.n / 484))); App::get().uiDirty = true; return 5000 + st->curAdr; }
        }
        st->curOn = false; st->onBelt = false; st->edge = false; return -1;
    };
    // the mouse follows: draws (id 0), moves the window on the belt (id 1), scrolls (id 2 / 3)
    auto setAt = [=] (LivePage& pg, int x, int y)
    {
        const Wv v = wave (pg);
        if (! v.w) return;
        const int ad = pg.byLabel ("Window Adrs")->p.value + std::max (0, std::min (kWdN - 1, (x - kWdX0) * kWdN / kWdW));
        if (ad >= v.n) return;
        const int val = vOf (zoom (pg), y);
        const int a0 = st->lastAdr < 0 ? ad : st->lastAdr, v0 = st->lastAdr < 0 ? val : st->lastVal;
        const int lo = std::min (a0, ad), hi = std::max (a0, ad);
        for (int q = lo; q <= hi && q < v.n; ++q)
        {
            const double f = hi == lo ? 1.0 : (double) (q - a0) / (double) (ad - a0);
            (*v.w)[(size_t) (v.base + q)] = (int16_t) std::max (-2048, std::min (2047, (int) std::lround (v0 + (val - v0) * f)));
            const int wi = q - pg.byLabel ("Window Adrs")->p.value;
            if (wi >= 0 && wi < kWdN) st->red[(size_t) wi] = 1;
        }
        st->lastAdr = ad; st->lastVal = val; st->curAdr = ad; st->curOn = true;
        ++App::get().waveRev; App::get().uiDirty = true;
    };
    p->graphGrab = [=] (int x, int y) -> int
    {
        auto* pg = pgOf();
        if (st->menu || pg->popupButtons) return -1;                                // a pull-down / the big tone view is open
        const Wv v = wave (*pg);
        if (! v.w) return -1;
        auto* wf = pg->byLabel ("Window Adrs");
        if (y < kWdY0) { wf->p.value = std::max (0, std::min (wf->p.max, (int) ((long long) std::max (0, std::min (483, x - 79)) * v.n / 484))); App::get().fileMsg.clear(); return 1; }
        if (x < kWdX0 || x >= kWdX0 + kWdW)                                    // edge zone: 114 addresses to the left / right
        {
            wf->p.value = std::max (0, std::min (wf->p.max, wf->p.value + (x < kWdX0 ? -114 : 114)));
            return x < kWdX0 ? 2 : 3;
        }
        st->lastAdr = -1; App::get().fileMsg.clear();
        setAt (*pg, x, y);
        return 0;
    };
    p->graphDrag = [=] (LivePage& pg, int id, int x, int y)
    {
        if (id == 1) { const Wv v = wave (pg); auto* wf = pg.byLabel ("Window Adrs"); if (v.w) wf->p.value = std::max (0, std::min (wf->p.max, (int) ((long long) std::max (0, std::min (483, x - 79)) * v.n / 484))); }
        else if (id == 0 && y >= kWdY0 && x >= kWdX0 && x < kWdX0 + kWdW) setAt (pg, x, y);
        else if (id == 0) { const int xc = std::max (kWdX0, std::min (kWdX0 + kWdW - 1, x)), yc = std::max (kWdY0, std::min (kWdY0 + kWdH - 1, y)); setAt (pg, xc, yc); }
    };
    // COM: COPY / CLR WINDOW / CLR ALL;  right button in the wave: roll the display
    p->menuOpen = [st] { return st->menu != 0; };
    p->buttonsActive = p->menuOpen;
    p->comHook = [st] { st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; App::get().fileMsg.clear(); return true; };
    p->clickHook = [=] (int x, int y, int dir) -> int
    {
        auto* pg = pgOf();
        if (st->menu == 2)
        {
            int tw = 0; Screen tmp;
            for (const auto& t : cmds) tw = std::max (tw, tmp.textWidth (t));
            int rx, ry, rw, rh;
            Screen::popupRect (336, 4, (int) cmds.size(), tw, rx, ry, rw, rh);
            const int top = 4 * kRowH - 1;
            if (x < rx || x >= rx + rw || y < top || y >= top + (int) cmds.size() * kRowH) return 0;
            st->sel = (y - top) / kRowH;
            return 2;
        }
        if (y >= 286 && y < 432 && x >= 79 && x < 563)
        {
            if (dir > 0 && y >= kWdY0 && x >= kWdX0 && x < kWdX0 + kWdW && zoom (*pg) > 1)      // right button: roll down (above the middle) / up (below)
            {
                const int step = (int) (2048 / zf[zoom (*pg) - 1]) / 2;
                st->off += y < kWdYM ? step : -step;
                return 1;
            }
            return 0;                                                               // left clicks are handled by the graph (draw)
        }
        return -1;
    };
    p->buttonHook = [=] (int which) -> std::string
    {
        auto& a = App::get();
        auto* pg = pgOf();
        if (st->menu != 2) return "";
        if (which == 1) { st->menu = 0; return ""; }
        const Wv v = wave (*pg);
        if (! v.w) { a.fileMsg = "Cannot Execute"; a.fileMsgErr = true; return ""; }
        const int w0 = win (*pg);
        auto& w = *v.w;
        if (st->sel == 0)                                                           // COPY: the window repeated to the end of the wave
            for (int q = w0 + kWdN; q < v.n; ++q) w[(size_t) (v.base + q)] = w[(size_t) (v.base + w0 + (q - w0) % kWdN)];
        else if (st->sel == 1) for (int q = w0; q < w0 + kWdN && q < v.n; ++q) w[(size_t) (v.base + q)] = 0;
        else for (int q = 0; q < v.n; ++q) w[(size_t) (v.base + q)] = 0;
        ++a.waveRev; a.fileMsg = "Complete"; a.fileMsgErr = false; a.uiDirty = true;
        return "";
    };
    return p;
}

// Wave Scope: oscilloscope of the audio input (confirmed by a device photo): red "Wave Scope" bar, EXE starts, exit leaves
std::unique_ptr<LivePage> waveScope()
{
    auto p = std::make_unique<LivePage>();
    p->id = "WaveScope"; p->title = "UTIL-Wave Scope"; p->shift = 0;
    p->comRed = true;
    // idle: Sel and ----- are inactive and no scope is shown; COM switches the scope on (EXE / exit appear)
    p->refresh = [] (LivePage& pg)
    {
        auto& a = App::get();
        pg.btnNames = a.scopeRun ? std::vector<std::string> { " ", "", "Stop", "----" } : a.scopeAct ? std::vector<std::string> { " ", "", "EXE", "exit" } : std::vector<std::string> { " ", "", "Sel", "----" };
    };
    // photo: the red bar (or EXE) switches the input on and the scope runs ("Start" on the status line, Stop on the button)
    p->clickHook = [] (int x, int y, int) -> int
    {
        auto& a = App::get();
        if (a.scopeAct && x >= 324 && x < 580 && y >= 75 && y < 99) return a.scopeRun ? 0 : 2;      // the bar acts like EXE (buttonHook starts)
        return -1;
    };
    p->comHook = [] { auto& a = App::get(); a.scopeAct = ! a.scopeAct; if (! a.scopeAct) a.scopeRun = false; return true; };
    p->buttonHook = [] (int which) -> std::string
    {
        auto& a = App::get();
        if (! a.scopeAct) return "";
        if (which == 0) { a.scopeRun = ! a.scopeRun; return ""; }                 // EXE starts, Stop stops
        if (a.scopeRun) return "";                                                 // "----" while running: nothing
        a.scopeAct = false;                                                        // exit: back to the idle picture
        return "";
    };
    p->extras = [] (Screen& s, const LivePage&)
    {
        if (App::get().scopeAct)
        {
            s.raw (324, 79, 256, 20, Black);                 // shadow of the red bar
            s.setShift (4);
            CellOpts r; r.bg = Red; r.fg = White; r.dy = 0;
            s.cell (4, 21, 16, "Wave Scope", r);
            s.setShift (0);
        }
        if (App::get().scopeRun) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 4.5, 12, "Start", w); }
        s.raw (16, 278, 608, 162, White);
        s.raw (77, 284, 488, 150, Ink);
        s.raw (79, 286, 484, 146, White);
        s.raw (79, 286, 484, 8, Ink);
        auto& a = App::get();
        if (! a.scopeRun) return;
        int prev = a.scope[0];
        for (int x = 0; x < 480; ++x)
        {
            const int v = a.scope[(size_t) x];
            const int y0 = 363 - std::max (prev, v) * 58 / 2048, y1 = 363 - std::min (prev, v) * 58 / 2048;
            s.raw (82 + x, y0, 1, std::max (1, y1 - y0 + 1), Cyan);
            prev = v;
        }
        s.raw (79 + 242, 294, 1, 138, Cyan);                // centre marker (photo)
    };
    return p;
}

void jsPopup (Screen& s, double col, int row0, double wc, const Names& items, int sel);

std::unique_ptr<LivePage> sampling()
{
    auto p = std::make_unique<LivePage>();
    p->id = "Sampling"; p->title = "UTIL-Sampling"; p->popupButtons = false; p->subRed = false; p->shift = 0;
    auto st = std::make_shared<std::pair<bool, int>> (false, 0);       // COM pull-down (Auto / Manual / Previous): open, selected entry
    Field src; src.p = mk ("Source", 0, 0, 63, toneIds()); src.row = 4; src.col = 21; src.w = 3; src.align = Align::Left;
    src.fg = White; src.label = "Source"; src.lcol = 6.6875; src.lw = 12;
    p->fields.push_back (src);
    auto row = [&] (Param q, int r, double col, double w, Align a, const char* lab)
    {
        Field f; f.label = lab; f.p = std::move (q); f.row = r; f.col = col; f.w = w; f.align = a; f.lcol = 6.6875; f.lw = 12;
        p->fields.push_back (std::move (f));
    };
    Names secs; for (int i = 0; i <= 144; ++i) secs.push_back (std::to_string (i / 10) + "." + std::to_string (i % 10));
    const double vc = 21;
    row (mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 5, vc, 3, Align::Right, "Wave Bank");
    row (mk ("Frequency", 15, 15, 30, {}, 15), 6, vc, 3, Align::Right, "Frequency");
    row (mk ("Time", 20, 0, 144, secs), 7, vc, 3, Align::Right, "Time");
    row (mk ("Orig.Key", indexOf (noteNames(), "C 5"), 0, 119, noteNames()), 8, vc, 3, Align::Right, "Orig.Key");
    row (mk ("Threshold", 0, 0, 127), 9, vc, 3, Align::Right, "Threshold");
    row (mk ("Pre-Trigger", 0, 0, 100), 10, vc, 3, Align::Right, "Pre-Trigger");
    row (mk ("Limiter", 0, 0, 1, kOnOff), 11, vc, 3, Align::Right, "Limiter");
    row (mk ("CD Cue Switch", 0, 0, 1, kOnOff), 12, vc, 3, Align::Right, "CD Cue Switch");
    p->extras = [st] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts y; y.fg = Yellow;
        const int f = pg.fields[2].p.value;
        y.align = Align::Left;
        s.cell (6, 24, 4, "KHz", y); s.cell (7, 24, 1, "s", y); s.cell (10, 24, 2, "ms", y);
        CellOpts x2; x2.fg = Cyan;
        if (f <= 15) { x2.align = Align::Left; s.cell (7, 25, 2, "x2", x2); s.cell (10, 26, 2, "x2", x2); }
        const bool rv = a.rec.review;
        // remaining time: Block I (A, B) and II (A, B); after a recording stacked right of Threshold
        static const char* bk[4] = { "A", "B", "A", "B" };
        if (rv) s.cell (9, 25.8, 14, "Remaining Time", y); else s.cell (15, 6.6875, 14, "Remaining Time", y);
        for (int i = 0; i < 4; ++i)
        {
            const int r = rv ? 10 + i : 15 + i / 2; const double c = rv ? 28 : (i % 2 ? 31 : 23);
            char buf[12]; std::snprintf (buf, sizeof buf, "%c-%.1fs", bk[i][0], data::secondsFree (i < 2 ? a.block : a.block2, i % 2));
            if (i < 2) { s.cell (r, c, 1, "I", y); }
            else
            {
                s.cell (r, c, 1, "\001", y);
            }
            s.cell (r, c + 1, 6, buf, y);
        }
        const int rs = a.rec.state.load();
        a.recWatch = true;
        if (rv)
        {
            const auto& t = a.toneAt (a.rec.tone);
            CellOpts c; c.fg = Cyan; c.align = Align::Left;
            char ib[12]; std::snprintf (ib, sizeof ib, "%c-%.1f", 'A' + (t.waveBank & 1), t.waveLen * (t.khz15 ? 0.8 : 0.4));
            s.cell (4, 24.3125, 8, trimSp (t.name), c); s.cell (4, 33.0625, 6, ib, c);
            const auto& w = a.blk (a.rec.tone / 32).wave[a.rec.bank & 1];
            s.raw (16, 278, 608, 162, White); s.raw (77, 284, 488, 150, Ink); s.raw (79, 286, 484, 146, White); s.raw (79, 286, 484, 8, Ink);
            for (int k2 = 0; k2 <= data::kSegments; ++k2) s.raw (102 + k2 * 424 / data::kSegments, 294, 1, 138, Cyan);
            const long long tot = data::kWaveWords;
            for (int x = 102; x < 526 && (long long) w.size() >= tot; ++x)
            {
                const int i0 = (int) ((long long) (x - 102) * tot / 424), i1 = (int) ((long long) (x - 101) * tot / 424);
                int lo = 0, hi = 0;
                for (int i = i0; i < i1; ++i) { lo = std::min (lo, (int) w[(size_t) i]); hi = std::max (hi, (int) w[(size_t) i]); }
                const int top = 363 - hi * 58 / 2048, bot = 363 - lo * 58 / 2048;
                const bool red = i0 >= t.waveStart * data::kSegWords - 521 && i1 <= (t.waveStart + t.waveLen) * data::kSegWords + 521;
                s.raw (x, top, 1, std::max (1, bot - top + 1), red ? Red : Ink);
            }
            s.raw (90, 363, 450, 1, Ink);
        }
        else
        {
            CellOpts sb; sb.fg = Cyan; sb.align = Align::Left; s.cell (4, 33, 7, "Sub --", sb);
            s.raw (32, 364, 577, 62, White);
            CellOpts k; k.fg = Ink; k.dy = 10; s.cell (21, 4, 6, "Level", k);
            for (int i = 0; i < 17; ++i) s.raw (152 + i * 16, 381, 1, 7, i >= 13 ? Red : Ink);
            const float pk = a.rec.level.load();
            const double db = pk > 1e-6f ? 20.0 * std::log10 ((double) pk) : -99.0;
            const int px = (int) std::lround (std::max (0.0, std::min (256.0, 256.0 * (1.0 + db / 48.0))));
            s.raw (150, 393, 9 + px, 6, Ink);
            const int tx = pg.fields[5].p.value * 256 / 127;
            for (int k2 = 0; k2 < 6; ++k2) s.raw (154 + tx - k2, 406 + 2 * k2, 2 * k2 + 1, 2, Ink);
        }
        if (rs != 0 || ! a.rec.msg.empty() || st->first)
        {
            const std::string txt = rs == 1 ? "Waiting" : rs >= 2 ? "Sampling" : ! a.rec.msg.empty() ? a.rec.msg : "Ready";
            CellOpts rd; rd.fg = White; rd.align = Align::Left; s.cell (2, 3.5, 12, txt, rd);
        }
        if (st->first && rs == 0)
        {
            jsPopup (s, 21, 4, 14, { "Auto", "Manual", "Previous" }, st->second);
        }
    };
    LivePage* self = p.get();
    p->refresh = [st] (LivePage& pg)
    {
        auto& a = App::get();
        a.recPoll();
        const bool act = a.rec.state.load() != 0;
        pg.popupButtons = st->first || act || a.rec.review; pg.comRed = st->first || act;
        pg.btnNames = a.rec.review ? std::vector<std::string> { "", "", "Rec", "Rec" } : act ? std::vector<std::string> { "", "", "----", "exit" }
                  : st->first ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> {};
    };
    p->menuOpen = [st] { return st->first || App::get().rec.state.load() != 0 || App::get().rec.review; };
    p->gx = 32; p->gy = 378; p->gw = 577; p->gh = 46;                     // level strip: the threshold triangle can be dragged
    p->graphAt = [st] (int, int) -> int { return st->first || App::get().rec.state.load() != 0 || App::get().rec.review ? -1 : 0; };
    p->graphGrab = p->graphAt;
    p->graphDrag = [] (LivePage& pg, int, int x, int) { pg.setValue ("Threshold", std::max (0, std::min (127, (int) std::lround ((x - 154) * 127.0 / 256.0)))); };
    p->buttonsActive = p->menuOpen;
    p->comHook = [st] { if (App::get().rec.state.load() != 0 || App::get().rec.review) return false; st->first = ! st->first; st->second = 0; App::get().rec.msg.clear(); return true; };
    p->clickHook = [st] (int x, int y, int) -> int
    {
        if (App::get().rec.state.load() != 0) return 0;
        if (! st->first) return -1;
        const int x0 = 20 * 16 - 8, w = 14 * 16 + 16, top = 4 * kRowH;
        if (x < x0 || x >= x0 + w || y < top || y >= top + 3 * kRowH) return 0;
        st->second = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st, self] (int which) -> std::string
    {
        auto& a = App::get();
        if (a.rec.state.load() != 0) { if (which == 1) a.recCancel(); return ""; }          // waiting / sampling: only exit
        if (! st->first) { self->change (which == 0 ? -1 : +1); return ""; }
        st->first = false;
        if (which == 1) return "";
        const auto V = [&] (int i) { return self->fields[(size_t) i].p.value; };
        const bool k15 = V (2) <= 15;
        const double secs = V (3) / 10.0 * (k15 ? 2.0 : 1.0);
        const double pre = V (6) * (k15 ? 2.0 : 1.0);
        const std::string err = a.recArm (V (0), V (1), k15, secs, pre, V (5), st->second == 1, V (7) != 0,
                                          std::max (11, std::min (120, indexToKey (V (4)))), a.hostRate);
        a.rec.msg = err;
        return "";
    };
    return p;
}

// ---------------------------------------------------------------- Tone Map / Patch Map
struct MapParam { std::string name; int lo, hi; Names names; std::function<int (int, int)> def; };

struct MapData
{
    std::vector<MapParam> params;
    std::vector<std::vector<int>> vals;      // [param][row * cols + col]
    int cur = 0, rows = 8, cols = 8;
};

// Builds a page with a parameter selector (row 3) and a rows x cols grid of that parameter's values.
std::unique_ptr<LivePage> mapPage (const char* id, const char* title, std::shared_ptr<MapData> d, double c0, double cw,
                                   int row0, int colHeadRow, const Names& colHead, double headCol, double headW)
{
    auto p = std::make_unique<LivePage>();
    p->id = id; p->title = title;
    d->vals.assign (d->params.size(), {});
    for (size_t k = 0; k < d->params.size(); ++k)
        for (int i = 0; i < d->rows; ++i)
            for (int j = 0; j < d->cols; ++j) d->vals[k].push_back (d->params[k].def (i, j));
    Names sel;
    for (size_t k = 0; k < d->params.size(); ++k) sel.push_back (std::to_string (k + 1) + " " + d->params[k].name);
    label (p->before, 3, 3, 8, "Param");
    for (int j = 0; j < d->cols; ++j) p->before.push_back ({ colHeadRow, c0 + cw * j, cw, colHead[(size_t) j], Cyan, Align::Center });
    for (int i = 0; i < d->rows; ++i) label (p->before, row0 + i, 3, 3, std::to_string (i + 1).c_str());
    for (int i = 0; i < d->rows; ++i)
        for (int j = 0; j < d->cols; ++j)
        {
            Field f; const auto& pr = d->params[0];
            f.p = mk ("cell", d->vals[0][(size_t) (i * d->cols + j)], pr.lo, pr.hi, pr.names);
            f.row = row0 + i; f.col = c0 + cw * j; f.w = cw; f.align = Align::Center;
            p->fields.push_back (std::move (f));
        }
    Field s; s.p = mk ("Param", 0, 0, (int) sel.size() - 1, sel); s.row = 3; s.col = headCol; s.w = headW; s.align = Align::Left; s.fg = Cyan;
    p->fields.push_back (std::move (s));
    p->selected = 0;
    p->postChange = [d] (LivePage& pg)
    {
        const int np = pg.byLabel ("Param")->p.value;
        const int ncell = d->rows * d->cols;
        if (np == d->cur) { for (int n = 0; n < ncell; ++n) d->vals[(size_t) d->cur][(size_t) n] = pg.fields[(size_t) n].p.value; return; }
        for (int n = 0; n < ncell; ++n) d->vals[(size_t) d->cur][(size_t) n] = pg.fields[(size_t) n].p.value;
        d->cur = np;
        const auto& pr = d->params[(size_t) np];
        for (int n = 0; n < ncell; ++n)
        {
            Param& q = pg.fields[(size_t) n].p;
            q.min = pr.lo; q.max = pr.hi; q.names = pr.names; q.value = d->vals[(size_t) np][(size_t) n];
        }
    };
    return p;
}

std::unique_ptr<LivePage> toneMap()
{
    auto d = std::make_shared<MapData>();
    const Names notes = noteNames();
    static const char* keys[] = { "D 2", "D#3", "F#3", "G#4", "A#4", "C#2", "A#3", "D#6", "C 3", "A 3", "G 4", "D 4", "C 5", "C#3", "A#6", "E 3", "F#5", "D 5", "G#2", "C 2" };
    d->params = {
        { "Original Key", 0, 119, notes, [notes] (int i, int j) { return indexOf (notes, keys[(i * 5 + j * 3) % 20]); } },
        { "Pitch Follow", 0, 1, kOnOff, [] (int, int) { return 1; } },
        { "Pitch Shift", -24, 24, {}, [] (int, int) { return 0; } },
        { "Fine Tune", -64, 63, {}, [] (int, int) { return 0; } },
        { "P.LFO Depth", 0, 127, {}, [] (int, int) { return 0; } },
        { "Pitch Bender", 0, 1, kOnOff, [] (int, int) { return 0; } },
        { "After Touch", 0, 1, kOnOff, [] (int, int) { return 0; } },
        { "TVF", 0, 1, kOnOff, [] (int, int) { return 0; } },
        { "Output Assign", 1, 8, {}, [] (int, int) { return 1; } },
        { "Level", 0, 127, {}, [] (int, int) { return 127; } } };
    Names head; for (int k = 1; k <= 8; ++k) head.push_back ("I" + std::to_string (k));
    auto p = mapPage ("ToneMap", "EDIT-Tone Map", d, 6, 4.25, 5, 4, head, 9, 18);
    label (p->before, 3, 26.6875, 5, "Type");
    Field t; t.p = mk ("Type", 0, 0, 1, { "P", "T" }); t.row = 3; t.col = 31.6875; t.w = 3; t.align = Align::Left; t.fg = Cyan;
    p->fields.push_back (std::move (t));
    p->shift = 4;
    p->extras = [] (Screen& s, const LivePage&) { pages::keyboard (s); drawEditHeld (s); };
    return p;
}

std::unique_ptr<LivePage> patchMap()
{
    auto d = std::make_shared<MapData>();
    d->cols = 4;
    const Names modes { "Normal", "V-Mix", "V-SW", "X-Fade" };
    d->params = {
        { "Key Mode", 0, 3, modes, [] (int i, int j) { return (i * 3 + j * 5 + (i >> 1)) % 4; } },
        { "Key Assign", 0, 1, { "Rotary", "Reset" }, [] (int, int) { return 0; } },
        { "Unison Detune", 0, 127, {}, [] (int, int) { return 4; } },
        { "V-Sw Thresh.", 0, 127, {}, [] (int, int) { return 64; } },
        { "V-Mix Ratio", 0, 127, {}, [] (int, int) { return 0; } },
        { "P.Bend Range", 0, 12, {}, [] (int, int) { return 12; } },
        { "A.T Sense", 0, 127, {}, [] (int, int) { return 127; } },
        { "Oct.Shift", -2, 2, {}, [] (int, int) { return 0; } },
        { "Output Assign", 1, 9, { "1", "2", "3", "4", "5", "6", "7", "8", "Tone" }, [] (int, int) { return 1; } },
        { "Level", 0, 127, {}, [] (int, int) { return 127; } } };
    auto p = mapPage ("PatchMap", "EDIT-Patch Map", d, 8, 7.5, 6, 5, { "I1", "I2", "\0011", "\0012" }, 9, 14);
    p->shift = 4;
    // columns I1 and I2 are the 16 patches of the disk (row i, column j = patch j * 8 + i); the others stay local
    p->refresh = [] (LivePage& pg)
    {
        auto& a = App::get();
        const auto* pb = patchByte (pg.byLabel ("Param")->p.text().substr (pg.byLabel ("Param")->p.text().find (' ') + 1));
        if (! pb) return;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j)
            {
                if (! a.boundPatch (j * 8 + i)) continue;
                Param& q = pg.fields[(size_t) (i * 4 + j)].p;
                q.value = std::max (q.min, std::min (q.max, patchGet (a.patchAt (j * 8 + i), pb->off, pb->sgn) + pb->plus));
            }
    };
    {
        auto base = p->postChange;
        auto last = std::make_shared<int> (0);
        p->postChange = [base, last] (LivePage& pg)
        {
            base (pg);
            auto& a = App::get();
            const int np = pg.byLabel ("Param")->p.value;
            if (np != *last) { *last = np; return; }      // the parameter was switched: nothing to store yet
            const auto* pb = patchByte (pg.byLabel ("Param")->p.text().substr (pg.byLabel ("Param")->p.text().find (' ') + 1));
            if (! pb) return;
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 4; ++j)
                    if (a.boundPatch (j * 8 + i)) patchSet (a.patchAt (j * 8 + i), pb->off, pg.fields[(size_t) (i * 4 + j)].p.value - pb->plus);
        };
    }
    return p;
}

// ---------------------------------------------------------------- DISK pages
std::string trimEnd (std::string s) { while (! s.empty() && s.back() == ' ') s.pop_back(); return s; }
struct Msg { std::string text; bool error = false; };

void drawMsg (Screen& s, const Msg& m, int row)
{
    CellOpts o; o.fg = m.error ? Red : Cyan;
    s.setShift (4);
    s.cell (row, 6.6875, 33, m.text, o);
    s.setShift (0);
}

void jsPopup (Screen& s, double col, int row0, double wc, const Names& items, int sel)
{
    s.setShift (0);
    const int x = (int) ((col - 1) * 16 - 8), y = row0 * kRowH - 8, w = (int) (wc * 16 + 16), h = (int) items.size() * kRowH + 16;
    s.raw (x + 4, y + 4, w, h, Black);
    s.raw (x, y, w, h, White);
    if (sel >= 0) s.raw (x, (row0 + sel) * kRowH - 1, w, kRowH + 1, Red);        // exactly as wide as the white box
    for (size_t i = 0; i < items.size(); ++i)
    {
        CellOpts o; o.fg = (int) i == sel ? White : Red; o.dy = 0;
        s.cell (row0 + (int) i, col, wc, items[i], o);
    }
}

// pull-down of the disk pages: the white box is exactly as high as its lines, the red bar fills the box edge to edge
// (a single entry is one red block with its shadow, no white frame)
void tightPopup (Screen& s, double col, int row0, double wc, const Names& items, int sel)
{
    s.setShift (0);
    const int n = (int) items.size();
    const int x = (int) ((col - 1) * 16 - 8), y = row0 * kRowH - 1, w = (int) (wc * 16 + 16), h = n * kRowH + 3;
    s.eraseCut (x, y, w, h);                                   // a text that reaches under the pull-down only partly is wiped completely
    s.raw (x + 4, y + 4, w, h, Black);
    s.raw (x, y, w, h, White);
    if (sel >= 0 && sel < n)
    {
        const int top = sel == 0 ? y : (row0 + sel) * kRowH - 1, bot = sel == n - 1 ? y + h : (row0 + sel + 1) * kRowH - 1;
        s.raw (x, top, w, bot - top, Red);
    }
    for (int i = 0; i < n; ++i)
    {
        CellOpts o; o.fg = i == sel ? White : Red; o.dy = 0;
        s.cell (row0 + i, col, wc, items[(size_t) i], o);
    }
}

// ---- actions of the pull-down menus.  Memory = App::block (what the edit pages work on), disk = App::disk.
std::string diskLoad (int item, Msg& m)
{
    auto& a = App::get();
    if (! a.hasDisk) { m = { "No disk loaded  (Load Image first)", true }; return std::string(); }
    static const char* nm[] = { "Set \002", "Set \001", "Func", "Block \002", "Block \001" };
    if (item == 2) { m = { std::string (nm[item]) + "  nothing to load on this disk", false }; return std::string(); }
    a.newSet (a.disk);
    int used = 0; for (auto& t : a.block.tones) if (t.used()) ++used;
    m = { std::string ("Loaded ") + nm[item] + "  " + trimEnd (a.block.label) + "  " + std::to_string (used) + " Tones", false };
    return std::string();
}

// IMG entries: after the disk has been written in memory the Finder asks where the .IMG file goes
void saveDiskFile (Msg& m)
{
    auto& a = App::get();
    if (! a.files->dialogs()) return;
    std::string name;
    for (char ch : trimEnd (a.disk.label))
    {
        if (ch >= 'a' && ch <= 'z') ch = (char) (ch - 32);
        if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) && name.size() < 8) name += ch;
    }
    if (name.empty()) name = a.imageName.empty() ? "S550" : a.imageName;
    m = { "Waiting for the Finder ...", false };
    a.files->saveAs (name, a.disk.serialize(), [] (bool ok, const std::string& t)
    {
        auto& app = App::get();
        app.fileMsg = ok ? "Saved  " + t : (t == "Cancelled" ? t : "Could not save: " + t);
        app.fileMsgErr = ! ok && t != "Cancelled";
        app.uiDirty = true;
    });
}

std::string diskSave (int item, Msg& m)
{
    auto& a = App::get();
    static const char* nm[] = { "Set \002", "Set \001", "Block \002", "Block \001", "Chain" };
    a.disk = a.block; a.hasDisk = true;
    m = { std::string ("Saved ") + nm[item] + " to disk", false };
    saveDiskFile (m);
    return std::string();
}

std::string diskFormat (int item, Msg& m)
{
    if (item == 0) return std::string();
    if (item == 2) return "Disk";
    auto& a = App::get();
    a.disk = data::Block(); a.hasDisk = true;
    m = { "Disk formatted", false };
    return std::string();
}

// FD / IMG Save and Backup (photos + manual p.133-140): the command window is open at once; EXE or a click starts the jobs:
// Backup = "Formatting" + save of Set I / II, Save Set / Block = "Now Saving" with the disk label in a white box, "FD I" and the system line,
// Save Chain = Block I, "Change Next Disk", Block II, Format = "Formatting".  The counters count DOWN to 0 (cylinders), "Complete" and Sel at the end.
struct BackupSt
{
    int phase = 1, sel = 0, ji = 0, total = 80, shown = 80;       // phase: 0 menu closed, 1 menu, 2 working, 3 finished, 4 "Change Next Disk", 5 FD: really erase the floppy?
    std::vector<int> jobs;                                         // -1 format, 0 Block I, 1 Block II, 2 Func / MIDI only
    std::chrono::steady_clock::time_point t0; std::string msg; bool writing = false;
    bool choosing = false, haveTarget = false; std::string target;   // IMG: the Finder's save dialog (where / which name) comes first
};

std::unique_ptr<LivePage> backupPage (bool save = false)
{
    auto p = std::make_unique<LivePage>();
    p->id = save ? "FdSave" : "Backup"; p->title = "DISK-FD Backup";
    auto st = std::make_shared<BackupSt>();
    // IMG: the Finder's save dialog first (folder and name), the command is chosen afterwards.  `next` runs when a place was chosen.
    auto ask = [st] (std::function<void()> next)
    {
        auto& a = App::get();
        if (st->choosing) return;
        if (! a.files || ! a.files->dialogs()) { st->haveTarget = false; next(); return; }          // no Finder dialogs (test / browser): carry on without a file
        std::string name;
        for (char ch : trimEnd (a.block.label))
        {
            if (ch >= 'a' && ch <= 'z') ch = (char) (ch - 32);
            if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) && name.size() < 8) name += ch;
        }
        if (name.empty()) name = a.imageName.empty() ? "S550" : a.imageName;
        st->choosing = true; st->haveTarget = false; st->msg.clear(); a.fileMsg.clear();
        a.files->chooseSave (name, [st, next] (bool ok, const std::string& t)
        {
            auto& ap = App::get();
            st->choosing = false; ap.uiDirty = true;
            if (ok) { st->haveTarget = true; st->target = t; st->msg.clear(); next(); }
            else { st->msg = t == "Cancelled" ? std::string() : t; st->phase = 0; }
        });
    };
    auto el = [st] { return std::chrono::duration<double> (std::chrono::steady_clock::now() - st->t0).count(); };
    auto items = [save] { const std::string pre = App::get().viaImg ? "IMG" : "FD";
                          if (save && ! App::get().viaImg)                      // only a real floppy has to be formatted; an image file is simply written
                              return Names { pre + " Save Chain", pre + " Save Set\002", pre + " Save Set\001", pre + " Save Block\002", pre + " Save Block\001", pre + " Save Func", pre + " Format" };
                          return save ? Names { pre + " Save Chain", pre + " Save Set\002", pre + " Save Set\001", pre + " Save Block\002", pre + " Save Block\001", pre + " Save Func" }
                                      : Names { pre + " Backup Set\002", pre + " Backup Set\001" }; };
    auto wc = [] { return 14.6 + (App::get().viaImg ? 1.0 : 0.0); };      // photos: the box is about 250 pixels wide (FD), one letter more for IMG
    auto fin = [st] (bool ok, const std::string& err) { st->phase = 3; st->msg = ok ? "Complete" : err; };
    auto jobsFor = [save] (int sel) -> std::vector<int>
    {
        if (! save) return App::get().viaImg ? std::vector<int> { sel } : std::vector<int> { -1, sel };
        switch (sel) { case 0: return { 0, 9, 1 }; case 1: case 3: return { 0 }; case 2: case 4: return { 1 }; case 6: return { -1 }; default: return { 2 }; }
    };
    // starts job st->ji: the counter total and, for the real drive, the write
    auto startJob = [=] ()
    {
        auto& a = App::get();
        const int j = st->jobs[(size_t) st->ji];
        st->t0 = std::chrono::steady_clock::now(); st->writing = false; st->phase = 2;
        const bool last = st->ji + 1 >= (int) st->jobs.size();
        if (j == -1) { st->total = 80; if (last) a.disk = data::Block(); }                 // (a Backup formats and writes in the following job)
        else if (j == 2) st->total = 3;
        else
        {
            a.disk = a.blk (j);
            long long endByte = data::kOffTone + data::kToneSize * 32;                      // the counter runs over the cylinders that hold data
            for (const auto& t : a.disk.tones)
                if (t.used() && ! t.subTone)
                    endByte = std::max (endByte, (t.waveBank ? data::kOffWaveB : data::kOffWaveA) + (long long) (t.waveStart + t.waveLen) * data::kSegBytes);
            st->total = (int) std::max (4LL, std::min (80LL, (endByte + 18431) / 18432));
        }
        st->shown = st->total;
        a.hasDisk = true;
        if (! a.viaImg && (j == 0 || j == 1 || (j == -1 && last)))
        {
            if (a.drive->startWrite (a.disk.serialize(), false)) st->writing = true; else fin (false, "Drive busy");
        }
    };
    p->refresh = [=] (LivePage& pg)
    {
        auto& a = App::get();
        pg.title = std::string (a.viaImg ? "DISK-IMG " : "DISK-FD ") + (save ? "Save" : "Backup");
        if (st->phase == 2)
        {
            a.uiDirty = true;
            const int j = st->jobs[(size_t) st->ji];
            const double dur = a.viaImg ? 1.0 : j == -1 ? 1.6 : j == 2 ? 0.6 : std::max (0.8, st->total * 0.05);      // an image: 1 s per step
            double frac;
            bool done;
            if (st->writing)
            {
                auto& d = *a.drive; const auto& jb = d.job();
                frac = jb.track / 160.0;
                done = d.state() != FloppyDrive::State::Busy && jb.finished;
                if (done && ! jb.ok) { fin (false, "Write failed\n" + jb.error); return; }
            }
            else { frac = el() / dur; done = el() >= dur; }
            const int cnt = std::max (0, (int) std::ceil (st->total * (1.0 - std::min (1.0, frac))));
            while (st->shown > cnt) { --st->shown; a.floppyClicks += 1; }
            if (done)
            {
                if (a.viaImg && (j == 0 || j == 1 || (j == -1 && st->ji + 1 >= (int) st->jobs.size())) && st->haveTarget)
                {
                    std::string err;
                    if (a.files->writeChosen (a.disk.serialize(), err)) { a.fileMsg = "Saved  " + st->target; a.fileMsgErr = false; }
                    else { a.fileMsg = "Could not save: " + err; a.fileMsgErr = true; }
                }
                a.floppyClicks += 1;
                if (++st->ji >= (int) st->jobs.size()) fin (true, "");
                else if (st->jobs[(size_t) st->ji] == 9)
                {
                    st->phase = 4; st->msg = "Change Next Disk"; ++st->ji;
                    if (a.viaImg) ask ([st, startJob] { st->msg.clear(); startJob(); });            // IMG: the second image gets its own place and name
                }
                else startJob();
            }
        }
        if (st->choosing) a.uiDirty = true;
        pg.comRed = (st->phase == 1 || st->phase == 2) && ! st->choosing;
        pg.btnNames = st->choosing ? std::vector<std::string> { " ", "", "----", "----" } : st->phase == 1 || st->phase == 4 || st->phase == 5 ? std::vector<std::string> { " ", "", "EXE", "exit" } :
                      st->phase == 2 ? std::vector<std::string> { " ", "", "----", "----" } :
                      std::vector<std::string> { " ", "", "Sel", st->phase == 3 ? "----" : "exit" };
    };
    p->extras = [=] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        CellOpts w; w.fg = White;
        const std::string kind = a.viaImg ? "IMG" : "FD";
        if (st->choosing) { s.cell (2, 3.4, 30, "Waiting for the Finder ...", w); return; }
        if (a.viaImg && st->haveTarget && (st->phase == 0 || st->phase == 1)) s.cell (2, 3.4, 20, st->target, w);        // the place chosen in the Finder
        if (st->phase == 2)
        {
            const int j = st->jobs[(size_t) st->ji];
            s.cell (2, 3.4, 16, j == -1 ? kind + " Formatting" : std::string ("Now Saving"), w);
            CellOpts r = w; r.align = Align::Right; s.cell (2, 15.9 + (a.viaImg ? 1.2 : 0.0), 4, std::to_string (st->shown), r);
            if (j == 0 || j == 1)
            {
                const auto& b = a.disk;
                const int bx = 47, by = 72, bw = 224, bh = 90;
                s.raw (bx + 5, by + 5, bw, bh, Black); s.raw (bx, by, bw, bh, White);
                CellOpts l; l.fg = Ink;
                (void) b;
                for (int i = 0; i < 5; ++i) s.cell (4 + i, 4.8, 12, trimEnd (a.labelText[(size_t) (j * 5 + i)]), l);
                CellOpts c; c.fg = Cyan;
                s.cell (10, 4.8, 4, kind, c);
                s.cell (10, 4.8 + s.textWidth (kind) / 16.0 + 1.0, 4, j == 0 ? "I" : "\001", c);
                if (save && ((int) (el() * 2.5)) % 2 == 0)                         // blinking arrow, points to the disk
                {
                    const double ax = 4.8 + s.textWidth (kind) / 16.0 + 0.45;
                    for (int i = 0; i < 6; ++i) { const int h = 11 - 2 * (5 - i); s.raw ((int) (ax * 16) - 9 + i, 10 * kRowH + 9 - h / 2 + 1, 1, h, Cyan); }
                }
                CellOpts y; y.fg = Yellow;
                const std::string sys = trimEnd (b.systemLine());
                s.cell (13, 3.9, 34, sys.empty() ? std::string ("S-550 Ver 1.xx Sound/No System") : sys, y);
            }
        }
        else if (! st->msg.empty() && (st->phase == 3 || st->phase == 4))
        {
            const auto nl = st->msg.find ('\n');
            s.cell (2, 3.4, 20, st->msg.substr (0, nl), w);
            if (nl != std::string::npos)
            {
                std::string rest = st->msg.substr (nl + 1);
                for (auto& ch : rest) if (ch < 32) ch = ' ';
                for (int row = 4; ! rest.empty() && row < 12; ++row) { s.cell (row, 3.4, 37, rest.substr (0, 36), w); rest = rest.size() > 36 ? rest.substr (36) : std::string(); }
            }
            if (! a.fileMsg.empty() && st->phase == 3) drawMsg (s, Msg { a.fileMsg, a.fileMsgErr }, 18);
        }
        else if (! st->msg.empty() && st->phase == 0) s.cell (2, 3.4, 20, st->msg, w);
        else if (st->phase == 5)
        {
            CellOpts r = w; r.fg = Red;
            s.cell (2, 3.4, 22, "FD will be erased !", r);
            s.cell (3, 3.4, 22, "EXE = go on   exit = back", w);
        }
        if ((st->phase == 1 || st->phase == 2 || st->phase == 5) && ! st->choosing) tightPopup (s, 22.0, 4, wc(), items(), st->sel);
    };
    p->menuOpen = [st] { return st->phase == 1 || st->phase == 4 || st->phase == 5; };
    p->buttonsActive = [st] { return st->phase != 2; };
    p->onEnter = [st, ask] { if (! App::get().viaImg) return; st->phase = 0; st->haveTarget = false; ask ([st] { st->phase = 1; }); };     // IMG: where / which name first
    p->comHook = [st, ask] { auto& a = App::get(); if (st->phase == 2 || st->choosing) return false; if (st->phase == 4) { st->phase = 3; st->msg = "Complete"; return true; }     // COM stops a chain
                        if (a.viaImg && ! st->haveTarget) { ask ([st] { st->phase = 1; }); return true; }
                        st->phase = st->phase == 1 ? 0 : 1; st->msg.clear(); return true; };
    p->changeHook = [st, items] (int d) { if (st->phase != 1) return 0; const int v = std::max (0, std::min ((int) items().size() - 1, st->sel + d)); const bool ch = v != st->sel; st->sel = v; return ch ? 1 : 0; };
    p->clickHook = [=] (int x, int y, int) -> int
    {
        if (st->phase != 1) return 0;
        const int x0 = (int) (21.0 * 16 - 8), w = (int) (wc() * 16 + 16), top = 4 * kRowH;
        if (x < x0 || x >= x0 + w || y < top || y >= top + (int) items().size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [=] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->phase == 2 || st->choosing) return "";
        if (st->phase == 3)
        {
            if (which == 0) { if (a.viaImg) { st->haveTarget = false; ask ([st] { st->phase = 1; }); } else { st->phase = 1; st->msg.clear(); } }
            return "";
        }
        if (st->phase == 4)                                          // a new disk was put in: Block II is saved
        {
            if (which == 1) { st->phase = 3; st->msg = "Complete"; return ""; }
            st->msg.clear(); startJob(); return "";
        }
        if (st->phase == 5)                                          // confirmation before a real floppy is formatted / overwritten
        {
            if (which == 1) { st->phase = 1; return ""; }
            st->jobs = jobsFor (st->sel); st->ji = 0; st->msg.clear();
            a.floppyClicks += 2;
            startJob();
            return "";
        }
        if (st->phase == 0)
        {
            if (which == 1) return "Disk";
            if (a.viaImg && ! st->haveTarget) { ask ([st] { st->phase = 1; }); return ""; }
            st->phase = 1; st->msg.clear(); return "";
        }
        if (which == 1) { st->phase = 0; return ""; }
        if (! a.viaImg)
        {
            const auto ds = a.drive->state();
            if (ds == FloppyDrive::State::NoDrive) { st->msg = "No Drive"; st->phase = 0; return ""; }
            if (ds == FloppyDrive::State::NoDisk)  { st->msg = "No Disk"; st->phase = 0; return ""; }
            if (ds == FloppyDrive::State::Busy)    { st->msg = "Drive busy"; st->phase = 0; return ""; }
        }
        if (! a.viaImg && (! save || st->sel == 6)) { st->phase = 5; return ""; }      // FD Backup / FD Format: ask first
        st->jobs = jobsFor (st->sel); st->ji = 0; st->msg.clear();
        a.floppyClicks += 2;
        startJob();
        return "";
    };
    return p;
}

std::string diskSystem (bool change, int item, Msg& m)
{
    if (item == 0) return std::string();
    if (item == 2) return "Disk";
    m = { change ? "System changed  (system area kept as read)" : "System saved  (system area kept as read)", false };
    return std::string();
}

// a popup menu page: Dec/Inc/wheel move the red bar, click selects, Sel opens, exit goes back
std::unique_ptr<LivePage> popupPage (const char* id, const char* title, double col, int row0, double wc, Names items, int sel0,
                                     std::function<std::string (int, Msg&)> onSel, std::string onExit)
{
    auto p = std::make_unique<LivePage>();
    p->id = id; p->title = title; p->popupButtons = true;
    auto sel = std::make_shared<int> (sel0);
    const int n = (int) items.size();
    auto msg = std::make_shared<Msg>();
    p->extras = [=] (Screen& s, const LivePage&)
    {
        jsPopup (s, col, row0, wc, items, *sel);
        auto& a = App::get();
        if (! a.fileMsg.empty()) drawMsg (s, Msg { a.fileMsg, a.fileMsgErr }, 18);
        else if (! msg->text.empty()) drawMsg (s, *msg, 18);
    };
    p->changeHook = [=] (int d) { const int v = std::max (0, std::min (n - 1, *sel + d)); const bool ch = v != *sel; *sel = v; return ch ? 1 : 0; };
    p->clickHook = [=] (int x, int y, int) 
    {
        const int x0 = (int) ((col - 1) * 16 - 8), w = (int) (wc * 16 + 16);
        if (x < x0 || x >= x0 + w) return 0;
        const int i = (y - (row0 * kRowH - 1)) / kRowH;
        if (y < row0 * kRowH - 1 || i < 0 || i >= n) return 0;
        if (i != *sel) *msg = {};
        *sel = i; return 2;
    };
    p->buttonHook = [=] (int b) { return b == 0 ? onSel (*sel, *msg) : onExit; };
    return p;
}

std::unique_ptr<LivePage> diskMenu()
{
    struct Item { const char* text; const char* target; };
    // left: the internal memory (HD of the original), right: disk images on the Mac (IMG) and the real floppy drive (FD)
    static const Item left[]  = { { "HD Load", "LoadAll" }, { "HD Load P", "LoadPRM" }, { "HD Load T", "LoadTone" },
                                  { "HD Save", "SaveAll" }, { "Setup", "DiskSetup" }, { "Set Label", "LabelSet" }, { "Change SYS", "ChangeSys" },
                                  { "MIDI Load", "MidiLoad" }, { "MIDI Save", "MidiSave" }, { "Load Wave", "LoadWave" } };
    static const Item right[] = { { "IMG Load", "ImgLoad" }, { "IMG Load P", "LoadPRM" }, { "IMG Load T", "LoadTone" },
                                  { "IMG Save", "FdSave" }, { "IMG Backup", "Backup" },
                                  { "FD Load", "FdLoad" }, { "FD Load P", "LoadPRM" }, { "FD Load T", "LoadTone" },
                                  { "FD Save", "FdSave" }, { "FD Backup", "Backup" } };
    const int nl = (int) (sizeof left / sizeof left[0]), nr = (int) (sizeof right / sizeof right[0]);
    auto p = std::make_unique<LivePage>();
    p->id = "Disk"; p->title = "DISK-HD Load"; p->popupButtons = true;
    auto sel = std::make_shared<int> (0);
    auto textOf = [=] (int i) { return i < nl ? left[i].text : right[i - nl].text; };
    p->refresh = [=] (LivePage& pg)
    {
        pg.title = std::string ("DISK-") + textOf (*sel);
        pg.btnNames = *sel >= nl ? std::vector<std::string> { " " } : std::vector<std::string> {};     // FD / IMG entries: SUB is blank
    };
    p->extras = [=] (Screen& s, const LivePage&)
    {
        CellOpts b; b.bg = White; b.fg = Red; b.align = Align::Center; s.cell (1, 9.5, 5, "MENU", b);
        s.setShift (0);
        const int rows = std::max (nl, nr);
        const int h = rows * 19 + 4;
        s.raw (136 + 4, 74 + 4, 368, h, Black);                 // PREVIEW: box flush with the MENU tab (x 136) and the right edge of Sel / Dec (x 504)
        s.raw (136, 74, 368, h, White);
        s.setShift (6);
        const int cs = *sel;
        const bool inLeft = cs < nl;
        s.bar (4 + (inLeft ? cs : cs - nl), inLeft ? 9.5 : 21.0, 11.5, Red);
        for (int i = 0; i < nl + nr; ++i)
        {
            CellOpts o; o.dy = 0; o.fg = i == cs ? White : Red;
            const bool l = i < nl;
            const std::string tx = textOf (i);
            s.cell (4 + (l ? i : i - nl), (l ? 10.25 : 21.75) - Screen::leadInk (tx[0]) / 16.0, l ? 10.5 : 12.5, tx, o);   // the ink of the first letters stands flush
        }
        CellOpts y; y.fg = Yellow; y.align = Align::Right;                      // block marks left of the menu
        { static const char* const mk6[] = { "S", "S", "S", "S", "H", "C" }; for (int i = 0; i < 6; ++i) s.cell (4 + i, 4.8125, 2, mk6[i], y); }   // marks of the CD-5 menu (photo)
        s.setShift (0);
    };
    p->changeHook = [=] (int d) { const int v = std::max (0, std::min (nl + nr - 1, *sel + d)); const bool ch = v != *sel; *sel = v; return ch ? 1 : 0; };
    p->clickHook = [=] (int x, int y, int)
    {
        const int r = (y - (4 * kRowH - 1 + 6)) / kRowH;
        if (y < 4 * kRowH + 5 || r < 0) return 0;
        int i;
        if (x >= 136 && x < 320) { if (r >= nl) return 0; i = r; }
        else if (x >= 320 && x < 504) { if (r >= nr) return 0; i = nl + r; }
        else return 0;
        *sel = i; return 2;
    };
    p->buttonHook = [=] (int b)
    {
        if (b != 0) return std::string ("Standard");
        const int i = *sel;
        App::get().viaImg = i >= nl && std::string (right[i - nl].text).compare (0, 3, "IMG") == 0;
        App::get().fileMsg.clear();
        if (i < 4)                                           // HD entries: the page of the real machine; the Finder opens first
        {
            static const char* const pg[] = { "HdSet", "HdLoadP", "HdLoadT", "HdSave" };
            App::get().hdTarget = pg[i]; return std::string (pg[i]);
        }
        return std::string (i < nl ? left[i].target : right[i - nl].target);
    };
    return p;
}

// Load P.PRM / Load Tone / Save P.PRM: two id fields (disk side and internal side)
void diskPair (LivePage& p, bool tone, bool internalFirst)
{
    const Names& ids = tone ? toneIds() : patchIds();
    const int top = internalFirst ? 4 : 4;
    const char* l0 = internalFirst ? "Internal" : "Disk";
    const char* l1 = internalFirst ? "Disk" : "Internal";
    const double w0 = internalFirst ? 8 : 5, w1 = internalFirst ? 5 : 8;
    auto addId = [&] (const char* lab, double lw, int row, bool selectedDefault)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = lw; f.row = row; f.col = 15.6875; f.w = 3; f.align = Align::Left;
        f.p = mk (lab, 0, 0, (int) ids.size() - 1, ids); p.fields.push_back (std::move (f));
        (void) selectedDefault;
    };
    addId (l0, w0, top, false); addId (l1, w1, top + 1, true);
    p.selected = internalFirst ? 0 : 1;
}

std::unique_ptr<LivePage> loadPrm (bool save)
{
    auto p = std::make_unique<LivePage>();
    p->id = save ? "SavePRM" : "LoadPRM"; p->title = save ? "DISK-Save P.PRM" : "DISK-Load P.PRM";
    diskPair (*p, false, save);
    p->extras = [] (Screen& s, const LivePage& pg)
    {
        s.setShift (4);
        CellOpts c; c.fg = Cyan;
        for (const auto& f : pg.fields) s.cell (f.row, 19.6875, 12, patchNames()[(size_t) f.p.value], c);
        s.setShift (0);
    };
    return p;
}



// geometry of the big tone view (SUB)
constexpr double kTmC0 = 4.75, kTmPitch = 8.7, kTmW = 8, kPatchPitch = 13.0;
int tmRow (int blk, int k) { return (blk ? 14 : 5) + k; }

bool tmHit (int x, int y, int& idx)
{
    const int r = y / kRowH;
    int blk, k;
    if (r >= 5 && r <= 12) { blk = 0; k = r - 5; }
    else if (r >= 14 && r <= 21) { blk = 1; k = r - 14; }
    else return false;
    const double cx = x / 16.0 + 1 - kTmC0;
    const int g = (int) std::floor (cx / kTmPitch);
    if (g < 0 || g > 3 || cx - g * kTmPitch >= kTmW) return false;
    idx = blk * 32 + g * 8 + k;
    return true;
}

// ---- the big tone view (Tone List Display, manual p.37-38): names, or with [N/T] the wave data (bank - sampling time, Sub xx)
// Original tone = white name / "A-0.8" (15 kHz: "A-0.8x2"), Sub tone = red name / red "Sub 11" (the original it borrows from), deleted tone = blank / red "Sub --"
struct ToneCell { std::string text; Col col = White; };
ToneCell toneCell (const data::Block* b, int idx, bool times)
{
    ToneCell r;
    if (! b) return r;
    const data::Tone& t = b->tones[(size_t) (idx % 32)];
    const bool sub = t.subTone && b->tones[(size_t) (t.sourceTone & 31)].used() && ! b->tones[(size_t) (t.sourceTone & 31)].subTone;
    const bool orig = ! t.subTone && t.used();
    if (orig)
    {
        if (! times) { r.text = trimSp (t.name); r.col = White; return r; }
        char buf[24]; std::snprintf (buf, sizeof buf, "%c-%.1f%s", t.waveBank ? 'B' : 'A', t.waveLen * 0.4, t.khz15 ? "x2" : "");
        r.text = buf; r.col = Cyan; return r;
    }
    r.col = Red;
    if (sub) r.text = times ? "Sub " + toneIds()[(size_t) (t.sourceTone & 31)].substr (1) : trimSp (t.name);
    else r.text = times ? "Sub --" : std::string();
    return r;
}
// the [N/T] switch: red while the mouse is over it, a click toggles between names and wave data
bool ntHit (int x, int y) { return x >= 426 && x < 557 && y >= 3 * kRowH - 1 && y < 4 * kRowH - 1; }
void drawNT (Screen& s, bool hover)
{
    if (hover) s.bar (3, 27.6, 8.2, Red);
    CellOpts c; c.fg = hover ? Black : Cyan; c.align = Align::Left;
    s.cell (3, 28.1, 4, "N/T", c);
}

// FD / IMG Load P (photos of the original): FD P# / Int P# / Used Tone Only; SUB = Disk / Internal (which line Dec / Inc change),
// COM = command window (Load P.PRM, Load T.List, Load Patch, *Delete Patch), "Now Loading", then the tone list of the disk
struct FdLP
{
    int menu = 0, sel = 0; long long until = 0, start = 0, dur = 1400; int tracks = 80; std::string pending; bool list = false; std::string msg; bool err = false;
    // FD: the real floppy is read when the page opens, at SUB and before every command (the disk may have been changed)
    int big = 0, pick = 0; bool times = false, nt = false;                                 // SUB -> Sel: the big tone view of the disk (1) / of the memory (2)
    bool opened = false, reading = false, fdOk = false; int readFor = -2, readLimit = 16; data::Block fdDisk; std::function<void (int)> exec;
};

// the disk the page works on: the loaded image (IMG) or the floppy that was just read (FD)
const data::Block* fdSource (FdLP& st)
{
    auto& a = App::get();
    if (a.viaImg) return a.hasDisk ? &a.disk : nullptr;
    return st.fdOk ? &st.fdDisk : nullptr;
}

// tracks (4608 bytes) of the floppy needed for a command: names of patches and tones lie in the first 16, wave data comes after them
int fdTracksNeeded (const data::Block& b, bool tone, int fi, int sel)
{
    auto end = [&] (const data::Tone& t)
    {
        if (! t.used() || t.subTone) return 16;
        const long long e = (t.waveBank ? data::kOffWaveB : data::kOffWaveA) + (long long) (t.waveStart + t.waveLen) * data::kSegBytes;
        return (int) std::min (160LL, (e - 1) / 4608 + 1);
    };
    int need = 16;
    if (tone) need = std::max (need, end (b.tones[(size_t) fi]));
    else if (sel == 2)                                                // Load Patch: the waves of every tone the patch plays
    {
        std::set<int> use;
        const auto& pt = b.patches[(size_t) fi];
        for (int k = 0; k < 109; ++k) { use.insert (pt.raw[17 + k] & 31); use.insert (pt.raw[126 + k] & 31); }
        for (int t : use)
        {
            const auto& tn = b.tones[(size_t) t];
            need = std::max (need, end (tn));
            if (tn.subTone) need = std::max (need, end (b.tones[(size_t) (tn.sourceTone & 31)]));
        }
    }
    return need;
}

bool fdStartRead (FdLP& st, int forCmd, int limit = 16)
{
    auto& a = App::get();
    using S = FloppyDrive::State;
    const auto ds = a.drive->state();
    st.menu = forCmd >= 0 ? 0 : st.menu; st.fdOk = false; st.list = false;
    if (ds == S::NoDrive) { st.msg = "No Drive"; st.err = true; return false; }
    if (ds == S::NoDisk)  { st.msg = "No Disk"; st.err = true; return false; }
    st.readLimit = limit;
    if (ds == S::Busy || ! a.drive->startRead (limit)) { st.msg = "Drive busy"; st.err = true; return false; }
    st.reading = true; st.readFor = forCmd; st.msg.clear(); st.err = false; a.floppyClicks += 2;
    return true;
}

// info text right of a tone name: wave bank and length, "Sub xxx" for a sub tone, "Sub --" for an empty place
std::string toneInfoText (const data::Tone& t)
{
    if (! t.used()) return "Sub --";
    if (t.subTone) return "Sub " + toneIds()[(size_t) (t.sourceTone & 31)];
    return data::toneInfo (t);
}

// FD / IMG Load P and Load T (photos of the original): FD P# / Int P# / Used Tone Only  or  FD T# / Int T# / Wave Bank;
// SUB = Disk / Internal (which line Dec / Inc change), COM = command window, "Now Loading", Load P: then the tone list of the disk
std::unique_ptr<LivePage> fdLoadPT (bool tone)
{
    auto p = std::make_unique<LivePage>();
    p->id = tone ? "LoadTone" : "LoadPRM"; p->title = tone ? "DISK-FD Load T" : "DISK-FD Load P";
    Names fdIds = tone ? toneIds() : patchIds();
    if (tone) fdIds.resize (data::kNumTones);                          // a disk holds one block: I11 - I48
    const Names intIds = tone ? toneIds() : patchIds();                // memory: Block I and II
    auto add = [&] (const char* lab, int row, Param q, double w)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = 14; f.row = row; f.col = 20.25; f.w = w; f.align = Align::Left; f.p = std::move (q);
        p->fields.push_back (std::move (f));
    };
    add ("FD", 4, mk ("FD #", 0, 0, (int) fdIds.size() - 1, fdIds), 3);
    add ("Int", 5, mk ("Int #", 0, 0, (int) intIds.size() - 1, intIds), 3);
    if (tone) add ("Wave Bank", 6, mk ("Wave Bank", 0, 0, 1, { "A", "B" }), 2);
    else add ("Used Tone Only", 6, mk ("Used", 0, 0, 1, kOnOff), 3);
    p->selected = 0;
    auto st = std::make_shared<FdLP>();
    LivePage* self = p.get();
    auto comItems = [tone] { const std::string pre = App::get().viaImg ? "IMG" : "FD";
                             return tone ? Names { pre + " Load Tone" } : Names { pre + " Load P.PRM", pre + " Load T.List", pre + " Load Patch", "*Delete Patch" }; };
    auto comWc = [comItems] { double w = 14; for (const auto& t : comItems()) w = std::max (w, (double) t.size()); return w; };
    auto subNames = [] { return App::get().viaImg ? Names { "IMG", "Internal" } : Names { "Disk", "Internal" }; };
    p->refresh = [st, self, tone] (LivePage& pg)
    {
        auto& a = App::get();
        pg.title = std::string (a.viaImg ? "DISK-IMG Load " : "DISK-FD Load ") + (tone ? "T" : "P");
        pg.fields[0].label = a.viaImg ? "IMG" : "FD";
        if (! a.viaImg && ! st->opened) { st->opened = true; fdStartRead (*st, -2); }
        if (st->reading)
        {
            a.uiDirty = true;
            auto& d = *a.drive; const auto& j = d.job();
            if (d.state() != FloppyDrive::State::Busy && j.finished)
            {
                st->reading = false;
                data::Block nb;
                if (! j.ok) { st->msg = j.error.empty() ? std::string ("Read failed") : j.error.substr (0, 36); st->err = true; }
                else if (const auto res = nb.parse (j.data.data(), j.data.size()); res != data::Block::Result::Ok)
                {
                    st->err = true;
                    if (res == data::Block::Result::BadSize) st->msg = "Read incomplete " + std::to_string (j.data.size());
                    else   // what the first bytes of the disk really are (a real S-550 disk starts 00 00 00 00 53 35 35 30)
                    {
                        static const char* hx = "0123456789ABCDEF"; std::string h;
                        for (int k = 0; k < 4 && k < (int) j.data.size(); ++k) { h += hx[j.data[(size_t) k] >> 4]; h += hx[j.data[(size_t) k] & 15]; }
                        st->msg = "No S-550 disk " + h;
                    }
                }
                else
                {
                    st->fdDisk = nb; st->fdOk = true;
                    if (st->readFor >= 0)
                    {
                        const int need = fdTracksNeeded (nb, tone, self->fields[0].p.value, st->readFor);
                        if (need > st->readLimit) fdStartRead (*st, st->readFor, need);          // the waves lie further out: read on
                        else if (st->exec) st->exec (st->readFor);
                    }
                }
                a.floppyClicks += 1;
            }
        }
        if (st->until)
        {
            if (nowMs() >= st->until) { st->until = 0; st->menu = 0; st->msg = st->pending; st->err = st->pending != "Complete" && ! st->pending.empty(); }
            else a.uiDirty = true;
        }
        const bool work = st->until != 0 || st->reading;
        pg.subRed = st->menu == 1 || st->big; pg.comRed = st->menu == 2 && ! st->big;
        pg.btnNames = work ? std::vector<std::string> { "", "", "----", "----" }
                  : st->big || st->menu == 1 ? std::vector<std::string> { "", "", "Sel", "exit" }
                  : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> {};
        (void) self;
    };
    p->extras = [st, comItems, comWc, tone, subNames] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        const data::Block* src = fdSource (*st);
        const int fi = pg.fields[0].p.value, ii = pg.fields[1].p.value;
        {   // the second word of the labels stands exactly under each other (the font is proportional)
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            s.setShift (4);
            s.cell (4, 10.94, 3, tone ? "T#" : "P#", y); s.cell (5, 10.94, 3, tone ? "T#" : "P#", y);
            if (! tone) s.cell (5, 13.9, 8, "(Del #)", y);
            s.setShift (0);
        }
        s.setShift (4);
        CellOpts c; c.fg = Cyan;
        if (tone && st->menu != 1)             // the SUB pull-down covers both lines completely: nothing may peek out beside it
        {
            if (src) { const auto& t = src->tones[fi]; s.cell (4, 24.25, 10, trimEnd (t.name), c); s.cell (4, 32.9, 7, toneInfoText (t), c); }
            const auto& it = a.blk (ii / 32).tones[ii % 32];
            s.cell (5, 24.25, 10, trimEnd (it.name), c); s.cell (5, 32.9, 7, toneInfoText (it), c);
        }
        else
        {
            if (src) s.cell (4, 24.25, 14, trimEnd (src->patches[fi].name()), c);
            s.cell (5, 24.25, 14, trimEnd (a.block.patches[ii].name()), c);
        }
        s.setShift (0);
        if (tone) remTime (s, 10);
        CellOpts w; w.fg = White; w.align = Align::Left;
        if (st->reading)
        {
            const int cnt = std::max (0, (int) std::ceil (64.0 * (1.0 - a.drive->job().track / (double) std::max (1, a.drive->job().limit))));
            s.cell (2, 3.5, 12, "Now Loading", w);
            s.cell (2, 16.7, 4, std::to_string (cnt), w);
            if (! a.drive->job().sysLine.empty()) { CellOpts y; y.fg = Yellow; y.align = Align::Left; s.cell (13, 4.4, 30, a.drive->job().sysLine, y); }
        }
        else if (st->until)
        {
            s.cell (2, 3.5, 12, "Now Loading", w);
            if (tone)
            {
                const int track = (int) std::max (0LL, std::min ((long long) st->tracks, (nowMs() - st->start) * st->tracks / std::max (1LL, st->dur)));
                s.cell (2, 16.7, 4, std::to_string (track), w);
                if (track >= 3 && a.hasDisk) { CellOpts y; y.fg = Yellow; y.align = Align::Left; s.cell (13, 4.4, 30, a.disk.systemLine(), y); }
            }
        }
        else if (! st->msg.empty() && st->menu == 0) { CellOpts m = w; if (st->err) m.fg = Red; s.cell (2, 3.5, 24, st->msg, m); }
        if (st->list && src && ! tone)
        {
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            CellOpts cc; cc.fg = White; cc.align = Align::Center;            // Org / --- / *** are white, labels and numbers yellow
            CellOpts yn = y; yn.align = Align::Center;
            std::set<int> use;
            if (pg.fields[2].p.value)                                // Used Tone Only: only the tones the patch plays
            {
                const auto& pt = src->patches[fi];
                for (int k = 0; k < 109; ++k) { use.insert (pt.raw[17 + k] & 31); use.insert (pt.raw[126 + k] & 31); }
            }
            auto grid = [&] (int row0, const char* head, const char* pre, bool disk)
            {
                s.cell (row0, 3.6, 5, head, y);
                for (int k = 0; k < 8; ++k) s.cell (row0, 7.2 + 3.8125 * k, 3, std::to_string (k + 1), yn);
                for (int r = 0; r < 4; ++r)
                {
                    s.cell (row0 + 1 + r, 4.6, 3, std::string (pre) + std::to_string (r + 1), y);
                    for (int k = 0; k < 8; ++k)
                    {
                        const int t = r * 8 + k;
                        const auto& tn = disk ? src->tones[t] : a.block.tones[t];
                        const bool shown = tn.used() && (! disk || ! pg.fields[2].p.value || use.count (t));
                        if (disk) s.cell (row0 + 1 + r, 7.2 + 3.8125 * k, 3, shown ? (tn.subTone ? "Sub" : "Org") : "---", cc);
                        else s.cell (row0 + 1 + r, 7.2 + 3.8125 * k, 3, shown ? "***" : "---", cc);
                    }
                }
            };
            grid (10, "[FD]", "D", true);
            grid (16, "[I]", "I", false);
        }
        if (st->menu == 1 || st->big)              // SUB: the big view of all places with the names (the pull-down stays over it), a click picks, Sel confirms
        {
            const int which = st->big ? st->big : st->sel + 1;       // 1 = the disk / image, 2 = the memory
            s.raw (0, 36, 360, 18, Black);
            s.raw (16, 54, 608, 386, Panel);
            s.forgetText();
            const Names ids = tone ? toneIds() : patchIds();
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            s.cell (2, 3, 12, subNames()[(size_t) (which - 1)], y);
            s.cell (2, 13.4, 4, ids[(size_t) (st->big ? st->pick : pg.fields[(size_t) (which - 1)].p.value)], y);
            CellOpts c; c.fg = Cyan; c.align = Align::Left;
            if (st->big)
            {
                for (int r = 0; r < 9; ++r) s.raw (268, 38 + r, (r <= 4 ? r + 1 : 9 - r) * 2, 1, Cyan);
                s.cell (2, 19.4, 4, ids[(size_t) st->pick], c);
            }
            if (tone) drawNT (s, st->nt);
            for (int blk = 0; blk < (which == 1 ? 1 : 2); ++blk)
            {
                for (int g = 0; g < (tone ? 4 : 2); ++g)
                {
                    CellOpts h; h.fg = Yellow; h.align = Align::Center;
                    s.cell (blk ? 13 : 4, kTmC0 + g * (tone ? kTmPitch : kPatchPitch), kTmW, std::string (blk ? "\001" : "\002") + std::to_string (g + 1), h);
                }
                for (int k = 0; k < 8; ++k)
                {
                    CellOpts n; n.fg = Yellow; n.align = Align::Left;
                    s.cell (tmRow (blk, k), 3.25, 1, std::to_string (k + 1), n);
                    for (int g = 0; g < (tone ? 4 : 2); ++g)
                    {
                        CellOpts o; o.align = Align::Left; o.fg = White;
                        std::string txt;
                        int idx;
                        if (tone)
                        {
                            idx = blk * 32 + g * 8 + k;
                            const data::Block* tb = which == 1 ? fdSource (*st) : &a.blk (idx / 32);
                            const ToneCell tc = toneCell (tb, idx, st->times);
                            txt = tc.text; o.fg = tc.col;
                        }
                        else
                        {
                            idx = blk * 16 + g * 8 + k;                           // patches: 16 per block, two columns
                            const data::Block* pb = which == 1 ? fdSource (*st) : &a.blk (idx / 16);
                            if (pb) txt = trimSp (pb->patches[(size_t) (idx % 16)].name());
                        }
                        if (st->big && idx == st->pick) { o.bg = Red; o.fg = White; }
                        s.cell (tmRow (blk, k), kTmC0 + g * (tone ? kTmPitch : kPatchPitch), tone ? kTmW : 12.5, txt, o);
                    }
                }
            }
        }
        if (st->menu == 1)
        {
            tightPopup (s, 15.3, 4, 10.3, subNames(), st->sel);
        }
        else if (st->menu == 2 && ! st->big) tightPopup (s, 21.25, 4, comWc(), comItems(), st->sel);
    };
    p->menuOpen = [st] { return st->menu != 0 || st->big; };
    p->buttonsActive = p->menuOpen;
    p->subHook = [st] { if (st->until || st->reading || st->big) return false; st->menu = st->menu == 1 ? 0 : 1; st->sel = 0; st->msg.clear();
                        if (st->menu == 1 && ! App::get().viaImg) fdStartRead (*st, -2);          // opening SUB reads the disk again
                        return true; };
    p->comHook = [st] { if (st->until || st->reading || st->big) return false; st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; st->msg.clear(); return true; };
    p->clickHook = [st, comItems, comWc, tone] (int x, int y, int d) -> int
    {
        if (tone && (st->big || st->menu == 1))                    // [N/T]: red under the mouse, a click switches names <-> wave data
        {
            const bool was = st->nt;
            st->nt = ntHit (x, y);
            if (st->nt) { if (d != 0) st->times = ! st->times; return 1; }
            if (was) return 1;                                      // the mouse left [N/T]: repaint
        }
        if (st->big)
        {
            int idx;
            if (tone) { if (! tmHit (x, y, idx)) return 0; }
            else                                                     // patch grid: blocks of 16, two wide columns of 8 (names of 12 letters)
            {
                const int r = y / kRowH;
                int blk, k;
                if (r >= 5 && r <= 12) { blk = 0; k = r - 5; }
                else if (r >= 14 && r <= 21) { blk = 1; k = r - 14; }
                else return 0;
                const double cx = x / 16.0 + 1 - kTmC0;
                const int g = (int) std::floor (cx / kPatchPitch);
                if (g < 0 || g > 1 || cx - g * kPatchPitch >= kPatchPitch - 0.5) return 0;
                idx = blk * 16 + g * 8 + k;
            }
            if (st->big == 1 && idx >= (tone ? 32 : 16)) return 0;
            st->pick = idx; return 2;
        }
        if (! st->menu) return -1;
        if (st->until || st->reading) return 0;
        const int n = st->menu == 1 ? 2 : (int) comItems().size();
        const double col = st->menu == 1 ? 15.3 : 21.25, wc = st->menu == 1 ? 10.3 : comWc();
        const int x0 = (int) ((col - 1) * 16 - 8), w = (int) (wc * 16 + 16), top = 4 * kRowH;
        if (x < x0 || x >= x0 + w || y < top || y >= top + n * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    // the command itself (runs at once for an image, after the floppy was read for FD)
    FdLP* sp = st.get();
    st->exec = [sp, self, tone] (int sel)
    {
        auto& a = App::get();
        const int fi = self->fields[0].p.value, ii = self->fields[1].p.value;
        const data::Block* src = fdSource (*sp);
        if (! src && ! (! tone && sel == 3)) { sp->menu = 0; sp->msg = "No disk loaded"; sp->err = true; return; }
        std::string err;
        if (tone)
        {
            err = data::loadTone (*src, fi, a.blk (ii / 32), ii % 32, self->fields[2].p.value);
            if (err.empty()) { if (ii < 32) a.linked = true; else a.linked2 = true; }
            sp->pending = err;                                             // success: nothing is shown (photo of the original)
            {   // the counter runs over the tracks that hold this tone: system area, tone records and the wave data (9216 bytes per track)
                const auto& t = src->tones[fi];
                long long endByte = data::kOffTone + data::kToneSize * 32;
                if (t.used() && ! t.subTone)
                    endByte = (t.waveBank ? data::kOffWaveB : data::kOffWaveA) + (long long) (t.waveStart + t.waveLen) * data::kSegBytes;
                sp->tracks = (int) std::min (159LL, (endByte - 1) / 9216);
                sp->dur = a.viaImg ? 1000 : std::max (500, sp->tracks * 20);    // an image loads in 1 s
            }
            sp->start = nowMs(); sp->until = sp->start + sp->dur;
            return;
        }
        if (sel == 0) a.block.patches[ii] = src->patches[fi];
        else if (sel == 1) sp->list = true;
        else if (sel == 2) { std::vector<int> nl; err = data::loadPatch (*src, fi, a.block, ii, &nl); }
        else data::deletePatch (a.block, ii);
        if (sel != 1) { a.linked = true; sp->list = false; }
        sp->pending = err.empty() ? (sel == 1 ? std::string() : std::string ("Complete")) : err;
        sp->start = nowMs(); sp->until = sp->start + (a.viaImg ? 1000 : 900);
    };
    p->buttonHook = [st, self, tone] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->until || st->reading) return "";
        if (st->big)                                                   // the big view: a click picked a place, Sel takes it, exit leaves
        {
            const int was = st->big;
            if (which == 0 && st->pick < (tone ? 32 : 16) * (was == 1 ? 1 : 2)) self->fields[(size_t) (was - 1)].p.value = st->pick;
            st->big = 0; st->menu = which == 0 ? 1 : 0; st->sel = was % 2;      // Sel: the next line is asked right away, exit leaves
            return "";
        }
        if (! st->menu) { self->change (which == 0 ? -1 : +1); return ""; }
        const int m = st->menu, sel = st->sel;
        if (which == 1) { st->menu = 0; return ""; }
        if (m == 1) { self->selected = sel; st->menu = 0; st->big = sel + 1; st->pick = self->fields[(size_t) sel].p.value; return ""; }
        if (! a.viaImg && ! (! tone && sel == 3)) { fdStartRead (*st, sel); return ""; }       // FD: read the floppy first, then execute
        st->exec (sel);
        return "";
    };
    return p;
}
std::unique_ptr<LivePage> fdLoadP() { return fdLoadPT (false); }

// ---------------------------------------------------------------- DISK: images and real floppies
std::string imgName (int n) { std::string d = std::to_string (n); while (d.size() < 3) d = "0" + d; return "S550_" + d; }
Names imgNames() { Names v; for (int i = 1; i <= 99; ++i) v.push_back (imgName (i)); return v; }
int firstFreeNumber()
{
    auto files = App::get().files->list();
    for (int i = 1; i <= 99; ++i) if (std::find (files.begin(), files.end(), imgName (i)) == files.end()) return i;
    return 1;
}

// the list of files in the folder (rows row0.., at most n), a name equal to `mark` in yellow
void drawFileList (Screen& s, int row0, int n, const std::string& mark)
{
    auto& f = *App::get().files;
    auto v = f.list();
    s.setShift (4);
    CellOpts y; y.fg = Yellow; s.cell (row0 - 1, 6.6875, 12, "In folder", y);
    for (int i = 0; i < (int) v.size() && i < n; ++i)
    {
        CellOpts c; c.fg = v[(size_t) i] == mark ? Yellow : White;
        s.cell (row0 + i, 6.6875, 11, v[(size_t) i], c);
        CellOpts e; e.fg = c.fg; s.cell (row0 + i, 6.6875 + s.textWidth (v[(size_t) i]) / (double) kCellW, 4, ".IMG", e);
        CellOpts z; z.fg = c.fg; z.align = Align::Right; s.cell (row0 + i, 29.6875, 8, std::to_string (f.sizeOf (v[(size_t) i])), z);
    }
    if (v.empty()) { CellOpts c; c.fg = Cyan; s.cell (row0, 6.6875, 20, "(no images)", c); }
    s.setShift (0);
}


// Load Image with Finder dialogs (no fixed folder): the dialog opens when the page opens; Sel opens it again
std::unique_ptr<LivePage> loadImageDialog()
{
    auto p = std::make_unique<LivePage>();
    p->id = "LoadImage"; p->title = "DISK-Load Image";
    label (p->before, 4, 6.6875, 8, "File");
    label (p->before, 5, 6.6875, 8, "Bank");
    p->extras = [] (Screen& s, const LivePage&)
    {
        auto& app = App::get();
        CellOpts c; c.fg = Cyan;
        s.setShift (4);
        if (! app.imageName.empty())
        {
            CellOpts w; s.cell (4, 15.6875, 12, app.imageName, w);
            CellOpts y; y.fg = Yellow; s.cell (4, 15.6875 + s.textWidth (app.imageName) / (double) kCellW, 4, ".IMG", y);
        }
        s.cell (5, 15.6875, 14, trimEnd (app.block.label), c);
        CellOpts y; y.fg = Yellow; s.cell (8, 6.6875, 30, "Sel: choose another image in the Finder", y);
        s.setShift (0);
        if (! app.fileMsg.empty()) drawMsg (s, Msg { app.fileMsg, app.fileMsgErr }, 12);
    };
    p->buttonHook = [] (int b)
    {
        if (b == 1) return std::string ("Disk");
        App::get().pickImage (true);
        return std::string();
    };
    p->changeHook = [] (int) { return 0; };
    p->clickHook = [] (int, int, int) { return 0; };
    return p;
}

std::unique_ptr<LivePage> loadImage()
{
    auto p = std::make_unique<LivePage>();
    p->id = "LoadImage"; p->title = "DISK-Load Image";
    label (p->before, 4, 6.6875, 8, "Folder");
    label (p->before, 5, 6.6875, 8, "File");
    label (p->before, 6, 6.6875, 8, "Bank");
    Field f; f.label = "File"; f.p = mk ("File", 0, 0, 0, { "(none)" }); f.row = 5; f.col = 15.6875; f.w = 11; f.align = Align::Left;
    p->fields.push_back (std::move (f));
    p->refresh = [] (LivePage& pg)
    {
        auto v = App::get().files->list();
        Param& q = pg.fields[0].p;
        q.names = v.empty() ? Names { "(none)" } : v;
        q.max = (int) q.names.size() - 1; q.value = std::max (0, std::min (q.value, q.max));
    };
    auto msg = std::make_shared<Msg>();
    LivePage* raw = p.get();
    p->extras = [msg] (Screen& s, const LivePage& pg)
    {
        auto& app = App::get();
        CellOpts c; c.fg = Cyan;
        s.setShift (4);
        s.cell (4, 15.6875, 22, app.files->folderName(), c);
        { CellOpts y; y.fg = Yellow; const std::string nm = pg.fields[0].p.text(); if (nm != "(none)") s.cell (5, 15.6875 + s.textWidth (nm) / (double) kCellW, 4, ".IMG", y); }
        s.cell (6, 15.6875, 14, trimEnd (app.block.label), c);
        s.setShift (0);
        drawFileList (s, 9, 7, pg.fields[0].p.text());
        drawMsg (s, *msg, 18);
    };
    p->buttonHook = [msg, raw] (int b)
    {
        if (b == 1) return std::string ("Disk");
        auto& app = App::get();
        const std::string name = raw->fields[0].p.text();
        std::vector<uint8_t> bytes;
        if (name == "(none)" || ! app.files->read (name, bytes)) { *msg = { "No image to load", true }; return std::string(); }
        data::Block nb;
        const auto r = nb.parse (bytes.data(), bytes.size());
        if (r == data::Block::Result::BadSize) *msg = { "Not a 720K disk image", true };
        else if (r == data::Block::Result::NotS550) *msg = { "Not an S-550 or S-50 disk", true };
        else
        {
            app.newSet (nb); app.disk = nb; app.hasDisk = true;
            int used = 0; for (auto& t : nb.tones) if (t.used()) ++used;
            *msg = { "Loaded  " + trimEnd (nb.label) + "  " + std::to_string (used) + " Tones", false };
        }
        return std::string();
    };
    return p;
}

void drawDrive (Screen& s, int row)
{
    auto& d = *App::get().drive;
    s.setShift (4);
    CellOpts c; c.fg = Cyan;
    const char* st[] = { "No drive", "No disk", "Ready", "Busy" };
    s.cell (row, 15.6875, 14, d.name(), c);
    CellOpts w; w.fg = d.state() == FloppyDrive::State::Ready ? White : Red;
    s.cell (row, 26.6875, 10, st[(int) d.state()], w);
    s.setShift (0);
}

void drawTrackBar (Screen& s, int row, const FloppyDrive::Job& j, bool running)
{
    s.setShift (4);
    CellOpts y; y.fg = Yellow; s.cell (row, 6.6875, 8, "Track", y);
    CellOpts w; w.align = Align::Right;
    s.cell (row, 15.6875, 6, std::to_string (std::min (j.track, 159) / 2) + "/" + std::to_string (std::min (j.track, 159) % 2), w);
    CellOpts c; c.fg = Cyan;
    s.cell (row, 22.6875, 12, running ? (j.verifying ? "Verify" : (j.write ? "Write" : "Read")) : "", c);
    s.setShift (0);
    const int x = 16 * 6 + 8, y0 = (row + 1) * kRowH + 8 + 4;
    s.raw (x - 2, y0 - 2, 480 + 4, 14, Ink);
    s.raw (x, y0, 480, 10, White);
    s.raw (x, y0, 3 * std::min (160, j.track), 10, Cyan);
}

std::unique_ptr<LivePage> ddRead()
{
    auto p = std::make_unique<LivePage>();
    p->id = "DdRead"; p->title = "DISK-FD Read";
    label (p->before, 4, 6.6875, 8, "Drive");
    label (p->before, 5, 6.6875, 8, "Format");
    label (p->before, 6, 6.6875, 8, "File");
    label (p->before, 6, 24.6875, 4, ".IMG");
    Field f; f.label = "File"; f.p = mk ("File", firstFreeNumber(), 1, 99, imgNames()); f.row = 6; f.col = 15.6875; f.w = 9; f.align = Align::Left;
    p->fields.push_back (std::move (f));
    auto msg = std::make_shared<Msg>();
    auto handled = std::make_shared<bool> (true);
    LivePage* raw = p.get();
    p->refresh = [msg, handled, raw] (LivePage&)
    {
        auto& d = *App::get().drive;
        const auto& j = d.job();
        if (! *handled && j.finished && ! j.write)
        {
            *handled = true;
            if (! j.ok) *msg = { "Read failed: " + j.error, true };
            else
            {
                const std::string name = raw->fields[0].p.text();
                if (App::get().files->dialogs())
                {
                    *msg = { "Waiting for the Finder ...", false };
                    App::get().files->saveAs (name, j.data, [msg] (bool ok, const std::string& t)
                    {
                        *msg = ok ? Msg { "Read OK  saved " + t, false } : Msg { t == "Cancelled" ? "Read OK  not saved" : "Could not save: " + t, t != "Cancelled" };
                        App::get().uiDirty = true;
                    });
                }
                else if (App::get().files->write (name, j.data))
                {
                    data::Block t;
                    const bool s550 = t.parse (j.data.data(), j.data.size()) == data::Block::Result::Ok;
                    *msg = { std::string (s550 ? "Read OK  " : "Read OK, no S-550 disk  ") + name + ".IMG", ! s550 };
                }
                else *msg = { "Could not write " + name + ".IMG", true };
            }
        }
    };
    p->extras = [msg] (Screen& s, const LivePage&)
    {
        auto& d = *App::get().drive;
        drawDrive (s, 4);
        CellOpts w; s.setShift (4); s.cell (5, 15.6875, 14, "DS/DD 720KB", w); s.setShift (0);
        drawTrackBar (s, 8, d.job(), d.state() == FloppyDrive::State::Busy);
        drawMsg (s, *msg, 12);
        if (d.state() == FloppyDrive::State::Busy) { CellOpts r; r.fg = Red; s.setShift (4); s.cell (14, 6.6875, 30, "Press exit to stop", r); s.setShift (0); }
    };
    p->changeHook = [] (int) { return App::get().drive->state() == FloppyDrive::State::Busy ? 0 : -1; };
    p->clickHook = [] (int, int, int) { return App::get().drive->state() == FloppyDrive::State::Busy ? 0 : -1; };
    p->buttonHook = [msg, handled] (int b)
    {
        auto& d = *App::get().drive;
        if (b == 1)
        {
            if (d.state() == FloppyDrive::State::Busy) { d.abort(); *msg = { "Stopped", true }; return std::string(); }
            return std::string ("Disk");
        }
        if (d.state() == FloppyDrive::State::NoDrive) { *msg = { "No USB floppy drive found", true }; return std::string(); }
        if (d.state() == FloppyDrive::State::NoDisk) { *msg = { "Insert a floppy disk", true }; return std::string(); }
        if (d.state() == FloppyDrive::State::Busy) return std::string();
        *handled = false; *msg = { "Reading ...", false };
        d.startRead();
        return std::string();
    };
    return p;
}

std::unique_ptr<LivePage> ddWrite()
{
    auto p = std::make_unique<LivePage>();
    p->id = "DdWrite"; p->title = "DISK-FD Write";
    label (p->before, 4, 6.6875, 8, "Drive");
    label (p->before, 5, 6.6875, 8, "Format");
    label (p->before, 6, 6.6875, 8, "Image");
    label (p->before, 6, 24.6875, 4, ".IMG");
    label (p->before, 7, 6.6875, 8, "Verify");
    Field f; f.label = "Image"; f.p = mk ("Image", 0, 0, 0, { "(none)" }); f.row = 6; f.col = 15.6875; f.w = 9; f.align = Align::Left;
    p->fields.push_back (std::move (f));
    Field v; v.label = "Verify"; v.p = mk ("Verify", 1, 0, 1, kOnOff); v.row = 7; v.col = 15.6875; v.w = 4; v.align = Align::Left;
    p->fields.push_back (std::move (v));
    auto img = std::make_shared<std::vector<uint8_t>> ();     // the image chosen in the Finder (Mac); the folder list is only used without dialogs
    auto imgName = std::make_shared<std::string>();
    p->refresh = [img, imgName] (LivePage& pg)
    {
        auto l = App::get().files->dialogs() ? std::vector<std::string> { img->empty() ? std::string() : *imgName } : App::get().files->list();
        if (l.size() == 1 && l[0].empty()) l.clear();
        Param& q = pg.fields[0].p;
        q.names = l.empty() ? Names { "(none)" } : l;
        q.max = (int) q.names.size() - 1; q.value = std::max (0, std::min (q.value, q.max));
    };
    auto msg = std::make_shared<Msg>();
    auto confirm = std::make_shared<int> (-1);          // -1 off, else popup selection (0 Execute, 1 Cancel)
    auto wasBusy = std::make_shared<bool> (false);
    LivePage* raw = p.get();
    p->extras = [msg, confirm, wasBusy] (Screen& s, const LivePage&)
    {
        auto& d = *App::get().drive;
        drawDrive (s, 4);
        CellOpts w; s.setShift (4); s.cell (5, 15.6875, 14, "DS/DD 720KB", w); s.setShift (0);
        const bool busy = d.state() == FloppyDrive::State::Busy;
        if (*wasBusy && ! busy && d.job().finished && d.job().write)
        {
            *msg = d.job().ok ? Msg { "Disk written and verified", false } : Msg { "Write failed: " + d.job().error, true };
            *wasBusy = false;
        }
        drawTrackBar (s, 9, d.job(), busy);
        drawMsg (s, *msg, 13);
        CellOpts r; r.fg = Red; s.setShift (4);
        if (busy) s.cell (15, 6.6875, 30, "Press exit to stop", r);
        s.setShift (0);
        if (*confirm >= 0)
        {
            jsPopup (s, 12, 12, 12, { "Execute", "Cancel" }, *confirm);
            s.setShift (0);
            CellOpts q; q.fg = Red; s.cell (17, 6.6875, 33, "All data on the floppy is erased!", q);
        }
    };
    p->changeHook = [confirm] (int d)
    {
        if (App::get().drive->state() == FloppyDrive::State::Busy) return 0;
        if (*confirm >= 0) { const int v = std::max (0, std::min (1, *confirm + d)); const bool ch = v != *confirm; *confirm = v; return ch ? 1 : 0; }
        return -1;
    };
    p->menuOpen = [confirm] { return *confirm >= 0 && App::get().drive->state() != FloppyDrive::State::Busy; };
    p->clickHook = [confirm] (int x, int y, int)
    {
        if (App::get().drive->state() == FloppyDrive::State::Busy) return 0;
        if (*confirm >= 0)
        {
            const int i = (y - (12 * kRowH - 1)) / kRowH;
            if (x >= 168 && x < 376 && y >= 12 * kRowH - 1 && i >= 0 && i < 2) { *confirm = i; return 2; }
            return 0;
        }
        return -1;
    };
    p->buttonHook = [msg, confirm, wasBusy, raw, img, imgName] (int b)
    {
        auto& d = *App::get().drive;
        if (b == 1)
        {
            if (d.state() == FloppyDrive::State::Busy) { d.abort(); *msg = { "Stopped", true }; return std::string(); }
            if (*confirm >= 0) { *confirm = -1; return std::string(); }
            return std::string ("Disk");
        }
        if (d.state() == FloppyDrive::State::Busy) return std::string();
        if (*confirm < 0)
        {
            if (d.state() == FloppyDrive::State::NoDrive) { *msg = { "No USB floppy drive found", true }; return std::string(); }
            if (d.state() == FloppyDrive::State::NoDisk) { *msg = { "Insert a floppy disk", true }; return std::string(); }
            std::vector<uint8_t> bytes;
            if (App::get().files->dialogs())
            {
                if (img->empty())
                {
                    *msg = { "Choose the image in the Finder ...", false };
                    App::get().files->pick ([msg, img, imgName] (bool ok, const std::string& name, std::vector<uint8_t>& b)
                    {
                        if (ok) { *img = b; *imgName = name; *msg = { "Image " + name + "  - press Sel to write", false }; }
                        else *msg = { name, name != "Cancelled" };
                        App::get().uiDirty = true;
                    });
                    return std::string();
                }
                bytes = *img;
            }
            else if (! App::get().files->read (raw->fields[0].p.text(), bytes)) { *msg = { "No image selected", true }; return std::string(); }
            data::Block t;
            if (t.parse (bytes.data(), bytes.size()) != data::Block::Result::Ok) { *msg = { "Not an S-550 or S-50 disk image", true }; return std::string(); }
            *confirm = 1; *msg = { "", false };
            return std::string();
        }
        if (*confirm == 1) { *confirm = -1; return std::string(); }
        *confirm = -1;
        std::vector<uint8_t> bytes;
        if (App::get().files->dialogs()) bytes = *img; else App::get().files->read (raw->fields[0].p.text(), bytes);
        img->clear();
        if (d.startWrite (bytes, raw->fields[1].p.value == 1)) { *wasBusy = true; *msg = { "Writing ...", false }; }
        return std::string();
    };
    return p;
}

// Dir Patch / Dir Tone: names of the loaded block
// Label Set: two blocks with four lines of text each (local text, not stored on the disk yet)
std::unique_ptr<LivePage> labelSet()
{
    auto p = std::make_unique<LivePage>();
    p->id = "LabelSet"; p->title = "DISK-Set Label";
    p->tableDx = -1;                                      // photo: the table is a character away from the right edge
    p->btnNames = { "", "", " ", " " };
    LivePage* self = p.get();
    p->refresh = [self] (LivePage& pg) { pg.btnNames = pg.editing() ? std::vector<std::string> { "", "", "Char.", "----" } : std::vector<std::string> { "", "", " ", " " }; (void) self; };
    label (p->before, 5, 3, 8, "Block \002");
    label (p->before, 14, 3, 8, "Block \001");
    for (int blk = 0; blk < 2; ++blk)
    {
        TextBlock tb; tb.row = blk ? 14 : 5; tb.col = 10.7; tb.cols = 12; tb.rows = 5;
        tb.get = [blk] ()
        {
            std::string s;
            for (int i = 0; i < 5; ++i) { std::string l = App::get().labelText[(size_t) (blk * 5 + i)]; l.resize (12, ' '); s += l; }
            return s;
        };
        tb.set = [blk] (const std::string& t)
        {
            for (int i = 0; i < 5; ++i) App::get().labelText[(size_t) (blk * 5 + i)] = trimSp (t.substr ((size_t) (i * 12), 12));
        };
        p->blocks.push_back (std::move (tb));
    }
    return p;
}

std::unique_ptr<LivePage> dirPatch()
{
    auto p = std::make_unique<LivePage>();
    p->id = "DirPatch"; p->title = "DISK-Dir Patch";
    p->extras = [] (Screen& s, const LivePage&)
    {
        auto& b = App::get().hasDisk ? App::get().disk : App::get().block;
        s.setShift (4);
        for (int i = 0; i < 16; ++i)
        {
            CellOpts a; s.cell (4 + i, 6.6875, 4, std::string ("I") + std::to_string (i / 8 + 1) + std::to_string (i % 8 + 1), a);
            CellOpts n; s.cell (4 + i, 10.6875, 12, trimEnd (b.patches[i].name()), n);
        }
        s.setShift (0);
    };
    return p;
}

std::unique_ptr<LivePage> dirTone()
{
    auto p = std::make_unique<LivePage>();
    p->id = "DirTone"; p->title = "DISK-Dir Tone";
    p->extras = [] (Screen& s, const LivePage&)
    {
        auto& b = App::get().hasDisk ? App::get().disk : App::get().block;
        s.setShift (4);
        for (int i = 0; i < 16; ++i)
        {
            CellOpts a;
            s.cell (4 + i, 3, 4, std::string ("I") + std::to_string (1 + i / 8) + std::to_string (i % 8 + 1), a);
            s.cell (4 + i, 7.6875, 10, trimEnd (b.tones[i].name), a);
            s.cell (4 + i, 23, 4, std::string ("I") + std::to_string (3 + i / 8) + std::to_string (i % 8 + 1), a);
            s.cell (4 + i, 26.6875, 10, trimEnd (b.tones[16 + i].name), a);
        }
        s.setShift (0);
    };
    return p;
}
}

// Progress window of IMG / FD / HD Load and Save (same position everywhere, device photos): message + counter in the header,
// the white info box flush with the top of the command window, "source  arrow  destination" below it
constexpr int kXferPopX = 336, kXferPopRow = 4;
struct XferInfo { std::string head, label, src, tail; std::vector<std::string> lines;      // lines: the 5 lines of the disk label (HD Save), otherwise label alone
    int count = 0; bool arrowLeft = false; double t = 0; };
void drawXfer (Screen& s, const XferInfo& x)
{
    CellOpts w; w.fg = White; w.align = Align::Left;
    s.cell (2, 4.5, 12, x.head, w);
    CellOpts r = w; r.align = Align::Right; s.cell (2, 16.9, 3, std::to_string (x.count), r);
    const int top = kXferPopRow * kRowH - 1;                    // flush with the command window
    s.raw (55 + 5, top + 5, 222, 90, Black); s.raw (55, top, 222, 90, White);
    CellOpts l; l.fg = Ink; l.align = Align::Left;
    if (x.lines.empty()) s.cell (kXferPopRow, 5.0, 12, x.label, l);
    else for (size_t i = 0; i < x.lines.size() && i < 5; ++i) s.cell (kXferPopRow + (int) i, 5.0, 12, x.lines[i], l);
    s.setShift (4);
    CellOpts c; c.fg = Cyan; c.align = Align::Left;
    s.cell (9, 5.3, 4, x.src, c);
    const double ax = 5.3 + s.textWidth (x.src) / 16.0 + 0.1;
    if (((int) (x.t * 2.5)) % 2 == 0)                           // blinking arrow, centred on the text line
        for (int i = 0; i < 6; ++i) { const int h = x.arrowLeft ? 1 + 2 * i : 11 - 2 * i; s.raw ((int) ((ax - 1) * 16) + 7 + i, 9 * kRowH + 8 - h / 2, 1, h, Cyan); }
    s.cell (9, ax + 1.0, 2, x.tail, c);
    s.setShift (0);
}

// IMG Load / FD Load (same flow, as on the real machine): COM opens the command window, a click (or EXE) starts "Now Loading", then "Complete".
// IMG: the disk comes from the Finder, the loading is simulated (5 s).  FD: the disk is read from the real USB floppy drive; the counter follows the tracks.
struct ImgLd { int phase = 0; int sel = 1; std::chrono::steady_clock::time_point t0; int cmd = 1; int lastCount = 64; int stage = 0; };   // phase: 0 idle, 1 menu, 2 loading, 3 complete, 4 "Change Next Disk" (Load Chain, stage 1 = second disk)

std::unique_ptr<LivePage> imgLoad (bool fd)
{
    auto p = std::make_unique<LivePage>();
    p->id = fd ? "FdLoad" : "ImgLoad"; p->title = fd ? "DISK-FD Load" : "DISK-IMG Load";
    static const Names cmdsImg = { "IMG Load Chain", "IMG Load Set\002", "IMG Load Set\001", "IMG Load Block\002", "IMG Load Block\001", "IMG Load Func\002" };
    static const Names cmdsFd  = { "FD Load Chain", "FD Load Set\002", "FD Load Set\001", "FD Load Block\002", "FD Load Block\001", "FD Load Func\002" };
    const Names* cmdp = fd ? &cmdsFd : &cmdsImg;
    const std::string kind = fd ? "FD" : "IMG";
    static const char* const dst[] = { "\002", "\002", "\001", "\002", "\001", "\002", "\002" };
    auto st = std::make_shared<ImgLd>();
    if (fd) st->phase = 1;                                   // FD Load: the command window is open at once
    auto elapsed = [st] { return std::chrono::duration<double> (std::chrono::steady_clock::now() - st->t0).count(); };
    auto msg = std::make_shared<std::string>();
    auto finish = [st, msg] (bool ok, const std::string& err)
    {
        auto& a = App::get();
        st->phase = 3;
        if (! ok) { *msg = err; st->stage = 0; return; }
        if (st->cmd == 0 && st->stage == 0)                                           // Load Chain: Block I is in, the next disk goes into Block II
        {
            a.newSet (a.disk); st->stage = 1; st->phase = 4; *msg = "Change Next Disk";
            a.pickDone = false; a.pickImage (false, "Select next image"); return;                  // the Finder opens at once
        }
        if (st->cmd == 0) { a.newSet2 (a.disk); st->stage = 0; }
        if (st->cmd == 1 || st->cmd == 3) a.newSet (a.disk);
        else if (st->cmd == 2 || st->cmd == 4) a.newSet2 (a.disk);                    // Set II / Block II
        *msg = err.empty() ? std::string ("Complete") : "Complete (" + err + ")";
    };
    p->refresh = [=] (LivePage& pg)
    {
        auto& a = App::get();
        if (st->phase == 2)
        {
            a.uiDirty = true;                               // the counter and the arrow keep moving
            int cnt = 0;
            if (fd)
            {
                auto& d = *a.drive;
                const auto& j = d.job();
                cnt = std::max (0, (int) std::ceil (64.0 * (1.0 - j.track / 160.0)));
                if (d.state() != FloppyDrive::State::Busy && j.finished && ! j.write)
                {
                    data::Block nb;
                    if (! j.ok) finish (false, "Read failed\n" + j.error);
                    else if (nb.parse (j.data.data(), j.data.size()) != data::Block::Result::Ok) finish (false, "Not an S-550 or S-50 disk");
                    else { a.disk = nb; a.hasDisk = true; a.imageName = "FD"; finish (true, j.error); }
                }
            }
            else
            {
                cnt = std::max (0, (int) std::ceil (64.0 * (1.0 - elapsed() / 1.0)));      // an image loads in 1 s
                if (elapsed() >= 1.0) finish (true, "");
            }
            while (st->phase == 2 && st->lastCount > cnt) { --st->lastCount; a.floppyClicks += 1; }          // one click per counter step
            if (st->phase != 2) a.floppyClicks += 1;
        }
        if (st->phase == 4 && a.pickDone && ! fd)                                      // Chain: the next image was picked, it is loaded into Block II
        { a.pickDone = false; st->t0 = std::chrono::steady_clock::now(); st->phase = 2; st->lastCount = 64; msg->clear(); a.floppyClicks += 2; a.uiDirty = true; }
        else if (st->phase != 2 && a.pickDone && ! fd) { a.pickDone = false; st->phase = 1; msg->clear(); }
        pg.comRed = st->phase >= 1;
        const std::vector<std::string> n =
            st->phase == 1 || st->phase == 4 ? std::vector<std::string> { " ", "", "EXE", "exit" } :
            st->phase == 2 ? std::vector<std::string> { " ", "", "----", fd ? "exit" : "----" } :
            st->phase == 3 ? std::vector<std::string> { " ", "", fd ? "----" : "Sel", "----" } : std::vector<std::string> { " ", "", fd ? "----" : "Sel", "exit" };
        pg.btnNames = n;
    };
    p->extras = [=] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        CellOpts w; w.fg = White;
        if (st->phase == 2)
        {
            int count = 0;
            double t = elapsed();
            if (fd) count = std::max (0, (int) std::ceil (64.0 * (1.0 - a.drive->job().track / 160.0)));
            else count = std::max (0, (int) std::ceil (64.0 * (1.0 - t / 1.0)));
            XferInfo xi; xi.head = "Now Loading"; xi.count = count; xi.t = t;
            xi.label = fd ? trimEnd (a.drive->job().label) : trimEnd (a.disk.label);
            xi.src = kind; xi.tail = (st->cmd == 0 && st->stage == 1) ? std::string ("\001") : std::string (dst[st->cmd]);
            drawXfer (s, xi);
            CellOpts y; y.fg = Yellow;
            const std::string sys = fd ? a.drive->job().sysLine : a.disk.systemLine();
            if (! sys.empty() || ! fd || ! a.drive->job().label.empty()) s.cell (13, 5.3, 30, sys.empty() ? std::string ("S-550 System   Ver. 1.00") : sys, y);
        }
        else if (! msg->empty() && st->phase != 1)
        {
            const auto nl = msg->find ('\n');
            s.cell (2, 4.5, 20, msg->substr (0, nl), w);
            if (nl != std::string::npos)                                    // details (the real error text) wrapped below
            {
                std::string rest = msg->substr (nl + 1);
                for (auto& ch : rest) if (ch < 32) ch = ' ';
                for (int row = 4; ! rest.empty() && row < 12; ++row)
                {
                    const std::string line = rest.substr (0, 36);
                    s.cell (row, 3.4, 37, line, w);
                    rest = rest.size() > 36 ? rest.substr (36) : std::string();
                }
            }
        }
        if (st->phase == 1 || st->phase == 2) s.popup (kXferPopX, kXferPopRow, *cmdp, st->sel);
    };
    p->menuOpen = [st] { return st->phase == 1 || st->phase == 4; };
    p->buttonsActive = [st, fd] { return st->phase != 2 || fd; };
    p->comHook = [st, msg] { if (st->phase == 2) return false; if (st->phase == 4) { st->phase = 3; st->stage = 0; *msg = "Complete"; return true; }     // COM stops the chain
                               st->phase = st->phase == 1 ? 0 : 1; msg->clear(); return true; };
    p->changeHook = [] (int) { return 0; };
    p->clickHook = [st, cmdp] (int x, int y, int) -> int
    {
        if (st->phase != 1) return 0;
        int tw = 0; Screen tmp;
        for (const auto& t : *cmdp) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (kXferPopX, kXferPopRow, (int) cmdp->size(), tw, rx, ry, rw, rh);
        const int top = kXferPopRow * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) cmdp->size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;                                           // a click starts the command at once (EXE is only the fallback)
    };
    p->buttonHook = [=] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->phase == 2)
        {
            if (fd && which == 1) { a.drive->abort(); *msg = "Stopped"; st->phase = 0; }
            return "";
        }
        if (st->phase == 4)                                          // "Change Next Disk": EXE opens the Finder for the next image, exit stops
        {
            if (which == 1) { st->phase = 3; st->stage = 0; *msg = "Complete"; return ""; }
            a.pickDone = false; a.pickImage (false, "Select next image"); return "";            // (Finder was cancelled: EXE opens it again)
        }
        if (st->phase == 1)
        {
            if (which == 1) { st->phase = 0; return ""; }
            if (st->sel == 0 && fd) { *msg = "Not available yet"; st->phase = 0; return ""; }
            if (st->sel == 0) st->stage = 0;
            if (fd)
            {
                using S = FloppyDrive::State;
                const auto ds = a.drive->state();
                if (ds == S::NoDrive) { *msg = "No Drive"; st->phase = 0; return ""; }
                if (ds == S::NoDisk) { *msg = "No Disk"; st->phase = 0; return ""; }
                if (! a.drive->startRead()) { *msg = "Drive busy"; st->phase = 0; return ""; }
            }
            else if (! a.hasDisk) { *msg = "No Disk"; st->phase = 0; return ""; }
            st->cmd = st->sel; st->t0 = std::chrono::steady_clock::now(); st->phase = 2; msg->clear();
            st->lastCount = 64; a.floppyClicks += 2;                       // disk "inserted"
            return "";
        }
        if (which == 1) return st->phase == 3 ? std::string() : std::string ("Disk");
        if (fd) return "";
        msg->clear(); st->phase = 0; a.pickDone = false; a.pickImage (false);      // Sel: another disk (Finder)
        return "";
    };
    return p;
}

// HD Load P / HD Load T (manual HD5-IF p.28-32): HD Area, HD P# / T#, Int P# / T#, Used Tone Only / Wave Bank;
// COM = command window, SUB = HD Area (slot grid) / HD # / Internal
struct HdPT { int menu = 0, sel = 0, big = 0, pick = 0; bool nt = false, times = false; };      // big: 1 = HD # / 2 = Internal in the big view

std::unique_ptr<LivePage> hdLoadPT (bool tone)
{
    auto p = std::make_unique<LivePage>();
    p->id = tone ? "HdLoadT" : "HdLoadP"; p->title = tone ? "DISK-HD Load T" : "DISK-HD Load P";
    Names slotIds; for (int i = 0; i < data::kHdSlots; ++i) slotIds.push_back (data::HdImage::slotId (i));
    const Names ids = tone ? toneIds() : patchIds();
    const int n = tone ? data::kNumTones : data::kNumPatches;
    auto add = [&] (const char* lab, int row, Param q)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = 10; f.row = row; f.col = 20.25; f.w = 3; f.align = Align::Left; f.p = std::move (q);
        p->fields.push_back (std::move (f));
    };
    add ("HD Area", 4, mk ("HD Area", App::get().hdArea, 0, data::kHdSlots - 1, slotIds));
    add (tone ? "HD T#" : "HD P#", 5, mk ("HD #", 0, 0, n - 1, ids));
    add (tone ? "Int T#" : "Int P# (Del #)", 6, mk ("Int #", 0, 0, n - 1, ids));
    if (tone) { Field f; f.label = "Wave Bank"; f.lcol = 6.6875; f.lw = 10; f.row = 7; f.col = 20.25; f.w = 2; f.align = Align::Left; f.p = mk ("Wave Bank", 0, 0, 1, { "A", "B" }); p->fields.push_back (std::move (f)); }
    else add ("Used Tone Only", 7, mk ("Used", 0, 0, 1, kOnOff));
    auto st = std::make_shared<HdPT>();
    LivePage* self = p.get();
    static const Names subItems = { "HD Area", "HD #", "Internal" };
    const Names comItems = tone ? Names { "HD Load Tone" } : Names { "HD Load P.PRM", "HD Load T.List", "HD Load Patch", "*Delete Patch" };
    p->refresh = [st] (LivePage& pg)
    {
        auto& a = App::get();
        if (a.hdSync) { pg.fields[0].p.value = a.hdArea; a.hdSync = false; }
        a.hdArea = pg.fields[0].p.value;
        pg.subRed = st->menu == 1 || st->big; pg.comRed = st->menu == 2 && ! st->big;
        pg.btnNames = st->menu == 1 || st->big ? std::vector<std::string> { "", "", "Sel", "exit" } : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> {};
    };
    p->extras = [st, tone, comItems] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        const data::Block* hb = a.hasHd ? a.hdBlockOf (pg.fields[0].p.value) : nullptr;
        s.setShift (4);
        CellOpts c; c.fg = Cyan;
        const int ai = pg.fields[0].p.value, hi = pg.fields[1].p.value, ii = pg.fields[2].p.value;
        const std::string lab = hb ? trimEnd (a.hd.slots[ai].blockLabel.empty() ? a.hd.slots[ai].name : a.hd.slots[ai].blockLabel) : std::string (a.hasHd ? "(empty)" : "");
        s.cell (4, 24.25, 20, lab, c);
        if (tone)
        {
            if (hb && hb->tones[hi].used())
            {
                s.cell (5, 24.25, 10, trimEnd (hb->tones[hi].name), c);
                s.cell (5, 32.9, 6, data::toneInfo (hb->tones[hi]), c);
            }
            const auto& it = a.block.tones[ii];
            if (it.used()) { s.cell (6, 24.25, 10, trimEnd (it.name), c); s.cell (6, 32.9, 6, data::toneInfo (it), c); }
            remTime (s, 9);
        }
        else
        {
            if (hb) s.cell (5, 24.25, 14, trimEnd (hb->patches[hi].name()), c);
            s.cell (6, 24.25, 14, trimEnd (a.block.patches[ii].name()), c);
        }
        s.setShift (0);
        if (! a.fileMsg.empty()) drawMsg (s, Msg { a.fileMsg, a.fileMsgErr }, tone ? 17 : 10);
        if (st->big)      // SUB: the big view (opens only when the entry of the SUB menu is clicked) of all places with the names (the pull-down stays over it), a click picks, Sel confirms
        {
            const int which = st->big ? st->big : st->sel;       // 1 = the hard disk block, 2 = the memory
            s.raw (0, 36, 360, 18, Black);
            s.raw (16, 54, 608, 386, Panel);
            s.forgetText();
            const Names ids2 = tone ? toneIds() : patchIds();
            CellOpts y; y.fg = Yellow; y.align = Align::Left;
            s.cell (2, 3, 12, which == 1 ? "HD #" : "Internal", y);
            s.cell (2, 13.4, 4, ids2[(size_t) (st->big ? st->pick : pg.fields[(size_t) which].p.value)], y);
            CellOpts cy; cy.fg = Cyan; cy.align = Align::Left;
            if (st->big)
            {
                for (int r = 0; r < 9; ++r) s.raw (268, 38 + r, (r <= 4 ? r + 1 : 9 - r) * 2, 1, Cyan);
                s.cell (2, 19.4, 4, ids2[(size_t) st->pick], cy);
            }
            if (tone) drawNT (s, st->nt);
            if (! tone)                                       // patches (photo): one column per block, 16 places each, the id left of the name, the red bar on the name
                for (int blk = 0; blk < (which == 1 ? 1 : 2); ++blk)
                    for (int k = 0; k < 16; ++k)
                    {
                        const int idx = blk * 16 + k;
                        CellOpts n; n.fg = Yellow; n.align = Align::Left;
                        s.cell (4 + k, blk ? 21.8 : 3.25, 4, ids2[(size_t) idx], n);
                        const data::Block* pb = which == 1 ? hb : &a.blk (idx / 16);
                        CellOpts o; o.align = Align::Left; o.fg = White;
                        if (st->big && idx == st->pick) { o.bg = Red; o.fg = White; }
                        s.cell (4 + k, blk ? 25.8 : 7.5, 12, pb ? trimSp (pb->patches[(size_t) (idx % 16)].name()) : std::string(), o);
                    }
            for (int blk = 0; blk < (! tone ? 0 : which == 1 ? 1 : 2); ++blk)
            {
                for (int g = 0; g < (tone ? 4 : 2); ++g)
                {
                    CellOpts h; h.fg = Yellow; h.align = Align::Center;
                    s.cell (blk ? 13 : 4, kTmC0 + g * (tone ? kTmPitch : kPatchPitch), kTmW, std::string (blk ? "\001" : "\002") + std::to_string (g + 1), h);
                }
                for (int k = 0; k < 8; ++k)
                {
                    CellOpts n; n.fg = Yellow; n.align = Align::Left;
                    s.cell (tmRow (blk, k), 3.25, 1, std::to_string (k + 1), n);
                    for (int g = 0; g < (tone ? 4 : 2); ++g)
                    {
                        CellOpts o; o.align = Align::Left; o.fg = White;
                        std::string txt; int idx;
                        if (tone)
                        {
                            idx = blk * 32 + g * 8 + k;
                            const data::Block* tb = which == 1 ? hb : &a.blk (idx / 32);
                            const ToneCell tc = toneCell (tb, idx, st->times);
                            txt = tc.text; o.fg = tc.col;
                        }
                        else
                        {
                            idx = blk * 16 + g * 8 + k;
                            const data::Block* pb = which == 1 ? hb : &a.blk (idx / 16);
                            if (pb) txt = trimSp (pb->patches[(size_t) (idx % 16)].name());
                        }
                        if (st->big && idx == st->pick) { o.bg = Red; o.fg = White; }
                        s.cell (tmRow (blk, k), kTmC0 + g * (tone ? kTmPitch : kPatchPitch), tone ? kTmW : 12.5, txt, o);
                    }
                }
            }
        }
        if (st->menu == 1) s.popup (212, 4, subItems, st->sel);
        if (st->menu == 2) s.popup (336, 4, comItems, st->sel);
    };
    p->menuOpen = [st] { return st->menu != 0 || st->big; };
    p->buttonsActive = p->menuOpen;
    p->subHook = [st] { if (st->big) return false; st->menu = st->menu == 1 ? 0 : 1; st->sel = 0; App::get().fileMsg.clear(); return true; };
    p->comHook = [st] { if (st->big) return false; st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; App::get().fileMsg.clear(); return true; };
    p->clickHook = [st, comItems, tone] (int x, int y, int d) -> int
    {
        if (tone && st->big)        // [N/T]: red under the mouse, a click switches names <-> wave data
        {
            const bool was = st->nt;
            st->nt = ntHit (x, y);
            if (st->nt) { if (d != 0) st->times = ! st->times; return 1; }
            if (was) return 1;
        }
        if (st->big)
        {
            int idx;
            if (tone) { if (! tmHit (x, y, idx)) return 0; }
            else
            {
                const int r = y / kRowH;
                if (r < 4 || r > 19) return 0;
                const bool right = x >= 21.8 * 16 - 8;
                if (right ? (x >= 38 * 16) : (x < 3 * 16 || x >= 20 * 16)) return 0;
                idx = (right ? 16 : 0) + (r - 4);
            }
            if (st->big == 1 && idx >= (tone ? 32 : 16)) return 0;
            st->pick = idx; return 2;
        }
        if (! st->menu) return -1;
        const Names& it = st->menu == 1 ? subItems : comItems;
        int tw = 0; Screen tmp;
        for (const auto& t : it) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (st->menu == 1 ? 212 : 336, 4, (int) it.size(), tw, rx, ry, rw, rh);
        const int top = 4 * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) it.size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st, self, tone] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->big)                                                   // the big view: a click picked a place, Sel takes it, exit leaves
        {
            const int was = st->big;
            if (which == 0 && st->pick < (tone ? 32 : 16) * (was == 1 ? 1 : 2)) self->fields[(size_t) was].p.value = st->pick;
            st->big = 0; st->menu = which == 0 ? 1 : 0; st->sel = was % 2 + 1;      // Sel: the next line is asked right away
            return "";
        }
        if (! st->menu) { self->change (which == 0 ? -1 : +1); return ""; }
        const int m = st->menu, sel = st->sel;
        if (m == 1 && which == 0 && sel >= 1) { st->menu = 0; st->big = sel; st->pick = self->fields[(size_t) sel].p.value; self->selected = sel; return ""; }
        st->menu = 0;
        if (which == 1) return "";
        const int ai = self->fields[0].p.value, hi = self->fields[1].p.value, ii = self->fields[2].p.value;
        a.fileMsgErr = false;
        if (m == 1)                                           // SUB: the slot grid shows the disk labels
        {
            a.hdTarget = self->id; a.hdSlot = ai; a.hdOpenGrid = true;
            return "";
        }
        const data::Block* hb = a.hasHd ? a.hdBlockOf (ai) : nullptr;
        if (! hb && ! (! tone && sel == 3)) { a.fileMsg = "No block in this area"; a.fileMsgErr = true; return ""; }
        std::string err;
        if (tone) err = data::loadTone (*hb, hi, a.block, ii, self->fields[3].p.value);
        else if (sel == 0) { a.block.patches[ii] = hb->patches[hi]; }
        else if (sel == 1) { a.fileMsg = "Tone List display not available yet"; a.fileMsgErr = true; return ""; }
        else if (sel == 2) { std::vector<int> nl; err = data::loadPatch (*hb, hi, a.block, ii, &nl); }
        else data::deletePatch (a.block, ii);
        a.linked = true;
        a.fileMsg = err.empty() ? "Complete" : err; a.fileMsgErr = ! err.empty();
        return "";
    };
    return p;
}

// HD Load (manual HD5-IF p.27): I > HD Area, II > HD Area, Select Chain; COM opens the command window,
// SUB the pull-down of the area lines (then the slot grid with the disk labels)
struct HdMenu { int menu = 0, sel = 0; long long until = 0, t0 = 0; std::string pending; bool saving = false, verify = false; std::function<std::string()> run; int slot = 0, tail = 0; std::string boxLabel, boxLabel2; std::vector<std::string> boxLines, boxLines2; bool chain = false; int slot2 = 0; };       // menu: 1 = SUB, 2 = COM; chain: the second area follows halfway

int& hdSlotRef (App& a, bool save, int which) { return save ? (which ? a.hdSaveII : a.hdSaveI) : (which ? a.hdSlotII : a.hdSlotI); }

std::unique_ptr<LivePage> hdAreaPage (bool save)
{
    auto p = std::make_unique<LivePage>();
    p->id = save ? "HdSave" : "HdSet"; p->title = save ? "DISK-HD Save" : "DISK-HD Load";
    static const Names loadCmds = { "HD Load Chain", "HD Load Set\002", "HD Load Set\001", "HD Load Block\002", "HD Load Block\001", "HD Load Func\002", "HD Load MIDI\002" };
    static const Names saveCmds = { "HD Save Chain", "HD Save Set\002", "HD Save Set\001", "HD Save Block\002", "HD Save Block\001", "HD Save Func\002", "HD Save MIDI\002" };
    const Names& cmds = save ? saveCmds : loadCmds;
    static const Names subs = { "HD Area 1", "HD Area 2" };
    Names ids; for (int i = 0; i < data::kHdSlots; ++i) ids.push_back (data::HdImage::slotId (i));
    auto addArea = [&] (const char* lab, int row, int v)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = 11; f.row = row; f.col = 20.6875; f.w = 3; f.align = Align::Left; f.fg = Yellow;
        f.p = mk (lab, v, 0, data::kHdSlots - 1, ids); p->fields.push_back (std::move (f));
    };
    const char* ar = save ? "\003" : "\004";
    static std::string l1, l2; l1 = std::string ("I ") + ar + " HD Area"; l2 = std::string ("\001 ") + ar + " HD Area";
    addArea (l1.c_str(), 4, hdSlotRef (App::get(), save, 0)); addArea (l2.c_str(), 5, hdSlotRef (App::get(), save, 1));
    { Field f; f.label = "Select Chain"; f.lcol = 6.6875; f.lw = 12; f.row = 6; f.col = 20.6875; f.w = 3; f.align = Align::Left; f.fg = Yellow;
      f.p = mk ("Select Chain", 0, 0, 1, kOnOff); p->fields.push_back (std::move (f)); }
    if (save) { Field f; f.label = "Wave Verify"; f.lcol = 6.6875; f.lw = 12; f.row = 7; f.col = 20.6875; f.w = 3; f.align = Align::Right; f.fg = Yellow;
      f.p = mk ("Wave Verify", App::get().hdVerify, 0, 1, kOnOff); p->fields.push_back (std::move (f)); }
    auto st = std::make_shared<HdMenu>();
    LivePage* self = p.get();
    p->refresh = [st, save] (LivePage& pg)
    {
        auto& a = App::get();
        if (st->until)
        {
            if (nowMs() >= st->until)                       // simulated load / write time is over: the work is done now
            {
                st->until = 0; st->menu = 0;
                const std::string r = st->run ? st->run() : std::string();
                st->run = nullptr;
                a.fileMsg = r.empty() ? "Complete" : r; a.fileMsgErr = ! r.empty();
                a.floppyClicks += 1;
            }
            else a.uiDirty = true;
        }
        if (a.hdSync)
        {
            if (pg.fields[2].p.value == 1)                  // Select Chain On: the other area follows the chosen one
            { if (a.hdWhich == 0) hdSlotRef (a, save, 1) = std::min (data::kHdSlots - 1, hdSlotRef (a, save, 0) + 1); else hdSlotRef (a, save, 0) = std::max (0, hdSlotRef (a, save, 1) - 1); }
            pg.fields[0].p.value = hdSlotRef (a, save, 0); pg.fields[1].p.value = hdSlotRef (a, save, 1); a.hdSync = false;
        }
        pg.subRed = st->menu == 1; pg.comRed = st->menu == 2;
        pg.btnNames = st->until ? std::vector<std::string> { "", "", "----", "----" } : st->menu == 1 ? std::vector<std::string> { "", "", "Sel", "exit" } : st->menu == 2 ? std::vector<std::string> { "", "", "EXE", "exit" } : std::vector<std::string> {};
    };
    p->postChange = [save] (LivePage& pg)
    {
        auto& a = App::get();
        const int ni = pg.fields[0].p.value, nii = pg.fields[1].p.value;
        if (pg.fields[2].p.value == 1)                      // Select Chain On: the other area follows
        {
            if (ni != hdSlotRef (a, save, 0)) pg.fields[1].p.value = std::min (data::kHdSlots - 1, ni + 1);
            else if (nii != hdSlotRef (a, save, 1)) pg.fields[0].p.value = std::max (0, nii - 1);
        }
        hdSlotRef (a, save, 0) = pg.fields[0].p.value; hdSlotRef (a, save, 1) = pg.fields[1].p.value;
    };
    p->extras = [st, save, cmds] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        s.setShift (4);
        CellOpts c; c.fg = Cyan;
        for (int k = 0; k < 2; ++k)
        {
            const auto& d = a.hd.slots[k ? hdSlotRef (a, save, 1) : hdSlotRef (a, save, 0)];
            s.cell (4 + k, 24.6875, 12, d.valid ? trimEnd (trimEnd (d.blockLabel).empty() ? d.name : d.blockLabel) : std::string(), c);
        }
        CellOpts y; y.fg = Yellow; if (! st->until) s.cell (9, 6.6875, 40, a.hdName + ".IMG", y);
        s.setShift (0);
        if (st->menu == 1)                                  // SUB: the line chosen in the pull-down is repeated under the buttons
        {
            const int k = st->sel;
            s.cell (2, 2.6875, 10, k ? "HD Area 2" : "HD Area 1", y);
            s.cell (2, 13.6875, 3, data::HdImage::slotId (k ? hdSlotRef (a, save, 1) : hdSlotRef (a, save, 0)), y);
        }
        if (st->until)                                      // "Now Loading / Now Saving / Now Verify" with the counting counter (photos)
        {
            const double fr = std::min (1.0, (double) (nowMs() - st->t0) / (double) (st->until - st->t0));
            const bool vf = st->saving && st->verify;
            const int nseg = st->chain ? (vf ? 4 : 2) : (vf ? 2 : 1);          // chain: area I, then area II (halfway the display changes to II); verify: the same again
            const int seg = std::min (nseg - 1, (int) (fr * nseg));
            const double part = std::max (0.0, std::min (1.0, fr * nseg - seg));
            const bool ver = vf && seg >= nseg / 2;
            const int area = st->chain ? seg % 2 : st->tail;
            const int count = std::max (0, (int) std::ceil (64.0 * (1.0 - part)));
            XferInfo xi; xi.head = st->saving ? (ver ? "Now Verify" : "Now Saving") : "Now Loading"; xi.count = count;
            xi.label = area ? st->boxLabel2 : st->boxLabel; xi.lines = area ? st->boxLines2 : st->boxLines; xi.src = data::HdImage::slotId (area ? st->slot2 : st->slot); xi.tail = area ? "\001" : "I";
            xi.arrowLeft = st->saving; xi.t = (double) (nowMs() - st->t0) / 1000.0;
            drawXfer (s, xi);
        }
        else if (! a.fileMsg.empty()) { CellOpts w; w.fg = a.fileMsgErr ? Red : White; w.align = Align::Left; s.cell (2, 4.5, 30, a.fileMsg, w); }
        if (st->menu == 1) s.popup (212, 4, subs, st->sel);
        if (st->menu == 2) s.popup (336, 4, cmds, st->sel);
    };
    p->menuOpen = [st] { return st->menu != 0; };
    p->buttonsActive = p->menuOpen;
    p->comHook = [st] { if (st->until) return false; st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; App::get().fileMsg.clear(); return true; };
    p->subHook = [st, self] { if (st->until) return false; st->menu = st->menu == 1 ? 0 : 1; st->sel = self->selected == 1 ? 1 : 0; App::get().fileMsg.clear(); return true; };
    p->clickHook = [st, cmdsP = &cmds] (int x, int y, int) -> int
    {
        if (st->until) return 0;
        if (! st->menu) return -1;
        const Names& it = st->menu == 1 ? subs : *cmdsP;
        int tw = 0; Screen tmp;
        for (const auto& t : it) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (st->menu == 1 ? 212 : 336, 4, (int) it.size(), tw, rx, ry, rw, rh);
        const int top = 4 * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) it.size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st, self, save, cmds] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->until) return "";
        if (! st->menu) { self->change (which == 0 ? -1 : +1); return ""; }
        const int m = st->menu, c = st->sel;
        st->menu = 0;
        if (which == 1) return "";
        a.fileMsgErr = false;
        if (m == 1) { a.hdWhich = c; a.hdSlot = c ? hdSlotRef (a, save, 1) : hdSlotRef (a, save, 0); a.hdTarget = save ? "HdSave" : "HdSet"; a.hdOpenGrid = true; return ""; }
        if (c >= 5) { a.fileMsg = std::string (cmds[(size_t) c]) + ": not available yet"; a.fileMsgErr = true; return ""; }
        // which areas / blocks the command touches: Chain = I and II, Set / Block I = I, Set / Block II = II
        const bool one1 = c == 1 || c == 3, one2 = c == 2 || c == 4;
        const int firstWhich = one2 ? 1 : 0;
        const int slot1 = hdSlotRef (a, save, firstWhich);
        if (! a.hasHd) { a.fileMsg = "HD Connection Error"; a.fileMsgErr = true; return ""; }
        auto check = [&] (int slot) { return save ? (a.hd.slots[slot].sectors == (uint32_t) data::kHdBlockSectors ? std::string() : std::string ("HD Area not formatted"))
                                                  : (a.hd.slots[slot].valid ? std::string() : std::string ("Empty slot")); };
        std::string pre = check (slot1);
        if (pre.empty() && c == 0) pre = check (hdSlotRef (a, save, 1));
        if (! pre.empty()) { a.fileMsg = pre; a.fileMsgErr = true; return ""; }
        if (save) a.hdVerify = self->fields[3].p.value;
        st->saving = save; st->verify = save && a.hdVerify == 1; st->slot = slot1; st->tail = firstWhich;
        st->boxLabel = save ? trimEnd ((firstWhich ? a.block2 : a.block).label) : trimEnd (a.hd.slots[slot1].blockLabel.empty() ? a.hd.slots[slot1].name : a.hd.slots[slot1].blockLabel);
        (void) one1;
        auto lab5 = [&a] (int which, const std::string& first)
        {
            std::vector<std::string> v; bool any = false;
            for (int i = 0; i < 5; ++i) { v.push_back (trimEnd (a.labelText[(size_t) (which * 5 + i)])); any = any || ! v.back().empty(); }
            if (! any) v[0] = first;
            return v;
        };
        if (save) { st->boxLines = lab5 (firstWhich, st->boxLabel); st->boxLines2 = lab5 (1, trimEnd (a.block2.label)); } else { st->boxLines.clear(); st->boxLines2.clear(); }
        st->chain = c == 0; st->slot2 = hdSlotRef (a, save, 1);
        st->boxLabel2 = save ? trimEnd (a.block2.label) : trimEnd (a.hd.slots[st->slot2].blockLabel.empty() ? a.hd.slots[st->slot2].name : a.hd.slots[st->slot2].blockLabel);
        st->run = [save, c, slot1, verify = st->verify] () -> std::string
        {
            auto& a2 = App::get();
            std::string err;
            if (save)
            {
                auto put = [&] (int slot, const data::Block& b) { return a2.hdSaveSlot (slot, b, err, c != 3 && c != 4) && (! verify || a2.hdVerifySlot (slot, b, err)); };      // Wave Verify: read the file again and compare
                if (c == 0) { if (! put (hdSlotRef (a2, true, 0), a2.block)) return err; if (! put (hdSlotRef (a2, true, 1), a2.block2)) return err; }
                else if (! put (slot1, c == 2 || c == 4 ? a2.block2 : a2.block)) return err;
                return "";
            }
            if (c == 0)
            {
                a2.hdSlot = a2.hdSlotI; if (! a2.hdLoadSlot()) return a2.fileMsg; a2.newSet (a2.disk);
                a2.hdSlot = a2.hdSlotII; if (! a2.hdLoadSlot()) return a2.fileMsg; a2.newSet2 (a2.disk);
                return "";
            }
            a2.hdSlot = slot1; if (! a2.hdLoadSlot()) return a2.fileMsg;
            if (c == 2 || c == 4) a2.newSet2 (a2.disk); else a2.newSet (a2.disk);
            return "";
        };
        st->menu = 2; st->t0 = nowMs(); st->until = st->t0 + 5000;      // simulated load / write time: 5 s, the command window stays open
        a.floppyClicks += 2;
        return "";
    };
    return p;
}

// DISK-Setup (photos of the CD-5 SYS V.1.00 screen): S/Load areas, S/Load(P.CHG), S/Controller,
// Hard Disk ID, CD-5 ID (both tied to the SCSI devices of the Options menu), *HD Capacity, system line.
// COM: SCSI Check, HD Save SYS, FD Save SYS, S-550 MIDI Link (no HD Format: new partitions come from the Options menu)
std::unique_ptr<LivePage> diskSetup()
{
    auto p = std::make_unique<LivePage>();
    p->id = "DiskSetup"; p->title = "DISK-Setup";
    auto& a0 = App::get();
    static const Names cmds = { "SCSI Check", "HD Save SYS", "FD Save SYS", "S-550 MIDI Link" };
    Names areas { "Off" }; for (int i = 0; i < data::kHdSlots; ++i) areas.push_back (data::HdImage::slotId (i));
    Names chans { "Off" }; for (int i = 1; i <= 16; ++i) chans.push_back (std::to_string (i));
    static const Names ctl = { "Off", "MU1", "RC-100" };
    Names hdIds, cdIds { "Off" }; for (int i = 0; i < 7; ++i) { hdIds.push_back (std::to_string (i)); cdIds.push_back (std::to_string (i)); }
    auto add = [&] (const char* lab, double lw, int row, Param prm, Align al)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = lw; f.row = row; f.col = 20.6875; f.w = 3; f.align = al; f.fg = Yellow;
        f.p = std::move (prm); p->fields.push_back (std::move (f));
    };
    add ("S/Load I\004Area",  14, 4, mk ("S/Load I", a0.setupLoadI, 0, 64, areas), Align::Left);
    add ("S/Load \001\004Area", 14, 5, mk ("S/Load II", a0.setupLoadII, 0, 64, areas), Align::Left);
    add ("S/Load(P.CHG)", 13, 6, mk ("S/Load(P.CHG)", a0.setupPchg, 0, 16, chans), Align::Left);
    add ("S/Controller", 12, 7, mk ("S/Controller", a0.setupCtrl, 0, 2, ctl), Align::Left);
    add ("Hard Disk", 9, 8, mk ("Hard Disk ID", a0.hdActiveId >= 0 ? a0.hdActiveId : a0.hdSetupId, 0, 6, hdIds), Align::Right);
    add ("CD-5", 4, 9, mk ("CD-5 ID", a0.cdId + 1, 0, 7, cdIds), Align::Right);
    label (p->before, 6, 24.6875, 2, "ch");
    label (p->before, 8, 16.6875, 4, "ID =");
    label (p->before, 9, 16.6875, 4, "ID =");
    label (p->before, 11, 6.6875, 12, "*HD Capacity");
    label (p->before, 14, 5.9375, 32, "S-550 CD-5 SYS V.1.00 22 Nov/88");
    auto st = std::make_shared<HdMenu>();                   // menu: 2 = COM; until = end of "Working" (photos: COM stays open, buttons ----)
    LivePage* self = p.get();
    p->refresh = [st] (LivePage& pg)
    {
        auto& a = App::get();
        if (st->until)
        {
            if (nowMs() >= st->until) { st->until = 0; st->menu = 0; a.fileMsg = st->pending; a.fileMsgErr = st->pending != "Complete"; }
            else a.uiDirty = true;
        }
        if (a.hdActiveId >= 0) a.hdSetupId = a.hdActiveId;
        pg.fields[4].p.value = a.hdSetupId; pg.fields[5].p.value = a.cdId + 1;
        pg.comRed = st->menu == 2;
        pg.btnNames = st->until ? std::vector<std::string> { " ", "", "----", "----" } : st->menu == 2 ? std::vector<std::string> { " ", "", "EXE", "exit" } : std::vector<std::string> { " " };
    };
    p->postChange = [] (LivePage& pg)
    {
        auto& a = App::get();
        a.setupLoadI = pg.fields[0].p.value; a.setupLoadII = pg.fields[1].p.value; a.setupPchg = pg.fields[2].p.value; a.setupCtrl = pg.fields[3].p.value;
        a.fileMsg.clear(); a.fileMsgErr = false;
        const int hid = pg.fields[4].p.value, cid = pg.fields[5].p.value - 1;
        if (hid != a.hdSetupId)
        {
            if (hid == a.cdId) { a.fileMsg = "ID " + std::to_string (hid) + " is used by the CD-5"; a.fileMsgErr = true; }
            else
            {
                a.hdSetupId = hid;
                if (a.hdDev[hid].ok) a.hdActivate (hid);
                else { a.hasHd = false; a.hdActiveId = -1; a.fileMsg = "HD Connection Error"; a.fileMsgErr = true; }
            }
        }
        if (cid != a.cdId)
        {
            std::string err;
            if (cid == a.hdSetupId && cid >= 0) err = "ID " + std::to_string (cid) + " is used by the hard disk";
            else a.setCdId (cid, err);
            if (! err.empty()) { a.fileMsg = err; a.fileMsgErr = true; }
        }
    };
    p->extras = [st] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        s.setShift (4);
        CellOpts c; c.fg = Cyan;
        std::string cap = "--- --- Area --";
        if (a.hasHd)
        {
            const int ar = std::max (1, a.hd.areas);
            cap = std::to_string (ar * 5 / 4) + "M --- Area " + std::to_string (ar);
        }
        s.cell (11, 20.6875, 16, cap, c);
        s.setShift (0);
        if (st->until && st->sel != 0) { CellOpts w; w.fg = White; w.align = Align::Left; s.cell (2, 3.5, 8, "Working", w); }
        else if (! st->until && ! a.fileMsg.empty()) { CellOpts w; w.fg = a.fileMsgErr ? Red : White; w.align = Align::Left; s.cell (2, 3.5, 24, a.fileMsg, w); }
        if (st->menu == 2) s.popup (336, 4, cmds, st->sel);
        if (st->until && st->sel == 0)                      // SCSI Check box (photo): left ID 6-4, right ID 3-0, IDs 7 not listed
        {
            s.raw (32 + 4, 15 * kRowH + 5 + 4, 538, 5 * kRowH + 3, Black);
            s.raw (32, 15 * kRowH + 5, 538, 5 * kRowH + 3, White);
            s.setShift (4);
            CellOpts k; k.fg = Black; k.dy = 0; k.align = Align::Left; CellOpts r; r.fg = Red; r.dy = 0; r.align = Align::Left; CellOpts b; b.fg = Blue; b.dy = 0; b.align = Align::Left;
            s.cell (15, 4.0, 33, "* SCSI Device Connection Check *", k);
            s.cell (16, 5.3125, 9, "I/F check", k); s.cell (16, 14.9, 2, "OK", b);
            for (int id = 6; id >= 0; --id)
            {
                const bool left = id >= 4;
                const int row = left ? 17 + (6 - id) : 16 + (3 - id);
                const double c0 = left ? 5.3125 : 20.8125;
                const bool cd = id == a.cdId, hdv = a.hdDev[id].ok;
                s.cell (row, c0, 4, "ID=" + std::to_string (id), k);
                s.cell (row, left ? 11.9 : 25.3125, 9, cd ? "CD-5" : hdv ? "Hard Disk" : "N.C.", cd || hdv ? r : b);
            }
            s.setShift (0);
        }
    };
    p->menuOpen = [st] { return st->menu != 0; };
    p->buttonsActive = p->menuOpen;
    p->comHook = [st] { if (st->until) return false; st->menu = st->menu == 2 ? 0 : 2; st->sel = 0; App::get().fileMsg.clear(); return true; };
    p->clickHook = [st] (int x, int y, int) -> int
    {
        if (st->until) return 0;
        if (st->menu != 2) return -1;
        int tw = 0; Screen tmp;
        for (const auto& t : cmds) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (336, 4, (int) cmds.size(), tw, rx, ry, rw, rh);
        const int top = 4 * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) cmds.size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st, self] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->until) return "";
        if (! st->menu) { self->change (which == 0 ? -1 : +1); return ""; }
        if (which == 1) { st->menu = 0; return ""; }
        const int c = st->sel;
        a.fileMsgErr = false; a.fileMsg.clear();
        long long dur = 900;
        st->pending = "Complete";
        if (c == 0) dur = 2500;                             // SCSI Check
        else if (c == 3)                                    // opens the SysEx dialog (Options menu has the same entry)
        {
            dur = 100;
            if (a.openMidiLinkDialog) a.openMidiLinkDialog(); else st->pending = "Only in the standalone app";
        }
        else if (c == 1)                                    // HD Save SYS: the system held in memory (UTIL-SYS.Backup: HD Load SYS)
        {
            std::string e;
            dur = 1500;
            if (! a.hdSaveSys (a.hdActiveId, e)) { st->pending = e; dur = 10; }
        }
        else st->pending = std::string (cmds[(size_t) c]) + ": not available yet";
        st->until = nowMs() + dur;
        return "";
    };
    return p;
}



// ---------------------------------------------------------------- UTIL-SYS.Backup (photos of the CD-5 SYS V.1.00 screen, manual CD-5 p.45 / S-550 p.145)
// Startup settings as in DISK-Setup, a warning, COM: SYS.Backup.  Sequence: Now Loading, Change disk, Formatting, Now Saving, Complete.
struct SysBk
{
    int menu = 0, sel = 0;                      // menu: 2 = COM, 3 = list of the linked hard disk images (SYS Load HD / SYS Save HD)
    int pickId = 0, listFor = 2; std::vector<int> ids;
    int cmd = -1, step = 0;                     // running command and its step
    bool run = false, err = false, waitEnter = false, choosing = false;
    long long t0 = 0;
    std::string msg, name;
    std::vector<uint8_t> img;                   // the system disk held in the sampler's memory (raw 720K disk image)
    std::string sysLine;                        // its first system line
    std::string target;                         // place chosen in the Finder
    std::string pend;                           // result of an asynchronous dialog
    bool pendOk = false, pendDone = false;
    std::vector<uint8_t> pendData;
};
std::unique_ptr<LivePage> sysBackupPage()
{
    auto p = std::make_unique<LivePage>();
    p->id = "SysBackup"; p->title = "UTIL-SYS.Backup";
    auto& a0 = App::get();
    Names areas { "Off" }; for (int i = 0; i < data::kHdSlots; ++i) areas.push_back (data::HdImage::slotId (i));
    Names chans { "Off" }; for (int i = 1; i <= 16; ++i) chans.push_back (std::to_string (i));
    static const Names ctl = { "Off", "MU1", "RC-100" };
    // COM: the original command (system disk -> memory -> backup disk) and the single steps, so that a system can also come from / go to an image file
    static const Names cmds = { "SYS Load FD", "SYS Load IMG", "SYS Load HD", "SYS Save FD", "SYS Save IMG", "SYS Save HD" };
    auto add = [&] (const char* lab, double lw, int row, Param prm)
    {
        Field f; f.label = lab; f.lcol = 7.0; f.lw = lw; f.row = row; f.col = 20.3; f.w = 3; f.align = Align::Left; f.fg = Yellow;
        f.p = std::move (prm); p->fields.push_back (std::move (f));
    };
    add ("S/Load I\004Area",  14, 4, mk ("S/Load I", a0.setupLoadI, 0, 64, areas));
    add ("S/Load \001\004Area", 14, 5, mk ("S/Load II", a0.setupLoadII, 0, 64, areas));
    add ("S/Load(P.CHG)", 13, 6, mk ("S/Load(P.CHG)", a0.setupPchg, 0, 16, chans));
    add ("S/Controller", 12, 7, mk ("S/Controller", a0.setupCtrl, 0, 2, ctl));
    label (p->before, 6, 24.3, 2, "ch");
    auto st = std::make_shared<SysBk>();
    auto fail = [st] (const std::string& m) { st->run = false; st->waitEnter = false; st->err = true; st->msg = m; };
    auto setImg = [st, fail] (const std::vector<uint8_t>& d) -> bool
    {
        data::Block t;
        if (d.size() != 737280) { fail ("Not a 720K disk image"); return false; }
        if (t.parse (d.data(), d.size()) != data::Block::Result::Ok) { fail ("Not an S-550 disk"); return false; }
        st->img = d; st->sysLine = trimEnd (t.systemLine());
        return true;
    };
    // advances the running command; steps: see below
    auto nextStep = [st] { ++st->step; st->t0 = nowMs(); };
    p->refresh = [=] (LivePage& pg)
    {
        auto& a = App::get();
        if (st->run)
        {
            a.uiDirty = true;
            const int c = st->cmd;
            // step kinds per command: 0 = read FD, 1 = pick file, 2 = 1 s Now Loading, 3 = change disk (EXE), 4 = write FD, 5 = choose place, 6 = 1 s Now Saving + write file
            static const std::vector<std::vector<int>> plan = { { 0 }, { 1, 2 }, { 8 }, { 4 }, { 5, 6 }, { 7 } };
            const auto& pl = plan[(size_t) c];
            if (st->step >= (int) pl.size()) { st->run = false; st->msg = "Complete"; st->err = false; st->menu = 0; }
            else
            {
                const int k = pl[(size_t) st->step];
                auto& d = a.drive;
                if (k == 0 || k == 4)
                {
                    if (! d) { fail ("No Drive"); return; }
                    if (st->t0 == 0 || st->t0 == -1)
                    {
                        if (d->state() == FloppyDrive::State::NoDisk) { fail ("No Disk"); return; }
                        if (k == 0 ? ! d->startRead() : ! d->startWrite (st->img, false)) { fail ("Drive busy"); return; }
                        st->t0 = nowMs(); st->pendDone = true;                       // marks "started"
                        st->msg = k == 0 ? "Now Loading" : "Formatting";
                    }
                    else
                    {
                        const auto& jb = d->job();
                        if (k == 4) st->msg = jb.track < 4 ? "Formatting" : "Now Saving";
                        if (d->state() != FloppyDrive::State::Busy && jb.finished)
                        {
                            if (! jb.ok) { fail (jb.error.empty() ? std::string ("Drive error") : jb.error); return; }
                            if (k == 0 && ! setImg (jb.data)) return;
                            nextStep(); st->t0 = -1;
                        }
                    }
                }
                else if (k == 1)
                {
                    if (st->t0 == 0)
                    {
                        if (! a.files || ! a.files->dialogs()) { fail ("Not available"); return; }
                        st->t0 = nowMs(); st->choosing = true; st->pendDone = false; st->msg = "Waiting for the Finder ...";
                        a.files->pick ([st] (bool ok, const std::string& n, std::vector<uint8_t>& dd)
                                       { st->pendOk = ok; st->pend = n; st->pendData = dd; st->pendDone = true; st->choosing = false; App::get().uiDirty = true; }, "Open system disk image");
                    }
                    else if (st->pendDone)
                    {
                        if (! st->pendOk) { st->run = false; st->msg = st->pend == "Cancelled" ? std::string() : st->pend; st->err = ! st->msg.empty(); return; }
                        if (! setImg (st->pendData)) return;
                        st->name = st->pend; st->msg = "Now Loading"; nextStep();
                    }
                }
                else if (k == 7)                                        // HD Save SYS: the system held in memory -> active hard disk
                {
                    if (st->t0 == 0) { st->t0 = nowMs(); st->msg = "Saving System"; }
                    else if (nowMs() - st->t0 >= 1500)
                    {
                        std::string e;
                        if (! a.hdSaveSys (st->pickId, e)) { fail (e); return; }
                        nextStep(); st->t0 = 0;
                    }
                }
                else if (k == 8)                                        // HD Load SYS: from the linked image chosen in the list
                {
                    if (st->t0 == 0)
                    {
                        std::string e;
                        if (! a.hdLoadSys (st->pickId, e)) { fail (e); return; }
                        st->sysLine = a.hdSysLine; st->t0 = nowMs(); st->msg = "Now Loading";
                    }
                    else if (nowMs() - st->t0 >= 1000) { nextStep(); st->t0 = 0; }
                }
                else if (k == 2) { if (nowMs() - st->t0 >= 1000) { nextStep(); st->t0 = 0; } }
                else if (k == 3) { st->msg = "Change disk"; st->waitEnter = true; }
                else if (k == 5)
                {
                    if (st->t0 == 0)
                    {
                        if (st->img.empty()) { fail ("No System loaded"); return; }
                        if (! a.files || ! a.files->dialogs()) { st->target.clear(); nextStep(); st->msg = "Now Saving"; return; }
                        st->t0 = nowMs(); st->pendDone = false; st->msg = "Waiting for the Finder ...";
                        a.files->chooseSave (st->name.empty() ? std::string ("S550SYS") : st->name, [st] (bool ok, const std::string& t)
                                             { st->pendOk = ok; st->pend = t; st->pendDone = true; App::get().uiDirty = true; });
                    }
                    else if (st->pendDone)
                    {
                        if (! st->pendOk) { st->run = false; st->msg = st->pend == "Cancelled" ? std::string() : st->pend; st->err = ! st->msg.empty(); return; }
                        st->target = st->pend; st->msg = "Now Saving"; nextStep();
                    }
                }
                else if (k == 6)
                {
                    if (nowMs() - st->t0 >= 1000)
                    {
                        std::string e;
                        if (! st->target.empty() && ! a.files->writeChosen (st->img, e)) { fail ("Could not save: " + e); return; }
                        nextStep(); st->t0 = 0;
                    }
                }
            }
        }
        pg.fields[0].p.value = a.setupLoadI; pg.fields[1].p.value = a.setupLoadII; pg.fields[2].p.value = a.setupPchg; pg.fields[3].p.value = a.setupCtrl;
        pg.comRed = st->menu >= 2;
        pg.btnNames = st->waitEnter ? std::vector<std::string> { " ", "", "EXE", "exit" }
                    : st->run ? std::vector<std::string> { " ", "", "----", "----" } : st->menu >= 2 ? std::vector<std::string> { " ", "", "EXE", "exit" } : std::vector<std::string> { " ", "", "Sel", "----" };
    };
    p->postChange = [st] (LivePage& pg)
    {
        auto& a = App::get();
        a.setupLoadI = pg.fields[0].p.value; a.setupLoadII = pg.fields[1].p.value; a.setupPchg = pg.fields[2].p.value; a.setupCtrl = pg.fields[3].p.value;
        if (! st->run) st->msg.clear();
    };
    p->extras = [st] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        for (int i = 0; i < 2; ++i)                                         // area name of the hard disk next to the area
        {
            const int v = i ? a.setupLoadII : a.setupLoadI;
            if (v > 0 && a.hasHd && v - 1 < data::kHdSlots)
            {
                const auto& sl = a.hd.slots[(size_t) v - 1];
                s.cell (4 + i, 24.5, 15, trimEnd (sl.blockLabel.empty() ? sl.name : sl.blockLabel), c);
            }
        }
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        s.cell (10, 4.9, 34, st->sysLine.empty() ? std::string ("S-550 CD-5 SYS V.1.00 22 Nov/88") : st->sysLine, y);   // the system held in memory
        CellOpts r; r.fg = Red; r.align = Align::Left; s.cell (12, 9.6, 10, "WARNING!", r);
        CellOpts w; w.fg = White; w.align = Align::Left;
        w.align = Align::Center;                                             // centred on the screen
        s.cell (14, 1, 40, "If you don't save Sound Data", w);
        s.cell (15, 1, 40, "before executing 'SYS.Backup'", w);
        s.cell (17, 1, 40, "you'll lose the Sound Data.", w);
        if (! st->msg.empty()) { CellOpts m; m.fg = st->err ? Red : White; m.align = Align::Left; s.cell (2, 3.5, 28, st->msg, m); }
        if (st->menu == 2) s.popup2 (325, 4, cmds, st->sel, (int) cmds.size(), 14, true);
        if (st->menu == 3)
        {
            Names names; size_t mx = 0;
            for (int id : st->ids) { names.push_back ((std::to_string (id) + "  " + a.hdDev[id].name).substr (0, 16)); mx = std::max (mx, names.back().size()); }
            s.popup2 (325, 4, names, st->sel, (int) names.size(), (int) std::min<size_t> (18, mx + 1), true);
        }
    };
    p->menuOpen = [st] { return st->menu != 0; };
    p->buttonsActive = [] { return true; };
    p->comHook = [st] { if (st->run) return false; st->menu = st->menu != 0 ? 0 : 2; st->sel = 0; st->msg.clear(); st->err = false; return true; };
    p->clickHook = [st] (int x, int y, int) -> int
    {
        if (st->run) return 0;
        if (st->menu == 0) return -1;
        const int top = 4 * kRowH - 1;
        size_t mx = 0; for (int id : st->ids) mx = std::max (mx, (std::to_string (id) + "  " + App::get().hdDev[id].name).substr (0, 16).size());
        const int rx = 325 - 8, rw = (st->menu == 3 ? (int) std::min<size_t> (18, mx + 1) : 14) * 16 + 16;
        const int n = st->menu == 3 ? (int) st->ids.size() : (int) cmds.size();
        if (x < rx || x >= rx + rw || y < top || y >= top + n * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->waitEnter) { if (which == 0) { st->waitEnter = false; st->msg.clear(); ++st->step; st->t0 = -1; } return ""; }
        if (st->run) return "";
        if (st->menu == 0) { if (which == 0) { st->menu = 2; st->sel = 0; } return ""; }
        if (which == 1) { if (st->menu == 3) { st->menu = 2; st->sel = st->listFor; } else st->menu = 0; return ""; }
        st->err = false; st->msg.clear();
        if (st->menu == 3)                                                   // an image was chosen: SYS Load HD (source) / SYS Save HD (target)
        {
            if (st->sel < 0 || st->sel >= (int) st->ids.size()) return "";
            st->pickId = st->ids[(size_t) st->sel];
            st->cmd = st->listFor; st->step = 0; st->t0 = 0; st->pendDone = false; st->run = true;
            return "";
        }
        if (st->sel == 2 || st->sel == 5)                                    // first choose among the linked images
        {
            if (st->sel == 5 && a.hdSysMem.empty()) { st->err = true; st->msg = "No System loaded"; return ""; }
            st->ids.clear();
            for (int i = 0; i < 7; ++i) if (a.hdDev[i].ok) st->ids.push_back (i);
            if (st->ids.empty()) { st->err = true; st->msg = "No HD image linked"; return ""; }
            st->listFor = st->sel; st->menu = 3; st->sel = 0;
            return "";
        }
        st->cmd = st->sel; st->step = 0; st->t0 = 0; st->pendDone = false;
        if ((st->sel == 3 || st->sel == 4) && st->img.empty()) { st->err = true; st->msg = "No System loaded"; return ""; }
        st->run = true;
        return "";
    };
    return p;
}

// ---------------------------------------------------------------- DISK-MIDI Load / Save: the real S-550 on the SysEx link as a disk drive
struct MidiX { int phase = 1; int sel = 0; long long t0 = 0; std::string msg; };       // phase: 0 idle, 1 command window, 2 running, 3 finished

std::unique_ptr<LivePage> midiXferPage (bool save)
{
    auto p = std::make_unique<LivePage>();
    p->id = save ? "MidiSave" : "MidiLoad"; p->title = save ? "DISK-MIDI Save" : "DISK-MIDI Load";
    static const Names loadCmds = { "MIDI Load Set\002", "MIDI Load Set\001", "MIDI Load Tone\002", "MIDI Load Tone\001", "MIDI Load Func", "MIDI Load MIDI" };
    static const Names saveCmds = { "MIDI Save Set\002", "MIDI Save Set\001", "MIDI Save Tone\002", "MIDI Save Tone\001", "MIDI Save Func", "MIDI Save MIDI" };
    const Names* cmds = save ? &saveCmds : &loadCmds;
    static const char* const tails[] = { "\002", "\001", "\002", "\001", "\002", "\002" };
    auto st = std::make_shared<MidiX>();
    p->popupButtons = true;
    p->refresh = [st] (LivePage& pg)
    {
        auto& a = App::get();
        if (st->phase == 2)
        {
            a.uiDirty = true;
            const int xs = a.xferState.load();
            if (xs == 2) { st->phase = 3; st->msg = "Complete"; }
            else if (xs == 3) { st->phase = 3; st->msg = a.xferErr.empty() ? "MIDI error" : a.xferErr; }
        }
        pg.comRed = st->phase >= 1;
        pg.btnNames = st->phase == 1 ? std::vector<std::string> { " ", "", "EXE", "exit" } :
                      st->phase == 2 ? std::vector<std::string> { " ", "", "----", "----" } :
                                       std::vector<std::string> { " ", "", "Sel", "exit" };
    };
    p->extras = [st, cmds, save] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        if (st->phase == 2)
        {
            const double fr = std::max (0.0, std::min (1.0, a.xferFrac.load()));
            const double el = (double) (nowMs() - st->t0) / 1000.0;
            XferInfo xi; xi.head = save ? "Now Saving" : "Now Loading"; xi.count = std::max (0, (int) std::ceil (64.0 * (1.0 - fr))); xi.t = el;
            xi.label = save ? trimEnd ((st->sel & 1 ? a.block2 : a.block).label) : "S-550";
            xi.src = "MIDI"; xi.tail = tails[st->sel]; xi.arrowLeft = save;
            drawXfer (s, xi);
            CellOpts w; w.fg = White; w.align = Align::Left;
            const int rem = fr > 0.02 ? (int) (el * (1.0 - fr) / fr + 0.5) : -1;
            char buf[40];
            if (rem >= 0) std::snprintf (buf, sizeof buf, "%d%%  left %d:%02d", (int) (fr * 100), rem / 60, rem % 60); else std::snprintf (buf, sizeof buf, "%d%%", (int) (fr * 100));
            s.cell (12, 5.3, 24, buf, w);
        }
        else if (! st->msg.empty() && st->phase != 1)
        {
            CellOpts w; w.fg = st->msg == "Complete" ? White : Red; w.align = Align::Left;
            s.cell (2, 4.5, 34, st->msg, w);
        }
        if (st->phase == 1 || st->phase == 2) s.popup (kXferPopX, kXferPopRow, *cmds, st->sel);
    };
    p->menuOpen = [st] { return st->phase == 1; };
    p->buttonsActive = [st] { return st->phase != 2; };
    p->comHook = [st] { if (st->phase == 2) return false; st->phase = st->phase == 1 ? 0 : 1; st->msg.clear(); return true; };
    p->changeHook = [] (int) { return 0; };
    p->clickHook = [st, cmds] (int x, int y, int) -> int
    {
        if (st->phase != 1) return 0;
        int tw = 0; Screen tmp;
        for (const auto& t : *cmds) tw = std::max (tw, tmp.textWidth (t));
        int rx, ry, rw, rh;
        Screen::popupRect (kXferPopX, kXferPopRow, (int) cmds->size(), tw, rx, ry, rw, rh);
        const int top = kXferPopRow * kRowH - 1;
        if (x < rx || x >= rx + rw || y < top || y >= top + (int) cmds->size() * kRowH) return 0;
        st->sel = (y - top) / kRowH;
        return 2;
    };
    p->buttonHook = [st, save] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->phase == 2) return "";
        if (st->phase == 1)
        {
            if (which == 1) { st->phase = 0; return ""; }
            std::string err;
            if (! a.midiXfer) { st->msg = "Only in the standalone app"; st->phase = 0; return ""; }
            if (! a.midiXfer (save, st->sel, err)) { st->msg = err; st->phase = 0; return ""; }
            st->phase = 2; st->t0 = nowMs(); st->msg.clear();
            return "";
        }
        if (which == 1) return "Disk";
        st->msg.clear(); st->phase = 1; return "";                    // Sel: the command window again
    };
    return p;
}

// ---------------------------------------------------------------- DISK-Load Wave: wav / aiff / mp3 ... file -> tone + wave
struct WaveLd
{
    int phase = 0; std::string msg; std::string info;       // phase: 0 idle, 1 command window, 2 waiting for the file dialog
    long long until = 0, start = 0;                          // "Now Loading" runs 1 s after the click
    bool have = false; std::string file; std::vector<float> mono; double rate = 0;
};

// Load Wave (layout like FD Load T): line 1 = Source (the file picked in the Finder at once when the page is entered), line 2 = Destination tone
// (an occupied slot shows its name and wave, an empty one only its number), Wave Bank, Rate, Normalize.  COM = red command bar, a click / EXE runs it.
std::unique_ptr<LivePage> loadWavePage()
{
    auto p = std::make_unique<LivePage>();
    p->id = "LoadWave"; p->title = "DISK-Load Wave";
    static const Names cmds = { "Load Wave" };
    static const Names banks = { "A", "B" }, rates = { "30 kHz", "15 kHz" };
    auto add = [&] (const char* lab, int row, Param prm, double w)
    {
        Field f; f.label = lab; f.lcol = 6.6875; f.lw = 14; f.row = row; f.col = 20.25; f.w = w; f.align = Align::Left;
        f.p = std::move (prm); p->fields.push_back (std::move (f));
    };
    add ("Int", 5, mk ("Int", std::max (0, App::get().curTone), 0, 63, toneIds()), 3);
    add ("Wave Bank", 6, mk ("Wave Bank", 0, 0, 1, banks), 2);
    add ("Rate", 7, mk ("Rate", 0, 0, 1, rates), 6);
    add ("Normalize", 8, mk ("Normalize", 1, 0, 1, kOnOff), 3);
    p->selected = 0;
    auto st = std::make_shared<WaveLd>();
    LivePage* self = p.get();
    auto pick = [st] (bool openCom)
    {
        auto& a = App::get();
        if (st->phase == 2) return;
        if (! a.files || ! a.files->dialogs()) { st->msg = "Not available here"; st->phase = 0; return; }
        st->phase = 2; st->msg.clear();
        a.files->pickAudio ([st] (bool ok, const std::string& name, std::vector<uint8_t>& bytes)
        {
            auto& ap = App::get();
            ap.uiDirty = true; st->phase = 0;                       // the file is only shown in line 1; destination first, then COM -> Load Wave
            if (! ok) { if (name != "Cancelled") st->msg = name; return; }
            std::vector<float> mono; double rate = 0; std::string err;
            if (! ap.files->decodeAudio (name, bytes, mono, rate, err)) { st->msg = err; return; }
            st->mono = std::move (mono); st->rate = rate; st->file = name; st->have = true; ap.waveName = name; st->info.clear();
        });
    };
    p->onEnter = [pick] { pick (true); };
    p->refresh = [st] (LivePage& pg)
    {
        if (st->until) { if (nowMs() >= st->until) { st->until = 0; st->msg = "Complete"; } else App::get().uiDirty = true; }
        pg.comRed = st->phase == 1;
        pg.btnNames = st->phase == 1 ? std::vector<std::string> { "", "", "EXE", "exit" } : (st->phase == 2 || st->until) ? std::vector<std::string> { "", "", "----", "----" } : std::vector<std::string> {};
        if (st->phase == 2) App::get().uiDirty = true;
    };
    p->extras = [st] (Screen& s, const LivePage& pg)
    {
        auto& a = App::get();
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        s.setShift (4);
        s.cell (4, 6.6875, 8, "File", y);
        s.cell (5, 10.94, 3, "T#", y);
        if (st->have && st->phase != 1)          // the command bar covers line 1 completely (nothing may peek out beside it)
        {
            std::string n = st->file.substr (0, st->file.rfind ('.') == std::string::npos ? st->file.size() : st->file.rfind ('.')); if (n.size() > 8) n = n.substr (0, 8);          // like a tone name: 8 characters
            s.cell (4, 24.25, 10, n, c);
            char b[24]; std::snprintf (b, sizeof b, "%.1f", st->mono.size() / std::max (1.0, st->rate));
            s.cell (4, 32.9 + s.textWidth ("A-") / 16.0, 4, b, c);      // the seconds stand exactly above the numbers of the wave info (A-0.8)
        }
        const int ti = pg.byLabel ("Int")->p.value;
        const auto& t = a.blk (ti / 32).tones[(size_t) (ti % 32)];
        if (t.used()) { s.cell (5, 24.25, 10, trimEnd (t.name), c); s.cell (5, 32.9, 7, toneInfoText (t), c); }   // empty slot: only the number
        s.setShift (0);
        remTime (s, 10);
        CellOpts w; w.fg = White; w.align = Align::Left;
        if (st->until)
        {
            s.cell (2, 3.5, 12, "Now Loading", w);
            const int cnt = (int) std::max (0LL, 64 - (nowMs() - st->start) * 64 / 1000);
            s.cell (2, 16.7, 4, std::to_string (cnt), w);
        }
        else if (st->phase == 2) s.cell (2, 3.5, 30, "Waiting for the Finder ...", w);
        else if (! st->msg.empty()) { w.fg = st->msg == "Complete" ? White : Red; s.cell (2, 3.5, 34, st->msg, w); }
        if (! st->info.empty()) { CellOpts i = c; s.setShift (4); s.cell (7, 29.0, 12, st->info, i); s.setShift (0); }
        if (st->phase == 1) tightPopup (s, 21.25, 4, 14, cmds, 0);
    };
    p->menuOpen = [st] { return st->phase == 1; };
    p->buttonsActive = p->menuOpen;
    p->subHook = [pick, st] { if (st->phase == 2 || st->until) return false; pick (true); return true; };            // SUB: pick another file
    p->comHook = [st] { if (st->phase == 2 || st->until) return false; st->phase = st->phase == 1 ? 0 : 1; st->msg.clear(); return true; };
    p->clickHook = [st] (int x, int y, int) -> int
    {
        if (st->phase != 1) return -1;
        const int x0 = (int) ((21.25 - 1) * 16 - 8), w = (int) (14 * 16 + 16), top = 4 * kRowH;
        if (x < x0 || x >= x0 + w || y < top || y >= top + kRowH) return 0;
        return 2;
    };
    p->buttonHook = [st, self] (int which) -> std::string
    {
        auto& a = App::get();
        if (st->phase == 2 || st->until) return "";
        if (st->phase != 1) { self->change (which == 0 ? -1 : +1); return ""; }
        st->phase = 0;
        if (which == 1) return "";
        if (! st->have) { st->msg = "No file"; return ""; }
        const int ti = self->byLabel ("Int")->p.value, bank = self->byLabel ("Wave Bank")->p.value;
        const bool khz15 = self->byLabel ("Rate")->p.value == 1, norm = self->byLabel ("Normalize")->p.value == 1;
        data::WaveImportOptions o;
        o.tone = ti % 32; o.bank = bank; o.khz15 = khz15; o.normalize = norm;
        const auto dot = st->file.rfind ('.');
        o.name = st->file.substr (0, dot == std::string::npos ? st->file.size() : dot);
        data::WaveImportResult r; std::string err;
        if (! data::importWave (a.blk (ti / 32), o, st->mono, st->rate, r, err)) { st->msg = err; return ""; }
        a.linked = true;
        char buf[64]; std::snprintf (buf, sizeof buf, "%s-%d.%d", bank ? "B" : "A", r.segStart + 1, r.segStart + r.segLen);
        st->info = buf; st->msg = r.truncated ? "Cut to bank size" : "";  st->start = nowMs(); st->until = st->start + 1000;
        return "";
    };
    return p;
}

// HD slot browser: 8 x 8 blocks of the hard disk image (ZIP / ZuluSCSI), Sel loads the slot
// grid of the real unit (photos): band H1-H4 on rows 5-12, band H5-H8 on rows 14-21, the block names in the cells
constexpr double kHdC0 = 4.75, kHdPitch = 8.7, kHdW = 8;
int hdGridRow (int slot) { return ((slot / 8) / 4 ? 14 : 5) + slot % 8; }
bool hdGridHit (int x, int y, int& slot)
{
    const int r = y / kRowH;
    int band, k;
    if (r >= 5 && r <= 12) { band = 0; k = r - 5; }
    else if (r >= 14 && r <= 21) { band = 1; k = r - 14; }
    else return false;
    const double cx = x / 16.0 + 1 - kHdC0;
    const int g = (int) std::floor (cx / kHdPitch);
    if (g < 0 || g > 3 || cx - g * kHdPitch >= kHdW) return false;
    slot = (band * 4 + g) * 8 + k;
    return true;
}

std::unique_ptr<LivePage> hdLoad()
{
    auto p = std::make_unique<LivePage>();
    p->id = "HdLoad"; p->title = "DISK-HD Load"; p->popupButtons = true;
    p->refresh = [] (LivePage& pg) { pg.title = App::get().hdTarget == "HdSave" ? "DISK-HD Save" : "DISK-HD Load"; };
    p->extras = [] (Screen& s, const LivePage&)
    {
        auto& a = App::get();
        s.setShift (0);
        if (a.hasHd)
        {
            s.raw (16, 54, 608, 386, Panel);
            CellOpts y; y.fg = Yellow; y.align = Align::Left; CellOpts c; c.fg = Cyan; c.align = Align::Left;
            const bool sv = a.hdTarget == "HdSave", two = a.hdTarget == "HdSet" || sv;
            const int cur = two ? hdSlotRef (a, sv, a.hdWhich) : a.hdArea;
            s.cell (2, 2.6875, 10, two ? (a.hdWhich ? "HD Area 2" : "HD Area 1") : "HD Area", y);
            s.cell (2, 13.6875, 3, data::HdImage::slotId (cur), y);
            s.cell (2, 17.1875, 1, "\003", c);
            s.cell (2, 19.1875, 3, data::HdImage::slotId (a.hdSlot), y);
            for (int band = 0; band < 2; ++band)
            {
                for (int g = 0; g < 4; ++g)
                {
                    CellOpts h; h.fg = Yellow; h.align = Align::Center;
                    s.cell (band ? 13 : 4, kHdC0 + g * kHdPitch, kHdW, "H" + std::to_string (band * 4 + g + 1), h);
                }
                for (int k = 0; k < 8; ++k)
                {
                    CellOpts n; n.fg = Yellow; n.align = Align::Left;
                    s.cell ((band ? 14 : 5) + k, 3.25, 1, std::to_string (k + 1), n);
                    for (int g = 0; g < 4; ++g)
                    {
                        const int i = (band * 4 + g) * 8 + k;
                        const auto& sl = a.hd.slots[i];
                        CellOpts o; o.align = Align::Left; o.fg = Cyan;
                        if (i == a.hdSlot) { o.bg = Red; o.fg = White; }
                        s.cell (hdGridRow (i), kHdC0 + g * kHdPitch, kHdW, sl.valid ? trimEnd (trimEnd (sl.blockLabel).empty() ? sl.name : sl.blockLabel).substr (0, 8) : std::string(), o);
                    }
                }
            }
        }
        else { CellOpts w; s.setShift (4); s.cell (6, 4.6875, 40, "Choose a hard disk image (Sel)", w); }
        s.setShift (0);
        if (! a.fileMsg.empty()) drawMsg (s, Msg { a.fileMsg, a.fileMsgErr }, 23);
    };
    p->changeHook = [] (int d)
    {
        auto& a = App::get();
        if (! a.hasHd) return 0;
        const int v = std::max (0, std::min (data::kHdSlots - 1, a.hdSlot + d));
        const bool ch = v != a.hdSlot; a.hdSlot = v; return ch ? 1 : 0;
    };
    p->clickHook = [] (int x, int y, int dir)
    {
        auto& a = App::get();
        int i;
        if (! a.hasHd || ! hdGridHit (x, y, i)) return 0;
        if (dir == 0) return 0;                          // only a click fixes the place: the red cursor does not follow the mouse
        a.hdSlot = i; if (a.hdTarget == "HdSet" || a.hdTarget == "HdSave") hdSlotRef (a, a.hdTarget == "HdSave", a.hdWhich) = a.hdSlot; else if (a.hdTarget != "LoadAll" && a.hdTarget != "LoadPRM" && a.hdTarget != "LoadTone") a.hdArea = a.hdSlot; a.hdSync = true; return 1;   // select only, Sel confirms
    };
    p->buttonHook = [] (int b)
    {
        auto& a = App::get();
        const bool back = a.hdTarget == "HdSet" || a.hdTarget == "HdSave" || a.hdTarget == "HdLoadP" || a.hdTarget == "HdLoadT";
        if (b == 1) return std::string (back && a.hasHd ? a.hdTarget : "Disk");
        if (! a.hasHd) { a.pickHd(); return std::string(); }
        if (back)
        {
            if (a.hdTarget == "HdSet" || a.hdTarget == "HdSave") hdSlotRef (a, a.hdTarget == "HdSave", a.hdWhich) = a.hdSlot; else a.hdArea = a.hdSlot;
            a.hdSync = true; return a.hdTarget;
        }
        if (! a.hdLoadSlot()) return std::string();
        if (a.hdTarget == "LoadAll")                      // HD Load: the block goes straight into memory
        {
            a.newSet (a.disk);
            a.fileMsg = "Loaded  " + a.imageName + "  Block I"; a.fileMsgErr = false;
            return std::string();
        }
        a.fileMsg.clear();
        return a.hdTarget;
    };
    return p;
}

// ---- SUB: the whole tone memory as one big view (UTIL pages that ask for a source or a destination)
struct ToneMapState { bool lit = false, open = false, boxHit = false, times = false, nt = false; int stage = 1, sel = 0; std::string field; int pick = 0; };   // stage -1: small menu over the page, 0: big view with the small menu, 1: big view, choose the place (red bar)

void addToneMap (LivePage& p)
{
    std::vector<std::string> labels;
    for (const auto& f : p.fields)
        if (f.p.max == 63 && f.p.names.size() == 64 && f.p.names[0] == "I11" && f.row >= 4 && f.w > 0) labels.push_back (f.label);
    if (labels.empty()) return;
    auto st = std::make_shared<ToneMapState>();
    LivePage* self = &p;
    Names titles;
    for (const auto& l : labels)                                  // "Source1" -> "Source 1"
        titles.push_back (p.id == "WaveLoop" && l == "Destination" ? "Destin" : l.size() > 1 && std::isdigit ((unsigned char) l.back()) && l[l.size() - 2] != ' ' ? l.substr (0, l.size() - 1) + " " + l.back() : l);
    auto open = [st, self, labels] (const std::string& lab)
    {
        const auto* f = self->byLabel (lab);
        if (! f) return;
        st->open = true; st->field = lab; st->pick = f->p.value;
        st->sel = 0;
        for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == lab) st->sel = (int) i;
        st->stage = labels.size() > 1 ? -1 : 1;
    };
    p.openToneMap = open;
    auto baseRef = p.refresh;
    p.refresh = [baseRef, st] (LivePage& pg)
    {
        if (baseRef) baseRef (pg);
        if (! st->open) { if (st->lit) { st->lit = false; pg.popupButtons = false; pg.subRed = false; if (pg.btnNames == std::vector<std::string> { "", "", "Sel", "exit" }) pg.btnNames.clear(); } return; }      // closed: the page gets its normal buttons back
        st->lit = true;
        pg.popupButtons = true; pg.subRed = true; pg.comRed = false;
        pg.btnNames = { "", "", "Sel", "exit" };
    };
    auto baseEx = p.extras;
    p.extras = [baseEx, st, labels, titles] (Screen& s, const LivePage& pg)
    {
        if (baseEx) baseEx (s, pg);
        if (! st->open) return;
        auto& a = App::get();
        s.raw (0, 36, 420, 18, Black);
        if (st->stage >= 0) s.raw (16, 54, 608, 386, Panel);                              // the big view always covers the page
        const Names ids = toneIds();
        CellOpts y; y.fg = Yellow; y.align = Align::Left;
        const int li = st->stage <= 0 ? st->sel : (int) (std::find (labels.begin(), labels.end(), st->field) - labels.begin());
        s.cell (2, 3, 12, titles[(size_t) std::min (li, (int) titles.size() - 1)], y);
        const auto* f = pg.byLabel (labels[(size_t) std::min (li, (int) labels.size() - 1)]);
        s.cell (2, 13.4, 4, f ? ids[(size_t) f->p.value] : "", y);
        CellOpts c; c.fg = Cyan; c.align = Align::Left;
        if (st->stage == 1)
        {
            for (int r = 0; r < 9; ++r) s.raw (268, 38 + r, (r <= 4 ? r + 1 : 9 - r) * 2, 1, Cyan);
            s.cell (2, 19.4, 4, ids[(size_t) st->pick], c);
        }
        if (st->stage >= 0) drawNT (s, st->nt);
        for (int blk = 0; blk < 2 && st->stage >= 0; ++blk)
        {
            for (int g = 0; g < 4; ++g)
            {
                CellOpts h; h.fg = Yellow; h.align = Align::Center;
                s.cell (blk ? 13 : 4, kTmC0 + g * kTmPitch, kTmW, std::string (blk ? "\001" : "I") + std::to_string (g + 1), h);
            }
            for (int k = 0; k < 8; ++k)
            {
                CellOpts n; n.fg = Yellow; n.align = Align::Left;
                s.cell (tmRow (blk, k), 3.25, 1, std::to_string (k + 1), n);
                for (int g = 0; g < 4; ++g)
                {
                    const int idx = blk * 32 + g * 8 + k;
                    const ToneCell tc = a.toneBound() ? toneCell (&a.blk (idx / 32), idx, st->times) : ToneCell();
                    CellOpts o; o.align = Align::Left; o.fg = tc.col;
                    if (st->stage == 1 && idx == st->pick) { o.bg = Red; o.fg = White; }
                    s.cell (tmRow (blk, k), kTmC0 + g * kTmPitch, kTmW, tc.text, o);
                }
            }
        }
        if (titles.size() > 1 && st->stage <= 0)                       // the small menu is asked before every place
        {
            s.eraseCut (225, 4 * kRowH - 8, 196, (int) titles.size() * kRowH + 20);
            jsPopup (s, 15.6, 4, 11, titles, st->sel);
        }
    };
    auto baseMenu = p.menuOpen;
    p.menuOpen = [baseMenu, st] { return st->open || (baseMenu && baseMenu()); };
    p.buttonsActive = p.menuOpen;
    auto baseCom = p.comHook;
    p.comHook = [baseCom, st] { if (st->open) return false; return baseCom ? baseCom() : false; };
    if (! p.subHook)
        p.subHook = [st, self, open, labels]
        {
            if (st->open) return false;
            std::string lab = labels.front();
            if (self->selected >= 0 && self->selected < (int) self->fields.size())
                for (const auto& l : labels) if (self->fields[(size_t) self->selected].label == l) lab = l;
            open (lab);
            return true;
        };
    auto baseClick = p.clickHook;
    const int nl = (int) labels.size();
    p.clickHook = [baseClick, st, nl, labels] (int x, int y, int d) -> int
    {
        if (! st->open) return baseClick ? baseClick (x, y, d) : -1;
        if (st->stage >= 0)                                            // [N/T]: red under the mouse, a click switches names <-> wave data
        {
            const bool was = st->nt;
            st->nt = ntHit (x, y);
            if (st->nt) { if (d != 0) st->times = ! st->times; return 1; }
            if (was) return 1;
        }
        const int top = 4 * kRowH;
        const bool inBox = nl > 1 && st->stage <= 0 && x >= 225 && x < 225 + 192 && y >= top && y < top + nl * kRowH;
        if (inBox) { st->boxHit = true; st->sel = (y - top) / kRowH; return 2; }
        st->boxHit = false;
        if (st->stage <= 0) return 0;
        for (int i = 0; i < nl; ++i) if (labels[(size_t) i] == st->field) st->sel = i;      // the box shows the field the places belong to
        int idx;
        if (! tmHit (x, y, idx)) return 0;
        st->pick = idx;
        return 2;
    };
    auto baseBtn = p.buttonHook;
    p.buttonHook = [baseBtn, st, self, labels] (int which) -> std::string
    {
        if (! st->open) return baseBtn ? baseBtn (which) : (self->change (which == 0 ? -1 : +1), std::string());
        const bool multi = labels.size() > 1;
        if (which == 1) { st->open = false; return ""; }          // exit / right click leave the whole menu
        if (st->stage <= 0)
        {
            st->field = labels[(size_t) st->sel];
            if (const auto* f = self->byLabel (st->field)) st->pick = f->p.value;
            st->stage = 1;
            return "";
        }
        if (which == 0 && st->boxHit)                          // a click in the small menu: another field, the big view stays
        {
            st->boxHit = false;
            st->field = labels[(size_t) st->sel];
            if (const auto* f = self->byLabel (st->field)) st->pick = f->p.value;
            return "";
        }
        if (which == 0) self->setValue (st->field, st->pick);
        if (multi) { st->stage = 0; if (which == 0) st->sel = (st->sel + 1) % (int) labels.size(); }   // the next field is asked right away
        else st->open = false;
        return "";
    };
}

std::unique_ptr<LivePage> makeLivePageRaw (const std::string& id)
{
    if (id == "ImgLoad") return imgLoad (false);
    if (id == "FdLoad") return imgLoad (true);
    if (id == "HdLoad") return hdLoad();
    if (id == "HdSet") return hdAreaPage (false);
    if (id == "HdSave") return hdAreaPage (true);
    if (id == "DiskSetup") return diskSetup();
    if (id == "MidiLoad") return midiXferPage (false);
    if (id == "MidiSave") return midiXferPage (true);
    if (id == "LoadWave") return loadWavePage();
    if (id == "HdLoadP") return hdLoadPT (false);
    if (id == "HdLoadT") return hdLoadPT (true);
    if (id == "ToneEdit") return toneEdit();
    if (id == "LabelSet") return labelSet();
    if (id == "Lfo") return lfo();
    if (id == "Loop") return loop();
    if (id == "Tvf") return tvf();
    if (id == "Tva") return tva();
    if (id == "Split") return split();
    if (id == "Master") return master();
    if (id == "Message") return message();
    if (id == "Monitor") return midiMonitor();
    if (id == "ProgNo") return progNo();
    if (id == "Disk") return diskMenu();
    if (id == "LoadImage") return App::get().files->dialogs() ? loadImageDialog() : loadImage();
    if (id == "DdRead") return ddRead();
    if (id == "DdWrite") return ddWrite();
    if (id == "DirPatch") return dirPatch();
    if (id == "DirTone") return dirTone();
    if (id == "LoadPRM") return fdLoadP();
    if (id == "SavePRM") return loadPrm (true);
    if (id == "LoadTone") return fdLoadPT (true);
    if (id == "LoadAll") return popupPage ("LoadAll", "DISK-Load", 11, 4, 9, { "Set \002", "Set \001", "Func", "Block \002", "Block \001" }, 0,
                                           [] (int i, Msg& m) { return diskLoad (i, m); }, "Disk");
    if (id == "SaveAll") return popupPage ("SaveAll", "DISK-Save", 11, 4, 9, { "Save Set \002", "Save Set \001", "Save BL \002", "Save BL \001", "Save Chain" }, 0,
                                           [] (int i, Msg& m) { return diskSave (i, m); }, "Disk");
    if (id == "Format") return popupPage ("Format", "DISK-Format", 13, 5, 12, { "Format", "Execute", "Cancel" }, 1,
                                          [] (int i, Msg& m) { return diskFormat (i, m); }, "Disk");
    if (id == "SysBackup") return sysBackupPage();
    if (id == "Backup") return backupPage();
    if (id == "FdSave") return backupPage (true);
    if (id == "SaveSys") return popupPage ("SaveSys", "DISK-Save SYS", 13, 5, 12, { "Save SYS", "Execute", "Cancel" }, 1,
                                           [] (int i, Msg& m) { return diskSystem (false, i, m); }, "Disk");
    if (id == "ChangeSys") return popupPage ("ChangeSys", "DISK-Change SYS", 13, 5, 12, { "Change SYS", "Execute", "Cancel" }, 1,
                                             [] (int i, Msg& m) { return diskSystem (true, i, m); }, "Disk");
    if (id == "ToneMap") return toneMap();
    if (id == "PatchMap") return patchMap();
    if (id == "Mix") return mix();
    if (id == "WaveDraw") return waveDraw();
    if (id == "WaveScope") return waveScope();
    if (id == "Sampling") return sampling();
    if (id == "Standard") return standard();
    if (id == "EightKey") return eightKey();
    if (id == "Main") return splitDisp();
    if (id == "Truncate") return truncate();
    if (id == "Combine") return combine();
    if (id == "DFilter") return dFilter();
    if (id == "WaveLoop") return waveLoop();
    if (id == "Delete") return deletePage();
    if (id == "CopyMove") return copyMove();
    if (id == "DispWave") return dispWave();
    return nullptr;
}

std::unique_ptr<LivePage> makeLivePage (const std::string& id)
{
    auto p = makeLivePageRaw (id);
    if (p && (id == "Sampling" || id == "Truncate" || id == "Mix" || id == "Combine" || id == "DFilter" || id == "WaveLoop" || id == "WaveDraw" || id == "Loop" || id == "WaveScope" )) addToneMap (*p);
    return p;
}
}

namespace s5
{
// pressed keys on the keyboard of the edit pages (mouse or MIDI, any channel): the same marks as on the PLAY keyboard
void drawEditHeld (Screen& s, int oct)
{
    auto& a = App::get();
    static const int white[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    s.setShift (0);
    {   // key maps of the patch on one line: a tick wherever the tone changes, 1st map below the line, 2nd map above it
        static const double pos[12] = { 0, 0.5, 1, 1.5, 2, 3, 3.5, 4, 4.5, 5, 5.5, 6 };
        auto xOf = [&] (int key) { key = std::max (36, std::min (96, key)); const int o = (key - 36) / 12, nn = (key - 36) % 12; return 45 + (int) ((o * 7 + pos[nn]) * 15); };
        const int base = 36 + 12 * oct;
        const auto& pt = a.patchAt (a.curPatch);
        for (int m = 0; m < 2; ++m)
            for (int key = base; key < base + 60; ++key)
            {
                const int off = (m ? 126 : 17) + (key - 12);
                if (key == base || pt.raw[off] != pt.raw[off - 1]) s.raw (xOf (key - 12 * oct), m ? 346 : 352, 1, 7, m ? Red : Blue);       // 2nd map red above the line, 1st map blue below it
            }
    }
    for (int n = 36 + 12 * oct; n <= 96 + 12 * oct; ++n)
    {
        bool on = false;
        for (int ch = 0; ch < 16 && ! on; ++ch) on = a.held[ch][n];
        if (! on) continue;
        const int r = (n - 36 - 12 * oct) % 12, w = (n - 36 - 12 * oct) / 12 * 7 + white[r];
        if (black[r]) s.raw (45 + (w + 1) * 15 - 4 + 2, 366 + 19, 5, 9, White);     // black key: white box
        else          s.raw (45 + w * 15 + 5, 366 + 34, 6, 11, Blue);               // white key: blue box low in the key
    }
}
}
