#include "Renderer2D.hpp"

#include <cmath>

namespace mgh {

namespace {
constexpr float kPi = 3.14159265358979f;
}

SDL_FColor Renderer2D::fc(Color c) const {
    return SDL_FColor{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, (c.a / 255.0f) * alpha_};
}

Vec2f Renderer2D::xf(Vec2f p) const {
    p.x += offX_;
    p.y += offY_;
    if (rot_) {
        p.x = 2 * pivX_ - p.x;
        p.y = 2 * pivY_ - p.y;
    }
    return p;
}

void Renderer2D::draw(SDL_Texture* tex) {
    if (!verts_.empty() && !idx_.empty()) {
        for (auto& v : verts_) {
            Vec2f p = xf({v.position.x, v.position.y});
            v.position = {p.x, p.y};
        }
        SDL_RenderGeometry(r_, tex, verts_.data(), static_cast<int>(verts_.size()), idx_.data(),
                           static_cast<int>(idx_.size()));
    }
    verts_.clear();
    idx_.clear();
}

int Renderer2D::segmentsFor(float radius) const {
    return std::clamp(static_cast<int>(radius / 2.5f) + 3, 3, 24);
}

void Renderer2D::pushClip(const Rect& r) {
    Vec2f a = xf({r.x, r.y});
    Vec2f b = xf({r.right(), r.bottom()});
    SDL_Rect cr{static_cast<int>(std::floor(std::min(a.x, b.x))), static_cast<int>(std::floor(std::min(a.y, b.y))),
                static_cast<int>(std::ceil(std::fabs(b.x - a.x))), static_cast<int>(std::ceil(std::fabs(b.y - a.y)))};
    if (!clips_.empty()) {
        SDL_Rect out;
        if (!SDL_GetRectIntersection(&clips_.back(), &cr, &out)) out = SDL_Rect{0, 0, 0, 0};
        cr = out;
    }
    clips_.push_back(cr);
    SDL_SetRenderClipRect(r_, &cr);
}

void Renderer2D::popClip() {
    if (!clips_.empty()) clips_.pop_back();
    SDL_SetRenderClipRect(r_, clips_.empty() ? nullptr : &clips_.back());
}

void Renderer2D::clear(Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
    SDL_RenderClear(r_);
}

void Renderer2D::fillRect(const Rect& r, Color c) { fillRectGradient(r, c, c); }

void Renderer2D::fillRectGradient(const Rect& r, Color top, Color bottom) {
    SDL_FColor t = fc(top), b = fc(bottom);
    verts_ = {{{r.x, r.y}, t, {0, 0}},
              {{r.right(), r.y}, t, {0, 0}},
              {{r.right(), r.bottom()}, b, {0, 0}},
              {{r.x, r.bottom()}, b, {0, 0}}};
    idx_ = {0, 1, 2, 0, 2, 3};
    draw(nullptr);
}

void Renderer2D::roundRectPath(const Rect& r, float radius, std::vector<PN>& out) {
    out.clear();
    radius = std::clamp(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    struct Corner {
        float cx, cy, a0;
    } corners[4] = {
        {r.x + radius, r.y + radius, kPi},
        {r.right() - radius, r.y + radius, kPi * 1.5f},
        {r.right() - radius, r.bottom() - radius, 0},
        {r.x + radius, r.bottom() - radius, kPi * 0.5f},
    };
    if (radius < 0.5f) {
        const float d = 0.70710678f * 1.41421356f;  // unit diagonal scaled so edges get 0.5px feather
        out.push_back({{r.x, r.y}, {-d, -d}});
        out.push_back({{r.right(), r.y}, {d, -d}});
        out.push_back({{r.right(), r.bottom()}, {d, d}});
        out.push_back({{r.x, r.bottom()}, {-d, d}});
        return;
    }
    const int n = std::clamp(static_cast<int>(radius / 3.0f) + 2, 2, 16);
    for (const Corner& c : corners) {
        for (int i = 0; i <= n; ++i) {
            float a = c.a0 + (kPi * 0.5f) * (static_cast<float>(i) / n);
            Vec2f nrm{std::cos(a), std::sin(a)};
            out.push_back({{c.cx + nrm.x * radius, c.cy + nrm.y * radius}, nrm});
        }
    }
}

void Renderer2D::circlePath(float cx, float cy, float radius, std::vector<PN>& out) {
    out.clear();
    const int n = segmentsFor(radius) * 4;
    for (int i = 0; i < n; ++i) {
        float a = 2 * kPi * static_cast<float>(i) / n;
        Vec2f nrm{std::cos(a), std::sin(a)};
        out.push_back({{cx + nrm.x * radius, cy + nrm.y * radius}, nrm});
    }
}

void Renderer2D::fillPathAA(const std::vector<PN>& path, Vec2f center, Color c0, Color c1, bool gradient, float top,
                            float bottom) {
    const int n = static_cast<int>(path.size());
    if (n < 3) return;
    auto colorAt = [&](float y) {
        if (!gradient || bottom <= top) return c0;
        return c0.lerp(c1, std::clamp((y - top) / (bottom - top), 0.0f, 1.0f));
    };
    verts_.clear();
    idx_.clear();
    verts_.push_back({{center.x, center.y}, fc(colorAt(center.y)), {0, 0}});
    for (const PN& pn : path) {
        Vec2f in = pn.p - pn.n * 0.5f;
        Vec2f out = pn.p + pn.n * 0.5f;
        Color col = colorAt(pn.p.y);
        verts_.push_back({{in.x, in.y}, fc(col), {0, 0}});
        verts_.push_back({{out.x, out.y}, fc(col.withA(0)), {0, 0}});
    }
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        int in_i = 1 + i * 2, out_i = in_i + 1, in_j = 1 + j * 2, out_j = in_j + 1;
        idx_.insert(idx_.end(), {0, in_i, in_j, in_i, out_i, out_j, in_i, out_j, in_j});
    }
    draw(nullptr);
}

void Renderer2D::strokePathAA(const std::vector<PN>& path, float thickness, Color c, bool closed) {
    const int n = static_cast<int>(path.size());
    if (n < 2) return;
    if (thickness < 1.0f) {
        c = c.alpha(thickness);
        thickness = 1.0f;
    }
    const float h = thickness * 0.5f;
    verts_.clear();
    idx_.clear();
    SDL_FColor solid = fc(c), clear = fc(c.withA(0));
    for (const PN& pn : path) {
        Vec2f a = pn.p + pn.n * (h + 0.5f), b = pn.p + pn.n * (h - 0.5f);
        Vec2f d = pn.p - pn.n * (h - 0.5f), e = pn.p - pn.n * (h + 0.5f);
        verts_.push_back({{a.x, a.y}, clear, {0, 0}});
        verts_.push_back({{b.x, b.y}, solid, {0, 0}});
        verts_.push_back({{d.x, d.y}, solid, {0, 0}});
        verts_.push_back({{e.x, e.y}, clear, {0, 0}});
    }
    const int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        int j = (i + 1) % n;
        for (int row = 0; row < 3; ++row) {
            int p0 = i * 4 + row, p1 = p0 + 1, q0 = j * 4 + row, q1 = q0 + 1;
            idx_.insert(idx_.end(), {p0, q0, q1, p0, q1, p1});
        }
    }
    draw(nullptr);
}

void Renderer2D::fillRoundRect(const Rect& r, float radius, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    roundRectPath(r, radius, path_);
    fillPathAA(path_, {r.cx(), r.cy()}, c, c, false, 0, 0);
}

void Renderer2D::fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    roundRectPath(r, radius, path_);
    fillPathAA(path_, {r.cx(), r.cy()}, top, bottom, true, r.y, r.bottom());
}

void Renderer2D::strokeRoundRect(const Rect& r, float radius, float thickness, Color c) {
    roundRectPath(r, radius, path_);
    strokePathAA(path_, thickness, c, true);
}

void Renderer2D::fillCircle(float cx, float cy, float radius, Color c) {
    if (radius <= 0) return;
    circlePath(cx, cy, radius, path_);
    fillPathAA(path_, {cx, cy}, c, c, false, 0, 0);
}

void Renderer2D::strokeCircle(float cx, float cy, float radius, float thickness, Color c) {
    circlePath(cx, cy, radius, path_);
    strokePathAA(path_, thickness, c, true);
}

void Renderer2D::fillArc(float cx, float cy, float radius, float thickness, float a0, float a1, Color c) {
    path_.clear();
    const int n = std::max(2, static_cast<int>(segmentsFor(radius) * 4 * std::fabs(a1 - a0) / (2 * kPi)) + 1);
    for (int i = 0; i <= n; ++i) {
        float a = a0 + (a1 - a0) * static_cast<float>(i) / n;
        Vec2f nrm{std::cos(a), std::sin(a)};
        path_.push_back({{cx + nrm.x * radius, cy + nrm.y * radius}, nrm});
    }
    strokePathAA(path_, thickness, c, false);
}

void Renderer2D::glow(float cx, float cy, float radius, Color inner) {
    const int n = segmentsFor(radius) * 4;
    verts_.clear();
    idx_.clear();
    verts_.push_back({{cx, cy}, fc(inner), {0, 0}});
    SDL_FColor outer = fc(inner.withA(0));
    for (int i = 0; i < n; ++i) {
        float a = 2 * kPi * static_cast<float>(i) / n;
        verts_.push_back({{cx + std::cos(a) * radius, cy + std::sin(a) * radius}, outer, {0, 0}});
    }
    for (int i = 0; i < n; ++i) idx_.insert(idx_.end(), {0, 1 + i, 1 + (i + 1) % n});
    draw(nullptr);
}

void Renderer2D::line(float x0, float y0, float x1, float y1, float thickness, Color c, bool roundCaps) {
    Vec2f d{x1 - x0, y1 - y0};
    float len = d.length();
    if (len < 1e-3f) {
        if (roundCaps) fillCircle(x0, y0, thickness * 0.5f, c);
        return;
    }
    Vec2f nrm{-d.y / len, d.x / len};
    path_.clear();
    path_.push_back({{x0, y0}, nrm});
    path_.push_back({{x1, y1}, nrm});
    strokePathAA(path_, thickness, c, false);
    if (roundCaps && thickness > 2.0f) {
        fillCircle(x0, y0, thickness * 0.5f, c);
        fillCircle(x1, y1, thickness * 0.5f, c);
    }
}

void Renderer2D::fillTriangle(Vec2f a, Vec2f b, Vec2f c, Color col) { fillConvex({a, b, c}, col); }

void Renderer2D::fillConvex(const std::vector<Vec2f>& pts, Color c) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return;
    Vec2f center{0, 0};
    for (auto& p : pts) center = center + p;
    center = center * (1.0f / n);
    // Vertex normals: average of adjacent edge normals pointing away from the centre.
    path_.clear();
    for (int i = 0; i < n; ++i) {
        Vec2f prev = pts[(i + n - 1) % n], cur = pts[i], next = pts[(i + 1) % n];
        auto edgeN = [&](Vec2f a, Vec2f b) {
            Vec2f e = b - a;
            float l = e.length();
            if (l < 1e-6f) return Vec2f{0, 0};
            Vec2f nn{e.y / l, -e.x / l};
            Vec2f mid = (a + b) * 0.5f - center;
            if (nn.x * mid.x + nn.y * mid.y < 0) nn = nn * -1.0f;
            return nn;
        };
        Vec2f n1 = edgeN(prev, cur), n2 = edgeN(cur, next);
        Vec2f avg = n1 + n2;
        float l = avg.length();
        Vec2f vn = l > 1e-6f ? avg * (1.0f / l) : n1;
        float cosHalf = std::max(0.3f, vn.x * n1.x + vn.y * n1.y);
        path_.push_back({cur, vn * (1.0f / cosHalf)});
    }
    fillPathAA(path_, center, c, c, false, 0, 0);
}

void Renderer2D::image(SDL_Texture* tex, const Rect& d, float u0, float v0, float u1, float v1, Color tint) {
    if (!tex) return;
    SDL_FColor col = fc(tint);
    verts_ = {{{d.x, d.y}, col, {u0, v0}},
              {{d.right(), d.y}, col, {u1, v0}},
              {{d.right(), d.bottom()}, col, {u1, v1}},
              {{d.x, d.bottom()}, col, {u0, v1}}};
    idx_ = {0, 1, 2, 0, 2, 3};
    draw(tex);
}

SDL_Texture* Renderer2D::asset(const std::string& name) {
    auto it = images_.find(name);
    if (it != images_.end()) return it->second;
    SDL_Texture* tex = nullptr;
    if (SDL_Surface* loaded = SDL_LoadPNG((assetRoot_ + name).c_str())) {
        // PNGs are stored as palette images with transparency to keep the APK small.
        // Convert to straight RGBA so every renderer keeps per-pixel alpha
        // (otherwise transparent areas can show up as solid squares).
        SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (s) {
            tex = SDL_CreateTextureFromSurface(r_, s);
            SDL_DestroySurface(s);
        }
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        }
    }
    images_[name] = tex;  // cache failures too, so we don't retry every frame
    return tex;
}

void Renderer2D::clearImages() {
    for (auto& [name, tex] : images_)
        if (tex) SDL_DestroyTexture(tex);
    images_.clear();
}

// ------------------------------------------------------------------ text

float Renderer2D::lineHeight(float px, FontId f) {
    FontAtlas* a = fonts_.atlas(f, px);
    return a ? a->lineHeight() : px * 1.2f;
}

float Renderer2D::measure(const std::string& s, float px, FontId f) {
    FontAtlas* a = fonts_.atlas(f, px);
    if (!a) return 0;
    float w = 0;
    GlyphQuad q;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = decodeUtf8(s, i);
        if (a->glyph(cp, q) || a->glyph('?', q)) w += q.advance;
    }
    return w;
}

float Renderer2D::text(const std::string& str, float x, float y, float px, Color c, FontId f, Align align,
                       float maxWidth) {
    FontAtlas* a = fonts_.atlas(f, px);
    if (!a || str.empty()) return 0;
    const std::string s = maxWidth > 0 ? elide(str, maxWidth, px, f) : str;
    float w = measure(s, px, f);
    if (align == Align::Center) x -= w * 0.5f;
    else if (align == Align::Right) x -= w;
    x = std::round(x);
    const float baseline = std::round(y + a->ascent());
    verts_.clear();
    idx_.clear();
    SDL_FColor col = fc(c);
    float pen = x;
    GlyphQuad q;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = decodeUtf8(s, i);
        if (!a->glyph(cp, q) && !a->glyph('?', q)) continue;
        if (q.x1 > q.x0) {
            int base = static_cast<int>(verts_.size());
            float x0 = pen + q.x0, x1 = pen + q.x1, y0 = baseline + q.y0, y1 = baseline + q.y1;
            verts_.push_back({{x0, y0}, col, {q.s0, q.t0}});
            verts_.push_back({{x1, y0}, col, {q.s1, q.t0}});
            verts_.push_back({{x1, y1}, col, {q.s1, q.t1}});
            verts_.push_back({{x0, y1}, col, {q.s0, q.t1}});
            idx_.insert(idx_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        pen += q.advance;
    }
    draw(a->texture());
    return w;
}

void Renderer2D::textIn(const std::string& s, const Rect& r, float px, Color c, FontId f, Align align) {
    FontAtlas* a = fonts_.atlas(f, px);
    if (!a) return;
    // Centre the cap height (~0.72em for Inter) on the rectangle's centre line.
    const float capH = px * 0.72f;
    const float y = r.cy() + capH * 0.5f - a->ascent();
    const float x = align == Align::Left ? r.x : align == Align::Right ? r.right() : r.cx();
    text(s, x, y, px, c, f, align, r.w);
}

std::vector<std::string> Renderer2D::wrap(const std::string& s, float width, float px, FontId f) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(cand, px, f) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            // Hard-break single words that are too long.
            while (measure(line, px, f) > width && line.size() > 1) {
                size_t cut = line.size() - 1;
                while (cut > 1 && measure(line.substr(0, cut), px, f) > width) --cut;
                lines.push_back(line.substr(0, cut));
                line = line.substr(cut);
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string Renderer2D::elide(const std::string& s, float width, float px, FontId f) {
    if (measure(s, px, f) <= width) return s;
    std::string t = s;
    while (!t.empty() && measure(t + "...", px, f) > width) {
        // Remove one UTF-8 code point from the end.
        size_t k = t.size() - 1;
        while (k > 0 && (static_cast<unsigned char>(t[k]) & 0xC0) == 0x80) --k;
        t.erase(k);
    }
    return t + "...";
}

void Renderer2D::glyphCentered(uint32_t cp, float cx, float cy, float px, Color c, FontId f) {
    FontAtlas* a = fonts_.atlas(f, px);
    GlyphQuad q;
    if (!a || !a->glyph(cp, q)) return;
    float w = q.x1 - q.x0, h = q.y1 - q.y0;
    float x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
    SDL_FColor col = fc(c);
    verts_ = {{{x0, y0}, col, {q.s0, q.t0}},
              {{x0 + w, y0}, col, {q.s1, q.t0}},
              {{x0 + w, y0 + h}, col, {q.s1, q.t1}},
              {{x0, y0 + h}, col, {q.s0, q.t1}}};
    idx_ = {0, 1, 2, 0, 2, 3};
    draw(a->texture());
}

}  // namespace mgh
