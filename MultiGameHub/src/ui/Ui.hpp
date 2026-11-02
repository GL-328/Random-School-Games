// Immediate-mode UI: tracks every touch pointer (true multi-touch), provides
// tap/press detection, scroll views with momentum, and themed widgets.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Gfx.hpp"
#include "Icons.hpp"
#include "Renderer2D.hpp"
#include "Theme.hpp"

namespace mgh {

struct PointerEvent {
    enum class Type { Down, Move, Up, Cancel };
    Type type;
    uint64_t id;
    float x, y;     // pixels
    double t = 0;   // event time in seconds (used for fling velocity)
};

struct ScrollState {
    float y = 0;
    float vel = 0;
    float content = 0;
    float viewport = 0;
    uint64_t dragId = 0;
    bool dragging = false;
    float anchorPointerY = 0;  // finger y when the drag started
    float anchorScroll = 0;    // unclamped scroll offset when the drag started
};

enum class ButtonStyle { Primary, Secondary, Ghost, Danger, Success };

class Ui {
public:
    struct Pointer {
        uint64_t id = 0;
        float x = 0, y = 0;    // current
        float sx = 0, sy = 0;  // where it went down
        bool down = false;
        bool pressedNow = false;   // went down this frame
        bool releasedNow = false;  // went up this frame
        bool dragged = false;      // moved beyond the tap slop
        bool consumed = false;     // claimed by a scroll view / game
        // Recent (time, y) samples for velocity estimation.
        double st[8] = {};
        float sy8[8] = {};
        int ns = 0;
        void sample(double t, float yy) {
            if (ns == 8) {
                for (int i = 1; i < 8; ++i) { st[i - 1] = st[i]; sy8[i - 1] = sy8[i]; }
                ns = 7;
            }
            st[ns] = t;
            sy8[ns] = yy;
            ++ns;
        }
        // Vertical velocity in px/s over the last ~100 ms of movement.
        float velocityY() const;
    };

    Ui(Renderer2D& r) : r_(r) {}

    void setTheme(const Theme& t) { theme_ = t; }
    const Theme& theme() const { return theme_; }
    void setScale(float dp) { dp_ = dp; }
    float dp() const { return dp_; }
    float px(float dpValue) const { return dpValue * dp_; }
    Renderer2D& r() { return r_; }

    // Event pump.
    void feed(const PointerEvent& e);
    void beginFrame(float dt);
    void endFrame();
    float dt() const { return dt_; }

    // Input gating for modal layers.
    void setInputEnabled(bool on) { inputEnabled_ = on; }
    bool inputEnabled() const { return inputEnabled_; }

    // Hit testing (rect in current content coordinates).
    bool tapped(const Rect& r);
    bool pressing(const Rect& r) const;
    bool anyPointerDown() const;
    const std::vector<Pointer>& pointers() const { return pointers_; }

    // Request continuous rendering for this frame (animations).
    void animate() { animating_ = true; }
    bool wantsAnimation() const { return animating_; }

    // ---- scroll view: content drawn between begin/end is translated by -scroll
    void scrollBegin(ScrollState& s, const Rect& viewport);
    void scrollEnd(ScrollState& s, float contentHeight);
    bool inScroll() const { return scrollDepth_ > 0; }
    float scrollY() const { return scrollOffset_; }  // add to a screen y to get content y

    // ---- widgets
    bool button(const Rect& r, const std::string& label, ButtonStyle style = ButtonStyle::Primary,
                Icon icon = Icon::None, bool enabled = true);
    bool iconButton(const Rect& r, Icon icon, Color fg, Color bg = Color(0, 0, 0, 0), float iconScale = 0.55f);
    bool toggle(const Rect& r, bool& value, float& anim);
    void progressBar(const Rect& r, float progress, Color fg);  // progress < 0 => indeterminate
    void badge(float cx, float cy, const std::string& text, Color bg, Color fg);
    void pill(const Rect& r, const std::string& text, Color bg, Color fg);

    float time() const { return time_; }

private:
    bool hit(const Rect& r, float x, float y) const;

    Renderer2D& r_;
    Theme theme_ = Theme::make(true);
    float dp_ = 1.0f;
    float dt_ = 0;
    float time_ = 0;
    bool inputEnabled_ = true;
    bool animating_ = false;
    std::vector<Pointer> pointers_;
    // Active scroll translation for hit tests.
    float scrollOffset_ = 0;
    float savedOffX_ = 0, savedOffY_ = 0;
    Rect scrollClip_;
    int scrollDepth_ = 0;
};

}  // namespace mgh
