// Validates a release manifest with the exact parser the app uses, and
// optionally verifies that local files match the listed SHA-256 and sizes.
// Usage: manifest_check manifest.json [dir-with-release-files]
#include <cstdio>
#include <string>

#include "core/FileUtil.hpp"
#include "updates/Manifest.hpp"
#include "updates/PackExtractor.hpp"
#include "updates/Sha256.hpp"

using namespace mgh;

static bool checkFile(const std::string& dir, const FileRef& ref) {
    std::string name = ref.url.substr(ref.url.rfind('/') + 1);
    std::string path = joinPath(dir, name);
    auto h = Sha256::fileHex(path);
    bool ok = h && Sha256::equalHex(*h, ref.sha256) && fileSize(path) == ref.size;
    std::printf("  %-40s %s\n", name.c_str(), ok ? "OK" : "MISMATCH");
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s manifest.json [release-dir]\n", argv[0]);
        return 2;
    }
    auto text = readTextFile(argv[1]);
    if (!text) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    std::string err;
    auto m = parseManifest(*text, err);
    if (!m) { std::fprintf(stderr, "INVALID manifest: %s\n", err.c_str()); return 1; }
    std::printf("manifest OK: app %s (code %d), %zu game entries\n", m->app.version.str().c_str(),
                m->app.versionCode, m->games.size());
    if (argc < 3) return 0;
    bool ok = checkFile(argv[2], m->app.apk);
    for (const auto& g : m->games) {
        if (!g.pack) continue;
        ok &= checkFile(argv[2], *g.pack);
        // The pack must also pass the app's safe extractor.
        std::string zip = joinPath(argv[2], g.pack->url.substr(g.pack->url.rfind('/') + 1));
        std::string tmp = zip + ".extract-test";
        removeTree(tmp);
        ExtractResult r = extractPack(zip, tmp);
        removeTree(tmp);
        std::printf("  %-40s %s\n", ("extract " + g.id).c_str(), r.ok ? "OK" : r.error.c_str());
        ok &= r.ok;
    }
    return ok ? 0 : 1;
}
