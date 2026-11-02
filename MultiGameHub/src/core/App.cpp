#include "App.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/FileUtil.hpp"

#ifndef MGH_UPDATE_REPO_STR
#define MGH_UPDATE_REPO_STR "GL-328/Random-School-Games"
#endif

namespace mgh {

namespace {
int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

Json AppContext::packTheme(const std::string& gameId) const {
    if (!storage.hasPack(gameId)) return Json();
    auto text = readTextFile(joinPath(storage.packDir(gameId), "theme.json"), 64 * 1024);
    if (!text) return Json();
    auto j = Json::parse(*text);
    return (j && j->isObject()) ? *j : Json();
}

Color themeColor(const Json& j, const char* key, Color def) {
    const std::string& s = j[key].asString();
    if (s.size() != 7 || s[0] != '#') return def;
    unsigned v = 0;
    for (size_t i = 1; i < 7; ++i) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return def;
    }
    return Color::hex(v);
}

// ====================================================================== init

static std::string assetRootPath() {
    // On Android, relative paths are read from the APK's assets/ folder by SDL.
#ifdef SDL_PLATFORM_ANDROID
    return "";
#else
    if (const char* env = SDL_getenv("MGH_ASSET_DIR")) return std::string(env) + "/";
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "assets/";
#endif
}

bool App::loadAsset(const std::string& name, std::vector<unsigned char>& out) {
    const std::string path = assetRootPath() + name;
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        SDL_Log("Failed to load asset %s: %s", path.c_str(), SDL_GetError());
        return false;
    }
    out.assign(static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
    SDL_free(data);
    return true;
}

bool App::init() {
    SDL_SetAppMetadata("Got Bored", MGH_VERSION_STR, "com.gl328.multigamehub");
    // Unified input: touches only (mouse is converted to touch on desktop).
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    platform_ = createPlatform();

#ifdef SDL_PLATFORM_ANDROID
    window_ = SDL_CreateWindow("Got Bored", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    int w = 412, h = 892;
    if (const char* env = SDL_getenv("MGH_WINDOW")) std::sscanf(env, "%dx%d", &w, &h);
    window_ = SDL_CreateWindow("Got Bored", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_Log("Renderer: %s", SDL_GetRendererName(renderer_));

    fonts_ = std::make_unique<FontSystem>(renderer_);
    std::vector<unsigned char> data;
    if (!loadAsset("fonts/ui.otf", data) || !fonts_->load(FontId::Regular, data)) return false;
    if (!loadAsset("fonts/bold.otf", data) || !fonts_->load(FontId::Bold, data)) return false;
    if (!loadAsset("fonts/chess.ttf", data) || !fonts_->load(FontId::Chess, data)) return false;
    r_ = std::make_unique<Renderer2D>(renderer_, *fonts_);
    ui_ = std::make_unique<Ui>(*r_);

    IStorage& st = platform_->storage();
    ensureDir(st.dataDir());
    ensureDir(st.cacheDir());
    settings_.setPath(joinPath(st.dataDir(), "settings.cfg"));

    audio_ = std::make_unique<SynthAudio>();
    audio_->setSoundEnabled(settings_.getBool(keys::kSound, true));
    audio_->setMusicEnabled(settings_.getBool(keys::kMusic, true));

    storage_ = std::make_unique<StorageManager>(st);
    storage_->cleanupStale();
    const std::string manifestUrl =
        std::string("https://github.com/") + MGH_UPDATE_REPO_STR + "/releases/latest/download/manifest.json";
    updates_ = std::make_unique<UpdateManager>(platform_->network(), platform_->installer(), *storage_,
                                               platform_->appInfo().versionCode, manifestUrl);
    ctx_ = std::make_unique<AppContext>(*platform_, settings_, *audio_, *storage_, *updates_, *ui_, *r_);
    ctx_->assetRoot = assetRootPath();
    updates_->onCheckFinished = [this](bool ok) {
        if (ok) settings_.setInt(keys::kLastUpdateCheck, nowSeconds());
        if (ok && updates_->appUpdateAvailable())
            ctx_->toast("Update available: version " + updates_->manifest()->app.version.str());
    };
    updates_->onPackInstalled = [this](const std::string&) { ctx_->toast(updates_->statusText()); };

    applyTheme(settings_.getString(keys::kTheme, "dark") != "light");
    hub_ = std::make_unique<Hub>(*ctx_, [this](const std::string& id) { launch(id); },
                                 [this](bool dark) { applyTheme(dark); });
    updateMetrics();
    return true;
}

void App::applyTheme(bool dark) { ui_->setTheme(Theme::make(dark)); }

void App::updateMetrics() {
    SDL_GetCurrentRenderOutputSize(renderer_, &pixelW_, &pixelH_);
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    const float pxPerUnit = ww > 0 ? static_cast<float>(pixelW_) / ww : 1.0f;
    float dp = SDL_GetWindowDisplayScale(window_);
    if (const char* env = SDL_getenv("MGH_DP")) dp = static_cast<float>(SDL_atof(env));
    if (!(dp > 0.5f)) dp = 1.0f;
    // Guarantee at least 340dp of layout width so phones never get cramped UIs.
    dp = std::min(dp, pixelW_ / 340.0f);
    dp = std::max(dp, 0.75f);

    SDL_Rect safe{0, 0, ww, wh};
    SDL_GetWindowSafeArea(window_, &safe);
    ctx_->dp = dp;
    ui_->setScale(dp);
    ctx_->screen = {0, 0, static_cast<float>(pixelW_), static_cast<float>(pixelH_)};
    ctx_->safe = {safe.x * pxPerUnit, safe.y * pxPerUnit, safe.w * pxPerUnit, safe.h * pxPerUnit};
    if (ctx_->safe.w <= 0 || ctx_->safe.h <= 0) ctx_->safe = ctx_->screen;
    // A little breathing room under status bars / notches.
    if (ctx_->safe.y < dp * 8) {
        float add = dp * 8 - ctx_->safe.y;
        ctx_->safe.y += add;
        ctx_->safe.h -= add;
    }
}

void App::shutdown() {
    game_.reset();
    hub_.reset();
    ctx_.reset();
    updates_.reset();
    storage_.reset();
    audio_.reset();
    ui_.reset();
    r_.reset();
    fonts_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    platform_.reset();
    SDL_Quit();
}

// ====================================================================== games

void App::launch(const std::string& id) {
    if (!findGame(id) || !pendingLaunch_.empty() || game_) return;
    ctx_->sfx(Sfx::Click);
    pendingLaunch_ = id;
}

void App::closeGame() { pendingClose_ = true; }

bool App::needsContinuousFrames() const {
    if (scripted_) return true;
    if (!pendingLaunch_.empty() || pendingClose_ || (curtain_ > 0 && curtain_ < 1)) return true;
    if (curtain_ > 0) return true;
    if (ctx_->toastTime > 0) return true;
    if (updates_->busy()) return true;
    if (ui_->wantsAnimation()) return true;
    if (game_) return game_->wantsContinuousFrames();
    return hub_->animating();
}

// ====================================================================== events

void App::dispatchPointer(const PointerEvent& pe) {
    ui_->feed(pe);
    if (game_ && curtain_ <= 0.0f) game_->onPointer(pe);
}

void App::handleBack() {
    if (game_) {
        game_->onBack();
        return;
    }
    if (!hub_->onBack()) {
        // At the root: send the app to the background like a normal Android app.
        platform_->minimize();
    }
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_TERMINATING:
            running_ = false;
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            PointerEvent pe;
            pe.type = e.type == SDL_EVENT_FINGER_DOWN     ? PointerEvent::Type::Down
                      : e.type == SDL_EVENT_FINGER_MOTION ? PointerEvent::Type::Move
                      : e.type == SDL_EVENT_FINGER_UP     ? PointerEvent::Type::Up
                                                          : PointerEvent::Type::Cancel;
            pe.id = static_cast<uint64_t>(e.tfinger.fingerID) + 1;  // 0 means "none" internally
            pe.x = e.tfinger.x * pixelW_;
            pe.y = e.tfinger.y * pixelH_;
            dispatchPointer(pe);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_AC_BACK || e.key.key == SDLK_ESCAPE) handleBack();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            updateMetrics();
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            backgrounded_ = true;
            if (game_) game_->onAppPaused();
            audio_->pauseAll(true);
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            backgrounded_ = false;
            audio_->pauseAll(false);
            updateMetrics();
            break;
        case SDL_EVENT_LOW_MEMORY:
            fonts_->clear();  // atlases are re-created on demand
            break;
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_TARGETS_RESET:
            fonts_->clear();
            break;
        default:
            break;
    }
}

// ====================================================================== frame

void App::frame(float dt) {
    ++frameCounter_;
    if (scripted_ && (!script_.empty() || scriptWait_ > 0)) runScript();  // injects input before the UI runs
    fonts_->beginFrame();
    ui_->beginFrame(dt);
    updates_->update();
    if (ctx_->toastTime > 0) ctx_->toastTime -= dt;
    audio_->setMusicPlaying(!game_ && pendingLaunch_.empty());

    // Curtain transition between hub and games.
    const float speed = dt / 0.18f;
    if (!pendingLaunch_.empty() || pendingClose_) {
        curtain_ = std::min(1.0f, curtain_ + speed);
        if (curtain_ >= 1.0f) {
            if (pendingClose_) {
                game_.reset();  // frees every resource the game owned
                gameId_.clear();
                fonts_->clear();  // drop glyph atlases baked for the game
                pendingClose_ = false;
            }
            if (!pendingLaunch_.empty()) {
                if (const GameInfo* g = findGame(pendingLaunch_)) {
                    ensureDir(storage_->savesDir(g->id));
                    game_ = g->create(*ctx_);
                    gameId_ = g->id;
                }
                pendingLaunch_.clear();
            }
        }
    } else if (curtain_ > 0) {
        curtain_ = std::max(0.0f, curtain_ - speed);
    }

    const Theme& th = ui_->theme();
    r_->clear(th.bg);
    if (game_) {
        game_->update(dt);
        game_->render();
        if (game_->exitRequested() && !pendingClose_) closeGame();
    } else {
        r_->fillRect(ctx_->screen, th.bg);
        hub_->frame();
    }

    // Toast.
    if (ctx_->toastTime > 0 && !ctx_->toastText.empty()) {
        const float a = std::min(1.0f, ctx_->toastTime * 3.0f);
        const float fpx = ctx_->px(13.5f);
        const float w = std::min(ctx_->safe.w - ctx_->px(32), r_->measure(ctx_->toastText, fpx) + ctx_->px(40));
        Rect tr{ctx_->safe.cx() - w * 0.5f, ctx_->safe.bottom() - ctx_->px(game_ ? 70 : 128), w, ctx_->px(44)};
        r_->setGlobalAlpha(a);
        r_->fillRoundRect(tr, ctx_->px(22), th.dark ? Color::hex(0xEEF1F7) : Color::hex(0x1E222B));
        r_->textIn(r_->elide(ctx_->toastText, w - ctx_->px(24), fpx), tr, fpx,
                   th.dark ? Color::hex(0x161922) : Color::hex(0xFFFFFF), FontId::Bold);
        r_->setGlobalAlpha(1.0f);
    }
    if (curtain_ > 0) r_->fillRect(ctx_->screen, th.bg.withA(static_cast<uint8_t>(255 * easeInOut(curtain_))));

    if (!pendingShot_.empty()) {
        screenshot(pendingShot_);
        pendingShot_.clear();
    }
    ui_->endFrame();
    SDL_RenderPresent(renderer_);

    // First automatic update check, at most once per day, after the UI is up.
    if (!startupCheckDone_ && frameCounter_ > 2) {
        startupCheckDone_ = true;
        if (!scripted_ && settings_.getBool(keys::kAutoUpdate, true) &&
            nowSeconds() - settings_.getInt(keys::kLastUpdateCheck, 0) >= 24 * 3600) {
            settings_.setInt(keys::kLastUpdateCheck, nowSeconds());
            updates_->checkForUpdates();
        }
    }
}

int App::run(const AppOptions& opts) {
    opts_ = opts;
    if (!init()) {
        shutdown();
        return 1;
    }
    if (!opts_.scriptPath.empty()) {
        if (!loadScript(opts_.scriptPath)) {
            shutdown();
            return 2;
        }
        scripted_ = true;
    }
    uint64_t last = SDL_GetTicksNS();
    while (running_) {
        SDL_Event e;
        if (!scripted_ && (backgrounded_ || !needsContinuousFrames())) {
            // Idle: sleep until input arrives (no busy rendering, saves battery).
            if (SDL_WaitEventTimeout(&e, backgrounded_ ? 1000 : 500)) handleEvent(e);
            if (backgrounded_) {
                while (SDL_PollEvent(&e)) handleEvent(e);
                last = SDL_GetTicksNS();
                continue;
            }
        }
        while (SDL_PollEvent(&e)) handleEvent(e);
        const uint64_t now = SDL_GetTicksNS();
        float dt = scripted_ ? 1.0f / 60.0f : static_cast<float>((now - last) / 1e9);
        last = now;
        dt = std::clamp(dt, 0.0f, 0.1f);
        frame(dt);
        if (scripted_ && script_.empty() && scriptWait_ <= 0) running_ = false;
    }
    shutdown();
    return scriptExit_;
}

// ====================================================================== test script

bool App::loadScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("Cannot open script %s", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        ScriptCmd c;
        ls >> c.op;
        std::string a;
        while (ls >> a) c.args.push_back(a);
        if (!c.op.empty()) script_.push_back(c);
    }
    return true;
}

void App::screenshot(const std::string& name) {
    SDL_Surface* s = SDL_RenderReadPixels(renderer_, nullptr);
    if (!s) return;
    std::string dir = opts_.screenshotDir.empty() ? "." : opts_.screenshotDir;
    std::string path = dir + "/" + name + ".bmp";
    SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    SDL_Log("screenshot %s", path.c_str());
}

void App::runScript() {
    if (scriptWait_ > 0) {
        --scriptWait_;
        return;
    }
    auto f = [&](const ScriptCmd& c, size_t i) { return i < c.args.size() ? static_cast<float>(SDL_atof(c.args[i].c_str())) : 0.0f; };
    // Script coordinates are in dp relative to the safe area's top-left corner.
    auto X = [&](float v) { return ctx_->safe.x + v * ctx_->dp; };
    auto Y = [&](float v) { return ctx_->safe.y + v * ctx_->dp; };
    while (!script_.empty() && scriptWait_ == 0) {
        ScriptCmd c = script_.front();
        script_.pop_front();
        const std::string& op = c.op;
        if (op == "wait") {
            scriptWait_ = std::max(1, static_cast<int>(f(c, 0)));
        } else if (op == "tap") {
            dispatchPointer({PointerEvent::Type::Down, 9001, X(f(c, 0)), Y(f(c, 1))});
            dispatchPointer({PointerEvent::Type::Up, 9001, X(f(c, 0)), Y(f(c, 1))});
            scriptWait_ = 2;
        } else if (op == "down" || op == "move" || op == "up") {
            PointerEvent::Type t = op == "down" ? PointerEvent::Type::Down
                                   : op == "move" ? PointerEvent::Type::Move
                                                  : PointerEvent::Type::Up;
            dispatchPointer({t, static_cast<uint64_t>(f(c, 0)) + 100, X(f(c, 1)), Y(f(c, 2))});
        } else if (op == "back") {
            handleBack();
            scriptWait_ = 2;
        } else if (op == "shot") {
            pendingShot_ = c.args.empty() ? "shot" : c.args[0];  // taken after this frame renders
            scriptWait_ = 1;
        } else if (op == "launch") {
            launch(c.args.empty() ? "" : c.args[0]);
            scriptWait_ = 30;
        } else if (op == "tab") {
            hub_->showTab(static_cast<Hub::Tab>(static_cast<int>(f(c, 0))));
            scriptWait_ = 20;
        } else if (op == "detail") {
            hub_->openGame(c.args.empty() ? "" : c.args[0]);
            scriptWait_ = 20;
        } else if (op == "page") {
            std::string which = c.args.empty() ? "" : c.args[0];
            hub_->openPage(which == "about" ? Hub::Page::About : Hub::Page::Storage);
            scriptWait_ = 20;
        } else if (op == "expect_game") {
            std::string want = c.args.empty() ? "none" : c.args[0];
            std::string have = game_ ? gameId_ : "none";
            if (want != have) {
                SDL_Log("SCRIPT FAILURE: expected game '%s' but have '%s'", want.c_str(), have.c_str());
                scriptExit_ = 3;
            } else {
                SDL_Log("ok: game is %s", have.c_str());
            }
        } else if (op == "log") {
            std::string msg;
            for (auto& a : c.args) msg += a + " ";
            SDL_Log("script: %s", msg.c_str());
        } else if (op == "quit") {
            script_.clear();
        } else {
            SDL_Log("SCRIPT: unknown command %s", op.c_str());
            scriptExit_ = 4;
        }
    }
}

}  // namespace mgh
