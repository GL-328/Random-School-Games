// Platform abstraction. Shared C++ code only talks to these interfaces;
// each platform (android/, desktop/) provides an implementation. Porting to
// Windows/macOS/iOS means adding another implementation of this file.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace mgh {

// ---- Networking -----------------------------------------------------------
struct DownloadStatus {
    enum class State { Invalid, Running, Done, Failed, Cancelled };
    State state = State::Invalid;
    uint64_t received = 0;
    int64_t total = -1;  // -1 when the server did not send a length
    std::string error;
};

class INetwork {
public:
    virtual ~INetwork() = default;
    // Starts an asynchronous HTTPS GET into `destPath`. Returns a handle (> 0) or 0 on failure.
    virtual int startDownload(const std::string& url, const std::string& destPath, uint64_t maxBytes) = 0;
    virtual DownloadStatus poll(int handle) = 0;
    virtual void cancel(int handle) = 0;
    // Frees the handle after it reached a final state.
    virtual void release(int handle) = 0;
};

// ---- Application package installation (self update) ------------------------
struct InstallStatus {
    enum class State { Idle, Pending, WaitingForUser, Success, Failed };
    State state = State::Idle;
    std::string message;
};

class IAppInstaller {
public:
    virtual ~IAppInstaller() = default;
    virtual bool supported() const = 0;
    // Android 8+: the user must allow "Install unknown apps" for this app.
    virtual bool canRequestInstall() = 0;
    virtual void openInstallPermissionSettings() = 0;
    // Checks package name + signing certificate of the APK match this app.
    virtual bool verifyPackage(const std::string& apkPath, std::string& error) = 0;
    // Hands the APK to the system installer. The system shows a confirmation dialog.
    virtual bool install(const std::string& apkPath, std::string& error) = 0;
    virtual InstallStatus status() = 0;
};

// ---- Haptics ----------------------------------------------------------------
class IHaptics {
public:
    virtual ~IHaptics() = default;
    virtual void vibrate(int milliseconds) = 0;
};

// ---- Storage / file system --------------------------------------------------
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual std::string dataDir() = 0;    // persistent, app-private
    virtual std::string cacheDir() = 0;   // may be purged by the OS
    virtual uint64_t appInstallSize() = 0;  // size of the installed package (APK / executable)
    virtual int64_t freeSpace() = 0;      // bytes free on the data volume, -1 if unknown
};

// ---- Aggregate --------------------------------------------------------------
struct AppInfo {
    std::string versionName;
    int versionCode = 0;
    std::string platformName;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual AppInfo appInfo() = 0;
    virtual INetwork& network() = 0;
    virtual IAppInstaller& installer() = 0;
    virtual IHaptics& haptics() = 0;
    virtual IStorage& storage() = 0;
    virtual void openUrl(const std::string& url) = 0;
    // Sends the app to the background (Android back at the root screen).
    virtual void minimize() = 0;
};

// Implemented once per platform.
std::unique_ptr<IPlatform> createPlatform();

}  // namespace mgh
