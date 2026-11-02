// Tiny dependency-free test framework (keeps the repo small).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

struct AbortCase {};

}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST_CASE(name)                                                                  \
    static void TEST_CAT(test_fn_, __LINE__)();                                          \
    static test::Registrar TEST_CAT(test_reg_, __LINE__)(name, TEST_CAT(test_fn_, __LINE__)); \
    static void TEST_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++test::checks();                                                               \
        if (!(cond)) {                                                                  \
            ++test::failures();                                                         \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define REQUIRE(cond)                                                                   \
    do {                                                                                \
        ++test::checks();                                                               \
        if (!(cond)) {                                                                  \
            ++test::failures();                                                         \
            std::printf("  FAILED (fatal) %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            throw test::AbortCase{};                                                    \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        ++test::checks();                                                               \
        auto _va = (a);                                                                 \
        auto _vb = (b);                                                                 \
        if (!(_va == _vb)) {                                                            \
            ++test::failures();                                                         \
            std::printf("  FAILED %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);      \
        }                                                                               \
    } while (0)
