#pragma once

#include <SDL3/SDL.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "audio/Audio.hpp"
#include "core/AppContext.hpp"
#include "core/Settings.hpp"
#include "games/Game.hpp"
#include "platform/Platform.hpp"
#include "storage/StorageManager.hpp"
#include "ui/Font.hpp"
#include "ui/Hub.hpp"
#include "ui/Renderer2D.hpp"
#include "ui/Ui.hpp"
#include "updates/UpdateManager.hpp"

namespace mgh {

struct AppOptions {
    std::string scriptPath;    // automated UI test script (desktop testing)
    std::string screenshotDir;
};

class App {
public:
    int run(const AppOptions& opts);

private:
    bool init();
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void dispatchPointer(const PointerEvent& pe);
    void handleBack();
    void frame(float dt);
    void updateMetrics();
    void applyTheme(bool dark);
    void launch(const std::string& id);
    void closeGame();
    bool needsContinuousFrames() const;
    bool loadAsset(const std::string& name, std::vector<unsigned char>& out);

    // test script support
    bool loadScript(const std::string& path);
    void runScript();
    void screenshot(const std::string& name);

    AppOptions opts_;
    std::unique_ptr<IPlatform> platform_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<FontSystem> fonts_;
    std::unique_ptr<Renderer2D> r_;
    std::unique_ptr<Ui> ui_;
    Settings settings_;
    std::unique_ptr<SynthAudio> audio_;
    std::unique_ptr<StorageManager> storage_;
    std::unique_ptr<UpdateManager> updates_;
    std::unique_ptr<AppContext> ctx_;
    std::unique_ptr<Hub> hub_;
    std::unique_ptr<Game> game_;
    std::string gameId_;

    bool running_ = true;
    bool backgrounded_ = false;
    bool startupCheckDone_ = false;
    float curtain_ = 0;  // 0 = open, 1 = fully covered
    std::string pendingLaunch_;
    bool pendingClose_ = false;
    int pixelW_ = 0, pixelH_ = 0;
    uint64_t frameCounter_ = 0;

    struct ScriptCmd {
        std::string op;
        std::vector<std::string> args;
    };
    std::deque<ScriptCmd> script_;
    int scriptWait_ = 0;
    int scriptExit_ = 0;
    bool scripted_ = false;
    std::string pendingShot_;
};

}  // namespace mgh
