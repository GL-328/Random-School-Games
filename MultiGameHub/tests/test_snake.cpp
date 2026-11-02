#include "TestFramework.hpp"
#include "games/snake/SnakeLogic.hpp"

using namespace snake;

TEST_CASE("snake: moves one cell per tick in its direction") {
    Logic s(10, 10, 7);
    s.setFood({0, 0});
    Cell head = s.body().front();
    CHECK(s.tick() == TickResult::Moved);
    CHECK((s.body().front() == Cell{head.x + 1, head.y}));
    CHECK(s.body().size() == 3u);
    s.turn(Dir::Down);
    s.tick();
    CHECK((s.body().front() == Cell{head.x + 1, head.y + 1}));
}

TEST_CASE("snake: cannot reverse into itself, buffered turns") {
    Logic s(10, 10, 7);
    s.setFood({0, 0});
    s.turn(Dir::Left);  // reverse: ignored
    s.tick();
    CHECK(s.direction() == Dir::Right);
    s.turn(Dir::Up);
    s.turn(Dir::Left);  // two quick turns are both applied over two ticks
    s.tick();
    CHECK(s.direction() == Dir::Up);
    s.tick();
    CHECK(s.direction() == Dir::Left);
}

TEST_CASE("snake: eating food grows and scores, food respawns on free cell") {
    Logic s(10, 10, 7);
    Cell head = s.body().front();
    s.setFood({head.x + 1, head.y});
    CHECK(s.tick() == TickResult::Ate);
    CHECK(s.score() == 1);
    CHECK(s.body().size() == 4u);
    Cell f = s.food();
    for (const Cell& c : s.body()) CHECK(c != f);
    CHECK(f.x >= 0 && f.x < 10 && f.y >= 0 && f.y < 10);
}

TEST_CASE("snake: wall collision ends the game") {
    Logic s(8, 8, 3);
    s.setFood({0, 0});
    TickResult r = TickResult::Moved;
    for (int i = 0; i < 10 && r != TickResult::Died; ++i) r = s.tick();
    CHECK(r == TickResult::Died);
    CHECK(s.dead());
}

TEST_CASE("snake: self collision ends the game") {
    Logic s(10, 10, 3);
    s.setFood({0, 0});
    // A long snake curled so that turning hits its own body.
    s.setBody({{5, 5}, {4, 5}, {3, 5}, {3, 6}, {4, 6}, {5, 6}, {6, 6}}, Dir::Right);
    s.turn(Dir::Down);  // (5,6) is body
    CHECK(s.tick() == TickResult::Died);
}

TEST_CASE("snake: moving into the tail cell that is vacating is allowed") {
    Logic s(10, 10, 3);
    s.setFood({0, 0});
    s.setBody({{5, 5}, {5, 6}, {4, 6}, {4, 5}}, Dir::Left);  // head moves left into tail (4,5)
    CHECK(s.tick() == TickResult::Moved);
    CHECK(!s.dead());
}

TEST_CASE("snake: difficulty increases with score") {
    Logic s(20, 20, 9);
    float start = s.interval();
    for (int i = 0; i < 5; ++i) {
        Cell h = s.body().front();
        s.setFood({h.x + 1, h.y});
        CHECK(s.tick() == TickResult::Ate);
    }
    CHECK(s.interval() < start);
    CHECK(s.interval() >= Logic::kMinInterval);
}

TEST_CASE("snake: time based update and restart") {
    Logic s(10, 10, 5);
    s.setFood({0, 0});
    bool ticked = false;
    s.update(s.interval() * 0.5f, &ticked);
    CHECK(!ticked);
    s.update(s.interval() * 0.6f, &ticked);
    CHECK(ticked);
    s.reset(10, 10, 5);
    CHECK(s.score() == 0);
    CHECK(!s.dead());
    CHECK(s.body().size() == 3u);
}
