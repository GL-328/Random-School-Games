#include "StorageManager.hpp"

#include <filesystem>
#include <system_error>

#include "core/FileUtil.hpp"
#include "core/Json.hpp"

namespace fs = std::filesystem;

namespace mgh {

StorageManager::StorageManager(IStorage& storage) : storage_(storage) {
    ensureDir(packsDir());
    ensureDir(downloadsDir());
    loadIndex();
}

std::string StorageManager::packsDir() const { return joinPath(storage_.dataDir(), "packs"); }
std::string StorageManager::packDir(const std::string& id) const {
    return isValidGameId(id) ? joinPath(packsDir(), id) : std::string();
}
std::string StorageManager::savesDir(const std::string& id) const {
    return isValidGameId(id) ? joinPath(joinPath(storage_.dataDir(), "saves"), id) : std::string();
}
std::string StorageManager::downloadsDir() const { return joinPath(storage_.cacheDir(), "downloads"); }

void StorageManager::loadIndex() {
    index_.clear();
    auto text = readTextFile(joinPath(packsDir(), "index.json"), 256 * 1024);
    if (!text) return;
    auto j = Json::parse(*text);
    if (!j || !j->isObject()) return;
    for (const auto& [id, v] : j->items()) {
        if (!isValidGameId(id)) continue;
        auto ver = Version::parse(v["version"].asString());
        std::error_code ec;
        if (ver && fs::is_directory(packDir(id), ec)) index_[id] = *ver;
    }
}

bool StorageManager::saveIndex() const {
    Json j = Json::object();
    for (const auto& [id, v] : index_) {
        Json e = Json::object();
        e.set("version", Json::string(v.str()));
        j.set(id, e);
    }
    return writeFileAtomic(joinPath(packsDir(), "index.json"), j.dump(2));
}

bool StorageManager::hasPack(const std::string& id) const { return index_.count(id) > 0; }

std::optional<Version> StorageManager::packVersion(const std::string& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

uint64_t StorageManager::packSize(const std::string& id) const {
    auto d = packDir(id);
    return d.empty() ? 0 : directorySize(d);
}

uint64_t StorageManager::saveDataSize(const std::string& id) const {
    auto d = savesDir(id);
    return d.empty() ? 0 : directorySize(d);
}

bool StorageManager::commitPack(const std::string& id, const Version& v, const std::string& stagingDir,
                                std::string& error) {
    const std::string dest = packDir(id);
    if (dest.empty()) { error = "invalid game id"; return false; }
    std::error_code ec;
    const std::string old = dest + ".old";
    fs::remove_all(old, ec);
    if (fs::exists(dest, ec)) {
        fs::rename(dest, old, ec);
        if (ec) { error = "cannot replace existing pack"; return false; }
    }
    fs::rename(stagingDir, dest, ec);
    if (ec) {
        // Roll back to the previous version.
        std::error_code e2;
        if (fs::exists(old, e2)) fs::rename(old, dest, e2);
        error = "cannot move pack into place";
        return false;
    }
    fs::remove_all(old, ec);  // remove outdated assets
    index_[id] = v;
    if (!saveIndex()) { error = "cannot write pack index"; return false; }
    return true;
}

bool StorageManager::uninstallPack(const std::string& id) {
    const std::string dest = packDir(id);
    if (dest.empty()) return false;
    bool ok = removeTree(dest);
    index_.erase(id);
    saveIndex();
    return ok;
}

bool StorageManager::clearSaveData(const std::string& id) {
    const std::string d = savesDir(id);
    return !d.empty() && removeTree(d);
}

uint64_t StorageManager::cacheSize() const { return directorySize(storage_.cacheDir()); }

void StorageManager::clearCache() {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(storage_.cacheDir(), ec)) fs::remove_all(e.path(), ec);
    ensureDir(downloadsDir());
}

void StorageManager::cleanupStale() {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(packsDir(), ec)) {
        const std::string name = e.path().filename().string();
        bool stale = name.size() > 8 && (name.ends_with(".staging") || name.ends_with(".old"));
        if (e.is_directory(ec) && !isValidGameId(name) && stale) fs::remove_all(e.path(), ec);
    }
    for (auto& e : fs::directory_iterator(downloadsDir(), ec)) {
        // Partial downloads, and a finished update APK left over from the last
        // update (once installed it is no longer needed).
        if (e.path().extension() == ".part" || e.path().filename() == "update.apk") fs::remove(e.path(), ec);
    }
}

}  // namespace mgh
