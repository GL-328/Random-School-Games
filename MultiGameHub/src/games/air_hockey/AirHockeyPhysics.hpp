// Air hockey simulation: puck, two kinematic paddles, walls with goal mouths
// and goal posts, friction, restitution, speed limits and scoring.
// Pure C++ (no SDL), deterministic, fixed sub-stepping.
#pragma once

#include <cmath>

namespace airhockey {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    float dot(Vec2 o) const { return x * o.x + y * o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
};

// World units: table width is 1.0, y grows downwards. Player 0 defends the
// bottom goal (y = height), player 1 defends the top goal (y = 0).
struct TableConfig {
    float width = 1.0f;
    float height = 1.75f;
    float goalWidth = 0.36f;
    float puckRadius = 0.042f;
    float paddleRadius = 0.072f;
    float maxPuckSpeed = 3.4f;     // units / second
    float maxPaddleSpeed = 9.0f;   // units / second
    float friction = 0.30f;        // exponential decay per second
    float wallRestitution = 0.88f;
    float paddleRestitution = 0.92f;
    int winScore = 7;
};

struct Body {
    Vec2 pos;
    Vec2 vel;
    float radius = 0;
};

struct StepEvents {
    int wallHits = 0;
    int paddleHits = 0;
    float strongestHit = 0;  // relative normal speed of the strongest paddle hit
    int goalScoredBy = -1;   // player index who scored this step, or -1
};

class Physics {
public:
    static constexpr float kStep = 1.0f / 240.0f;

    explicit Physics(const TableConfig& cfg = TableConfig()) { reset(cfg); }

    void reset(const TableConfig& cfg);
    void resetMatch();
    // Put the puck at rest in the middle of `player`'s half (the side that conceded).
    void servePuck(int player);
    void setPaddleTarget(int player, Vec2 target) { target_[player] = target; }
    void releasePaddle(int player) { target_[player] = paddle[player].pos; }

    // Advances the simulation by dt seconds using fixed sub-steps.
    StepEvents step(float dt);

    const TableConfig& config() const { return cfg_; }
    bool matchOver() const { return winner >= 0; }

    // Clamps a position to the area a paddle is allowed to occupy.
    Vec2 clampPaddle(int player, Vec2 p) const;
    bool inGoalMouth(float x) const;

    Body puck;
    Body paddle[2];
    int score[2] = {0, 0};
    int winner = -1;

private:
    void subStep(float dt, StepEvents& ev);
    void collidePaddle(int i, StepEvents& ev);
    void collidePost(Vec2 post, StepEvents& ev);
    void limitPuckSpeed();

    TableConfig cfg_;
    Vec2 target_[2];
    float accumulator_ = 0;
};

}  // namespace airhockey
