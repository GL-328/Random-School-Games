// Basic value types for 2D drawing.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mgh {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    constexpr Color() = default;
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
    static constexpr Color hex(uint32_t rgb, uint8_t alpha = 255) {
        return Color(static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), alpha);
    }
    Color alpha(float f) const {
        return Color(r, g, b, static_cast<uint8_t>(std::clamp(a * f, 0.0f, 255.0f)));
    }
    Color withA(uint8_t na) const { return Color(r, g, b, na); }
    Color lerp(Color o, float t) const {
        auto L = [t](uint8_t x, uint8_t y) {
            return static_cast<uint8_t>(std::clamp(x + (y - x) * t, 0.0f, 255.0f));
        };
        return Color(L(r, o.r), L(g, o.g), L(b, o.b), L(a, o.a));
    }
    Color lighten(float t) const { return lerp(Color(255, 255, 255, a), t); }
    Color darken(float t) const { return lerp(Color(0, 0, 0, a), t); }
};

struct Vec2f {
    float x = 0, y = 0;
    constexpr Vec2f() = default;
    constexpr Vec2f(float x_, float y_) : x(x_), y(y_) {}
    Vec2f operator+(Vec2f o) const { return {x + o.x, y + o.y}; }
    Vec2f operator-(Vec2f o) const { return {x - o.x, y - o.y}; }
    Vec2f operator*(float s) const { return {x * s, y * s}; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    // Splits helpers
    Rect takeTop(float hh) const { return {x, y, w, hh}; }
    Rect takeBottom(float hh) const { return {x, y + h - hh, w, hh}; }
    Rect takeLeft(float ww) const { return {x, y, ww, h}; }
    Rect takeRight(float ww) const { return {x + w - ww, y, ww, h}; }
};

inline float easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float u = 1 - t;
    return 1 - u * u * u;
}
inline float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}
// Frame-rate independent exponential approach.
inline float approach(float cur, float target, float rate, float dt) {
    return target + (cur - target) * std::exp(-rate * dt);
}

}  // namespace mgh
