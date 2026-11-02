// Two-player chess on one device. White sits at the bottom, Black at the top;
// Black's status panel is drawn rotated 180 degrees so it faces that player.
#include <algorithm>
#include <cmath>
#include <optional>

#include "ChessRules.hpp"
#include "games/Game.hpp"

namespace mgh {

namespace {

using chess::Bishop;
using chess::Black;
using chess::colorOf;
using chess::fileOf;
using chess::FlagCapture;
using chess::isDraw;
using chess::King;
using chess::Knight;
using chess::makePiece;
using chess::Move;
using chess::opposite;
using chess::Piece;
using chess::PieceType;
using chess::Position;
using chess::Queen;
using chess::rankOf;
using chess::Result;
using chess::resultText;
using chess::Rook;
using chess::typeOf;
using chess::White;

constexpr uint32_t kFilledGlyph[7] = {0, 0x265F, 0x265E, 0x265D, 0x265C, 0x265B, 0x265A};
constexpr uint32_t kOutlineGlyph[7] = {0, 0x2659, 0x2658, 0x2657, 0x2656, 0x2655, 0x2654};

class ChessGame : public mgh::Game {
public:
    explicit ChessGame(AppContext& ctx) : mgh::Game(ctx) {
        Json theme = ctx.packTheme("chess");
        light_ = themeColor(theme, "light", Color::hex(0xEBD9B4));
        dark_ = themeColor(theme, "dark", Color::hex(0xB07D54));
        frame_ = themeColor(theme, "frame", Color::hex(0x3A2A1E));
        showHistory_ = ctx.settings.getBool(keys::kChessShowHistory, true);
    }

    void onBack() override {
        if (promo_) { promo_.reset(); return; }
        if (confirmRestart_) { confirmRestart_ = false; return; }
        menuOpen_ = !menuOpen_;
    }

    bool wantsContinuousFrames() const override { return anim_.active || dragging_ || menuAnim_ > 0.001f; }

    void onPointer(const PointerEvent& e) override {
        if (menuOpen_ || confirmRestart_ || promo_ || game_.isOver()) return;
        if (e.type == PointerEvent::Type::Down) {
            if (dragId_ != 0) return;  // single active drag
            int sq = squareAt(e.x, e.y);
            if (sq < 0) return;
            Piece pc = game_.position().board[sq];
            if (pc && colorOf(pc) == game_.position().side) {
                dragId_ = e.id;
                dragFrom_ = sq;
                dragX_ = e.x;
                dragY_ = e.y;
                downX_ = e.x;
                downY_ = e.y;
                dragging_ = false;
            }
        } else if (e.type == PointerEvent::Type::Move && e.id == dragId_) {
            dragX_ = e.x;
            dragY_ = e.y;
            if (std::hypot(e.x - downX_, e.y - downY_) > ctx_.px(10)) {
                dragging_ = true;
                selected_ = dragFrom_;
            }
        } else if ((e.type == PointerEvent::Type::Up || e.type == PointerEvent::Type::Cancel) && e.id == dragId_) {
            if (dragging_ && e.type == PointerEvent::Type::Up) {
                int to = squareAt(e.x, e.y);
                if (to >= 0 && to != dragFrom_) tryMove(dragFrom_, to, false);
            }
            dragging_ = false;
            dragId_ = 0;
        }
    }

    void update(float dt) override {
        if (anim_.active) {
            anim_.t += dt / 0.16f;
            if (anim_.t >= 1) anim_.active = false;
        }
        menuAnim_ = approach(menuAnim_, menuOpen_ ? 1.0f : 0.0f, 14, dt);
        if (std::fabs(menuAnim_ - (menuOpen_ ? 1.0f : 0.0f)) < 0.002f) menuAnim_ = menuOpen_ ? 1.0f : 0.0f;
    }

    void render() override;

private:
    void layout();
    int squareAt(float x, float y) const;
    Rect squareRect(int sq) const;
    void tryMove(int from, int to, bool animate);
    void commit(const Move& m, bool animate);
    void drawPiece(Piece p, float cx, float cy, float size, float alpha = 1.0f);
    void drawPanel(Color side, const Rect& rc, bool rotated);
    void drawBoard();
    void drawHistory(const Rect& rc);
    void drawPromotion();
    void drawGameOver();
    void drawMenu();

    chess::Game game_;
    Color light_, dark_, frame_;
    Rect board_, topPanel_, bottomPanel_, toolbar_, historyRect_;
    float sqSize_ = 0;
    int selected_ = -1;
    bool showHistory_ = true;

    // drag
    uint64_t dragId_ = 0;
    int dragFrom_ = -1;
    bool dragging_ = false;
    float dragX_ = 0, dragY_ = 0, downX_ = 0, downY_ = 0;

    struct Anim {
        bool active = false;
        int from = 0, to = 0;
        Piece piece = 0;
        float t = 0;
    } anim_;

    struct Promo {
        int from, to;
    };
    std::optional<Promo> promo_;
    bool menuOpen_ = false;
    float menuAnim_ = 0;
    bool confirmRestart_ = false;
};

void ChessGame::layout() {
    const Rect s = ctx_.safe;
    const float m = ctx_.px(12);
    const float toolbarH = ctx_.px(64);
    const float panelH = ctx_.px(64);
    const float histH = showHistory_ ? ctx_.px(34) : 0;
    toolbar_ = {s.x + m, s.bottom() - toolbarH - ctx_.px(4), s.w - 2 * m, toolbarH};
    const float availH = toolbar_.y - s.y - 2 * panelH - histH - 4 * m;
    const float size = std::max(ctx_.px(160), std::min(s.w - 2 * m, availH));
    sqSize_ = std::floor(size / 8);
    const float bs = sqSize_ * 8;
    const float totalH = panelH * 2 + bs + histH + m * 3;
    float y = s.y + std::max(m, (toolbar_.y - s.y - totalH) * 0.5f);
    topPanel_ = {s.x + m, y, s.w - 2 * m, panelH};
    y += panelH + m;
    board_ = {std::round(s.cx() - bs * 0.5f), std::round(y), bs, bs};
    y += bs + m;
    historyRect_ = {s.x + m, y, s.w - 2 * m, histH};
    y += histH + (showHistory_ ? m : 0);
    bottomPanel_ = {s.x + m, y, s.w - 2 * m, panelH};
}

int ChessGame::squareAt(float x, float y) const {
    if (!board_.contains(x, y)) return -1;
    int f = static_cast<int>((x - board_.x) / sqSize_);
    int rk = 7 - static_cast<int>((y - board_.y) / sqSize_);
    if (f < 0 || f > 7 || rk < 0 || rk > 7) return -1;
    return chess::squareAt(f, rk);
}

Rect ChessGame::squareRect(int sq) const {
    return {board_.x + fileOf(sq) * sqSize_, board_.y + (7 - rankOf(sq)) * sqSize_, sqSize_, sqSize_};
}

void ChessGame::tryMove(int from, int to, bool animate) {
    if (game_.needsPromotionChoice(from, to)) {
        promo_ = Promo{from, to};
        selected_ = -1;
        return;
    }
    auto m = game_.findMove(from, to);
    if (!m) {
        Piece pc = game_.position().board[to];
        if (pc && colorOf(pc) == game_.position().side) {
            selected_ = to;  // switch selection to another own piece
        } else {
            selected_ = -1;
            ctx_.sfx(Sfx::Error, 0.5f);
        }
        return;
    }
    commit(*m, animate);
}

void ChessGame::commit(const Move& m, bool animate) {
    Piece moving = game_.position().board[m.from];
    if (!game_.play(m)) return;
    selected_ = -1;
    if (animate) anim_ = Anim{true, m.from, m.to, moving, 0};
    const auto& rec = game_.records().back();
    if (game_.isOver()) {
        ctx_.sfx(Sfx::GameEnd);
        ctx_.vibrate(60);
    } else if (game_.isCheck()) {
        ctx_.sfx(Sfx::Check);
        ctx_.vibrate(25);
    } else {
        ctx_.sfx(rec.captured ? Sfx::Capture : Sfx::Move);
    }
}

void ChessGame::drawPiece(Piece p, float cx, float cy, float size, float alpha) {
    if (!p) return;
    auto& r = ctx_.r;
    const PieceType t = typeOf(p);
    const float px = size * 0.86f;
    const float yOff = size * 0.02f;
    if (colorOf(p) == White) {
        r.glyphCentered(kFilledGlyph[t], cx, cy + yOff + size * 0.03f, px, Color(0, 0, 0, 60).alpha(alpha), FontId::Chess);
        r.glyphCentered(kFilledGlyph[t], cx, cy + yOff, px, Color::hex(0xFBFAF7).alpha(alpha), FontId::Chess);
        r.glyphCentered(kOutlineGlyph[t], cx, cy + yOff, px, Color::hex(0x232323).alpha(alpha), FontId::Chess);
    } else {
        r.glyphCentered(kFilledGlyph[t], cx, cy + yOff + size * 0.03f, px, Color(0, 0, 0, 50).alpha(alpha), FontId::Chess);
        r.glyphCentered(kOutlineGlyph[t], cx, cy + yOff, px, Color::hex(0xD8D2C4).alpha(alpha * 0.9f), FontId::Chess);
        r.glyphCentered(kFilledGlyph[t], cx, cy + yOff, px * 0.97f, Color::hex(0x1E1E22).alpha(alpha), FontId::Chess);
    }
}

void ChessGame::drawBoard() {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    const Position& pos = game_.position();
    // Frame and shadow.
    r.fillRoundRect(board_.inset(-ctx_.px(6)).offset(0, ctx_.px(3)), ctx_.px(10), Color(0, 0, 0, 70));
    r.fillRoundRect(board_.inset(-ctx_.px(6)), ctx_.px(10), frame_);
    for (int sq = 0; sq < 64; ++sq) {
        Rect rc = squareRect(sq);
        bool isLight = ((fileOf(sq) + rankOf(sq)) & 1) != 0;
        r.fillRect(rc, isLight ? light_ : dark_);
    }
    // Last move highlight.
    if (auto lm = game_.lastMove()) {
        for (int sq : {static_cast<int>(lm->from), static_cast<int>(lm->to)})
            r.fillRect(squareRect(sq), Color(255, 214, 64, 95));
    }
    // Check glow on the king.
    if (game_.isCheck()) {
        Rect kr = squareRect(pos.kingSquare(pos.side));
        r.glow(kr.cx(), kr.cy(), sqSize_ * 0.75f, Color(255, 40, 60, 210));
    }
    // Selected square + legal moves.
    const int sel = dragging_ ? dragFrom_ : selected_;
    if (sel >= 0) r.fillRect(squareRect(sel), th.accent.withA(110));

    // Coordinates.
    const float cpx = std::max(ctx_.px(8), sqSize_ * 0.17f);
    for (int i = 0; i < 8; ++i) {
        Rect fr = squareRect(chess::squareAt(i, 0));
        bool lightSq = (i & 1) != 0;
        r.text(std::string(1, static_cast<char>('a' + i)), fr.right() - cpx * 0.75f, fr.bottom() - cpx * 1.3f, cpx,
               (lightSq ? dark_ : light_).alpha(0.95f), FontId::Bold, Align::Center);
        Rect rr = squareRect(chess::squareAt(0, i));
        bool lightR = (i & 1) != 0;
        r.text(std::string(1, static_cast<char>('1' + i)), rr.x + cpx * 0.3f, rr.y + cpx * 0.15f, cpx,
               (lightR ? dark_ : light_).alpha(0.95f), FontId::Bold, Align::Left);
    }

    // Pieces.
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = pos.board[sq];
        if (!p) continue;
        if (anim_.active && sq == anim_.to) continue;
        if (dragging_ && sq == dragFrom_) {
            Rect rc = squareRect(sq);
            drawPiece(p, rc.cx(), rc.cy(), sqSize_, 0.35f);
            continue;
        }
        Rect rc = squareRect(sq);
        drawPiece(p, rc.cx(), rc.cy(), sqSize_);
    }
    // Move hints on top of pieces so captures are visible.
    if (sel >= 0) {
        for (const Move& m : game_.movesFrom(sel)) {
            Rect rc = squareRect(m.to);
            if (m.flags & FlagCapture)
                r.strokeCircle(rc.cx(), rc.cy(), sqSize_ * 0.44f, sqSize_ * 0.08f, Color(20, 20, 30, 120));
            else
                r.fillCircle(rc.cx(), rc.cy(), sqSize_ * 0.15f, Color(20, 20, 30, 95));
        }
    }
    if (anim_.active) {
        Rect a = squareRect(anim_.from), b = squareRect(anim_.to);
        float t = easeOutCubic(anim_.t);
        Piece shown = pos.board[anim_.to];  // shows promoted piece correctly
        drawPiece(shown ? shown : anim_.piece, a.cx() + (b.cx() - a.cx()) * t, a.cy() + (b.cy() - a.cy()) * t, sqSize_);
    }
    if (dragging_ && dragFrom_ >= 0) {
        int over = squareAt(dragX_, dragY_);
        if (over >= 0) r.strokeRoundRect(squareRect(over).inset(ctx_.px(1)), ctx_.px(2), ctx_.px(3), th.accent);
        drawPiece(pos.board[dragFrom_], dragX_, dragY_ - sqSize_ * 0.35f, sqSize_ * 1.25f);
    }
}

void ChessGame::drawPanel(Color side, const Rect& rc, bool rotated) {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    const Position& pos = game_.position();
    const Color player = side.r > 128 ? Color::hex(0xFFFFFF) : Color::hex(0x000000);
    const chess::Color c = side.r > 128 ? White : Black;
    const bool toMove = pos.side == c && !game_.isOver();
    if (rotated) r.setRotate180(true, rc.cx(), rc.cy());

    r.fillRoundRect(rc, ctx_.px(16), toMove ? th.surface2 : th.surface);
    if (toMove) r.strokeRoundRect(rc, ctx_.px(16), ctx_.px(2), th.accent);
    // Player chip.
    const float chip = rc.h * 0.56f;
    Rect chipR{rc.x + ctx_.px(12), rc.cy() - chip * 0.5f, chip, chip};
    r.fillCircle(chipR.cx(), chipR.cy(), chip * 0.5f, player);
    r.strokeCircle(chipR.cx(), chipR.cy(), chip * 0.5f, ctx_.px(1.5f), th.border);
    const float tx = chipR.right() + ctx_.px(10);
    r.text(c == White ? "White" : "Black", tx, rc.y + ctx_.px(10), ctx_.px(16), th.text, FontId::Bold);
    std::string status;
    Color statusC = th.textDim;
    if (game_.isOver()) {
        Result res = game_.result();
        if ((res == Result::WhiteWinsCheckmate && c == White) || (res == Result::BlackWinsCheckmate && c == Black)) {
            status = "Winner!";
            statusC = th.success;
        } else if (isDraw(res)) {
            status = "Draw";
        } else {
            status = "Checkmated";
            statusC = th.danger;
        }
    } else if (toMove) {
        status = game_.isCheck() ? "Your move - CHECK!" : "Your move";
        statusC = game_.isCheck() ? th.danger : th.accent;
    } else {
        status = "Waiting";
    }
    r.text(status, tx, rc.y + ctx_.px(33), ctx_.px(13), statusC, FontId::Regular);

    // Captured pieces (by this player) and material advantage.
    auto caps = game_.capturedBy(c);
    std::sort(caps.begin(), caps.end(), [](Piece a, Piece b) { return typeOf(a) > typeOf(b); });
    const float ps = ctx_.px(20);
    const float maxW = rc.right() - ctx_.px(12) - (tx + ctx_.px(100));
    float step = caps.empty() ? 0 : std::min(ps * 0.62f, maxW / std::max<size_t>(1, caps.size()));
    float x = rc.right() - ctx_.px(16) - step * (caps.size() > 0 ? caps.size() - 1 : 0) - ps * 0.5f;
    if (!caps.empty()) {
        // Light tray so captured black pieces stay visible on dark panels.
        Rect tray{x - ctx_.px(4), rc.cy() - ps * 0.62f, step * (caps.size() - 1) + ps + ctx_.px(8), ps * 1.24f};
        r.fillRoundRect(tray, ctx_.px(8), Color::hex(0xE8E2D4));
    }
    for (Piece p : caps) {
        drawPiece(p, x + ps * 0.5f, rc.cy(), ps);
        x += step;
    }
    static const int kValue[7] = {0, 1, 3, 3, 5, 9, 0};
    int mine = 0, theirs = 0;
    for (Piece p : game_.capturedBy(c)) mine += kValue[typeOf(p)];
    for (Piece p : game_.capturedBy(opposite(c))) theirs += kValue[typeOf(p)];
    if (mine > theirs) {
        r.text("+" + std::to_string(mine - theirs), rc.right() - ctx_.px(14), rc.y + ctx_.px(4), ctx_.px(11),
               th.textDim, FontId::Bold, Align::Right);
    }
    if (rotated) r.setRotate180(false);
}

void ChessGame::drawHistory(const Rect& rc) {
    if (!showHistory_) return;
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    r.fillRoundRect(rc, rc.h * 0.5f, th.surface);
    const auto& recs = game_.records();
    if (recs.empty()) {
        r.textIn("Move history appears here", rc, ctx_.px(12.5f), th.textFaint, FontId::Regular);
        return;
    }
    // Build tokens and show the most recent ones that fit, right-aligned.
    std::vector<std::string> tokens;
    for (size_t i = 0; i < recs.size(); ++i) {
        std::string t;
        if (i % 2 == 0) t = std::to_string(i / 2 + 1) + ". ";
        t += recs[i].san;
        tokens.push_back(t);
    }
    const float fpx = ctx_.px(13);
    const float avail = rc.w - ctx_.px(24);
    float x = rc.right() - ctx_.px(12);
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        float w = r.measure(tokens[i], fpx, FontId::Bold);
        if (rc.right() - ctx_.px(12) - (x - w) > avail) break;
        Color col = i == static_cast<int>(tokens.size()) - 1 ? th.accent : th.textDim;
        r.textIn(tokens[i], {x - w, rc.y, w, rc.h}, fpx, col, FontId::Bold, Align::Left);
        x -= w + ctx_.px(10);
    }
}

void ChessGame::drawPromotion() {
    if (!promo_) return;
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const bool black = game_.position().side == Black;
    r.fillRect(ctx_.screen, th.overlay.alpha(0.6f));
    const float cell = sqSize_ * 1.1f;
    Rect panel{board_.cx() - cell * 2 - ctx_.px(12), board_.cy() - cell * 0.5f - ctx_.px(40), cell * 4 + ctx_.px(24),
               cell + ctx_.px(52)};
    if (black) r.setRotate180(true, panel.cx(), panel.cy());
    r.fillRoundRect(panel, ctx_.px(18), th.surface);
    r.text("Promote pawn to", panel.cx(), panel.y + ctx_.px(12), ctx_.px(14), th.text, FontId::Bold, Align::Center);
    const PieceType opts[4] = {Queen, Rook, Bishop, Knight};
    for (int i = 0; i < 4; ++i) {
        Rect c{panel.x + ctx_.px(12) + i * cell, panel.y + ctx_.px(40), cell, cell};
        r.fillRoundRect(c.inset(ctx_.px(3)), ctx_.px(12), ui.pressing(c) ? th.accentSoft : th.surface2);
        drawPiece(makePiece(game_.position().side, opts[i]), c.cx(), c.cy(), cell * 0.95f);
    }
    if (black) r.setRotate180(false);
    // Hit testing in screen space (rotate the rects too when needed).
    for (int i = 0; i < 4; ++i) {
        Rect c{panel.x + ctx_.px(12) + i * cell, panel.y + ctx_.px(40), cell, cell};
        if (black) c = {2 * panel.cx() - c.right(), 2 * panel.cy() - c.bottom(), c.w, c.h};
        if (ui.tapped(c)) {
            auto m = game_.findMove(promo_->from, promo_->to, opts[i]);
            promo_.reset();
            if (m) commit(*m, true);
            return;
        }
    }
    if (ui.tapped(ctx_.screen)) promo_.reset();  // tap outside cancels
}

void ChessGame::drawGameOver() {
    if (!game_.isOver() || anim_.active) return;
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    Rect card{board_.x + board_.w * 0.08f, board_.cy() - ctx_.px(80), board_.w * 0.84f, ctx_.px(160)};
    r.fillRoundRect(card.offset(0, ctx_.px(4)), ctx_.px(20), Color(0, 0, 0, 90));
    r.fillRoundRect(card, ctx_.px(20), th.surface);
    Result res = game_.result();
    std::string title = isDraw(res) ? "Draw" : (res == Result::WhiteWinsCheckmate ? "White wins!" : "Black wins!");
    drawIcon(r, isDraw(res) ? Icon::Flag : Icon::Trophy, {card.cx() - ctx_.px(14), card.y + ctx_.px(12), ctx_.px(28), ctx_.px(28)},
             isDraw(res) ? th.textDim : th.warning);
    r.text(title, card.cx(), card.y + ctx_.px(44), ctx_.px(22), th.text, FontId::Bold, Align::Center);
    std::string reason = resultText(res);
    r.text(reason, card.cx(), card.y + ctx_.px(74), ctx_.px(13), th.textDim, FontId::Regular, Align::Center);
    float bw = (card.w - ctx_.px(36)) / 2;
    Rect b1{card.x + ctx_.px(12), card.bottom() - ctx_.px(56), bw, ctx_.px(44)};
    Rect b2{b1.right() + ctx_.px(12), b1.y, bw, b1.h};
    if (ui.button(b1, "Undo", ButtonStyle::Secondary, Icon::Undo)) {
        game_.undo();
        ctx_.sfx(Sfx::Click);
    }
    if (ui.button(b2, "Play again", ButtonStyle::Primary, Icon::Restart)) {
        game_.reset();
        ctx_.sfx(Sfx::Click);
    }
}

void ChessGame::drawMenu() {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    if (confirmRestart_) {
        r.fillRect(ctx_.screen, th.overlay);
        Rect card{ctx_.safe.cx() - ctx_.px(150), ctx_.safe.cy() - ctx_.px(80), ctx_.px(300), ctx_.px(160)};
        r.fillRoundRect(card, ctx_.px(20), th.surface);
        r.text("Restart the game?", card.cx(), card.y + ctx_.px(22), ctx_.px(18), th.text, FontId::Bold, Align::Center);
        r.text("The current position will be lost.", card.cx(), card.y + ctx_.px(52), ctx_.px(13), th.textDim,
               FontId::Regular, Align::Center);
        float bw = (card.w - ctx_.px(36)) / 2;
        Rect b1{card.x + ctx_.px(12), card.bottom() - ctx_.px(58), bw, ctx_.px(44)};
        Rect b2{b1.right() + ctx_.px(12), b1.y, bw, b1.h};
        if (ui.button(b1, "Cancel", ButtonStyle::Secondary)) confirmRestart_ = false;
        if (ui.button(b2, "Restart", ButtonStyle::Danger)) {
            confirmRestart_ = false;
            game_.reset();
            selected_ = -1;
            anim_.active = false;
            ctx_.sfx(Sfx::Click);
        }
        return;
    }
    if (menuAnim_ <= 0.001f) return;
    r.fillRect(ctx_.screen, th.overlay.alpha(menuAnim_));
    Rect card{ctx_.safe.cx() - ctx_.px(140), ctx_.safe.cy() - ctx_.px(88) + (1 - menuAnim_) * ctx_.px(30),
              ctx_.px(280), ctx_.px(176)};
    r.setGlobalAlpha(menuAnim_);
    r.fillRoundRect(card, ctx_.px(22), th.surface);
    r.text("Paused", card.cx(), card.y + ctx_.px(18), ctx_.px(20), th.text, FontId::Bold, Align::Center);
    Rect b{card.x + ctx_.px(16), card.y + ctx_.px(60), card.w - ctx_.px(32), ctx_.px(44)};
    if (ui.button(b, "Resume", ButtonStyle::Primary, Icon::Play)) menuOpen_ = false;
    b.y += ctx_.px(52);
    if (ui.button(b, "Exit to hub", ButtonStyle::Secondary, Icon::Home)) requestExit();
    r.setGlobalAlpha(1.0f);
    if (menuOpen_ && ui.tapped(ctx_.screen)) menuOpen_ = false;
}

void ChessGame::render() {
    layout();
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    r.fillRectGradient(ctx_.screen, th.bg, th.bgElevated);

    const bool modal = menuOpen_ || confirmRestart_ || promo_.has_value();
    ui.setInputEnabled(!modal);

    drawPanel(Color::hex(0x000000), topPanel_, true);
    drawPanel(Color::hex(0xFFFFFF), bottomPanel_, false);
    drawBoard();
    drawHistory(historyRect_);

    // Board taps (selection / move) — only when not dragging.
    if (!modal && !game_.isOver()) {
        for (const auto& p : ui.pointers()) {
            if (!p.releasedNow || p.dragged || p.consumed) continue;
            int sq = squareAt(p.x, p.y);
            if (sq < 0 || squareAt(p.sx, p.sy) != sq) continue;
            Piece pc = game_.position().board[sq];
            if (selected_ >= 0 && sq != selected_) {
                tryMove(selected_, sq, true);
            } else if (pc && colorOf(pc) == game_.position().side) {
                selected_ = (selected_ == sq) ? -1 : sq;
                ctx_.sfx(Sfx::Click, 0.5f);
            } else {
                selected_ = -1;
            }
        }
    }

    // Toolbar.
    const float gap = ctx_.px(10);
    const float bw = (toolbar_.w - gap * 3) / 4;
    Rect b{toolbar_.x, toolbar_.y + ctx_.px(8), bw, toolbar_.h - ctx_.px(16)};
    if (ui.button(b, "Menu", ButtonStyle::Secondary, Icon::Pause)) menuOpen_ = true;
    b.x += bw + gap;
    if (ui.button(b, "Undo", ButtonStyle::Secondary, Icon::Undo, game_.canUndo())) {
        game_.undo();
        selected_ = -1;
        anim_.active = false;
        ctx_.sfx(Sfx::Click);
    }
    b.x += bw + gap;
    if (ui.button(b, "Moves", showHistory_ ? ButtonStyle::Primary : ButtonStyle::Secondary, Icon::List)) {
        showHistory_ = !showHistory_;
        ctx_.settings.setBool(keys::kChessShowHistory, showHistory_);
    }
    b.x += bw + gap;
    if (ui.button(b, "New", ButtonStyle::Secondary, Icon::Restart, game_.canUndo())) confirmRestart_ = true;

    ui.setInputEnabled(true);
    if (!menuOpen_ && !confirmRestart_ && !promo_) drawGameOver();
    if (promo_) drawPromotion();
    drawMenu();
}

}  // namespace

std::unique_ptr<mgh::Game> createChessGame(AppContext& ctx) { return std::make_unique<ChessGame>(ctx); }

}  // namespace mgh
