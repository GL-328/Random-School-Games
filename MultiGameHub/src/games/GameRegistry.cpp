// The list of games compiled into this build. To add a game: implement the
// Game interface in src/games/<id>/, then add one entry below.
#include <cmath>

#include "Game.hpp"

namespace mgh {

std::unique_ptr<Game> createChessGame(AppContext& ctx);
std::unique_ptr<Game> createAirHockeyGame(AppContext& ctx);
std::unique_ptr<Game> createSnakeGame(AppContext& ctx);

namespace {

void chessArt(Renderer2D& r, const Rect& rc, float /*time*/) {
    const float s = std::min(rc.w, rc.h);
    // Checker pattern fading out.
    const int n = 6;
    const float cs = rc.w / n;
    for (int y = 0; y * cs < rc.h; ++y)
        for (int x = (y & 1); x < n; x += 2)
            r.fillRect({rc.x + x * cs, rc.y + y * cs, cs, std::min(cs, rc.bottom() - (rc.y + y * cs))},
                       Color(255, 255, 255, static_cast<uint8_t>(18 + 10 * (y % 2))));
    const float px = s * 0.72f;
    const float cx = rc.cx(), cy = rc.cy() + s * 0.02f;
    r.glyphCentered(0x265E, cx + s * 0.02f, cy + s * 0.035f, px, Color(0, 0, 0, 70), FontId::Chess);
    r.glyphCentered(0x265E, cx, cy, px, Color::hex(0xFBFAF7), FontId::Chess);
    r.glyphCentered(0x2658, cx, cy, px, Color::hex(0x2A1F4D), FontId::Chess);
}

void airHockeyArt(Renderer2D& r, const Rect& rc, float time) {
    const float s = std::min(rc.w, rc.h);
    const Color line(255, 255, 255, 70);
    r.fillRect({rc.x, rc.cy() - s * 0.012f, rc.w, s * 0.024f}, line);
    r.strokeCircle(rc.cx(), rc.cy(), s * 0.2f, s * 0.024f, line);
    const float wob = std::sin(time * 2.0f) * s * 0.05f;
    auto paddle = [&](float x, float y, Color c) {
        r.fillCircle(x, y, s * 0.15f, c.darken(0.15f));
        r.fillCircle(x, y, s * 0.12f, c);
        r.fillCircle(x, y, s * 0.06f, c.lighten(0.3f));
    };
    paddle(rc.cx() - s * 0.18f + wob, rc.cy() + s * 0.28f, Color::hex(0xFF4D6D));
    paddle(rc.cx() + s * 0.2f - wob, rc.cy() - s * 0.3f, Color::hex(0x36D6F5));
    const float px = rc.cx() + s * 0.08f + wob * 0.6f, py = rc.cy() + s * 0.02f;
    r.fillCircle(px, py, s * 0.07f, Color::hex(0x14171D));
    r.strokeCircle(px, py, s * 0.055f, s * 0.018f, Color::hex(0xF5F7FA));
}

void snakeArt(Renderer2D& r, const Rect& rc, float time) {
    const float s = std::min(rc.w, rc.h);
    const float c = s / 7;
    for (int y = 0; y * c < rc.h; ++y)
        for (int x = (y & 1); x * c < rc.w; x += 2)
            r.fillRect({rc.x + x * c, rc.y + y * c, c, std::min(c, rc.bottom() - (rc.y + y * c))}, Color(255, 255, 255, 14));
    const Vec2f pts[] = {{-0.3f, 0.25f}, {-0.1f, 0.25f}, {-0.1f, 0.05f}, {0.1f, 0.05f}, {0.1f, -0.15f}, {0.28f, -0.15f}};
    const int n = 6;
    for (int i = 0; i < n - 1; ++i) {
        Color col = Color::hex(0x1FAF6A).lerp(Color::hex(0x7CF29A), static_cast<float>(i) / (n - 1));
        r.line(rc.cx() + pts[i].x * s, rc.cy() + pts[i].y * s, rc.cx() + pts[i + 1].x * s, rc.cy() + pts[i + 1].y * s,
               s * 0.14f, col);
    }
    const float hx = rc.cx() + pts[n - 1].x * s, hy = rc.cy() + pts[n - 1].y * s;
    r.fillCircle(hx + s * 0.02f, hy - s * 0.03f, s * 0.025f, Color::hex(0xFFFFFF));
    r.fillCircle(hx + s * 0.02f, hy + s * 0.03f, s * 0.025f, Color::hex(0xFFFFFF));
    const float bob = std::sin(time * 4) * s * 0.01f;
    const float ax = rc.cx() + s * 0.3f, ay = rc.cy() + s * 0.22f + bob;
    r.fillCircle(ax, ay, s * 0.07f, Color::hex(0xFF5A5F));
}

std::vector<GameInfo> buildRegistry() {
    std::vector<GameInfo> v;
    {
        GameInfo g;
        g.id = "chess";
        g.name = "Chess";
        g.tagline = "Classic duel for two";
        g.description =
            "Full standard chess for two players sharing one device: castling, en passant, promotion, "
            "check, checkmate, stalemate and every draw rule (threefold repetition, fifty moves, "
            "insufficient material). Undo, move history and captured pieces included.";
        g.version = "1.0.0";
        g.players = "2 players";
        g.colorA = Color::hex(0x6A4CF5);
        g.colorB = Color::hex(0x2B1C7A);
        g.featured = true;
        g.drawArt = chessArt;
        g.create = createChessGame;
        v.push_back(std::move(g));
    }
    {
        GameInfo g;
        g.id = "air_hockey";
        g.name = "Air Hockey";
        g.tagline = "Fast real-time face-off";
        g.description =
            "Real-time air hockey for two players on the same screen with true multi-touch. "
            "Smooth physics, goal celebrations, first to seven wins.";
        g.version = "1.0.0";
        g.players = "2 players, live";
        g.colorA = Color::hex(0x0FB5C9);
        g.colorB = Color::hex(0x0B3F66);
        g.featured = true;
        g.drawArt = airHockeyArt;
        g.create = createAirHockeyGame;
        v.push_back(std::move(g));
    }
    {
        GameInfo g;
        g.id = "snake";
        g.name = "Snake";
        g.tagline = "Eat, grow, survive";
        g.description =
            "The classic arcade snake. Swipe or use the arrow pad, eat to grow and survive as the pace "
            "picks up. Your high score is saved on this device.";
        g.version = "1.0.0";
        g.players = "1 player";
        g.colorA = Color::hex(0x2BC46E);
        g.colorB = Color::hex(0x0E5A3A);
        g.featured = true;
        g.drawArt = snakeArt;
        g.create = createSnakeGame;
        v.push_back(std::move(g));
    }
    return v;
}

}  // namespace

const std::vector<GameInfo>& gameRegistry() {
    static const std::vector<GameInfo> reg = buildRegistry();
    return reg;
}

const GameInfo* findGame(const std::string& id) {
    for (const auto& g : gameRegistry())
        if (g.id == id) return &g;
    return nullptr;
}

}  // namespace mgh
