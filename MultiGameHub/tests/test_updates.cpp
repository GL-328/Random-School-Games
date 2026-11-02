#include <filesystem>
#include <fstream>
#include <map>

#include "TestFramework.hpp"
#include "core/FileUtil.hpp"
#include "core/Json.hpp"
#include "core/Settings.hpp"
#include "miniz.h"
#include "storage/StorageManager.hpp"
#include "updates/Manifest.hpp"
#include "updates/PackExtractor.hpp"
#include "updates/Sha256.hpp"
#include "updates/UpdateManager.hpp"

namespace fs = std::filesystem;
using namespace mgh;

// ---------------------------------------------------------------- helpers

static std::string tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("mgh_test_" + name);
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

static void writeFile(const std::string& path, const std::string& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}

static bool makeZip(const std::string& path, const std::map<std::string, std::string>& files) {
    mz_zip_archive z{};
    if (!mz_zip_writer_init_file(&z, path.c_str(), 0)) return false;
    for (const auto& [name, data] : files)
        mz_zip_writer_add_mem(&z, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
    return true;
}

// Serves "https://github.com/..." URLs from a local map (no real network in tests).
class FakeNetwork : public INetwork {
public:
    std::map<std::string, std::string> files;  // url -> local source file
    bool failNext = false;
    bool hold = false;  // keep downloads "running" until released
    struct Job { std::string url, dest; uint64_t max; bool cancelled = false; };
    std::map<int, Job> jobs;
    int next = 1;

    int startDownload(const std::string& url, const std::string& dest, uint64_t maxBytes) override {
        jobs[next] = Job{url, dest, maxBytes};
        return next++;
    }
    DownloadStatus poll(int h) override {
        DownloadStatus st;
        auto it = jobs.find(h);
        if (it == jobs.end()) return st;
        if (it->second.cancelled) { st.state = DownloadStatus::State::Cancelled; return st; }
        if (hold) { st.state = DownloadStatus::State::Running; st.received = 10; st.total = 100; return st; }
        if (failNext) { failNext = false; st.state = DownloadStatus::State::Failed; st.error = "HTTP 404"; return st; }
        auto f = files.find(it->second.url);
        if (f == files.end()) { st.state = DownloadStatus::State::Failed; st.error = "HTTP 404"; return st; }
        std::error_code ec;
        fs::copy_file(f->second, it->second.dest, fs::copy_options::overwrite_existing, ec);
        st.state = ec ? DownloadStatus::State::Failed : DownloadStatus::State::Done;
        st.received = st.total = static_cast<int64_t>(fileSize(it->second.dest));
        return st;
    }
    void cancel(int h) override { jobs[h].cancelled = true; }
    void release(int h) override { jobs.erase(h); }
};

class FakeInstaller : public IAppInstaller {
public:
    bool allowed = true;
    bool signatureOk = true;
    std::string installed;
    bool supported() const override { return true; }
    bool canRequestInstall() override { return allowed; }
    void openInstallPermissionSettings() override {}
    bool verifyPackage(const std::string&, std::string& err) override {
        if (!signatureOk) err = "signature mismatch";
        return signatureOk;
    }
    bool install(const std::string& p, std::string&) override { installed = p; return true; }
    InstallStatus status() override {
        InstallStatus s;
        s.state = installed.empty() ? InstallStatus::State::Idle : InstallStatus::State::WaitingForUser;
        return s;
    }
};

class TempStorage : public IStorage {
public:
    std::string root;
    explicit TempStorage(std::string r) : root(std::move(r)) {
        fs::create_directories(root + "/data");
        fs::create_directories(root + "/cache");
    }
    std::string dataDir() override { return root + "/data"; }
    std::string cacheDir() override { return root + "/cache"; }
    uint64_t appInstallSize() override { return 0; }
    int64_t freeSpace() override { return -1; }
};

static std::string manifestJson(const std::string& apkSha, uint64_t apkSize, const std::string& packSha,
                                uint64_t packSize, const std::string& gameVersion = "1.1.0") {
    Json root = Json::object();
    root.set("schema", Json::number(1));
    Json app = Json::object();
    app.set("version", Json::string("1.2.0"));
    app.set("versionCode", Json::number(10200));
    Json apk = Json::object();
    apk.set("url", Json::string("https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/app.apk"));
    apk.set("sha256", Json::string(apkSha));
    apk.set("size", Json::number(static_cast<double>(apkSize)));
    app.set("apk", apk);
    app.set("notes", Json::string("Bug fixes"));
    root.set("app", app);
    Json games = Json::array();
    Json g = Json::object();
    g.set("id", Json::string("chess"));
    g.set("name", Json::string("Chess"));
    g.set("version", Json::string(gameVersion));
    Json pack = Json::object();
    pack.set("url", Json::string("https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/chess.zip"));
    pack.set("sha256", Json::string(packSha));
    pack.set("size", Json::number(static_cast<double>(packSize)));
    g.set("pack", pack);
    games.push(g);
    root.set("games", games);
    return root.dump(2);
}

static const char* kManifestUrl = "https://github.com/GL-328/Random-School-Games/releases/latest/download/manifest.json";

// ---------------------------------------------------------------- SHA-256

TEST_CASE("sha256: FIPS test vectors") {
    CHECK(Sha256::hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(Sha256::hex(two.data(), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string million(1000000, 'a');
    CHECK(Sha256::hex(million.data(), million.size()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Incremental updates across block boundaries give the same digest.
    Sha256 s;
    for (char c : two) s.update(&c, 1);
    CHECK(s.finishHex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha256::equalHex("ABCDEF", "abcdef"));
    CHECK(!Sha256::equalHex("abcdef", "abcdee"));
}

TEST_CASE("sha256: file hashing") {
    std::string dir = tempDir("sha");
    writeFile(dir + "/f.bin", "abc");
    CHECK(*Sha256::fileHex(dir + "/f.bin") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(!Sha256::fileHex(dir + "/missing").has_value());
}

// ---------------------------------------------------------------- JSON / versions

TEST_CASE("json: parse, access, round trip and rejection") {
    auto j = Json::parse(R"({"a": [1, 2.5, -3e2], "b": {"c": "x\u00e9\n"}, "t": true, "n": null})");
    REQUIRE(j.has_value());
    CHECK((*j)["a"].size() == 3u);
    CHECK((*j)["a"].at(2).asNumber() == -300.0);
    CHECK((*j)["b"]["c"].asString() == "x\xC3\xA9\n");
    CHECK((*j)["t"].asBool());
    CHECK((*j)["n"].isNull());
    CHECK((*j)["missing"].isNull());
    auto again = Json::parse(j->dump());
    REQUIRE(again.has_value());
    CHECK(again->dump() == j->dump());
    CHECK(!Json::parse("{\"a\":1,}").has_value());
    CHECK(!Json::parse("[1 2]").has_value());
    CHECK(!Json::parse("{} trailing").has_value());
    CHECK(!Json::parse("\"unterminated").has_value());
    CHECK(!Json::parse("01").has_value());
    CHECK(!Json::parse(std::string(100, '[') + std::string(100, ']')).has_value());  // depth limit
}

TEST_CASE("version: parse and compare") {
    CHECK(Version::parse("1.2.3")->code() == 10203);
    CHECK(Version::parse("v2.0")->str() == "2.0.0");
    CHECK(*Version::parse("1.10.0") > *Version::parse("1.9.9"));
    CHECK(!Version::parse("1..2").has_value());
    CHECK(!Version::parse("1.2.3.4").has_value());
    CHECK(!Version::parse("abc").has_value());
    CHECK(!Version::parse("1.100.0").has_value());
    CHECK(!Version::parse("").has_value());
}

// ---------------------------------------------------------------- manifest

TEST_CASE("manifest: valid manifest parses") {
    std::string err;
    auto m = parseManifest(manifestJson(std::string(64, 'a'), 1000, std::string(64, 'b'), 500), err);
    REQUIRE(m.has_value());
    CHECK(m->app.version.str() == "1.2.0");
    CHECK(m->games.size() == 1u);
    CHECK(m->findGame("chess")->pack->size == 500u);
}

TEST_CASE("manifest: rejects insecure or malformed content") {
    std::string err;
    std::string good = manifestJson(std::string(64, 'a'), 1000, std::string(64, 'b'), 500);
    auto mutate = [&](const std::string& from, const std::string& to) {
        std::string s = good;
        auto pos = s.find(from);
        if (pos != std::string::npos) s.replace(pos, from.size(), to);
        return s;
    };
    CHECK(!parseManifest(mutate("https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/app.apk",
                                "http://github.com/x.apk"), err));
    CHECK(!parseManifest(mutate("https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/app.apk",
                                "https://evil.example.com/x.apk"), err));
    CHECK(!parseManifest(mutate(std::string(64, 'a'), "zz"), err));             // bad hash
    CHECK(!parseManifest(mutate("\"chess\"", "\"../etc\""), err));              // bad id
    CHECK(!parseManifest(mutate("10200", "10300"), err));                       // code/version mismatch
    CHECK(!parseManifest(mutate("\"schema\": 1", "\"schema\": 2"), err));
    CHECK(!parseManifest(mutate("\"size\": 1000", "\"size\": -5"), err));
    CHECK(!parseManifest("not json", err));
    CHECK(!isTrustedDownloadUrl("https://github.com.evil.com/a"));
    CHECK(!isTrustedDownloadUrl("https://github.com/a b"));
    CHECK(isTrustedDownloadUrl("https://objects.githubusercontent.com/x"));
}

// ---------------------------------------------------------------- extraction

TEST_CASE("pack paths: traversal and unsafe names are rejected") {
    CHECK(isSafeArchivePath("theme.json"));
    CHECK(isSafeArchivePath("sounds/hit.wav"));
    CHECK(!isSafeArchivePath("../theme.json"));
    CHECK(!isSafeArchivePath("a/../../theme.json"));
    CHECK(!isSafeArchivePath("/etc/passwd.txt"));
    CHECK(!isSafeArchivePath("a\\..\\b.json"));
    CHECK(!isSafeArchivePath("C:/x.json"));
    CHECK(!isSafeArchivePath("a//b.json"));
    CHECK(!isSafeArchivePath("./a.json"));
    CHECK(!isSafeArchivePath(".hidden.json"));
    CHECK(!isSafeArchivePath("lib/libevil.so"));   // executable content
    CHECK(!isSafeArchivePath("classes.dex"));
    CHECK(!isSafeArchivePath("noext"));
}

TEST_CASE("pack extraction: good archive extracts, malicious archives fail cleanly") {
    std::string dir = tempDir("zip");
    REQUIRE(makeZip(dir + "/good.zip", {{"theme.json", "{\"a\":1}"}, {"img/x.png", std::string(5000, 'x')}}));
    ExtractResult r = extractPack(dir + "/good.zip", dir + "/out");
    CHECK(r.ok);
    CHECK(r.files == 2);
    CHECK(fs::exists(dir + "/out/img/x.png"));
    CHECK(*readTextFile(dir + "/out/theme.json") == "{\"a\":1}");

    REQUIRE(makeZip(dir + "/evil.zip", {{"ok.json", "{}"}, {"../../escape.json", "{}"}}));
    r = extractPack(dir + "/evil.zip", dir + "/out2");
    CHECK(!r.ok);
    CHECK(!fs::exists(dir + "/out2"));
    CHECK(!fs::exists(dir + "/escape.json"));
    CHECK(!fs::exists(fs::path(dir).parent_path() / "escape.json"));

    REQUIRE(makeZip(dir + "/exe.zip", {{"libgame.so", "ELF"}}));
    CHECK(!extractPack(dir + "/exe.zip", dir + "/out3").ok);

    writeFile(dir + "/notzip.zip", "this is not a zip file");
    CHECK(!extractPack(dir + "/notzip.zip", dir + "/out4").ok);

    ExtractLimits small;
    small.maxTotalBytes = 1000;  // zip-bomb style limit
    CHECK(!extractPack(dir + "/good.zip", dir + "/out5", small).ok);
    CHECK(!fs::exists(dir + "/out5"));
}

// ---------------------------------------------------------------- update manager

struct UpdateFixture {
    std::string dir;
    TempStorage storage;
    StorageManager sm;
    FakeNetwork net;
    FakeInstaller inst;
    UpdateManager um;
    std::string apkPath, packPath;

    explicit UpdateFixture(const std::string& name, bool corruptApk = false)
        : dir(tempDir(name)), storage(dir), sm(storage), um(net, inst, sm, 10000, kManifestUrl) {
        apkPath = dir + "/src.apk";
        writeFile(apkPath, std::string(4096, 'A'));
        packPath = dir + "/src.zip";
        makeZip(packPath, {{"theme.json", "{\"light\":\"#ffffff\"}"}});
        std::string apkSha = *Sha256::fileHex(apkPath);
        if (corruptApk) apkSha = std::string(64, '0');
        writeFile(dir + "/manifest.json",
                  manifestJson(apkSha, fileSize(apkPath), *Sha256::fileHex(packPath), fileSize(packPath)));
        net.files[kManifestUrl] = dir + "/manifest.json";
        net.files["https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/app.apk"] = apkPath;
        net.files["https://github.com/GL-328/Random-School-Games/releases/download/v1.2.0/chess.zip"] = packPath;
    }
    void pump() {
        for (int i = 0; i < 10 && um.busy(); ++i) um.update();
    }
};

TEST_CASE("updates: check detects new app version") {
    UpdateFixture f("um_check");
    bool notified = false;
    f.um.onCheckFinished = [&](bool ok) { notified = ok; };
    f.um.checkForUpdates();
    CHECK(f.um.phase() == UpdateManager::Phase::CheckingManifest);
    f.pump();
    CHECK(notified);
    CHECK(f.um.hasManifest());
    CHECK(f.um.appUpdateAvailable());
    CHECK(f.um.manifest()->app.notes == "Bug fixes");
}

TEST_CASE("updates: app download verifies SHA-256 then installs") {
    UpdateFixture f("um_app");
    f.um.checkForUpdates();
    f.pump();
    f.um.downloadApp();
    f.pump();
    CHECK(f.um.phase() == UpdateManager::Phase::ReadyToInstall);
    f.um.installApp();
    CHECK(f.um.phase() == UpdateManager::Phase::Installing);
    CHECK(!f.inst.installed.empty());
    f.um.update();
    CHECK(f.um.statusText().find("confirm") != std::string::npos);
}

TEST_CASE("updates: hash mismatch is rejected and file removed") {
    UpdateFixture f("um_badhash", true);
    f.um.checkForUpdates();
    f.pump();
    f.um.downloadApp();
    f.pump();
    CHECK(f.um.phase() == UpdateManager::Phase::Error);
    CHECK(f.um.error().find("SHA-256") != std::string::npos);
    CHECK(!fs::exists(f.sm.downloadsDir() + "/update.apk"));
    CHECK(!fs::exists(f.sm.downloadsDir() + "/update.apk.part"));
}

TEST_CASE("updates: signature mismatch and missing install permission") {
    UpdateFixture f("um_sig");
    f.inst.signatureOk = false;
    f.um.checkForUpdates();
    f.pump();
    f.um.downloadApp();
    f.pump();
    CHECK(f.um.phase() == UpdateManager::Phase::Error);
    CHECK(f.um.error().find("signature") != std::string::npos);

    UpdateFixture g("um_perm");
    g.inst.allowed = false;
    g.um.checkForUpdates();
    g.pump();
    g.um.downloadApp();
    g.pump();
    g.um.installApp();
    CHECK(g.um.phase() == UpdateManager::Phase::ReadyToInstall);
    CHECK(g.um.error().find("Install unknown apps") != std::string::npos);
    CHECK(g.inst.installed.empty());
}

TEST_CASE("updates: network failure and cancellation") {
    UpdateFixture f("um_fail");
    f.net.failNext = true;
    bool result = true;
    f.um.onCheckFinished = [&](bool ok) { result = ok; };
    f.um.checkForUpdates();
    f.pump();
    CHECK(f.um.phase() == UpdateManager::Phase::Error);
    CHECK(!result);
    f.um.clearError();
    CHECK(f.um.phase() == UpdateManager::Phase::Idle);

    f.um.checkForUpdates();
    f.pump();
    f.net.hold = true;
    f.um.downloadApp();
    f.um.update();
    CHECK(f.um.phase() == UpdateManager::Phase::DownloadingApp);
    CHECK(f.um.progress() > 0.0f);
    f.um.cancel();
    CHECK(f.um.phase() == UpdateManager::Phase::Idle);
    CHECK(!f.um.busy());
}

TEST_CASE("updates: game pack install, update and uninstall") {
    UpdateFixture f("um_pack");
    f.um.checkForUpdates();
    f.pump();
    CHECK(f.um.packInstallable("chess"));
    CHECK(!f.um.packUpdateAvailable("chess"));  // not installed yet
    std::string installedId;
    f.um.onPackInstalled = [&](const std::string& id) { installedId = id; };
    f.um.downloadPack("chess");
    f.pump();
    CHECK(installedId == "chess");
    CHECK(f.sm.hasPack("chess"));
    CHECK(f.sm.packVersion("chess")->str() == "1.1.0");
    CHECK(fs::exists(f.sm.packDir("chess") + "/theme.json"));
    CHECK(f.sm.packSize("chess") > 0);

    // A newer pack version in a later manifest is detected as an update, replaces old files.
    writeFile(f.dir + "/manifest.json", manifestJson(*Sha256::fileHex(f.apkPath), fileSize(f.apkPath),
                                                     *Sha256::fileHex(f.packPath), fileSize(f.packPath), "1.2.0"));
    f.um.checkForUpdates();
    f.pump();
    CHECK(f.um.packUpdateAvailable("chess"));
    CHECK(f.um.pendingGameUpdates() == 1);
    f.um.downloadPack("chess");
    f.pump();
    CHECK(f.sm.packVersion("chess")->str() == "1.2.0");
    CHECK(!fs::exists(f.sm.packDir("chess") + ".old"));
    CHECK(!fs::exists(f.sm.packDir("chess") + ".staging"));

    // Index survives a restart.
    StorageManager again(f.storage);
    CHECK(again.hasPack("chess"));

    CHECK(f.sm.uninstallPack("chess"));
    CHECK(!f.sm.hasPack("chess"));
    CHECK(!fs::exists(f.sm.packDir("chess")));
}

// ---------------------------------------------------------------- settings / storage

TEST_CASE("settings: persist between launches") {
    std::string dir = tempDir("settings");
    {
        Settings s(dir + "/settings.cfg");
        CHECK(s.getBool("sound", true));
        s.setBool("sound", false);
        s.setInt(keys::kSnakeHighScore, 42);
        s.setString("theme", "light\nx=1");  // newline injection is neutralised
    }
    Settings s(dir + "/settings.cfg");
    CHECK(!s.getBool("sound", true));
    CHECK(s.getInt(keys::kSnakeHighScore, 0) == 42);
    CHECK(s.getString("theme") == "light x=1");
    CHECK(s.getString("x", "none") == "none");
}

TEST_CASE("storage: cache clearing and stale cleanup") {
    std::string dir = tempDir("storage");
    TempStorage st(dir);
    StorageManager sm(st);
    writeFile(sm.downloadsDir() + "/half.apk.part", std::string(100, 'x'));
    writeFile(sm.downloadsDir() + "/update.apk", std::string(100, 'x'));
    fs::create_directories(sm.packsDir() + "/chess.staging");
    sm.cleanupStale();
    CHECK(!fs::exists(sm.downloadsDir() + "/half.apk.part"));
    CHECK(!fs::exists(sm.downloadsDir() + "/update.apk"));
    CHECK(!fs::exists(sm.packsDir() + "/chess.staging"));
    writeFile(st.cacheDir() + "/junk.bin", std::string(2048, 'x'));
    CHECK(sm.cacheSize() >= 2048u);
    sm.clearCache();
    CHECK(sm.cacheSize() == 0u);
    CHECK(fs::is_directory(sm.downloadsDir()));
    CHECK(formatBytes(1536) == "1.5 KB");
}
