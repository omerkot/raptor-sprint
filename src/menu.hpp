// Menus: a list of items, some with a value changed by left/right. Read with the d-pad or stick
// (held directions repeat), A to choose, B to go back; drawn on the lower panel, or in a box over
// the view on a single screen.
#pragma once
#include "core.hpp"
#include "regions.hpp"

namespace rr {

// The buttons labelled A and B, as the system reports them by label. With --pad-position, A is the
// east button and B the south, which is what the launchers for direct installs set up.
inline GamepadButton pad_a = GAMEPAD_BUTTON_RIGHT_FACE_DOWN, pad_b = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;

enum class MenuInput { None, Up, Down, Left, Right, Confirm, Back, Pause };
int active_pad();  // the pad in use, or -1
MenuInput read_menu_input(float dt);

struct MenuItem {
    int id;
    std::string label;
    std::string value;  // shown on the right with < > when the item can be changed
    std::string note;   // a quieter line under the label (best time, etc.)
    int claws = -1;     // claw marks shown before the value (0..3), -1 none
    bool locked = false;
    bool arrows = true;  // < > round the value when selected (it changes with left/right)
};

struct Menu {
    std::string title, subtitle, hint;
    std::vector<MenuItem> items;
    int sel = 0;
    // Moves the selection; returns true if it moved.
    bool move(int d);
    const MenuItem& current() const { return items[sel]; }
};

// The menu filling a screen (lower panel), or as a box over the view (`left`: on the left half,
// without dimming the rest, so the view on the right stays clear).
void draw_menu_panel(Rectangle r, const Menu& m, float time);
void draw_menu_overlay(Rectangle r, const Menu& m, float time, bool left = false);
// The title over the view on the upper panel.
void draw_title(Rectangle r, float time, const char* prompt);
// A region over the view while choosing: its name, what it's like, and what's new there.
void draw_region_banner(Rectangle r, const Region& rg, int index, bool open);

}  // namespace rr
