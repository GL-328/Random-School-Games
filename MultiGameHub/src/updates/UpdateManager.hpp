// Update state machine: fetches the release manifest from GitHub Releases,
// detects app and game-pack updates, downloads with progress/cancel,
// verifies size + SHA-256, safely installs content packs and hands verified
// APKs to the platform installer. Nothing downloaded is ever executed.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "Manifest.hpp"
#include "platform/Platform.hpp"
#include "storage/StorageManager.hpp"

namespace mgh {

class UpdateManager {
public:
    enum class Phase {
        Idle,
        CheckingManifest,
        DownloadingApp,
        VerifyingApp,
        ReadyToInstall,
        Installing,
        DownloadingPack,
        Error,
    };

    UpdateManager(INetwork& net, IAppInstaller& installer, StorageManager& storage, int currentVersionCode,
                  std::string manifestUrl);

    // Call every frame while work is in progress (cheap when idle).
    void update();
    bool busy() const;

    void checkForUpdates();
    bool hasManifest() const { return manifest_.has_value(); }
    const Manifest* manifest() const { return manifest_ ? &*manifest_ : nullptr; }
    bool appUpdateAvailable() const;
    // Pack is available but not installed, or installed with an older version.
    bool packUpdateAvailable(const std::string& gameId) const;
    bool packInstallable(const std::string& gameId) const;
    int pendingGameUpdates() const;

    void downloadApp();
    void installApp();
    void downloadPack(const std::string& gameId);
    void cancel();
    void clearError();

    Phase phase() const { return phase_; }
    const std::string& error() const { return error_; }
    const std::string& statusText() const { return status_; }
    // 0..1, or -1 when indeterminate.
    float progress() const;
    uint64_t bytesReceived() const { return received_; }
    int64_t bytesTotal() const { return total_; }
    const std::string& activePack() const { return activePack_; }
    int64_t lastCheckTime() const { return lastCheckTime_; }
    bool lastCheckSucceeded() const { return lastCheckOk_; }

    // Invoked when a check completes (success or failure) — used for notifications.
    std::function<void(bool ok)> onCheckFinished;
    std::function<void(const std::string& gameId)> onPackInstalled;

    // Clock injection for tests.
    std::function<int64_t()> now;

private:
    void fail(const std::string& msg);
    void finishManifest(const std::string& path);
    void finishAppDownload(const std::string& path);
    void finishPackDownload(const std::string& path);
    bool startDownload(const std::string& url, const std::string& dest, uint64_t maxBytes, Phase phase);

    INetwork& net_;
    IAppInstaller& installer_;
    StorageManager& storage_;
    int currentVersionCode_;
    std::string manifestUrl_;

    Phase phase_ = Phase::Idle;
    int handle_ = 0;
    std::string dest_;
    std::string error_;
    std::string status_;
    std::string activePack_;
    uint64_t received_ = 0;
    int64_t total_ = -1;
    std::optional<Manifest> manifest_;
    std::string verifiedApk_;
    int64_t lastCheckTime_ = 0;
    bool lastCheckOk_ = false;
};

}  // namespace mgh
