#include "UpdateManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "PackExtractor.hpp"
#include "Sha256.hpp"
#include "core/FileUtil.hpp"

namespace fs = std::filesystem;

namespace mgh {

UpdateManager::UpdateManager(INetwork& net, IAppInstaller& installer, StorageManager& storage,
                             int currentVersionCode, std::string manifestUrl)
    : net_(net),
      installer_(installer),
      storage_(storage),
      currentVersionCode_(currentVersionCode),
      manifestUrl_(std::move(manifestUrl)) {
    now = [] {
        return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
    };
}

bool UpdateManager::busy() const {
    return phase_ == Phase::CheckingManifest || phase_ == Phase::DownloadingApp || phase_ == Phase::VerifyingApp ||
           phase_ == Phase::DownloadingPack || phase_ == Phase::Installing;
}

float UpdateManager::progress() const {
    if (total_ <= 0) return -1.0f;
    return std::min(1.0f, static_cast<float>(received_) / static_cast<float>(total_));
}

void UpdateManager::fail(const std::string& msg) {
    if (handle_) {
        net_.release(handle_);
        handle_ = 0;
    }
    if (!dest_.empty()) {
        std::error_code ec;
        fs::remove(dest_, ec);
        dest_.clear();
    }
    const bool wasCheck = phase_ == Phase::CheckingManifest;
    phase_ = Phase::Error;
    error_ = msg;
    status_.clear();
    activePack_.clear();
    if (wasCheck) {
        lastCheckOk_ = false;
        if (onCheckFinished) onCheckFinished(false);
    }
}

void UpdateManager::clearError() {
    if (phase_ == Phase::Error) phase_ = verifiedApk_.empty() ? Phase::Idle : Phase::ReadyToInstall;
    error_.clear();
}

bool UpdateManager::startDownload(const std::string& url, const std::string& dest, uint64_t maxBytes, Phase phase) {
    if (url.rfind("https://", 0) != 0) {
        fail("Refusing non-HTTPS download");
        return false;
    }
    ensureDir(storage_.downloadsDir());
    std::error_code ec;
    fs::remove(dest, ec);
    phase_ = phase;
    dest_ = dest;
    received_ = 0;
    total_ = -1;
    error_.clear();
    handle_ = net_.startDownload(url, dest, maxBytes);
    if (!handle_) {
        fail("Could not start download");
        return false;
    }
    return true;
}

void UpdateManager::checkForUpdates() {
    if (busy()) return;
    status_ = "Checking for updates...";
    lastCheckTime_ = now();
    startDownload(manifestUrl_, joinPath(storage_.downloadsDir(), "manifest.json.part"), limits::kMaxManifestBytes,
                  Phase::CheckingManifest);
}

bool UpdateManager::appUpdateAvailable() const {
    return manifest_ && manifest_->app.versionCode > currentVersionCode_;
}

bool UpdateManager::packInstallable(const std::string& id) const {
    if (!manifest_) return false;
    const GameEntry* g = manifest_->findGame(id);
    return g && g->pack && g->minAppVersionCode <= currentVersionCode_;
}

bool UpdateManager::packUpdateAvailable(const std::string& id) const {
    if (!packInstallable(id)) return false;
    auto installed = storage_.packVersion(id);
    return installed && *installed < manifest_->findGame(id)->version;
}

int UpdateManager::pendingGameUpdates() const {
    if (!manifest_) return 0;
    int n = 0;
    for (const auto& g : manifest_->games)
        if (packUpdateAvailable(g.id)) ++n;
    return n;
}

void UpdateManager::downloadApp() {
    if (busy() || !appUpdateAvailable()) return;
    verifiedApk_.clear();
    status_ = "Downloading version " + manifest_->app.version.str() + "...";
    startDownload(manifest_->app.apk.url, joinPath(storage_.downloadsDir(), "update.apk.part"),
                  manifest_->app.apk.size, Phase::DownloadingApp);
}

void UpdateManager::downloadPack(const std::string& id) {
    if (busy() || !packInstallable(id)) return;
    const GameEntry* g = manifest_->findGame(id);
    activePack_ = id;
    status_ = "Downloading " + g->name + " theme...";
    startDownload(g->pack->url, joinPath(storage_.downloadsDir(), id + ".zip.part"), g->pack->size,
                  Phase::DownloadingPack);
}

void UpdateManager::cancel() {
    if (!handle_) return;
    net_.cancel(handle_);
    net_.release(handle_);
    handle_ = 0;
    std::error_code ec;
    if (!dest_.empty()) fs::remove(dest_, ec);
    dest_.clear();
    const bool wasCheck = phase_ == Phase::CheckingManifest;
    phase_ = Phase::Idle;
    status_ = "Download cancelled";
    activePack_.clear();
    if (wasCheck && onCheckFinished) onCheckFinished(false);
}

void UpdateManager::update() {
    if (phase_ == Phase::Installing) {
        InstallStatus st = installer_.status();
        if (st.state == InstallStatus::State::Success) {
            phase_ = Phase::Idle;
            status_ = "Update installed";
        } else if (st.state == InstallStatus::State::Failed) {
            phase_ = Phase::ReadyToInstall;
            error_ = st.message.empty() ? "Installation failed or was cancelled" : st.message;
            status_.clear();
        } else if (st.state == InstallStatus::State::WaitingForUser) {
            status_ = "Tap Update in Android's installer to confirm";
        }
        return;
    }
    if (!handle_) return;
    DownloadStatus st = net_.poll(handle_);
    received_ = st.received;
    total_ = st.total;
    switch (st.state) {
        case DownloadStatus::State::Running: return;
        case DownloadStatus::State::Cancelled:
            cancel();
            return;
        case DownloadStatus::State::Failed:
        case DownloadStatus::State::Invalid:
            fail(st.error.empty() ? "Download failed. Check your internet connection." : "Download failed: " + st.error);
            return;
        case DownloadStatus::State::Done: break;
    }
    net_.release(handle_);
    handle_ = 0;
    const std::string path = dest_;
    switch (phase_) {
        case Phase::CheckingManifest: finishManifest(path); break;
        case Phase::DownloadingApp: finishAppDownload(path); break;
        case Phase::DownloadingPack: finishPackDownload(path); break;
        default: break;
    }
}

void UpdateManager::finishManifest(const std::string& path) {
    auto text = readTextFile(path, limits::kMaxManifestBytes);
    std::error_code ec;
    fs::remove(path, ec);
    dest_.clear();
    if (!text) { fail("Could not read update information"); return; }
    std::string err;
    auto m = parseManifest(*text, err);
    if (!m) { fail("Invalid update manifest: " + err); return; }
    manifest_ = std::move(*m);
    phase_ = Phase::Idle;
    lastCheckOk_ = true;
    status_ = appUpdateAvailable() ? "Version " + manifest_->app.version.str() + " is available"
                                   : "You're up to date";
    if (onCheckFinished) onCheckFinished(true);
}

void UpdateManager::finishAppDownload(const std::string& path) {
    phase_ = Phase::VerifyingApp;
    status_ = "Verifying download...";
    const FileRef& ref = manifest_->app.apk;
    if (fileSize(path) != ref.size) { fail("Downloaded file has the wrong size"); return; }
    auto hash = Sha256::fileHex(path);
    if (!hash || !Sha256::equalHex(*hash, ref.sha256)) { fail("Integrity check failed (SHA-256 mismatch)"); return; }
    const std::string finalPath = joinPath(storage_.downloadsDir(), "update.apk");
    std::error_code ec;
    fs::remove(finalPath, ec);
    fs::rename(path, finalPath, ec);
    dest_.clear();
    if (ec) { fail("Could not store the downloaded update"); return; }
    std::string err;
    if (installer_.supported() && !installer_.verifyPackage(finalPath, err)) {
        fs::remove(finalPath, ec);
        fail("Update rejected: " + err);
        return;
    }
    verifiedApk_ = finalPath;
    phase_ = Phase::ReadyToInstall;
    status_ = "Download verified. Ready to install.";
}

void UpdateManager::installApp() {
    if (phase_ != Phase::ReadyToInstall || verifiedApk_.empty()) return;
    error_.clear();
    if (!installer_.supported()) {
        error_ = "Self-update is not supported on this platform";
        return;
    }
    if (!installer_.canRequestInstall()) {
        error_ = "Allow \"Install unknown apps\" for Got Bored, then tap Install again.";
        installer_.openInstallPermissionSettings();
        return;
    }
    // Re-verify right before install in case the cache file was tampered with.
    auto hash = Sha256::fileHex(verifiedApk_);
    if (!hash || !Sha256::equalHex(*hash, manifest_->app.apk.sha256)) {
        verifiedApk_.clear();
        fail("Integrity check failed. Please download again.");
        return;
    }
    std::string err;
    if (!installer_.install(verifiedApk_, err)) {
        error_ = err.empty() ? "Could not start the installer" : err;
        return;
    }
    phase_ = Phase::Installing;
    status_ = "Opening the Android installer...";
}

void UpdateManager::finishPackDownload(const std::string& path) {
    const std::string id = activePack_;
    const GameEntry* g = manifest_ ? manifest_->findGame(id) : nullptr;
    std::error_code ec;
    if (!g || !g->pack) { fail("Game is no longer in the manifest"); return; }
    if (fileSize(path) != g->pack->size) { fail("Downloaded pack has the wrong size"); return; }
    auto hash = Sha256::fileHex(path);
    if (!hash || !Sha256::equalHex(*hash, g->pack->sha256)) { fail("Integrity check failed (SHA-256 mismatch)"); return; }

    const std::string staging = storage_.packDir(id) + ".staging";
    fs::remove_all(staging, ec);
    ExtractResult r = extractPack(path, staging);
    fs::remove(path, ec);
    dest_.clear();
    if (!r.ok) { fail("Pack rejected: " + r.error); return; }
    std::string err;
    if (!storage_.commitPack(id, g->version, staging, err)) {
        fs::remove_all(staging, ec);
        fail("Install failed: " + err);
        return;
    }
    phase_ = Phase::Idle;
    status_ = g->name + " theme installed (" + formatBytes(r.bytes) + ")";
    activePack_.clear();
    if (onPackInstalled) onPackInstalled(id);
}

}  // namespace mgh
