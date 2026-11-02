// Tracks downloaded game content packs and app storage usage.
// Layout under IStorage::dataDir():
//   settings.cfg            persistent settings
//   packs/<gameId>/...      extracted content packs (data only)
//   packs/index.json        installed pack versions
//   saves/<gameId>/...      per-game save data
// IStorage::cacheDir() holds temporary downloads and is safe to purge.
#pragma once

#include <map>
#include <string>

#include "platform/Platform.hpp"
#include "updates/Manifest.hpp"

namespace mgh {

class StorageManager {
public:
    explicit StorageManager(IStorage& storage);

    std::string packsDir() const;
    std::string packDir(const std::string& gameId) const;
    std::string savesDir(const std::string& gameId) const;
    std::string downloadsDir() const;

    bool hasPack(const std::string& gameId) const;
    std::optional<Version> packVersion(const std::string& gameId) const;
    uint64_t packSize(const std::string& gameId) const;
    uint64_t saveDataSize(const std::string& gameId) const;

    // Atomically replaces the installed pack with the extracted directory `stagingDir`.
    bool commitPack(const std::string& gameId, const Version& v, const std::string& stagingDir, std::string& error);
    bool uninstallPack(const std::string& gameId);
    bool clearSaveData(const std::string& gameId);

    uint64_t cacheSize() const;
    void clearCache();
    // Removes leftovers from interrupted installs (staging dirs, partial downloads).
    void cleanupStale();

    IStorage& storage() { return storage_; }

private:
    void loadIndex();
    bool saveIndex() const;

    IStorage& storage_;
    std::map<std::string, Version> index_;
};

}  // namespace mgh
