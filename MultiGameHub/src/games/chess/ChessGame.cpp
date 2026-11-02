// Two-player chess on one device, styled after popular chess sites: flat green
// board, yellow move highlights, player bars with captured pieces and a status
// "clock" box. White sits at the bottom; Black's bar is rotated 180 degrees so
// it faces the player on the other side of the phone.
#include <SDL3/SDL.h>

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
using chess::Pawn;
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

// Piece atlas (assets/pieces/chessnut.png): order K Q R B N P, white then black.
// Row 0 = 160 px cells, row 1 (y = 160) = 64 px cells.
constexpr float kAtlasW = 1920.0f, kAtlasH = 224.0f;
constexpr float kBigCell = 160.0f, kSmallCell = 64.0f;

int atlasIndex(Piece p) {
    static const int order[7] = {0, 5, 4, 3, 2, 1, 0};  // by PieceType: Pawn=5 ... King=0
    return order[typeOf(p)] + (colorOf(p) == Black ? 6 : 0);
}

class ChessGame : public mgh::Game {
public:
    explicit ChessGame(AppContext& ctx) : mgh::Game(ctx) {
        Json theme = ctx.packTheme("chess");
        light_ = themeColor(theme, "light", Color::hex(0xEEEED2));
        dark_ = themeColor(theme, "dark", Color::hex(0x769656));
        showHistory_ = ctx.settings.getBool(keys::kChessShowHistory, true);
        // Piece artwork is loaded only while chess is open and freed on exit.
        if (SDL_Surface* s = SDL_LoadPNG(ctx.assetPath("pieces/chessnut.png").c_str())) {
            pieces_ = SDL_CreateTextureFromSurface(ctx.r.sdl(), s);
            SDL_DestroySurface(s);
            if (pieces_) {
                SDL_SetTextureBlendMode(pieces_, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(pieces_, SDL_SCALEMODE_LINEAR);
            }
        }
        if (!pieces_) SDL_Log("chess: piece atlas failed to load: %s", SDL_GetError());
    }
    ~ChessGame() override {
        if (pieces_) SDL_DestroyTexture(pieces_);
    }

    void onBack() override {
        if (promo_) { promo_.reset(); return; }
        if (confirmRestart_) { confirmRestart_ = false; return; }
        menuOpen_ = !menuOpen_;
    }

    bool wantsContinuousFrames() const override { return anim_.active || dragging_ || menuAnim_ > 0.001f; }

    void onPointer(const PointerEvent& e) override;
    void update(float dt) override {
        if (anim_.active) {
            anim_.t += dt / 0.15f;
            if (anim_.t >= 1) anim_.active = false;
        }
        const float target = menuOpen_ ? 1.0f : 0.0f;
        menuAnim_ = approach(menuAnim_, target, 16, dt);
        if (std::fabs(menuAnim_ - target) < 0.002f) menuAnim_ = target;
    }
    void render() override;

private:
    void layout();
    int squareAt(float x, float y) const;
    Rect squareRect(int sq) const;
    bool lightSquare(int sq) const { return ((fileOf(sq) + rankOf(sq)) & 1) != 0; }
    void tryMove(int from, int to, bool animate);
    void commit(const Move& m, bool animate);
    void drawPiece(Piece p, float cx, float cy, float size, float alpha = 1.0f);
    void drawBar(chess::Color c, const Rect& rc);
    void drawBoard();
    void drawHistory(const Rect& rc);
    bool toolButton(const Rect& rc, Icon icon, const std::string& label, bool enabled, bool active);
    void drawPromotion();
    void drawGameOver();
    void drawMenu();

    chess::Game game_;
    SDL_Texture* pieces_ = nullptr;
    Color light_, dark_;
    Rect board_, topBar_, bottomBar_, toolbar_, historyRect_;
    float sq_ = 0;
    int selected_ = -1;
    bool showHistory_ = true;

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

// ---------------------------------------------------------------- layout

void ChessGame::layout() {
    const Rect s = ctx_.safe;
    const float barH = ctx_.px(56);
    const float histH = showHistory_ ? ctx_.px(32) : 0;
    const float gap = ctx_.px(6);
    const float toolH = ctx_.px(60);
    toolbar_ = {ctx_.screen.x, s.bottom() - toolH, ctx_.screen.w, toolH};

    // The board uses the full width of the screen unless height is the limit.
    const float avail = toolbar_.y - s.y - histH - 2 * barH - 4 * gap;
    const float size = std::max(ctx_.px(200), std::min(s.w, avail));
    sq_ = std::floor(size / 8);
    const float bs = sq_ * 8;
    const float block = histH + (showHistory_ ? gap : 0) + barH + gap + bs + gap + barH;
    float y = s.y + std::max(0.0f, (toolbar_.y - s.y - block) * 0.5f);
    const float side = ctx_.px(10);
    historyRect_ = {s.x + side, y, s.w - 2 * side, histH};
    if (showHistory_) y += histH + gap;
    topBar_ = {s.x + side, y, s.w - 2 * side, barH};
    y += barH + gap;
    board_ = {std::round(s.cx() - bs * 0.5f), std::round(y), bs, bs};
    y += bs + gap;
    bottomBar_ = {s.x + side, y, s.w - 2 * side, barH};
}

int ChessGame::squareAt(float x, float y) const {
    if (!board_.contains(x, y)) return -1;
    int f = static_cast<int>((x - board_.x) / sq_);
    int rk = 7 - static_cast<int>((y - board_.y) / sq_);
    if (f < 0 || f > 7 || rk < 0 || rk > 7) return -1;
    return chess::squareAt(f, rk);
}

Rect ChessGame::squareRect(int sq) const {
    return {board_.x + fileOf(sq) * sq_, board_.y + (7 - rankOf(sq)) * sq_, sq_, sq_};
}

// ---------------------------------------------------------------- input / moves

void ChessGame::onPointer(const PointerEvent& e) {
    if (menuOpen_ || confirmRestart_ || promo_ || game_.isOver()) return;
    if (e.type == PointerEvent::Type::Down) {
        if (dragId_ != 0) return;
        int sq = squareAt(e.x, e.y);
        if (sq < 0) return;
        Piece pc = game_.position().board[sq];
        if (pc && colorOf(pc) == game_.position().side) {
            dragId_ = e.id;
            dragFrom_ = sq;
            dragX_ = downX_ = e.x;
            dragY_ = downY_ = e.y;
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
            selected_ = to;
        } else {
            selected_ = -1;
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

// ---------------------------------------------------------------- drawing

void ChessGame::drawPiece(Piece p, float cx, float cy, float size, float alpha) {
    if (!p || !pieces_) return;
    const int i = atlasIndex(p);
    const bool small = size <= 80.0f;  // pick the closest atlas resolution
    const float cell = small ? kSmallCell : kBigCell;
    const float u0 = i * cell / kAtlasW, u1 = (i + 1) * cell / kAtlasW;
    const float v0 = small ? kBigCell / kAtlasH : 0.0f, v1 = small ? 1.0f : kBigCell / kAtlasH;
    ctx_.r.image(pieces_, {cx - size * 0.5f, cy - size * 0.5f, size, size}, u0, v0, u1, v1,
                 Color(255, 255, 255, static_cast<uint8_t>(255 * alpha)));
}

void ChessGame::drawBoard() {
    auto& r = ctx_.r;
    const Position& pos = game_.position();
    const Color hlLight = Color::hex(0xF5F682), hlDark = Color::hex(0xB9CA43);
    auto highlight = [&](int sq) { r.fillRect(squareRect(sq), lightSquare(sq) ? hlLight : hlDark); };

    for (int sq = 0; sq < 64; ++sq) r.fillRect(squareRect(sq), lightSquare(sq) ? light_ : dark_);
    if (auto lm = game_.lastMove()) {
        highlight(lm->from);
        highlight(lm->to);
    }
    const int sel = dragging_ ? dragFrom_ : selected_;
    if (sel >= 0) highlight(sel);
    if (game_.isCheck()) r.fillRect(squareRect(pos.kingSquare(pos.side)), Color(235, 64, 52, 200));

    // Coordinates in the corner squares, in the opposite square colour.
    const float cpx = std::max(ctx_.px(9), sq_ * 0.2f);
    for (int i = 0; i < 8; ++i) {
        int fsq = chess::squareAt(i, 0);
        Rect fr = squareRect(fsq);
        r.text(std::string(1, static_cast<char>('a' + i)), fr.right() - cpx * 0.45f, fr.bottom() - cpx * 1.25f, cpx,
               lightSquare(fsq) ? dark_ : light_, FontId::Bold, Align::Right);
        int rsq = chess::squareAt(0, i);
        Rect rr = squareRect(rsq);
        r.text(std::string(1, static_cast<char>('1' + i)), rr.x + cpx * 0.3f, rr.y + cpx * 0.1f, cpx,
               lightSquare(rsq) ? dark_ : light_, FontId::Bold, Align::Left);
    }

    // Pieces.
    const float ps = sq_;
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = pos.board[sq];
        if (!p || (anim_.active && sq == anim_.to)) continue;
        Rect rc = squareRect(sq);
        drawPiece(p, rc.cx(), rc.cy(), ps, (dragging_ && sq == dragFrom_) ? 0.35f : 1.0f);
    }
    // Legal move hints on top of pieces.
    if (sel >= 0) {
        for (const Move& m : game_.movesFrom(sel)) {
            Rect rc = squareRect(m.to);
            if (m.flags & FlagCapture)
                r.strokeCircle(rc.cx(), rc.cy(), sq_ * 0.455f, sq_ * 0.09f, Color(0, 0, 0, 36));
            else
                r.fillCircle(rc.cx(), rc.cy(), sq_ * 0.165f, Color(0, 0, 0, 36));
        }
    }
    if (anim_.active) {
        Rect a = squareRect(anim_.from), b = squareRect(anim_.to);
        float t = easeOutCubic(anim_.t);
        Piece shown = pos.board[anim_.to];
        drawPiece(shown ? shown : anim_.piece, a.cx() + (b.cx() - a.cx()) * t, a.cy() + (b.cy() - a.cy()) * t, ps);
    }
    if (dragging_ && dragFrom_ >= 0) {
        int over = squareAt(dragX_, dragY_);
        if (over >= 0) r.strokeRoundRect(squareRect(over).inset(ctx_.px(1.5f)), 0, ctx_.px(3), Color(255, 255, 255, 170));
        drawPiece(pos.board[dragFrom_], dragX_, dragY_ - sq_ * 0.45f, sq_ * 1.25f);
    }
}

void ChessGame::drawBar(chess::Color c, const Rect& rc) {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    const bool rotated = c == Black;
    if (rotated) r.setRotate180(true, rc.cx(), rc.cy());

    // Avatar: the side's king on a contrasting square.
    const float av = ctx_.px(40);
    Rect avatar{rc.x, rc.cy() - av * 0.5f, av, av};
    r.fillRoundRect(avatar, ctx_.px(6), c == White ? Color::hex(0xEDEBE6) : Color::hex(0xBDBAB4));
    drawPiece(makePiece(c, King), avatar.cx(), avatar.cy(), av * 0.86f);

    // Status "clock" box on the right.
    const Position& pos = game_.position();
    const bool toMove = pos.side == c && !game_.isOver();
    std::string status = "Waiting";
    Color boxBg = th.surface2, boxFg = th.textDim;
    if (game_.isOver()) {
        Result res = game_.result();
        bool won = (res == Result::WhiteWinsCheckmate && c == White) || (res == Result::BlackWinsCheckmate && c == Black);
        if (won) { status = "Winner"; boxBg = th.success; boxFg = Color::hex(0x0B2018); }
        else if (isDraw(res)) { status = "Draw"; }
        else { status = "Checkmated"; boxBg = th.danger; boxFg = Color::hex(0xFFFFFF); }
    } else if (toMove) {
        status = game_.isCheck() ? "Check!" : "Your move";
        boxBg = game_.isCheck() ? th.danger : Color::hex(0xFFFFFF);
        boxFg = game_.isCheck() ? Color::hex(0xFFFFFF) : Color::hex(0x1A1A1A);
    }
    const float boxW = std::min(ctx_.px(112), rc.w * 0.34f);
    Rect box{rc.right() - boxW, rc.cy() - ctx_.px(18), boxW, ctx_.px(36)};
    r.fillRoundRect(box, ctx_.px(6), boxBg);
    r.textIn(status, box.inset(ctx_.px(6), 0), ctx_.px(14), boxFg, FontId::Bold);

    // Name and captured pieces between the avatar and the status box.
    const float tx = avatar.right() + ctx_.px(10);
    const float textW = box.x - ctx_.px(10) - tx;
    r.text(c == White ? "White" : "Black", tx, rc.y + ctx_.px(7), ctx_.px(15), th.text, FontId::Bold, Align::Left, textW);
    auto caps = game_.capturedBy(c);
    std::sort(caps.begin(), caps.end(), [](Piece a, Piece b) { return typeOf(a) < typeOf(b); });
    static const int kValue[7] = {0, 1, 3, 3, 5, 9, 0};
    int mine = 0, theirs = 0;
    for (Piece p : caps) mine += kValue[typeOf(p)];
    for (Piece p : game_.capturedBy(opposite(c))) theirs += kValue[typeOf(p)];
    const std::string adv = mine > theirs ? "+" + std::to_string(mine - theirs) : "";
    const float advW = adv.empty() ? 0 : r.measure(adv, ctx_.px(12), FontId::Bold) + ctx_.px(6);
    const float ps = ctx_.px(20);
    const float rowY = rc.y + ctx_.px(39);
    float step = ps * 0.62f;
    const float room = textW - advW - ps;
    if (caps.size() > 1 && step * (caps.size() - 1) > room) step = std::max(0.0f, room / (caps.size() - 1));
    float x = tx + ps * 0.5f;
    if (!caps.empty()) {
        // Light tray so black pieces stay visible on the dark bar.
        float span = step * (caps.size() - 1);
        for (size_t i = 1; i < caps.size(); ++i)
            if (typeOf(caps[i]) != typeOf(caps[i - 1]) && step >= ps * 0.6f) span += ps * 0.25f;
        r.fillRoundRect({tx - ctx_.px(3), rowY - ps * 0.5f - ctx_.px(1), span + ps + ctx_.px(6), ps + ctx_.px(2)}, ctx_.px(4),
                        Color::hex(0xD9D6CF));
    }
    for (size_t i = 0; i < caps.size(); ++i) {
        // Extra spacing between groups of different piece types.
        if (i > 0 && typeOf(caps[i]) != typeOf(caps[i - 1]) && step >= ps * 0.6f) x += ps * 0.25f;
        drawPiece(caps[i], x, rowY, ps);
        if (i + 1 < caps.size()) x += step;
    }
    if (!adv.empty()) {
        float ax = caps.empty() ? tx : x + ps * 0.5f + ctx_.px(8);
        r.text(adv, ax, rowY - ctx_.px(8), ctx_.px(12), th.textDim, FontId::Bold, Align::Left, box.x - ctx_.px(4) - ax);
    }
    if (rotated) r.setRotate180(false);
}

void ChessGame::drawHistory(const Rect& rc) {
    if (!showHistory_) return;
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    r.fillRoundRect(rc, ctx_.px(8), th.surface);
    const auto& recs = game_.records();
    const float fpx = ctx_.px(13);
    if (recs.empty()) {
        r.textIn("Moves will appear here", rc.inset(ctx_.px(10), 0), fpx, th.textFaint, FontId::Regular);
        return;
    }
    // Most recent moves, right-aligned, clipped to the strip.
    std::vector<std::string> tokens;
    for (size_t i = 0; i < recs.size(); ++i) {
        std::string t;
        if (i % 2 == 0) t = std::to_string(i / 2 + 1) + ". ";
        t += recs[i].san;
        tokens.push_back(t);
    }
    r.pushClip(rc.inset(ctx_.px(8), 0));
    float x = rc.right() - ctx_.px(12);
    const float left = rc.x + ctx_.px(10);
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        float w = r.measure(tokens[i], fpx, FontId::Bold);
        if (x - w < left) break;
        bool last = i == static_cast<int>(tokens.size()) - 1;
        if (last) r.fillRoundRect({x - w - ctx_.px(5), rc.cy() - ctx_.px(11), w + ctx_.px(10), ctx_.px(22)}, ctx_.px(5), th.surface2);
        r.textIn(tokens[i], {x - w, rc.y, w + ctx_.px(2), rc.h}, fpx, last ? th.text : th.textDim, FontId::Bold, Align::Left);
        x -= w + ctx_.px(14);
    }
    r.popClip();
}

bool ChessGame::toolButton(const Rect& rc, Icon icon, const std::string& label, bool enabled, bool active) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const bool down = enabled && ui.pressing(rc);
    if (down) r.fillRoundRect(rc.inset(ctx_.px(4)), ctx_.px(10), th.surfacePressed);
    Color c = !enabled ? th.textFaint.alpha(0.6f) : (active ? th.accent : th.text);
    const float is = ctx_.px(22);
    drawIcon(r, icon, {rc.cx() - is * 0.5f, rc.y + ctx_.px(9), is, is}, c);
    r.text(label, rc.cx(), rc.y + ctx_.px(36), ctx_.px(11.5f), c, FontId::Bold, Align::Center, rc.w - ctx_.px(6));
    return enabled && ui.tapped(rc);
}

void ChessGame::drawPromotion() {
    if (!promo_) return;
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const bool black = game_.position().side == Black;
    r.fillRect(ctx_.screen, Color(0, 0, 0, 120));
    const float cell = std::min(sq_ * 1.1f, (ctx_.safe.w - ctx_.px(48)) / 4);
    Rect panel{board_.cx() - cell * 2 - ctx_.px(12), board_.cy() - cell * 0.5f - ctx_.px(28), cell * 4 + ctx_.px(24),
               cell + ctx_.px(52)};
    if (black) r.setRotate180(true, panel.cx(), panel.cy());
    r.fillRoundRect(panel, ctx_.px(14), th.surface);
    r.text("Promote to", panel.cx(), panel.y + ctx_.px(12), ctx_.px(14), th.text, FontId::Bold, Align::Center,
           panel.w - ctx_.px(24));
    const PieceType opts[4] = {Queen, Rook, Bishop, Knight};
    for (int i = 0; i < 4; ++i) {
        Rect c{panel.x + ctx_.px(12) + i * cell, panel.y + ctx_.px(40), cell, cell};
        r.fillRoundRect(c.inset(ctx_.px(3)), ctx_.px(8), (i % 2 == 0) ? light_ : dark_);
        drawPiece(makePiece(game_.position().side, opts[i]), c.cx(), c.cy(), cell * 0.9f);
    }
    if (black) r.setRotate180(false);
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
    if (ui.tapped(ctx_.screen)) promo_.reset();
}

void ChessGame::drawGameOver() {
    if (!game_.isOver() || anim_.active) return;
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float w = std::min(board_.w - ctx_.px(32), ctx_.px(320));
    Rect card{board_.cx() - w * 0.5f, board_.cy() - ctx_.px(84), w, ctx_.px(168)};
    r.fillRoundRect(card, ctx_.px(14), th.surface);
    r.strokeRoundRect(card, ctx_.px(14), ctx_.px(1), th.border);
    Result res = game_.result();
    std::string title = isDraw(res) ? "Draw" : (res == Result::WhiteWinsCheckmate ? "White wins" : "Black wins");
    const float is = ctx_.px(26);
    drawIcon(r, isDraw(res) ? Icon::Flag : Icon::Trophy, {card.cx() - is * 0.5f, card.y + ctx_.px(14), is, is},
             isDraw(res) ? th.textDim : th.warning);
    r.text(title, card.cx(), card.y + ctx_.px(46), ctx_.px(20), th.text, FontId::Bold, Align::Center, card.w - ctx_.px(24));
    r.text(resultText(res), card.cx(), card.y + ctx_.px(74), ctx_.px(12.5f), th.textDim, FontId::Regular, Align::Center,
           card.w - ctx_.px(24));
    const float bw = (card.w - ctx_.px(36)) / 2;
    Rect b1{card.x + ctx_.px(12), card.bottom() - ctx_.px(56), bw, ctx_.px(44)};
    Rect b2{b1.right() + ctx_.px(12), b1.y, bw, b1.h};
    if (ui.button(b1, "Undo", ButtonStyle::Secondary, Icon::Undo)) {
        game_.undo();
        ctx_.sfx(Sfx::Click);
    }
    if (ui.button(b2, "New game", ButtonStyle::Primary)) {
        game_.reset();
        ctx_.sfx(Sfx::Click);
    }
}

void ChessGame::drawMenu() {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float w = std::min(ctx_.px(300), ctx_.safe.w - ctx_.px(32));
    if (confirmRestart_) {
        r.fillRect(ctx_.screen, th.overlay);
        Rect card{ctx_.safe.cx() - w * 0.5f, ctx_.safe.cy() - ctx_.px(80), w, ctx_.px(160)};
        r.fillRoundRect(card, ctx_.px(16), th.surface);
        r.text("Start a new game?", card.cx(), card.y + ctx_.px(22), ctx_.px(18), th.text, FontId::Bold, Align::Center,
               card.w - ctx_.px(32));
        r.text("The current game will be lost.", card.cx(), card.y + ctx_.px(52), ctx_.px(13), th.textDim,
               FontId::Regular, Align::Center, card.w - ctx_.px(32));
        const float bw = (card.w - ctx_.px(36)) / 2;
        Rect b1{card.x + ctx_.px(12), card.bottom() - ctx_.px(58), bw, ctx_.px(44)};
        Rect b2{b1.right() + ctx_.px(12), b1.y, bw, b1.h};
        if (ui.button(b1, "Cancel", ButtonStyle::Secondary)) confirmRestart_ = false;
        if (ui.button(b2, "New game", ButtonStyle::Danger)) {
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
    Rect card{ctx_.safe.cx() - w * 0.5f, ctx_.safe.cy() - ctx_.px(88) + (1 - menuAnim_) * ctx_.px(20), w, ctx_.px(176)};
    r.setGlobalAlpha(menuAnim_);
    r.fillRoundRect(card, ctx_.px(16), th.surface);
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
    r.fillRect(ctx_.screen, th.bg);

    const bool modal = menuOpen_ || confirmRestart_ || promo_.has_value();
    ui.setInputEnabled(!modal);

    drawHistory(historyRect_);
    drawBar(Black, topBar_);
    drawBar(White, bottomBar_);
    drawBoard();

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
            } else {
                selected_ = -1;
            }
        }
    }

    // Toolbar: icon over label, equal-width cells.
    r.fillRect({toolbar_.x, toolbar_.y, toolbar_.w, ctx_.screen.bottom() - toolbar_.y}, th.bgElevated);
    const float maxW = std::min(toolbar_.w, ctx_.px(480));
    const float cw = maxW / 4;
    const float x0 = toolbar_.cx() - maxW * 0.5f;
    if (toolButton({x0, toolbar_.y, cw, toolbar_.h}, Icon::Pause, "Menu", true, false)) menuOpen_ = true;
    if (toolButton({x0 + cw, toolbar_.y, cw, toolbar_.h}, Icon::Undo, "Undo", game_.canUndo(), false)) {
        game_.undo();
        selected_ = -1;
        anim_.active = false;
        ctx_.sfx(Sfx::Click);
    }
    if (toolButton({x0 + 2 * cw, toolbar_.y, cw, toolbar_.h}, Icon::List, "Moves", true, showHistory_)) {
        showHistory_ = !showHistory_;
        ctx_.settings.setBool(keys::kChessShowHistory, showHistory_);
    }
    if (toolButton({x0 + 3 * cw, toolbar_.y, cw, toolbar_.h}, Icon::Restart, "New", game_.canUndo(), false))
        confirmRestart_ = true;

    ui.setInputEnabled(true);
    if (!modal) drawGameOver();
    if (promo_) drawPromotion();
    drawMenu();
}

}  // namespace

std::unique_ptr<mgh::Game> createChessGame(AppContext& ctx) { return std::make_unique<ChessGame>(ctx); }

}  // namespace mgh
