// Small filesystem helpers shared by every platform (std::filesystem works on
// Android's app-private storage as well as on desktop systems).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mgh {

std::optional<std::string> readTextFile(const std::string& path, uint64_t maxBytes = 4 * 1024 * 1024);
// Writes to "<path>.tmp" then renames over the target so a crash never leaves a half-written file.
bool writeFileAtomic(const std::string& path, const std::string& data);
uint64_t directorySize(const std::string& path);
uint64_t fileSize(const std::string& path);
bool removeTree(const std::string& path);
bool ensureDir(const std::string& path);
std::string joinPath(const std::string& a, const std::string& b);
std::string formatBytes(uint64_t bytes);

}  // namespace mgh
