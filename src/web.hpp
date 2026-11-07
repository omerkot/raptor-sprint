// What only the browser version needs: its size on the page, handing each frame back to the browser,
// and playing on a touch screen. The page (packaging/web/index.html) passes on every finger; here
// they become a steering stick, buttons, and taps on the menus.
#pragma once
#if defined(__EMSCRIPTEN__)
#include "menu.hpp"
#include "player.hpp"

namespace rr {

enum class TouchMode { Menu, Race, Tap, Finish };  // (Tap: anywhere goes on)

int web_view_width();
int web_view_height();
// Lets the browser show what was drawn, and comes back when it's time for the next frame.
void web_wait_frame();
// The part of a rectangle of the screen clear of the notch and the rounded corners.
Rectangle web_safe_area(Rectangle r);
// A phone held upright: enough height of view for a useful width of forest.
float web_upright_fovy(float fovy, float aspect);

// Once a frame, before anything reads the controls.
void touch_update(TouchMode mode, Rectangle area);
// A menu row under a finger (select it), or -1.
int touch_row();
// What was tapped this frame, for read_menu_input. (Stays for the frame, as a key press does.)
MenuInput touch_menu_input();
void touch_controls(Controls& in);
// How the stick answers: turn speed at full lock (%), the drag to full lock (points), the softness
// round the middle (0 a straight line .. 10), the dead zone (%), and smoothing (0 none .. 10).
void touch_steering(int speed, int travel, int curve, int dead, int smooth);
// The menu rows as drawn, to know what a tap lands on. `less` and `more` are the middles of the
// < and > beside a value; 0 when the row has none.
void touch_menu_clear();
void touch_menu_row(Rectangle row, float less, float more);
// The controls over the view. `hint` is the finish card's ("A  next trail      B  menu"), `alpha` how
// far it has come in.
void touch_draw(TouchMode mode, const std::string& hint, float alpha);

}  // namespace rr
#endif
