#include "Icons.hpp"

#include <cmath>

#include "Renderer2D.hpp"

namespace mgh {

namespace {
constexpr float kPi = 3.14159265358979f;
}

void drawIcon(Renderer2D& r, Icon icon, const Rect& box, Color c) {
    const float s = std::min(box.w, box.h);
    const float cx = box.cx(), cy = box.cy();
    const float t = std::max(1.5f, s * 0.09f);  // stroke width
    auto P = [&](float x, float y) { return Vec2f{cx + x * s, cy + y * s}; };
    auto L = [&](float x0, float y0, float x1, float y1) {
        Vec2f a = P(x0, y0), b = P(x1, y1);
        r.line(a.x, a.y, b.x, b.y, t, c);
    };
    switch (icon) {
        case Icon::None: break;
        case Icon::Home:
            L(-0.38f, -0.02f, 0.0f, -0.36f);
            L(0.0f, -0.36f, 0.38f, -0.02f);
            L(-0.28f, -0.08f, -0.28f, 0.34f);
            L(0.28f, -0.08f, 0.28f, 0.34f);
            L(-0.28f, 0.34f, 0.28f, 0.34f);
            r.fillRoundRect({cx - s * 0.08f, cy + s * 0.08f, s * 0.16f, s * 0.26f}, s * 0.03f, c);
            break;
        case Icon::Grid: {
            float q = s * 0.3f, g = s * 0.08f;
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    r.fillRoundRect({cx - q - g * 0.5f + i * (q + g), cy - q - g * 0.5f + j * (q + g), q, q}, s * 0.07f, c);
            break;
        }
        case Icon::Download:
            L(0, -0.36f, 0, 0.14f);
            L(-0.2f, -0.05f, 0, 0.15f);
            L(0.2f, -0.05f, 0, 0.15f);
            L(-0.34f, 0.2f, -0.34f, 0.34f);
            L(-0.34f, 0.34f, 0.34f, 0.34f);
            L(0.34f, 0.34f, 0.34f, 0.2f);
            break;
        case Icon::Gear: {
            r.strokeCircle(cx, cy, s * 0.2f, t, c);
            for (int i = 0; i < 8; ++i) {
                float a = i * kPi / 4;
                Vec2f a0 = P(std::cos(a) * 0.27f, std::sin(a) * 0.27f), a1 = P(std::cos(a) * 0.4f, std::sin(a) * 0.4f);
                r.line(a0.x, a0.y, a1.x, a1.y, t * 1.5f, c, false);
            }
            r.strokeCircle(cx, cy, s * 0.29f, t, c);
            break;
        }
        case Icon::Back:
            L(0.12f, -0.3f, -0.18f, 0);
            L(-0.18f, 0, 0.12f, 0.3f);
            break;
        case Icon::ChevronRight:
            L(-0.08f, -0.24f, 0.14f, 0);
            L(0.14f, 0, -0.08f, 0.24f);
            break;
        case Icon::Pause:
            r.fillRoundRect({cx - s * 0.24f, cy - s * 0.3f, s * 0.16f, s * 0.6f}, s * 0.04f, c);
            r.fillRoundRect({cx + s * 0.08f, cy - s * 0.3f, s * 0.16f, s * 0.6f}, s * 0.04f, c);
            break;
        case Icon::Play:
            r.fillTriangle(P(-0.2f, -0.32f), P(0.32f, 0), P(-0.2f, 0.32f), c);
            break;
        case Icon::Restart:
        case Icon::Refresh: {
            r.fillArc(cx, cy, s * 0.28f, t, -kPi * 0.35f, kPi * 1.45f, c);
            float a = -kPi * 0.35f;
            Vec2f tip = P(std::cos(a) * 0.28f, std::sin(a) * 0.28f);
            r.fillTriangle({tip.x - s * 0.14f, tip.y - s * 0.06f}, {tip.x + s * 0.1f, tip.y - s * 0.16f},
                           {tip.x + s * 0.06f, tip.y + s * 0.12f}, c);
            break;
        }
        case Icon::Undo: {
            r.fillArc(cx + s * 0.02f, cy + s * 0.06f, s * 0.24f, t, -kPi * 0.5f, kPi * 0.5f, c);
            L(-0.12f, -0.18f, 0.02f, -0.18f);
            L(-0.12f, 0.3f, 0.02f, 0.3f);
            r.fillTriangle(P(-0.32f, -0.18f), P(-0.1f, -0.34f), P(-0.1f, -0.02f), c);
            break;
        }
        case Icon::List:
            for (int i = -1; i <= 1; ++i) {
                r.fillCircle(cx - s * 0.28f, cy + i * s * 0.22f, t * 0.8f, c);
                L(-0.14f, i * 0.22f, 0.34f, i * 0.22f);
            }
            break;
        case Icon::Trash:
            L(-0.32f, -0.22f, 0.32f, -0.22f);
            L(-0.1f, -0.32f, 0.1f, -0.32f);
            L(-0.22f, -0.16f, -0.18f, 0.34f);
            L(0.22f, -0.16f, 0.18f, 0.34f);
            L(-0.18f, 0.34f, 0.18f, 0.34f);
            L(-0.05f, -0.08f, -0.05f, 0.24f);
            L(0.07f, -0.08f, 0.07f, 0.24f);
            break;
        case Icon::Check:
            L(-0.3f, 0.02f, -0.08f, 0.24f);
            L(-0.08f, 0.24f, 0.32f, -0.2f);
            break;
        case Icon::Close:
            L(-0.25f, -0.25f, 0.25f, 0.25f);
            L(0.25f, -0.25f, -0.25f, 0.25f);
            break;
        case Icon::Storage:
            for (int i = 0; i < 3; ++i) {
                Rect rr{cx - s * 0.34f, cy - s * 0.34f + i * s * 0.24f, s * 0.68f, s * 0.2f};
                r.strokeRoundRect(rr, s * 0.05f, t * 0.9f, c);
                r.fillCircle(rr.right() - s * 0.1f, rr.cy(), t * 0.7f, c);
            }
            break;
        case Icon::Info:
            r.strokeCircle(cx, cy, s * 0.36f, t, c);
            L(0, -0.04f, 0, 0.2f);
            r.fillCircle(cx, cy - s * 0.17f, t * 0.75f, c);
            break;
        case Icon::Sound:
            r.fillConvex({P(-0.34f, -0.1f), P(-0.16f, -0.1f), P(0.04f, -0.3f), P(0.04f, 0.3f), P(-0.16f, 0.1f),
                          P(-0.34f, 0.1f)},
                         c);
            r.fillArc(cx + s * 0.04f, cy, s * 0.17f, t, -kPi * 0.3f, kPi * 0.3f, c);
            r.fillArc(cx + s * 0.04f, cy, s * 0.31f, t, -kPi * 0.3f, kPi * 0.3f, c);
            break;
        case Icon::Music:
            L(-0.12f, 0.2f, -0.12f, -0.28f);
            L(0.26f, 0.12f, 0.26f, -0.36f);
            L(-0.12f, -0.28f, 0.26f, -0.36f);
            r.fillCircle(cx - s * 0.2f, cy + s * 0.22f, s * 0.1f, c);
            r.fillCircle(cx + s * 0.18f, cy + s * 0.14f, s * 0.1f, c);
            break;
        case Icon::Vibrate:
            r.strokeRoundRect({cx - s * 0.14f, cy - s * 0.3f, s * 0.28f, s * 0.6f}, s * 0.06f, t, c);
            L(-0.3f, -0.12f, -0.3f, 0.12f);
            L(0.3f, -0.12f, 0.3f, 0.12f);
            L(-0.4f, -0.06f, -0.4f, 0.06f);
            L(0.4f, -0.06f, 0.4f, 0.06f);
            break;
        case Icon::Moon:
            r.fillCircle(cx, cy, s * 0.3f, c);
            break;
        case Icon::Sun:
            r.fillCircle(cx, cy, s * 0.16f, c);
            for (int i = 0; i < 8; ++i) {
                float a = i * kPi / 4;
                L(std::cos(a) * 0.26f, std::sin(a) * 0.26f, std::cos(a) * 0.36f, std::sin(a) * 0.36f);
            }
            break;
        case Icon::Star: {
            std::vector<Vec2f> pts;
            for (int i = 0; i < 10; ++i) {
                float a = -kPi / 2 + i * kPi / 5;
                float rr = (i % 2 == 0) ? 0.38f : 0.16f;
                pts.push_back(P(std::cos(a) * rr, std::sin(a) * rr));
            }
            // Star is concave: draw as 5 triangles around the inner pentagon.
            std::vector<Vec2f> inner;
            for (int i = 1; i < 10; i += 2) inner.push_back(pts[i]);
            r.fillConvex(inner, c);
            for (int i = 0; i < 10; i += 2) r.fillTriangle(pts[(i + 9) % 10], pts[i], pts[i + 1], c);
            break;
        }
        case Icon::Trophy:
            r.fillConvex({P(-0.24f, -0.32f), P(0.24f, -0.32f), P(0.18f, 0.0f), P(0.0f, 0.1f), P(-0.18f, 0.0f)}, c);
            r.fillArc(cx - s * 0.24f, cy - s * 0.16f, s * 0.1f, t * 0.8f, kPi * 0.5f, kPi * 1.5f, c);
            r.fillArc(cx + s * 0.24f, cy - s * 0.16f, s * 0.1f, t * 0.8f, -kPi * 0.5f, kPi * 0.5f, c);
            L(0, 0.1f, 0, 0.24f);
            r.fillRoundRect({cx - s * 0.18f, cy + s * 0.22f, s * 0.36f, s * 0.1f}, s * 0.03f, c);
            break;
        case Icon::Flag:
            L(-0.24f, -0.34f, -0.24f, 0.36f);
            r.fillConvex({P(-0.22f, -0.34f), P(0.3f, -0.2f), P(-0.22f, -0.04f)}, c);
            break;
        case Icon::Box:
            r.strokeRoundRect({cx - s * 0.32f, cy - s * 0.24f, s * 0.64f, s * 0.54f}, s * 0.06f, t, c);
            L(-0.32f, -0.06f, 0.32f, -0.06f);
            L(-0.08f, 0.08f, 0.08f, 0.08f);
            break;
        case Icon::Clock:
            r.strokeCircle(cx, cy, s * 0.34f, t, c);
            L(0, 0, 0, -0.2f);
            L(0, 0, 0.14f, 0.1f);
            break;
    }
}

}  // namespace mgh
