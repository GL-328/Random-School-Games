// Minimal, strict JSON parser/serialiser (RFC 8259) for manifests and
// small data files. Depth-limited; rejects trailing garbage.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mgh {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json boolean(bool b) { Json j; j.type_ = Type::Bool; j.b_ = b; return j; }
    static Json number(double d) { Json j; j.type_ = Type::Number; j.n_ = d; return j; }
    static Json string(std::string s) { Json j; j.type_ = Type::String; j.s_ = std::move(s); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    static std::optional<Json> parse(const std::string& text, std::string* error = nullptr);
    std::string dump(int indent = -1) const;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? b_ : def; }
    double asNumber(double def = 0) const { return isNumber() ? n_ : def; }
    int64_t asInt(int64_t def = 0) const { return isNumber() ? static_cast<int64_t>(n_) : def; }
    const std::string& asString() const { static const std::string empty; return isString() ? s_ : empty; }

    // Object access. Returns a shared null value for missing keys.
    const Json& operator[](const std::string& key) const;
    bool has(const std::string& key) const;
    void set(const std::string& key, Json v);
    const std::vector<std::pair<std::string, Json>>& items() const { return obj_; }

    // Array access.
    size_t size() const { return isArray() ? arr_.size() : (isObject() ? obj_.size() : 0); }
    const Json& at(size_t i) const;
    void push(Json v) { arr_.push_back(std::move(v)); }
    const std::vector<Json>& elements() const { return arr_; }

private:
    friend class JsonParser;
    void dumpTo(std::string& out, int indent, int level) const;

    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

}  // namespace mgh
