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
    // The same "chessnut" knight as the in-game pieces.
    const float s = std::min(rc.w, rc.h) * 0.74f;
    r.image(r.asset("pieces/knight_icon.png"), {rc.cx() - s * 0.5f, rc.cy() - s * 0.5f, s, s}, 0, 0, 1, 1);
}

void airHockeyArt(Renderer2D& r, const Rect& rc, float /*time*/) {
    const float s = std::min(rc.w, rc.h);
    const Color line(255, 255, 255, 70);
    r.fillRect({rc.x, rc.cy() - s * 0.012f, rc.w, s * 0.024f}, line);
    r.strokeCircle(rc.cx(), rc.cy(), s * 0.2f, s * 0.024f, line);
    auto paddle = [&](float x, float y, Color c) {
        r.fillCircle(x, y, s * 0.15f, c.darken(0.22f));
        r.fillCircle(x, y, s * 0.12f, c);
        r.fillCircle(x, y, s * 0.06f, c.darken(0.22f));
    };
    paddle(rc.cx() - s * 0.18f, rc.cy() + s * 0.28f, Color::hex(0xFF4D6D));
    paddle(rc.cx() + s * 0.2f, rc.cy() - s * 0.3f, Color::hex(0x36D6F5));
    const float px = rc.cx() + s * 0.08f, py = rc.cy() + s * 0.02f;
    r.fillCircle(px, py, s * 0.07f, Color::hex(0x14171D));
    r.strokeCircle(px, py, s * 0.055f, s * 0.018f, Color::hex(0xF5F7FA));
}

void snakeArt(Renderer2D& r, const Rect& rc, float /*time*/) {
    // Same look as the in-game snake: one outlined tube with round corners.
    const float s = std::min(rc.w, rc.h);
    const Vec2f pts[] = {{-0.3f, 0.25f}, {-0.1f, 0.25f}, {-0.1f, 0.05f}, {0.1f, 0.05f}, {0.1f, -0.15f}, {0.28f, -0.15f}};
    const int n = 6;
    for (int pass = 0; pass < 2; ++pass) {
        const Color c = pass == 0 ? Color::hex(0x127A48) : Color::hex(0x7CF29A);
        const float w = pass == 0 ? s * 0.17f : s * 0.13f;
        for (int i = 0; i + 1 < n; ++i)
            r.line(rc.cx() + pts[i].x * s, rc.cy() + pts[i].y * s, rc.cx() + pts[i + 1].x * s, rc.cy() + pts[i + 1].y * s, w,
                   c, false);
        for (const Vec2f& p : pts) r.fillCircle(rc.cx() + p.x * s, rc.cy() + p.y * s, w * 0.5f, c);
    }
    const float hx = rc.cx() + pts[n - 1].x * s, hy = rc.cy() + pts[n - 1].y * s;
    r.fillCircle(hx + s * 0.02f, hy - s * 0.03f, s * 0.025f, Color::hex(0xFFFFFF));
    r.fillCircle(hx + s * 0.02f, hy + s * 0.03f, s * 0.025f, Color::hex(0xFFFFFF));
    r.fillCircle(rc.cx() + s * 0.3f, rc.cy() + s * 0.22f, s * 0.07f, Color::hex(0xFF5A5F));
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
