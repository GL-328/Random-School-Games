// Persistent key/value settings stored as a small text file
// ("key=value" per line). Writes are atomic (temp file + rename).
#pragma once

#include <map>
#include <string>

namespace mgh {

class Settings {
public:
    Settings() = default;
    explicit Settings(std::string path) : path_(std::move(path)) { load(); }

    void setPath(std::string path) { path_ = std::move(path); load(); }
    bool load();
    bool save() const;

    bool getBool(const std::string& key, bool def) const;
    long long getInt(const std::string& key, long long def) const;
    std::string getString(const std::string& key, const std::string& def = "") const;

    // Setters persist immediately (only when the value actually changes).
    void setBool(const std::string& key, bool v) { setString(key, v ? "1" : "0"); }
    void setInt(const std::string& key, long long v) { setString(key, std::to_string(v)); }
    void setString(const std::string& key, const std::string& v);

private:
    std::string path_;
    std::map<std::string, std::string> values_;
};

// Well-known keys.
namespace keys {
inline constexpr const char* kTheme = "theme";              // "dark" | "light"
inline constexpr const char* kSound = "sound";
inline constexpr const char* kMusic = "music";
inline constexpr const char* kVibration = "vibration";
inline constexpr const char* kAutoUpdate = "auto_update";
inline constexpr const char* kLastUpdateCheck = "last_update_check";
inline constexpr const char* kSnakeHighScore = "snake.high_score";
inline constexpr const char* kChessShowHistory = "chess.show_history";
}  // namespace keys

}  // namespace mgh
