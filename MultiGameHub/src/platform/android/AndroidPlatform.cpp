// Android implementation of the platform interfaces. Everything that needs the
// Android framework (HTTPS, package installer, vibrator) is delegated over JNI
// to the small Java class com.gl328.multigamehub.PlatformBridge.
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>

#include <filesystem>
#include <string>

#include "core/FileUtil.hpp"
#include "platform/Platform.hpp"

namespace mgh {

namespace {

constexpr const char* kBridgeClass = "com/gl328/multigamehub/PlatformBridge";

// Small RAII helpers around the JNI environment of the SDL main thread.
JNIEnv* env() { return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv()); }

class LocalActivity {
public:
    LocalActivity() : obj_(static_cast<jobject>(SDL_GetAndroidActivity())) {}
    ~LocalActivity() {
        if (obj_) env()->DeleteLocalRef(obj_);
    }
    jobject get() const { return obj_; }

private:
    jobject obj_;
};

jclass bridge() {
    static jclass cls = nullptr;
    if (!cls) {
        JNIEnv* e = env();
        jclass local = e->FindClass(kBridgeClass);
        if (!local) {
            e->ExceptionClear();
            SDL_Log("PlatformBridge class not found");
            return nullptr;
        }
        cls = static_cast<jclass>(e->NewGlobalRef(local));
        e->DeleteLocalRef(local);
    }
    return cls;
}

jmethodID method(const char* name, const char* sig) {
    jclass c = bridge();
    if (!c) return nullptr;
    jmethodID m = env()->GetStaticMethodID(c, name, sig);
    if (!m) {
        env()->ExceptionClear();
        SDL_Log("JNI method %s%s missing", name, sig);
    }
    return m;
}

bool checkException() {
    JNIEnv* e = env();
    if (e->ExceptionCheck()) {
        e->ExceptionDescribe();
        e->ExceptionClear();
        return true;
    }
    return false;
}

std::string toStd(jstring s) {
    if (!s) return {};
    JNIEnv* e = env();
    const char* c = e->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) e->ReleaseStringUTFChars(s, c);
    e->DeleteLocalRef(s);
    return r;
}

class LocalString {
public:
    explicit LocalString(const std::string& s) : s_(env()->NewStringUTF(s.c_str())) {}
    ~LocalString() {
        if (s_) env()->DeleteLocalRef(s_);
    }
    jstring get() const { return s_; }

private:
    jstring s_;
};

// ---------------------------------------------------------------- network

class AndroidNetwork : public INetwork {
public:
    int startDownload(const std::string& url, const std::string& dest, uint64_t maxBytes) override {
        static jmethodID m = method("startDownload", "(Ljava/lang/String;Ljava/lang/String;J)I");
        if (!m) return 0;
        LocalString u(url), d(dest);
        jint id = env()->CallStaticIntMethod(bridge(), m, u.get(), d.get(), static_cast<jlong>(maxBytes));
        if (checkException()) return 0;
        return id;
    }
    DownloadStatus poll(int handle) override {
        static jmethodID m = method("pollDownload", "(I)[J");
        static jmethodID me = method("downloadError", "(I)Ljava/lang/String;");
        DownloadStatus st;
        if (!m) return st;
        auto arr = static_cast<jlongArray>(env()->CallStaticObjectMethod(bridge(), m, handle));
        if (checkException() || !arr) return st;
        jlong v[3] = {0, 0, -1};
        env()->GetLongArrayRegion(arr, 0, 3, v);
        env()->DeleteLocalRef(arr);
        switch (v[0]) {
            case 1: st.state = DownloadStatus::State::Running; break;
            case 2: st.state = DownloadStatus::State::Done; break;
            case 3: st.state = DownloadStatus::State::Failed; break;
            case 4: st.state = DownloadStatus::State::Cancelled; break;
            default: st.state = DownloadStatus::State::Invalid; break;
        }
        st.received = static_cast<uint64_t>(v[1]);
        st.total = v[2];
        if (st.state == DownloadStatus::State::Failed && me) {
            st.error = toStd(static_cast<jstring>(env()->CallStaticObjectMethod(bridge(), me, handle)));
            checkException();
        }
        return st;
    }
    void cancel(int handle) override {
        static jmethodID m = method("cancelDownload", "(I)V");
        if (m) env()->CallStaticVoidMethod(bridge(), m, handle);
        checkException();
    }
    void release(int handle) override {
        static jmethodID m = method("releaseDownload", "(I)V");
        if (m) env()->CallStaticVoidMethod(bridge(), m, handle);
        checkException();
    }
};

// ---------------------------------------------------------------- installer

class AndroidInstaller : public IAppInstaller {
public:
    bool supported() const override { return true; }
    bool canRequestInstall() override {
        static jmethodID m = method("canRequestInstall", "(Landroid/content/Context;)Z");
        if (!m) return false;
        LocalActivity a;
        jboolean ok = env()->CallStaticBooleanMethod(bridge(), m, a.get());
        return !checkException() && ok;
    }
    void openInstallPermissionSettings() override {
        static jmethodID m = method("openInstallSettings", "(Landroid/content/Context;)V");
        if (!m) return;
        LocalActivity a;
        env()->CallStaticVoidMethod(bridge(), m, a.get());
        checkException();
    }
    bool verifyPackage(const std::string& apk, std::string& error) override {
        static jmethodID m = method("verifyApk", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");
        if (!m) { error = "verification unavailable"; return false; }
        LocalActivity a;
        LocalString p(apk);
        auto res = static_cast<jstring>(env()->CallStaticObjectMethod(bridge(), m, a.get(), p.get()));
        if (checkException()) { error = "verification failed"; return false; }
        if (!res) return true;  // null == verified
        error = toStd(res);
        return false;
    }
    bool install(const std::string& apk, std::string& error) override {
        static jmethodID m = method("installApk", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");
        if (!m) { error = "installer unavailable"; return false; }
        LocalActivity a;
        LocalString p(apk);
        auto res = static_cast<jstring>(env()->CallStaticObjectMethod(bridge(), m, a.get(), p.get()));
        if (checkException()) { error = "installer failed"; return false; }
        if (!res) return true;
        error = toStd(res);
        return false;
    }
    InstallStatus status() override {
        static jmethodID ms = method("installState", "()I");
        static jmethodID mm = method("installMessage", "()Ljava/lang/String;");
        InstallStatus st;
        if (!ms) return st;
        jint s = env()->CallStaticIntMethod(bridge(), ms);
        checkException();
        switch (s) {
            case 1: st.state = InstallStatus::State::Pending; break;
            case 2: st.state = InstallStatus::State::WaitingForUser; break;
            case 3: st.state = InstallStatus::State::Success; break;
            case 4: st.state = InstallStatus::State::Failed; break;
            default: st.state = InstallStatus::State::Idle; break;
        }
        if (mm && st.state == InstallStatus::State::Failed) {
            st.message = toStd(static_cast<jstring>(env()->CallStaticObjectMethod(bridge(), mm)));
            checkException();
        }
        return st;
    }
};

// ---------------------------------------------------------------- haptics

class AndroidHaptics : public IHaptics {
public:
    void vibrate(int ms) override {
        static jmethodID m = method("vibrate", "(Landroid/content/Context;I)V");
        if (!m) return;
        LocalActivity a;
        env()->CallStaticVoidMethod(bridge(), m, a.get(), static_cast<jint>(ms));
        checkException();
    }
};

// ---------------------------------------------------------------- storage

class AndroidStorage : public IStorage {
public:
    std::string dataDir() override {
        const char* p = SDL_GetAndroidInternalStoragePath();
        return p ? std::string(p) : std::string();
    }
    std::string cacheDir() override {
        const char* p = SDL_GetAndroidCachePath();
        return p ? std::string(p) : joinPath(dataDir(), "cache");
    }
    uint64_t appInstallSize() override {
        static jmethodID m = method("apkSize", "(Landroid/content/Context;)J");
        if (!m) return 0;
        LocalActivity a;
        jlong v = env()->CallStaticLongMethod(bridge(), m, a.get());
        return checkException() ? 0 : static_cast<uint64_t>(v);
    }
    int64_t freeSpace() override {
        std::error_code ec;
        auto sp = std::filesystem::space(dataDir(), ec);
        return ec ? -1 : static_cast<int64_t>(sp.available);
    }
};

class AndroidPlatform : public IPlatform {
public:
    AppInfo appInfo() override { return {MGH_VERSION_STR, MGH_VERSION_CODE, "Android"}; }
    INetwork& network() override { return net_; }
    IAppInstaller& installer() override { return installer_; }
    IHaptics& haptics() override { return haptics_; }
    IStorage& storage() override { return storage_; }
    void openUrl(const std::string& url) override { SDL_OpenURL(url.c_str()); }
    void minimize() override {
        static jmethodID m = method("moveToBack", "(Landroid/app/Activity;)V");
        if (!m) return;
        LocalActivity a;
        env()->CallStaticVoidMethod(bridge(), m, a.get());
        checkException();
    }

private:
    AndroidNetwork net_;
    AndroidInstaller installer_;
    AndroidHaptics haptics_;
    AndroidStorage storage_;
};

}  // namespace

std::unique_ptr<IPlatform> createPlatform() {
    bridge();  // resolve the class early on the SDL main thread
    return std::make_unique<AndroidPlatform>();
}

}  // namespace mgh
