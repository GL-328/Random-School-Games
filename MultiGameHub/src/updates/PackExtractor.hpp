// Safe extraction of downloaded game content packs (zip archives).
// Protects against path traversal, absolute paths, symlinks, zip bombs,
// encrypted entries and executable content. Content packs are DATA ONLY.
#pragma once

#include <cstdint>
#include <string>

namespace mgh {

struct ExtractLimits {
    uint64_t maxTotalBytes = 128ull * 1024 * 1024;
    uint64_t maxFileBytes = 64ull * 1024 * 1024;
    int maxFiles = 2000;
};

struct ExtractResult {
    bool ok = false;
    std::string error;
    uint64_t bytes = 0;
    int files = 0;
};

// Validates a relative path from an archive. Accepts only
// [A-Za-z0-9._-] components separated by '/', no "." / ".." components,
// and only whitelisted data file extensions.
bool isSafeArchivePath(const std::string& path);

// Extracts `zipPath` into `destDir`, which must not exist yet (it is created).
// On any failure the partially-extracted directory is removed.
ExtractResult extractPack(const std::string& zipPath, const std::string& destDir,
                          const ExtractLimits& limits = ExtractLimits());

}  // namespace mgh
