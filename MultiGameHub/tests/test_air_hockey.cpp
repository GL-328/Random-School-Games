#include "TestFramework.hpp"
#include "games/air_hockey/AirHockeyPhysics.hpp"

using namespace airhockey;

static void run(Physics& p, float seconds, StepEvents* total = nullptr) {
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) {
        StepEvents ev = p.step(1.0f / 60.0f);
        if (total) {
            total->wallHits += ev.wallHits;
            total->paddleHits += ev.paddleHits;
            if (ev.goalScoredBy >= 0) total->goalScoredBy = ev.goalScoredBy;
        }
        if (ev.goalScoredBy >= 0) return;
    }
}

TEST_CASE("air hockey: side wall collision reflects and stays inside") {
    Physics p;
    p.paddle[0].pos = {0.5f, 1.6f};
    p.paddle[1].pos = {0.5f, 0.15f};
    p.setPaddleTarget(0, p.paddle[0].pos);
    p.setPaddleTarget(1, p.paddle[1].pos);
    p.puck.pos = {0.5f, 0.875f};
    p.puck.vel = {-2.0f, 0.0f};
    StepEvents total;
    run(p, 0.5f, &total);
    CHECK(total.wallHits >= 1);
    CHECK(p.puck.vel.x > 0);  // bounced back
    CHECK(p.puck.pos.x >= p.puck.radius - 1e-4f);
    CHECK(p.puck.pos.x <= p.config().width - p.puck.radius + 1e-4f);
}

TEST_CASE("air hockey: end wall outside goal mouth bounces") {
    Physics p;
    p.paddle[1].pos = {0.9f, 0.3f};
    p.setPaddleTarget(1, p.paddle[1].pos);
    p.puck.pos = {0.12f, 0.4f};
    p.puck.vel = {0.0f, -2.0f};
    StepEvents total;
    run(p, 0.6f, &total);
    CHECK(total.goalScoredBy == -1);
    CHECK(total.wallHits >= 1);
    CHECK(p.puck.vel.y > 0);
}

TEST_CASE("air hockey: goal detection and scoring") {
    Physics p;
    p.paddle[1].pos = {0.1f, 0.2f};  // keep the defender out of the way
    p.setPaddleTarget(1, p.paddle[1].pos);
    p.puck.pos = {0.5f, 0.5f};
    p.puck.vel = {0.0f, -3.0f};
    StepEvents total;
    run(p, 1.0f, &total);
    CHECK(total.goalScoredBy == 0);  // bottom player scores in top goal
    CHECK(p.score[0] == 1);
    CHECK(p.score[1] == 0);

    p.servePuck(0);
    p.paddle[0].pos = {0.9f, 1.6f};
    p.setPaddleTarget(0, p.paddle[0].pos);
    p.puck.pos = {0.5f, 1.2f};
    p.puck.vel = {0.0f, 3.0f};
    total = StepEvents();
    run(p, 1.0f, &total);
    CHECK(total.goalScoredBy == 1);
    CHECK(p.score[1] == 1);
}

TEST_CASE("air hockey: moving paddle hits puck and transfers velocity") {
    Physics p;
    p.puck.pos = {0.5f, 1.2f};
    p.puck.vel = {0, 0};
    p.paddle[0].pos = {0.5f, 1.5f};
    p.setPaddleTarget(0, {0.5f, 0.95f});  // swing upwards through the puck
    StepEvents total;
    run(p, 0.2f, &total);
    CHECK(total.paddleHits >= 1);
    CHECK(p.puck.vel.y < -0.5f);  // puck now travels towards the top goal
    // Never overlapping after resolution.
    float d = (p.puck.pos - p.paddle[0].pos).length();
    CHECK(d >= p.puck.radius + p.paddle[0].radius - 1e-3f);
}

TEST_CASE("air hockey: speed limit and friction") {
    Physics p;
    p.puck.pos = {0.5f, 0.875f};
    p.puck.vel = {50.0f, 30.0f};
    p.step(1.0f / 60.0f);
    CHECK(p.puck.vel.length() <= p.config().maxPuckSpeed + 1e-3f);
    Physics q;
    q.paddle[0].pos = {0.1f, 1.6f};
    q.paddle[1].pos = {0.1f, 0.15f};
    q.setPaddleTarget(0, q.paddle[0].pos);
    q.setPaddleTarget(1, q.paddle[1].pos);
    q.puck.pos = {0.5f, 0.875f};
    q.puck.vel = {0.3f, 0.0f};
    float before = q.puck.vel.length();
    run(q, 0.5f);
    CHECK(q.puck.vel.length() < before);
}

TEST_CASE("air hockey: paddles are confined to their own half") {
    Physics p;
    p.setPaddleTarget(0, {0.5f, 0.1f});   // bottom player tries to cross the centre line
    p.setPaddleTarget(1, {-5.0f, 9.0f});  // top player drags off the table
    run(p, 1.0f);
    float half = p.config().height * 0.5f;
    CHECK(p.paddle[0].pos.y >= half + p.paddle[0].radius - 1e-4f);
    CHECK(p.paddle[1].pos.y <= half - p.paddle[1].radius + 1e-4f);
    CHECK(p.paddle[1].pos.x >= p.paddle[1].radius - 1e-4f);
}

TEST_CASE("air hockey: simultaneous control of both paddles") {
    Physics p;
    p.setPaddleTarget(0, {0.2f, 1.4f});
    p.setPaddleTarget(1, {0.8f, 0.3f});
    run(p, 0.5f);
    CHECK((p.paddle[0].pos - Vec2{0.2f, 1.4f}).length() < 0.01f);
    CHECK((p.paddle[1].pos - Vec2{0.8f, 0.3f}).length() < 0.01f);
}

TEST_CASE("air hockey: puck pinned against wall cannot escape the table") {
    Physics p;
    p.puck.pos = {p.puck.radius, 1.2f};
    p.puck.vel = {0, 0};
    p.paddle[0].pos = {0.3f, 1.2f};
    p.setPaddleTarget(0, {0.0f, 1.2f});  // push the puck into the left wall
    run(p, 0.5f);
    CHECK(p.puck.pos.x >= p.puck.radius - 1e-4f);
    float d = (p.puck.pos - p.paddle[0].pos).length();
    CHECK(d >= p.puck.radius + p.paddle[0].radius - 2e-3f);
}

TEST_CASE("air hockey: first to seven wins and restart resets") {
    Physics p;
    p.score[0] = 6;
    p.paddle[1].pos = {0.1f, 0.2f};
    p.setPaddleTarget(1, p.paddle[1].pos);
    p.puck.pos = {0.5f, 0.4f};
    p.puck.vel = {0.0f, -3.0f};
    run(p, 1.0f);
    CHECK(p.score[0] == 7);
    CHECK(p.winner == 0);
    CHECK(p.matchOver());
    StepEvents ev = p.step(0.1f);  // no further simulation after the match ends
    CHECK(ev.goalScoredBy == -1);
    p.resetMatch();
    CHECK(p.score[0] == 0);
    CHECK(p.winner == -1);
}

TEST_CASE("air hockey: goal posts deflect the puck") {
    Physics p;
    const float postX = 0.5f - p.config().goalWidth * 0.5f;
    p.paddle[1].pos = {0.9f, 0.3f};
    p.setPaddleTarget(1, p.paddle[1].pos);
    p.puck.pos = {postX, 0.3f};
    p.puck.vel = {0.0f, -2.5f};
    StepEvents total;
    run(p, 0.5f, &total);
    CHECK(total.wallHits >= 1);
}
