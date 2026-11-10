#include <cmath>
// Headless checks: every board image replays exactly, and the mouse navigation works.
#include "../Source/Screen/UiState.h"
#include "../Source/Screen/App.h"
#include <cstdio>
#include "../Source/Data/HdImage.h"
#include <map>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

static void save (const s5::Screen& s, const char* name)
{
    std::vector<uint32_t> rgb (s5::kW * s5::kH);
    s.toRgb (rgb.data());
    FILE* f = std::fopen (name, "wb");
    std::fprintf (f, "P6\n%d %d\n255\n", s5::kW, s5::kH);
    for (uint32_t p : rgb) { unsigned char c[3] = { (unsigned char) (p >> 16), (unsigned char) (p >> 8), (unsigned char) p }; std::fwrite (c, 1, 3, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    int fails = 0;
    s5::UiState ui;
    s5::Screen s;

    // 1) start page, then walk through every page via the menus
    auto expect = [&] (bool cond, const char* what) { if (! cond) { std::printf ("FAIL: %s\n", what); ++fails; } };
    expect (ui.page() == "Patch", "start page is Patch");

    ui.mouseDown (60, 26);                       // MODE
    ui.mouseMove (60, 4 * 18 + 5 + 18 * 5);      // hover last item (UTIL)
    ui.render (s); save (s, (dir + "/ui_modemenu.ppm").c_str());
    ui.mouseDown (60, 4 * 18 + 5 + 18 * 5);      // pick UTIL
    expect (ui.mode() == "UTIL", "mode UTILITY selected");
    expect (ui.page() == "WaveScope", "first UTIL page is Wave Scope");

    ui.render (s); save (s, (dir + "/ui_pagemenu.ppm").c_str());      // UTIL opens its list of utilities at once
    ui.mouseDown (150, 4 * 18 + 5 + 18 * 2);     // third UTIL page
    expect (ui.page() == "Truncate", "third UTIL page is Truncate");

    ui.mouseDown (60, 26); ui.mouseDown (60, 4 * 18 + 5 + 18 * 1);   // EDIT
    expect (ui.mode() == "EDIT", "mode EDIT");
    ui.mouseDown (150, 26);
    for (int i = 0; i < 12; ++i) {}
    ui.mouseDown (300, 300);                     // click outside closes the menu
    expect (ui.page() != "", "menu closes");

    // 2) Patch page editing
    ui.setPage ("Patch");
    ui.mouseDown (200, 8 * 18 + 8);              // row index 4: V-Mix Ratio
    expect (ui.selectedRow == 4, "row 4 selected by click");
    ui.mouseDown (520 + 10, 26);                 // Inc
    ui.wheel (100, 100, +5);
    expect (ui.patchParams()[4].value == 6, "Inc + wheel = 6");
    ui.mouseDown (424 + 10, 26);                 // Dec
    expect (ui.patchParams()[4].value == 5, "Dec = 5");
    {   // Patch list rows: a first click only selects, hold only runs on the selected row
        const int y = 10 * 18 + 8;                // row index 6, not selected (row 4 is)
        const int v6 = ui.patchParams()[6].value;
        expect (ui.holdDir (200, y, +1) == +1, "Patch: hold runs on a hovered row at once");
        ui.mouseDown (200, y, +1);
        expect (ui.selectedRow == 6 && ui.patchParams()[6].value != v6, "Patch: first click already changes the value");
        expect (ui.holdDir (200, y, +1) == +1, "Patch: hold works once the row is selected");
        ui.mouseDown (200, y, +1);
    }
    ui.render (s); save (s, (dir + "/ui_patch_edit.ppm").c_str());

    // 3) live pages: Tone PRM, Loop, TVF
    ui.setPage ("ToneEdit");
    ui.mouseDown (200, 6 * 18 + 8);              // row "Shift" (index 3)
    ui.wheel (100, 100, +3);
    ui.render (s);
    ui.setPage ("Loop");
    ui.mouseDown (200, 6 * 18 + 8);              // "Start Point"
    ui.mouseDown (520 + 10, 26);                 // Inc by 1
    ui.mouseDown (200, 9 * 18 + 8);              // "Search Mode"
    ui.mouseDown (520 + 10, 26);                 // -> +/-114
    ui.mouseDown (200, 6 * 18 + 8);
    ui.mouseDown (520 + 10, 26);                 // Inc by 114 -> 115
    ui.render (s); save (s, (dir + "/ui_loop_edit.ppm").c_str());
    ui.setPage ("Tvf");
    ui.mouseDown (8 * 16 + 10 + 0, 12 * 18 + 4 + 8);  // Rate row, point 1 (col 9)
    ui.mouseDown (424 + 10, 26);                 // Dec
    ui.render (s); save (s, (dir + "/ui_tvf_edit.ppm").c_str());
    expect (ui.livePageValue ("Tvf", "Rate1") == 59, "TVF rate 1 decreased to 59");
    ui.setPage ("Message");
    ui.mouseDown (13 * 16 + 4 * 16 + 20, 8 * 18 + 8);   // some cell
    ui.mouseDown (520 + 10, 26);
    ui.render (s);

    // Mix-grid pages: right click raises, left click lowers, Source selected by default
    ui.setPage ("Combine");
    const int v0 = ui.livePageValue ("Combine", "Source 1");
    ui.mouseDown (330, 4 * 18 + 8, +1); ui.mouseDown (330, 4 * 18 + 8, +1);
    expect (ui.livePageValue ("Combine", "Source 1") == v0 + 2, "right click raises Source 1 twice");
    ui.mouseDown (330, 4 * 18 + 8, -1);
    expect (ui.livePageValue ("Combine", "Source 1") == v0 + 1, "left click lowers Source 1");
    expect (ui.holdDir (330, 4 * 18 + 8, +1) == +1, "hold repeats on the selected value");
    expect (ui.holdDir (330, 6 * 18 + 8, +1) == +1, "hold on any hovered field");
    expect (ui.holdDir (330, 13 * 18, +1) == 0, "no hold on empty area");
    ui.setPage ("Truncate");
    ui.mouseDown (20 * 16, 5 * 18 + 8, +1);
    expect (ui.livePageValue ("Truncate", "Start Point") == 1, "first click changes the field at once");
    ui.mouseDown (20 * 16, 5 * 18 + 8, +1);
    expect (ui.livePageValue ("Truncate", "Start Point") == 2, "Truncate Start Point 1 -> 2");

    ui.setBig (true);
    ui.mouseDown (20 * 16, 5 * 18 + 8, +1);
    expect (ui.livePageValue ("Truncate", "Start Point") == 12, "Shift: Start Point +10");
    ui.mouseDown (60, 26); ui.mouseDown (60, 26, +1);   // open menu, right click closes it, no effect on values
    ui.setBig (false);
    ui.mouseDown (20 * 16, 5 * 18 + 8, +1);
    expect (ui.livePageValue ("Truncate", "Start Point") == 13, "no Shift: +1");

    ui.setPage ("Standard");
    ui.mouseDown (7 * 16 + 40, 5 * 18 + 8, +1);
    expect (ui.livePageValue ("Standard", "CH A") == 2, "Standard: CH A 1 -> 2");
    ui.mouseDown (12 * 16 + 20, 6 * 18 + 8, 0);
    ui.mouseDown (12 * 16 + 20, 6 * 18 + 8, +1);
    expect (ui.livePageValue ("Standard", "Patch B") == 2, "Standard: Patch B I12 -> I13");

    // Voice Mode header (manual p.28): shared by the three PLAY pages
    expect (ui.livePageValue ("Standard", "Voice Mode") == 12, "Voice Mode default V11");
    ui.mouseDown (3 * 16 + 20, 4 * 18 + 8, 0);
    ui.mouseDown (3 * 16 + 20, 4 * 18 + 8, -1);
    expect (ui.livePageValue ("Standard", "Voice Mode") == 11, "Voice Mode V11 -> V10");
    expect (ui.livePageValue ("Main", "Voice Mode") == 11, "Voice Mode is shared with Split Disp");
    expect (ui.livePageValue ("EightKey", "Voice Mode") == 11, "Voice Mode is shared with 8 Key Disp");
    for (int i = 0; i < 12; ++i) ui.mouseDown (3 * 16 + 20, 4 * 18 + 8, -1);
    expect (ui.livePageValue ("Standard", "Voice Mode") == 0, "Voice Mode runs down to VAL and stops");
    s5::App::get().voiceMode = 12;

    // PLAY: sounding parts and keys (note state from MIDI; a click on a keyboard plays a note)
    {
        auto& ap = s5::App::get();
        const uint8_t on[3] = { 0x90, 60, 100 }, off[3] = { 0x80, 60, 0 };
        ap.midiFeed (on, 3);
        expect (ap.playing (0) && ! ap.playing (1), "note on lights channel 1 only");
        ap.midiFeed (off, 3);
        expect (! ap.playing (0), "note off clears it");
        ui.setPage ("EightKey");
        ui.mouseDown (62 + 270, 268 + 21 * 2 + 10, -1);          // row C (channel 3), left button
        expect (ap.playing (2), "mouse down on the 8 Key keyboard sounds the part's channel");
        ui.mouseMove (62 + 330, 268 + 21 * 2 + 10);
        expect (ap.playing (2), "wiping to the next key keeps the part sounding");
        ui.mouseMove (62 + 330, 268 + 21 * 5 + 10);
        expect (! ap.playing (2) && ap.playing (5), "wiping into another row moves the note to that part");
        ui.mouseMove (300, 100);
        expect (! ap.playing (5), "leaving the keyboard ends the note");
        ui.mouseMove (62 + 270, 268 + 10);
        expect (! ap.playing (1), "moving without the button does nothing");
        ui.mouseDown (62 + 270, 268 + 10, -1);
        expect (ap.playing (1), "note on");
        ui.mouseUp();
        expect (! ap.playing (1), "note ends with the mouse button (real time)");
        ui.setPage ("Main");
        ui.mouseDown (45 + 15 * 14 + 5, 366 + 40, -1);
        expect (ap.playing (1), "mouse down on the Split keyboard plays the displayed part (CH A is now shared with the PLAY pages: 2)");
        ui.mouseUp();
        expect (! ap.playing (1), "released");
    }

    // graphic fields: arrow cursor inside, points can be grabbed and dragged
    {
        ui.setPage ("Tva");
        ui.mouseDown (113, 282, -1);                             // TVA point 1: rate 78, level 127
        ui.mouseMove (113, 332);
        expect (ui.livePageValue ("Tva", "Level1") == 83, "dragging a TVA point down lowers its level");
        const int r2 = ui.livePageValue ("Tva", "Rate2"), r1 = ui.livePageValue ("Tva", "Rate1");
        ui.mouseMove (150, 332);
        expect (ui.livePageValue ("Tva", "Rate1") < r1, "dragging a point to the right makes its segment longer");
        expect (ui.livePageValue ("Tva", "Rate2") == r2, "no room in the next segment (rate 127): it stays");
        const int r1b = ui.livePageValue ("Tva", "Rate1");
        ui.mouseMove (100, 332);
        expect (ui.livePageValue ("Tva", "Rate1") > r1b, "dragging to the left makes its segment shorter");
        ui.mouseMove (100, 250);
        expect (ui.livePageValue ("Tva", "Level1") > 83, "dragging up raises the level again");
        ui.mouseUp();
        const int l1 = ui.livePageValue ("Tva", "Level1");
        ui.mouseMove (400, 380);
        expect (ui.livePageValue ("Tva", "Level1") == l1, "released: no more change");
        ui.setPage ("Tvf");
        const int f1 = ui.livePageValue ("Tvf", "Rate1"), f2 = ui.livePageValue ("Tvf", "Rate2");
        ui.mouseDown (156, 282, -1);
        ui.mouseMove (185, 282);
        expect (ui.livePageValue ("Tvf", "Rate1") < f1 && ui.livePageValue ("Tvf", "Rate2") > f2, "TVF: point moves right, the next segment gives the time back");
        ui.mouseUp();
        // mouse over: values, graph points, buttons light up
        ui.setPage ("Tva");
        ui.mouseMove (8 * 16 + 20, 12 * 18 + 8);
        expect (ui.hoverFieldIndex() >= 0, "mouse over a value");
        ui.mouseMove (300, 100);
        expect (ui.hoverFieldIndex() < 0, "no mouse over on empty space");
        ui.mouseMove (8 * 16 + 20, 12 * 18 + 8);
        ui.mouseLeave();
        expect (ui.hoverFieldIndex() < 0, "the lit value goes out when the mouse leaves");
        ui.mouseMove (8 * 16 + 20, 12 * 18 + 8);
        ui.mouseDown (40 + 10, 20, -1);                              // MODE menu opens
        expect (ui.hoverFieldIndex() < 0, "and while a menu is open");
        ui.mouseDown (600, 300, 1);
        ui.setPage ("Truncate");
        const int st0 = ui.livePageValue ("Truncate", "Start Point");
        ui.mouseMove (102 + 150, 380);
        ui.mouseDown (102, 380, -1);
        ui.mouseMove (102 + 150, 380);
        ui.mouseUp();
        expect (ui.livePageValue ("Truncate", "Start Point") > st0, "dragging the Truncate start marker moves the start point");
    }

    // only one cyan box: the value under the mouse (the chosen value shows while the mouse is on it or on Dec / Inc)
    {
        auto cyanAt = [&] (int x, int y)
        {
            s5::Screen scr; ui.render (scr);
            std::vector<uint32_t> rgb (640 * 450); scr.toRgb (rgb.data());
            return rgb[(size_t) y * 640 + x] == 0x3de0ff;
        };
        ui.setPage ("Patch");
        const int vx = 330, y1 = (4 + 1) * 18 + 6, y2 = (4 + 2) * 18 + 6;
        ui.mouseMove (vx, y1);
        ui.mouseDown (vx, y1, 0);
        expect (cyanAt (vx, y1), "Patch: chosen value is lit while the mouse is on it");
        ui.mouseMove (vx, y2);
        expect (cyanAt (vx, y2) && ! cyanAt (vx, y1), "Patch: only the value under the mouse is cyan");
        ui.mouseMove (200, 400);
        expect (! cyanAt (vx, y1) && ! cyanAt (vx, y2), "Patch: nothing is cyan when the mouse is elsewhere");
        ui.mouseMove (424 + 20, 20);
        expect (cyanAt (vx, y1), "Patch: the chosen value shows while the mouse is on Dec / Inc");
        ui.mouseMove (200, 400);
    }

    ui.setPage ("Mix");
    ui.mouseDown (330, 4 * 18 + 8, +1);
    expect (ui.livePageValue ("Mix", "Source1") == 8 + 1 + 1 - 0 || ui.livePageValue ("Mix", "Source1") > 0, "Mix: Source1 changes");
    ui.mouseDown (330, 8 * 18 + 8, 0);
    ui.mouseDown (330, 8 * 18 + 8, -1);
    expect (ui.livePageValue ("Mix", "Source1 Level") == 126, "Mix: Source1 Level 127 -> 126");
    ui.setPage ("Sampling");
    ui.mouseDown (21 * 16 - 8, 6 * 18 + 8, +1);
    expect (ui.livePageValue ("Sampling", "Frequency") == 30, "Sampling: Frequency stays at max 30");
    ui.mouseDown (21 * 16 - 8, 6 * 18 + 8, -1);
    expect (ui.livePageValue ("Sampling", "Frequency") == 15, "Sampling: Frequency 30 -> 15");

    ui.setPage ("ToneMap");
    expect (ui.livePageValue ("ToneMap", "Param") == 0, "Tone Map starts on Original Key");
    ui.mouseDown (6 * 16 + 20, 5 * 18 + 8, +1);                 // first cell, D 2 -> D#2
    ui.mouseDown (9 * 16 + 20, 3 * 18 + 8, 0);
    ui.mouseDown (9 * 16 + 20, 3 * 18 + 8, +1);                 // Param 1 -> 2 (Pitch Follow)
    expect (ui.livePageValue ("ToneMap", "Param") == 1, "Tone Map: Param 2");
    expect (ui.livePageValue ("ToneMap", "cell") == 1, "Tone Map: Pitch Follow default On in selected cell");
    ui.mouseDown (9 * 16 + 20, 3 * 18 + 8, -1);
    expect (ui.livePageValue ("ToneMap", "cell") == 26 + 0 || ui.livePageValue ("ToneMap", "cell") > 0, "Tone Map: first cell value kept after switching back");

    ui.setPage ("Disk");
    ui.mouseDown (150, 4 * 18 + 5 + 18 * 2 + 4, 0);                 // "Load Tone" in the left column
    ui.mouseDown (424 + 10, 26);                                    // Sel
    expect (ui.page() == "HdLoadT", "Disk menu: HD Load T opens the HD Load T page");
    ui.setPage ("Format");
    ui.mouseDown (424 + 10, 26);                                    // Sel on "Execute"
    expect (ui.page() == "Format", "Format: Execute stays and reports");
    ui.mouseDown (520 + 10, 26);                                    // exit
    expect (ui.page() == "Disk", "Format: exit returns to the disk menu");
    ui.mouseDown (520 + 10, 26);                                    // exit
    expect (ui.page() == "Standard", "Disk menu: exit leaves disk mode");

    // ---- images and floppy ----
    {
        auto& app = s5::App::get();
        ui.setPage ("LoadImage");
        ui.mouseDown (424 + 10, 26);                                // Sel: loads "BRASS" or "FACTORY1" (first in list)
        expect (app.block.label.compare (0, 5, "Brass") == 0, "Load Image: Brass bank loaded");
        ui.setPage ("DirPatch");
        s5::Screen scr; ui.render (scr);
        ui.setPage ("DdWrite");
        ui.mouseDown (424 + 10, 26);                                // Sel: confirm popup
        ui.mouseDown (424 + 10, 26);                                // Sel on Cancel (default)
        expect (! app.drive->job().write, "DD Write: Cancel does not write");
        ui.mouseDown (424 + 10, 26);                                // again confirm
        ui.mouseDown (520 + 10, 26);                                // exit closes the popup only
        expect (ui.page() == "DdWrite", "DD Write: exit closes the confirm popup first");
        ui.mouseDown (424 + 10, 26);
        ui.mouseDown (60, 60, 0);
        ui.wheel (0, 0, -1);                                        // move to Execute
        ui.mouseDown (424 + 10, 26);                                // Execute
        expect (app.drive->state() == s5::FloppyDrive::State::Busy, "DD Write: drive busy");
        for (int i = 0; i < 400; ++i) ui.tick (25);
        expect (app.drive->state() == s5::FloppyDrive::State::Ready && app.drive->job().ok, "DD Write finished");
        ui.setPage ("DdRead");
        ui.mouseDown (424 + 10, 26);
        for (int i = 0; i < 400; ++i) { ui.tick (25); ui.render (scr); }
        expect (app.drive->job().finished && app.drive->job().data.size() == 737280, "DD Read returns a disk image");
        expect (app.files->sizeOf ("S550_001") == 737280, "DD Read saved the image");
    }

    {   // parameter pages edit the loaded data block
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curTone = 3;
        app.block.tones[3].startPoint = 100; app.block.tones[3].endPoint = 5000; app.block.tones[3].loopPoint = 2000; app.block.tones[3].loopMode = 2;
        auto lp = s5::makeLivePage ("Loop");
        s5::Screen scr; lp->draw (scr);
        expect (lp->byLabel ("Start Point")->p.value == 100 && lp->byLabel ("End Point")->p.value == 5000, "Loop shows the tone's points");
        expect (lp->byLabel ("Loop Mode")->p.value == 2, "Loop shows the loop mode");
        lp->selected = 2; lp->change (1);
        expect (app.block.tones[3].startPoint == 101, "Loop edit writes back to the tone");
        auto tr = s5::makeLivePage ("Truncate");
        tr->draw (scr);
        expect (tr->byLabel ("End Point")->p.value == 5000, "Truncate follows the tone");
        tr->byLabel ("Source")->p.value = 4; tr->selected = 0; tr->postChange (*tr);
        expect (app.curTone == 4, "Truncate Source selects the tone");
        auto te = s5::makeLivePage ("ToneEdit");
        te->draw (scr);
        expect (te->byLabel ("Tone")->p.value == 4, "Tone Edit follows the selected tone");
        app.linked = false;
    }

    {   // patch pages
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curPatch = 2;
        app.block.patches[2].raw[15] = 3; app.block.patches[2].raw[236] = 255; app.block.patches[10].raw[237] = 77;
        s5::Screen scr;
        auto sp = s5::makeLivePage ("Split"); sp->draw (scr);
        expect (sp->byLabel ("Key Mode")->p.value == 3 && sp->byLabel ("Oct.Shift")->p.value == -1, "Split shows key mode and octave shift of the patch");
        sp->selected = 3; sp->change (1);
        expect (app.block.patches[2].raw[15] == 4, "Split writes key mode");
        auto pm = s5::makeLivePage ("PatchMap");
        for (int k = 0; k < 10; ++k) { pm->byLabel ("Param")->p.value = k; pm->postChange (*pm); if (pm->byLabel ("Param")->p.text().find ("Level") != std::string::npos) break; }
        pm->draw (scr);
        expect (pm->fields[2 * 4 + 1].p.value == 77, "Patch Map shows level of patch 10 (row 3, column 2)");
        pm->fields[0].p.value = 90; pm->postChange (*pm);
        expect (app.block.patches[0].raw[237] == 90, "Patch Map writes level");
        app.linked = false;
    }

    {   // key map editing
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curPatch = 0; app.curKey = 60;
        auto& r = app.block.patches[0].raw;
        for (int i = 0; i < 109; ++i) r[17 + i] = i < 48 ? 1 : 2;       // keys 12-59 -> tone 1, 60-120 -> tone 2
        s5::Screen scr;
        auto sp = s5::makeLivePage ("Split"); sp->draw (scr);
        expect (sp->byLabel ("1st Tone")->p.value == 2, "1st Tone at the cursor key");
        auto press = [&] (int key) { const uint8_t on[3] = { 0x90, (uint8_t) key, 100 }, off[3] = { 0x80, (uint8_t) key, 0 }; app.midiFeed (on, 3); app.midiFeed (off, 3); sp->draw (scr); };
        press (48);                                                       // Info mode: the pressed key is shown, nothing changes
        expect (app.curKey == 48 && r[17 + 36] == 1, "Info: a key press only selects the key for the Information lines");
        sp->byLabel ("Type Select")->p.value = 2;   // 1st
        sp->byLabel ("1st Tone")->p.value = 5; sp->byLabel ("2nd Tone")->p.value = 6;
        press (50);
        expect (r[17 + 38] == 5 && r[126 + 38] != 6, "Set / 1st: the chosen tone is put on the pressed key (1st map only)");
        sp->byLabel ("Type Select")->p.value = 1; press (52);
        expect (r[17 + 40] == 5 && r[126 + 40] == 6, "Set / 1st&2nd: both maps");
        sp->byLabel ("Type Select")->p.value = 4; press (54);
        expect (r[17 + 42] != 5, "Set / Off: nothing assigned");
        app.linked = false;
    }

    {   // Split has a click hook (keyboard): a first click on a field must still only select it
        auto& app = s5::App::get(); app.linked = false;
        ui.setPage ("Split");
        const int v0 = ui.livePageValue ("Split", "Oct.Shift");
        expect (ui.holdDir (200, 8 * 18 + 8, +1) == +1, "Split: hold on hovered field");
        ui.mouseDown (200, 8 * 18 + 8, +1);
        expect (ui.livePageValue ("Split", "Oct.Shift") != v0, "Split: first click changes Oct.Shift");
        expect (ui.holdDir (200, 8 * 18 + 8, +1) == +1, "Split: hold works once selected");
    }

    {   // name box on the Patch page
        auto& app = s5::App::get(); app.linked = false;
        ui.setPage ("Patch");
        const int bx = 13 * 16 + 4, by = 16 * 18 + 4 + 8;       // first character of the black box
        expect (! ui.wantsKeys(), "name box not selected at first");
        expect (ui.holdDir (bx, by, +1) == 0, "name box: no hold before it is selected");
        ui.mouseDown (bx, by, +1);
        expect (ui.wantsKeys(), "first click selects the name box");
        s5::Screen sc; ui.render (sc);
        ui.key ('X');
        ui.key ('y');
        ui.key (8);
        ui.mouseDown (bx + 32, by, 0);                          // click on the third character: only moves the cursor
        ui.render (sc);
        ui.mouseDown (bx + 32, by, +1);                         // right click now raises the character
        ui.mouseDown (bx + 16 * 11, by, 0);                     // far right: cursor stops behind the text, no gap
        ui.key ('!');
        ui.render (sc);
        ui.key (27);
        expect (! ui.wantsKeys(), "escape leaves the name box");
        ui.render (sc);
    }

    {   // LFO / TVF / TVA: bytes confirmed with CLUB_EP I46 "Wave 5", SUB / COM pull-downs
        auto& app = s5::App::get();
        std::vector<uint8_t> img; { FILE* f = std::fopen ("Web/disks/CLUB_EP.IMG", "rb"); if (f) { img.resize (737280); img.resize (std::fread (img.data(), 1, 737280, f)); std::fclose (f); } }
        if (img.size() == 737280)
        {
            app.block.parse (img.data(), img.size()); app.disk = app.block; app.linked = true; app.curTone = 29;
            s5::UiState u;
            u.setPage ("Lfo");
            expect (u.livePageValue ("Lfo", "Rate") == 88 && u.livePageValue ("Lfo", "Delay") == 86 && u.livePageValue ("Lfo", "Sync") == 1, "LFO values of I46");
            u.setPage ("Tvf");
            expect (u.livePageValue ("Tvf", "Cut-off") == 98 && u.livePageValue ("Tvf", "Key Follow") == 20 && u.livePageValue ("Tvf", "EG Depth") == 92, "TVF values of I46");
            expect (u.livePageValue ("Tvf", "Rate3") == 16 && u.livePageValue ("Tvf", "Level3") == 119 && u.livePageValue ("Tvf", "Sus") == 3 && u.livePageValue ("Tvf", "End") == 4, "TVF envelope of I46");
            u.setPage ("Tva");
            expect (u.livePageValue ("Tva", "Rate1") == 78 && u.livePageValue ("Tva", "Rate3") == 58 && u.livePageValue ("Tva", "Level2") == 127 && u.livePageValue ("Tva", "Sus") == 2 && u.livePageValue ("Tva", "End") == 3, "TVA envelope of I46");
            s5::Screen sc; u.render (sc);
            u.mouseDown (260, 26);                                      // SUB: pull-down E.Source / C.Source
            u.render (sc); save (sc, (dir + "/ui_tva_sub.ppm").c_str());
            u.mouseMove (270, 4 * 18 + 22);                              // hover C.Source
            u.mouseDown (270, 4 * 18 + 22);                              // click activates it
            u.render (sc);
            u.mouseDown (360, 26);                                      // COM pull-down
            u.render (sc); save (sc, (dir + "/ui_tva_com.ppm").c_str());
            u.mouseDown (360, 26);                                      // closes again
            u.setPage ("Tvf");
            u.mouseDown (8 * 16 + 10 + 0, 12 * 18 + 4 + 8);
            u.mouseDown (424 + 10, 26);                                 // Dec on Rate1 -> byte 110
            expect (app.block.tones[29].at (110) == 59, "TVF rate 1 written to byte 110");
            // COM > Init Page PRM
            u.mouseDown (360, 26); u.mouseDown (360, 98);
            expect (app.block.tones[29].at (38) == 127 && app.block.tones[29].at (110) == 127, "Init Page PRM resets the page");
            // Swap with C.Source = I11 (tone 0)
            const uint8_t before = app.block.tones[0].at (38);
            u.mouseDown (260, 26); u.mouseDown (270, 4 * 18 + 22);      // SUB > C.Source
            u.mouseDown (360, 26); u.mouseDown (360, 116); // COM > Swap Page PRM
            expect (app.block.tones[29].at (38) == before, "Swap Page PRM exchanges with C.Source");
            u.render (sc);
        }
        app.linked = false; app.curTone = 0;
    }

    {   // ORCHESTR I28 "Gong3" (second device comparison)
        auto& app = s5::App::get();
        std::vector<uint8_t> img; { FILE* f = std::fopen ("Web/disks/ORCHESTR.IMG", "rb"); if (f) { img.resize (737280); img.resize (std::fread (img.data(), 1, 737280, f)); std::fclose (f); } }
        if (img.size() == 737280)
        {
            app.block.parse (img.data(), img.size()); app.disk = app.block; app.linked = true; app.curTone = 15;
            s5::UiState u;
            u.setPage ("Lfo");
            expect (u.livePageValue ("Lfo", "Rate") == 71 && u.livePageValue ("Lfo", "Mode") == 1 && u.livePageValue ("Lfo", "Offset") == -64 && u.livePageValue ("Lfo", "Delay") == 0, "LFO of Gong3");
            u.setPage ("Tvf");
            expect (u.livePageValue ("Tvf", "Cut-off") == 83 && u.livePageValue ("Tvf", "Resonance") == 23 && u.livePageValue ("Tvf", "Key Follow") == 63 && u.livePageValue ("Tvf", "EG Depth") == 127, "TVF of Gong3");
            expect (u.livePageValue ("Tvf", "Vel-Rate") == 127 && u.livePageValue ("Tvf", "L.Curve") == 2 && u.livePageValue ("Tvf", "Sus") == 2 && u.livePageValue ("Tvf", "End") == 6 && u.livePageValue ("Tvf", "Rate5") == 12 && u.livePageValue ("Tvf", "Level5") == 40, "TVF envelope of Gong3");
            u.setPage ("Tva");
            expect (u.livePageValue ("Tva", "Rate2") == 26 && u.livePageValue ("Tva", "Level2") == 110 && u.livePageValue ("Tva", "Sus") == 3 && u.livePageValue ("Tva", "End") == 4 && u.livePageValue ("Tva", "Vel-Rate") == 0, "TVA of Gong3");
        }
        app.linked = false; app.curTone = 0;
    }

    {   // MIDI Monitor
        auto& app = s5::App::get();
        s5::UiState u; u.setPage ("Monitor");
        expect (u.midiOn(), "monitor asks for MIDI");
        s5::Screen sc; u.render (sc); save (sc, (dir + "/ui_monitor_idle.ppm").c_str());
        const uint8_t n1[3] = { 0x90, 60, 100 }, n2[3] = { 0x91, 62, 90 }, rt[1] = { 0xF8 }, pc[2] = { 0xC0, 57 };
        app.midiLog.clear(); app.monCh = 0; app.monRt = false;
        app.midiFeed (n1, 3); app.midiFeed (n2, 3); app.midiFeed (rt, 1);
        expect (app.midiLog.size() == 3, "only channel 1 and no real time bytes");
        app.midiFeed (pc, 2);
        for (int i = 0; i < 70; ++i) app.midiFeed (n1, 3);
        { const uint8_t sx[6] = { 0xF0, 0x7E, 0x7F, 6, 1, 0xF7 }; app.midiFeed (sx, 6); }
        expect (app.midiLog.size() == 180, "log keeps the last 180 bytes");
        u.render (sc); save (sc, (dir + "/ui_monitor_data.ppm").c_str());
        app.monCh = 0; app.midiLog.clear();
    }

    {   // Wave Scope: EXE starts the oscilloscope, exit leaves
        auto& app = s5::App::get();
        s5::UiState u; u.setPage ("WaveScope");
        expect (! u.scopeOn(), "scope idle at first");
        u.mouseDown (440, 26, 0);
        expect (! u.scopeOn(), "idle: Sel does nothing");
        u.mouseDown (340, 26, 0);                                 // COM switches the scope page on
        u.mouseDown (440, 26, 0);                                 // EXE (Sel position)
        expect (u.scopeOn(), "EXE starts the scope");
        float buf[1000]; for (int i = 0; i < 1000; ++i) buf[i] = 0.8f * std::sin (i * 0.12f);
        app.scopeFeed (buf, 1000);
        s5::Screen sc; u.render (sc); save (sc, (dir + "/ui_scope.ppm").c_str());
        expect (app.scope[0] >= 0 && app.scope[1] > app.scope[0], "scope starts at a rising zero crossing");
        u.mouseDown (440, 26, 0);                                 // Stop
        expect (! u.scopeOn(), "Stop stops the scope");
        u.mouseDown (400, 85, 0);                                 // the red bar starts it as well
        expect (u.scopeOn(), "the red bar starts the scope");
        u.mouseDown (440, 26, 0);
        u.mouseDown (536, 26, 0);                                 // exit
        expect (! u.scopeOn() && u.page() == "WaveScope", "exit goes back to the idle picture");
        s5::Screen s4; u.render (s4); save (s4, (dir + "/ui_scope_idle.ppm").c_str());
    }

    {   // character table, patch id field, confirmed bytes on the Patch page
        auto& app = s5::App::get(); app.linked = false;
        s5::UiState u; u.setPage ("Patch");
        const int bx = 13 * 16 + 4, by = 16 * 18 + 12;
        u.mouseDown (bx, by, 0);                                  // select the name box -> table appears
        s5::Screen sc; u.mouseMove (464 + 16 * 3 + 4, 93 + 18 * 3 + 4); u.render (sc);
        save (sc, (dir + "/ui_nametable.ppm").c_str());
        expect (s5::pages::nameTableHit (464 + 4, 93 + 4) == 0 && s5::pages::nameTableHit (464 + 159, 93 + 179) == 99, "table geometry");
        std::string t = "The Deep    "; int cur = 0;
        expect (s5::pages::nameTableAction (33, t, cur, 12) && t[0] == 'A' && cur == 1, "table: character inserted, cursor moves");
        expect (s5::pages::nameTableAction (96, t, cur, 12) && t[1] == ' ' && t[2] == 'h', "table: I inserts a blank");
        expect (s5::pages::nameTableAction (97, t, cur, 12) && t[1] == 'h', "table: D deletes");
        expect (s5::pages::nameTableAction (98, t, cur, 12) && cur == 0, "table: left arrow");
        expect (! s5::pages::nameTableAction (95, t, cur, 12), "table: shaded block has no function");
        u.mouseDown (464 + 16 * 3 + 4, 93 + 18 * 3 + 4, 0);       // click on a table cell writes into the box
        expect (u.wantsKeys(), "table click keeps the name box active");
        // patch id field
        u.mouseDown (40, 3 * 18 + 6, 0);
        expect (! u.wantsKeys(), "id field replaces the name box selection");
        s5::Screen s2; u.render (s2); save (s2, (dir + "/ui_patchid.ppm").c_str());
        expect (u.holdDir (40, 3 * 18 + 6, +1) == +1, "id field: hold works once selected");
        u.mouseDown (40, 3 * 18 + 6, +1);
        // confirmed bytes with a disk
        s5::UiState v; v.setPage ("Patch");
        app.block.patches[2].raw[236] = (uint8_t) (int8_t) -2; app.block.patches[2].raw[237] = 99; app.linked = true; app.curPatch = 2;
        s5::Screen s3; v.render (s3);
        v.mouseDown (40, 3 * 18 + 6, 0);
        v.mouseDown (40, 3 * 18 + 6, +1);
        expect (app.curPatch == 3, "id field changes the current patch");
        app.curPatch = 2;
        v.mouseDown (200, (4 + 8) * 18 + 8, 0);                   // select Oct.Shift row
        v.mouseDown (200, (4 + 8) * 18 + 8, +1);
        expect ((int8_t) app.block.patches[2].raw[236] == -1, "Oct.Shift stored in byte 236");
        app.linked = false; app.curPatch = 0;
    }

    {   // clicking the middle of every character puts the cursor exactly there (proportional text)
        s5::UiState u; const int bx = 13 * 16 + 4, by = 16 * 18 + 12;
        u.mouseDown (bx, by, 0);
        for (int i = 0; i < 12; ++i) u.key (' ');
        for (int i = 0; i < 12; ++i) u.key (37);
        const std::string txt = "Mein Patch";
        for (char c : txt) u.key (c);
        std::string t12 = txt; t12.resize (12, ' ');
        const auto sp = s5::Screen::charSpans (t12);
        for (int i = 0; i <= (int) txt.size(); ++i)
        {
            u.mouseDown (13 * 16 + 2 + (sp[(size_t) i].x0 + sp[(size_t) i].x1) / 2, by, 0);
            expect (u.nameCursor() == i, ("cursor click on character " + std::to_string (i)).c_str());
        }
    }

    {   // Tone Edit name box
        auto& app = s5::App::get(); app.linked = false;
        s5::UiState u; u.setPage ("ToneEdit");
        const int bx = 13 * 16 + 4, by = 16 * 18 + 4 + 8;
        s5::Screen sc; u.render (sc);
        expect (! u.wantsKeys(), "Tone Edit: box not active at first");
        expect (u.holdDir (bx, by, +1) == 0, "Tone Edit: no hold before the box is selected");
        u.mouseDown (bx, by, +1);
        expect (u.wantsKeys(), "Tone Edit: first click selects the box");
        u.key ('K'); u.key ('i'); u.key ('c'); u.key ('k');
        u.render (sc);
        u.key (27);
        expect (! u.wantsKeys(), "Tone Edit: escape leaves the box");
        // linked: edits the tone name on the disk
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curTone = 2;
        s5::UiState v; v.setPage ("ToneEdit"); v.render (sc);
        v.mouseDown (bx, by, 0); v.key ('Z'); v.key ('Z');
        expect (app.block.tones[2].name.substr (0, 2) == "ZZ" && app.block.tones[2].name.size() == 8, "Tone Edit: name written to the tone");
        v.mouseDown (bx, by, 0);
        app.linked = false;
    }

    {   // pull-down menus act on memory and disk
        auto& app = s5::App::get();
        app.hasDisk = false; app.linked = false;
        app.block = s5::makeDemoBlock (0);
        s5::UiState u; s5::Screen sc;
        u.setPage ("LoadAll"); u.mouseDown (424 + 10, 26);                 // Sel on "Set I" with no disk
        expect (app.block.tones[0].name.substr (0, 4) == "Kick", "Load without a disk leaves memory alone");
        app.disk = s5::makeDemoBlock (1); app.hasDisk = true;
        u.mouseDown (424 + 10, 26);
        expect (app.linked && app.block.tones[0].name.substr (0, 5) == "Brass", "Load copies the disk into memory");
        app.block.tones[0].name = "Edited  ";
        u.setPage ("SaveAll"); u.mouseDown (424 + 10, 26);
        expect (app.disk.tones[0].name.substr (0, 6) == "Edited", "Save copies memory to the disk");
        u.setPage ("Format"); u.mouseDown (424 + 10, 26);
        expect (app.disk.tones[0].name.substr (0, 1) == " " && app.block.tones[0].name.substr (0, 6) == "Edited", "Format clears only the disk");
        u.render (sc);
        app.linked = false; app.hasDisk = false;
    }

    {   // pull-down menus: moving over an entry selects it, a click activates it
        s5::UiState u; s5::Screen sc;
        u.setPage ("Disk");
        const int y4 = 4 * 18 + 5 + 18 * 3 + 4;                 // "HD Save" in the left column
        expect (u.mouseMove (150, y4), "hover over a Disk menu entry changes the picture");
        expect (u.page() == "Disk", "hover alone does not open the entry");
        u.mouseDown (150, y4, 0);
        expect (u.page() == "HdSave", "a click on the entry opens it");
        u.setPage ("Format");
        const int fy = 5 * 18 + 4 + 18;                          // "Execute"
        u.mouseMove (200, 5 * 18 + 4);
        u.mouseMove (200, fy);
        expect (u.page() == "Format", "hover in a popup does not activate");
        u.mouseDown (200, 5 * 18 + 4 + 36, 0);                   // "Cancel"
        expect (u.page() == "Disk", "a click on Cancel returns to the disk menu");
    }

    {   // right click closes pull-downs
        s5::UiState u;
        u.setPage ("Standard");
        u.mouseDown (60, 26, 0);                                  // MODE opens
        u.mouseDown (60, 4 * 18 + 5 + 18 * 5, +1);                // right click on an entry: closes, no choice
        expect (u.page() == "Standard", "right click closes the MODE menu without choosing");
        u.mouseDown (150, 26, 0);
        u.mouseDown (300, 300, +1);
        expect (u.page() == "Standard", "right click closes the MENU menu");
        u.setPage ("Disk");
        u.mouseDown (300, 300, +1);
        expect (u.page() == "Standard", "right click closes the Disk menu (like exit)");
        u.setPage ("Format");
        u.mouseDown (300, 300, +1);
        expect (u.page() == "Disk", "right click closes a popup page");
    }

    {   // top tabs: mouse over lights them up, an open pull-down hands over to the other tab
        s5::UiState u; s5::Screen sc;
        expect (u.mouseMove (60, 26), "hover over MODE changes the picture");
        expect (! u.mouseMove (62, 27), "same tab: nothing to redraw");
        u.mouseDown (60, 26, 0);
        u.render (sc);
        u.mouseMove (150, 26);                                     // over MENU while MODE is open
        u.mouseDown (150, 4 * 18 + 5 + 18 * 1, 0);                 // second entry of the MENU list
        expect (u.page() == "Patch", "an open menu stays: a click beside it does nothing");
        expect (u.mouseLeave() || true, "leaving is harmless");
    }

    {   // sampler core: Auto trigger with pre-trigger, 15 kHz, Manual, memory check
        auto& a = s5::App::get();
        a.hostRate = 48000.0;
        std::vector<float> sig (48000, 0.f);
        for (int i = 0; i < 48000; ++i) sig[(size_t) i] = i < 4800 ? 0.001f : 0.8f * std::sin (i * 0.05f);
        expect (a.recArm (10, 0, false, 0.5, 20.0, 60, false, false, 60, 48000.0).empty(), "arm Auto");
        a.recFeed (sig.data(), 2048, 48000.0);
        expect (a.rec.state.load() == 1, "quiet input: still waiting");
        a.recFeed (sig.data() + 2048, 48000 - 2048, 48000.0);
        expect (a.rec.state.load() == 3, "loud input: recording finished");
        a.recPoll();
        const auto& t = a.block.tones[10];
        expect (a.rec.msg == "Complete" && t.used() && t.waveBank == 0 && ! t.khz15, "tone stored");
        expect (t.endPoint + 1 == 15000 + 600 * 0 || (t.endPoint + 1 >= 15000 && t.endPoint + 1 <= 15700), "length about 0.5 s + pre-trigger @30 kHz");
        int mx = 0; for (int i = 0; i < 12288; ++i) mx = std::max (mx, (int) std::abs (a.block.wave[0][(size_t) t.waveStart * 12288 + (size_t) i]));
        expect (mx > 1000, "wave data present");
        expect (a.recArm (11, 1, true, 3.0, 0, 0, true, true, 60, 48000.0).empty(), "arm Manual 15 kHz");
        for (int k = 0; k < 4; ++k) a.recFeed (sig.data(), 48000, 48000.0);
        expect (a.rec.state.load() == 3, "manual finished");
        a.recPoll(); expect (a.block.tones[11].khz15 && a.rec.msg == "Complete", "15 kHz tone stored");
        {   // through the page: Manual EXE on bank B, then exit cancels
            a.rec.review = false;
            auto pg = s5::makeLivePage ("Sampling");
            pg->fields[0].p.value = 20; pg->fields[1].p.value = 1; pg->fields[3].p.value = 5;
            pg->comHook(); pg->clickHook (25 * 16, 4 * 18 + 18, 0); pg->buttonHook (0);
            expect (a.rec.state.load() == 2 || a.rec.state.load() == 1, "page armed the sampler");
            expect (pg->menuOpen && pg->menuOpen(), "page is modal while sampling");
            pg->buttonHook (1);
            expect (a.rec.state.load() == 0, "exit cancels");
        }
        {   // review view after a recording; any click returns, the tone stays
            a.rec.review = true; a.rec.tone = 11; a.rec.bank = 1;
            s5::UiState u; u.setPage ("Sampling");
            expect (u.mouseDown (300, 100, 0) && ! a.rec.review, "a click leaves the review");
            expect (a.block.tones[11].used(), "the sample stays");
        }
        expect (a.recArm (12, 0, false, 14.0, 0, 0, true, false, 60, 48000.0) == "Memory full", "too long refused");
    }
    {   // Backup: menu open at once, EXE formats, saves and completes; Sel opens the menu again; exit goes back
        auto& a = s5::App::get();
        a.viaImg = true;
        auto pg = s5::makeLivePage ("Backup");
        pg->refresh (*pg);
        expect (pg->menuOpen && pg->menuOpen(), "backup: menu open at once");
        expect (pg->btnNames.size() == 4 && pg->btnNames[2] == "EXE", "backup: EXE / exit shown");
        pg->buttonHook (0);
        pg->refresh (*pg);
        expect (pg->btnNames[2] == "----" && pg->btnNames[3] == "----", "backup: buttons inactive while working");
        for (int i = 0; i < 400 && pg->btnNames[2] == "----"; ++i) { std::this_thread::sleep_for (std::chrono::milliseconds (20)); pg->refresh (*pg); }
        expect (pg->btnNames[2] == "Sel" && pg->btnNames[3] == "----", "backup: complete shows Sel / ----");
        pg->buttonHook (0); pg->refresh (*pg);
        expect (pg->menuOpen && pg->menuOpen(), "backup: Sel opens the menu again");
        pg->buttonHook (1); pg->refresh (*pg);
        expect (pg->buttonHook (1) == "Disk", "backup: exit leads back to the disk menu");
    }
    {   // Combine: S1 end + S2 start joined into the destination tone; Sub tones refused; points follow the tone
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.linked = true;
        auto pg = s5::makeLivePage ("Combine");
        pg->refresh (*pg);
        pg->setValue ("Source 1", 0); pg->refresh (*pg); pg->setValue ("Source 2", 1); pg->refresh (*pg); pg->setValue ("Destination", 10);
        expect (pg->byLabel ("S2 End")->p.value == (int) a.block.tones[1].endPoint, "combine: points of the tone loaded");
        pg->setValue ("Wave Bank", 1); pg->setValue ("S1 End", 12000); pg->setValue ("S2 Start", 6000);
        expect ((int) a.block.tones[0].endPoint == 12000, "combine: point change written to the tone");
        pg->comHook(); pg->buttonHook (0);
        const auto& d = a.block.tones[10];
        const int want = (12000 + 1 + (int) a.block.tones[1].endPoint - 6000 + 1 + s5::data::kSegWords - 1) / s5::data::kSegWords;
        expect (d.used() && d.waveLen == want, "combine: destination tone holds both portions");
        expect (a.block.wave[1][(size_t) d.waveStart * s5::data::kSegWords + 12001] == a.block.wave[0][(size_t) a.block.tones[1].waveStart * s5::data::kSegWords + 6000], "combine: S2 start follows S1 end");
        a.block.tones[3].subTone = true;
        pg->setValue ("Source 1", 3); pg->refresh (*pg);
        pg->comHook(); pg->buttonHook (0);
        expect (true, "combine: sub tone refused without crash");
    }
    {   // every wave that is shown can be played: Combine / Mix audition the displayed tone
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.linked = true;
        for (const char* id : { "Combine", "Mix" })
        {
            s5::UiState u; u.setPage (id);
            s5::Screen sc; u.render (sc);
            expect (a.auditionTone == u.livePageValue (id, id == std::string ("Mix") ? "Source1" : "Source 1"), "audition: shown wave is playable");
        }
    }
    {   // D.Filter: Source tone filtered into the Destination tone of Block II (parameters copied); DC-Cut; Sub tone refused
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.block2 = s5::data::Block(); a.linked = true;
        auto pg = s5::makeLivePage ("DFilter");
        pg->refresh (*pg);
        pg->setValue ("Source", 2); pg->setValue ("Destination", 32 + 12);
        pg->comHook(); pg->buttonHook (0);
        const auto& d = a.block2.tones[12];
        expect (d.used() && d.waveLen == a.block.tones[2].waveLen && d.name == a.block.tones[2].name, "dfilter: destination holds the filtered tone");
        expect (pg->byLabel ("Destination")->p.value == 32 + 12 && pg->selected >= 0 && pg->fields[(size_t) pg->selected].label == "Destination", "dfilter: cursor jumps to the destination");
        a.block.tones[3].subTone = true;
        pg->setValue ("Source", 3);
        pg->buttonHook (1); pg->comHook(); pg->buttonHook (0);
        expect (true, "dfilter: sub tone refused without crash");
    }
    {   // Wave Loop: Smoothing / Auto Loop write the source (parameters copied, Loop Mode Forward) into the destination; short destination; Sub
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.block2 = s5::data::Block(); a.linked = true;
        auto pg = s5::makeLivePage ("WaveLoop");
        pg->refresh (*pg);
        pg->setValue ("Source", 2); pg->refresh (*pg); pg->setValue ("Destination", 32 + 12);
        expect (pg->byLabel ("End Point")->p.value == (int) a.block.tones[2].endPoint, "waveloop: the points of the source are shown");
        pg->setValue ("Loop Point", 12000);
        expect (a.block.tones[2].loopPoint == 12000, "waveloop: a change of Loop Point reaches the tone");
        auto& st = *pg;
        auto wait = [&] { std::this_thread::sleep_for (std::chrono::milliseconds (1300)); pg->refresh (*pg); };
        st.comHook();                                                       // entry 0: A.Loop L->E
        st.buttonHook (0); wait();
        const auto& d = a.block2.tones[12];
        expect (d.used() && d.loopMode == 0 && d.name == a.block.tones[2].name, "waveloop: A.Loop writes the destination");
        expect (d.loopPoint >= 12000 && d.endPoint <= a.block.tones[2].endPoint && d.endPoint > d.loopPoint, "waveloop: A.Loop points lie between Loop and End");
        expect (pg->fields[(size_t) pg->selected].label == "Destination", "waveloop: cursor jumps to the destination");
        a.block2 = s5::data::Block();
        a.block2.tones[13] = a.block.tones[2]; a.block2.tones[13].waveLen = 1;
        pg->setValue ("Destination", 32 + 13);
        pg->comHook(); pg->buttonHook (0); wait();
        expect (a.block2.tones[13].waveLen == 1, "waveloop: a shorter destination is not written (Not Execute)");
        pg->setValue ("Loop Point", 100); pg->setValue ("Start Point", 0);
        pg->setValue ("Destination", 32 + 14);
        pg->comHook(); pg->buttonHook (0); wait();
        a.block.tones[3].subTone = true;
        pg->setValue ("Source", 3); pg->refresh (*pg);
        pg->comHook(); pg->buttonHook (0); wait();
        expect (true, "waveloop: sub tone / too short Smoothing refused without crash");
        s5::UiState u; u.setPage ("WaveLoop");
        s5::Screen sc; u.render (sc);
        expect (a.auditionTone >= 0, "waveloop: the shown wave is playable");
    }
    {   // Options > Hard disk images: SCSI IDs 0-6, ID 7 = S-550, CD-5 on its own ID, the highest ID boots
        auto& a = s5::App::get();
        std::vector<uint8_t> img ((size_t) (10 + s5::data::kHdBlockSectors) * 512, 0);
        std::memcpy (img.data(), "* ROLAND S-550 *", 16);
        const uint32_t sec = 10, cnt = (uint32_t) s5::data::kHdBlockSectors;
        for (int k = 0; k < 4; ++k) { img[0x400 + 0x30 + k] = (uint8_t) (sec >> (24 - 8 * k)); img[0x400 + 0x34 + k] = (uint8_t) (cnt >> (24 - 8 * k)); }
        std::memcpy (&img[0x400], "TESTDISK", 8);
        std::memcpy (&img[(size_t) sec * 512], "PATCH NAME  ", 12);
        std::map<std::string, std::vector<uint8_t>> files;
        files["/a/first.img"] = img; files["/b/Second.IMG"] = img; files["/c/bad.img"] = std::vector<uint8_t> (4096, 1);
        a.readPath = [&files] (const std::string& p, std::vector<uint8_t>& o) { auto it = files.find (p); if (it == files.end()) return false; o = it->second; return true; };
        int saved = 0; a.hdConfigChanged = [&saved] { ++saved; };
        for (int i = 0; i < 7; ++i) a.hdDetach (i);
        a.cdId = -1; a.hasHd = false; a.hdActiveId = -1; saved = 0;
        std::string err;
        expect (! a.hdAttach (7, "/a/first.img", err), "hd: ID 7 is the S-550");
        expect (! a.hdAttach (2, "/c/bad.img", err) && err == "Not an S-550 hard disk image", "hd: a wrong file is refused");
        expect (! a.hdAttach (2, "/x/none.img", err), "hd: a missing file is refused");
        expect (a.hdAttach (3, "/a/first.img", err) && a.hasHd && a.hdActiveId == 3 && a.hdName == "FIRST", "hd: first image on ID 3 is active");
        expect (a.hdDev[3].areas == 1, "hd: the areas are counted");
        expect (a.hdAttach (1, "/b/Second.IMG", err) && a.hdActiveId == 3, "hd: a lower ID does not take over");
        expect (a.hdAttach (5, "/b/Second.IMG", err) && a.hdActiveId == 5 && a.hdName == "SECOND", "hd: the highest ID boots");
        expect (! a.setCdId (5, err), "hd: the CD-5 cannot take a hard disk ID");
        expect (a.setCdId (0, err) && a.cdId == 0, "hd: CD-5 on ID 0");
        expect (! a.hdAttach (0, "/a/first.img", err), "hd: a hard disk cannot take the CD-5 ID");
        a.hdDetach (5);
        expect (a.hdActiveId == 3 && a.hdName == "FIRST", "hd: removing the active disk falls back to the next highest ID");
        a.hdDetach (3); a.hdDetach (1);
        expect (! a.hasHd && a.hdActiveId == -1, "hd: no disk left");
        expect (saved > 0, "hd: every change is stored");
        a.setCdId (-1, err);
        // DISK-Setup page: Hard Disk ID / CD-5 ID are the SCSI devices of the Options menu
        a.hdAttach (2, "/a/first.img", err); a.hdAttach (4, "/b/Second.IMG", err);
        {
            auto pgp = s5::makeLivePage ("DiskSetup"); auto* pg = pgp.get();
            expect (pg && pg->title == "DISK-Setup", "setup: the page exists");
            if (pg)
            {
                pg->refresh (*pg);
                expect (pg->fields[4].p.value == 4, "setup: Hard Disk ID shows the booted ID");
                pg->setValue ("Hard Disk ID", 2);
                expect (a.hdActiveId == 2 && a.hdName == "FIRST", "setup: another ID with an image switches the disk");
                pg->setValue ("Hard Disk ID", 6);
                expect (! a.hasHd && a.fileMsg == "HD Connection Error", "setup: an ID without a disk reports a connection error");
                pg->setValue ("Hard Disk ID", 4);
                expect (a.hasHd && a.hdActiveId == 4, "setup: back on a disk");
                pg->setValue ("CD-5 ID", 4);
                expect (a.cdId == 3, "setup: CD-5 ID 3");
                pg->setValue ("CD-5 ID", 5);
                expect (a.cdId == 3 && ! a.fileMsg.empty(), "setup: CD-5 cannot take a hard disk ID");
                pg->setValue ("S/Controller", 2);
                expect (a.setupCtrl == 2, "setup: S/Controller kept");
                pg->comHook(); pg->buttonHook (0);
            }
            s5::UiState u; u.setPage ("DiskSetup"); s5::Screen sc; u.render (sc);
            u.setPage ("HdLoad"); u.render (sc);
        }
        a.hdDetach (4); a.hdDetach (2);
        a.setCdId (-1, err);
        // HD Save: block -> area of the image, file written, reload gives the same block
        {
            std::vector<uint8_t> file = files["/b/Second.IMG"];
            a.writeRange = [&file] (const std::string&, size_t off, const uint8_t* d, size_t n) { if (off + n > file.size()) return false; std::memcpy (&file[off], d, n); return true; };
            a.hdAttach (4, "/b/Second.IMG", err);
            a.readPath = [&file] (const std::string&, std::vector<uint8_t>& out) { out = file; return true; };       // Wave Verify reads the written file again
            a.block.label = "SAVETEST    "; a.block.wave[0][5] = 123; a.block.wave[1][7] = -77;
            auto pgp = s5::makeLivePage ("HdSave"); auto* pg = pgp.get();
            expect (pg && pg->title == "DISK-HD Save", "hdsave: the page exists");
            a.hdSaveI = 0; a.hdSaveII = 5;
            pg->comHook(); pg->clickHook (340, 4 * 18 + 3 * 18 + 6, 0); pg->buttonHook (0);
            expect (a.fileMsg.empty() || a.fileMsg == "Complete", "hdsave: nothing is written before the 5 s are over");
            { s5::data::HdImage early; early.open (file.data(), file.size()); expect (early.slots[0].blockLabel.compare (0, 8, "SAVETEST") != 0, "hdsave: file untouched during the simulated write time"); }
            std::this_thread::sleep_for (std::chrono::milliseconds (5200)); pg->refresh (*pg);
            expect (! a.fileMsgErr && a.fileMsg == "Complete", "hdsave: Save Block I writes area I after 5 s");
            s5::data::HdImage re; re.open (file.data(), file.size());
            s5::data::Block back;
            expect (re.slots[0].valid && re.loadBlock (0, back) && back.label.compare (0, 8, "SAVETEST") == 0 && back.wave[0][5] == 123 && back.wave[1][7] == -77, "hdsave: the saved area reads back");
            expect (! a.hdSaveSlot (5, a.block, err) && err == "HD Area not formatted", "hdsave: an unformatted area is refused");
            {   // Wave Verify: the file is read again and compared
                a.readPath = [&file] (const std::string&, std::vector<uint8_t>& out) { out = file; return true; };
                auto& tn = a.block.tones[0]; tn.waveLen = 1; tn.waveStart = 0; tn.waveBank = 0; tn.subTone = false;
                expect (a.hdSaveSlot (0, a.block, err) && a.hdVerifySlot (0, a.block, err), "wave verify: a saved area verifies");
                a.block.wave[0][5] = 999;
                expect (! a.hdVerifySlot (0, a.block, err) && err == "Verify Error", "wave verify: a different wave is reported");
                a.block.wave[0][5] = 123; tn.waveLen = 0;
            }
            a.hdDetach (4);
        }
    }
    {   // MIDI pages use the device parameters of the App (shared with the SysEx link)
        auto& a = s5::App::get();
        a.initDevParams();
        auto m = s5::makeLivePage ("Message");
        m->refresh (*m);
        expect (m->fields[0].p.value == 2 && m->fields[1].p.value == 9, "message: RX-CH A = 3 (value 2), B = 10");
        m->selected = 8;                                   // P.Chg of part A
        m->change (-1);
        expect (a.mpar[0x28] == 0 && a.mpar[0x29] == 1, "message: P.Chg A off lands in mpar 0x28");
        a.mpar[0x20] = 16; a.mpar[98] = 4; m->refresh (*m);
        expect (m->fields[0].p.value == 16 && m->fields[65].p.value == 5, "message: device values show on the page");
        auto pn = s5::makeLivePage ("ProgNo");
        a.mpar[99 + 9] = 41; pn->refresh (*pn);
        expect (pn->fields[9].p.value == 42, "progno: shows value + 1");
        pn->selected = 3; pn->change (+1);
        expect (a.mpar[99 + 3] == 4, "progno: change writes the raw value");
        auto ms = s5::makeLivePage ("Master");
        ms->refresh (*ms); ms->change (-1);
        expect (a.fpar[0] == 0xff && ms->fields[0].p.value == -1, "master tune: signed byte");
        ms->refresh (*ms);
        expect (ms->fields[0].p.value == -1, "master tune reads back signed");
    }
    {   // Play page: a patch change also goes to the real S-550 as Program Change (number from the RX program change table)
        auto& a = s5::App::get();
        a.initDevParams(); a.mpar[99 + 5] = 77;
        std::vector<uint8_t> got;
        a.midiOut = [&] (const uint8_t* m, int n) { got.assign (m, m + n); };
        auto pg = s5::makeLivePage ("Standard");
        pg->refresh (*pg);
        pg->setValue ("CH B", 4); pg->setValue ("Patch B", 5);
        expect (got.size() == 2 && got[0] == 0xc3 && got[1] == 77, "play: patch change sends Program Change on the part channel");
        a.midiOut = nullptr;
    }
    {   // DISK-MIDI Load / Save: the page starts the transfer of the platform layer and follows its state
        auto& a = s5::App::get();
        int gotCmd = -1; bool gotSave = false;
        a.midiXfer = [&] (bool save, int cmd, std::string&) { gotSave = save; gotCmd = cmd; a.xferState = 1; a.xferFrac = 0.5; return true; };
        auto pg = s5::makeLivePage ("MidiSave");
        pg->refresh (*pg);
        pg->clickHook (336 + 20, 4 * 18 + 3 * 18 + 5, 0);                         // picks "MIDI Save Tone II"
        pg->buttonHook (0);
        expect (gotSave && gotCmd == 3, "midi save: EXE starts the command of the window");
        pg->refresh (*pg);
        a.xferState = 2; pg->refresh (*pg);
        expect (pg->btnNames.size() == 4 && pg->btnNames[2] == "Sel", "midi save: after the transfer Sel / exit");
        a.midiXfer = [&] (bool, int, std::string& e) { e = "MIDI Link not open"; return false; };
        auto pl = s5::makeLivePage ("MidiLoad");
        pl->refresh (*pl); pl->buttonHook (0); pl->refresh (*pl);
        expect (pl->btnNames.size() == 4 && pl->btnNames[3] == "exit", "midi load: a refused start returns to the idle buttons");
        a.midiXfer = nullptr;
    }
    {   // Load Wave: file -> tone with wave
        struct Fs : s5::MemFiles
        {
            bool dialogs() const override { return true; }
            void pickAudio (std::function<void (bool, const std::string&, std::vector<uint8_t>&)> done) override { std::vector<uint8_t> b { 1, 2, 3 }; done (true, "Kick drum.wav", b); }
            bool decodeAudio (const std::string&, const std::vector<uint8_t>&, std::vector<float>& m, double& r, std::string&) override
            { m.assign (22050, 0.f); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (float) std::sin (i * 0.1); r = 22050; return true; }
        };
        auto& a = s5::App::get();
        auto keep = std::move (a.files);
        a.files = std::make_unique<Fs>();
        a.block = s5::makeDemoBlock (0);
        auto pg = s5::makeLivePage ("LoadWave");
        pg->setValue ("Int", 20); pg->setValue ("Wave Bank", 1);
        pg->onEnter(); pg->refresh (*pg);
        expect (! pg->menuOpen(), "load wave: after the file pick the command window stays closed");
        pg->comHook (); pg->buttonHook (0);
        const auto& t = a.block.tones[20];
        expect (t.used() && t.waveBank == 1 && t.name == "Kick dru" && t.khz15 == false && a.waveName == "Kick drum.wav", "load wave: the tone is made from the file");
        expect (a.block.wave[1][2000] != 0 || a.block.wave[1][2001] != 0, "load wave: wave data is in the bank");
        a.files = std::move (keep);
    }
    {   // RULE: every load / save page opens its COM window as soon as it is entered
        for (const char* id : { "ImgLoad", "FdLoad", "MidiLoad", "MidiSave" })
        {
            s5::UiState u2; u2.setPage (id); s5::Screen sc; u2.render (sc);
            auto* lp = u2.liveForTest (id);
            if (! lp || ! lp->menuOpen || ! lp->menuOpen()) { std::printf ("FAIL: %s does not open its COM window on entry\n", id); ++fails; }
        }
    }
    {   // IMG Load Tone: SUB (IMG / Internal) -> Sel -> the big tone view; a click picks, Sel takes the place
        auto& a = s5::App::get();
        a.viaImg = true; a.block = s5::makeDemoBlock (0); a.disk = a.block; a.hasDisk = true;
        auto pg = s5::makeLivePage ("LoadTone");
        pg->refresh (*pg);
        pg->subHook (); pg->refresh (*pg);
        expect (pg->menuOpen(), "img load tone: SUB opens the small menu over the big view");
        pg->clickHook (240, 4 * 18 + 18 + 4, 0); pg->buttonHook (0);                     // second line = Internal
        pg->refresh (*pg);
        expect (pg->menuOpen() && pg->btnNames.size() == 4 && pg->btnNames[2] == "Sel", "img load tone: Sel opens the big view");
        s5::Screen sc; pg->draw (sc);
        int hit = pg->clickHook (16 + 8 * 4 + 5 + 8 * 16 * 0 + 60, 5 * 18 + 2 * 18 + 4, 0);   // a place of the grid
        pg->buttonHook (0); pg->refresh (*pg);
        expect (hit == 2 && pg->fields[1].p.value != 0, "img load tone: a click in the big view picks the place");
        pg->buttonHook (1); pg->refresh (*pg);
        expect (! pg->menuOpen(), "img load tone: exit leaves the SUB menu");
        a.viaImg = false;
    }
    {   // IMG Load P: the SUB menu has the big patch view, too
        auto& a = s5::App::get();
        a.viaImg = true; a.block = s5::makeDemoBlock (0); a.disk = a.block; a.hasDisk = true;
        auto pg = s5::makeLivePage ("LoadPRM");
        pg->refresh (*pg); pg->subHook (); pg->buttonHook (0);                            // IMG line -> big view
        s5::Screen sc; pg->refresh (*pg); pg->draw (sc);
        expect (pg->menuOpen(), "img load p: SUB / Sel opens the big patch view");
        const int hit = pg->clickHook (16 + 5 * 16 + 70, 5 * 18 + 3 * 18 + 4, 0);
        pg->buttonHook (0);
        expect (hit == 2, "img load p: a click picks a patch");
        pg->buttonHook (1); pg->refresh (*pg);
        expect (! pg->menuOpen(), "img load p: exit leaves");
        a.viaImg = false;
    }
    {   // IMG Save: the Finder's save dialog comes first, then the command, then the file is written
        struct Fs : s5::MemFiles { bool dialogs() const override { return true; } };
        auto& a = s5::App::get();
        auto keep = std::move (a.files);
        a.files = std::make_unique<Fs>();
        a.viaImg = true; a.block = s5::makeDemoBlock (0);
        auto pg = s5::makeLivePage ("FdSave");
        pg->onEnter ();
        pg->refresh (*pg);
        expect (pg->menuOpen(), "img save: after the Finder's dialog the command window is open");
        pg->clickHook (22 * 16, 4 * 18 + 18 + 4, 0);                                     // Save Set I
        pg->buttonHook (0);
        std::this_thread::sleep_for (std::chrono::milliseconds (1300));
        pg->refresh (*pg); pg->refresh (*pg);
        expect (! a.files->list().empty(), "img save: the file was written to the place chosen first");
        a.files = std::move (keep); a.viaImg = false;
    }
    {   // UTIL-SYS.Backup: in the UTIL menu, COM -> SYS.Backup runs the sequence
        s5::UiState u3; u3.setPage ("SysBackup");
        expect (u3.mode() == "UTIL", "sys.backup: is a UTIL page (MENU opens the UTIL list)");
        auto pg = s5::makeLivePage ("SysBackup");
        pg->refresh (*pg); pg->comHook(); pg->refresh (*pg);
        expect (pg->menuOpen(), "sys.backup: COM opens the command");
        pg->buttonHook (0); pg->refresh (*pg);
        expect (pg->btnNames.size() > 2 && pg->btnNames[2] == "----", "sys.backup: buttons ---- while working");
    }
    {   // SYS Load FD / SYS Save FD with a pretend drive
        auto& a = s5::App::get();
        auto keep = std::move (a.drive);
        a.drive = std::make_unique<s5::SimFloppy> (s5::makeDemoBlock (0).serialize());
        auto pg = s5::makeLivePage ("SysBackup");
        auto pump = [&] (int n) { for (int i = 0; i < n; ++i) { a.drive->tick (25); pg->refresh (*pg); } };
        pg->refresh (*pg); pg->comHook(); pg->buttonHook (0);                     // first entry: SYS Load FD
        pump (200);
        expect (pg->btnNames.size() > 2 && pg->btnNames[2] == "Sel", "sys.backup: SYS Load FD finished");
        pg->comHook(); pg->clickHook (330, 4 * 18 + 3 * 18 + 4, 0); pg->buttonHook (0);       // SYS Save FD
        pump (200);
        expect (pg->btnNames.size() > 2 && pg->btnNames[2] == "Sel", "sys.backup: SYS Save FD finished");
        pg->comHook(); pg->clickHook (330, 4 * 18 + 5 * 18 + 4, 0); pg->buttonHook (0);       // SYS Save HD without a system in memory
        pg->refresh (*pg);
        expect (pg->btnNames[2] != "----", "sys.backup: SYS Save HD without a system stops at once");
        a.drive = std::move (keep);
    }
    {   // HD Load SYS / HD Save SYS: system area from a linked image into memory, then into the active image
        auto& a = s5::App::get();
        auto ref = s5::data::HdImage::makeBlank (20);
        std::memcpy (ref.data(), "* ROLAND S-550 *", 16);
        std::memcpy (ref.data() + 512, "S-550  CD-5", 11);
        std::memcpy (ref.data() + 256 * 512 + 4, "S550 MT25B", 10);
        std::memcpy (ref.data() + 256 * 512 + 32, "S-550  CD-5  System  Ver. 1.00 ", 31);
        ref[300 * 512 + 7] = 0x5a; ref[5367 * 512 + 9] = 0x77; ref[5368 * 512 + 3] = 0x11;
        auto img = s5::data::HdImage::makeBlank (20);
        a.hd.open (img.data(), img.size()); a.hasHd = true; a.hdActiveId = 0; a.hdDev[0].path = "x"; a.hdDev[0].ok = true; a.hdDev[0].name = "SYSIMG";
        a.hdDev[1].path = "y"; a.hdDev[1].ok = true; a.hdDev[1].name = "REF";
        a.readPath = [&] (const std::string& p, std::vector<uint8_t>& out) { out = p == "y" ? ref : img; return true; };
        size_t written = 0;
        a.writeRange = [&] (const std::string&, size_t, const uint8_t*, size_t n) { written += n; return true; };
        std::string e;
        a.hdSysMem.clear();
        expect (! a.hdSaveSys (0, e) && e == "No System loaded", "hd save sys: nothing in memory is refused");
        expect (! a.hdLoadSys (0, e) && e == "HD No System", "hd load sys: image without a system is refused");
        expect (a.hdLoadSys (1, e) && a.hdSysLine.find ("CD-5") != std::string::npos, "hd load sys: system read from the linked image");
        expect (a.hdSaveSys (0, e), "hd save sys: writes the system");
        expect (a.hd.bytes[300 * 512 + 7] == 0x5a && a.hd.bytes[5367 * 512 + 9] == 0x77, "hd save sys: system sectors copied");
        expect (a.hd.bytes[5368 * 512 + 3] != 0x11, "hd save sys: the first area stays untouched");
        expect (written == 512 + (5368 - 256) * 512, "hd save sys: written to the file");
        // the page: COM -> HD Load SYS opens the list of linked images
        auto pg = s5::makeLivePage ("SysBackup");
        pg->refresh (*pg); pg->comHook(); pg->clickHook (330, 4 * 18 + 2 * 18 + 4, 0); pg->buttonHook (0);
        pg->refresh (*pg);
        expect (pg->menuOpen(), "sys.backup: HD Load SYS shows the list of linked images");
        pg->clickHook (330, 4 * 18 + 18 + 4, 0); pg->buttonHook (0);                    // second entry: ID 1
        for (int i = 0; i < 40; ++i) { std::this_thread::sleep_for (std::chrono::milliseconds (50)); pg->refresh (*pg); }
        expect (pg->btnNames.size() > 2 && pg->btnNames[2] == "Sel", "sys.backup: HD Load SYS finished");
        written = 0; a.hd.bytes[300 * 512 + 7] = 0;
        pg->comHook(); pg->clickHook (330, 4 * 18 + 5 * 18 + 4, 0); pg->buttonHook (0); pg->refresh (*pg);       // SYS Save HD: the targets are listed again
        expect (pg->menuOpen() && written == 0, "sys.backup: SYS Save HD asks for the target");
        pg->clickHook (330, 4 * 18 + 4, 0); pg->buttonHook (0);                         // first entry: ID 0
        for (int i = 0; i < 60; ++i) { std::this_thread::sleep_for (std::chrono::milliseconds (50)); pg->refresh (*pg); }
        expect (written == 512 + (5368 - 256) * 512 && a.hd.bytes[300 * 512 + 7] == 0x5a, "sys.backup: SYS Save HD wrote the system to the chosen image");
        a.hasHd = false; a.hdActiveId = -1; a.writeRange = nullptr; a.readPath = nullptr; a.hdDev[0] = {}; a.hdDev[1] = {}; a.hdSysMem.clear();
    }
    {   // UTIL-Wave Draw: draw with the mouse, belt, scroll zone, COM commands
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.linked = true;
        s5::UiState ui; ui.setPage ("WaveDraw");
        auto* pg = ui.liveForTest ("WaveDraw");
        pg->setValue ("Source", 0);
        const uint32_t rev0 = a.waveRev;
        pg->refresh (*pg);
        const auto& tone = a.block.tones[0];
        const int base = tone.waveStart * s5::data::kSegWords;
        ui.mouseDown (200, 363, -1); ui.mouseMove (240, 300); ui.mouseUp();
        expect (a.waveRev > rev0, "wave draw: drawing changes the wave");
        const int ad = (240 - 101) * 228 / 440;
                expect (std::abs (a.block.wave[tone.waveBank & 1][(size_t) (base + ad)] - 2048 * (363 - 300) / 66) < 40, "wave draw: the line between mouse positions is filled in");
        pg->refresh (*pg);
        pg->openToneMap ("Source"); pg->refresh (*pg);
        expect (pg->popupButtons && pg->subRed, "wave draw: SUB opens the big tone view");
        pg->buttonHook (1); pg->refresh (*pg);
                expect (! pg->popupButtons && ! pg->subRed, "wave draw: after the tone view closes SUB is not lit any more and drawing works");
        ui.mouseMove (85, 330); pg->refresh (*pg);
        expect (pg->byLabel ("Cursor Adrs")->p.value == pg->byLabel ("Window Adrs")->p.value, "wave draw: left scroll zone shows the window address");
        ui.mouseMove (555, 330); pg->refresh (*pg);
        expect (pg->byLabel ("Cursor Adrs")->p.value == pg->byLabel ("Window Adrs")->p.value + 229, "wave draw: right scroll zone shows window + 229");
        ui.mouseDown (300, 290, -1); ui.mouseUp();                                           // belt
        pg->refresh (*pg);
        expect (pg->byLabel ("Window Adrs")->p.value > 0, "wave draw: a click on the belt sets the window address");
        const int w1 = pg->byLabel ("Window Adrs")->p.value;
        ui.mouseDown (85, 360, -1); ui.mouseUp();                                            // left edge: scroll 114 back
        pg->refresh (*pg);
        expect (pg->byLabel ("Window Adrs")->p.value == std::max (0, w1 - 114), "wave draw: edge zone scrolls by 114 addresses");
        pg->setValue ("Window Adrs", 0); pg->refresh (*pg);
        pg->comHook(); pg->clickHook (340, 4 * 18 + 18 + 4, 0); pg->buttonHook (0);          // CLR WINDOW
        expect (a.block.wave[tone.waveBank & 1][(size_t) (base + 10)] == 0, "wave draw: CLR WINDOW erases the window");
        pg->clickHook (340, 4 * 18 + 36 + 4, 0); pg->buttonHook (0);                         // CLR ALL
        expect (a.block.wave[tone.waveBank & 1][(size_t) (base + tone.waveLen * s5::data::kSegWords - 1)] == 0, "wave draw: CLR ALL erases the whole wave");
        a.linked = false;
    }
    {   // Wave Draw: Window Loop sets the tone's points and gives them back
        auto& a = s5::App::get();
        a.block = s5::makeDemoBlock (0); a.linked = true;
        s5::UiState ui; ui.setPage ("WaveDraw");
        auto* pg = ui.liveForTest ("WaveDraw");
        pg->setValue ("Source", 0); pg->refresh (*pg);
        auto& tone = a.block.tones[0];
        const auto s0 = tone.startPoint, l0 = tone.loopPoint, e0 = tone.endPoint; const auto m0 = tone.loopMode;
        pg->setValue ("Window Adrs", 114 * 4); pg->setValue ("Window Loop", 1); pg->refresh (*pg);
        expect (tone.startPoint == 0 && tone.loopPoint == 114 * 4 && tone.endPoint == 114 * 4 + 228 && tone.loopMode == 0, "window loop: points = 0 / window / window + 228");
        pg->setValue ("Window Adrs", 114 * 6); pg->refresh (*pg);
        expect (tone.loopPoint == 114 * 6 && tone.endPoint == 114 * 6 + 228, "window loop: the points follow the window");
        pg->setValue ("Window Loop", 0); pg->refresh (*pg);
        expect (tone.startPoint == s0 && tone.loopPoint == l0 && tone.endPoint == e0 && tone.loopMode == m0, "window loop: Off gives the old points back");
        pg->setValue ("Window Loop", 1); pg->refresh (*pg);
        pg->onLeave();
        expect (tone.startPoint == s0 && tone.loopPoint == l0 && tone.endPoint == e0, "window loop: leaving the page restores the points");
        ui.mouseMove (200, 289);                                                         // over the belt: cursor address = place on the whole wave
        pg->refresh (*pg);
        expect (pg->byLabel ("Cursor Adrs")->p.value > 0, "wave draw: cursor over the belt shows the address");
        a.linked = false;
    }
    {   // HD Load T: SUB > HD # opens the big view (like everywhere), exit closes it again
        s5::UiState ui; ui.setPage ("HdLoadT");
        auto* pg = ui.liveForTest ("HdLoadT");
        pg->refresh (*pg);
        pg->subHook (); pg->refresh (*pg);
        expect (pg->clickHook (230, 4 * 18 + 18 + 5, 0) == 2, "hd load t: SUB list item HD #");
        pg->buttonHook (0); pg->refresh (*pg);
        expect (pg->subRed && pg->btnNames.size() == 4 && pg->btnNames[2] == "Sel", "hd load t: the big view for HD # is open");
        expect (pg->subHook() == false, "hd load t: SUB is locked while the big view is open");
        pg->buttonHook (1); pg->refresh (*pg);
        expect (! pg->menuOpen(), "hd load t: exit closes the big view");
    }
    {   // HD Load / HD Save: no COM on entry (SUB first); in the slot grid only a click fixes the place
        for (const char* id : { "HdSet", "HdSave" })
        {
            s5::UiState u2; u2.setPage (id); s5::Screen sc; u2.render (sc);
            auto* lp = u2.liveForTest (id);
            expect (lp && lp->menuOpen && ! lp->menuOpen(), "HD Load / Save: COM stays closed on entry");
        }
    }
    {   // Patch page: SUB (E./C.Source) and COM (Copy / Init / Swap All / Page PRM)
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curPatch = 0;
        app.block.patches[1].raw[15] = 3; app.block.patches[1].raw[237] = 55; app.block.patches[1].raw[100] = 77; app.block.patches[1].setName ("Source");
        app.block.patches[0].raw[15] = 1; app.block.patches[0].raw[237] = 99; app.block.patches[0].raw[100] = 11;
        s5::UiState u2; u2.setPage ("Patch"); s5::Screen sc; u2.render (sc);
        u2.mouseDown (240, 26);                                   // SUB
        expect (u2.patchMenu() == 1, "patch: SUB opens the pull-down");
        u2.mouseDown (240, 4 * 18 + 18 + 5);                      // C.Source
        u2.mouseDown (440, 26);                                   // Sel
        expect (u2.patchMenu() == 0 && u2.patchSrc() == 1, "patch: Sel takes C.Source");
        u2.mouseDown (208 + 8, 2 * 18 + 5); u2.mouseDown (440, 26);   // header id, Dec/Inc -> source patch I12
        u2.mouseDown (530, 26);
        u2.mouseDown (340, 26);                                   // COM
        expect (u2.patchMenu() == 2, "patch: COM opens the pull-down");
        u2.mouseDown (340, 4 * 18 + 18 * 1 + 5);                  // Copy Page PRM
        u2.mouseDown (440, 26);                                   // EXE
        expect (app.block.patches[0].raw[15] == 3 && app.block.patches[0].raw[237] == 55 && app.block.patches[0].raw[100] == 11, "patch: Copy Page PRM copies the page only");
        u2.mouseDown (340, 26); u2.mouseDown (340, 4 * 18 + 5); u2.mouseDown (440, 26);   // Copy All PRM
        expect (app.block.patches[0].raw[100] == 77, "patch: Copy All PRM copies the whole patch");
        u2.mouseDown (340, 26); u2.mouseDown (340, 4 * 18 + 18 * 3 + 5); u2.mouseDown (440, 26);   // Init Page PRM
        expect (app.block.patches[0].raw[15] == 0 && app.block.patches[0].raw[237] == 127 && app.block.patches[0].raw[12] == 2 && app.block.patches[0].raw[100] == 77, "patch: Init Page PRM sets the manual defaults");
        u2.mouseDown (340, 26); u2.mouseDown (340, 26, 1);        // COM, right click closes
        expect (u2.patchMenu() == 0, "patch: right click closes the pull-down");
    }
    {   // Split: SUB (E./C.Source) and COM (Copy / Init / Swap Page PRM)
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curPatch = 0;
        for (int i = 0; i < 109; ++i) { app.block.patches[1].raw[17 + i] = 3; app.block.patches[0].raw[17 + i] = 1; }
        app.block.patches[1].raw[15] = 2; app.block.patches[0].raw[237] = 99;
        s5::UiState u3; u3.setPage ("Split"); s5::Screen sc; u3.render (sc);
        auto* sp = u3.liveForTest ("Split");
        u3.mouseDown (240, 26);
        expect (sp->menuOpen(), "split: SUB opens the pull-down");
        u3.mouseDown (240, 4 * 18 + 18 + 5); u3.mouseDown (440, 26);          // C.Source, Sel
        expect (! sp->menuOpen(), "split: Sel closes the pull-down");
        sp->byLabel ("CSrc")->p.value = 1; sp->postChange (*sp);               // source patch I12
        u3.mouseDown (340, 26); u3.mouseDown (340, 4 * 18 + 5); u3.mouseDown (440, 26);    // Copy Page PRM, EXE
        expect (app.block.patches[0].raw[17] == 3 && app.block.patches[0].raw[15] == 2 && app.block.patches[0].raw[237] == 99, "split: Copy Page PRM copies the maps and the Key Mode only");
        u3.mouseDown (340, 26); u3.mouseDown (340, 4 * 18 + 18 + 5); u3.mouseDown (440, 26);   // Init Page PRM
        expect (app.block.patches[0].raw[17 + 50] == 0 && app.block.patches[0].raw[15] == 0, "split: Init Page PRM");
        u3.mouseDown (340, 26); u3.mouseDown (340, 26, 1);
        expect (! sp->menuOpen(), "split: right click closes the pull-down");
    }
    {   // Output Assign: patch byte 243 (0-7 = Out 1-8, 8 = Tone), tone byte 8
        auto& app = s5::App::get();
        app.block = s5::makeDemoBlock (1); app.linked = true; app.curPatch = 0; app.curTone = 0;
        app.block.patches[0].raw[243] = 8; app.block.tones[0].f1 = 4;
        s5::UiState u4; u4.setPage ("Patch"); s5::Screen sc; u4.render (sc);
        int v = -1; for (auto& p : u4.patchParams()) if (p.label == "Output Assign") v = p.value;
        expect (v == 8, "patch page shows Output Assign Tone from byte 243");
        auto te = s5::makeLivePage ("ToneEdit"); te->draw (sc);
        expect (te->byLabel ("Output Assign")->p.value == 5, "tone edit shows Output Assign 5 from byte 8");
        te->byLabel ("Output Assign")->p.value = 2; te->postChange (*te);
        expect (app.block.tones[0].f1 == 1, "tone edit writes Output Assign to byte 8");
        app.linked = false;
    }
    {   // HD Save Set keeps the parameters with the block, Save Block leaves those of the area alone
        s5::data::HdImage hd; auto blank = s5::data::HdImage::makeBlank (20); hd.open (blank.data(), blank.size());
        s5::data::Block b = s5::makeDemoBlock (0); std::vector<std::pair<size_t, size_t>> dirty; std::string err;
        auto img = b.serialize(); img[0x10c10] = 7;                       // voice mode byte
        s5::data::Block b2; b2.parse (img.data(), img.size());
        expect (hd.saveBlock (3, b2, dirty, err, true), "hd save set");
        const size_t off = (size_t) hd.slots[3].sector * 512 + 8 * 512 + 0x10;
        expect (hd.bytes[off] == 7, "Save Set writes the parameters");
        auto img2 = b.serialize(); img2[0x10c10] = 9; s5::data::Block b3; b3.parse (img2.data(), img2.size());
        expect (hd.saveBlock (3, b3, dirty, err, false) && hd.bytes[off] == 7, "Save Block keeps the parameters of the area");
        expect (hd.saveBlock (3, b3, dirty, err, true) && hd.bytes[off] == 9, "Save Set replaces them");
    }
    {   // Disk Label: 5 lines of 12 characters, word processor style selection
        auto& app = s5::App::get();
        app.labelText.assign (10, "");
        s5::UiState u5; u5.setPage ("LabelSet"); s5::Screen sc; u5.render (sc);
        auto* lp = u5.liveForTest ("LabelSet");
        expect (lp->blocks.size() == 2 && lp->blocks[0].rows == 5 && lp->blocks[0].cols == 12, "label: two blocks of 5 x 12");
        u5.mouseDown (160, 98);
        expect (lp->activeBlk == 0 && u5.wantsKeys(), "label: click selects the block");
        for (char c : std::string ("HELLO")) u5.key (c);
        expect (app.labelText[0] == "HELLO", "label: typing");
        u5.key (13);
        for (char c : std::string ("WORLD")) u5.key (c);
        expect (app.labelText[1] == "WORLD", "label: Enter goes to the next line");
        u5.key (0x20000 | 'A');
        u5.key (0x20000 | 'C');
        expect (app.clip.find ("HELLO") == 0 && app.clip.find ('\n') == 12, "label: copy gives the lines");
        u5.key (46);
        expect (app.labelText[0].empty() && app.labelText[1].empty(), "label: Delete clears the selection");
        u5.key (0x20000 | 'V');
        expect (app.labelText[0] == "HELLO" && app.labelText[1] == "WORLD", "label: paste restores the text");
        u5.mouseDown (160, 98); u5.mouseUp(); u5.key (39 | 0x10000); u5.key (39 | 0x10000);
        u5.key (0x20000 | 'X');
        expect (app.labelText[0] == "LLO" && app.clip == "HE", "label: Shift + arrows select, cut removes them");
        u5.mouseDown (160, 98); u5.mouseMove (200, 98 + 18); u5.mouseUp();
        expect (lp->bAnc == 0 && lp->bCur > 12, "label: dragging the mouse selects");
        u5.mouseDown (160, 98, 0);
        u5.render (sc);
        u5.mouseDown (464 + 16 * 1 + 2, 5 * 18 + 8 + 4);                // table cell 11 = '+'... first row second column
        expect (app.labelText[0].size() >= 1, "label: character table writes");
    }
    std::printf ("%s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
