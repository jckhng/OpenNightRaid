#include "test_harness.hpp"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace {
class TestWorkspace {
public:
    TestWorkspace() : previous_(std::filesystem::current_path())
    {
        const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
        root_ = parent / ("niteraid-unit-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root_)) throw std::runtime_error("test workspace already exists");
        root_ = std::filesystem::canonical(root_);
        if (root_.parent_path() != parent) throw std::runtime_error("test workspace escaped temporary directory");
        std::filesystem::current_path(root_);
    }
    ~TestWorkspace()
    {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
        if (!error) std::filesystem::remove_all(root_, error);
    }
private:
    std::filesystem::path previous_;
    std::filesystem::path root_;
};
}

int main()
{
    // Save/config tests must never read or delete the caller's game data.
    TestWorkspace workspace;
    auto& reg = niteraid::testing::registry();
    std::printf("Running %zu test cases\n", reg.size());
    for (const auto& tc : reg) {
        std::printf("[ run  ] %s\n", tc.name);
        const int failures_before = niteraid::testing::stats().failures;
        tc.fn();
        const int failures_after = niteraid::testing::stats().failures;
        std::printf("[ %s ] %s\n", failures_after == failures_before ? " ok " : "FAIL",
                    tc.name);
    }
    const auto& s = niteraid::testing::stats();
    std::printf("\n%d checks, %d failures\n", s.checks, s.failures);
    return s.failures == 0 ? 0 : 1;
}
