#include <cstring>

#include "TestFramework.hpp"
#include "games/chess/ChessRules.hpp"

using namespace chess;

static int sq(const char* s) { return squareAt(s[0] - 'a', s[1] - '1'); }

static bool playUci(Game& g, const char* uci) {
    int from = sq(uci), to = sq(uci + 2);
    PieceType promo = NoType;
    if (std::strlen(uci) > 4) {
        switch (uci[4]) {
            case 'q': promo = Queen; break;
            case 'r': promo = Rook; break;
            case 'b': promo = Bishop; break;
            case 'n': promo = Knight; break;
        }
    }
    auto m = g.findMove(from, to, promo);
    if (!m) return false;
    return g.play(*m);
}

// Reference node counts from https://www.chessprogramming.org/Perft_Results
TEST_CASE("chess perft: start position") {
    Position p = Position::startPosition();
    CHECK_EQ(perft(p, 1), 20ull);
    CHECK_EQ(perft(p, 2), 400ull);
    CHECK_EQ(perft(p, 3), 8902ull);
    CHECK_EQ(perft(p, 4), 197281ull);
}

TEST_CASE("chess perft: kiwipete (castling, ep, promotions, pins)") {
    auto p = Position::fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    REQUIRE(p.has_value());
    CHECK_EQ(perft(*p, 1), 48ull);
    CHECK_EQ(perft(*p, 2), 2039ull);
    CHECK_EQ(perft(*p, 3), 97862ull);
}

TEST_CASE("chess perft: position 3 (en passant edge cases)") {
    auto p = Position::fromFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    REQUIRE(p.has_value());
    CHECK_EQ(perft(*p, 1), 14ull);
    CHECK_EQ(perft(*p, 2), 191ull);
    CHECK_EQ(perft(*p, 3), 2812ull);
    CHECK_EQ(perft(*p, 4), 43238ull);
    CHECK_EQ(perft(*p, 5), 674624ull);
}

TEST_CASE("chess perft: position 4 (promotions, checks)") {
    auto p = Position::fromFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    REQUIRE(p.has_value());
    CHECK_EQ(perft(*p, 1), 6ull);
    CHECK_EQ(perft(*p, 2), 264ull);
    CHECK_EQ(perft(*p, 3), 9467ull);
}

TEST_CASE("chess perft: position 5") {
    auto p = Position::fromFen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
    REQUIRE(p.has_value());
    CHECK_EQ(perft(*p, 1), 44ull);
    CHECK_EQ(perft(*p, 2), 1486ull);
    CHECK_EQ(perft(*p, 3), 62379ull);
}

TEST_CASE("chess: illegal moves are rejected") {
    Game g;
    CHECK(!playUci(g, "e2e5"));  // pawn cannot move three
    CHECK(!playUci(g, "e7e5"));  // not black's turn
    CHECK(!playUci(g, "f1c4"));  // bishop blocked
    CHECK(!playUci(g, "e1e2"));  // own piece
    CHECK(playUci(g, "e2e4"));
    CHECK(playUci(g, "e7e5"));
    CHECK(playUci(g, "f1c4"));
}

TEST_CASE("chess: pinned piece cannot move, king cannot move into check") {
    Game g;
    REQUIRE(g.loadFen("4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1"));
    CHECK(g.movesFrom(sq("e2")).empty());       // knight pinned on the e-file
    CHECK(playUci(g, "e1f2"));                   // f2 is safe
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/3r4/4K3 w - - 0 1"));
    CHECK(!playUci(g, "e1e2"));  // e2 is attacked along the 2nd rank
    CHECK(playUci(g, "e1d2"));   // capturing the undefended rook is legal
}

TEST_CASE("chess: check and fool's mate") {
    Game g;
    CHECK(playUci(g, "f2f3"));
    CHECK(playUci(g, "e7e5"));
    CHECK(playUci(g, "g2g4"));
    CHECK(playUci(g, "d8h4"));
    CHECK(g.isCheck());
    CHECK(g.result() == Result::BlackWinsCheckmate);
    CHECK(g.records().back().san == "Qh4#");
    CHECK(g.legalMoves().empty());
}

TEST_CASE("chess: scholar's mate SAN and result") {
    Game g;
    for (const char* m : {"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"}) CHECK(playUci(g, m));
    CHECK(g.result() == Result::WhiteWinsCheckmate);
    CHECK(g.records().back().san == "Qxf7#");
}

TEST_CASE("chess: castling both sides and rights") {
    Game g;
    REQUIRE(g.loadFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(playUci(g, "e1g1"));
    CHECK(g.position().board[sq("f1")] == makePiece(White, Rook));
    CHECK(g.position().board[sq("g1")] == makePiece(White, King));
    CHECK(g.records().back().san == "O-O");
    CHECK(playUci(g, "e8c8"));
    CHECK(g.position().board[sq("d8")] == makePiece(Black, Rook));
    CHECK(g.records().back().san == "O-O-O");
    CHECK(g.position().castling == 0);
}

TEST_CASE("chess: cannot castle through or out of check") {
    Game g;
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1"));
    CHECK(!g.findMove(sq("e1"), sq("g1")).has_value());  // f1 attacked? f2 rook attacks f1
    CHECK(g.findMove(sq("e1"), sq("c1")).has_value());
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1"));
    CHECK(!g.findMove(sq("e1"), sq("g1")).has_value());  // in check
    CHECK(!g.findMove(sq("e1"), sq("c1")).has_value());
    // b1 attacked does not prevent queen-side castling (only king path matters).
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/1r6/R3K3 w Q - 0 1"));
    CHECK(g.findMove(sq("e1"), sq("c1")).has_value());
    // Rights lost after the rook moves.
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
    CHECK(playUci(g, "h1h2"));
    CHECK(playUci(g, "e8e7"));
    CHECK(!g.findMove(sq("e1"), sq("g1")).has_value());
}

TEST_CASE("chess: en passant capture and expiry") {
    Game g;
    for (const char* m : {"e2e4", "a7a6", "e4e5", "d7d5"}) CHECK(playUci(g, m));
    auto ep = g.findMove(sq("e5"), sq("d6"));
    REQUIRE(ep.has_value());
    CHECK((ep->flags & FlagEnPassant) != 0);
    CHECK(g.play(*ep));
    CHECK(g.position().board[sq("d5")] == kEmpty);
    CHECK(g.records().back().san == "exd6");
    CHECK(g.capturedBy(White).size() == 1u);
    CHECK(g.undo());
    CHECK(g.position().board[sq("d5")] == makePiece(Black, Pawn));
    // Not playing ep immediately loses the right.
    CHECK(playUci(g, "h2h3"));
    CHECK(playUci(g, "h7h6"));
    CHECK(!g.findMove(sq("e5"), sq("d6")).has_value());
}

TEST_CASE("chess: promotion to every piece") {
    for (PieceType t : {Queen, Rook, Bishop, Knight}) {
        Game g;
        REQUIRE(g.loadFen("8/P6k/8/8/8/8/8/K7 w - - 0 1"));
        CHECK(g.needsPromotionChoice(sq("a7"), sq("a8")));
        auto m = g.findMove(sq("a7"), sq("a8"), t);
        REQUIRE(m.has_value());
        CHECK(g.play(*m));
        CHECK(g.position().board[sq("a8")] == makePiece(White, t));
    }
    Game g;
    REQUIRE(g.loadFen("1r5k/P7/8/8/8/8/8/K7 w - - 0 1"));
    auto cap = g.findMove(sq("a7"), sq("b8"), Knight);
    REQUIRE(cap.has_value());
    CHECK(g.play(*cap));
    CHECK(g.records().back().san == "axb8=N");
}

TEST_CASE("chess: stalemate") {
    Game g;
    REQUIRE(g.loadFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(g.result() == Result::Stalemate);
    CHECK(isDraw(g.result()));
}

TEST_CASE("chess: insufficient material") {
    Game g;
    CHECK(g.loadFen("8/8/4k3/8/8/3K4/8/8 w - - 0 1"));
    CHECK(g.result() == Result::DrawInsufficientMaterial);
    CHECK(g.loadFen("8/8/4k3/8/8/3KB3/8/8 w - - 0 1"));
    CHECK(g.result() == Result::DrawInsufficientMaterial);
    CHECK(g.loadFen("8/8/4k3/8/8/3KN3/8/8 w - - 0 1"));
    CHECK(g.result() == Result::DrawInsufficientMaterial);
    CHECK(g.loadFen("8/8/2b1k3/8/4B3/3K4/8/8 w - - 0 1"));  // same-coloured bishops (c6, e4 both light)
    CHECK(g.result() == Result::DrawInsufficientMaterial);
    CHECK(g.loadFen("8/8/3bk3/8/4B3/3K4/8/8 w - - 0 1"));   // opposite colours: mate possible
    CHECK(g.result() == Result::Ongoing);
    CHECK(g.loadFen("8/8/4k3/8/8/3KNN2/8/8 w - - 0 1"));
    CHECK(g.result() == Result::Ongoing);
    CHECK(g.loadFen("8/8/4k3/8/8/3K4/4P3/8 w - - 0 1"));
    CHECK(g.result() == Result::Ongoing);
    // Capturing the last pawn ends the game in a draw.
    CHECK(g.loadFen("8/8/8/8/8/3k4/4P3/4K3 b - - 0 1"));
    CHECK(playUci(g, "d3e2") == false);  // e2 defended by king -> illegal
    CHECK(g.loadFen("8/8/8/8/8/3k4/4P3/6K1 b - - 0 1"));
    CHECK(playUci(g, "d3e2"));
    CHECK(g.result() == Result::DrawInsufficientMaterial);
}

TEST_CASE("chess: threefold repetition") {
    Game g;
    const char* cycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int i = 0; i < 2; ++i)
        for (const char* m : cycle) CHECK(playUci(g, m));
    // Start position occurred 3 times now (initial + 2 returns).
    CHECK(g.result() == Result::DrawThreefold);
    CHECK(g.undo());
    CHECK(g.result() == Result::Ongoing);
}

TEST_CASE("chess: fifty-move rule") {
    Game g;
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/8/R3K3 w - - 99 80"));
    CHECK(g.result() == Result::Ongoing);
    CHECK(playUci(g, "a1a2"));
    CHECK(g.result() == Result::DrawFiftyMove);
    // A pawn move or capture resets the counter.
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/P7/4K3 w - - 99 80"));
    CHECK(playUci(g, "a2a3"));
    CHECK(g.result() == Result::Ongoing);
    CHECK(g.position().halfmove == 0);
}

TEST_CASE("chess: undo and restart") {
    Game g;
    CHECK(!g.canUndo());
    CHECK(playUci(g, "e2e4"));
    CHECK(playUci(g, "d7d5"));
    CHECK(playUci(g, "e4d5"));
    CHECK(g.capturedBy(White).size() == 1u);
    CHECK(g.undo());
    CHECK(g.undo());
    CHECK(g.undo());
    CHECK(!g.undo());
    CHECK(g.position().toFen() == Position::startPosition().toFen());
    CHECK(playUci(g, "d2d4"));
    g.reset();
    CHECK(g.records().empty());
    CHECK(g.position().side == White);
}

TEST_CASE("chess: SAN disambiguation") {
    Game g;
    REQUIRE(g.loadFen("4k3/8/8/8/8/8/4K3/R6R w - - 0 1"));
    auto m = g.findMove(sq("a1"), sq("d1"));
    REQUIRE(m.has_value());
    CHECK(g.play(*m));
    CHECK(g.records().back().san == "Rad1");
    REQUIRE(g.loadFen("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1"));
    m = g.findMove(sq("a1"), sq("a3"));
    REQUIRE(m.has_value());
    CHECK(g.play(*m));
    CHECK(g.records().back().san == "R1a3");
}

TEST_CASE("chess: FEN round trip and validation") {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    auto p = Position::fromFen(fen);
    REQUIRE(p.has_value());
    CHECK(p->toFen() == fen);
    CHECK(!Position::fromFen("8/8/8/8/8/8/8/8 w - - 0 1").has_value());          // no kings
    CHECK(!Position::fromFen("rnbqkbnr/ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w").has_value());  // bad rank
    CHECK(!Position::fromFen("garbage").has_value());
}
