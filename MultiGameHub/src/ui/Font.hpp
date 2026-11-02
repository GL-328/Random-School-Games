// TrueType/OpenType text via stb_truetype. Glyph atlases are baked lazily per
// (font, pixel size) and evicted when unused, keeping GPU memory small.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mgh {

enum class FontId : int { Regular = 0, Bold = 1, Chess = 2, Count = 3 };

struct GlyphQuad {
    float x0, y0, x1, y1;  // screen offsets relative to pen position / baseline
    float s0, t0, s1, t1;  // texture coordinates
    float advance;
};

class FontAtlas;

class FontSystem {
public:
    explicit FontSystem(SDL_Renderer* r);
    ~FontSystem();

    // Loads font data from memory (copied).
    bool load(FontId id, std::vector<unsigned char> data);
    FontAtlas* atlas(FontId id, float pixelSize);
    void beginFrame();  // evicts atlases unused for a while
    void clear();       // drops all atlases (e.g. when the renderer is reset)
    int bakeCount() const { return bakes_; }  // atlases built so far (diagnostics)
    size_t atlasCount() const { return atlases_.size(); }

private:
    struct FontData;
    SDL_Renderer* renderer_;
    std::unique_ptr<FontData> fonts_[static_cast<int>(FontId::Count)];
    std::map<std::pair<int, int>, std::unique_ptr<FontAtlas>> atlases_;
    uint64_t frame_ = 0;
    int bakes_ = 0;
};

class FontAtlas {
public:
    ~FontAtlas();
    bool glyph(uint32_t codepoint, GlyphQuad& out) const;
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }
    float lineHeight() const { return ascent_ - descent_ + lineGap_; }
    SDL_Texture* texture() const { return texture_; }

private:
    friend class FontSystem;
    struct Glyph {
        uint32_t cp;
        GlyphQuad q;
    };
    std::vector<Glyph> glyphs_;  // sorted by codepoint
    SDL_Texture* texture_ = nullptr;
    float ascent_ = 0, descent_ = 0, lineGap_ = 0;
    uint64_t lastUsed_ = 0;
};

// Decodes one UTF-8 code point and advances `i`. Invalid bytes yield U+FFFD.
uint32_t decodeUtf8(const std::string& s, size_t& i);

}  // namespace mgh
