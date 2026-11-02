#include "PackExtractor.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "miniz.h"

namespace fs = std::filesystem;

namespace mgh {

namespace {

bool hasAllowedExtension(const std::string& name) {
    static const char* kAllowed[] = {".json", ".txt", ".png", ".jpg", ".webp", ".wav",
                                     ".ogg", ".ttf", ".otf", ".bin", ".csv", ".svg"};
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* a : kAllowed)
        if (ext == a) return true;
    return false;
}

struct WriteCtx {
    FILE* f = nullptr;
    uint64_t written = 0;
    uint64_t limit = 0;
    bool overflow = false;
};

size_t writeCb(void* opaque, mz_uint64 /*ofs*/, const void* buf, size_t n) {
    auto* ctx = static_cast<WriteCtx*>(opaque);
    if (ctx->written + n > ctx->limit) {
        ctx->overflow = true;
        return 0;  // aborts extraction
    }
    size_t w = std::fwrite(buf, 1, n, ctx->f);
    ctx->written += w;
    return w;
}

}  // namespace

bool isSafeArchivePath(const std::string& path) {
    if (path.empty() || path.size() > 200) return false;
    if (path.front() == '/' || path.back() == '/') return false;
    std::string component;
    for (size_t i = 0; i <= path.size(); ++i) {
        char c = i < path.size() ? path[i] : '/';
        if (c == '/') {
            if (component.empty() || component == "." || component == "..") return false;
            if (component.front() == '.') return false;  // no hidden files
            component.clear();
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                  c == '_' || c == '-';
        if (!ok) return false;  // rejects '\\', ':', NUL, spaces, unicode tricks
        component += c;
    }
    return hasAllowedExtension(path);
}

ExtractResult extractPack(const std::string& zipPath, const std::string& destDir, const ExtractLimits& limits) {
    ExtractResult res;
    std::error_code ec;
    if (fs::exists(destDir, ec)) {
        res.error = "destination already exists";
        return res;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
        res.error = "not a valid zip archive";
        return res;
    }

    auto failWith = [&](const std::string& msg) {
        res.ok = false;
        res.error = msg;
        mz_zip_reader_end(&zip);
        fs::remove_all(destDir, ec);
        return res;
    };

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count == 0) return failWith("archive is empty");
    if (static_cast<int>(count) > limits.maxFiles) return failWith("archive has too many entries");

    // Pass 1: validate every entry before writing anything.
    uint64_t declaredTotal = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) return failWith("corrupt archive entry");
        if (st.m_is_encrypted || !st.m_is_supported) return failWith("encrypted or unsupported entry");
        if (st.m_is_directory) continue;
        // Unix mode lives in the upper 16 bits of the external attributes; reject symlinks.
        const unsigned mode = (st.m_external_attr >> 16) & 0xF000u;
        if (mode == 0xA000u) return failWith("symbolic links are not allowed");
        if (!isSafeArchivePath(st.m_filename)) return failWith(std::string("unsafe path in archive: ") + st.m_filename);
        if (st.m_uncomp_size > limits.maxFileBytes) return failWith("entry too large");
        declaredTotal += st.m_uncomp_size;
        if (declaredTotal > limits.maxTotalBytes) return failWith("archive too large when extracted");
    }

    // Pass 2: extract.
    fs::create_directories(destDir, ec);
    if (ec) return failWith("cannot create destination directory");
    const fs::path base = fs::weakly_canonical(destDir, ec);

    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, i, &st);
        if (st.m_is_directory) continue;
        fs::path out = base / fs::path(st.m_filename);
        // Defence in depth: the normalised path must remain inside the base.
        fs::path norm = out.lexically_normal();
        auto rel = norm.lexically_relative(base);
        if (rel.empty() || rel.native().rfind("..", 0) == 0) return failWith("path escapes destination");
        fs::create_directories(norm.parent_path(), ec);
        if (ec) return failWith("cannot create directory");
        WriteCtx ctx;
        ctx.limit = st.m_uncomp_size;  // never write more than declared (bomb protection)
        ctx.f = std::fopen(norm.string().c_str(), "wb");
        if (!ctx.f) return failWith("cannot write file");
        mz_bool ok = mz_zip_reader_extract_to_callback(&zip, i, writeCb, &ctx, 0);
        bool closeOk = std::fclose(ctx.f) == 0;
        if (!ok || ctx.overflow || !closeOk || ctx.written != st.m_uncomp_size)
            return failWith("failed to extract entry (corrupt or size mismatch)");
        res.bytes += ctx.written;
        ++res.files;
    }
    mz_zip_reader_end(&zip);
    res.ok = true;
    return res;
}

}  // namespace mgh
