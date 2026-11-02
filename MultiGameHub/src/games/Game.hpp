// Interface every game module implements, plus the static registry entry
// describing it. Adding a game = implement Game + add one GameInfo entry.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/AppContext.hpp"

namespace mgh {

class Game {
public:
    explicit Game(AppContext& ctx) : ctx_(ctx) {}
    virtual ~Game() = default;

    // Raw multi-touch events in pixels (also fed to the Ui for buttons).
    virtual void onPointer(const PointerEvent&) {}
    // Android back button / Escape. Typically toggles the pause menu.
    virtual void onBack() = 0;
    // App moved to background: pause gameplay.
    virtual void onAppPaused() {}
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    // Return false when the screen is static so the app can sleep between frames.
    virtual bool wantsContinuousFrames() const { return true; }

    bool exitRequested() const { return exit_; }

protected:
    void requestExit() { exit_ = true; }
    AppContext& ctx_;

private:
    bool exit_ = false;
};

struct GameInfo {
    std::string id;           // unique, [a-z0-9_]
    std::string name;
    std::string tagline;
    std::string description;
    std::string version;      // version of the built-in module
    std::string players;      // e.g. "2 players · same device"
    Color colorA, colorB;     // card gradient
    bool featured = false;
    // Card / icon artwork drawn procedurally.
    std::function<void(Renderer2D&, const Rect&, float time)> drawArt;
    std::function<std::unique_ptr<Game>(AppContext&)> create;
};

// All games compiled into this build, in display order.
const std::vector<GameInfo>& gameRegistry();
const GameInfo* findGame(const std::string& id);

}  // namespace mgh
