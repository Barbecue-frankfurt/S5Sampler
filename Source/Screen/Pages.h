#pragma once
#include "PixelScreen.h"
#include <string>
#include <utility>
#include <vector>

namespace s5::pages
{
// Common frame: black outer, blue panel, button row and right-aligned title.
// popupButtons: top right buttons read "Sel"/"exit" instead of "Dec"/"Inc".
void frame (Screen& s, const std::string& title, bool popupButtons = false, bool subRed = false,
           const std::vector<std::string>* names = nullptr, bool comRed = false);   // names: SUB, COM, left, right ("" = default, " " = blank)

// White keyboard panel at the bottom with split ticks and "2nd"/"1st" markers.
void keyboard (Screen& s, bool endTicksOnly = false, int oct = 0);   // oct: keyboard moved by that many octaves (labels follow)

struct PatchPrm
{
    int oct = 0;
    std::string id = "I11", name = "The Deep";
    std::vector<std::pair<std::string, std::string>> rows;
    int selected = 7;            // highlighted row, -1 for none
    int hoverRow = -1;           // value row under the mouse
    std::string nameField = "The Deep";
    bool idSelected = false;     // the patch id cell (I27) is selected
    bool showTable = false;      // character table next to the values (name box selected)
    int tableHover = -1;
    int nameCursor = -1;         // character that is being edited in the name box, -1 = box not selected
    int menu = 0;                // 0 none, 1 SUB pull-down open, 2 COM pull-down open
    int menuSel = 0;             // lit entry of the pull-down
    int src = -1;                // -1 no header, 0 "E.Source", 1 "C.Source"
    std::string srcId;           // patch shown in the header next to E.Source / C.Source
    bool srcSelected = false;    // header patch id is the field Dec / Inc change
    std::vector<std::string> menuItems;
    int menuX = 240;
};

// characters a name can be made of, in the order Inc / Dec walks through them
const char* nameChars();
// black text input box with an optional cursor (cursor < 0: none).  Text is drawn proportionally;
// the cursor covers the real glyph box of its character.
// width in pixels of a box that holds exactly `len` of the widest characters (so it never looks like more would fit)
int textBoxWidth (int len);
void textBox (Screen& s, int row, double col, int len, const std::string& text, int cursor);
// Character table for name entry (10 x 10 cells right of the values): the printable ASCII characters,
// a shaded block, I (insert a blank), D (delete a character) and the two cursor arrows.
void nameTable (Screen& s, int hoverCell);
extern int tableDx;
int nameTableHit (int x, int y);                              // cell 0-99 under the pixel or -1
bool nameTableAction (int cell, std::string& text, int& cur, int len);   // true when text or cursor changed
PatchPrm defaultPatchPrm();
void patchPrm (Screen& s, const PatchPrm& d);
}
