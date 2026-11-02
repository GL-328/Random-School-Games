// Release manifest published as `manifest.json` on each GitHub Release.
// Everything downloaded by the app is described (URL, size, SHA-256) here,
// and the manifest is strictly validated before anything is trusted.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mgh {

struct Version {
    int major = 0, minor = 0, patch = 0;
    static std::optional<Version> parse(const std::string& s);
    std::string str() const;
    int code() const { return major * 10000 + minor * 100 + patch; }
    auto operator<=>(const Version&) const = default;
};

struct FileRef {
    std::string url;
    std::string sha256;
    uint64_t size = 0;
};

struct AppRelease {
    Version version;
    int versionCode = 0;
    FileRef apk;
    std::string notes;
    int minSdk = 0;
};

struct GameEntry {
    std::string id;
    std::string name;
    Version version;
    std::string description;
    std::string notes;
    int minAppVersionCode = 0;
    std::optional<FileRef> pack;  // optional downloadable content pack (data only)
};

struct Manifest {
    int schema = 1;
    std::string publishedAt;
    AppRelease app;
    std::vector<GameEntry> games;

    const GameEntry* findGame(const std::string& id) const;
};

namespace limits {
constexpr uint64_t kMaxApkBytes = 150ull * 1024 * 1024;
constexpr uint64_t kMaxPackBytes = 64ull * 1024 * 1024;
constexpr uint64_t kMaxManifestBytes = 256 * 1024;
constexpr size_t kMaxGames = 512;
constexpr size_t kMaxNotesChars = 8000;
}  // namespace limits

// Only HTTPS URLs on GitHub release hosts are accepted.
bool isTrustedDownloadUrl(const std::string& url);
// Game ids become directory names, so they are restricted to [a-z0-9_], 1-32 chars.
bool isValidGameId(const std::string& id);

std::optional<Manifest> parseManifest(const std::string& json, std::string& error);

}  // namespace mgh
