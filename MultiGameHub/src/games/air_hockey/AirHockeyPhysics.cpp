#include "AirHockeyPhysics.hpp"

#include <algorithm>

namespace airhockey {

void Physics::reset(const TableConfig& cfg) {
    cfg_ = cfg;
    puck.radius = cfg.puckRadius;
    paddle[0].radius = paddle[1].radius = cfg.paddleRadius;
    resetMatch();
}

void Physics::resetMatch() {
    score[0] = score[1] = 0;
    winner = -1;
    accumulator_ = 0;
    paddle[0].pos = {cfg_.width * 0.5f, cfg_.height * 0.85f};
    paddle[1].pos = {cfg_.width * 0.5f, cfg_.height * 0.15f};
    paddle[0].vel = paddle[1].vel = {0, 0};
    target_[0] = paddle[0].pos;
    target_[1] = paddle[1].pos;
    puck.pos = {cfg_.width * 0.5f, cfg_.height * 0.5f};
    puck.vel = {0, 0};
}

void Physics::servePuck(int player) {
    puck.vel = {0, 0};
    puck.pos = {cfg_.width * 0.5f, player == 0 ? cfg_.height * 0.68f : cfg_.height * 0.32f};
    // Move a paddle out of the way if it overlaps the freshly served puck.
    for (int i = 0; i < 2; ++i) {
        Vec2 d = paddle[i].pos - puck.pos;
        float minD = paddle[i].radius + puck.radius + 0.01f;
        if (d.length() < minD) {
            float dir = (i == 0) ? 1.0f : -1.0f;
            paddle[i].pos = clampPaddle(i, {paddle[i].pos.x, puck.pos.y + dir * minD});
            target_[i] = paddle[i].pos;
        }
    }
    accumulator_ = 0;
}

bool Physics::inGoalMouth(float x) const {
    return std::fabs(x - cfg_.width * 0.5f) < cfg_.goalWidth * 0.5f;
}

Vec2 Physics::clampPaddle(int player, Vec2 p) const {
    const float r = cfg_.paddleRadius;
    const float half = cfg_.height * 0.5f;
    p.x = std::clamp(p.x, r, cfg_.width - r);
    if (player == 0) p.y = std::clamp(p.y, half + r, cfg_.height - r);
    else p.y = std::clamp(p.y, r, half - r);
    return p;
}

StepEvents Physics::step(float dt) {
    StepEvents ev;
    if (winner >= 0) return ev;
    accumulator_ += std::min(dt, 0.1f);  // avoid spiral of death after a long stall
    while (accumulator_ >= kStep) {
        accumulator_ -= kStep;
        subStep(kStep, ev);
        if (ev.goalScoredBy >= 0) {
            accumulator_ = 0;
            break;
        }
    }
    return ev;
}

void Physics::limitPuckSpeed() {
    float s = puck.vel.length();
    if (s > cfg_.maxPuckSpeed) puck.vel = puck.vel * (cfg_.maxPuckSpeed / s);
}

void Physics::subStep(float dt, StepEvents& ev) {
    // 1. Kinematic paddles chase their touch targets with a speed limit.
    for (int i = 0; i < 2; ++i) {
        Vec2 target = clampPaddle(i, target_[i]);
        Vec2 delta = target - paddle[i].pos;
        float maxMove = cfg_.maxPaddleSpeed * dt;
        float len = delta.length();
        if (len > maxMove) delta = delta * (maxMove / len);
        Vec2 newPos = clampPaddle(i, paddle[i].pos + delta);
        paddle[i].vel = (newPos - paddle[i].pos) * (1.0f / dt);
        paddle[i].pos = newPos;
    }

    // 2. Integrate the puck with friction.
    puck.vel = puck.vel * std::exp(-cfg_.friction * dt);
    puck.pos += puck.vel * dt;

    // 3. Paddle collisions.
    collidePaddle(0, ev);
    collidePaddle(1, ev);

    // 4. Side walls.
    const float r = puck.radius;
    if (puck.pos.x < r) {
        puck.pos.x = r;
        if (puck.vel.x < 0) { puck.vel.x = -puck.vel.x * cfg_.wallRestitution; ++ev.wallHits; }
    } else if (puck.pos.x > cfg_.width - r) {
        puck.pos.x = cfg_.width - r;
        if (puck.vel.x > 0) { puck.vel.x = -puck.vel.x * cfg_.wallRestitution; ++ev.wallHits; }
    }

    // 5. End walls, goal posts and goals.
    const float postL = cfg_.width * 0.5f - cfg_.goalWidth * 0.5f;
    const float postR = cfg_.width * 0.5f + cfg_.goalWidth * 0.5f;
    for (float y : {0.0f, cfg_.height}) {
        collidePost({postL, y}, ev);
        collidePost({postR, y}, ev);
    }
    const bool mouth = inGoalMouth(puck.pos.x);
    if (puck.pos.y < r && !mouth) {
        puck.pos.y = r;
        if (puck.vel.y < 0) { puck.vel.y = -puck.vel.y * cfg_.wallRestitution; ++ev.wallHits; }
    } else if (puck.pos.y > cfg_.height - r && !mouth) {
        puck.pos.y = cfg_.height - r;
        if (puck.vel.y > 0) { puck.vel.y = -puck.vel.y * cfg_.wallRestitution; ++ev.wallHits; }
    }

    // A goal counts once the whole puck has crossed the goal line.
    int scorer = -1;
    if (puck.pos.y < -r) scorer = 0;               // bottom player scored in the top goal
    else if (puck.pos.y > cfg_.height + r) scorer = 1;
    if (scorer >= 0) {
        ++score[scorer];
        ev.goalScoredBy = scorer;
        if (score[scorer] >= cfg_.winScore) winner = scorer;
        puck.vel = {0, 0};
        return;
    }

    limitPuckSpeed();
}

void Physics::collidePaddle(int i, StepEvents& ev) {
    Body& pd = paddle[i];
    Vec2 d = puck.pos - pd.pos;
    float dist = d.length();
    float minDist = pd.radius + puck.radius;
    if (dist >= minDist) return;
    Vec2 n = dist > 1e-6f ? d * (1.0f / dist) : Vec2{0, i == 0 ? -1.0f : 1.0f};
    // Push the puck out (paddle is kinematic / infinite mass).
    puck.pos = pd.pos + n * minDist;
    Vec2 rel = puck.vel - pd.vel;
    float vn = rel.dot(n);
    if (vn < 0) {
        puck.vel -= n * ((1.0f + cfg_.paddleRestitution) * vn);
        ++ev.paddleHits;
        ev.strongestHit = std::max(ev.strongestHit, -vn);
    }
    // If the puck is pinned against a wall, move the paddle back instead so
    // the two never overlap (prevents the puck being forced through walls).
    const float r = puck.radius;
    Vec2 clamped = {std::clamp(puck.pos.x, r, cfg_.width - r), puck.pos.y};
    if (!inGoalMouth(clamped.x)) clamped.y = std::clamp(clamped.y, r, cfg_.height - r);
    if (clamped.x != puck.pos.x || clamped.y != puck.pos.y) {
        puck.pos = clamped;
        pd.pos = clampPaddle(i, puck.pos - n * minDist);
    }
    limitPuckSpeed();
}

void Physics::collidePost(Vec2 post, StepEvents& ev) {
    Vec2 d = puck.pos - post;
    float dist = d.length();
    if (dist >= puck.radius || dist < 1e-6f) return;
    Vec2 n = d * (1.0f / dist);
    puck.pos = post + n * puck.radius;
    float vn = puck.vel.dot(n);
    if (vn < 0) {
        puck.vel -= n * ((1.0f + cfg_.wallRestitution) * vn);
        ++ev.wallHits;
    }
}

}  // namespace airhockey
