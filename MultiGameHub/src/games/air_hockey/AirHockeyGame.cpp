// Real-time two-player air hockey on one device with simultaneous multi-touch.
// Player 1 (bottom) and Player 2 (top) each own any finger that first touches
// their half of the table.
#include <algorithm>
#include <cmath>
#include <deque>

#include "AirHockeyPhysics.hpp"
#include "games/Game.hpp"

namespace mgh {

namespace {

using airhockey::Physics;
using airhockey::TableConfig;
using airhockey::Vec2;

class AirHockeyGame : public Game {
public:
    enum class State { Start, Countdown, Playing, Goal, Won };

    explicit AirHockeyGame(AppContext& ctx) : Game(ctx) {
        Json theme = ctx.packTheme("air_hockey");
        rink_ = themeColor(theme, "rink", Color::hex(0x0E1A2B));
        rinkEdge_ = themeColor(theme, "rink_edge", Color::hex(0x15263D));
        lines_ = themeColor(theme, "lines", Color::hex(0x3A5A86));
        p1_ = themeColor(theme, "player1", Color::hex(0xFF4D6D));
        p2_ = themeColor(theme, "player2", Color::hex(0x36D6F5));
        puckColor_ = themeColor(theme, "puck", Color::hex(0xF5F7FA));
    }

    void onBack() override {
        if (state_ == State::Start || state_ == State::Won) requestExit();
        else paused_ = !paused_;
    }
    void onAppPaused() override {
        if (state_ != State::Start && state_ != State::Won) paused_ = true;
    }

    void onPointer(const PointerEvent& e) override;
    void update(float dt) override;
    void render() override;

private:
    void layout();
    Vec2 toWorld(float x, float y) const { return {(x - table_.x) / scale_, (y - table_.y) / scale_}; }
    Vec2f toScreen(Vec2 w) const { return {table_.x + w.x * scale_, table_.y + w.y * scale_}; }
    void startMatch();
    void drawTable();
    void drawPaddle(int i);
    void drawScores();
    void drawCentered(const std::string& text, float px, Color c, int player, float yOffsetDp = 0);
    Rect pauseButton() const;

    Physics phys_;
    bool configured_ = false;
    float lastAspect_ = 0;
    Rect table_;
    float scale_ = 1;
    State state_ = State::Start;
    bool paused_ = false;
    float timer_ = 0;
    int lastCount_ = -1;
    int lastScorer_ = -1;
    uint64_t finger_[2] = {0, 0};
    std::deque<Vec2> trail_;
    float hitFlash_[2] = {0, 0};
    float shake_ = 0;
    Vec2 savedVel_{0, 0};
    Color rink_, rinkEdge_, lines_, p1_, p2_, puckColor_;
};

void AirHockeyGame::layout() {
    const Rect s = ctx_.safe;
    const float m = ctx_.px(10);
    const float aspect = std::clamp((s.h - 2 * m) / (s.w - 2 * m), 1.4f, 2.1f);
    if (!configured_ || std::fabs(aspect - lastAspect_) > 0.01f) {
        TableConfig cfg;
        cfg.height = aspect;
        phys_.reset(cfg);
        phys_.servePuck(0);
        phys_.puck.pos = {0.5f, aspect * 0.5f};
        lastAspect_ = aspect;
        configured_ = true;
    }
    const float h = phys_.config().height;
    scale_ = std::min(s.w - 2 * m, (s.h - 2 * m) / h);
    const float w = scale_, hh = scale_ * h;
    table_ = {std::round(s.cx() - w * 0.5f), std::round(s.cy() - hh * 0.5f), w, hh};
}

Rect AirHockeyGame::pauseButton() const {
    const float sz = ctx_.px(44);
    return {table_.x + ctx_.px(8), table_.cy() - sz * 0.5f, sz, sz};
}

void AirHockeyGame::startMatch() {
    phys_.resetMatch();
    phys_.servePuck(lastScorer_ == 0 ? 1 : 0);
    phys_.puck.pos = {0.5f, phys_.config().height * 0.5f};
    state_ = State::Countdown;
    savedVel_ = {0, 0};
    timer_ = 3.0f;
    lastCount_ = -1;
    trail_.clear();
    paused_ = false;
}

void AirHockeyGame::onPointer(const PointerEvent& e) {
    if (paused_) return;
    if (e.type == PointerEvent::Type::Down) {
        if (pauseButton().contains(e.x, e.y)) return;
        if (state_ == State::Start || state_ == State::Won) return;  // handled by UI
        int player = e.y >= table_.cy() ? 0 : 1;
        if (finger_[player] == 0) {
            finger_[player] = e.id;
            phys_.setPaddleTarget(player, toWorld(e.x, e.y));
        }
    } else if (e.type == PointerEvent::Type::Move) {
        for (int i = 0; i < 2; ++i)
            if (finger_[i] == e.id) phys_.setPaddleTarget(i, toWorld(e.x, e.y));
    } else {
        for (int i = 0; i < 2; ++i)
            if (finger_[i] == e.id) finger_[i] = 0;
    }
}

void AirHockeyGame::update(float dt) {
    layout();
    for (float& f : hitFlash_) f = std::max(0.0f, f - dt * 4);
    shake_ = std::max(0.0f, shake_ - dt * 3);
    if (paused_ || state_ == State::Start || state_ == State::Won) return;

    if (state_ == State::Countdown || state_ == State::Goal) {
        // Paddles may move while the puck is held in place.
        Vec2 held = phys_.puck.pos;
        phys_.puck.vel = {0, 0};
        phys_.step(dt);
        phys_.puck.pos = held;
        phys_.puck.vel = {0, 0};
        timer_ -= dt;
        if (state_ == State::Goal && timer_ <= 0) {
            phys_.servePuck(lastScorer_ == 0 ? 1 : 0);
            state_ = State::Countdown;
            timer_ = 1.5f;
            lastCount_ = -1;
            trail_.clear();
        } else if (state_ == State::Countdown) {
            int count = static_cast<int>(std::ceil(timer_));
            if (count != lastCount_ && count > 0) {
                ctx_.sfx(Sfx::Countdown);
                lastCount_ = count;
            }
            if (timer_ <= 0) {
                state_ = State::Playing;
                phys_.puck.vel = savedVel_;  // resume motion after a pause
                savedVel_ = {0, 0};
                ctx_.sfx(Sfx::Go);
            }
        }
        return;
    }

    // Playing.
    auto ev = phys_.step(dt);
    if (ev.paddleHits > 0) {
        float vol = std::clamp(0.4f + ev.strongestHit * 0.25f, 0.4f, 1.0f);
        ctx_.sfx(Sfx::PaddleHit, vol, 0.9f + std::min(ev.strongestHit, 3.0f) * 0.08f);
        if (ev.strongestHit > 1.2f) ctx_.vibrate(12);
        int nearest = (phys_.puck.pos - phys_.paddle[0].pos).length() < (phys_.puck.pos - phys_.paddle[1].pos).length() ? 0 : 1;
        hitFlash_[nearest] = 1;
    }
    if (ev.wallHits > 0) ctx_.sfx(Sfx::WallHit, 0.6f);
    trail_.push_front(phys_.puck.pos);
    if (trail_.size() > 10) trail_.pop_back();
    if (ev.goalScoredBy >= 0) {
        lastScorer_ = ev.goalScoredBy;
        ctx_.sfx(Sfx::Goal);
        ctx_.vibrate(45);
        shake_ = 1;
        trail_.clear();
        if (phys_.matchOver()) {
            state_ = State::Won;
            ctx_.sfx(Sfx::Win);
            finger_[0] = finger_[1] = 0;
        } else {
            state_ = State::Goal;
            timer_ = 1.4f;
            phys_.puck.pos = {0.5f, phys_.config().height * (ev.goalScoredBy == 0 ? 0.32f : 0.68f)};
        }
    }
}

void AirHockeyGame::drawTable() {
    auto& r = ctx_.r;
    const float rail = ctx_.px(7);
    Rect outer = table_.inset(-rail);
    r.fillRoundRect(outer, ctx_.px(30), Color::hex(0x2B3442));
    r.fillRoundRect(table_, ctx_.px(24), rink_);
    // Centre line and circle.
    const float lw = ctx_.px(3);
    r.fillRect({table_.x, table_.cy() - lw * 0.5f, table_.w, lw}, lines_);
    r.strokeCircle(table_.cx(), table_.cy(), scale_ * 0.17f, lw, lines_);
    r.fillCircle(table_.cx(), table_.cy(), ctx_.px(5), lines_);
    // Goal creases.
    const float gw = phys_.config().goalWidth * scale_;
    r.fillArc(table_.cx(), table_.y, gw * 0.62f, lw, 0, 3.14159f, p2_.withA(150));
    r.fillArc(table_.cx(), table_.bottom(), gw * 0.62f, lw, 3.14159f, 6.28318f, p1_.withA(150));
    // Goal mouths cut into the rail.
    r.fillRoundRect({table_.cx() - gw * 0.5f, table_.y - rail - ctx_.px(2), gw, rail + ctx_.px(5)}, ctx_.px(3), Color::hex(0x05080D));
    r.fillRoundRect({table_.cx() - gw * 0.5f, table_.bottom() - ctx_.px(3), gw, rail + ctx_.px(5)}, ctx_.px(3), Color::hex(0x05080D));
    r.fillRect({table_.cx() - gw * 0.5f, table_.y - ctx_.px(2), gw, ctx_.px(3)}, p2_);
    r.fillRect({table_.cx() - gw * 0.5f, table_.bottom() - ctx_.px(1), gw, ctx_.px(3)}, p1_);
}

void AirHockeyGame::drawPaddle(int i) {
    auto& r = ctx_.r;
    Vec2f c = toScreen(phys_.paddle[i].pos);
    const float rad = phys_.paddle[i].radius * scale_;
    Color col = i == 0 ? p1_ : p2_;
    // Flat paddle: rim, face and handle knob.
    r.fillCircle(c.x, c.y, rad, col.darken(0.22f + 0.12f * hitFlash_[i]));
    r.fillCircle(c.x, c.y, rad * 0.8f, col);
    r.fillCircle(c.x, c.y, rad * 0.4f, col.darken(0.22f));
}

void AirHockeyGame::drawCentered(const std::string& text, float px, Color c, int player, float yOffsetDp) {
    // Draws text in the middle of a player's half, rotated for the top player.
    auto& r = ctx_.r;
    const float halfH = table_.h * 0.5f;
    float cy = player == 0 ? table_.cy() + halfH * 0.5f : table_.cy() - halfH * 0.5f;
    if (player == 1) r.setRotate180(true, table_.cx(), cy);
    r.textIn(text, {table_.x, cy - px + ctx_.px(yOffsetDp), table_.w, px * 2}, px, c, FontId::Bold, Align::Center);
    if (player == 1) r.setRotate180(false);
}

void AirHockeyGame::drawScores() {
    auto& r = ctx_.r;
    const float fpx = ctx_.px(40);
    for (int i = 0; i < 2; ++i) {
        float cy = i == 0 ? table_.cy() + ctx_.px(40) : table_.cy() - ctx_.px(40);
        float cx = table_.right() - ctx_.px(36);
        if (i == 1) r.setRotate180(true, cx, cy);
        Color col = (i == 0 ? p1_ : p2_).withA(225);
        r.textIn(std::to_string(phys_.score[i]), {cx - ctx_.px(30), cy - fpx * 0.5f, ctx_.px(60), fpx}, fpx, col,
                 FontId::Bold, Align::Center);
        if (i == 1) r.setRotate180(false);
    }
}

void AirHockeyGame::render() {
    layout();
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    r.fillRect(ctx_.screen, Color::hex(0x0A0D12));

    float sx = shake_ > 0 ? std::sin(ui.time() * 90) * shake_ * ctx_.px(4) : 0;
    r.setOffset(sx, 0);
    drawTable();
    if (state_ != State::Start && state_ != State::Won) drawScores();
    {
        Vec2f p = toScreen(phys_.puck.pos);
        const float pr = phys_.puck.radius * scale_;
        if (state_ != State::Goal) {
            r.fillCircle(p.x, p.y, pr, Color::hex(0x1A1D24));
            r.strokeCircle(p.x, p.y, pr * 0.82f, ctx_.px(2.5f), puckColor_);
        }
    }
    drawPaddle(0);
    drawPaddle(1);
    r.setOffset(0, 0);

    // Pause button on the centre line.
    const bool overlay = paused_ || state_ == State::Start || state_ == State::Won;
    ui.setInputEnabled(!overlay);
    if (state_ != State::Start && state_ != State::Won &&
        ui.iconButton(pauseButton(), Icon::Pause, Color::hex(0xFFFFFF), Color(255, 255, 255, 30)))
        paused_ = true;
    ui.setInputEnabled(true);

    if (state_ == State::Countdown && !paused_) {
        int count = std::max(1, static_cast<int>(std::ceil(timer_)));
        float frac = timer_ - std::floor(timer_);
        float pxSize = ctx_.px(64) * (0.8f + 0.4f * frac);
        for (int p = 0; p < 2; ++p) drawCentered(std::to_string(count), pxSize, Color(255, 255, 255, 230), p);
    }
    if (state_ == State::Goal) {
        float a = std::min(1.0f, timer_ * 2);
        for (int p = 0; p < 2; ++p) {
            bool scored = p == lastScorer_;
            drawCentered(scored ? "GOAL!" : "Conceded", ctx_.px(scored ? 46 : 26),
                         (scored ? (p == 0 ? p1_ : p2_) : Color(255, 255, 255, 160)).alpha(a), p);
        }
    }

    if (state_ == State::Start || state_ == State::Won) {
        r.fillRect(ctx_.screen, Color(5, 7, 10, 170));
        for (int p = 0; p < 2; ++p) {
            Color pc = p == 0 ? p1_ : p2_;
            if (state_ == State::Start) {
                drawCentered(p == 0 ? "Player 1" : "Player 2", ctx_.px(13), Color(255, 255, 255, 160), p, -66);
                drawCentered("Air Hockey", ctx_.px(32), Color::hex(0xFFFFFF), p, -32);
                drawCentered("First to " + std::to_string(phys_.config().winScore) + " wins", ctx_.px(16), pc, p, 8);
            } else {
                bool won = phys_.winner == p;
                drawCentered(won ? "YOU WIN!" : "You lose", ctx_.px(won ? 40 : 26), won ? pc : Color(255, 255, 255, 170), p, -20);
                drawCentered(std::to_string(phys_.score[p]) + " - " + std::to_string(phys_.score[1 - p]), ctx_.px(20),
                             Color(255, 255, 255, 200), p, 26);
            }
        }
        // Buttons on the centre line (usable by either player).
        const float bw = std::min(ctx_.px(130), (table_.w - ctx_.px(36)) * 0.5f), bh = ctx_.px(48);
        Rect play{table_.cx() + ctx_.px(6), table_.cy() - bh * 0.5f, bw, bh};
        Rect exitB{table_.cx() - ctx_.px(6) - bw, play.y, bw, bh};
        r.fillRoundRect({exitB.x - ctx_.px(10), play.y - ctx_.px(10), bw * 2 + ctx_.px(32), bh + ctx_.px(20)}, ctx_.px(18),
                        th.surface);
        if (ui.button(play, state_ == State::Start ? "Start" : "Rematch", ButtonStyle::Primary, Icon::Play)) {
            ctx_.sfx(Sfx::Click);
            startMatch();
        }
        if (ui.button(exitB, "Exit", ButtonStyle::Secondary, Icon::Home)) requestExit();
    }

    if (paused_) {
        r.fillRect(ctx_.screen, th.overlay);
        const float cw = std::min(ctx_.px(280), ctx_.safe.w - ctx_.px(32));
        Rect card{ctx_.safe.cx() - cw * 0.5f, ctx_.safe.cy() - ctx_.px(120), cw, ctx_.px(240)};
        r.fillRoundRect(card, ctx_.px(16), th.surface);
        r.text("Paused", card.cx(), card.y + ctx_.px(18), ctx_.px(20), th.text, FontId::Bold, Align::Center);
        r.text(std::to_string(phys_.score[0]) + " - " + std::to_string(phys_.score[1]), card.cx(), card.y + ctx_.px(48),
               ctx_.px(14), th.textDim, FontId::Regular, Align::Center);
        Rect b{card.x + ctx_.px(16), card.y + ctx_.px(78), card.w - ctx_.px(32), ctx_.px(44)};
        if (ui.button(b, "Resume", ButtonStyle::Primary, Icon::Play)) {
            paused_ = false;
            finger_[0] = finger_[1] = 0;
            if (state_ == State::Playing) {
                savedVel_ = phys_.puck.vel;
                state_ = State::Countdown;  // short countdown so nobody is surprised
                timer_ = 1.5f;
                lastCount_ = -1;
            }
        }
        b.y += ctx_.px(50);
        if (ui.button(b, "Restart match", ButtonStyle::Secondary, Icon::Restart)) startMatch();
        b.y += ctx_.px(50);
        if (ui.button(b, "Exit to hub", ButtonStyle::Secondary, Icon::Home)) requestExit();
    }
}

}  // namespace

std::unique_ptr<Game> createAirHockeyGame(AppContext& ctx) { return std::make_unique<AirHockeyGame>(ctx); }

}  // namespace mgh
