#include "Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mgh {

class JsonParser {
public:
    JsonParser(const std::string& s) : s_(s) {}

    bool parseDocument(Json& out) {
        skipWs();
        if (!parseValue(out, 0)) return false;
        skipWs();
        if (pos_ != s_.size()) return fail("trailing characters");
        return true;
    }

    std::string error;

private:
    static constexpr int kMaxDepth = 64;

    bool fail(const char* msg) {
        if (error.empty()) error = std::string(msg) + " at offset " + std::to_string(pos_);
        return false;
    }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }
    bool literal(const char* lit) {
        size_t n = std::strlen(lit);
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        return true;
    }

    bool parseValue(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (pos_ >= s_.size()) return fail("unexpected end");
        char c = s_[pos_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            out.type_ = Json::Type::String;
            return parseString(out.s_);
        }
        if (c == 't') { if (!literal("true")) return fail("bad literal"); out = Json::boolean(true); return true; }
        if (c == 'f') { if (!literal("false")) return fail("bad literal"); out = Json::boolean(false); return true; }
        if (c == 'n') { if (!literal("null")) return fail("bad literal"); out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        return fail("unexpected character");
    }

    bool parseNumber(Json& out) {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size()) return fail("bad number");
        if (s_[pos_] == '0') {
            ++pos_;
        } else if (s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        } else {
            return fail("bad number");
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("bad number");
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("bad number");
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        std::string num = s_.substr(start, pos_ - start);
        double d = std::strtod(num.c_str(), nullptr);
        if (!std::isfinite(d)) return fail("number out of range");
        out = Json::number(d);
        return true;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t& v) {
        if (pos_ + 4 > s_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (true) {
            if (pos_ >= s_.size()) return fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) return fail("bad escape");
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return fail("bad unicode escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo;
                        if (pos_ + 2 > s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') return fail("lone surrogate");
                        pos_ += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("lone surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
    }

    bool parseArray(Json& out, int depth) {
        ++pos_;
        out = Json::array();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skipWs();
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.arr_.push_back(std::move(v));
            skipWs();
            if (pos_ >= s_.size()) return fail("unterminated array");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return fail("expected , or ]");
        }
    }

    bool parseObject(Json& out, int depth) {
        ++pos_;
        out = Json::object();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected key");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected :");
            ++pos_;
            skipWs();
            Json v;
            if (!parseValue(v, depth + 1)) return false;
            out.set(key, std::move(v));  // duplicate keys: last wins
            skipWs();
            if (pos_ >= s_.size()) return fail("unterminated object");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return fail("expected , or }");
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
};

std::optional<Json> Json::parse(const std::string& text, std::string* error) {
    JsonParser p(text);
    Json j;
    if (!p.parseDocument(j)) {
        if (error) *error = p.error;
        return std::nullopt;
    }
    return j;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json null;
    if (!isObject()) return null;
    for (const auto& kv : obj_)
        if (kv.first == key) return kv.second;
    return null;
}

bool Json::has(const std::string& key) const {
    if (!isObject()) return false;
    for (const auto& kv : obj_)
        if (kv.first == key) return true;
    return false;
}

void Json::set(const std::string& key, Json v) {
    if (type_ == Type::Null) type_ = Type::Object;
    for (auto& kv : obj_)
        if (kv.first == key) { kv.second = std::move(v); return; }
    obj_.emplace_back(key, std::move(v));
}

const Json& Json::at(size_t i) const {
    static const Json null;
    return (isArray() && i < arr_.size()) ? arr_[i] : null;
}

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto nl = [&](int lvl) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * lvl), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[32];
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n_);
            else std::snprintf(buf, sizeof buf, "%.17g", n_);
            out += buf;
            break;
        }
        case Type::String: escapeTo(out, s_); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                nl(level + 1);
                arr_[i].dumpTo(out, indent, level + 1);
            }
            if (!arr_.empty()) nl(level);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                nl(level + 1);
                escapeTo(out, obj_[i].first);
                out += indent >= 0 ? ": " : ":";
                obj_[i].second.dumpTo(out, indent, level + 1);
            }
            if (!obj_.empty()) nl(level);
            out += '}';
            break;
    }
}

std::string Json::dump(int indent) const {
    std::string s;
    dumpTo(s, indent, 0);
    return s;
}

}  // namespace mgh
