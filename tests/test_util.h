#pragma once
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace clomdb::test {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> reg;
    return reg;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline int failures = 0;
inline int checks = 0;

inline std::string TempDir() {
    static std::string dir = [] {
        std::filesystem::path base = std::filesystem::temp_directory_path() /
                                      ("clomdb_test_" + std::to_string(static_cast<long>(getpid())));
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base);
        return base.string();
    }();
    return dir;
}

}

#define CLOMDB_TEST(name)                                                     \
    static void clomdb_test_##name();                                        \
    static ::clomdb::test::Registrar clomdb_test_registrar_##name(#name,      \
                                                                    clomdb_test_##name); \
    static void clomdb_test_##name()

#define CHECK(cond)                                                                        \
    do {                                                                                    \
        ::clomdb::test::checks++;                                                           \
        if (!(cond)) {                                                                      \
            ::clomdb::test::failures++;                                                     \
            std::cerr << "  CHECK FAILED: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                              \
        }                                                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                       \
    do {                                                                                     \
        ::clomdb::test::checks++;                                                            \
        auto va = (a);                                                                       \
        auto vb = (b);                                                                       \
        if (!(va == vb)) {                                                                   \
            ::clomdb::test::failures++;                                                      \
            std::cerr << "  CHECK_EQ FAILED: " << #a << " != " << #b << " (" << va << " vs "  \
                      << vb << ") at " << __FILE__ << ":" << __LINE__ << "\n";                \
        }                                                                                     \
    } while (0)

#define CLOMDB_TEST_MAIN()                                              \
    int main() {                                                        \
        for (auto& tc : ::clomdb::test::Registry()) {                   \
            std::cout << "[ RUN      ] " << tc.name << "\n";            \
            tc.fn();                                                    \
            std::cout << "[       OK ] " << tc.name << "\n";            \
        }                                                               \
        std::cout << ::clomdb::test::checks << " checks, "              \
                  << ::clomdb::test::failures << " failures\n";         \
        return ::clomdb::test::failures == 0 ? 0 : 1;                   \
    }
