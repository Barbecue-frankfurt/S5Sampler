#pragma once
// Navigation and mouse handling for the screen.  No JUCE dependency, fully testable.
#include "Pages.h"
#include "PageImages.h"
#include "Param.h"
#include "LivePage.h"
#include "App.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace s5
{
class UiState
{
public:
    UiState();

    void render (Screen& s) const;

    // All coordinates are screen pixels (0..639, 0..449).  Return true if the picture changed.
    // dir: 0 = only select, -1 = left click on a value lowers it, +1 = right click raises it
    bool mouseDown (int x, int y, int dir = 0);
    // direction a held button should keep changing the value in (0 = nothing to repeat at this pixel)
    int holdDir (int x, int y, int dir) const;
    // Shift state: with Shift held, address fields (Start/Loop/End Point) change in steps of 10
    void setBig (bool b) { for (auto& kv : live) kv.second->bigStep = b; big = b; }
    bool mouseMove (int x, int y);
    bool mouseLeave();
    int hoverFieldIndex() const { auto* lp = liveFor (pageId); return lp ? lp->hoverField : -1; }   // for tests
    bool mouseUp();                              // releases a note held with the mouse
    void dragTo (int ch, int note);
    bool tick (int ms);                          // progress of floppy jobs; true when the picture changed
    bool wheel (int x, int y, int steps);
    // text entry for the patch name (computer keyboard): returns true when the key was used.
    // 8 = backspace, 13 = enter, 27 = escape, 37 / 39 = cursor left / right, 46 = delete, otherwise the character
    bool key (int code);
    int nameCursor() const { return nameSel ? nameCur : -1; }
    bool wantsKeys() const { if (pageId == "Patch") return nameSel; auto* lp = liveFor (pageId); return lp && lp->editing(); }

    // for tests: current value of a field on a live page (-9999 when unknown)
    int livePageValue (const std::string& page, const std::string& label) const
    { auto* lp = liveFor (page); auto* f = lp ? lp->byLabel (label) : nullptr; return f ? f->p.value : -9999; }

    // Wave Scope: true while the scope runs (the host then feeds App::scopeFeed); resets when the page is left
    bool scopeOn() { auto& a = App::get(); if (pageId != "WaveScope") { a.scopeRun = false; a.scopeAct = false; } return a.scopeRun; }

    bool midiOn() const { return pageId == "Monitor" || pageId == "Standard" || pageId == "Main" || pageId == "EightKey"; }   // the host should deliver MIDI input
    LivePage* liveForTest (const std::string& id) const { return liveFor (id); }
    const std::string& page() const { return pageId; }
    void setPage (const std::string& id);
    std::string mode() const;

    std::vector<Param>& patchParams() { return patch; }
    int selectedRow = 7;
    int patchMenu() const { return pMenu; }        // for tests
    int patchSrc() const { return pSrc; }

private:
    bool mouseDown0 (int x, int y, int dir);
    enum class Overlay { None, ModeMenu, PageMenu };

    std::vector<std::string> overlayItems() const;
    int overlayX() const { return overlay == Overlay::ModeMenu ? 48 : 144; }
    bool overlayHit (int x, int y, int& item) const;
    bool twoCols() const;
    bool utilBlank = false;                                    // the UTIL list was opened from the MODE menu: it comes on an empty page
    bool utilMenu() const { return overlay == Overlay::PageMenu && mode() == "UTIL" && utilBlank; }     // the UTIL start menu: its own page with Sel / exit
    std::vector<std::pair<std::string, std::string>> menuEntries() const;                  // name, page id ("" = not built yet)
    int menuRows() const;
    std::vector<const pages::PageImage*> pagesOfMode (const std::string& m, bool menuOnly = false) const;
    bool changeSelected (int delta);

    LivePage* liveFor (const std::string& id) const;

    mutable std::map<std::string, std::unique_ptr<LivePage>> live;
    std::string pageId = "Patch";
    mutable std::string enteredId = "Patch";
    Overlay overlay = Overlay::None;
    int gripId = -1;                             // point grabbed in a graphic field
    int dragCh = -1, dragNote = -1;              // note held with the mouse
    int hover = -1;
    bool overStep = false;                       // mouse over Dec / Inc
    int hoverRow = -1;                           // Patch page: value row under the mouse
    int hoverTab = -1;                           // top tab under the mouse (0 MODE, 1 MENU, 2 SUB, 3 COM)
    int tabAt (int x, int y) const;
    bool big = false;
    mutable std::vector<Param> patch;
    bool idSel = false;                          // the patch id cell (I27) is selected
    int idLocal = 0;                             // patch number when no disk is loaded
    int tblHover = -1;                           // cell of the character table under the mouse
    static bool idHit (int x, int y) { return x >= 32 && x < 80 && y >= 3 * 18 - 1 && y < 4 * 18; }
    std::string patchId() const;
    // SUB (E.Source / C.Source) and COM (Copy / Init / Swap, All / Page PRM) of the Patch page
    int pMenu = 0, pSel = 0, pSrc = -1, pC = 0;     // open pull-down (0 none, 1 SUB, 2 COM), lit entry, header (-1 none, 0 E., 1 C.), source patch
    bool hdrSel = false;                             // the patch id in the header is the field Dec / Inc change
    std::vector<std::string> pItems() const;
    bool pHit (int x, int y, int& item) const;
    void pExecute (int cmd);
    std::string srcId() const;
    bool idChange (int delta);
    void pullPatch() const;                      // block -> parameters (confirmed bytes only)
    void pushPatch();                            // parameters -> block
    std::string patchName = "The Deep";
    bool nameSel = false;                        // the black name box on the Patch page is selected
    int nameCur = 0;                             // character under the cursor (0-11)
    bool nameBox (int x, int y, int& ch) const;  // pixel in the name box? (ch = character index)
    int nameLen() const;                         // number of characters before the trailing blanks
    std::string nameText() const;                // 12 characters, from the disk when one is loaded
    void storeName (const std::string&);
    bool nameChange (int delta);
};
}
