#include "SnakeLogic.hpp"

#include <algorithm>

namespace snake {

void Logic::reset(int cols, int rows, uint32_t seed) {
    cols_ = std::max(cols, 6);
    rows_ = std::max(rows, 6);
    rng_ = seed ? seed : 1;
    body_.clear();
    queue_.clear();
    const int cy = rows_ / 2;
    const int cx = cols_ / 2;
    for (int i = 0; i < 3; ++i) body_.push_back({cx - i, cy});
    prev_ = body_;
    dir_ = Dir::Right;
    score_ = 0;
    dead_ = false;
    timer_ = 0;
    placeFood();
}

uint32_t Logic::rand() {
    // xorshift32
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

bool Logic::occupied(Cell c) const {
    return std::find(body_.begin(), body_.end(), c) != body_.end();
}

void Logic::placeFood() {
    const int total = cols_ * rows_;
    const int freeCells = total - static_cast<int>(body_.size());
    if (freeCells <= 0) { food_ = {-1, -1}; return; }
    // Pick the k-th free cell uniformly: guaranteed to terminate.
    int k = static_cast<int>(rand() % static_cast<uint32_t>(freeCells));
    for (int i = 0; i < total; ++i) {
        Cell c{i % cols_, i / cols_};
        if (occupied(c)) continue;
        if (k-- == 0) { food_ = c; return; }
    }
}

void Logic::turn(Dir d) {
    Dir last = queue_.empty() ? dir_ : queue_.back();
    if (d == last || opposite(d, last)) return;
    if (queue_.size() < 2) queue_.push_back(d);
}

float Logic::interval() const {
    return std::max(kMinInterval, kStartInterval - kIntervalStep * static_cast<float>(score_));
}

TickResult Logic::tick() {
    if (dead_) return TickResult::Died;
    if (!queue_.empty()) {
        dir_ = queue_.front();
        queue_.pop_front();
    }
    prev_ = body_;
    Cell head = body_.front();
    switch (dir_) {
        case Dir::Up: --head.y; break;
        case Dir::Down: ++head.y; break;
        case Dir::Left: --head.x; break;
        case Dir::Right: ++head.x; break;
    }
    if (head.x < 0 || head.y < 0 || head.x >= cols_ || head.y >= rows_) {
        dead_ = true;
        return TickResult::Died;
    }
    const bool eating = head == food_;
    // Moving into the cell the tail is leaving this tick is allowed.
    auto end = eating ? body_.end() : body_.end() - 1;
    if (std::find(body_.begin(), end, head) != end) {
        dead_ = true;
        return TickResult::Died;
    }
    body_.push_front(head);
    if (eating) {
        ++score_;
        placeFood();
        if (food_.x < 0) return TickResult::Won;  // board full
        return TickResult::Ate;
    }
    body_.pop_back();
    return TickResult::Moved;
}

TickResult Logic::update(float dt, bool* ticked) {
    if (ticked) *ticked = false;
    if (dead_) return TickResult::Died;
    timer_ += dt;
    TickResult last = TickResult::Moved;
    int guard = 0;
    while (timer_ >= interval() && guard++ < 4) {
        timer_ -= interval();
        last = tick();
        if (ticked) *ticked = true;
        if (last == TickResult::Died || last == TickResult::Won || last == TickResult::Ate) break;
    }
    if (guard >= 4) timer_ = 0;
    return last;
}

}  // namespace snake
