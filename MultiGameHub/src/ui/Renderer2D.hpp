// Lightweight vector renderer on top of SDL_Renderer (GLES2 on Android).
// All shapes are tessellated into triangles with a 1px alpha feather for
// anti-aliasing, so no textures or images are needed for the UI.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

#include "Font.hpp"
#include "Gfx.hpp"

namespace mgh {

enum class Align { Left, Center, Right };

class Renderer2D {
public:
    Renderer2D(SDL_Renderer* r, FontSystem& fonts) : r_(r), fonts_(fonts) {}
    ~Renderer2D() { clearImages(); }

    SDL_Renderer* sdl() const { return r_; }
    FontSystem& fonts() { return fonts_; }

    // ---- transform: translation and optional 180-degree rotation about a pivot
    void setOffset(float dx, float dy) { offX_ = dx; offY_ = dy; }
    float offsetX() const { return offX_; }
    float offsetY() const { return offY_; }
    void setRotate180(bool on, float pivotX = 0, float pivotY = 0) { rot_ = on; pivX_ = pivotX; pivY_ = pivotY; }
    bool rotated() const { return rot_; }
    void setGlobalAlpha(float a) { alpha_ = std::clamp(a, 0.0f, 1.0f); }
    float globalAlpha() const { return alpha_; }

    // Clip stack: each push intersects with the current clip.
    void pushClip(const Rect& r);
    void popClip();

    // ---- primitives
    void clear(Color c);
    void fillRect(const Rect& r, Color c);
    void fillRectGradient(const Rect& r, Color top, Color bottom);
    void fillRoundRect(const Rect& r, float radius, Color c);
    void fillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom);
    void strokeRoundRect(const Rect& r, float radius, float thickness, Color c);
    void fillCircle(float cx, float cy, float radius, Color c);
    void strokeCircle(float cx, float cy, float radius, float thickness, Color c);
    void fillArc(float cx, float cy, float radius, float thickness, float a0, float a1, Color c);
    void glow(float cx, float cy, float radius, Color inner);  // radial gradient to transparent
    void line(float x0, float y0, float x1, float y1, float thickness, Color c, bool roundCaps = true);
    void fillTriangle(Vec2f a, Vec2f b, Vec2f c, Color col);
    void fillConvex(const std::vector<Vec2f>& pts, Color c);
    // Draws part of a texture (UVs in 0..1) into dst; honours offset/rotation/alpha.
    void image(SDL_Texture* tex, const Rect& dst, float u0, float v0, float u1, float v1, Color tint = Color(255, 255, 255));
    // Small PNG assets (cached after first use; freed by clearImages()).
    void setAssetRoot(std::string root) { assetRoot_ = std::move(root); }
    SDL_Texture* asset(const std::string& name);
    void clearImages();

    // ---- text (y is the top of the line box)
    // maxWidth > 0 shortens the string with "..." so it never exceeds that width.
    float text(const std::string& s, float x, float y, float px, Color c, FontId f = FontId::Regular,
               Align align = Align::Left, float maxWidth = 0);
    // Draws text vertically centred inside `r` (always elided to fit r.w).
    void textIn(const std::string& s, const Rect& r, float px, Color c, FontId f = FontId::Regular,
                Align align = Align::Center);
    float measure(const std::string& s, float px, FontId f = FontId::Regular);
    float lineHeight(float px, FontId f = FontId::Regular);
    // Word-wraps into lines that fit `width`.
    std::vector<std::string> wrap(const std::string& s, float width, float px, FontId f = FontId::Regular);
    // Elides with "..." to fit `width`.
    std::string elide(const std::string& s, float width, float px, FontId f = FontId::Regular);
    // Draws a single glyph centred at (cx, cy) (used for chess pieces).
    void glyphCentered(uint32_t cp, float cx, float cy, float px, Color c, FontId f);

private:
    struct PN {
        Vec2f p, n;
    };
    void roundRectPath(const Rect& r, float radius, std::vector<PN>& out);
    void circlePath(float cx, float cy, float radius, std::vector<PN>& out);
    void fillPathAA(const std::vector<PN>& path, Vec2f center, Color inner, Color outer, bool gradient,
                    float top, float bottom);
    void strokePathAA(const std::vector<PN>& path, float thickness, Color c, bool closed);
    SDL_FColor fc(Color c) const;
    Vec2f xf(Vec2f p) const;
    void draw(SDL_Texture* tex);
    int segmentsFor(float radius) const;

    SDL_Renderer* r_;
    FontSystem& fonts_;
    float offX_ = 0, offY_ = 0;
    bool rot_ = false;
    float pivX_ = 0, pivY_ = 0;
    float alpha_ = 1.0f;
    std::vector<SDL_Vertex> verts_;
    std::vector<int> idx_;
    std::vector<PN> path_;
    std::vector<SDL_Rect> clips_;
    std::string assetRoot_;
    std::map<std::string, SDL_Texture*> images_;
};

}  // namespace mgh
