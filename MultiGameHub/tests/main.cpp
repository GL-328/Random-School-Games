#include <cstring>

#include "TestFramework.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedCases = 0;
    for (auto& c : test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = test::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        try {
            c.fn();
        } catch (const test::AbortCase&) {
        } catch (const std::exception& e) {
            ++test::failures();
            std::printf("  EXCEPTION: %s\n", e.what());
        }
        bool ok = test::failures() == before;
        if (!ok) ++failedCases;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
        ++ran;
    }
    std::printf("\n%d test cases, %d checks, %d failed cases\n", ran, test::checks(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
