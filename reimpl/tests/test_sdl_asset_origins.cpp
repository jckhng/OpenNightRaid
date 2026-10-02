#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "niteraid/ntr_assets.hpp"
#include "niteraid/renderer.hpp"
#include "test_harness.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {

constexpr std::size_t kArchiveSize = 596269;
constexpr std::uint16_t kResourceCount = 875;
constexpr std::size_t kRecordSize = 14;
constexpr int kBlitterStride = 420;
constexpr std::array<int, 9> kCropX {{7, 6, 6, 3, 1, 3, 6, 6, 7}};
constexpr std::array<int, 9> kShellOffsetX {{22, 25, 30, 39, 44, 39, 30, 25, 22}};
constexpr std::array<int, 6> kFragmentBases {{0x121, 0x129, 0x131, 0x143, 0x14b, 0x153}};

constexpr int synthetic_origin_x(int sprite) { return 2 + sprite % 7; }
constexpr int synthetic_origin_y(int sprite) { return sprite == 0x128 ? 20 : 2 + sprite % 5; }

struct ShellArchiveFixture {
    std::filesystem::path root;
    bool previous_archive_only = niteraid::original_archive_only();

    ShellArchiveFixture()
    {
        if (niteraid::original_asset_archive_available()) {
            throw std::runtime_error("shell test requires an unconfigured asset archive");
        }
        root = std::filesystem::temp_directory_path() /
            ("niteraid-shell-origins-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root)) {
            throw std::runtime_error("shell test directory already exists");
        }
        root = std::filesystem::canonical(root);
        if (root.parent_path() != std::filesystem::canonical(std::filesystem::temp_directory_path())) {
            throw std::runtime_error("shell test directory escaped the temporary root");
        }
        std::vector<std::uint8_t> data(2 + kResourceCount * kRecordSize);
        const auto word = [&data](std::size_t offset, std::uint16_t value) {
            data[offset] = static_cast<std::uint8_t>(value);
            data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        };
        word(0, kResourceCount);
        const auto resource = [&](std::uint16_t id, const std::vector<std::uint8_t>& payload,
                                  std::uint16_t width, std::uint16_t height) {
            const auto record = 2 + id * kRecordSize;
            word(record + 2, static_cast<std::uint16_t>(data.size()));
            word(record + 4, static_cast<std::uint16_t>(data.size() >> 16));
            word(record + 6, static_cast<std::uint16_t>(payload.size()));
            word(record + 8, static_cast<std::uint16_t>(payload.size()));
            word(record + 10, width);
            word(record + 12, height);
            data.insert(data.end(), payload.begin(), payload.end());
        };
        std::vector<std::uint8_t> palette(768);
        palette[3] = 63;
        resource(0x09b, palette, 0, 0);
        resource(0x0e5, std::vector<std::uint8_t>(320 * 200), 320, 200);
        const auto blitter = [&](int sprite, int x, int y) {
            const int displacement = x + y * kBlitterStride;
            // Synthetic blitter: write one red pixel at a nonzero X/Y origin.
            resource(static_cast<std::uint16_t>(sprite),
                     {0xc6, 0x87, static_cast<std::uint8_t>(displacement),
                      static_cast<std::uint8_t>(displacement >> 8), 1, 0xcb}, 1, 1);
        };
        for (int sprite = 0x139; sprite <= 0x142; ++sprite) {
            blitter(sprite, 0, 0);
        }
        for (std::size_t frame = 0; frame < kCropX.size(); ++frame) {
            blitter(0x1be + static_cast<int>(frame), kCropX[frame], static_cast<int>(frame % 3));
        }
        for (const int base : kFragmentBases) {
            for (int frame = 0; frame < 8; ++frame) {
                const int sprite = base + frame;
                if (sprite == 0x128) {
                    const int top = synthetic_origin_x(sprite) + 20 * kBlitterStride;
                    const int score = synthetic_origin_x(sprite) + 24 * kBlitterStride;
                    resource(static_cast<std::uint16_t>(sprite),
                             {0xc6, 0x87, static_cast<std::uint8_t>(top),
                              static_cast<std::uint8_t>(top >> 8), 1,
                              0xc6, 0x87, static_cast<std::uint8_t>(score),
                              static_cast<std::uint8_t>(score >> 8), 1, 0xcb}, 1, 1);
                    continue;
                }
                blitter(sprite, synthetic_origin_x(sprite), synthetic_origin_y(sprite));
            }
        }
        for (int sprite = 0x1c7; sprite <= 0x1cc; ++sprite) {
            blitter(sprite, synthetic_origin_x(sprite), synthetic_origin_y(sprite));
        }
        // A tall mark exposes SDL's partial clipping above the original Y anchor.
        resource(0x1e7, {0xc6, 0x87, 0x00, 0x00, 1,
                         0xc6, 0x87, 0xa4, 0x01, 1,
                         0xc6, 0x87, 0x48, 0x03, 1, 0xcb}, 1, 3);
        data.resize(kArchiveSize);
        const auto path = root / "GRAPHICS.NTR";
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(data.data()), data.size());
        stream.close();
        if (!stream || !niteraid::configure_original_asset_archive(path)) {
            throw std::runtime_error("cannot configure synthetic shell archive");
        }
        niteraid::set_original_archive_only(true);
    }

    ~ShellArchiveFixture()
    {
        niteraid::configure_original_asset_archive(root / "absent.ntr");
        niteraid::set_original_archive_only(previous_archive_only);
        std::error_code error;
        // The canonical target was verified inside the owned temporary root.
        std::filesystem::remove_all(root, error);
    }
};

void expect_red_pixel(niteraid::Renderer& renderer, const niteraid::WorldState& world,
                      const std::filesystem::path& path, int x, int y)
{
    renderer.render(world);
    CHECK(renderer.save_frame_bmp(path.string().c_str()));
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> image(
        SDL_LoadBMP(path.string().c_str()), SDL_FreeSurface);
    CHECK(image != nullptr);
    if (!image) {
        return;
    }
    const int scale = image->w / 320;
    CHECK(scale > 0 && image->w == 320 * scale && image->h == 200 * scale);
    if (scale <= 0) {
        return;
    }
    const auto* pixel = static_cast<const std::uint8_t*>(image->pixels) +
        y * scale * image->pitch + x * scale * image->format->BytesPerPixel;
    Uint32 value = 0;
    std::memcpy(&value, pixel, image->format->BytesPerPixel);
    Uint8 red, green, blue;
    SDL_GetRGB(value, image->format, &red, &green, &blue);
    CHECK_EQ(static_cast<int>(red), 255);
    CHECK_EQ(static_cast<int>(green), 0);
    CHECK_EQ(static_cast<int>(blue), 0);
}

} // namespace

TEST_CASE("SDL shell draws retain compiled crop origins through all nine resources")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    niteraid::Object shell {};
    shell.active = true;
    shell.type = niteraid::ObjectType::EnemyDeath;
    shell.aircraft_variant = niteraid::AircraftVariant::D;
    shell.position = {100.0, 50.0};
    world.objects.push_back(shell);
    const auto path = (fixture.root / "page.bmp").string();
    for (const int direction : {-1, 1}) {
        for (int frame = 0; frame <= 9; ++frame) {
            world.objects[0].direction = direction;
            world.objects[0].frame = frame;
            const int sprite = frame - (frame > 5 ? 1 : 0);
            const int x = 100 + (direction > 0 ? 44 : 28) - kShellOffsetX[sprite] + kCropX[sprite];
            const int y = 50 + sprite % 3;
            expect_red_pixel(renderer, world, path, x, y);
        }
    }
}

TEST_CASE("SDL fragments preserve crop origins and signed high-word placement in every family")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    niteraid::Object fragment {};
    fragment.active = true;
    fragment.type = niteraid::ObjectType::AircraftDebris;
    fragment.has_dropped_payload = true;
    world.objects.push_back(fragment);
    for (const niteraid::WorldPosition position : {
             niteraid::WorldPosition {100.875, 50.875}, {-0.125, 50.875}, {100.875, -0.125}}) {
        world.objects[0].position = position;
        for (const int base : kFragmentBases) {
            world.objects[0].sprite_id = base;
            for (int frame = 0; frame < 8; ++frame) {
                world.objects[0].frame = frame;
                const int sprite = base + frame;
                expect_red_pixel(renderer, world, fixture.root / "page.bmp",
                                 static_cast<int>(std::floor(position.x)) + synthetic_origin_x(sprite),
                                 static_cast<int>(std::floor(position.y)) + synthetic_origin_y(sprite));
            }
        }
    }
}

TEST_CASE("SDL aircraft and bomb explosions retain crop origins at high-word anchors")
{
    enum class Source { Aircraft, ArmedBomb, FallingBomb };
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    niteraid::Object death {};
    death.active = true;
    death.type = niteraid::ObjectType::EnemyDeath;
    death.position = {100.875, 50.875};
    world.objects.push_back(death);
    for (const auto source : {Source::Aircraft, Source::ArmedBomb, Source::FallingBomb}) {
        world.objects[0].aircraft_variant = source == Source::Aircraft
            ? niteraid::AircraftVariant::A : niteraid::AircraftVariant::D;
        world.objects[0].has_dropped_payload = source != Source::Aircraft;
        world.objects[0].finale_drop = source == Source::ArmedBomb;
        for (const int direction : {-1, 1}) {
            world.objects[0].direction = direction;
            const int offset_x = source == Source::ArmedBomb ? -18 :
                                 source == Source::FallingBomb ? -15 : direction > 0 ? 0 : -9;
            const int offset_y = source == Source::ArmedBomb ? -20 :
                                 source == Source::FallingBomb ? -10 : -13;
            for (int frame = 0; frame <= 6; ++frame) {
                world.objects[0].frame = frame;
                const int sprite = 0x1c7 + frame - (frame > 3 ? 1 : 0);
                expect_red_pixel(renderer, world, fixture.root / "page.bmp",
                                 100 + offset_x + synthetic_origin_x(sprite),
                                 50 + offset_y + synthetic_origin_y(sprite));
            }
        }
    }
}

TEST_CASE("SDL converted A carrier draws unmasked rotor selectors four and five")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    niteraid::Object carrier {};
    carrier.active = true;
    carrier.type = niteraid::ObjectType::AircraftDebris;
    carrier.aircraft_variant = niteraid::AircraftVariant::A;
    carrier.position = {100.0, 50.0};
    world.objects.push_back(carrier);

    for (const int direction : {-1, 1}) {
        for (const int selector : {4, 5}) {
            auto& actor = world.objects[0];
            actor.direction = direction;
            actor.frame = (selector + 1) % 4;
            actor.presented_rotor_frame = selector;
            const int sprite = (direction > 0 ? 0x13f : 0x13b) + selector;
            const int origin_x = sprite <= 0x142 ? 0 : synthetic_origin_x(sprite);
            const int origin_y = sprite <= 0x142 ? 0 : synthetic_origin_y(sprite);
            expect_red_pixel(renderer, world, fixture.root / "page.bmp",
                             100 + (direction > 0 ? 0x26 : 0) + origin_x,
                             55 + origin_y);
        }
    }
}

TEST_CASE("SDL faithful projectiles reject out-of-range Y anchors before clipping")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    const auto reference_path = fixture.root / "reference.bmp";
    renderer.render(world);
    CHECK(renderer.save_frame_bmp(reference_path.string().c_str()));
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> reference(
        SDL_LoadBMP(reference_path.string().c_str()), SDL_FreeSurface);
    CHECK(reference != nullptr);
    if (!reference) {
        return;
    }
    niteraid::Object projectile {};
    projectile.active = true;
    projectile.type = niteraid::ObjectType::PlayerProjectile;
    world.objects.push_back(projectile);
    const auto path = fixture.root / "page.bmp";
    for (const double y : {-2.125, -1.125, -0.125, 200.0}) {
        world.objects[0].position = {100.875, y};
        renderer.render(world);
        CHECK(renderer.save_frame_bmp(path.string().c_str()));
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> actual(
            SDL_LoadBMP(path.string().c_str()), SDL_FreeSurface);
        CHECK(actual != nullptr);
        if (!actual) {
            continue;
        }
        CHECK(actual->w == reference->w && actual->h == reference->h &&
              actual->format->format == reference->format->format);
        if (actual->w != reference->w || actual->h != reference->h ||
            actual->format->format != reference->format->format) {
            continue;
        }
        for (int row = 0; row < reference->h; ++row) {
            CHECK(std::memcmp(static_cast<const std::uint8_t*>(actual->pixels) + row * actual->pitch,
                              static_cast<const std::uint8_t*>(reference->pixels) + row * reference->pitch,
                              reference->w * reference->format->BytesPerPixel) == 0);
        }
    }
    for (const double y : {0.0, 0.875, 50.875}) {
        world.objects[0].position = {100.875, y};
        expect_red_pixel(renderer, world, path, 100, static_cast<int>(std::floor(y)) + 2);
    }
}

TEST_CASE("SDL gameplay plate covers fragments entering the HUD")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    niteraid::Object fragment {};
    fragment.active = true;
    fragment.type = niteraid::ObjectType::AircraftDebris;
    fragment.has_dropped_payload = true;
    fragment.sprite_id = kFragmentBases[0];
    fragment.position = {100.875, 182.875};
    fragment.extent = {3.0f, 3.0f};
    world.objects.push_back(fragment);
    const auto path = fixture.root / "page.bmp";
    renderer.render(world);
    CHECK(renderer.save_frame_bmp(path.string().c_str()));
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> image(
        SDL_LoadBMP(path.string().c_str()), SDL_FreeSurface);
    CHECK(image != nullptr);
    if (!image) {
        return;
    }
    const int scale = image->w / 320;
    const int x = 100 + synthetic_origin_x(kFragmentBases[0]);
    const int y = 182 + synthetic_origin_y(kFragmentBases[0]);
    CHECK_EQ(y, 188);
    const auto* pixel = static_cast<const std::uint8_t*>(image->pixels) +
        y * scale * image->pitch + x * scale * image->format->BytesPerPixel;
    Uint32 value = 0;
    std::memcpy(&value, pixel, image->format->BytesPerPixel);
    Uint8 red, green, blue;
    SDL_GetRGB(value, image->format, &red, &green, &blue);
    CHECK_EQ(static_cast<int>(red), 0);
    CHECK_EQ(static_cast<int>(green), 0);
    CHECK_EQ(static_cast<int>(blue), 0);

    world.objects[0].position.y = 181.875;
    expect_red_pixel(renderer, world, path, x, 187);

    // The original's partial HUD redraw leaves row 188 but restores the score cells.
    world.objects[0].frame = 7;
    world.objects[0].position = {55.875, 168.875};
    expect_red_pixel(renderer, world, path, 55 + synthetic_origin_x(0x128), 188);
    renderer.render(world);
    CHECK(renderer.save_frame_bmp(path.string().c_str()));
    image.reset(SDL_LoadBMP(path.string().c_str()));
    CHECK(image != nullptr);
    if (image) {
        const auto* score_pixel = static_cast<const std::uint8_t*>(image->pixels) +
            192 * scale * image->pitch + (55 + synthetic_origin_x(0x128)) * scale *
                image->format->BytesPerPixel;
        std::memcpy(&value, score_pixel, image->format->BytesPerPixel);
        SDL_GetRGB(value, image->format, &red, &green, &blue);
        CHECK_EQ(static_cast<int>(red), 0);
        CHECK_EQ(static_cast<int>(green), 0);
        CHECK_EQ(static_cast<int>(blue), 0);
    }
}

TEST_CASE("SDL armed wave clear draws its original completion panel")
{
    ShellArchiveFixture fixture;
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.screen_fade_frames_remaining = 0;
    world.level_banner_frames_remaining = 0;
    world.transition_armed = true;
    const auto path = fixture.root / "wave-clear.bmp";
    renderer.render(world);
    CHECK(renderer.save_frame_bmp(path.string().c_str()));
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> image(
        SDL_LoadBMP(path.string().c_str()), SDL_FreeSurface);
    CHECK(image != nullptr);
    if (!image) {
        return;
    }
    const int scale = image->w / 320;
    const auto* pixel = static_cast<const std::uint8_t*>(image->pixels) +
        63 * scale * image->pitch + 160 * scale * image->format->BytesPerPixel;
    Uint32 value = 0;
    std::memcpy(&value, pixel, image->format->BytesPerPixel);
    Uint8 red, green, blue;
    SDL_GetRGB(value, image->format, &red, &green, &blue);
    CHECK_EQ(static_cast<int>(red), 186);
    CHECK_EQ(static_cast<int>(green), 190);
    CHECK_EQ(static_cast<int>(blue), 255);

    // The original glyph advances center wave 1's panel at x=83.
    const auto red_at = [&](int x) {
        const auto* sample = static_cast<const std::uint8_t*>(image->pixels) +
            63 * scale * image->pitch + x * scale * image->format->BytesPerPixel;
        Uint32 packed = 0;
        std::memcpy(&packed, sample, image->format->BytesPerPixel);
        Uint8 r, g, b;
        SDL_GetRGB(packed, image->format, &r, &g, &b);
        return static_cast<int>(r);
    };
    CHECK(red_at(82) != 186);
    CHECK_EQ(red_at(83), 186);
}
