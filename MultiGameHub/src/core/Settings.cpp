#include "Settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "FileUtil.hpp"

namespace mgh {

bool Settings::load() {
    values_.clear();
    if (path_.empty()) return false;
    std::ifstream in(path_);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return true;
}

bool Settings::save() const {
    if (path_.empty()) return false;
    std::ostringstream out;
    for (const auto& [k, v] : values_) out << k << '=' << v << '\n';
    return writeFileAtomic(path_, out.str());
}

bool Settings::getBool(const std::string& key, bool def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    return it->second == "1" || it->second == "true";
}

long long Settings::getInt(const std::string& key, long long def) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return def;
    char* end = nullptr;
    long long v = std::strtoll(it->second.c_str(), &end, 10);
    return (end && *end == '\0') ? v : def;
}

std::string Settings::getString(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

void Settings::setString(const std::string& key, const std::string& v) {
    // Keys/values are single-line by construction.
    std::string clean = v;
    for (char& c : clean)
        if (c == '\n' || c == '\r') c = ' ';
    auto it = values_.find(key);
    if (it != values_.end() && it->second == clean) return;
    values_[key] = clean;
    save();
}

}  // namespace mgh
