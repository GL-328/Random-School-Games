#include "Font.hpp"

#include <algorithm>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace mgh {

struct FontSystem::FontData {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
};

uint32_t decodeUtf8(const std::string& s, size_t& i) {
    auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    unsigned char c = b(i);
    if (c < 0x80) { ++i; return c; }
    int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    if (len == 0 || i + len > s.size()) { ++i; return 0xFFFD; }
    uint32_t cp = c & (0xFF >> (len + 1));
    for (int k = 1; k < len; ++k) {
        unsigned char cc = b(i + k);
        if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += len;
    return cp;
}

FontSystem::FontSystem(SDL_Renderer* r) : renderer_(r) {}
FontSystem::~FontSystem() { clear(); }

bool FontSystem::load(FontId id, std::vector<unsigned char> data) {
    auto fd = std::make_unique<FontData>();
    fd->bytes = std::move(data);
    int offset = stbtt_GetFontOffsetForIndex(fd->bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&fd->info, fd->bytes.data(), offset)) return false;
    fonts_[static_cast<int>(id)] = std::move(fd);
    return true;
}

void FontSystem::clear() { atlases_.clear(); }

void FontSystem::beginFrame() {
    ++frame_;
    // Drop atlases not used for ~10 seconds of rendered frames.
    for (auto it = atlases_.begin(); it != atlases_.end();) {
        if (frame_ - it->second->lastUsed_ > 600) it = atlases_.erase(it);
        else ++it;
    }
}

FontAtlas::~FontAtlas() {
    if (texture_) SDL_DestroyTexture(texture_);
}

bool FontAtlas::glyph(uint32_t cp, GlyphQuad& out) const {
    auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), cp,
                               [](const Glyph& g, uint32_t c) { return g.cp < c; });
    if (it == glyphs_.end() || it->cp != cp) return false;
    out = it->q;
    return true;
}

FontAtlas* FontSystem::atlas(FontId id, float pixelSize) {
    const int px = std::max(6, static_cast<int>(std::lround(pixelSize)));
    auto key = std::make_pair(static_cast<int>(id), px);
    auto it = atlases_.find(key);
    if (it != atlases_.end()) {
        it->second->lastUsed_ = frame_;
        return it->second.get();
    }
    FontData* fd = fonts_[static_cast<int>(id)].get();
    if (!fd) return nullptr;

    // Code point set: printable ASCII, plus a few symbols / Latin-1 for smaller sizes.
    std::vector<uint32_t> cps;
    if (id == FontId::Chess) {
        for (uint32_t c = 0x2654; c <= 0x265F; ++c) cps.push_back(c);
    } else {
        for (uint32_t c = 32; c < 127; ++c) cps.push_back(c);
        if (px <= 48) {
            for (uint32_t c = 0xA0; c <= 0xFF; ++c) cps.push_back(c);
            for (uint32_t c : {0x2013u, 0x2014u, 0x2022u, 0x2026u, 0x2190u, 0x2191u, 0x2192u, 0x2193u}) cps.push_back(c);
        }
    }
    cps.erase(std::remove_if(cps.begin(), cps.end(),
                             [&](uint32_t c) { return stbtt_FindGlyphIndex(&fd->info, static_cast<int>(c)) == 0 && c != 32; }),
              cps.end());

    // Size the atlas from the glyph count (power of two, square-ish).
    const int pad = 2;
    const long area = static_cast<long>(cps.size()) * (px + pad) * (px + pad);
    int w = 128;
    while (static_cast<long>(w) * w < area * 13 / 10) w *= 2;
    int h = w;
    std::vector<unsigned char> alpha(static_cast<size_t>(w) * h);
    std::vector<stbtt_packedchar> packed(cps.size());

    stbtt_pack_context pc;
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        alpha.assign(static_cast<size_t>(w) * h, 0);
        if (!stbtt_PackBegin(&pc, alpha.data(), w, h, 0, pad, nullptr)) return nullptr;
        stbtt_PackSetOversampling(&pc, 1, 1);
        std::vector<int> cpsInt(cps.begin(), cps.end());
        stbtt_pack_range range{};
        range.font_size = static_cast<float>(px);
        range.array_of_unicode_codepoints = cpsInt.data();
        range.num_chars = static_cast<int>(cpsInt.size());
        range.chardata_for_range = packed.data();
        ok = stbtt_PackFontRanges(&pc, fd->bytes.data(), 0, &range, 1) != 0;
        stbtt_PackEnd(&pc);
        if (!ok) { h *= 2; alpha.resize(static_cast<size_t>(w) * h); }
    }
    if (!ok) return nullptr;

    // Upload as white RGBA with coverage in alpha so colour modulation works everywhere.
    SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return nullptr;
    for (int y = 0; y < h; ++y) {
        auto* row = static_cast<uint8_t*>(surf->pixels) + static_cast<size_t>(y) * surf->pitch;
        for (int x = 0; x < w; ++x) {
            uint8_t a = alpha[static_cast<size_t>(y) * w + x];
            row[x * 4 + 0] = 255;
            row[x * 4 + 1] = 255;
            row[x * 4 + 2] = 255;
            row[x * 4 + 3] = a;
        }
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_DestroySurface(surf);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    auto atlas = std::make_unique<FontAtlas>();
    atlas->texture_ = tex;
    float scale = stbtt_ScaleForPixelHeight(&fd->info, static_cast<float>(px));
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &gap);
    atlas->ascent_ = asc * scale;
    atlas->descent_ = desc * scale;
    atlas->lineGap_ = gap * scale;
    for (size_t i = 0; i < cps.size(); ++i) {
        const stbtt_packedchar& p = packed[i];
        FontAtlas::Glyph g;
        g.cp = cps[i];
        g.q.x0 = p.xoff;
        g.q.y0 = p.yoff;
        g.q.x1 = p.xoff2;
        g.q.y1 = p.yoff2;
        g.q.s0 = p.x0 / static_cast<float>(w);
        g.q.t0 = p.y0 / static_cast<float>(h);
        g.q.s1 = p.x1 / static_cast<float>(w);
        g.q.t1 = p.y1 / static_cast<float>(h);
        g.q.advance = p.xadvance;
        atlas->glyphs_.push_back(g);
    }
    std::sort(atlas->glyphs_.begin(), atlas->glyphs_.end(), [](auto& a, auto& b) { return a.cp < b.cp; });
    atlas->lastUsed_ = frame_;
    FontAtlas* raw = atlas.get();
    atlases_[key] = std::move(atlas);
    return raw;
}

}  // namespace mgh
