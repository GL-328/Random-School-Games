// Entry point. On Android SDL calls SDL_main() from its Java activity.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <string>

#include "core/App.hpp"

int main(int argc, char* argv[]) {
    mgh::AppOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) opts.scriptPath = argv[++i];
        else if (a == "--shots" && i + 1 < argc) opts.screenshotDir = argv[++i];
    }
    mgh::App app;
    return app.run(opts);
}
