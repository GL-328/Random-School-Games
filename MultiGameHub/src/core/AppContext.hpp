// Services shared by the hub and every game. Games receive this context when
// launched and must not keep references after they are destroyed.
#pragma once

#include <string>

#include "audio/Audio.hpp"
#include "core/Json.hpp"
#include "core/Settings.hpp"
#include "platform/Platform.hpp"
#include "storage/StorageManager.hpp"
#include "ui/Renderer2D.hpp"
#include "ui/Ui.hpp"
#include "updates/UpdateManager.hpp"

namespace mgh {

class AppContext {
public:
    AppContext(IPlatform& p, Settings& s, IAudio& a, StorageManager& st, UpdateManager& u, Ui& ui, Renderer2D& r)
        : platform(p), settings(s), audio(a), storage(st), updates(u), ui(ui), r(r) {}

    IPlatform& platform;
    Settings& settings;
    IAudio& audio;
    StorageManager& storage;
    UpdateManager& updates;
    Ui& ui;
    Renderer2D& r;

    Rect screen;  // full drawable area in pixels
    Rect safe;    // area not covered by notches / system bars
    float dp = 1.0f;
    float px(float v) const { return v * dp; }
    const Theme& theme() const { return ui.theme(); }

    void sfx(Sfx s, float volume = 1.0f, float pitch = 1.0f) { audio.play(s, volume, pitch); }
    void vibrate(int ms) {
        if (settings.getBool(keys::kVibration, true)) platform.haptics().vibrate(ms);
    }
    // Shows a short message at the bottom of the screen.
    void toast(const std::string& msg) {
        toastText = msg;
        toastTime = 2.8f;
    }
    // Optional downloaded content pack data (theme colours etc.). Returns null Json if absent.
    Json packTheme(const std::string& gameId) const;

    // Path for SDL file loaders (APK asset on Android, assets/ folder on desktop).
    std::string assetPath(const std::string& name) const { return assetRoot + name; }
    std::string assetRoot;

    std::string toastText;
    float toastTime = 0;
};

// Parses "#RRGGBB" colours from pack theme files; returns `def` when absent/invalid.
Color themeColor(const Json& j, const char* key, Color def);

}  // namespace mgh
