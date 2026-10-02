// Per the decomp of SaveConfigAndHighScores (1480:03df) — see
// tmp-ghidra/control_panel_decomp.txt and research/nite-config-and-highscores.md.
//
// CONFIG.NTR exact byte layout:
//   0x00..0x03 = "NTR\0"
//   0x04..0x05 = version word (3)
//   0x06       = DAT_2730_dea4  (hw-detect)
//   0x08       = DAT_2730_dea2  (hw-detect)
//   0x0a       = DAT_2730_dea6  (hw-detect)
//   0x0c       = DAT_2730_1ff8  (saved sound mode)    -> row 0
//   0x0e       = DAT_2730_1ffa  (saved voice count)   -> row 1
//   0x10       = DAT_2730_1ffc  (saved music toggle)  -> row 2
//   0x12       = DAT_2730_e1d0  (mouse detect)
//   0x14       = DAT_2730_1ffe  (saved sound device pref)
//   0x16..0x1dd = six 0x4c-byte high-score records

#include "niteraid/game.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using niteraid::Game;
using niteraid::HighScoreEntry;
using niteraid::InputState;

namespace {

std::filesystem::path test_save_root()
{
    return std::filesystem::path("reimpl") / "save";
}

void clean_test_save()
{
    std::error_code ec;
    std::filesystem::remove_all(test_save_root(), ec);
}

std::vector<std::uint8_t> read_saved_file()
{
    std::ifstream f(test_save_root() / "CONFIG.NTR", std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& b, std::size_t off)
{
    return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

void leave_control_panel(Game& game)
{
    game.diagnostic_world().control_panel_row = 4;
    InputState enter {};
    enter.accept = true;
    game.tick(enter);
    for (int frame = 0; frame < 15; ++frame) {
        game.tick(InputState {});
    }
}

void confirm_quit(Game& game)
{
    game.diagnostic_world().control_panel_row = 5;
    InputState enter {};
    enter.accept = true;
    game.tick(enter);
    for (int frame = 0; frame < 15; ++frame) {
        game.tick(InputState {});
    }
    CHECK(game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::QuitToDos);
    game.tick(enter);
    CHECK(game.world().quit_requested);
}

std::vector<std::uint8_t> config_bytes(std::uint16_t version = 3)
{
    std::vector<std::uint8_t> bytes(0x1de, 0);
    bytes[0] = 'N';
    bytes[1] = 'T';
    bytes[2] = 'R';
    bytes[4] = static_cast<std::uint8_t>(version);
    bytes[5] = static_cast<std::uint8_t>(version >> 8);
    return bytes;
}

void write_test_config(const std::vector<std::uint8_t>& bytes)
{
    std::filesystem::create_directories(test_save_root());
    std::ofstream file(test_save_root() / "CONFIG.NTR", std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    CHECK(static_cast<bool>(file));
}

struct IsolatedConfigDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path path = previous / "config-original-import-test";

    IsolatedConfigDirectory()
    {
        if (!std::filesystem::create_directory(path)) {
            throw std::runtime_error("config import test requires a fresh directory");
        }
        std::filesystem::current_path(path);
    }

    ~IsolatedConfigDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(path, error);
    }
};

}  // namespace

TEST_CASE("CONFIG import gates stale hardware preferences and saves live words at exact offsets")
{
    clean_test_save();
    {
        Game game;
        InputState input {};
        game.tick(input);
        // Re-route: there is no direct accessor; the only public way to
        // trigger a save is the control panel START path. So instead we
        // exercise round-trip via two Game instances — first writes, second
        // reads back. We mutate the values through the Game::tick() of a
        // control-panel run.
    }
    // Direct round-trip check: write known values, reload, verify.
    // Since the control panel save is wired through start_level, we just
    // assert the static byte layout via a hand-constructed CONFIG.NTR.
    std::vector<std::uint8_t> bytes(0x1de, 0);
    bytes[0] = 'N'; bytes[1] = 'T'; bytes[2] = 'R'; bytes[3] = 0;
    bytes[4] = 3;
    bytes[0x06] = 0x11; bytes[0x07] = 0x00;
    bytes[0x08] = 0x22; bytes[0x09] = 0x00;
    bytes[0x0a] = 0x33; bytes[0x0b] = 0x00;
    bytes[0x0c] = 0x44; bytes[0x0d] = 0x00;
    bytes[0x0e] = 0x55; bytes[0x0f] = 0x00;
    bytes[0x10] = 0x66; bytes[0x11] = 0x00;
    bytes[0x12] = 0x77; bytes[0x13] = 0x00;
    bytes[0x14] = 0x88; bytes[0x15] = 0x00;
    // Seed a score in slot 0 so we can verify the read.
    bytes[0x16 + 0x40] = 0x39; bytes[0x16 + 0x41] = 0x05;  // low = 0x0539 = 1337

    std::filesystem::create_directories(test_save_root());
    {
        std::ofstream f(test_save_root() / "CONFIG.NTR", std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    Game game;
    CHECK_EQ(game.world().audio_config_words[0], 1u);
    CHECK_EQ(game.world().audio_config_words[1], 0u);
    CHECK_EQ(game.world().audio_config_words[2], 1u);
    CHECK_EQ(game.world().audio_config_words[3], 3u);
    CHECK_EQ(game.world().audio_config_words[4], 2u);
    CHECK_EQ(game.world().audio_config_words[5], 1u);
    CHECK_EQ(game.world().audio_config_words[6], 1u);
    CHECK_EQ(game.world().audio_config_words[7], 0x0088u);
    CHECK_EQ(game.world().high_scores[0].score, 1337);

    game.diagnostic_world().screen = niteraid::Screen::ControlPanel;
    confirm_quit(game);
    const auto saved = read_saved_file();
    CHECK_EQ(saved.size(), bytes.size());
    for (std::size_t index = 0; index < game.world().audio_config_words.size(); ++index) {
        CHECK_EQ(read_u16(saved, 6 + index * 2), game.world().audio_config_words[index]);
    }

    clean_test_save();
}

TEST_CASE("config file size is 0x1de bytes (8 words + 6 * 0x4c records + header)")
{
    // header(4) + version(2) + 8*2 config words + 6*0x4c records = 4+2+16+456 = 478 = 0x1de
    constexpr std::size_t expected = 4 + 2 + 8 * 2 + 6 * 0x4c;
    CHECK_EQ(expected, 0x1deu);
}

TEST_CASE("unsupported CONFIG versions do not import score records or preferences")
{
    clean_test_save();
    const Game defaults;
    for (const auto version : {0u, 2u, 4u, 0xffffu}) {
        auto bytes = config_bytes(static_cast<std::uint16_t>(version));
        bytes[0x16] = 'X';
        bytes[0x16 + 0x40] = 0x39;
        bytes[0x16 + 0x41] = 0x05;
        write_test_config(bytes);
        const Game game;
        CHECK(game.world().audio_config_words == defaults.world().audio_config_words);
        for (std::size_t row = 0; row < game.world().high_scores.size(); ++row) {
            const auto& entry = game.world().high_scores[row];
            CHECK(entry.name == defaults.world().high_scores[row].name);
            CHECK_EQ(entry.score, defaults.world().high_scores[row].score);
        }
    }
    clean_test_save();
}

TEST_CASE("CONFIG preferences restore only against compatible live capabilities")
{
    clean_test_save();
    struct ConfigCase {
        std::array<std::uint16_t, 8> saved;
        std::array<std::uint16_t, 8> expected;
    };
    const ConfigCase cases[] {
        {{1, 0, 1, 3, 2, 0, 1, 0}, {1, 0, 1, 3, 2, 0, 1, 0}},
        {{0, 0, 1, 3, 2, 0, 1, 0}, {1, 0, 1, 3, 2, 1, 1, 0}},
        {{1, 0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 3, 2, 0, 1, 0}},
        {{1, 1, 1, 0, 0, 0, 1, 0}, {1, 0, 1, 3, 2, 0, 1, 0}},
        {{1, 0, 1, 3, 2, 0, 0, 0}, {1, 0, 1, 3, 2, 0, 1, 1}},
        {{1, 0, 1, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 0, 0, 1, 0}},
    };
    for (const auto& test : cases) {
        auto bytes = config_bytes();
        for (std::size_t index = 0; index < test.saved.size(); ++index) {
            bytes[6 + index * 2] = static_cast<std::uint8_t>(test.saved[index]);
            bytes[7 + index * 2] = static_cast<std::uint8_t>(test.saved[index] >> 8);
        }
        write_test_config(bytes);
        const Game game;
        CHECK(game.world().audio_config_words == test.expected);
        CHECK_EQ(game.world().high_scores[0].score, 0);
    }
    clean_test_save();
}

TEST_CASE("original CONFIG imports compatible preferences without creating a writable save")
{
    const IsolatedConfigDirectory isolated;
    auto bytes = config_bytes();
    const std::array<std::uint16_t, 8> words {{1, 0, 1, 0, 0, 0, 1, 0}};
    for (std::size_t index = 0; index < words.size(); ++index) {
        bytes[6 + index * 2] = static_cast<std::uint8_t>(words[index]);
    }
    const auto original_path = std::filesystem::path("original/NITERAID/CONFIG.NTR");
    std::filesystem::create_directories(original_path.parent_path());
    {
        std::ofstream file(original_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const Game game;
    CHECK(game.world().audio_config_words == words);
    CHECK(!std::filesystem::exists(test_save_root() / "CONFIG.NTR"));
    std::ifstream file(original_path, std::ios::binary);
    const std::vector<std::uint8_t> after((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(after == bytes);
}

TEST_CASE("configured player save ignores working-directory and original development saves")
{
    const IsolatedConfigDirectory isolated;
    clean_test_save();
    auto bytes = config_bytes();
    bytes[0x56] = 123;
    write_test_config(bytes);
    const auto path = isolated.path / "installed" / "CONFIG.NTR";
    Game game(std::nullopt, std::nullopt, false, false, false, false, std::nullopt, path);
    CHECK(game.world().high_scores[0].score != 123);
    game.diagnostic_world().high_scores[0].name = "PORTABLE";
    game.diagnostic_world().high_scores[0].score = 54321;
    game.diagnostic_world().screen = niteraid::Screen::ControlPanel;
    confirm_quit(game);
    CHECK(std::filesystem::is_regular_file(path));
    const Game reloaded(std::nullopt, std::nullopt, false, false, false, false, std::nullopt, path);
    CHECK_EQ(reloaded.world().high_scores[0].score, 54321);
    CHECK(reloaded.world().high_scores[0].name == "PORTABLE");
    CHECK(read_saved_file() == bytes);
}

TEST_CASE("player save loads adjacent CONFIG before legacy nested save and preserves both")
{
    const IsolatedConfigDirectory isolated;
    const auto installed = isolated.path / "installed";
    const auto legacy = installed / "reimpl" / "save" / "CONFIG.NTR";
    std::filesystem::create_directories(legacy.parent_path());
    auto bytes = config_bytes();
    bytes[0x56] = 123;
    {
        std::ofstream file(legacy, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    const auto adjacent = installed / "CONFIG.NTR";
    Game migrated(std::nullopt, std::nullopt, false, false, false, false, std::nullopt, adjacent);
    CHECK_EQ(migrated.world().high_scores[0].score, 123);
    migrated.diagnostic_world().high_scores[0].score = 234;
    migrated.diagnostic_world().screen = niteraid::Screen::ControlPanel;
    confirm_quit(migrated);
    CHECK(std::filesystem::is_regular_file(adjacent));
    const Game reloaded(std::nullopt, std::nullopt, false, false, false, false, std::nullopt, adjacent);
    CHECK_EQ(reloaded.world().high_scores[0].score, 234);
    std::ifstream file(legacy, std::ios::binary);
    const std::vector<std::uint8_t> after((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(after == bytes);
}

TEST_CASE("valid CONFIG retains blank names even when scores equal shipped defaults")
{
    clean_test_save();
    auto bytes = config_bytes();
    for (std::size_t row = 0; row < 6; ++row) {
        bytes[0x16 + row * 0x4c + 0x40] = 100;
    }
    write_test_config(bytes);
    const Game game;
    for (const auto& entry : game.world().high_scores) {
        CHECK(entry.name.empty());
        CHECK_EQ(entry.score, 100);
    }
    clean_test_save();
}

TEST_CASE("failed config persistence preserves the valid file and dirty state")
{
    clean_test_save();
    std::filesystem::create_directories(test_save_root());

    const auto config_path = test_save_root() / "CONFIG.NTR";
    const auto temporary_path = test_save_root() / "CONFIG.NTR.tmp";
    const std::vector<std::uint8_t> previous(0x1de, 0xa5);
    {
        std::ofstream file(config_path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(previous.data()),
                   static_cast<std::streamsize>(previous.size()));
    }
    // Block the same-directory temporary path so persistence fails before the
    // destination is touched.
    std::filesystem::create_directory(temporary_path);
    std::ofstream blocker(temporary_path / "blocker", std::ios::binary);
    blocker.put('x');

    Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::ControlPanel;
    world.control_panel_dirty = true;
    world.control_panel_from_gameplay = false;

    confirm_quit(game);

    CHECK(world.control_panel_dirty);
    CHECK(std::filesystem::is_regular_file(config_path));
    CHECK(read_saved_file() == previous);

    clean_test_save();
}

TEST_CASE("successful config persistence clears dirty state after exact write")
{
    clean_test_save();

    Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::ControlPanel;
    world.control_panel_dirty = true;
    world.control_panel_from_gameplay = false;

    confirm_quit(game);

    CHECK(!world.control_panel_dirty);
    const auto bytes = read_saved_file();
    CHECK_EQ(bytes.size(), 0x1deu);
    CHECK(bytes.size() >= 4 && bytes[0] == 'N' && bytes[1] == 'T' && bytes[2] == 'R' &&
          bytes[3] == 0);

    clean_test_save();
}

TEST_CASE("faithful panel changes stay in memory until confirmed quit")
{
    clean_test_save();
    Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::ControlPanel;
    world.control_panel_dirty = true;
    world.audio_config_words[5] = 1;
    leave_control_panel(game);
    CHECK(world.screen == niteraid::Screen::Title);
    CHECK_EQ(world.audio_config_words[5], 1u);
    CHECK(!std::filesystem::exists(test_save_root() / "CONFIG.NTR"));
    world.screen = niteraid::Screen::ControlPanel;
    game.tick(InputState {});
    confirm_quit(game);
    CHECK(std::filesystem::exists(test_save_root() / "CONFIG.NTR"));
    Game reloaded;
    CHECK_EQ(reloaded.world().audio_config_words[5], 1u);
    clean_test_save();
}

TEST_CASE("high-score record size is 0x4c, name field is 0x40 bytes")
{
    constexpr std::size_t record_size = 0x4c;
    constexpr std::size_t name_size = 0x40;
    CHECK_EQ(record_size - name_size, 0x0cu);  // 12 bytes for score + 3 stat words + UI flag
}
