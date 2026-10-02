#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "niteraid/renderer.hpp"
#include "test_harness.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>

TEST_CASE("text-card screenshots preserve centred letterbox margins")
{
    const auto path = std::filesystem::temp_directory_path() /
        ("niteraid-text-card-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".bmp");
    struct RemoveCapture {
        std::filesystem::path path;
        ~RemoveCapture() { std::filesystem::remove(path); }
    } cleanup {path};
    for (const bool startup : {true, false}) {
        niteraid::Renderer renderer;
        CHECK(renderer.is_interactive());
        if (!renderer.is_interactive()) {
            return;
        }
        if (startup) {
            renderer.show_faithful_startup_card(path.string().c_str());
        } else {
            renderer.show_faithful_exit_card(false, path.string().c_str());
        }
        const std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_LoadBMP(path.string().c_str()), SDL_FreeSurface);
        CHECK(surface != nullptr);
        if (!surface) {
            return;
        }
        const int margin = (surface->w - surface->h * 4 / 3) / 2;
        CHECK(margin > 0);
        const auto color_at = [&](int x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, static_cast<const char*>(surface->pixels) +
                        x * surface->format->BytesPerPixel, surface->format->BytesPerPixel);
            std::uint8_t red = 0, green = 0, blue = 0;
            SDL_GetRGB(pixel, surface->format, &red, &green, &blue);
            return (red << 16) | (green << 8) | blue;
        };
        CHECK_EQ(color_at(margin / 2), 0);
        CHECK_EQ(color_at(surface->w - margin / 2 - 1), 0);
        CHECK_EQ(color_at(margin + 2), 0x0000aa);
        CHECK_EQ(color_at(surface->w - margin - 3), 0x0000aa);
    }
}
