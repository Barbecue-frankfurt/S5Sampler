#pragma once
// A page made of editable fields.  Pages are described as data (fields, static texts) plus
// optional extra drawing (keyboard, waveform, envelope).  All pixels still go through s5::Screen.
#include "Param.h"
#include "PixelScreen.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace s5
{
struct StaticText
{
    int row; double col, w; std::string text; Col fg = White; Align align = Align::Left;
    int bg = -1; int dy = 1;
};

struct Field
{
    Param p;
    int row = 0; double col = 0, w = 0;          // value cell
    Align align = Align::Right;
    Col fg = White;                              // colour when not selected
    std::string label; double lcol = 0, lw = 0;  // optional label (yellow); label width 0 = none
    bool lblHit = true;
    int dy = -1;                                 // text offset while not selected (-1 = default)
    int selBg = -1; Col selFg = Ink;             // look when selected (default cyan bar, ink text)
    bool shadow = false;                         // black drop shadow behind the selected bar
    int hideWhen = -1; const char* hideBy = nullptr;   // value hidden while field <hideBy> has value <hideWhen>
};

// A black text input box (names).  Selected like a field; then the cursor character is changed with
// Dec/Inc / right and left click / wheel, or by typing on the computer keyboard.
struct TextBox
{
    int row = 0; double col = 0;
    int len = 8;                                  // characters the name has
    std::function<std::string()> get;             // current text (any length)
    std::function<void (const std::string&)> set;
    int cur = 0;                                  // cursor character
};

// A block of text lines (Disk Label): cols x rows characters, selectable like in a word processor
// (mouse drag, Shift + arrows, Ctrl / Cmd + A C X V).  Key codes with modifiers: + kKeyShift / + kKeyCtrl.
constexpr int kKeyShift = 0x10000, kKeyCtrl = 0x20000;
struct TextBlock
{
    int row = 0; double col = 0;
    int cols = 12, rows = 5;
    std::function<std::string()> get;             // cols * rows characters
    std::function<void (const std::string&)> set;
};

class LivePage
{
public:
    int tableDx = 0;                               // character table: columns moved sideways
    std::vector<TextBlock> blocks;
    int activeBlk = -1, bCur = 0, bAnc = 0;        // selected block, cursor, other end of the selection
    bool bDrag = false;                            // the mouse button is down inside a block
    bool editing() const { return activeBox >= 0 || activeBlk >= 0; }
    bool blockHit (int x, int y, int& idx, int& pos) const;
    bool blockDrag (int x, int y);                 // true when the picture changed
    bool blockKey (int code);
    bool blockTable (int cell);                    // character table click
    std::vector<TextBox> boxes;
    int tblHover = -1;                            // cell of the character table under the mouse
    bool tableMove (int x, int y);                // hover; true when the picture changed
    int activeBox = -1;                          // selected text box, -1 = a field is selected
    std::string id, title;
    bool popupButtons = false;
    std::vector<std::string> btnNames;           // texts for SUB, COM, left, right button ("" = default, " " = blank)
    bool comRed = false;
    bool subRed = false;                         // SUB button lettering in red (sampling)
    int shift = 4;
    int bodyRow = 0, bodyShift = 0;              // rows from bodyRow on are drawn bodyShift px lower (header row stays)
    int shiftFor (int row) const { if (row == 2) return 0;   /* header line of the SUB / COM menus sits under the buttons, above the panel */ return shift + (bodyRow > 0 && row >= bodyRow ? bodyShift : 0); }
    std::vector<StaticText> before;              // drawn first (headers)
    std::vector<Field> fields;
    std::vector<StaticText> after;               // drawn after fields (e.g. name field)
    int selected = 0;
    bool bigStep = false;                        // Shift held: fields with p.fast > 1 change in bigger steps
    std::function<void (Screen&, const LivePage&)> extras;   // drawn last, shift 0
    // pages without value fields (popup menus): own click / change handling and the Sel / exit buttons
    // return 1 = handled, changed picture; 0 = handled, nothing changed; -1 = not handled, use the normal field behaviour
    // 2 = a menu entry was hit (it is now the selected one): a click on it activates it, moving over it only selects it
    bool itemHit = false;
    std::function<bool()> menuOpen;              // a pull-down is open on this page (besides popupButtons pages)
    // graphic field (envelope / waveform): arrow cursor inside, points can be grabbed and dragged with the mouse
    int gx = 0, gy = 0, gw = 0, gh = 0;
    double graphScale = 0;                                   // > 0: scale frozen while a point is held (set by graphGrab)
    bool showSel = false;                                    // mouse over Dec / Inc: show the chosen value
    int hoverField = -1, hoverGraph = -1;                    // mouse over a changeable value / a graph point
    std::function<void()> hoverOut;                          // the mouse is outside the graphic field
    std::function<int (int, int)> graphAt;                   // id of the point under the mouse (no side effects)
    bool hoverMove (int x, int y);                           // true when the picture changed
    std::function<int (int, int)> graphGrab;                 // x, y -> id of the point under the mouse or -1
    std::function<void (LivePage&, int, int, int)> graphDrag; // page, id, x, y
    bool inGraph (int x, int y) const { return gw > 0 && x >= gx && x < gx + gw && y >= gy && y < gy + gh; }
    bool setValue (const std::string& label, int v);          // sets a field like Dec/Inc would (clamped, postChange when it changed)
    std::function<bool (int, int, int&, int&)> keyAt;        // x, y -> channel, note of a keyboard key under the mouse (PLAY pages)
    std::function<int (int, int, int)> clickHook;            // x, y, dir
    std::function<int (int)> changeHook;                     // Dec/Inc/wheel
    std::function<void (LivePage&)> refresh;                 // called before every draw (lists that follow the files)
    std::function<void()> onLeave;                           // another page was opened
    std::function<void()> onEnter;                           // the page was just entered (e.g. open the Finder at once)
    std::function<bool()> subHook;                           // SUB button pressed
    std::function<bool()> buttonsActive;                     // when set: buttonHook only serves Sel / exit while this is true
    std::function<bool()> comHook;                           // COM button pressed; true when the picture changed
    bool showSub = false, showCom = false;                   // the button is labelled although the page has no hook for it (photos of the original)
    std::vector<std::string> frameNames() const;             // btnNames; SUB / COM without a function stay blank (white)
    std::function<std::string (int)> buttonHook;             // 0 = Sel, 1 = exit; returns the page to open ('' = stay)
    std::function<void (LivePage&)> postChange;              // after a value really changed (e.g. the Map pages switch parameter)
    std::function<void (const std::string&)> openToneMap;   // opens the big memory view (SUB) for the tone field with this label
    std::function<void (LivePage&)> preChange;               // e.g. set the step size from another field

    void draw (Screen& s) const;
    bool hitSelected (int x, int y) const;                  // is the pixel on the field that is selected right now?
    bool hit (int x, int y) const;
    bool boxChar (int x, int y, int& idx, int& ch) const;   // pixel in a text box? which box and character
    bool boxKey (int code);                                  // computer keyboard while a box is active
    int boxLen (int idx) const;                              // characters before trailing blanks                          // is there a field at this pixel?
    bool click (int x, int y, int dir = 0);                 // selects a field; true when selection changed
    bool change (int dir);                       // Dec/Inc/wheel on the selected field
    Field* byLabel (const std::string& l);
    const Field* byLabel (const std::string& l) const;
};

// Builds the live page for a page id, or nullptr when the page is still an image.
std::unique_ptr<LivePage> makeLivePage (const std::string& id);
void drawEditHeld (Screen& s, int oct = 0);          // pressed keys on the edit-page keyboards
}
