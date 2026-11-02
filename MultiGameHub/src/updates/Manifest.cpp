#include "Manifest.hpp"

#include <cstdio>

#include "Sha256.hpp"
#include "core/Json.hpp"

namespace mgh {

std::optional<Version> Version::parse(const std::string& s) {
    Version v;
    int parts[3] = {0, 0, 0};
    int idx = 0;
    size_t i = 0;
    std::string str = s;
    if (!str.empty() && (str[0] == 'v' || str[0] == 'V')) str = str.substr(1);
    if (str.empty()) return std::nullopt;
    while (i < str.size()) {
        if (idx > 2) return std::nullopt;
        size_t start = i;
        long n = 0;
        while (i < str.size() && str[i] >= '0' && str[i] <= '9') {
            n = n * 10 + (str[i] - '0');
            if (n > 99999) return std::nullopt;
            ++i;
        }
        if (i == start) return std::nullopt;
        parts[idx++] = static_cast<int>(n);
        if (i < str.size()) {
            if (str[i] != '.') return std::nullopt;
            ++i;
            if (i == str.size()) return std::nullopt;
        }
    }
    if (parts[1] > 99 || parts[2] > 99 || parts[0] > 20000) return std::nullopt;  // keeps code() unique
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    return v;
}

std::string Version::str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

const GameEntry* Manifest::findGame(const std::string& id) const {
    for (const auto& g : games)
        if (g.id == id) return &g;
    return nullptr;
}

bool isTrustedDownloadUrl(const std::string& url) {
    static const char* kPrefixes[] = {
        "https://github.com/",
        "https://objects.githubusercontent.com/",
        "https://release-assets.githubusercontent.com/",
        "https://raw.githubusercontent.com/",
    };
    if (url.size() > 2048) return false;
    for (char c : url) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F || c == '\\') return false;
    }
    for (const char* p : kPrefixes) {
        std::string pre(p);
        if (url.compare(0, pre.size(), pre) == 0 && url.size() > pre.size()) return true;
    }
    return false;
}

bool isValidGameId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

namespace {

bool readFileRef(const Json& j, uint64_t maxSize, FileRef& out, std::string& error, const std::string& what) {
    if (!j.isObject()) { error = what + ": missing"; return false; }
    out.url = j["url"].asString();
    out.sha256 = j["sha256"].asString();
    double size = j["size"].asNumber(-1);
    if (!isTrustedDownloadUrl(out.url)) { error = what + ": URL is not an HTTPS GitHub release URL"; return false; }
    if (!Sha256::isValidHex(out.sha256)) { error = what + ": invalid sha256"; return false; }
    if (!(size > 0) || size > static_cast<double>(maxSize)) { error = what + ": invalid size"; return false; }
    out.size = static_cast<uint64_t>(size);
    return true;
}

bool readText(const Json& j, size_t maxLen, std::string& out) {
    if (j.isNull()) { out.clear(); return true; }
    if (!j.isString() || j.asString().size() > maxLen) return false;
    out = j.asString();
    return true;
}

}  // namespace

std::optional<Manifest> parseManifest(const std::string& json, std::string& error) {
    if (json.size() > limits::kMaxManifestBytes) { error = "manifest too large"; return std::nullopt; }
    std::string perr;
    auto doc = Json::parse(json, &perr);
    if (!doc) { error = "manifest is not valid JSON (" + perr + ")"; return std::nullopt; }
    if (!doc->isObject()) { error = "manifest root must be an object"; return std::nullopt; }

    Manifest m;
    m.schema = static_cast<int>((*doc)["schema"].asInt(0));
    if (m.schema != 1) { error = "unsupported manifest schema"; return std::nullopt; }
    if (!readText((*doc)["publishedAt"], 64, m.publishedAt)) { error = "bad publishedAt"; return std::nullopt; }

    const Json& app = (*doc)["app"];
    if (!app.isObject()) { error = "missing app section"; return std::nullopt; }
    auto ver = Version::parse(app["version"].asString());
    if (!ver) { error = "app.version invalid"; return std::nullopt; }
    m.app.version = *ver;
    m.app.versionCode = static_cast<int>(app["versionCode"].asInt(0));
    if (m.app.versionCode <= 0) { error = "app.versionCode invalid"; return std::nullopt; }
    if (m.app.versionCode != ver->code()) { error = "app.versionCode does not match app.version"; return std::nullopt; }
    if (!readFileRef(app["apk"], limits::kMaxApkBytes, m.app.apk, error, "app.apk")) return std::nullopt;
    if (!readText(app["notes"], limits::kMaxNotesChars, m.app.notes)) { error = "app.notes invalid"; return std::nullopt; }
    m.app.minSdk = static_cast<int>(app["minSdk"].asInt(0));

    const Json& games = (*doc)["games"];
    if (!games.isNull()) {
        if (!games.isArray() || games.size() > limits::kMaxGames) { error = "games must be an array"; return std::nullopt; }
        for (const Json& g : games.elements()) {
            GameEntry e;
            e.id = g["id"].asString();
            if (!isValidGameId(e.id)) { error = "invalid game id"; return std::nullopt; }
            if (m.findGame(e.id)) { error = "duplicate game id " + e.id; return std::nullopt; }
            auto gv = Version::parse(g["version"].asString());
            if (!gv) { error = "game " + e.id + ": invalid version"; return std::nullopt; }
            e.version = *gv;
            if (!readText(g["name"], 64, e.name) || e.name.empty()) { error = "game " + e.id + ": invalid name"; return std::nullopt; }
            if (!readText(g["description"], 512, e.description) || !readText(g["notes"], 2000, e.notes)) {
                error = "game " + e.id + ": invalid text";
                return std::nullopt;
            }
            e.minAppVersionCode = static_cast<int>(g["minAppVersionCode"].asInt(0));
            if (g.has("pack") && !g["pack"].isNull()) {
                FileRef ref;
                if (!readFileRef(g["pack"], limits::kMaxPackBytes, ref, error, "game " + e.id + " pack")) return std::nullopt;
                e.pack = ref;
            }
            m.games.push_back(std::move(e));
        }
    }
    return m;
}

}  // namespace mgh
