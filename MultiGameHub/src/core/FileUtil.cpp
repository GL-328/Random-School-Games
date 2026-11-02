#include "FileUtil.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace mgh {

std::optional<std::string> readTextFile(const std::string& path, uint64_t maxBytes) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec || size > maxBytes) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string s(static_cast<size_t>(size), '\0');
    in.read(s.data(), static_cast<std::streamsize>(size));
    if (static_cast<uint64_t>(in.gcount()) != size) return std::nullopt;
    return s;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

uint64_t directorySize(const std::string& path) {
    std::error_code ec;
    uint64_t total = 0;
    if (!fs::exists(path, ec)) return 0;
    if (fs::is_regular_file(path, ec)) return fs::file_size(path, ec);
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && !it->is_symlink(e2)) total += it->file_size(e2);
    }
    return total;
}

uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    auto s = fs::file_size(path, ec);
    return ec ? 0 : s;
}

bool removeTree(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    return !ec;
}

bool ensureDir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return fs::is_directory(path, ec);
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string formatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

}  // namespace mgh
