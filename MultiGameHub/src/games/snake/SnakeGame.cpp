// Single-player Snake with swipe and D-pad controls, smooth interpolated
// movement, increasing speed and a persistent high score.
#include <algorithm>
#include <cmath>

#include "SnakeLogic.hpp"
#include "games/Game.hpp"

namespace mgh {

namespace {

using snake::Cell;
using snake::Dir;
using snake::TickResult;

class SnakeGame : public Game {
public:
    enum class State { Ready, Playing, Paused, Over };

    explicit SnakeGame(AppContext& ctx) : Game(ctx) {
        Json theme = ctx.packTheme("snake");
        board_ = themeColor(theme, "board", Color::hex(0x10261C));
        boardAlt_ = themeColor(theme, "board_alt", Color::hex(0x132C21));
        head_ = themeColor(theme, "head", Color::hex(0x7CF29A));
        tail_ = themeColor(theme, "tail", Color::hex(0x1FAF6A));
        food_ = themeColor(theme, "food", Color::hex(0xFF5A5F));
        best_ = static_cast<int>(ctx.settings.getInt(keys::kSnakeHighScore, 0));
    }

    void onBack() override {
        if (state_ == State::Playing) state_ = State::Paused;
        else if (state_ == State::Paused) state_ = State::Playing;
        else requestExit();
    }
    void onAppPaused() override {
        if (state_ == State::Playing) state_ = State::Paused;
    }
    bool wantsContinuousFrames() const override { return state_ == State::Playing || flash_ > 0; }

    void onPointer(const PointerEvent& e) override;
    void update(float dt) override;
    void render() override;

private:
    void layout();
    void restart();
    void steer(Dir d);
    Rect cellRect(float cx, float cy) const {
        return {grid_.x + cx * cell_, grid_.y + cy * cell_, cell_, cell_};
    }

    snake::Logic logic_;
    State state_ = State::Ready;
    Rect hud_, grid_, pad_;
    Rect padBtn_[4];  // up, down, left, right
    float cell_ = 10;
    int cols_ = 15, rows_ = 20;
    bool sized_ = false;
    int best_ = 0;
    bool newBest_ = false;
    float flash_ = 0;   // eat flash
    float pulse_ = 0;
    uint64_t swipeId_ = 0;
    float swipeX_ = 0, swipeY_ = 0;
    Color board_, boardAlt_, head_, tail_, food_;
};

void SnakeGame::layout() {
    const Rect s = ctx_.safe;
    const float m = ctx_.px(12);
    hud_ = {s.x + m, s.y + ctx_.px(8), s.w - 2 * m, ctx_.px(56)};
    const float padH = std::min(ctx_.px(190), s.h * 0.28f);
    pad_ = {s.x + m, s.bottom() - padH - ctx_.px(6), s.w - 2 * m, padH};
    Rect area{s.x + m, hud_.bottom() + ctx_.px(8), s.w - 2 * m, pad_.y - hud_.bottom() - ctx_.px(16)};
    if (!sized_) {
        cols_ = 15;
        cell_ = std::floor(area.w / cols_);
        rows_ = std::max(10, static_cast<int>(area.h / cell_));
        logic_.reset(cols_, rows_, static_cast<uint32_t>(SDL_GetTicksNS() & 0xFFFFFFFF) | 1u);
        sized_ = true;
    }
    cell_ = std::floor(std::min(area.w / cols_, area.h / rows_));
    const float gw = cell_ * cols_, gh = cell_ * rows_;
    grid_ = {std::round(area.cx() - gw * 0.5f), std::round(area.y + (area.h - gh) * 0.5f), gw, gh};

    const float b = std::min(pad_.h / 3.0f, ctx_.px(64));
    const float cx = pad_.cx(), cy = pad_.cy();
    padBtn_[0] = {cx - b * 0.5f, cy - b * 1.5f, b, b};
    padBtn_[1] = {cx - b * 0.5f, cy + b * 0.5f, b, b};
    padBtn_[2] = {cx - b * 1.5f - ctx_.px(6), cy - b * 0.5f, b, b};
    padBtn_[3] = {cx + b * 0.5f + ctx_.px(6), cy - b * 0.5f, b, b};
}

void SnakeGame::restart() {
    logic_.reset(cols_, rows_, static_cast<uint32_t>(SDL_GetTicksNS() & 0xFFFFFFFF) | 1u);
    state_ = State::Playing;
    newBest_ = false;
}

void SnakeGame::steer(Dir d) {
    if (state_ == State::Ready) state_ = State::Playing;
    if (state_ != State::Playing) return;
    logic_.turn(d);
}

void SnakeGame::onPointer(const PointerEvent& e) {
    if (e.type == PointerEvent::Type::Down) {
        // D-pad reacts on touch-down for responsiveness.
        if (state_ == State::Playing || state_ == State::Ready) {
            static const Dir dirs[4] = {Dir::Up, Dir::Down, Dir::Left, Dir::Right};
            for (int i = 0; i < 4; ++i) {
                if (padBtn_[i].inset(-ctx_.px(8)).contains(e.x, e.y)) {
                    steer(dirs[i]);
                    ctx_.vibrate(8);
                    return;
                }
            }
        }
        if (swipeId_ == 0 && !pad_.contains(e.x, e.y) && !hud_.contains(e.x, e.y)) {
            swipeId_ = e.id;
            swipeX_ = e.x;
            swipeY_ = e.y;
        }
    } else if (e.type == PointerEvent::Type::Move && e.id == swipeId_) {
        float dx = e.x - swipeX_, dy = e.y - swipeY_;
        if (std::max(std::fabs(dx), std::fabs(dy)) > ctx_.px(22)) {
            if (std::fabs(dx) > std::fabs(dy)) steer(dx > 0 ? Dir::Right : Dir::Left);
            else steer(dy > 0 ? Dir::Down : Dir::Up);
            swipeX_ = e.x;  // allow chained swipes in one gesture
            swipeY_ = e.y;
        }
    } else if (e.id == swipeId_) {
        swipeId_ = 0;
    }
}

void SnakeGame::update(float dt) {
    layout();
    flash_ = std::max(0.0f, flash_ - dt * 3);
    pulse_ += dt;
    if (state_ != State::Playing) return;
    bool ticked = false;
    TickResult res = logic_.update(dt, &ticked);
    if (!ticked) return;
    if (res == TickResult::Ate) {
        ctx_.sfx(Sfx::Eat, 0.8f, 1.0f + std::min(logic_.score(), 40) * 0.01f);
        flash_ = 1;
    } else if (res == TickResult::Died || res == TickResult::Won) {
        state_ = State::Over;
        ctx_.sfx(res == TickResult::Won ? Sfx::Win : Sfx::Die);
        ctx_.vibrate(70);
        if (logic_.score() > best_) {
            best_ = logic_.score();
            newBest_ = true;
            ctx_.settings.setInt(keys::kSnakeHighScore, best_);
        }
    }
}

void SnakeGame::render() {
    layout();
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    r.fillRect(ctx_.screen, th.bg);

    const bool overlay = state_ == State::Paused || state_ == State::Over;
    ui.setInputEnabled(!overlay);

    // HUD.
    if (ui.iconButton({hud_.x, hud_.cy() - ctx_.px(22), ctx_.px(44), ctx_.px(44)}, Icon::Back, th.text, th.surface))
        requestExit();
    // Score boxes share the space between the back and pause buttons.
    const float btn = ctx_.px(44), g = ctx_.px(8);
    const float boxesW = hud_.w - 2 * btn - 4 * g;
    const float boxW = std::min(ctx_.px(120), (boxesW - g) / 2);
    const float boxesX = hud_.cx() - (boxW * 2 + g) * 0.5f;
    Rect scoreBox{boxesX, hud_.y + ctx_.px(4), boxW, hud_.h - ctx_.px(8)};
    Rect bestBox = scoreBox.offset(boxW + g, 0);
    for (int k = 0; k < 2; ++k) {
        const Rect& bx = k == 0 ? scoreBox : bestBox;
        r.fillRoundRect(bx, ctx_.px(12), th.surface);
        r.text(k == 0 ? "SCORE" : "BEST", bx.x + ctx_.px(12), bx.y + ctx_.px(6), ctx_.px(10), th.textFaint, FontId::Bold,
               Align::Left, bx.w - ctx_.px(20));
        r.text(std::to_string(k == 0 ? logic_.score() : best_), bx.x + ctx_.px(12), bx.y + ctx_.px(19), ctx_.px(20),
               k == 0 ? th.text : th.warning, FontId::Bold, Align::Left, bx.w - ctx_.px(20));
    }
    if (state_ == State::Playing &&
        ui.iconButton({hud_.right() - ctx_.px(44), hud_.cy() - ctx_.px(22), ctx_.px(44), ctx_.px(44)}, Icon::Pause,
                      th.text, th.surface))
        state_ = State::Paused;

    // Board.
    r.fillRoundRect(grid_.inset(-ctx_.px(6)), ctx_.px(14), board_.darken(0.25f));
    r.fillRect(grid_, board_);
    for (int y = 0; y < rows_; ++y)
        for (int x = (y & 1); x < cols_; x += 2) r.fillRect(cellRect(static_cast<float>(x), static_cast<float>(y)), boardAlt_);

    // Food: a flat apple.
    Cell f = logic_.food();
    if (f.x >= 0) {
        Rect fr = cellRect(static_cast<float>(f.x), static_cast<float>(f.y));
        r.fillCircle(fr.cx(), fr.cy() + cell_ * 0.04f, cell_ * 0.34f, food_);
        r.line(fr.cx(), fr.cy() - cell_ * 0.26f, fr.cx() + cell_ * 0.14f, fr.cy() - cell_ * 0.42f, cell_ * 0.1f,
               Color::hex(0x5BBF5B));
    }

    // Snake: one continuous tube along the grid path. The head slides from its
    // previous cell, the tail end slides out of the old tail cell, and every
    // corner sits exactly on a cell centre, so turns are clean and round.
    const auto& body = logic_.body();
    const auto& prev = logic_.previousBody();
    const float t = state_ == State::Playing ? std::clamp(logic_.tickProgress(), 0.0f, 1.0f) : 1.0f;
    const size_t n = body.size();
    auto centre = [&](float cx, float cy) { return Vec2f{grid_.x + (cx + 0.5f) * cell_, grid_.y + (cy + 0.5f) * cell_}; };
    auto lerpCell = [&](Cell a, Cell b) {
        return centre(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    };
    std::vector<Vec2f> path;
    if (n > 0) {
        path.push_back(prev.empty() ? centre(body[0].x, body[0].y) : lerpCell(prev[0], body[0]));
        for (size_t i = 1; i < n; ++i) path.push_back(centre(body[i].x, body[i].y));
        if (prev.size() == n && n > 1) path.push_back(lerpCell(prev[n - 1], body[n - 1]));
    }
    const float width = cell_ * 0.78f;
    for (int pass = 0; pass < 2; ++pass) {
        const Color c = pass == 0 ? tail_.darken(0.25f) : head_;
        const float w = pass == 0 ? width + ctx_.px(3) : width;
        for (size_t i = 0; i + 1 < path.size(); ++i) r.line(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y, w, c, false);
        for (const Vec2f& p : path) r.fillCircle(p.x, p.y, w * 0.5f, c);  // round joins and ends
    }
    if (!path.empty()) {
        // Eyes look in the travel direction.
        const Vec2f h = path[0];
        float ex = 0, ey = 0;
        switch (logic_.direction()) {
            case Dir::Up: ey = -1; break;
            case Dir::Down: ey = 1; break;
            case Dir::Left: ex = -1; break;
            case Dir::Right: ex = 1; break;
        }
        const float side = cell_ * 0.19f;
        for (int k = -1; k <= 1; k += 2) {
            float x = h.x + ex * cell_ * 0.1f + (-ey) * side * k;
            float y = h.y + ey * cell_ * 0.1f + ex * side * k;
            r.fillCircle(x, y, cell_ * 0.11f, Color::hex(0xFFFFFF));
            r.fillCircle(x + ex * cell_ * 0.04f, y + ey * cell_ * 0.04f, cell_ * 0.055f, Color::hex(0x10231A));
        }
    }

    // D-pad.
    for (int i = 0; i < 4; ++i) {
        bool down = false;
        for (const auto& p : ui.pointers())
            if (p.down && padBtn_[i].inset(-ctx_.px(8)).contains(p.x, p.y)) down = true;
        Rect b = padBtn_[i];
        r.fillRoundRect(b, ctx_.px(16), down ? th.accent : th.surface);
        const float cx = b.cx(), cy = b.cy(), a = b.w * 0.18f;
        Color ac = down ? th.accentText : th.text;
        switch (i) {
            case 0: r.fillTriangle({cx, cy - a}, {cx + a, cy + a * 0.6f}, {cx - a, cy + a * 0.6f}, ac); break;
            case 1: r.fillTriangle({cx, cy + a}, {cx - a, cy - a * 0.6f}, {cx + a, cy - a * 0.6f}, ac); break;
            case 2: r.fillTriangle({cx - a, cy}, {cx + a * 0.6f, cy - a}, {cx + a * 0.6f, cy + a}, ac); break;
            case 3: r.fillTriangle({cx + a, cy}, {cx - a * 0.6f, cy + a}, {cx - a * 0.6f, cy - a}, ac); break;
        }
    }
    ui.setInputEnabled(true);

    if (state_ == State::Ready) {
        const float cw = std::min(ctx_.px(260), grid_.w - ctx_.px(24));
        Rect card{grid_.cx() - cw * 0.5f, grid_.cy() - ctx_.px(58), cw, ctx_.px(116)};
        r.fillRoundRect(card, ctx_.px(16), th.surface);
        r.text("Snake", card.cx(), card.y + ctx_.px(16), ctx_.px(22), th.text, FontId::Bold, Align::Center);
        r.text("Swipe or use the arrows", card.cx(), card.y + ctx_.px(52), ctx_.px(13.5f), th.textDim,
               FontId::Regular, Align::Center, card.w - ctx_.px(24));
        r.text("to start moving", card.cx(), card.y + ctx_.px(72), ctx_.px(13.5f), th.textDim, FontId::Regular,
               Align::Center, card.w - ctx_.px(24));
    }

    if (overlay) {
        r.fillRect(ctx_.screen, th.overlay);
        const bool over = state_ == State::Over;
        const float ow = std::min(ctx_.px(290), ctx_.safe.w - ctx_.px(32));
        Rect card{ctx_.safe.cx() - ow * 0.5f, ctx_.safe.cy() - ctx_.px(135), ow, ctx_.px(270)};
        r.fillRoundRect(card, ctx_.px(16), th.surface);
        r.text(over ? "Game over" : "Paused", card.cx(), card.y + ctx_.px(18), ctx_.px(22), th.text, FontId::Bold,
               Align::Center);
        float by = card.y + ctx_.px(76);
        if (over) {
            r.text(std::to_string(logic_.score()), card.cx(), card.y + ctx_.px(50), ctx_.px(40), th.text, FontId::Bold,
                   Align::Center);
            r.text(newBest_ ? "New high score!" : "Best: " + std::to_string(best_), card.cx(), card.y + ctx_.px(102),
                   ctx_.px(14), newBest_ ? th.warning : th.textDim, FontId::Bold, Align::Center);
            by = card.y + ctx_.px(140);
        } else {
            r.text("Score " + std::to_string(logic_.score()), card.cx(), card.y + ctx_.px(52), ctx_.px(14), th.textDim,
                   FontId::Regular, Align::Center);
        }
        Rect b{card.x + ctx_.px(16), by, card.w - ctx_.px(32), ctx_.px(46)};
        if (ui.button(b, over ? "Play again" : "Resume", ButtonStyle::Primary, over ? Icon::Restart : Icon::Play)) {
            ctx_.sfx(Sfx::Click);
            if (over) restart();
            else state_ = State::Playing;
        }
        if (!over) {
            b.y += ctx_.px(54);
            if (ui.button(b, "Restart", ButtonStyle::Secondary, Icon::Restart)) restart();
        }
        b.y += ctx_.px(54);
        if (ui.button(b, "Exit to hub", ButtonStyle::Secondary, Icon::Home)) requestExit();
    }
}

}  // namespace

std::unique_ptr<Game> createSnakeGame(AppContext& ctx) { return std::make_unique<SnakeGame>(ctx); }

}  // namespace mgh
