#include "ChessRules.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace chess {

namespace {

constexpr int kKnightD[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int kKingD[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int kBishopD[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int kRookD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

inline bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

void addPawnMove(std::vector<Move>& out, int from, int to, uint8_t flags, Color side) {
    int r = rankOf(to);
    if ((side == White && r == 7) || (side == Black && r == 0)) {
        for (uint8_t t : {Queen, Rook, Bishop, Knight}) {
            out.push_back(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), t,
                               static_cast<uint8_t>(flags | FlagPromotion)});
        }
    } else {
        out.push_back(Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), NoType, flags});
    }
}

char pieceChar(PieceType t) {
    switch (t) {
        case Pawn: return 'p';
        case Knight: return 'n';
        case Bishop: return 'b';
        case Rook: return 'r';
        case Queen: return 'q';
        case King: return 'k';
        default: return '?';
    }
}

}  // namespace

std::string squareName(int sq) {
    std::string s;
    s += static_cast<char>('a' + fileOf(sq));
    s += static_cast<char>('1' + rankOf(sq));
    return s;
}

Position Position::startPosition() {
    return *fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

std::optional<Position> Position::fromFen(const std::string& fen) {
    Position p;
    std::istringstream in(fen);
    std::string placement, side, castle, ep;
    int half = 0, full = 1;
    if (!(in >> placement >> side)) return std::nullopt;
    if (!(in >> castle)) castle = "-";
    if (!(in >> ep)) ep = "-";
    if (!(in >> half)) half = 0;
    if (!(in >> full)) full = 1;

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8) return std::nullopt;
            --rank;
            file = 0;
            if (rank < 0) return std::nullopt;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return std::nullopt;
        } else {
            if (file > 7) return std::nullopt;
            Color col = std::isupper(static_cast<unsigned char>(c)) ? White : Black;
            PieceType t;
            switch (std::tolower(static_cast<unsigned char>(c))) {
                case 'p': t = Pawn; break;
                case 'n': t = Knight; break;
                case 'b': t = Bishop; break;
                case 'r': t = Rook; break;
                case 'q': t = Queen; break;
                case 'k': t = King; break;
                default: return std::nullopt;
            }
            p.board[squareAt(file, rank)] = makePiece(col, t);
            ++file;
        }
    }
    if (rank != 0 || file != 8) return std::nullopt;
    if (side == "w") p.side = White;
    else if (side == "b") p.side = Black;
    else return std::nullopt;
    p.castling = 0;
    if (castle != "-") {
        for (char c : castle) {
            switch (c) {
                case 'K': p.castling |= WK; break;
                case 'Q': p.castling |= WQ; break;
                case 'k': p.castling |= BK; break;
                case 'q': p.castling |= BQ; break;
                default: return std::nullopt;
            }
        }
    }
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || (ep[1] != '3' && ep[1] != '6')) return std::nullopt;
        p.ep = static_cast<int8_t>(squareAt(ep[0] - 'a', ep[1] - '1'));
    }
    p.halfmove = half;
    p.fullmove = full;
    // Exactly one king per side is required.
    int wk = 0, bk = 0;
    for (Piece pc : p.board) {
        if (pc == makePiece(White, King)) ++wk;
        if (pc == makePiece(Black, King)) ++bk;
    }
    if (wk != 1 || bk != 1) return std::nullopt;
    return p;
}

std::string Position::toFen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Piece pc = board[squareAt(f, r)];
            if (!pc) { ++empty; continue; }
            if (empty) { s += static_cast<char>('0' + empty); empty = 0; }
            char c = pieceChar(typeOf(pc));
            s += colorOf(pc) == White ? static_cast<char>(std::toupper(c)) : c;
        }
        if (empty) s += static_cast<char>('0' + empty);
        if (r) s += '/';
    }
    s += side == White ? " w " : " b ";
    std::string c;
    if (castling & WK) c += 'K';
    if (castling & WQ) c += 'Q';
    if (castling & BK) c += 'k';
    if (castling & BQ) c += 'q';
    s += c.empty() ? "-" : c;
    s += ' ';
    s += ep >= 0 ? squareName(ep) : "-";
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

int Position::kingSquare(Color c) const {
    Piece k = makePiece(c, King);
    for (int i = 0; i < 64; ++i)
        if (board[i] == k) return i;
    return -1;
}

bool isSquareAttacked(const Position& p, int sq, Color by) {
    const int f = fileOf(sq), r = rankOf(sq);
    // Pawns: a pawn of colour `by` attacks diagonally forward.
    const int pr = by == White ? r - 1 : r + 1;
    for (int df : {-1, 1}) {
        if (onBoard(f + df, pr) && p.board[squareAt(f + df, pr)] == makePiece(by, Pawn)) return true;
    }
    for (auto& d : kKnightD) {
        int nf = f + d[0], nr = r + d[1];
        if (onBoard(nf, nr) && p.board[squareAt(nf, nr)] == makePiece(by, Knight)) return true;
    }
    for (auto& d : kKingD) {
        int nf = f + d[0], nr = r + d[1];
        if (onBoard(nf, nr) && p.board[squareAt(nf, nr)] == makePiece(by, King)) return true;
    }
    for (auto& d : kBishopD) {
        for (int nf = f + d[0], nr = r + d[1]; onBoard(nf, nr); nf += d[0], nr += d[1]) {
            Piece pc = p.board[squareAt(nf, nr)];
            if (!pc) continue;
            if (colorOf(pc) == by && (typeOf(pc) == Bishop || typeOf(pc) == Queen)) return true;
            break;
        }
    }
    for (auto& d : kRookD) {
        for (int nf = f + d[0], nr = r + d[1]; onBoard(nf, nr); nf += d[0], nr += d[1]) {
            Piece pc = p.board[squareAt(nf, nr)];
            if (!pc) continue;
            if (colorOf(pc) == by && (typeOf(pc) == Rook || typeOf(pc) == Queen)) return true;
            break;
        }
    }
    return false;
}

bool inCheck(const Position& p) {
    int k = p.kingSquare(p.side);
    return k >= 0 && isSquareAttacked(p, k, opposite(p.side));
}

void generatePseudoLegal(const Position& p, std::vector<Move>& out) {
    const Color us = p.side, them = opposite(us);
    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = p.board[sq];
        if (!pc || colorOf(pc) != us) continue;
        const int f = fileOf(sq), r = rankOf(sq);
        const auto from = static_cast<uint8_t>(sq);
        switch (typeOf(pc)) {
            case Pawn: {
                const int dir = us == White ? 1 : -1;
                const int startRank = us == White ? 1 : 6;
                int nr = r + dir;
                if (onBoard(f, nr) && !p.board[squareAt(f, nr)]) {
                    addPawnMove(out, sq, squareAt(f, nr), FlagNone, us);
                    int nr2 = r + 2 * dir;
                    if (r == startRank && !p.board[squareAt(f, nr2)]) {
                        out.push_back(Move{from, static_cast<uint8_t>(squareAt(f, nr2)), NoType, FlagDoublePush});
                    }
                }
                for (int df : {-1, 1}) {
                    int nf = f + df;
                    if (!onBoard(nf, nr)) continue;
                    int to = squareAt(nf, nr);
                    Piece target = p.board[to];
                    if (target && colorOf(target) == them) {
                        addPawnMove(out, sq, to, FlagCapture, us);
                    } else if (to == p.ep) {
                        out.push_back(Move{from, static_cast<uint8_t>(to), NoType,
                                           static_cast<uint8_t>(FlagCapture | FlagEnPassant)});
                    }
                }
                break;
            }
            case Knight:
            case King: {
                const auto& ds = typeOf(pc) == Knight ? kKnightD : kKingD;
                for (auto& d : ds) {
                    int nf = f + d[0], nr = r + d[1];
                    if (!onBoard(nf, nr)) continue;
                    int to = squareAt(nf, nr);
                    Piece target = p.board[to];
                    if (!target) out.push_back(Move{from, static_cast<uint8_t>(to), NoType, FlagNone});
                    else if (colorOf(target) == them)
                        out.push_back(Move{from, static_cast<uint8_t>(to), NoType, FlagCapture});
                }
                if (typeOf(pc) == King) {
                    // Castling: king and rook unmoved (rights), squares empty, king not in
                    // check and does not pass through or land on an attacked square.
                    const int home = us == White ? 0 : 56;
                    if (sq == home + 4) {
                        const uint8_t kRight = us == White ? WK : BK;
                        const uint8_t qRight = us == White ? WQ : BQ;
                        if ((p.castling & kRight) && !p.board[home + 5] && !p.board[home + 6] &&
                            p.board[home + 7] == makePiece(us, Rook) && !isSquareAttacked(p, home + 4, them) &&
                            !isSquareAttacked(p, home + 5, them) && !isSquareAttacked(p, home + 6, them)) {
                            out.push_back(Move{from, static_cast<uint8_t>(home + 6), NoType, FlagCastle});
                        }
                        if ((p.castling & qRight) && !p.board[home + 3] && !p.board[home + 2] &&
                            !p.board[home + 1] && p.board[home + 0] == makePiece(us, Rook) &&
                            !isSquareAttacked(p, home + 4, them) && !isSquareAttacked(p, home + 3, them) &&
                            !isSquareAttacked(p, home + 2, them)) {
                            out.push_back(Move{from, static_cast<uint8_t>(home + 2), NoType, FlagCastle});
                        }
                    }
                }
                break;
            }
            case Bishop:
            case Rook:
            case Queen: {
                auto slide = [&](const int (*dirs)[2], int n) {
                    for (int i = 0; i < n; ++i) {
                        for (int nf = f + dirs[i][0], nr = r + dirs[i][1]; onBoard(nf, nr);
                             nf += dirs[i][0], nr += dirs[i][1]) {
                            int to = squareAt(nf, nr);
                            Piece target = p.board[to];
                            if (!target) {
                                out.push_back(Move{from, static_cast<uint8_t>(to), NoType, FlagNone});
                                continue;
                            }
                            if (colorOf(target) == them)
                                out.push_back(Move{from, static_cast<uint8_t>(to), NoType, FlagCapture});
                            break;
                        }
                    }
                };
                if (typeOf(pc) != Rook) slide(kBishopD, 4);
                if (typeOf(pc) != Bishop) slide(kRookD, 4);
                break;
            }
            default: break;
        }
    }
}

void makeMove(Position& p, const Move& m) {
    const Color us = p.side;
    Piece pc = p.board[m.from];
    const bool isPawn = typeOf(pc) == Pawn;
    const bool capture = (m.flags & FlagCapture) != 0;

    if (m.flags & FlagEnPassant) {
        int capSq = us == White ? m.to - 8 : m.to + 8;
        p.board[capSq] = kEmpty;
    }
    p.board[m.to] = (m.flags & FlagPromotion) ? makePiece(us, static_cast<PieceType>(m.promo)) : pc;
    p.board[m.from] = kEmpty;

    if (m.flags & FlagCastle) {
        const int home = us == White ? 0 : 56;
        if (m.to == home + 6) {
            p.board[home + 5] = p.board[home + 7];
            p.board[home + 7] = kEmpty;
        } else {
            p.board[home + 3] = p.board[home + 0];
            p.board[home + 0] = kEmpty;
        }
    }

    // Update castling rights when king/rook move or a rook is captured on its home square.
    auto clearFor = [&](int sq) {
        switch (sq) {
            case 4: p.castling &= ~(WK | WQ); break;
            case 60: p.castling &= ~(BK | BQ); break;
            case 0: p.castling &= ~WQ; break;
            case 7: p.castling &= ~WK; break;
            case 56: p.castling &= ~BQ; break;
            case 63: p.castling &= ~BK; break;
            default: break;
        }
    };
    clearFor(m.from);
    clearFor(m.to);

    p.ep = (m.flags & FlagDoublePush) ? static_cast<int8_t>((m.from + m.to) / 2) : -1;
    p.halfmove = (isPawn || capture) ? 0 : p.halfmove + 1;
    if (us == Black) ++p.fullmove;
    p.side = opposite(us);
}

void generateLegal(const Position& p, std::vector<Move>& out) {
    std::vector<Move> pseudo;
    pseudo.reserve(64);
    generatePseudoLegal(p, pseudo);
    out.clear();
    for (const Move& m : pseudo) {
        Position q = p;
        makeMove(q, m);
        int k = q.kingSquare(p.side);
        if (!isSquareAttacked(q, k, q.side)) out.push_back(m);
    }
}

uint64_t perft(const Position& p, int depth) {
    if (depth == 0) return 1;
    std::vector<Move> moves;
    generateLegal(p, moves);
    if (depth == 1) return moves.size();
    uint64_t n = 0;
    for (const Move& m : moves) {
        Position q = p;
        makeMove(q, m);
        n += perft(q, depth - 1);
    }
    return n;
}

bool insufficientMaterial(const Position& p) {
    int minors[2] = {0, 0};
    int bishopsOnColor[2] = {0, 0};  // by square colour (light/dark), any side
    int knights = 0, bishops = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece pc = p.board[sq];
        if (!pc) continue;
        switch (typeOf(pc)) {
            case King: break;
            case Knight:
                ++minors[colorOf(pc)];
                ++knights;
                break;
            case Bishop:
                ++minors[colorOf(pc)];
                ++bishops;
                ++bishopsOnColor[(fileOf(sq) + rankOf(sq)) & 1];
                break;
            default: return false;  // pawn, rook or queen: mate is possible
        }
    }
    const int total = minors[0] + minors[1];
    if (total == 0) return true;                                 // K vs K
    if (total == 1) return true;                                 // K+minor vs K
    if (knights == 0 && bishops > 0 && (bishopsOnColor[0] == 0 || bishopsOnColor[1] == 0))
        return true;                                             // only same-coloured bishops
    return false;
}

uint64_t repetitionKey(const Position& p) {
    // FNV-1a over the position state relevant for repetition (FIDE 9.2).
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    for (Piece pc : p.board) mix(pc);
    mix(static_cast<uint8_t>(p.side));
    mix(p.castling);
    int8_t ep = -1;
    if (p.ep >= 0) {
        std::vector<Move> legal;
        generateLegal(p, legal);
        for (const Move& m : legal)
            if (m.flags & FlagEnPassant) { ep = p.ep; break; }
    }
    mix(static_cast<uint8_t>(ep));
    return h;
}

std::string toSan(const Position& p, const Move& m) {
    std::string s;
    Piece pc = p.board[m.from];
    PieceType t = typeOf(pc);
    if (m.flags & FlagCastle) {
        s = fileOf(m.to) == 6 ? "O-O" : "O-O-O";
    } else {
        if (t != Pawn) {
            s += static_cast<char>(std::toupper(pieceChar(t)));
            // Disambiguation among identical pieces that can reach the same square.
            std::vector<Move> legal;
            generateLegal(p, legal);
            bool ambiguous = false, sameFile = false, sameRank = false;
            for (const Move& o : legal) {
                if (o.to != m.to || o.from == m.from || p.board[o.from] != pc) continue;
                ambiguous = true;
                if (fileOf(o.from) == fileOf(m.from)) sameFile = true;
                if (rankOf(o.from) == rankOf(m.from)) sameRank = true;
            }
            if (ambiguous) {
                if (!sameFile) s += static_cast<char>('a' + fileOf(m.from));
                else if (!sameRank) s += static_cast<char>('1' + rankOf(m.from));
                else s += squareName(m.from);
            }
        } else if (m.flags & FlagCapture) {
            s += static_cast<char>('a' + fileOf(m.from));
        }
        if (m.flags & FlagCapture) s += 'x';
        s += squareName(m.to);
        if (m.flags & FlagPromotion) {
            s += '=';
            s += static_cast<char>(std::toupper(pieceChar(static_cast<PieceType>(m.promo))));
        }
    }
    Position q = p;
    makeMove(q, m);
    if (inCheck(q)) {
        std::vector<Move> replies;
        generateLegal(q, replies);
        s += replies.empty() ? '#' : '+';
    }
    return s;
}

const char* resultText(Result r) {
    switch (r) {
        case Result::Ongoing: return "In progress";
        case Result::WhiteWinsCheckmate: return "Checkmate - White wins";
        case Result::BlackWinsCheckmate: return "Checkmate - Black wins";
        case Result::Stalemate: return "Stalemate - Draw";
        case Result::DrawThreefold: return "Threefold repetition - Draw";
        case Result::DrawFiftyMove: return "Fifty-move rule - Draw";
        case Result::DrawInsufficientMaterial: return "Insufficient material - Draw";
    }
    return "";
}

// ---------------------------------------------------------------- Game

void Game::reset() {
    pos_ = Position::startPosition();
    history_.clear();
    records_.clear();
    keys_.assign(1, repetitionKey(pos_));
    refresh();
}

bool Game::loadFen(const std::string& fen) {
    auto p = Position::fromFen(fen);
    if (!p) return false;
    pos_ = *p;
    history_.clear();
    records_.clear();
    keys_.assign(1, repetitionKey(pos_));
    refresh();
    return true;
}

void Game::refresh() {
    generateLegal(pos_, legal_);
    if (legal_.empty()) {
        result_ = inCheck(pos_) ? (pos_.side == White ? Result::BlackWinsCheckmate : Result::WhiteWinsCheckmate)
                                : Result::Stalemate;
        return;
    }
    if (insufficientMaterial(pos_)) { result_ = Result::DrawInsufficientMaterial; return; }
    if (pos_.halfmove >= 100) { result_ = Result::DrawFiftyMove; return; }
    // Threefold repetition: only positions since the last irreversible move can repeat.
    int count = 0;
    const uint64_t cur = keys_.back();
    const size_t n = keys_.size();
    const size_t window = std::min<size_t>(n, static_cast<size_t>(pos_.halfmove) + 1);
    for (size_t i = n - window; i < n; ++i)
        if (keys_[i] == cur) ++count;
    result_ = count >= 3 ? Result::DrawThreefold : Result::Ongoing;
}

std::vector<Move> Game::movesFrom(int sq) const {
    std::vector<Move> v;
    if (isOver()) return v;
    for (const Move& m : legal_)
        if (m.from == sq) v.push_back(m);
    return v;
}

std::optional<Move> Game::findMove(int from, int to, PieceType promo) const {
    for (const Move& m : legal_) {
        if (m.from != from || m.to != to) continue;
        if (m.flags & FlagPromotion) {
            if (m.promo == (promo == NoType ? Queen : promo)) return m;
        } else {
            return m;
        }
    }
    return std::nullopt;
}

bool Game::needsPromotionChoice(int from, int to) const {
    for (const Move& m : legal_)
        if (m.from == from && m.to == to && (m.flags & FlagPromotion)) return true;
    return false;
}

bool Game::play(const Move& want) {
    if (isOver()) return false;
    const Move* found = nullptr;
    for (const Move& m : legal_)
        if (m == want) { found = &m; break; }
    if (!found) return false;
    const Move m = *found;

    Record rec;
    rec.move = m;
    rec.moved = pos_.board[m.from];
    if (m.flags & FlagEnPassant) rec.captured = makePiece(opposite(pos_.side), Pawn);
    else rec.captured = pos_.board[m.to];
    rec.san = toSan(pos_, m);

    history_.push_back(pos_);
    makeMove(pos_, m);
    keys_.push_back(repetitionKey(pos_));
    records_.push_back(std::move(rec));
    refresh();
    return true;
}

bool Game::undo() {
    if (history_.empty()) return false;
    pos_ = history_.back();
    history_.pop_back();
    keys_.pop_back();
    records_.pop_back();
    refresh();
    return true;
}

std::optional<Move> Game::lastMove() const {
    if (records_.empty()) return std::nullopt;
    return records_.back().move;
}

std::vector<Piece> Game::capturedBy(Color by) const {
    std::vector<Piece> v;
    for (const Record& r : records_)
        if (r.captured && colorOf(r.moved) == by) v.push_back(r.captured);
    return v;
}

}  // namespace chess
