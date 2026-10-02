#include "niteraid/ntr_assets.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
struct ArchiveDiscoveryFixture {
    std::filesystem::path root;
    ArchiveDiscoveryFixture()
    {
        root = std::filesystem::temp_directory_path() /
            ("niteraid-archive-discovery-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory already exists");
        root = std::filesystem::canonical(root);
    }
    ~ArchiveDiscoveryFixture()
    {
        std::error_code error;
        // Only the unique, owned temporary fixture is removed.
        std::filesystem::remove_all(root, error);
    }
    std::filesystem::path file(const std::filesystem::path& relative)
    {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path);
        stream << "discovery fixture, not a game archive";
        return path;
    }
};
}

TEST_CASE("adjacent shareware takes priority over working-directory and development archives")
{
    ArchiveDiscoveryFixture fixture;
    const auto shareware = fixture.file("reimpl/build/Release/graphics.nrd");
    fixture.file("GRAPHICS.NTR");
    fixture.file("reimpl/src/app.cpp");
    fixture.file("original/NITERAID/GRAPHICS.NTR");
    CHECK(std::filesystem::equivalent(
        niteraid::find_original_asset_archive(shareware.parent_path() / "game.exe", fixture.root), shareware));
}

TEST_CASE("plain development launches discover resources independently of working directory")
{
    ArchiveDiscoveryFixture fixture;
    fixture.file("reimpl/src/app.cpp");
    const auto graphics = fixture.file("original/NITERAID/GRAPHICS.NTR");
    const auto executable = fixture.root / "reimpl/build/Release/game.exe";
    CHECK(niteraid::find_original_asset_archive(executable, fixture.root / "unrelated",
        niteraid::ArchiveSearchScope::Development) == graphics);
    CHECK(niteraid::find_original_asset_archive(fixture.root / "elsewhere/game.exe", fixture.root,
        niteraid::ArchiveSearchScope::Development) == graphics);
}

TEST_CASE("archive discovery does not borrow an ancestor archive outside a source checkout")
{
    ArchiveDiscoveryFixture fixture;
    fixture.file("original/NITERAID/GRAPHICS.NTR");
    CHECK(niteraid::find_original_asset_archive(
        fixture.root / "release/game.exe", fixture.root / "unrelated").empty());
}

TEST_CASE("working-directory archive is supported when none is adjacent")
{
    ArchiveDiscoveryFixture fixture;
    const auto graphics = fixture.file("working/graphics.ntr");
    CHECK(std::filesystem::equivalent(niteraid::find_original_asset_archive(
        fixture.root / "release/game.exe", graphics.parent_path(),
        niteraid::ArchiveSearchScope::Development), graphics));
}

TEST_CASE("player archive discovery never borrows source-checkout or working-directory data")
{
    ArchiveDiscoveryFixture fixture;
    fixture.file("reimpl/src/app.cpp");
    fixture.file("original/NITERAID/GRAPHICS.NTR");
    fixture.file("working/GRAPHICS.NRD");
    CHECK(niteraid::find_original_asset_archive(
        fixture.root / "release/bin/game.exe", fixture.root / "working").empty());
    CHECK(niteraid::find_original_asset_archive(
        fixture.root / "release/bin/game.exe", fixture.root).empty());
}

TEST_CASE("registered archive resource ids remain unchanged")
{
    using niteraid::OriginalAssetEdition;
    const auto first = niteraid::original_resource_id_for_edition(
        OriginalAssetEdition::Registered, 0x000);
    const auto last = niteraid::original_resource_id_for_edition(
        OriginalAssetEdition::Registered, 0x36a);
    CHECK(first.has_value());
    CHECK(last.has_value());
    CHECK_EQ(*first, 0x000);
    CHECK_EQ(*last, 0x36a);
}

TEST_CASE("shareware archive maps its four-level visual and audio resources")
{
    using niteraid::OriginalAssetEdition;
    const auto mapped = [](std::uint16_t id) {
        return niteraid::original_resource_id_for_edition(
            OriginalAssetEdition::Shareware, id);
    };

    CHECK_EQ(*mapped(0x15a), 0x15a);
    CHECK(!mapped(0x15b).has_value());
    CHECK_EQ(*mapped(0x1be), 0x15b);
    CHECK_EQ(*mapped(0x267), 0x204);
    CHECK(!mapped(0x268).has_value());
    CHECK_EQ(*mapped(0x28d), 0x205);
    CHECK_EQ(*mapped(0x2e0), 0x258);
    CHECK(!mapped(0x2e1).has_value());
    CHECK_EQ(*mapped(0x318), 0x2f5);
    CHECK_EQ(*mapped(0x31b), 0x2f8);
    CHECK(!mapped(0x31c).has_value());
    CHECK_EQ(*mapped(0x31e), 0x2f9);
    CHECK_EQ(*mapped(0x321), 0x2fc);
    CHECK(!mapped(0x322).has_value());
    CHECK_EQ(*mapped(0x327), 0x2fd);
    CHECK_EQ(*mapped(0x348), 0x31e);
    CHECK(!mapped(0x349).has_value());
    CHECK_EQ(*mapped(0x357), 0x31f);
    CHECK_EQ(*mapped(0x36a), 0x332);
}
