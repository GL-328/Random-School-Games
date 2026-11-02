// Desktop (Linux/macOS) implementation used for development and automated
// testing. Networking uses the system `curl` binary (HTTPS only); self-update
// installation is not supported on desktop.
#include <SDL3/SDL.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/FileUtil.hpp"
#include "platform/Platform.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#define MGH_HAVE_POSIX_SPAWN 1
#endif

namespace fs = std::filesystem;

namespace mgh {

namespace {

class CurlNetwork : public INetwork {
public:
    ~CurlNetwork() override {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& [id, job] : jobs_) {
#ifdef MGH_HAVE_POSIX_SPAWN
            if (job->pid > 0 && job->state == DownloadStatus::State::Running) kill(job->pid, SIGTERM);
#endif
            if (job->thread.joinable()) job->thread.join();
        }
    }

    int startDownload(const std::string& url, const std::string& dest, uint64_t maxBytes) override {
        if (url.rfind("https://", 0) != 0) return 0;
#ifdef MGH_HAVE_POSIX_SPAWN
        auto job = std::make_unique<Job>();
        job->dest = dest;
        std::string maxStr = std::to_string(maxBytes);
        std::vector<std::string> args = {"curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https",
                                         "--connect-timeout", "15", "--max-filesize", maxStr,
                                         "-A", "MultiGameHub-desktop", "-o", dest, url};
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        if (posix_spawnp(&pid, "curl", nullptr, nullptr, argv.data(), environ) != 0) return 0;
        job->pid = pid;
        job->state = DownloadStatus::State::Running;
        Job* raw = job.get();
        job->thread = std::thread([raw] {
            int status = 0;
            waitpid(raw->pid, &status, 0);
            if (raw->cancelled) raw->state = DownloadStatus::State::Cancelled;
            else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) raw->state = DownloadStatus::State::Done;
            else {
                raw->error = "curl exit code " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                raw->state = DownloadStatus::State::Failed;
            }
        });
        std::lock_guard<std::mutex> lock(mu_);
        int id = next_++;
        jobs_[id] = std::move(job);
        return id;
#else
        (void)dest;
        (void)maxBytes;
        return 0;
#endif
    }

    DownloadStatus poll(int handle) override {
        std::lock_guard<std::mutex> lock(mu_);
        DownloadStatus st;
        auto it = jobs_.find(handle);
        if (it == jobs_.end()) return st;
        Job& j = *it->second;
        st.state = j.state.load();
        st.received = fileSize(j.dest);
        st.total = -1;
        if (st.state == DownloadStatus::State::Failed) st.error = j.error;
        return st;
    }

    void cancel(int handle) override {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = jobs_.find(handle);
        if (it == jobs_.end()) return;
        it->second->cancelled = true;
#ifdef MGH_HAVE_POSIX_SPAWN
        if (it->second->state == DownloadStatus::State::Running) kill(it->second->pid, SIGTERM);
#endif
    }

    void release(int handle) override {
        std::unique_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = jobs_.find(handle);
            if (it == jobs_.end()) return;
            job = std::move(it->second);
            jobs_.erase(it);
        }
#ifdef MGH_HAVE_POSIX_SPAWN
        if (job->state == DownloadStatus::State::Running) kill(job->pid, SIGTERM);
#endif
        if (job->thread.joinable()) job->thread.join();
    }

private:
    struct Job {
        std::string dest;
        std::atomic<DownloadStatus::State> state{DownloadStatus::State::Invalid};
        std::atomic<bool> cancelled{false};
        std::string error;
        int pid = 0;
        std::thread thread;
    };
    std::mutex mu_;
    std::map<int, std::unique_ptr<Job>> jobs_;
    int next_ = 1;
};

class NoInstaller : public IAppInstaller {
public:
    bool supported() const override { return false; }
    bool canRequestInstall() override { return false; }
    void openInstallPermissionSettings() override {}
    bool verifyPackage(const std::string&, std::string& e) override {
        e = "not supported on desktop";
        return false;
    }
    bool install(const std::string&, std::string& e) override {
        e = "Self-update is only available in the Android app";
        return false;
    }
    InstallStatus status() override { return {}; }
};

class NoHaptics : public IHaptics {
public:
    void vibrate(int) override {}
};

class DesktopStorage : public IStorage {
public:
    DesktopStorage() {
        if (const char* env = SDL_getenv("MGH_DATA_DIR")) {
            root_ = env;
        } else {
            char* pref = SDL_GetPrefPath("GL328", "MultiGameHub");
            root_ = pref ? pref : "./mgh_data/";
            SDL_free(pref);
        }
        if (!root_.empty() && root_.back() == '/') root_.pop_back();
    }
    std::string dataDir() override { return root_ + "/data"; }
    std::string cacheDir() override { return root_ + "/cache"; }
    uint64_t appInstallSize() override {
        const char* base = SDL_GetBasePath();
        if (!base) return 0;
        std::error_code ec;
        uint64_t total = directorySize(std::string(base) + "assets");
        for (auto& e : fs::directory_iterator(base, ec))
            if (e.is_regular_file(ec) && e.path().filename().string().rfind("multigamehub", 0) == 0) total += e.file_size(ec);
        return total;
    }
    int64_t freeSpace() override {
        std::error_code ec;
        auto sp = fs::space(root_, ec);
        return ec ? -1 : static_cast<int64_t>(sp.available);
    }

private:
    std::string root_;
};

class DesktopPlatform : public IPlatform {
public:
    AppInfo appInfo() override { return {MGH_VERSION_STR, MGH_VERSION_CODE, "Desktop"}; }
    INetwork& network() override { return net_; }
    IAppInstaller& installer() override { return installer_; }
    IHaptics& haptics() override { return haptics_; }
    IStorage& storage() override { return storage_; }
    void openUrl(const std::string& url) override { SDL_OpenURL(url.c_str()); }
    void minimize() override {
        SDL_Event e{};
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }

private:
    CurlNetwork net_;
    NoInstaller installer_;
    NoHaptics haptics_;
    DesktopStorage storage_;
};

}  // namespace

std::unique_ptr<IPlatform> createPlatform() { return std::make_unique<DesktopPlatform>(); }

}  // namespace mgh
