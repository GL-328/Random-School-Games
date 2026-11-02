#include "Hub.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

#include "core/FileUtil.hpp"

namespace mgh {

namespace {
constexpr const char* kTabNames[] = {"Home", "Games", "Installed", "Updates", "Settings"};
constexpr Icon kTabIcons[] = {Icon::Home, Icon::Grid, Icon::Box, Icon::Download, Icon::Gear};

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

Hub::Hub(AppContext& ctx, std::function<void(const std::string&)> launchGame, std::function<void(bool)> setTheme)
    : ctx_(ctx), launch_(std::move(launchGame)), setTheme_(std::move(setTheme)) {}

bool Hub::animating() const {
    return trans_ < 1.0f || featuredAnim_ < 1.0f || confirm_.open || tab_ == Tab::Home || ctx_.updates.busy();
}

void Hub::startTransition(int dir) {
    trans_ = 0;
    transDir_ = dir;
}

void Hub::showTab(Tab t) {
    stack_.clear();
    if (t == tab_) {
        scroll_[static_cast<int>(t)].vel = 0;
        scroll_[static_cast<int>(t)].y = 0;  // tapping the active tab scrolls to top
        return;
    }
    startTransition(static_cast<int>(t) > static_cast<int>(tab_) ? 1 : -1);
    tab_ = t;
    if (t == Tab::Installed) refreshStorageInfo();
}

void Hub::push(Page p, const std::string& id) {
    stack_.push_back({p, id});
    pageScroll_ = ScrollState();
    startTransition(1);
    if (p == Page::Storage) refreshStorageInfo();
}

void Hub::pop() {
    if (stack_.empty()) return;
    stack_.pop_back();
    pageScroll_ = ScrollState();
    startTransition(-1);
}

void Hub::openGame(const std::string& id) {
    if (findGame(id)) push(Page::GameDetail, id);
}

bool Hub::onBack() {
    if (confirm_.open) {
        confirm_.open = false;
        return true;
    }
    if (!stack_.empty()) {
        pop();
        return true;
    }
    if (tab_ != Tab::Home) {
        showTab(Tab::Home);
        return true;
    }
    return false;
}

void Hub::ask(const std::string& title, const std::string& message, const std::string& label, bool danger,
              std::function<void()> fn) {
    confirm_.open = true;
    confirm_.title = title;
    confirm_.message = message;
    confirm_.confirmLabel = label;
    confirm_.danger = danger;
    confirm_.onConfirm = std::move(fn);
}

void Hub::refreshStorageInfo() {
    IStorage& st = ctx_.storage.storage();
    storageInfo_.app = st.appInstallSize();
    storageInfo_.packs = directorySize(ctx_.storage.packsDir());
    storageInfo_.saves = directorySize(joinPath(st.dataDir(), "saves")) + fileSize(joinPath(st.dataDir(), "settings.cfg"));
    storageInfo_.cache = ctx_.storage.cacheSize();
    storageInfo_.free = st.freeSpace();
}

std::string Hub::lastCheckedText() const {
    int64_t t = ctx_.settings.getInt(keys::kLastUpdateCheck, 0);
    if (t <= 0) return "Never checked";
    int64_t d = std::max<int64_t>(0, nowSeconds() - t);
    if (d < 60) return "Last checked just now";
    if (d < 3600) return "Last checked " + std::to_string(d / 60) + " min ago";
    if (d < 86400) return "Last checked " + std::to_string(d / 3600) + " h ago";
    return "Last checked " + std::to_string(d / 86400) + " days ago";
}

// ====================================================================== frame

void Hub::frame() {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const Rect s = ctx_.safe;
    const float dt = ui.dt();

    const float navH = ctx_.px(66);
    Rect nav{ctx_.screen.x, s.bottom() - navH, ctx_.screen.w, ctx_.screen.bottom() - (s.bottom() - navH)};
    Rect area{s.x, s.y, s.w, nav.y - s.y};

    if (trans_ < 1.0f) {
        trans_ = std::min(1.0f, trans_ + dt / 0.24f);
        ui.animate();
    }
    const float e = easeOutCubic(trans_);
    ui.setInputEnabled(!confirm_.open);
    r.setOffset((1.0f - e) * transDir_ * ctx_.px(36), 0);
    r.setGlobalAlpha(0.2f + 0.8f * e);

    // Constrain content width on tablets.
    const float maxW = ctx_.px(720);
    Rect content = area;
    if (content.w > maxW) content = {area.cx() - maxW * 0.5f, area.y, maxW, area.h};

    ScrollState& sc = stack_.empty() ? scroll_[static_cast<int>(tab_)] : pageScroll_;
    ui.scrollBegin(sc, area);
    float y = content.y + ctx_.px(6);
    float end = y;
    if (!stack_.empty()) {
        const PageEntry& pe = stack_.back();
        switch (pe.page) {
            case Page::GameDetail: end = gameDetailPage(y, content, pe.gameId); break;
            case Page::Storage: end = storagePage(y, content); break;
            case Page::About: end = aboutPage(y, content); break;
            case Page::Root: break;
        }
    } else {
        switch (tab_) {
            case Tab::Home: end = homeTab(y, content); break;
            case Tab::Games: end = gamesTab(y, content); break;
            case Tab::Installed: end = installedTab(y, content); break;
            case Tab::Updates: end = updatesTab(y, content); break;
            case Tab::Settings: end = settingsTab(y, content); break;
            case Tab::Count: break;
        }
    }
    ui.scrollEnd(sc, end - area.y + ctx_.px(20));
    r.setOffset(0, 0);
    r.setGlobalAlpha(1.0f);

    drawNavBar(nav);
    ui.setInputEnabled(true);
    drawConfirm();
    (void)th;
}

// ====================================================================== pieces

void Hub::drawLogo(float x, float y, float size) {
    auto& r = ctx_.r;
    const float g = size * 0.1f, q = (size - g) / 2;
    const Color cols[4] = {Color::hex(0x7C5CFF), Color::hex(0x1FC8AA), Color::hex(0xFF5470), Color::hex(0xFFB547)};
    for (int i = 0; i < 4; ++i) {
        float qx = x + (i % 2) * (q + g), qy = y + (i / 2) * (q + g);
        r.fillRoundRectGradient({qx, qy, q, q}, q * 0.3f, cols[i].lighten(0.12f), cols[i].darken(0.08f));
    }
    r.fillCircle(x + q * 0.5f, y + q * 0.5f, q * 0.18f, Color(255, 255, 255, 220));
    r.fillTriangle({x + q + g + q * 0.35f, y + q * 0.3f}, {x + q + g + q * 0.72f, y + q * 0.5f},
                   {x + q + g + q * 0.35f, y + q * 0.7f}, Color(255, 255, 255, 220));
}

void Hub::gameTile(const GameInfo& g, const Rect& rc, float radius) {
    auto& r = ctx_.r;
    r.fillRoundRectGradient(rc, radius, g.colorA, g.colorB);
    r.pushClip(rc);
    g.drawArt(r, rc, ctx_.ui.time());
    r.popClip();
}

float Hub::pageHeader(float y, const Rect& area, const std::string& title, bool back, const std::string& subtitle) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    float x = area.x + pad;
    if (back) {
        Rect b{area.x + ctx_.px(8), y + ctx_.px(6), ctx_.px(44), ctx_.px(44)};
        if (ui.iconButton(b, Icon::Back, th.text, th.surface)) pop();
        x = b.right() + ctx_.px(10);
    }
    r.text(title, x, y + ctx_.px(subtitle.empty() ? 14 : 6), ctx_.px(24), th.text, FontId::Bold);
    if (!subtitle.empty()) r.text(subtitle, x, y + ctx_.px(36), ctx_.px(13), th.textDim);
    return y + ctx_.px(62);
}

float Hub::sectionTitle(float y, const Rect& area, const std::string& title) {
    ctx_.r.text(title, area.x + ctx_.px(18), y + ctx_.px(8), ctx_.px(16), ctx_.theme().text, FontId::Bold);
    return y + ctx_.px(36);
}

void Hub::installedChip(const Rect& rc, const std::string& gameId) {
    const Theme& th = ctx_.theme();
    const bool hasPack = ctx_.storage.hasPack(gameId);
    const bool upd = ctx_.updates.packUpdateAvailable(gameId);
    if (upd) ctx_.ui.pill(rc, "Update", th.warning.alpha(0.2f), th.warning);
    else ctx_.ui.pill(rc, hasPack ? "Installed +" : "Installed", th.success.alpha(0.16f), th.success);
}

void Hub::drawNavBar(const Rect& bar) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    r.fillRect(bar, th.bgElevated);
    r.fillRect({bar.x, bar.y, bar.w, ctx_.px(1)}, th.border);
    const int n = static_cast<int>(Tab::Count);
    const float maxW = ctx_.px(560);
    const float w = std::min(bar.w, maxW);
    const float x0 = bar.cx() - w * 0.5f;
    const float tw = w / n;
    const float itemH = ctx_.px(66);
    const int active = static_cast<int>(tab_);
    navPill_ = approach(navPill_, static_cast<float>(active), 18, ui.dt());
    if (std::fabs(navPill_ - active) > 0.002f) ui.animate();
    else navPill_ = static_cast<float>(active);
    Rect pill{x0 + navPill_ * tw + tw * 0.5f - ctx_.px(30), bar.y + ctx_.px(9), ctx_.px(60), ctx_.px(30)};
    r.fillRoundRect(pill, ctx_.px(15), th.accentSoft);
    for (int i = 0; i < n; ++i) {
        Rect item{x0 + i * tw, bar.y, tw, itemH};
        const bool on = i == active && stack_.empty();
        const Color c = i == active ? th.accent : th.textDim;
        drawIcon(r, kTabIcons[i], {item.cx() - ctx_.px(11), bar.y + ctx_.px(13), ctx_.px(22), ctx_.px(22)}, c);
        r.text(kTabNames[i], item.cx(), bar.y + ctx_.px(43), ctx_.px(11), c, i == active ? FontId::Bold : FontId::Regular,
               Align::Center);
        if (i == static_cast<int>(Tab::Updates)) {
            int count = (ctx_.updates.appUpdateAvailable() ? 1 : 0) + ctx_.updates.pendingGameUpdates();
            if (count > 0) ui.badge(item.cx() + ctx_.px(14), bar.y + ctx_.px(12), std::to_string(count), th.danger, Color::hex(0xFFFFFF));
        }
        if (ui.tapped(item)) {
            if (!on) ctx_.sfx(Sfx::Click, 0.6f);
            showTab(static_cast<Tab>(i));
        }
    }
}

bool Hub::settingsToggleRow(const Rect& rc, Icon icon, const std::string& title, const std::string& sub, bool& value,
                            float& anim) {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    Rect ib{rc.x + ctx_.px(14), rc.cy() - ctx_.px(17), ctx_.px(34), ctx_.px(34)};
    r.fillRoundRect(ib, ctx_.px(10), th.accentSoft);
    drawIcon(r, icon, ib.inset(ctx_.px(7)), th.accent);
    const float tx = ib.right() + ctx_.px(12);
    r.text(title, tx, rc.y + ctx_.px(sub.empty() ? 21 : 12), ctx_.px(15), th.text, FontId::Bold);
    if (!sub.empty()) r.text(r.elide(sub, rc.right() - tx - ctx_.px(76), ctx_.px(12.5f)), tx, rc.y + ctx_.px(33), ctx_.px(12.5f), th.textDim);
    return ctx_.ui.toggle(rc.inset(ctx_.px(14), 0), value, anim);
}

bool Hub::settingsLinkRow(const Rect& rc, Icon icon, const std::string& title, const std::string& sub,
                          const std::string& trailing) {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    if (ctx_.ui.pressing(rc)) r.fillRect(rc, th.surfacePressed);
    Rect ib{rc.x + ctx_.px(14), rc.cy() - ctx_.px(17), ctx_.px(34), ctx_.px(34)};
    r.fillRoundRect(ib, ctx_.px(10), th.accentSoft);
    drawIcon(r, icon, ib.inset(ctx_.px(7)), th.accent);
    const float tx = ib.right() + ctx_.px(12);
    r.text(title, tx, rc.y + ctx_.px(sub.empty() ? 21 : 12), ctx_.px(15), th.text, FontId::Bold);
    if (!sub.empty()) r.text(r.elide(sub, rc.right() - tx - ctx_.px(90), ctx_.px(12.5f)), tx, rc.y + ctx_.px(33), ctx_.px(12.5f), th.textDim);
    float right = rc.right() - ctx_.px(14);
    drawIcon(r, Icon::ChevronRight, {right - ctx_.px(18), rc.cy() - ctx_.px(9), ctx_.px(18), ctx_.px(18)}, th.textFaint);
    if (!trailing.empty())
        r.text(trailing, right - ctx_.px(24), rc.cy() - ctx_.px(9), ctx_.px(13), th.textDim, FontId::Regular, Align::Right);
    return ctx_.ui.tapped(rc);
}

void Hub::drawConfirm() {
    if (!confirm_.open) return;
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    r.fillRect(ctx_.screen, th.overlay);
    const float w = std::min(ctx_.px(320), ctx_.safe.w - ctx_.px(32));
    auto lines = r.wrap(confirm_.message, w - ctx_.px(40), ctx_.px(14));
    const float h = ctx_.px(130) + lines.size() * ctx_.px(20);
    Rect card{ctx_.safe.cx() - w * 0.5f, ctx_.safe.cy() - h * 0.5f, w, h};
    r.fillRoundRect(card, ctx_.px(22), th.surface);
    r.text(confirm_.title, card.x + ctx_.px(20), card.y + ctx_.px(20), ctx_.px(18), th.text, FontId::Bold);
    float ly = card.y + ctx_.px(52);
    for (auto& l : lines) {
        r.text(l, card.x + ctx_.px(20), ly, ctx_.px(14), th.textDim);
        ly += ctx_.px(20);
    }
    float bw = (w - ctx_.px(52)) / 2;
    Rect b1{card.x + ctx_.px(20), card.bottom() - ctx_.px(62), bw, ctx_.px(44)};
    Rect b2{b1.right() + ctx_.px(12), b1.y, bw, b1.h};
    if (ui.button(b1, "Cancel", ButtonStyle::Secondary)) confirm_.open = false;
    if (ui.button(b2, confirm_.confirmLabel, confirm_.danger ? ButtonStyle::Danger : ButtonStyle::Primary)) {
        confirm_.open = false;
        if (confirm_.onConfirm) confirm_.onConfirm();
    }
    if (ui.tapped(ctx_.screen)) confirm_.open = false;
}

// ====================================================================== Home

float Hub::homeTab(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    const auto& games = gameRegistry();

    // Header.
    drawLogo(area.x + pad, y + ctx_.px(10), ctx_.px(40));
    r.text("MultiGame Hub", area.x + pad + ctx_.px(52), y + ctx_.px(8), ctx_.px(22), th.text, FontId::Bold);
    r.text(std::to_string(games.size()) + " games ready - play offline", area.x + pad + ctx_.px(52), y + ctx_.px(36),
           ctx_.px(12.5f), th.textDim);
    if (ui.iconButton({area.right() - pad - ctx_.px(40), y + ctx_.px(10), ctx_.px(40), ctx_.px(40)}, Icon::Gear, th.text,
                      th.surface))
        showTab(Tab::Settings);
    y += ctx_.px(68);

    // Update banner.
    if (ctx_.updates.appUpdateAvailable()) {
        Rect b{area.x + pad, y, area.w - 2 * pad, ctx_.px(60)};
        r.fillRoundRectGradient(b, ctx_.px(16), th.accent.lighten(0.05f), th.accent.darken(0.2f));
        drawIcon(r, Icon::Download, {b.x + ctx_.px(14), b.cy() - ctx_.px(12), ctx_.px(24), ctx_.px(24)}, th.accentText);
        r.text("Update available", b.x + ctx_.px(50), b.y + ctx_.px(11), ctx_.px(15), th.accentText, FontId::Bold);
        r.text("Version " + ctx_.updates.manifest()->app.version.str() + " - tap to view", b.x + ctx_.px(50),
               b.y + ctx_.px(32), ctx_.px(12.5f), th.accentText.alpha(0.85f));
        if (ui.tapped(b)) showTab(Tab::Updates);
        y += b.h + ctx_.px(16);
    }

    // Featured carousel.
    y = sectionTitle(y, area, "Featured");
    std::vector<const GameInfo*> feat;
    for (const auto& g : games)
        if (g.featured) feat.push_back(&g);
    if (!feat.empty()) {
        featured_ %= static_cast<int>(feat.size());
        featuredTimer_ += ui.dt();
        if (featuredTimer_ > 6.0f && !ui.anyPointerDown()) {
            featured_ = (featured_ + 1) % static_cast<int>(feat.size());
            featuredTimer_ = 0;
            featuredAnim_ = 0;
        }
        if (featuredAnim_ < 1) featuredAnim_ = std::min(1.0f, featuredAnim_ + ui.dt() / 0.35f);
        const GameInfo& g = *feat[featured_];
        Rect card{area.x + pad, y, area.w - 2 * pad, ctx_.px(196)};
        // Horizontal swipe changes the featured game.
        for (const auto& p : ui.pointers()) {
            if (!p.releasedNow || !ui.inputEnabled()) continue;
            float dx = p.x - p.sx, dy = p.y - p.sy;
            if (card.contains(p.sx, p.sy + ui.scrollY()) && std::fabs(dx) > ctx_.px(40) && std::fabs(dx) > std::fabs(dy) * 1.3f) {
                int n = static_cast<int>(feat.size());
                featured_ = (featured_ + (dx < 0 ? 1 : n - 1)) % n;
                featuredTimer_ = 0;
                featuredAnim_ = 0;
            }
        }
        const float fa = easeOutCubic(featuredAnim_);
        r.fillRoundRect(card.offset(0, ctx_.px(4)), ctx_.px(22), Color(0, 0, 0, th.dark ? 90 : 35));
        r.fillRoundRectGradient(card, ctx_.px(22), g.colorA, g.colorB);
        const float artW = card.h * 0.95f;
        Rect art{card.right() - artW - ctx_.px(4) + (1 - fa) * ctx_.px(30), card.y + (card.h - artW) * 0.5f, artW, artW};
        r.setGlobalAlpha(r.globalAlpha() * fa);
        g.drawArt(r, art, ui.time());
        r.setGlobalAlpha(0.2f + 0.8f * easeOutCubic(trans_));
        const float tx = card.x + ctx_.px(20);
        r.text("FEATURED", tx, card.y + ctx_.px(20), ctx_.px(11), Color(255, 255, 255, 190), FontId::Bold);
        r.text(g.name, tx, card.y + ctx_.px(38), ctx_.px(28), Color::hex(0xFFFFFF), FontId::Bold);
        r.text(g.tagline, tx, card.y + ctx_.px(76), ctx_.px(14), Color(255, 255, 255, 215));
        r.text(g.players, tx, card.y + ctx_.px(97), ctx_.px(12), Color(255, 255, 255, 170));
        Rect play{tx, card.bottom() - ctx_.px(62), ctx_.px(120), ctx_.px(44)};
        bool down = ui.pressing(play);
        r.fillRoundRect(play, ctx_.px(22), down ? Color(235, 235, 245) : Color::hex(0xFFFFFF));
        drawIcon(r, Icon::Play, {play.x + ctx_.px(16), play.cy() - ctx_.px(9), ctx_.px(18), ctx_.px(18)}, g.colorB);
        r.textIn("Play", {play.x + ctx_.px(40), play.y, play.w - ctx_.px(52), play.h}, ctx_.px(15), g.colorB, FontId::Bold);
        if (ui.tapped(play)) launch_(g.id);
        else if (ui.tapped(card)) openGame(g.id);
        // Dots.
        const float dotY = card.bottom() + ctx_.px(12);
        const float total = feat.size() * ctx_.px(14);
        for (size_t i = 0; i < feat.size(); ++i) {
            bool on = static_cast<int>(i) == featured_;
            float dx = card.cx() - total * 0.5f + i * ctx_.px(14) + ctx_.px(7);
            r.fillRoundRect({dx - ctx_.px(on ? 8 : 3), dotY - ctx_.px(3), ctx_.px(on ? 16 : 6), ctx_.px(6)}, ctx_.px(3),
                            on ? th.accent : th.textFaint.alpha(0.6f));
        }
        y = dotY + ctx_.px(18);
    }

    // Library grid.
    y = sectionTitle(y, area, "Your library");
    const int cols = area.w > ctx_.px(560) ? 3 : 2;
    const float gap = ctx_.px(14);
    const float cw = (area.w - 2 * pad - gap * (cols - 1)) / cols;
    const float tileH = cw * 0.68f;
    const float ch = tileH + ctx_.px(100);
    for (size_t i = 0; i < games.size(); ++i) {
        const GameInfo& g = games[i];
        int c = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        Rect card{area.x + pad + c * (cw + gap), y + row * (ch + gap), cw, ch};
        const bool pressed = ui.pressing(card);
        r.fillRoundRect(card, ctx_.px(18), pressed ? th.surface2 : th.surface);
        Rect tile{card.x + ctx_.px(8), card.y + ctx_.px(8), card.w - ctx_.px(16), tileH};
        gameTile(g, tile, ctx_.px(13));
        r.text(r.elide(g.name, card.w - ctx_.px(24), ctx_.px(15.5f), FontId::Bold), card.x + ctx_.px(12),
               tile.bottom() + ctx_.px(10), ctx_.px(15.5f), th.text, FontId::Bold);
        r.text(r.elide(g.players, card.w - ctx_.px(24), ctx_.px(12)), card.x + ctx_.px(12), tile.bottom() + ctx_.px(32),
               ctx_.px(12), th.textDim);
        Rect chip{card.x + ctx_.px(12), card.bottom() - ctx_.px(40), ctx_.px(78), ctx_.px(24)};
        installedChip(chip, g.id);
        Rect play{card.right() - ctx_.px(12) - ctx_.px(44), card.bottom() - ctx_.px(50), ctx_.px(44), ctx_.px(44)};
        if (ui.iconButton(play, Icon::Play, th.accentText, th.accent, 0.42f)) launch_(g.id);
        else if (ui.tapped(card)) openGame(g.id);
    }
    const int rows = (static_cast<int>(games.size()) + cols - 1) / cols;
    y += rows * (ch + gap) + ctx_.px(4);

    // Expansion hint.
    Rect more{area.x + pad, y, area.w - 2 * pad, ctx_.px(72)};
    r.strokeRoundRect(more, ctx_.px(18), ctx_.px(1.5f), th.border);
    drawIcon(r, Icon::Star, {more.x + ctx_.px(16), more.cy() - ctx_.px(13), ctx_.px(26), ctx_.px(26)}, th.warning);
    r.text("More games are on the way", more.x + ctx_.px(56), more.y + ctx_.px(16), ctx_.px(14.5f), th.text, FontId::Bold);
    r.text("New games and content arrive through updates", more.x + ctx_.px(56), more.y + ctx_.px(39), ctx_.px(12), th.textDim);
    if (ui.tapped(more)) showTab(Tab::Updates);
    y += more.h + ctx_.px(12);
    return y;
}

// ====================================================================== Games

float Hub::gamesTab(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    const auto& games = gameRegistry();
    y = pageHeader(y, area, "All Games", false, std::to_string(games.size()) + " games in this version");
    for (const auto& g : games) {
        Rect row{area.x + pad, y, area.w - 2 * pad, ctx_.px(92)};
        r.fillRoundRect(row, ctx_.px(18), ui.pressing(row) ? th.surface2 : th.surface);
        Rect tile{row.x + ctx_.px(10), row.y + ctx_.px(10), ctx_.px(72), ctx_.px(72)};
        gameTile(g, tile, ctx_.px(14));
        const float tx = tile.right() + ctx_.px(14);
        const float tw = row.right() - tx - ctx_.px(70);
        r.text(r.elide(g.name, tw, ctx_.px(16), FontId::Bold), tx, row.y + ctx_.px(14), ctx_.px(16), th.text, FontId::Bold);
        r.text(r.elide(g.tagline, tw, ctx_.px(12.5f)), tx, row.y + ctx_.px(37), ctx_.px(12.5f), th.textDim);
        installedChip({tx, row.y + ctx_.px(58), ctx_.px(78), ctx_.px(22)}, g.id);
        r.text("v" + g.version, tx + ctx_.px(86), row.y + ctx_.px(61), ctx_.px(11.5f), th.textFaint);
        Rect play{row.right() - ctx_.px(58), row.cy() - ctx_.px(22), ctx_.px(44), ctx_.px(44)};
        if (ui.iconButton(play, Icon::Play, th.accentText, th.accent, 0.42f)) launch_(g.id);
        else if (ui.tapped(row)) openGame(g.id);
        y += row.h + ctx_.px(12);
    }
    // Games announced in the release manifest that need a newer app version.
    if (const Manifest* m = ctx_.updates.manifest()) {
        bool header = false;
        for (const auto& g : m->games) {
            if (findGame(g.id)) continue;
            if (!header) {
                y = sectionTitle(y + ctx_.px(4), area, "Available with the next update");
                header = true;
            }
            Rect row{area.x + pad, y, area.w - 2 * pad, ctx_.px(76)};
            r.fillRoundRect(row, ctx_.px(18), th.surface);
            Rect tile{row.x + ctx_.px(10), row.y + ctx_.px(10), ctx_.px(56), ctx_.px(56)};
            r.fillRoundRect(tile, ctx_.px(12), th.surface2);
            drawIcon(r, Icon::Download, tile.inset(ctx_.px(14)), th.textDim);
            r.text(g.name, tile.right() + ctx_.px(12), row.y + ctx_.px(16), ctx_.px(15), th.text, FontId::Bold);
            r.text(r.elide(g.description.empty() ? "New game" : g.description, row.w - ctx_.px(170), ctx_.px(12)),
                   tile.right() + ctx_.px(12), row.y + ctx_.px(40), ctx_.px(12), th.textDim);
            if (ui.button({row.right() - ctx_.px(92), row.cy() - ctx_.px(18), ctx_.px(80), ctx_.px(36)}, "Update",
                          ButtonStyle::Secondary))
                showTab(Tab::Updates);
            y += row.h + ctx_.px(12);
        }
    }
    return y;
}

// ====================================================================== Installed

float Hub::installedTab(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    y = pageHeader(y, area, "Installed", false, "Built-in games work offline, no download needed");
    for (const auto& g : gameRegistry()) {
        Rect row{area.x + pad, y, area.w - 2 * pad, ctx_.px(84)};
        r.fillRoundRect(row, ctx_.px(18), th.surface);
        Rect tile{row.x + ctx_.px(10), row.y + ctx_.px(10), ctx_.px(64), ctx_.px(64)};
        gameTile(g, tile, ctx_.px(14));
        const float tx = tile.right() + ctx_.px(14);
        r.text(g.name, tx, row.y + ctx_.px(14), ctx_.px(16), th.text, FontId::Bold);
        uint64_t content = ctx_.storage.packSize(g.id);
        uint64_t saves = ctx_.storage.saveDataSize(g.id);
        std::string sub = "Built-in v" + g.version;
        if (auto pv = ctx_.storage.packVersion(g.id)) sub += " - pack v" + pv->str() + " (" + formatBytes(content) + ")";
        else if (saves) sub += " - " + formatBytes(saves) + " data";
        r.text(r.elide(sub, row.right() - tx - ctx_.px(100), ctx_.px(12.5f)), tx, row.y + ctx_.px(38), ctx_.px(12.5f), th.textDim);
        installedChip({tx, row.y + ctx_.px(56), ctx_.px(78), ctx_.px(20)}, g.id);
        if (ui.button({row.right() - ctx_.px(96), row.cy() - ctx_.px(18), ctx_.px(84), ctx_.px(36)}, "Manage",
                      ButtonStyle::Secondary))
            openGame(g.id);
        y += row.h + ctx_.px(12);
    }
    Rect link{area.x + pad, y + ctx_.px(4), area.w - 2 * pad, ctx_.px(64)};
    r.fillRoundRect(link, ctx_.px(18), th.surface);
    if (settingsLinkRow(link, Icon::Storage, "Storage", "App, content, data and cache usage")) push(Page::Storage);
    return link.bottom() + ctx_.px(12);
}

// ====================================================================== Updates

float Hub::updatesTab(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    UpdateManager& um = ctx_.updates;
    const float pad = ctx_.px(18);
    y = pageHeader(y, area, "Updates", false, lastCheckedText());

    // App card.
    const AppInfo info = ctx_.platform.appInfo();
    const auto phase = um.phase();
    const bool available = um.appUpdateAvailable();
    Rect card{area.x + pad, y, area.w - 2 * pad, 0};
    const float inner = card.w - ctx_.px(32);
    std::vector<std::string> notes;
    if (available && !um.manifest()->app.notes.empty()) notes = r.wrap(um.manifest()->app.notes, inner, ctx_.px(13));
    if (notes.size() > 14) {
        notes.resize(14);
        notes.back() += " ...";
    }
    float h = ctx_.px(150);
    if (available) h += ctx_.px(34) + notes.size() * ctx_.px(19) + ctx_.px(70);
    if (phase == UpdateManager::Phase::ReadyToInstall || phase == UpdateManager::Phase::Installing) h += ctx_.px(64);
    if (!um.error().empty()) h += ctx_.px(54);
    card.h = h;
    r.fillRoundRect(card, ctx_.px(20), th.surface);
    drawLogo(card.x + ctx_.px(16), card.y + ctx_.px(16), ctx_.px(44));
    r.text("MultiGame Hub", card.x + ctx_.px(74), card.y + ctx_.px(16), ctx_.px(17), th.text, FontId::Bold);
    r.text("Installed version " + info.versionName, card.x + ctx_.px(74), card.y + ctx_.px(40), ctx_.px(13), th.textDim);
    float cy = card.y + ctx_.px(76);

    // Status line.
    std::string status = um.statusText();
    if (status.empty()) status = um.hasManifest() ? (available ? "An update is available" : "You're up to date")
                                                  : "Tap below to check for updates";
    Color statusC = available ? th.accent : (um.hasManifest() ? th.success : th.textDim);
    r.text(r.elide(status, inner, ctx_.px(13.5f), FontId::Bold), card.x + ctx_.px(16), cy, ctx_.px(13.5f), statusC, FontId::Bold);
    cy += ctx_.px(26);

    const bool downloading = phase == UpdateManager::Phase::CheckingManifest ||
                             phase == UpdateManager::Phase::DownloadingApp || phase == UpdateManager::Phase::DownloadingPack ||
                             phase == UpdateManager::Phase::VerifyingApp;
    Rect btn{card.x + ctx_.px(16), cy, inner, ctx_.px(44)};
    if (downloading) {
        Rect bar{btn.x, btn.y + ctx_.px(6), btn.w - ctx_.px(96), ctx_.px(8)};
        ui.progressBar(bar, um.progress(), th.accent);
        std::string prog = formatBytes(um.bytesReceived());
        if (um.bytesTotal() > 0) prog += " of " + formatBytes(static_cast<uint64_t>(um.bytesTotal()));
        r.text(prog, bar.x, bar.bottom() + ctx_.px(6), ctx_.px(11.5f), th.textDim);
        if (ui.button({btn.right() - ctx_.px(84), btn.y - ctx_.px(4), ctx_.px(84), ctx_.px(36)}, "Cancel",
                      ButtonStyle::Secondary))
            um.cancel();
    } else if (!available || phase == UpdateManager::Phase::Error) {
        if (ui.button(btn, "Check for updates", ButtonStyle::Primary, Icon::Refresh)) {
            um.clearError();
            um.checkForUpdates();
            ctx_.settings.setInt(keys::kLastUpdateCheck, nowSeconds());
        }
    }
    cy += ctx_.px(52);

    if (!um.error().empty()) {
        Rect er{card.x + ctx_.px(16), cy, inner, ctx_.px(44)};
        r.fillRoundRect(er, ctx_.px(12), th.danger.alpha(0.14f));
        r.text(r.elide(um.error(), inner - ctx_.px(24), ctx_.px(12.5f)), er.x + ctx_.px(12), er.y + ctx_.px(14),
               ctx_.px(12.5f), th.danger);
        cy += ctx_.px(54);
    }

    if (available) {
        const Manifest* m = um.manifest();
        r.text("What's new in " + m->app.version.str(), card.x + ctx_.px(16), cy, ctx_.px(15), th.text, FontId::Bold);
        cy += ctx_.px(28);
        for (const auto& line : notes) {
            r.text(line, card.x + ctx_.px(16), cy, ctx_.px(13), th.textDim);
            cy += ctx_.px(19);
        }
        cy += ctx_.px(12);
        Rect b{card.x + ctx_.px(16), cy, inner, ctx_.px(46)};
        if (phase == UpdateManager::Phase::ReadyToInstall || phase == UpdateManager::Phase::Installing) {
            if (ui.button(b, "Install update", ButtonStyle::Success, Icon::Download, phase == UpdateManager::Phase::ReadyToInstall))
                um.installApp();
            cy += ctx_.px(54);
            auto expl = r.wrap("Android will ask you to confirm the installation. If asked, allow MultiGame Hub to "
                               "\"Install unknown apps\". Your settings and scores are kept.",
                               inner, ctx_.px(12));
            for (auto& l : expl) {
                r.text(l, card.x + ctx_.px(16), cy, ctx_.px(12), th.textFaint);
                cy += ctx_.px(17);
            }
            card.h = std::max(card.h, cy - card.y + ctx_.px(12));
        } else if (!downloading) {
            std::string label = "Download " + formatBytes(m->app.apk.size);
            if (ui.button(b, label, ButtonStyle::Primary, Icon::Download)) um.downloadApp();
        }
    }
    y = std::max(card.bottom(), cy) + ctx_.px(20);

    // Game content packs.
    y = sectionTitle(y, area, "Game content");
    bool any = false;
    for (const auto& g : gameRegistry()) {
        const GameEntry* e = um.manifest() ? um.manifest()->findGame(g.id) : nullptr;
        if (!e || !e->pack) continue;
        any = true;
        y = packSection(y, area, g);
    }
    if (!any) {
        Rect info2{area.x + pad, y, area.w - 2 * pad, ctx_.px(64)};
        r.fillRoundRect(info2, ctx_.px(18), th.surface);
        r.text(um.hasManifest() ? "No optional content packs in this release" : "Check for updates to see content packs",
               info2.x + ctx_.px(16), info2.y + ctx_.px(14), ctx_.px(13.5f), th.textDim, FontId::Bold);
        r.text("All built-in games are fully playable offline.", info2.x + ctx_.px(16), info2.y + ctx_.px(36),
               ctx_.px(12), th.textFaint);
        y += info2.h + ctx_.px(12);
    }
    return y;
}

float Hub::packSection(float y, const Rect& area, const GameInfo& g) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    UpdateManager& um = ctx_.updates;
    const float pad = ctx_.px(18);
    const GameEntry* e = um.manifest() ? um.manifest()->findGame(g.id) : nullptr;
    auto installed = ctx_.storage.packVersion(g.id);
    Rect row{area.x + pad, y, area.w - 2 * pad, ctx_.px(80)};
    r.fillRoundRect(row, ctx_.px(18), th.surface);
    Rect tile{row.x + ctx_.px(10), row.y + ctx_.px(10), ctx_.px(60), ctx_.px(60)};
    gameTile(g, tile, ctx_.px(13));
    const float tx = tile.right() + ctx_.px(12);
    r.text(g.name + " content", tx, row.y + ctx_.px(14), ctx_.px(15), th.text, FontId::Bold);
    std::string sub;
    if (installed) sub = "Installed v" + installed->str() + " - " + formatBytes(ctx_.storage.packSize(g.id));
    else sub = "Optional - not installed";
    if (e && e->pack) sub += (installed && *installed < e->version) ? " - v" + e->version.str() + " available" : "";
    r.text(r.elide(sub, row.right() - tx - ctx_.px(110), ctx_.px(12)), tx, row.y + ctx_.px(38), ctx_.px(12), th.textDim);
    if (e && e->pack)
        r.text(formatBytes(e->pack->size) + " download", tx, row.y + ctx_.px(56), ctx_.px(11.5f), th.textFaint);

    Rect b{row.right() - ctx_.px(104), row.cy() - ctx_.px(18), ctx_.px(92), ctx_.px(36)};
    if (um.phase() == UpdateManager::Phase::DownloadingPack && um.activePack() == g.id) {
        ui.progressBar({b.x, b.cy() - ctx_.px(3), b.w, ctx_.px(6)}, um.progress(), th.accent);
    } else if (um.packUpdateAvailable(g.id)) {
        if (ui.button(b, "Update", ButtonStyle::Primary, Icon::None, !um.busy())) um.downloadPack(g.id);
    } else if (!installed && um.packInstallable(g.id)) {
        if (ui.button(b, "Get", ButtonStyle::Primary, Icon::Download, !um.busy())) um.downloadPack(g.id);
    } else if (installed) {
        if (ui.button(b, "Remove", ButtonStyle::Secondary)) {
            std::string id = g.id, name = g.name;
            ask("Remove content?", "The optional " + name + " content pack will be deleted. The game stays playable.",
                "Remove", true, [this, id] {
                    ctx_.storage.uninstallPack(id);
                    refreshStorageInfo();
                    ctx_.toast("Content removed");
                });
        }
    }
    return y + row.h + ctx_.px(12);
}

// ====================================================================== Settings

float Hub::settingsTab(float y, const Rect& area) {
    auto& r = ctx_.r;
    const Theme& th = ctx_.theme();
    Settings& st = ctx_.settings;
    const float pad = ctx_.px(18);
    const float rowH = ctx_.px(60);
    y = pageHeader(y, area, "Settings", false);

    auto group = [&](const std::string& title, int rows) {
        y = sectionTitle(y, area, title);
        Rect g{area.x + pad, y, area.w - 2 * pad, rowH * rows};
        r.fillRoundRect(g, ctx_.px(18), th.surface);
        for (int i = 1; i < rows; ++i)
            r.fillRect({g.x + ctx_.px(60), g.y + i * rowH, g.w - ctx_.px(60), ctx_.px(1)}, th.border.alpha(0.7f));
        return g;
    };
    auto rowAt = [&](const Rect& g, int i) { return Rect{g.x, g.y + i * rowH, g.w, rowH}; };

    Rect g1 = group("Appearance", 1);
    bool dark = st.getString(keys::kTheme, "dark") != "light";
    if (settingsToggleRow(rowAt(g1, 0), dark ? Icon::Moon : Icon::Sun, "Dark theme", "Easy on the eyes and battery", dark,
                          toggleAnim_[0])) {
        st.setString(keys::kTheme, dark ? "dark" : "light");
        setTheme_(dark);
    }
    y = g1.bottom() + ctx_.px(14);

    Rect g2 = group("Sound & feel", 3);
    bool sound = st.getBool(keys::kSound, true), music = st.getBool(keys::kMusic, true),
         vib = st.getBool(keys::kVibration, true);
    if (settingsToggleRow(rowAt(g2, 0), Icon::Sound, "Sound effects", "", sound, toggleAnim_[1])) {
        st.setBool(keys::kSound, sound);
        ctx_.audio.setSoundEnabled(sound);
        ctx_.sfx(Sfx::Click);
    }
    if (settingsToggleRow(rowAt(g2, 1), Icon::Music, "Music", "Calm background music in menus", music, toggleAnim_[2])) {
        st.setBool(keys::kMusic, music);
        ctx_.audio.setMusicEnabled(music);
    }
    if (settingsToggleRow(rowAt(g2, 2), Icon::Vibrate, "Vibration", "Haptic feedback in games", vib, toggleAnim_[3])) {
        st.setBool(keys::kVibration, vib);
        if (vib) ctx_.vibrate(30);
    }
    y = g2.bottom() + ctx_.px(14);

    Rect g3 = group("Updates", 2);
    bool autoUp = st.getBool(keys::kAutoUpdate, true);
    if (settingsToggleRow(rowAt(g3, 0), Icon::Clock, "Automatic update checks", "Checks GitHub Releases once a day",
                          autoUp, toggleAnim_[4]))
        st.setBool(keys::kAutoUpdate, autoUp);
    if (settingsLinkRow(rowAt(g3, 1), Icon::Refresh, "Check for updates", lastCheckedText())) {
        showTab(Tab::Updates);
        ctx_.updates.clearError();
        ctx_.updates.checkForUpdates();
        st.setInt(keys::kLastUpdateCheck, nowSeconds());
    }
    y = g3.bottom() + ctx_.px(14);

    Rect g4 = group("General", 3);
    if (settingsLinkRow(rowAt(g4, 0), Icon::Storage, "Storage management", "Sizes, content packs and cache")) push(Page::Storage);
    if (settingsLinkRow(rowAt(g4, 1), Icon::Info, "About", "Licences and project information")) push(Page::About);
    settingsLinkRow(rowAt(g4, 2), Icon::Check, "App version", ctx_.platform.appInfo().platformName,
                    ctx_.platform.appInfo().versionName);
    y = g4.bottom() + ctx_.px(14);
    return y;
}

// ====================================================================== Game detail

float Hub::gameDetailPage(float y, const Rect& area, const std::string& id) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const GameInfo* g = findGame(id);
    if (!g) return y;
    const float pad = ctx_.px(18);
    y = pageHeader(y, area, g->name, true);
    Rect hero{area.x + pad, y, area.w - 2 * pad, ctx_.px(210)};
    r.fillRoundRect(hero.offset(0, ctx_.px(4)), ctx_.px(24), Color(0, 0, 0, th.dark ? 90 : 35));
    gameTile(*g, hero, ctx_.px(24));
    y = hero.bottom() + ctx_.px(18);
    r.text(g->tagline, area.x + pad, y, ctx_.px(18), th.text, FontId::Bold);
    y += ctx_.px(30);
    Rect chip{area.x + pad, y, ctx_.px(78), ctx_.px(24)};
    installedChip(chip, g->id);
    ui.pill({chip.right() + ctx_.px(8), y, ctx_.px(64), ctx_.px(24)}, "v" + g->version, th.surface2, th.textDim);
    ui.pill({chip.right() + ctx_.px(80), y, r.measure(g->players, ctx_.px(11.5f), FontId::Bold) + ctx_.px(20), ctx_.px(24)},
            g->players, th.surface2, th.textDim);
    y += ctx_.px(40);
    for (auto& line : r.wrap(g->description, area.w - 2 * pad, ctx_.px(14))) {
        r.text(line, area.x + pad, y, ctx_.px(14), th.textDim);
        y += ctx_.px(21);
    }
    y += ctx_.px(14);
    Rect play{area.x + pad, y, area.w - 2 * pad, ctx_.px(54)};
    if (ui.button(play, "Play " + g->name, ButtonStyle::Primary, Icon::Play)) launch_(g->id);
    y += play.h + ctx_.px(22);

    // Storage & content for this game.
    y = sectionTitle(y, area, "Storage");
    Rect box{area.x + pad, y, area.w - 2 * pad, ctx_.px(60)};
    r.fillRoundRect(box, ctx_.px(18), th.surface);
    uint64_t saves = ctx_.storage.saveDataSize(g->id);
    std::string desc = "Game code is built in (no download). ";
    desc += ctx_.storage.hasPack(g->id) ? "Content pack: " + formatBytes(ctx_.storage.packSize(g->id)) : "No content pack installed";
    r.text(r.elide(desc, box.w - ctx_.px(32), ctx_.px(12.5f)), box.x + ctx_.px(16), box.y + ctx_.px(12), ctx_.px(12.5f), th.textDim);
    r.text("Saved data: " + formatBytes(saves), box.x + ctx_.px(16), box.y + ctx_.px(34), ctx_.px(12.5f), th.textDim);
    y = box.bottom() + ctx_.px(12);
    const GameEntry* e = ctx_.updates.manifest() ? ctx_.updates.manifest()->findGame(g->id) : nullptr;
    if ((e && e->pack) || ctx_.storage.hasPack(g->id)) y = packSection(y, area, *g);
    if (id == "snake" && ctx_.settings.getInt(keys::kSnakeHighScore, 0) > 0) {
        if (ui.button({area.x + pad, y, area.w - 2 * pad, ctx_.px(44)}, "Reset high score", ButtonStyle::Secondary)) {
            ask("Reset high score?", "Your best Snake score will be set back to zero.", "Reset", true, [this] {
                ctx_.settings.setInt(keys::kSnakeHighScore, 0);
                ctx_.toast("High score reset");
            });
        }
        y += ctx_.px(56);
    }
    return y;
}

// ====================================================================== Storage

float Hub::storagePage(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    y = pageHeader(y, area, "Storage", true);
    const StorageInfo& si = storageInfo_;
    const uint64_t total = si.app + si.packs + si.saves + si.cache;

    Rect card{area.x + pad, y, area.w - 2 * pad, ctx_.px(236)};
    r.fillRoundRect(card, ctx_.px(20), th.surface);
    r.text("Total used by MultiGame Hub", card.x + ctx_.px(16), card.y + ctx_.px(16), ctx_.px(13), th.textDim);
    r.text(formatBytes(total), card.x + ctx_.px(16), card.y + ctx_.px(36), ctx_.px(28), th.text, FontId::Bold);
    // Stacked usage bar.
    Rect bar{card.x + ctx_.px(16), card.y + ctx_.px(84), card.w - ctx_.px(32), ctx_.px(12)};
    r.fillRoundRect(bar, ctx_.px(6), th.surface2);
    const uint64_t parts[4] = {si.app, si.packs, si.saves, si.cache};
    const Color cols[4] = {th.accent, th.accent2, th.warning, th.danger};
    const char* names[4] = {"App (APK)", "Game content", "Saved data & settings", "Cache"};
    float x = bar.x;
    for (int i = 0; i < 4; ++i) {
        if (!total || !parts[i]) continue;
        float w = std::max(ctx_.px(3), bar.w * static_cast<float>(parts[i]) / total);
        r.fillRect({x, bar.y, std::min(w, bar.right() - x), bar.h}, cols[i]);
        x += w;
    }
    float ly = bar.bottom() + ctx_.px(14);
    for (int i = 0; i < 4; ++i) {
        r.fillCircle(card.x + ctx_.px(22), ly + ctx_.px(8), ctx_.px(5), cols[i]);
        r.text(names[i], card.x + ctx_.px(36), ly, ctx_.px(13), th.text);
        r.text(formatBytes(parts[i]), card.right() - ctx_.px(16), ly, ctx_.px(13), th.textDim, FontId::Bold, Align::Right);
        ly += ctx_.px(24);
    }
    r.text(si.free >= 0 ? "Free space on device: " + formatBytes(static_cast<uint64_t>(si.free)) : "Free space: unknown",
           card.x + ctx_.px(16), ly + ctx_.px(4), ctx_.px(12), th.textFaint);
    y = card.bottom() + ctx_.px(12);

    Rect cacheB{area.x + pad, y, area.w - 2 * pad, ctx_.px(46)};
    if (ui.button(cacheB, "Clear cache (" + formatBytes(si.cache) + ")", ButtonStyle::Secondary, Icon::Trash,
                  si.cache > 0 && !ctx_.updates.busy())) {
        ctx_.storage.clearCache();
        refreshStorageInfo();
        ctx_.toast("Cache cleared");
    }
    y = cacheB.bottom() + ctx_.px(20);

    y = sectionTitle(y, area, "Games");
    for (const auto& g : gameRegistry()) {
        Rect row{area.x + pad, y, area.w - 2 * pad, ctx_.px(76)};
        r.fillRoundRect(row, ctx_.px(18), th.surface);
        Rect tile{row.x + ctx_.px(10), row.y + ctx_.px(10), ctx_.px(56), ctx_.px(56)};
        gameTile(g, tile, ctx_.px(12));
        const float tx = tile.right() + ctx_.px(12);
        uint64_t pack = ctx_.storage.packSize(g.id);
        uint64_t saves = ctx_.storage.saveDataSize(g.id);
        r.text(g.name, tx, row.y + ctx_.px(14), ctx_.px(15), th.text, FontId::Bold);
        r.text("Content " + formatBytes(pack) + " - Data " + formatBytes(saves), tx, row.y + ctx_.px(38), ctx_.px(12), th.textDim);
        if (ctx_.storage.hasPack(g.id)) {
            if (ui.button({row.right() - ctx_.px(100), row.cy() - ctx_.px(18), ctx_.px(88), ctx_.px(36)}, "Uninstall",
                          ButtonStyle::Secondary)) {
                std::string id = g.id, name = g.name;
                ask("Uninstall content?", "Remove the downloaded " + name + " content pack (" + formatBytes(pack) +
                                              ")? The built-in game remains playable.",
                    "Uninstall", true, [this, id] {
                        ctx_.storage.uninstallPack(id);
                        refreshStorageInfo();
                        ctx_.toast("Content uninstalled");
                    });
            }
        } else {
            ui.pill({row.right() - ctx_.px(88), row.cy() - ctx_.px(12), ctx_.px(76), ctx_.px(24)}, "Built-in",
                    th.surface2, th.textDim);
        }
        y += row.h + ctx_.px(12);
    }
    r.text("Built-in games cannot be removed individually: their code", area.x + pad, y + ctx_.px(4), ctx_.px(11.5f), th.textFaint);
    r.text("is part of the app and takes only a few kilobytes each.", area.x + pad, y + ctx_.px(21), ctx_.px(11.5f), th.textFaint);
    return y + ctx_.px(48);
}

// ====================================================================== About

float Hub::aboutPage(float y, const Rect& area) {
    auto& r = ctx_.r;
    auto& ui = ctx_.ui;
    const Theme& th = ctx_.theme();
    const float pad = ctx_.px(18);
    y = pageHeader(y, area, "About", true);
    const AppInfo info = ctx_.platform.appInfo();
    drawLogo(area.cx() - ctx_.px(36), y + ctx_.px(6), ctx_.px(72));
    y += ctx_.px(92);
    r.text("MultiGame Hub", area.cx(), y, ctx_.px(22), th.text, FontId::Bold, Align::Center);
    y += ctx_.px(30);
    r.text("Version " + info.versionName + " (" + std::to_string(info.versionCode) + ") - " + info.platformName, area.cx(), y,
           ctx_.px(13), th.textDim, FontId::Regular, Align::Center);
    y += ctx_.px(34);
    const std::string about =
        "A lightweight collection of small games written in C++20 with SDL3. Every graphic and sound is generated "
        "in code, so the whole app stays tiny. Updates are delivered through GitHub Releases and every download "
        "is verified with SHA-256 before use.";
    for (auto& l : r.wrap(about, area.w - 2 * pad, ctx_.px(14))) {
        r.text(l, area.x + pad, y, ctx_.px(14), th.textDim);
        y += ctx_.px(21);
    }
    y += ctx_.px(12);
    y = sectionTitle(y, area, "Open-source components");
    const char* libs[][2] = {{"SDL 3", "zlib licence"},
                             {"stb_truetype", "Public domain / MIT"},
                             {"miniz", "MIT licence"},
                             {"Inter font", "SIL Open Font Licence 1.1"},
                             {"DejaVu Sans (chess glyphs)", "Bitstream Vera licence"}};
    Rect box{area.x + pad, y, area.w - 2 * pad, ctx_.px(40) * 5 + ctx_.px(8)};
    r.fillRoundRect(box, ctx_.px(18), th.surface);
    float ly = box.y + ctx_.px(12);
    for (auto& l : libs) {
        r.text(l[0], box.x + ctx_.px(16), ly, ctx_.px(14), th.text, FontId::Bold);
        r.text(l[1], box.right() - ctx_.px(16), ly + ctx_.px(1), ctx_.px(12.5f), th.textDim, FontId::Regular, Align::Right);
        ly += ctx_.px(40);
    }
    y = box.bottom() + ctx_.px(14);
    if (ui.button({area.x + pad, y, area.w - 2 * pad, ctx_.px(46)}, "Project on GitHub", ButtonStyle::Secondary, Icon::Info))
        ctx_.platform.openUrl("https://github.com/" MGH_UPDATE_REPO_STR);
    return y + ctx_.px(60);
}

}  // namespace mgh
