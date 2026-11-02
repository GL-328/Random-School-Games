#include "Ui.hpp"

#include <cmath>

namespace mgh {

void Ui::feed(const PointerEvent& e) {
    Pointer* p = nullptr;
    for (auto& q : pointers_)
        if (q.id == e.id && (q.down || e.type != PointerEvent::Type::Down)) p = &q;
    switch (e.type) {
        case PointerEvent::Type::Down: {
            Pointer np;
            np.id = e.id;
            np.x = np.sx = e.x;
            np.y = np.sy = e.y;
            np.down = true;
            np.pressedNow = true;
            np.sample(e.t, e.y);
            pointers_.push_back(np);
            break;
        }
        case PointerEvent::Type::Move:
            if (p && p->down) {
                p->x = e.x;
                p->y = e.y;
                p->sample(e.t, e.y);
                if (std::hypot(p->x - p->sx, p->y - p->sy) > px(10)) p->dragged = true;
            }
            break;
        case PointerEvent::Type::Up:
        case PointerEvent::Type::Cancel:
            if (p && p->down) {
                p->x = e.x;
                p->y = e.y;
                p->sample(e.t, e.y);
                if (std::hypot(p->x - p->sx, p->y - p->sy) > px(10)) p->dragged = true;
                p->down = false;
                p->releasedNow = true;
                if (e.type == PointerEvent::Type::Cancel) p->consumed = true;
            }
            break;
    }
}

void Ui::beginFrame(float dt) {
    dt_ = dt;
    time_ += dt;
    animating_ = anyPointerDown();
    scrollOffset_ = 0;
    scrollDepth_ = 0;
}

void Ui::endFrame() {
    // Drop pointers that were released; clear per-frame flags.
    std::vector<Pointer> keep;
    for (auto& p : pointers_) {
        if (!p.down) continue;
        p.pressedNow = false;
        keep.push_back(p);
    }
    pointers_.swap(keep);
}

bool Ui::anyPointerDown() const {
    for (const auto& p : pointers_)
        if (p.down) return true;
    return false;
}

bool Ui::hit(const Rect& r, float x, float y) const {
    if (scrollDepth_ > 0 && !scrollClip_.contains(x, y)) return false;
    return r.contains(x, y + scrollOffset_);
}

bool Ui::tapped(const Rect& r) {
    if (!inputEnabled_) return false;
    for (auto& p : pointers_) {
        if (!p.releasedNow || p.consumed || p.dragged) continue;
        if (hit(r, p.sx, p.sy) && hit(r, p.x, p.y)) {
            p.consumed = true;  // one tap activates one widget
            return true;
        }
    }
    return false;
}

bool Ui::pressing(const Rect& r) const {
    if (!inputEnabled_) return false;
    for (const auto& p : pointers_)
        if (p.down && !p.consumed && !p.dragged && hit(r, p.sx, p.sy) && hit(r, p.x, p.y)) return true;
    return false;
}

// ------------------------------------------------------------------ scroll

float Ui::Pointer::velocityY() const {
    if (ns < 2) return 0;
    const double tEnd = st[ns - 1];
    int i0 = ns - 1;
    while (i0 > 0 && tEnd - st[i0 - 1] <= 0.1) --i0;
    const double dt = tEnd - st[i0];
    if (dt < 0.005) return 0;
    return static_cast<float>((sy8[ns - 1] - sy8[i0]) / dt);
}

void Ui::scrollBegin(ScrollState& s, const Rect& vp) {
    s.viewport = vp.h;
    const float maxY = std::max(0.0f, s.content - s.viewport);
    // Overscroll shows with resistance (rubber band) and springs back afterwards.
    auto rubber = [&](float raw) {
        if (raw < 0) return raw * 0.35f;
        if (raw > maxY) return maxY + (raw - maxY) * 0.35f;
        return raw;
    };
    if (inputEnabled_ && !s.dragging) {
        for (auto& p : pointers_) {
            if (!p.down || p.consumed || !vp.contains(p.sx, p.sy)) continue;
            // Touching a list that is still flinging just stops it (no accidental tap).
            if (p.pressedNow && std::fabs(s.vel) > px(150)) {
                s.vel = 0;
                p.consumed = true;
                break;
            }
            const float dy = p.y - p.sy, dx = p.x - p.sx;
            if (std::fabs(dy) > px(8) && std::fabs(dy) > std::fabs(dx)) {
                s.dragging = true;
                s.dragId = p.id;
                s.anchorPointerY = p.y;  // start from here: no jump when the drag begins
                s.anchorScroll = s.y;
                s.vel = 0;
                p.consumed = true;
                p.dragged = true;
                break;
            }
        }
    }
    if (s.dragging) {
        const Pointer* dp = nullptr;
        for (const auto& p : pointers_)
            if (p.id == s.dragId) dp = &p;
        if (!dp || !inputEnabled_) {
            s.dragging = false;
        } else {
            // Content follows the finger exactly.
            const float raw = s.anchorScroll - (dp->y - s.anchorPointerY);
            s.y = rubber(raw);
            if (!dp->down) {
                s.dragging = false;
                s.vel = (s.y < 0 || s.y > maxY) ? 0.0f : std::clamp(-dp->velocityY(), -px(6000), px(6000));
            }
        }
    }
    if (!s.dragging) {
        if (s.y < 0 || s.y > maxY) {
            // Spring back after an overscroll.
            const float target = s.y < 0 ? 0.0f : maxY;
            s.vel = 0;
            s.y = approach(s.y, target, 16, dt_);
            if (std::fabs(s.y - target) < 0.5f) s.y = target;
        } else if (s.vel != 0) {
            s.y += s.vel * dt_;
            s.vel *= std::exp(-3.2f * dt_);  // gentle deceleration
            if (s.y < 0 || s.y > maxY) {
                s.y = std::clamp(s.y, 0.0f, maxY);
                s.vel = 0;
            }
            if (std::fabs(s.vel) < px(12)) s.vel = 0;
        }
    }
    if (s.dragging || s.vel != 0 || s.y < 0 || s.y > maxY) animate();

    savedOffX_ = r_.offsetX();
    savedOffY_ = r_.offsetY();
    r_.pushClip(vp);
    r_.setOffset(savedOffX_, savedOffY_ - std::round(s.y));
    scrollOffset_ = std::round(s.y);
    scrollClip_ = vp;
    ++scrollDepth_;
}

void Ui::scrollEnd(ScrollState& s, float contentHeight) {
    s.content = contentHeight;
    r_.setOffset(savedOffX_, savedOffY_);
    r_.popClip();
    scrollOffset_ = 0;
    --scrollDepth_;
    // Scroll indicator.
    if (s.content > s.viewport + 1 && (s.dragging || s.vel != 0)) {
        float frac = s.viewport / s.content;
        float barH = std::max(px(24), s.viewport * frac);
        float maxY = s.content - s.viewport;
        float t = maxY > 0 ? std::clamp(s.y / maxY, 0.0f, 1.0f) : 0;
        Rect clip = scrollClip_;
        r_.fillRoundRect({clip.right() - px(4), clip.y + t * (clip.h - barH), px(3), barH}, px(1.5f),
                         theme_.textFaint.alpha(0.6f));
    }
}

// ------------------------------------------------------------------ widgets

bool Ui::button(const Rect& rc, const std::string& label, ButtonStyle style, Icon icon, bool enabled) {
    const bool down = enabled && pressing(rc);
    Color bg, fg;
    switch (style) {
        case ButtonStyle::Primary: bg = theme_.accent; fg = theme_.accentText; break;
        case ButtonStyle::Secondary: bg = theme_.surface2; fg = theme_.text; break;
        case ButtonStyle::Ghost: bg = Color(0, 0, 0, 0); fg = theme_.accent; break;
        case ButtonStyle::Danger: bg = theme_.danger; fg = Color::hex(0xFFFFFF); break;
        case ButtonStyle::Success: bg = theme_.success; fg = Color::hex(0x0B2018); break;
    }
    if (!enabled) {
        bg = theme_.surface2.alpha(0.6f);
        fg = theme_.textFaint;
    }
    if (down) bg = style == ButtonStyle::Ghost ? theme_.accentSoft : bg.darken(theme_.dark ? 0.18f : 0.1f);
    Rect drawn = down ? rc.inset(px(1)) : rc;
    const float radius = std::min(drawn.h * 0.5f, px(14));
    if (bg.a > 0) {
        r_.fillRoundRect(drawn, radius, bg);
    }
    const float fontPx = std::min(px(15), rc.h * 0.42f);
    if (icon != Icon::None) {
        const float iconSize = fontPx * 1.35f;
        float tw = label.empty() ? 0 : r_.measure(label, fontPx, FontId::Bold);
        float gap = label.empty() ? 0 : px(8);
        float total = iconSize + gap + tw;
        float x0 = rc.cx() - total * 0.5f;
        drawIcon(r_, icon, {x0, rc.cy() - iconSize * 0.5f, iconSize, iconSize}, fg);
        if (!label.empty())
            r_.textIn(label, {x0 + iconSize + gap, rc.y, tw, rc.h}, fontPx, fg, FontId::Bold, Align::Left);
    } else {
        std::string text = r_.elide(label, rc.w - px(16), fontPx, FontId::Bold);
        r_.textIn(text, rc, fontPx, fg, FontId::Bold, Align::Center);
    }
    return enabled && tapped(rc);
}

bool Ui::iconButton(const Rect& rc, Icon icon, Color fg, Color bg, float iconScale) {
    const bool down = pressing(rc);
    if (bg.a > 0 || down) {
        Color b = bg.a > 0 ? bg : theme_.surfacePressed;
        if (down && bg.a > 0) b = b.lighten(0.12f);
        r_.fillRoundRect(rc, std::min(rc.w, rc.h) * 0.5f, b);
    }
    float s = std::min(rc.w, rc.h) * iconScale;
    drawIcon(r_, icon, {rc.cx() - s * 0.5f, rc.cy() - s * 0.5f, s, s}, fg);
    return tapped(rc);
}

bool Ui::toggle(const Rect& rc, bool& value, float& anim) {
    bool changed = false;
    if (tapped(rc)) {
        value = !value;
        changed = true;
    }
    float target = value ? 1.0f : 0.0f;
    anim = approach(anim, target, 16, dt_);
    if (std::fabs(anim - target) > 0.01f) animate();
    else anim = target;
    // Switch is drawn at the right edge of rc.
    const float h = px(28), w = px(48);
    Rect track{rc.right() - w, rc.cy() - h * 0.5f, w, h};
    r_.fillRoundRect(track, h * 0.5f, theme_.surface2.lerp(theme_.accent, anim));
    float knobR = h * 0.5f - px(3);
    float kx = track.x + h * 0.5f + (w - h) * anim;
    r_.fillCircle(kx, track.cy(), knobR, Color::hex(0xFFFFFF));
    return changed;
}

void Ui::progressBar(const Rect& rc, float progress, Color fg) {
    r_.fillRoundRect(rc, rc.h * 0.5f, theme_.surface2);
    if (progress >= 0) {
        float w = std::max(rc.h, rc.w * std::clamp(progress, 0.0f, 1.0f));
        r_.fillRoundRect({rc.x, rc.y, w, rc.h}, rc.h * 0.5f, fg);
    } else {
        // Indeterminate: a sliding segment.
        animate();
        float t = std::fmod(time_ * 0.8f, 1.0f);
        float segW = rc.w * 0.3f;
        float x = rc.x - segW + (rc.w + segW) * t;
        float x0 = std::max(rc.x, x), x1 = std::min(rc.right(), x + segW);
        if (x1 > x0) r_.fillRoundRect({x0, rc.y, x1 - x0, rc.h}, rc.h * 0.5f, fg);
    }
}

void Ui::badge(float cx, float cy, const std::string& text, Color bg, Color fg) {
    const float fpx = px(10);
    float w = std::max(px(16), r_.measure(text, fpx, FontId::Bold) + px(8));
    Rect rc{cx - w * 0.5f, cy - px(8), w, px(16)};
    r_.fillRoundRect(rc, px(8), bg);
    r_.textIn(text, rc, fpx, fg, FontId::Bold);
}

void Ui::pill(const Rect& rc, const std::string& text, Color bg, Color fg) {
    r_.fillRoundRect(rc, rc.h * 0.5f, bg);
    r_.textIn(text, rc, std::min(px(11.5f), rc.h * 0.55f), fg, FontId::Bold);
}

}  // namespace mgh
