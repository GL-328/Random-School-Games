// Complete standard chess rules: legal move generation, check, checkmate,
// stalemate, castling, en passant, promotion, threefold repetition,
// fifty-move rule and insufficient material. Pure C++ (no SDL) so it can be
// unit-tested on any platform.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chess {

enum Color : uint8_t { White = 0, Black = 1 };
inline Color opposite(Color c) { return c == White ? Black : White; }

enum PieceType : uint8_t { NoType = 0, Pawn = 1, Knight = 2, Bishop = 3, Rook = 4, Queen = 5, King = 6 };

// Piece encoding: 0 = empty, otherwise (type | color << 3).
using Piece = uint8_t;
constexpr Piece kEmpty = 0;
inline Piece makePiece(Color c, PieceType t) { return static_cast<Piece>(t | (c << 3)); }
inline PieceType typeOf(Piece p) { return static_cast<PieceType>(p & 7); }
inline Color colorOf(Piece p) { return static_cast<Color>((p >> 3) & 1); }

// Squares: a1 = 0, b1 = 1, ..., h8 = 63.
inline int fileOf(int sq) { return sq & 7; }
inline int rankOf(int sq) { return sq >> 3; }
inline int squareAt(int file, int rank) { return rank * 8 + file; }
std::string squareName(int sq);

enum MoveFlag : uint8_t {
    FlagNone = 0,
    FlagCapture = 1,
    FlagEnPassant = 2,
    FlagCastle = 4,
    FlagDoublePush = 8,
    FlagPromotion = 16,
};

struct Move {
    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t promo = NoType;  // PieceType when FlagPromotion
    uint8_t flags = FlagNone;
    bool operator==(const Move& o) const {
        return from == o.from && to == o.to && promo == o.promo;
    }
};

enum CastleRight : uint8_t { WK = 1, WQ = 2, BK = 4, BQ = 8 };

struct Position {
    std::array<Piece, 64> board{};
    Color side = White;
    uint8_t castling = 0;
    int8_t ep = -1;  // en passant target square or -1
    int halfmove = 0;
    int fullmove = 1;

    static Position startPosition();
    static std::optional<Position> fromFen(const std::string& fen);
    std::string toFen() const;
    int kingSquare(Color c) const;
};

bool isSquareAttacked(const Position& p, int sq, Color by);
bool inCheck(const Position& p);
void generatePseudoLegal(const Position& p, std::vector<Move>& out);
void generateLegal(const Position& p, std::vector<Move>& out);
// Applies a (pseudo-)legal move. Caller guarantees validity.
void makeMove(Position& p, const Move& m);
uint64_t perft(const Position& p, int depth);
bool insufficientMaterial(const Position& p);
// Key used for repetition detection (board, side, castling, *capturable* ep).
uint64_t repetitionKey(const Position& p);
// Standard Algebraic Notation for a legal move in position p.
std::string toSan(const Position& p, const Move& m);

enum class Result {
    Ongoing,
    WhiteWinsCheckmate,
    BlackWinsCheckmate,
    Stalemate,
    DrawThreefold,
    DrawFiftyMove,
    DrawInsufficientMaterial,
};
const char* resultText(Result r);
inline bool isDraw(Result r) {
    return r == Result::Stalemate || r == Result::DrawThreefold ||
           r == Result::DrawFiftyMove || r == Result::DrawInsufficientMaterial;
}

// A game: position + history with undo, results and move records.
class Game {
public:
    struct Record {
        Move move;
        Piece moved = kEmpty;
        Piece captured = kEmpty;
        std::string san;
    };

    Game() { reset(); }
    void reset();
    bool loadFen(const std::string& fen);

    const Position& position() const { return pos_; }
    const std::vector<Move>& legalMoves() const { return legal_; }
    std::vector<Move> movesFrom(int sq) const;
    std::optional<Move> findMove(int from, int to, PieceType promo = NoType) const;
    bool needsPromotionChoice(int from, int to) const;

    bool play(const Move& m);  // returns false if the move is illegal
    bool undo();
    bool canUndo() const { return !history_.empty(); }

    Result result() const { return result_; }
    bool isOver() const { return result_ != Result::Ongoing; }
    bool isCheck() const { return inCheck(pos_); }
    const std::vector<Record>& records() const { return records_; }
    std::optional<Move> lastMove() const;
    // Pieces captured BY `by` (i.e. of the opposite colour), in capture order.
    std::vector<Piece> capturedBy(Color by) const;

private:
    void refresh();

    Position pos_;
    std::vector<Position> history_;
    std::vector<uint64_t> keys_;  // repetition keys of all positions incl. current
    std::vector<Record> records_;
    std::vector<Move> legal_;
    Result result_ = Result::Ongoing;
};

}  // namespace chess
