// Minimal single-header test harness used by the Tier 1 unit-test suite.
//
// Why hand-rolled and not doctest/Catch2: avoids pulling a header dependency
// during this first pass. The harness is dumb on purpose — TEST_CASE registers
// a function, CHECK / CHECK_EQ / CHECK_NEAR record pass/fail with file/line,
// and main() returns nonzero if anything failed. Move to doctest once we have
// > 100 cases or want section/fixture support.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace niteraid::testing {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

struct TestReg {
    TestReg(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Stats {
    int checks = 0;
    int failures = 0;
};

inline Stats& stats()
{
    static Stats s;
    return s;
}

inline void fail(const char* file, int line, const std::string& detail)
{
    ++stats().failures;
    std::printf("  FAIL %s:%d  %s\n", file, line, detail.c_str());
}

inline bool nearly_equal(float a, float b, float eps)
{
    return std::abs(a - b) <= eps;
}

}  // namespace niteraid::testing

#define NR_TEST_CONCAT_INNER(a, b) a##b
#define NR_TEST_CONCAT(a, b) NR_TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name_str)                                                                          \
    static void NR_TEST_CONCAT(nr_test_fn_, __LINE__)();                                             \
    namespace {                                                                                      \
    static ::niteraid::testing::TestReg NR_TEST_CONCAT(nr_test_reg_, __LINE__)(                      \
        name_str, &NR_TEST_CONCAT(nr_test_fn_, __LINE__));                                           \
    }                                                                                                \
    static void NR_TEST_CONCAT(nr_test_fn_, __LINE__)()

#define CHECK(expr)                                                                                  \
    do {                                                                                             \
        ++::niteraid::testing::stats().checks;                                                       \
        if (!(expr)) ::niteraid::testing::fail(__FILE__, __LINE__, #expr);                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                               \
    do {                                                                                             \
        ++::niteraid::testing::stats().checks;                                                       \
        auto _av = (a);                                                                              \
        auto _bv = (b);                                                                              \
        if (!(_av == _bv)) {                                                                         \
            ::niteraid::testing::fail(__FILE__, __LINE__,                                            \
                std::string(#a " == " #b " (") + std::to_string(_av) + " vs " +                      \
                std::to_string(_bv) + ")");                                                          \
        }                                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                        \
    do {                                                                                             \
        ++::niteraid::testing::stats().checks;                                                       \
        float _av = static_cast<float>(a);                                                           \
        float _bv = static_cast<float>(b);                                                           \
        if (!::niteraid::testing::nearly_equal(_av, _bv, eps)) {                                     \
            ::niteraid::testing::fail(__FILE__, __LINE__,                                            \
                std::string(#a " ~= " #b " (") + std::to_string(_av) + " vs " +                      \
                std::to_string(_bv) + ")");                                                          \
        }                                                                                            \
    } while (0)
