// The game hub: bottom navigation (Home, Games, Installed, Updates, Settings)
// plus sub-pages (game details, storage management, about).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/AppContext.hpp"
#include "games/Game.hpp"

namespace mgh {

class Hub {
public:
    enum class Tab { Home = 0, Games, Installed, Updates, Settings, Count };
    enum class Page { Root, GameDetail, Storage, About };

    Hub(AppContext& ctx, std::function<void(const std::string&)> launchGame,
        std::function<void(bool dark)> setTheme);

    void frame();
    bool onBack();  // returns false when already at the root of Home
    bool animating() const;
    void showTab(Tab t);
    void openGame(const std::string& id);
    Tab currentTab() const { return tab_; }

private:
    struct PageEntry {
        Page page;
        std::string gameId;
    };
    struct Confirm {
        bool open = false;
        std::string title, message, confirmLabel;
        bool danger = false;
        std::function<void()> onConfirm;
    };

    void push(Page p, const std::string& id = "");
    void pop();
    void startTransition(int dir);

    // pages
    float homeTab(float y, const Rect& area);
    float gamesTab(float y, const Rect& area);
    float installedTab(float y, const Rect& area);
    float updatesTab(float y, const Rect& area);
    float settingsTab(float y, const Rect& area);
    float gameDetailPage(float y, const Rect& area, const std::string& id);
    float storagePage(float y, const Rect& area);
    float aboutPage(float y, const Rect& area);

    // building blocks
    void drawNavBar(const Rect& bar);
    float pageHeader(float y, const Rect& area, const std::string& title, bool back, const std::string& subtitle = "");
    float sectionTitle(float y, const Rect& area, const std::string& title);
    void drawLogo(float x, float y, float size);
    void gameTile(const GameInfo& g, const Rect& rc, float radius);
    bool settingsToggleRow(const Rect& rc, Icon icon, const std::string& title, const std::string& sub, bool& value,
                           float& anim);
    bool settingsLinkRow(const Rect& rc, Icon icon, const std::string& title, const std::string& sub,
                         const std::string& trailing = "");
    void installedChip(const Rect& rc, const std::string& gameId);
    float packSection(float y, const Rect& area, const GameInfo& g);
    void drawConfirm();
    void ask(const std::string& title, const std::string& message, const std::string& label, bool danger,
             std::function<void()> fn);
    std::string lastCheckedText() const;

    AppContext& ctx_;
    std::function<void(const std::string&)> launch_;
    std::function<void(bool)> setTheme_;
    Tab tab_ = Tab::Home;
    std::vector<PageEntry> stack_;
    ScrollState scroll_[static_cast<int>(Tab::Count)];
    ScrollState pageScroll_;
    float trans_ = 1.0f;
    int transDir_ = 1;
    float navPill_ = 0;
    int featured_ = 0;
    float featuredTimer_ = 0;
    float featuredAnim_ = 1.0f;
    Confirm confirm_;
    float toggleAnim_[8] = {};
    // Cached storage numbers (refreshed when the storage page opens).
    struct StorageInfo {
        uint64_t app = 0, packs = 0, saves = 0, cache = 0;
        int64_t free = -1;
    } storageInfo_;
    void refreshStorageInfo();
};

}  // namespace mgh
