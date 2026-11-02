// Snake game rules on a fixed grid: movement with buffered turns, food,
// growth, wall and self collision, and speed that increases with score.
// Pure C++ (no SDL) for unit testing.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace snake {

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

enum class Dir { Up, Down, Left, Right };
inline bool opposite(Dir a, Dir b) {
    return (a == Dir::Up && b == Dir::Down) || (a == Dir::Down && b == Dir::Up) ||
           (a == Dir::Left && b == Dir::Right) || (a == Dir::Right && b == Dir::Left);
}

enum class TickResult { Moved, Ate, Died, Won };

class Logic {
public:
    static constexpr float kStartInterval = 0.17f;   // seconds per move at score 0
    static constexpr float kMinInterval = 0.065f;
    static constexpr float kIntervalStep = 0.0045f;  // faster per food eaten

    Logic(int cols = 16, int rows = 24, uint32_t seed = 12345) { reset(cols, rows, seed); }

    void reset(int cols, int rows, uint32_t seed);
    // Queue a direction change; reversing into yourself or duplicates are ignored.
    void turn(Dir d);
    // Advances the snake exactly one cell.
    TickResult tick();
    // Advances by real time; returns the result of the last tick (or Moved if none).
    TickResult update(float dt, bool* ticked = nullptr);

    float interval() const;
    float tickProgress() const { return timer_ / interval(); }  // 0..1 for interpolation

    const std::deque<Cell>& body() const { return body_; }      // front = head
    const std::deque<Cell>& previousBody() const { return prev_; }
    Cell food() const { return food_; }
    Dir direction() const { return dir_; }
    int score() const { return score_; }
    bool dead() const { return dead_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

    // Test hooks.
    void setFood(Cell c) { food_ = c; }
    void setBody(const std::deque<Cell>& b, Dir d) { body_ = b; prev_ = b; dir_ = d; queue_.clear(); }

private:
    uint32_t rand();
    void placeFood();
    bool occupied(Cell c) const;

    int cols_ = 16, rows_ = 24;
    std::deque<Cell> body_, prev_;
    std::deque<Dir> queue_;
    Dir dir_ = Dir::Right;
    Cell food_;
    int score_ = 0;
    bool dead_ = false;
    float timer_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace snake
