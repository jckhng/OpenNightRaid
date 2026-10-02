#include "niteraid/renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "niteraid/audio_internals.hpp"
#include "niteraid/config_internals.hpp"
#include "niteraid/exit_card.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/game_types.hpp"
#include "niteraid/ntr_assets.hpp"
#include "niteraid/presentation.hpp"
#include "niteraid/presenter_timing.hpp"
#include "niteraid/shareware_ending.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "niteraid/finale_particles.hpp"
#include "niteraid/finale_presenter.hpp"
#include "niteraid/pizza_presenter.hpp"
#include "niteraid/helicopter_presenter.hpp"
#include "niteraid/text_mode.hpp"

#if defined(NITERAID_ENABLE_SDL3)
#include <SDL3/SDL.h>
#elif defined(NITERAID_ENABLE_SDL2)
#include <SDL.h>
#endif

namespace niteraid {

namespace {

constexpr int kLogicalWidth = 320;
constexpr int kLogicalHeight = 200;
constexpr int kHudFullRedrawBottom = 187;
constexpr int kExitCardWidth = kTextModeRasterWidth;
constexpr int kExitCardHeight = kTextModeRasterHeight;
#if defined(NITERAID_PORTMASTER)
constexpr int kWindowScale = 2;
#else
constexpr int kWindowScale = 3;
#endif
constexpr int kAimFrameMin = 6;
constexpr int kAimFrameMax = 0x79;
constexpr std::int16_t kGamepadStickDeadzone = 8000;
constexpr std::int16_t kGamepadTriggerThreshold = 8000;
constexpr std::uint64_t kGamepadRepeatDelayMs = 350;
constexpr std::uint64_t kGamepadRepeatIntervalMs = 110;
#if defined(NITERAID_REMAKE)
constexpr const char* kWindowTitle = "Night Raid Remake";
constexpr float kRemakeDebrisSettlementLine = 168.0f;
constexpr float kRemakeDebrisBunkerLeft = 140.0f;
constexpr float kRemakeDebrisBunkerTop = 154.0f;
constexpr float kRemakeDebrisBunkerRight = 184.0f;
constexpr float kRemakeDebrisBunkerBottom = 188.0f;
#else
constexpr const char* kWindowTitle = "Night Raid";
#endif

int fixed_high_word(double value)
{
    return static_cast<int>(std::floor(value));
}


#if defined(NITERAID_REMAKE)
bool remake_debris_overlaps_bunker(double x, double y, float width, float height)
{
    return x < kRemakeDebrisBunkerRight &&
           x + width > kRemakeDebrisBunkerLeft &&
           y < kRemakeDebrisBunkerBottom &&
           y + height > kRemakeDebrisBunkerTop;
}
#endif

constexpr std::array<int, 0x1f> kPlayerAimOriginX {{
    0, 0, 0, 0, 0, 1, 1, 2,
    3, 4, 5, 6, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8,
}};
constexpr std::array<int, 0x1f> kPlayerAimOriginY {{
    9, 9, 8, 7, 6, 5, 4, 3,
    2, 2, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 1, 1, 2, 2, 3,
    4, 5, 6, 7, 8, 9, 9,
}};
constexpr std::array<int, 9> kParatrooperFrameOriginX {{0, 0, 0, 0, 1, 2, 3, 4, 0}};
constexpr std::array<int, 9> kParatrooperFrameOriginY {{0, 3, 7, 11, 12, 10, 9, 11, 2}};
constexpr std::array<int, 10> kParatrooperDeployOriginX {{8, 7, 5, 4, 2, 1, 1, 0, 1, 1}};
constexpr std::array<int, 10> kParatrooperDeployOriginY {{4, 2, 1, 0, 0, 4, 2, 1, 0, 0}};
constexpr std::array<int, 12> kParatrooperLandingOriginX {{5, 4, 4, 3, 3, 2, 1, 1, 1, 0, 0, 0}};
constexpr std::array<int, 12> kParatrooperLandingOriginY {{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
constexpr int kLandingOverlayFirstY = 0x98;
constexpr int kLandingOverlayGroundY = 0xb3;
constexpr int kLandingOverlayInitialXOffset = 0x0b;
constexpr std::array<int, 4> kAssaultLeftOriginX {{1, 0, 1, 1}};
constexpr std::array<int, 4> kAssaultLeftOriginY {{3, 3, 3, 3}};
constexpr std::array<int, 4> kAssaultRightOriginX {{2, 0, 0, 0}};
constexpr std::array<int, 4> kAssaultRightOriginY {{3, 3, 3, 3}};
constexpr std::array<int, 3> kOverrunAssaultResidueX {{174, 170, 163}};
constexpr std::array<int, 3> kOverrunAssaultResidueY {{175, 177, 175}};
constexpr std::array<int, 3> kBunkerSpecialOriginX {{6, 0, 1}};
constexpr std::array<int, 3> kBunkerSpecialOriginY {{0, 0, 0}};
constexpr std::array<int, 3> kSurvivorUfoGlintOriginX {{1, 2, 0}};
constexpr std::array<int, 3> kSurvivorUfoGlintOriginY {{5, 5, 5}};
constexpr std::array<int, 6> kSurvivorUfoOriginX {{11, 9, 6, 4, 2, 0}};
constexpr std::array<int, 6> kSurvivorUfoOriginY {{4, 3, 3, 2, 1, 3}};
constexpr std::array<int, 17> kSurvivorJokeActorOriginY {{
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 7,
}};
constexpr std::array<int, 4> kNoSurvivorSegmentOriginX {{1, 0, 0, 0}};
constexpr std::array<int, 4> kNoSurvivorSegmentOriginY {{0, 3, 3, 3}};
constexpr std::array<int, 15> kTerminalFlagOriginX {{
    12, 12, 12, 12, 12, 12, 11, 11, 11, 6, 7, 8, 9, 10, 12,
}};
constexpr std::array<int, 15> kTerminalFlagOriginY {{
    7, 5, 3, 3, 4, 3, 3, 2, 3, 3, 3, 3, 2, 2, 3,
}};
// 0aa2 selects this nine-word table through ES = 2595.
constexpr std::array<int, 9> kAircraftShellDrawOffsetX {{
    0x16, 0x19, 0x1e, 0x27, 0x2c, 0x27, 0x1e, 0x19, 0x16}};
constexpr int kLeftAircraftBodyOriginX = 1;
#if defined(NITERAID_ENABLE_SDL2)
template <std::size_t Size>
void destroy_textures(const std::array<void*, Size>& textures)
{
    for (auto* texture : textures) {
        if (texture != nullptr) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(texture));
        }
    }
}

std::string sprite_asset_path(int sprite_id)
{
    char buffer[64] {};
    std::snprintf(buffer, sizeof(buffer), "assets/sprites/sprite_%03x.bmp", sprite_id);
    return buffer;
}

std::string font_asset_path(int sprite_id)
{
    char buffer[64] {};
    std::snprintf(buffer, sizeof(buffer), "assets/font/sprite_%03x.bmp", sprite_id);
    return buffer;
}

std::string fullscreen_sequence_asset_path(std::string_view stem, int index)
{
    char buffer[96] {};
    std::snprintf(buffer, sizeof(buffer), "assets/fullscreen/%.*s_%02d.bmp",
                  static_cast<int>(stem.size()), stem.data(), index);
    return buffer;
}

std::vector<std::filesystem::path> asset_path_candidates(std::string_view relative_path)
{
    if (original_archive_only()) {
        return {};
    }
    const auto relative = std::filesystem::path(relative_path);
    std::vector<std::filesystem::path> candidates {
        relative,
        std::filesystem::path("reimpl") / relative,
    };

    char* base_path_raw = SDL_GetBasePath();
    if (base_path_raw != nullptr) {
        const auto base_path = std::filesystem::path(base_path_raw);
        SDL_free(base_path_raw);
        candidates.push_back(base_path / relative);
        candidates.push_back(base_path / ".." / ".." / relative);
        candidates.push_back(base_path / ".." / ".." / "reimpl" / relative);
    }

    return candidates;
}

SDL_Texture* create_indexed_texture(SDL_Renderer* renderer,
                                    const IndexedAssetImage& image)
{
    auto* surface = SDL_CreateRGBSurfaceWithFormat(
        0, image.width, image.height, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr || SDL_LockSurface(surface) != 0) {
        if (surface != nullptr) {
            SDL_FreeSurface(surface);
        }
        return nullptr;
    }

    auto* pixels = static_cast<std::uint32_t*>(surface->pixels);
    const int pitch = surface->pitch / static_cast<int>(sizeof(std::uint32_t));
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto index = image.pixels[static_cast<std::size_t>(y) * image.width + x];
            if (index < 0) {
                pixels[y * pitch + x] = SDL_MapRGBA(surface->format, 255, 0, 255, 255);
                continue;
            }
            const auto palette_offset = static_cast<std::size_t>(index) * 3;
            const auto expand = [&image](std::size_t offset) {
                return static_cast<std::uint8_t>(image.palette[offset] * 255u / 63u);
            };
            pixels[y * pitch + x] = SDL_MapRGBA(
                surface->format,
                expand(palette_offset), expand(palette_offset + 1),
                expand(palette_offset + 2), 255);
        }
    }
    SDL_UnlockSurface(surface);
    auto* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

SDL_Texture* load_bmp_texture(SDL_Renderer* renderer, std::string_view relative_path, bool use_magenta_key = false)
{
    SDL_Surface* surface = nullptr;
    for (const auto& path : asset_path_candidates(relative_path)) {
        surface = SDL_LoadBMP(path.string().c_str());
        if (surface != nullptr) {
            break;
        }
    }

    if (surface == nullptr) {
        if (auto image = load_original_image(relative_path)) {
            surface = SDL_CreateRGBSurfaceWithFormat(
                0, image->width, image->height, 32, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr && SDL_LockSurface(surface) == 0) {
                auto* pixels = static_cast<std::uint32_t*>(surface->pixels);
                const int pitch = surface->pitch / static_cast<int>(sizeof(std::uint32_t));
                for (int y = 0; y < image->height; ++y) {
                    for (int x = 0; x < image->width; ++x) {
                        const auto index = image->pixels[static_cast<std::size_t>(y) * image->width + x];
                        if (index < 0) {
                            pixels[y * pitch + x] = SDL_MapRGBA(surface->format, 255, 0, 255, 255);
                            continue;
                        }
                        const auto palette_offset = static_cast<std::size_t>(index) * 3;
                        const auto expand = [&image](std::size_t offset) {
                            return static_cast<std::uint8_t>(
                                image->palette[offset] * 255u / 63u);
                        };
                        pixels[y * pitch + x] = SDL_MapRGBA(
                            surface->format,
                            expand(palette_offset), expand(palette_offset + 1),
                            expand(palette_offset + 2), 255);
                    }
                }
                SDL_UnlockSurface(surface);
            } else if (surface != nullptr) {
                SDL_FreeSurface(surface);
                surface = nullptr;
            }
        }
    }

    if (surface == nullptr) {
        return nullptr;
    }

    if (use_magenta_key) {
        const auto key = SDL_MapRGB(surface->format, 255, 0, 255);
        SDL_SetColorKey(surface, SDL_TRUE, key);
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

SDL_Texture* load_font_foreground_mask_texture(SDL_Renderer* renderer, std::string_view relative_path)
{
    SDL_Surface* source = nullptr;
    for (const auto& path : asset_path_candidates(relative_path)) {
        source = SDL_LoadBMP(path.string().c_str());
        if (source != nullptr) {
            break;
        }
    }
    if (source == nullptr) {
        if (auto image = load_original_image(relative_path)) {
            source = SDL_CreateRGBSurfaceWithFormat(
                0, image->width, image->height, 32, SDL_PIXELFORMAT_RGBA32);
            if (source != nullptr && SDL_LockSurface(source) == 0) {
                auto* pixels = static_cast<std::uint32_t*>(source->pixels);
                const int pitch = source->pitch / static_cast<int>(sizeof(std::uint32_t));
                for (int y = 0; y < image->height; ++y) {
                    for (int x = 0; x < image->width; ++x) {
                        const auto index = image->pixels[static_cast<std::size_t>(y) * image->width + x];
                        if (index < 0) {
                            pixels[y * pitch + x] = SDL_MapRGBA(source->format, 255, 0, 255, 255);
                            continue;
                        }
                        const auto offset = static_cast<std::size_t>(index) * 3;
                        pixels[y * pitch + x] = SDL_MapRGBA(
                            source->format,
                            static_cast<std::uint8_t>(image->palette[offset] * 255u / 63u),
                            static_cast<std::uint8_t>(image->palette[offset + 1] * 255u / 63u),
                            static_cast<std::uint8_t>(image->palette[offset + 2] * 255u / 63u),
                            255);
                    }
                }
                SDL_UnlockSurface(source);
            } else if (source != nullptr) {
                SDL_FreeSurface(source);
                source = nullptr;
            }
        }
    }
    if (source == nullptr) {
        return nullptr;
    }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(source);
    if (converted == nullptr) {
        return nullptr;
    }

    SDL_LockSurface(converted);
    auto* pixels = static_cast<std::uint32_t*>(converted->pixels);
    const int pitch_pixels = converted->pitch / static_cast<int>(sizeof(std::uint32_t));
    for (int y = 0; y < converted->h; ++y) {
        for (int x = 0; x < converted->w; ++x) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            auto& pixel = pixels[y * pitch_pixels + x];
            SDL_GetRGBA(pixel, converted->format, &r, &g, &b, &a);
            const bool foreground = r > 200 && g < 80 && b < 80;
            pixel = SDL_MapRGBA(converted->format, 255, 255, 255, foreground ? 255 : 0);
        }
    }
    SDL_UnlockSurface(converted);

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, converted);
    SDL_FreeSurface(converted);
    if (texture != nullptr) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}

void render_texture(SDL_Renderer* renderer, void* texture)
{
    if (texture == nullptr) {
        return;
    }

    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(texture), nullptr, nullptr);
}

void render_transition_texture_or_clear(SDL_Renderer* renderer, void* texture)
{
    if (texture != nullptr) {
        render_texture(renderer, texture);
        return;
    }

    SDL_SetRenderDrawColor(renderer, 8, 12, 24, 255);
    SDL_RenderClear(renderer);
}

void render_texture_at(SDL_Renderer* renderer, void* texture, const SDL_Rect& destination, bool flip_horizontal)
{
    if (texture == nullptr) {
        return;
    }

    const auto flip = flip_horizontal ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(renderer, static_cast<SDL_Texture*>(texture), nullptr, &destination, 0.0, nullptr, flip);
}

void render_moving_texture_at(SDL_Renderer* renderer, void* texture,
                              const SDL_Rect& destination, const WorldPosition& anchor,
                              bool modern_presentation,
                              bool flip_horizontal = false)
{
#if defined(NITERAID_REMAKE)
    if (modern_presentation) {
        if (texture == nullptr) {
            return;
        }

        const double subpixel_x = anchor.x - std::round(anchor.x);
        const double subpixel_y = anchor.y - std::round(anchor.y);
        SDL_FRect precise_destination {
            static_cast<float>(destination.x + subpixel_x),
            static_cast<float>(destination.y + subpixel_y),
            static_cast<float>(destination.w),
            static_cast<float>(destination.h),
        };
        const auto flip = flip_horizontal ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyExF(renderer, static_cast<SDL_Texture*>(texture), nullptr,
                          &precise_destination, 0.0, nullptr, flip);
        return;
    }
#endif
    (void)anchor;
    render_texture_at(renderer, texture, destination, flip_horizontal);
}

void render_texture_at(SDL_Renderer* renderer, void* texture, int x, int y)
{
    if (texture == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr, &width, &height);
    SDL_Rect destination {x, y, width, height};
    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(texture), nullptr, &destination);
}

#if defined(NITERAID_REMAKE)
void draw_additive_glow(SDL_Renderer* renderer, double x, double y,
                        float radius, SDL_Color color)
{
    constexpr std::array<float, 5> kLayerAlpha {{0.16f, 0.25f, 0.38f, 0.58f, 1.0f}};
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

    for (int layer = 5; layer >= 1; --layer) {
        const float layer_radius = radius * static_cast<float>(layer) / 5.0f;
        const float vertical_radius = std::max(1.0f, layer_radius * 0.65f);
        const auto alpha = static_cast<std::uint8_t>(
            static_cast<float>(color.a) *
            kLayerAlpha[static_cast<std::size_t>(5 - layer)]);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
        const int scanline_radius = static_cast<int>(std::ceil(vertical_radius));
        for (int y_offset = -scanline_radius; y_offset <= scanline_radius; ++y_offset) {
            const float normalized_y = static_cast<float>(y_offset) / vertical_radius;
            if (std::abs(normalized_y) > 1.0f) {
                continue;
            }
            const float half_width =
                layer_radius * std::sqrt(1.0f - normalized_y * normalized_y);
            SDL_FRect scanline {
                static_cast<float>(x - half_width),
                static_cast<float>(y + y_offset),
                half_width * 2.0f + 1.0f,
                1.0f,
            };
            SDL_RenderFillRectF(renderer, &scanline);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, previous_blend);
}

float remake_light_pulse(std::uint32_t frame_tick, std::uint32_t phase)
{
    const float cycle = static_cast<float>(frame_tick + phase * 13U) * 0.11f;
    return 0.80f + 0.20f * (0.5f + 0.5f * std::sin(cycle));
}

void draw_remake_aircraft_light(SDL_Renderer* renderer, const SDL_Rect& body,
                                 const Object& object, std::uint32_t frame_tick)
{
    const float pulse = remake_light_pulse(frame_tick, object.timer);
    const float center_x = static_cast<float>(body.x) + static_cast<float>(body.w) * 0.5f;
    const float center_y = static_cast<float>(body.y) + static_cast<float>(body.h) * 0.55f;
    draw_additive_glow(
        renderer, center_x, center_y,
        std::max(10.0f, static_cast<float>(body.w) * 0.30f),
        SDL_Color {255, 158, 74, static_cast<std::uint8_t>(24.0f * pulse)});

    const float engine_x = object.direction > 0
                               ? static_cast<float>(body.x + 4)
                               : static_cast<float>(body.x + body.w - 4);
    draw_additive_glow(
        renderer, engine_x, center_y, 4.0f + pulse * 1.5f,
        SDL_Color {255, 194, 96, static_cast<std::uint8_t>(72.0f * pulse)});
}

void draw_remake_paratrooper_light(SDL_Renderer* renderer, const SDL_Rect& body,
                                   const Object& object, std::uint32_t frame_tick)
{
    const float pulse = remake_light_pulse(frame_tick, object.timer + object.frame);
    const float center_x = static_cast<float>(body.x) + static_cast<float>(body.w) * 0.5f;
    const float center_y = static_cast<float>(body.y) + static_cast<float>(body.h) * 0.55f;
    draw_additive_glow(
        renderer, center_x, center_y,
        std::max(5.0f, static_cast<float>(std::max(body.w, body.h)) * 0.40f),
        SDL_Color {255, 174, 92, static_cast<std::uint8_t>(28.0f * pulse)});
}

void draw_remake_effects(SDL_Renderer* renderer, const WorldState& world,
                          const std::array<void*, 6>& enemy_death_textures,
                          const WorldPosition& muzzle_position, float muzzle_afterglow_seconds)
{
    constexpr float kMuzzleFadeSeconds =
        8.0f / static_cast<float>(kEnhancedPresentationRateHz);
    if (muzzle_afterglow_seconds > 0.0f) {
        const float strength = std::clamp(
            muzzle_afterglow_seconds / kMuzzleFadeSeconds, 0.18f, 1.0f);
        draw_additive_glow(renderer,
                           muzzle_position.x,
                           muzzle_position.y,
                           8.0f + 7.0f * strength,
                           SDL_Color {
                               255,
                               182,
                               48,
                               static_cast<std::uint8_t>(230.0f * strength),
                           });
    }

    for (const auto& object : world.objects) {
        if (!object.active || object.pending_destroy ||
            object.type != ObjectType::EnemyDeath) {
            continue;
        }
        const int frame = std::clamp(object.frame - (object.frame > 3 ? 1 : 0), 0, 5);
        void* texture = enemy_death_textures[static_cast<std::size_t>(frame)];
        int texture_width = 12;
        int texture_height = 12;
        if (texture != nullptr) {
            SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                             &texture_width, &texture_height);
        }
        const int draw_x_offset = object.has_dropped_payload
                                      ? (object.finale_drop ? -18 : -15)
                                      : (object.direction > 0 ? 0 : -9);
        const int draw_y_offset = object.has_dropped_payload
                                      ? (object.finale_drop ? -20 : -10)
                                      : -13;
        const double center_x = object.position.x + static_cast<float>(draw_x_offset) +
                               static_cast<float>(texture_width) * 0.5f;
        const double center_y = object.position.y + static_cast<float>(draw_y_offset) +
                               static_cast<float>(texture_height) * 0.5f;
        const float artwork_radius =
            static_cast<float>(std::max(texture_width, texture_height)) * 0.90f;
        const float radius = std::max(24.0f, artwork_radius) *
                             std::max(0.62f, 1.0f - static_cast<float>(object.frame) * 0.07f);
        draw_additive_glow(renderer,
                           center_x,
                           center_y,
                           radius,
                           SDL_Color {255, 68, 24, 150});
    }
}
#endif

template <std::size_t Count>
std::size_t attract_overlay_frame_index(int elapsed_frames)
{
    const int stepped = std::max(0, elapsed_frames) / kAttractOverlayDelayFrames;
    return static_cast<std::size_t>(std::min(stepped, static_cast<int>(Count) - 1));
}
#endif

float aim_angle_radians(int frame)
{
    const auto clamped = std::clamp(frame, kAimFrameMin, kAimFrameMax);
    const auto t = static_cast<float>(clamped - kAimFrameMin) /
                   static_cast<float>(kAimFrameMax - kAimFrameMin);
    const auto degrees = 160.0f - (t * 140.0f);
    return degrees * 3.14159265f / 180.0f;
}

#if defined(NITERAID_ENABLE_SDL2) || defined(NITERAID_ENABLE_SDL3)
MenuKeyAction menu_action_for_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:
    case SDLK_LEFT:
        return MenuKeyAction::Up;
    case SDLK_DOWN:
    case SDLK_RIGHT:
        return MenuKeyAction::Down;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return MenuKeyAction::Enter;
    case SDLK_SPACE:
        return MenuKeyAction::Space;
    case SDLK_ESCAPE:
        return MenuKeyAction::Escape;
#if defined(NITERAID_ENABLE_SDL3)
    case SDLK_Y:
        return MenuKeyAction::Confirm;
    case SDLK_N:
        return MenuKeyAction::Decline;
#else
    case SDLK_y:
        return MenuKeyAction::Confirm;
    case SDLK_n:
        return MenuKeyAction::Decline;
#endif
    default:
        return MenuKeyAction::Ignored;
    }
}
#endif

#if defined(NITERAID_ENABLE_SDL2)
using FontTextures = std::array<void*, 256>;
using ControlPanelGlyphTextures = std::array<void*, 0x26>;

int font_sprite_for_char(char c)
{
    if ('0' <= c && c <= '9') {
        return 0x7e + (c - '0');
    }
    if ('A' <= c && c <= 'Z') {
        return 0x53 + (c - 'A');
    }
    if ('a' <= c && c <= 'z') {
        return 0x9e + (c - 'a');
    }
    switch (c) {
    case '.':
        return 0x75;
    case ',':
        return 0x7a;
    case '(':
        return 0x76;
    case ')':
        return 0x77;
    case '-':
        return 0x92;
    case '!':
        return 0x6f;
    case ':':
        return 0x89;
    case '/':
        return 0x7d;
    case '\'':
        return 0x75;
    default:
        return 0;
    }
}

int text_width(const FontTextures& font_textures, std::string_view text, int scale)
{
    int width = 0;
    for (char c : text) {
        if (c == ' ') {
            width += 4 * scale;
            continue;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        int texture_width = 0;
        int texture_height = 0;
        if (texture != nullptr &&
            SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height) == 0) {
            width += texture_width * scale;
        } else {
            width += 6 * scale;
        }
    }
    return width;
}

int text_width_with_space(const FontTextures& font_textures, std::string_view text, int scale,
                          int space_width)
{
    int width = 0;
    for (char c : text) {
        if (c == ' ') {
            width += space_width * scale;
            continue;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        int texture_width = 0;
        int texture_height = 0;
        if (texture != nullptr &&
            SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height) == 0) {
            width += texture_width * scale;
        } else {
            width += 6 * scale;
        }
    }
    return width;
}

void draw_text(SDL_Renderer* renderer, const FontTextures& font_textures,
               int x, int y, std::string_view text, int scale)
{
    int cursor_x = x;
    for (char c : text) {
        if (c == ' ') {
            cursor_x += 4 * scale;
            continue;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        if (texture == nullptr) {
            cursor_x += 6 * scale;
            continue;
        }
        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
        SDL_Rect destination {cursor_x, y, texture_width * scale, texture_height * scale};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        cursor_x += texture_width * scale;
    }
}

void draw_text_tinted(SDL_Renderer* renderer, const FontTextures& font_textures,
                      int x, int y, std::string_view text, int scale, SDL_Color color)
{
    int cursor_x = x;
    for (char c : text) {
        if (c == ' ') {
            cursor_x += 4 * scale;
            continue;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        if (texture == nullptr) {
            cursor_x += 6 * scale;
            continue;
        }
        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);
        SDL_Rect destination {cursor_x, y, texture_width * scale, texture_height * scale};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
        cursor_x += texture_width * scale;
    }
}

void draw_text_tinted_with_space(SDL_Renderer* renderer, const FontTextures& font_textures,
                                 int x, int y, std::string_view text, int scale,
                                 SDL_Color color, int space_width)
{
    int cursor_x = x;
    for (char c : text) {
        if (c == ' ') {
            cursor_x += space_width * scale;
            continue;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        if (texture == nullptr) {
            cursor_x += 6 * scale;
            continue;
        }
        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);
        const int origin_y = sprite_id == 0x7a ? 6 * scale : 0;
        SDL_Rect destination {cursor_x, y + origin_y,
                              texture_width * scale, texture_height * scale};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
        cursor_x += texture_width * scale;
    }
}

void draw_masked_text(SDL_Renderer* renderer, const FontTextures& font_textures,
                      int x, int y, std::string_view text, int scale, SDL_Color color)
{
    draw_text_tinted(renderer, font_textures, x, y, text, scale, color);
}

void draw_digit_cells(SDL_Renderer* renderer, const FontTextures& font_textures,
                      std::string_view text, std::initializer_list<SDL_Rect> cells,
                      SDL_Color color)
{
    auto cell_it = cells.begin();
    for (char c : text) {
        if (cell_it == cells.end()) {
            return;
        }
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        if (texture != nullptr) {
            int texture_width = 0;
            int texture_height = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
            SDL_Rect destination {
                cell_it->x + (cell_it->w - texture_width) / 2,
                cell_it->y + (cell_it->h - texture_height) / 2,
                texture_width,
                texture_height,
            };
            SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(texture, color.a);
            SDL_RenderCopy(renderer, texture, nullptr, &destination);
            SDL_SetTextureColorMod(texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(texture, 255);
        }
        ++cell_it;
    }
}

template <std::size_t CellCount>
void draw_digit_cells_right_aligned(SDL_Renderer* renderer, const FontTextures& font_textures,
                                    std::string_view text,
                                    const std::array<SDL_Rect, CellCount>& cells,
                                    SDL_Color color)
{
    const auto first_digit = text.find_first_of("0123456789");
    if (first_digit == std::string_view::npos) {
        return;
    }

    text.remove_prefix(first_digit);
    const std::size_t count = std::min<std::size_t>(text.size(), cells.size());
    const std::size_t first_cell = cells.size() - count;
    for (std::size_t index = 0; index < count; ++index) {
        const char c = text[text.size() - count + index];
        const int sprite_id = font_sprite_for_char(c);
        auto* texture = sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
        if (texture == nullptr) {
            continue;
        }

        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
        const auto& cell = cells[first_cell + index];
        SDL_Rect destination {
            cell.x + (cell.w - texture_width) / 2,
            cell.y + (cell.h - texture_height) / 2,
            texture_width,
            texture_height,
        };
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
    }
}

template <std::size_t CellCount>
void draw_hud_digits_right_aligned(SDL_Renderer* renderer, std::string_view text,
                                   const std::array<SDL_Rect, CellCount>& cells,
                                   const std::array<void*, 10>& textures,
                                   const std::array<int, 10>& origin_x,
                                   const std::array<int, 10>& origin_y)
{
    const auto first_digit = text.find_first_of("0123456789");
    if (first_digit == std::string_view::npos) {
        return;
    }

    text.remove_prefix(first_digit);
    const std::size_t count = std::min<std::size_t>(text.size(), cells.size());
    const std::size_t first_cell = cells.size() - count;

    for (std::size_t index = 0; index < count; ++index) {
        const auto digit = static_cast<std::size_t>(text[text.size() - count + index] - '0');
        if (digit >= textures.size() || textures[digit] == nullptr) continue;
        auto* texture = static_cast<SDL_Texture*>(textures[digit]);
        int width = 0;
        int height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
        const auto& cell = cells[first_cell + index];
        // 16c8:2ba6 uses fixed nine-pixel cells and blitter 0x210 + digit*6.
        SDL_Rect destination {cell.x + origin_x[digit], 192 + origin_y[digit], width, height};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
    }
}

bool bunker_door_is_open(const WorldState& world)
{
    // Native overrun uses the recovered door actor, not procedural door fills.
    if (world.bunker_native_choreography) return false;
    if (world.bunker_explosion_frames_remaining > 0) {
        return false;
    }
    return world.bunker_assault_entries > 0 ||
           world.bunker_special_effect_frames_remaining != 0 ||
           world.bunker_white_flag;
}

bool overrun_intermission_banner_is_visible(const WorldState& world)
{
    if (world.overrun_interrupt_residue) {
        return true;
    }
    if (world.screen == Screen::GameOver) {
        return world.bunker_white_flag &&
               world.frame_tick - world.transition_started_tick <
                   presenter_timing::terminal_audio_timing(world).retained;
    }
    if (world.bunker_assault_entries >= 3 && world.bunker_explosion_frames_remaining > 0) {
        return false;
    }
    return world.bunker_assault_active ||
           world.gameplay_state == GameplayState::ScriptedSequence ||
           world.bunker_explosion_frames_remaining > 0 ||
           world.bunker_white_flag;
}

bool game_over_banner_is_visible(const WorldState& world)
{
    if (world.screen != Screen::GameOver) {
        return false;
    }
    if (world.overrun_interrupt_residue) {
        return false;
    }
    return !overrun_intermission_banner_is_visible(world);
}

bool terminal_game_over_presenter_is_active(const WorldState& world)
{
    return presenter_timing::terminal_presenter_frame(world).has_value();
}

bool bunker_special_actor_is_visible(const WorldState& world)
{
    return world.bunker_special_effect_frames_remaining != 0;
}

bool bunker_door_spill_is_visible(const WorldState& world)
{
    if (world.bunker_explosion_frames_remaining > 0) {
        return false;
    }
    if (world.overrun_interrupt_residue && world.bunker_assault_entries > 0) {
        return true;
    }
    if (!(world.bunker_assault_active || world.gameplay_state == GameplayState::ScriptedSequence)) {
        return false;
    }
    if (world.bunker_assault_entries >= 2 || world.bunker_special_effect_frames_remaining != 0) {
        return true;
    }
    if (world.bunker_assault_entries >= 3 && world.bunker_explosion_frames_remaining > 10) {
        return true;
    }
    if (world.bunker_assault_entries != 1) {
        return false;
    }
    return std::any_of(world.objects.begin(), world.objects.end(), [](const Object& object) {
        return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader &&
               object.position.x >= 120.0f && object.position.x <= 155.0f;
    });
}

bool bunker_assault_entrant_is_in_doorway(const WorldState& world)
{
    return std::any_of(world.objects.begin(), world.objects.end(), [](const Object& object) {
        return object.active && !object.pending_destroy &&
               object.type == ObjectType::LandedInvader &&
               internals::bunker_assault_actor_draws_in_doorway(object);
    });
}

int overrun_assault_residue_index(const WorldState& world)
{
    if (!world.bunker_assault_active || world.bunker_assault_entries != 2 ||
        world.bunker_special_effect_frames_remaining >= 0 ||
        world.bunker_explosion_frames_remaining != 0) {
        return -1;
    }

    for (const auto& object : world.objects) {
        if (!object.active || object.pending_destroy || object.type != ObjectType::LandedInvader) {
            continue;
        }
        if (object.frame >= 35 && object.position.x <= 162.0f) {
            return 2;
        }
    }
    return -1;
}

int overrun_assault_visual_frame(const WorldState& world, const Object& object)
{
    if (world.bunker_native_choreography) return object.frame % 4;
    const bool captured_phase_advance =
        world.bunker_assault_active && world.bunker_assault_entries >= 2 &&
        ((object.frame >= 22 && object.frame < 27) || object.frame >= 35);
    return std::clamp((object.frame + (captured_phase_advance ? 1 : 0)) % 4, 0, 3);
}

int overrun_assault_visual_x_offset(const WorldState& world, const Object& object)
{
    if (world.bunker_native_choreography) return 0;
    if (!world.bunker_assault_active || world.bunker_assault_entries < 2) {
        return 0;
    }
    if (object.frame >= 22 && object.frame < 27) {
        return -1;
    }
    if (object.frame >= 27 && object.frame < 35) {
        return 1;
    }
    return 0;
}

void draw_open_bunker_door(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 236, 188, 64, 255);
    SDL_Rect open_door {163, 172, 5, 7};
    SDL_RenderFillRect(renderer, &open_door);
}

void draw_bunker_door_spill_pattern(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 230, 218, 0, 255);
    SDL_Rect door {163, 173, 4, 6};
    SDL_RenderFillRect(renderer, &door);

    SDL_SetRenderDrawColor(renderer, 182, 174, 0, 255);
    SDL_Rect left_edge_179 {162, 179, 1, 1};
    SDL_RenderFillRect(renderer, &left_edge_179);
    SDL_Rect right_edge_179 {167, 179, 1, 1};
    SDL_RenderFillRect(renderer, &right_edge_179);
    SDL_Rect left_edge_180 {161, 180, 1, 1};
    SDL_RenderFillRect(renderer, &left_edge_180);

    SDL_SetRenderDrawColor(renderer, 206, 198, 0, 255);
    const std::array<SDL_Rect, 4> spill_rows {{
        SDL_Rect {163, 179, 4, 1},
        SDL_Rect {162, 180, 7, 1},
        SDL_Rect {161, 181, 9, 1},
        SDL_Rect {160, 182, 11, 1},
    }};
    for (const auto& row : spill_rows) {
        SDL_RenderFillRect(renderer, &row);
    }
    SDL_Rect lower_row {159, 183, 13, 1};
    SDL_RenderFillRect(renderer, &lower_row);
    for (int x = 158; x <= 172; x += 2) {
        SDL_Rect pixel {x, 184, 1, 1};
        SDL_RenderFillRect(renderer, &pixel);
    }
}

void draw_bunker_base(SDL_Renderer* renderer, void* player_base_texture)
{
    if (player_base_texture != nullptr) {
        SDL_Rect base {0x94, 0xa6, 27, 13};
        render_texture_at(renderer, player_base_texture, base, false);
        return;
    }

    SDL_SetRenderDrawColor(renderer, 96, 150, 96, 255);
    SDL_Rect bunker {0x94, 0xa6, 26, 12};
    SDL_RenderFillRect(renderer, &bunker);
    SDL_SetRenderDrawColor(renderer, 52, 36, 20, 255);
    SDL_Rect bunker_door {0x9f, 0xab, 12, 9};
    SDL_RenderFillRect(renderer, &bunker_door);
}

void draw_texture_if_available(SDL_Renderer* renderer, void* texture, int x, int y)
{
    if (texture == nullptr) {
        return;
    }
    render_texture_at(renderer, texture, x, y);
}

void draw_moving_texture_if_available(SDL_Renderer* renderer, void* texture,
                                      double x, double y, bool modern_presentation)
{
    if (texture == nullptr) {
        return;
    }
#if defined(NITERAID_REMAKE)
    if (modern_presentation) {
        int width = 0;
        int height = 0;
        SDL_QueryTexture(
            static_cast<SDL_Texture*>(texture), nullptr, nullptr, &width, &height);
        SDL_FRect destination {static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(width), static_cast<float>(height)};
        SDL_RenderCopyExF(renderer, static_cast<SDL_Texture*>(texture), nullptr,
                          &destination, 0.0, nullptr, SDL_FLIP_NONE);
        return;
    }
#endif
    render_texture_at(
        renderer, texture, static_cast<int>(x), static_cast<int>(y));
}

void draw_terminal_game_over_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 5>& strip_textures,
    void* ground_texture,
    const std::array<void*, 15>& flag_textures)
{
    const auto frame = presenter_timing::terminal_presenter_frame(world).value_or(0);
    const auto draw = presenter_timing::terminal_presenter_draw(frame);
    if (draw < presenter_timing::kExplosionFrameCount) {
        draw_texture_if_available(renderer, strip_textures[draw], 0, 0x96);
        draw_texture_if_available(renderer, ground_texture, 0, 0xbc);
        return;
    }

    draw_texture_if_available(renderer, strip_textures.back(), 0, 0x96);
    draw_texture_if_available(renderer, ground_texture, 0, 0xbc);

    const auto flag_draw = draw - presenter_timing::kExplosionFrameCount;
    std::size_t flag_index = 0;
    if (flag_draw < presenter_timing::kFlagRaiseFrameCount) {
        flag_index = flag_draw;
    } else {
        const auto wave_frame = flag_draw - presenter_timing::kFlagRaiseFrameCount;
        flag_index = static_cast<std::size_t>(
            presenter_timing::kFlagRaiseFrameCount +
            (wave_frame + 3u) % presenter_timing::kFlagWaveFrameCount);
    }
    draw_texture_if_available(
        renderer,
        flag_textures[flag_index],
        0x95 + kTerminalFlagOriginX[flag_index],
        0x9a + kTerminalFlagOriginY[flag_index]);
}

void draw_survivor_intermission_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 6>& finale_controller_textures,
    const std::array<void*, 0x20>& survivor_ufo_textures,
    const std::array<void*, 0x16>& survivor_joke_textures,
    void* retained_trooper_texture,
    void* backdrop_texture,
    bool modern_presentation)
{
    const auto state = original_survivor_presenter_frame(world);
    Vec2 motion_offset {};
    if (modern_presentation && world.survivor_intermission_frame > 0 && state.active) {
        auto previous_world = world;
        --previous_world.survivor_intermission_frame;
        const auto previous = original_survivor_presenter_frame(previous_world);
        const auto alpha = world.presentation_alpha;
        motion_offset.x = (previous.x_fixed + (state.x_fixed - previous.x_fixed) * alpha) / 65536.0f -
                          std::floor(static_cast<float>(state.x_fixed) / 65536.0f);
        motion_offset.y = (previous.y_fixed + (state.y_fixed - previous.y_fixed) * alpha) / 65536.0f -
                          std::floor(static_cast<float>(state.y_fixed) / 65536.0f);
    }
    for (const auto& command : state.draws) {
        void* texture = nullptr;
        auto position = command.position;
        const auto sprite = command.sprite;
        if (sprite == 0x28d) {
            texture = backdrop_texture;
        } else if (sprite >= 0x28e && sprite <= 0x293) {
            const auto index = sprite - 0x28e;
            texture = finale_controller_textures[index];
            position.x += kSurvivorUfoOriginX[index];
            position.y += kSurvivorUfoOriginY[index];
        } else if (sprite >= 0x294 && sprite <= 0x2b3) {
            const auto index = sprite - 0x294;
            texture = survivor_ufo_textures[index];
            if (index < 3) {
                position.x += kSurvivorUfoGlintOriginX[index];
                position.y += kSurvivorUfoGlintOriginY[index];
            }
        } else if (sprite >= 0x2b4 && sprite <= 0x2c9) {
            const auto index = sprite - 0x2b4;
            texture = survivor_joke_textures[index];
            if (index < 2) position.x += 5;
            if (index >= 5) position.y += kSurvivorJokeActorOriginY[index - 5];
        }
        if (sprite != 0x28d) {
            position.x += motion_offset.x;
            position.y += motion_offset.y;
        }
        draw_moving_texture_if_available(renderer, texture, position.x, position.y, modern_presentation);
    }
    for (const auto& trooper : state.retained_troopers) {
        draw_moving_texture_if_available(
            renderer, retained_trooper_texture, trooper.x, trooper.y + 2, modern_presentation);
    }
}

void draw_no_survivor_flyby_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 8>& textures)
{
    for (const auto& command : original_no_survivor_presenter_frame(world).draws) {
        const auto index = command.sprite - 0x260;
        const int x = static_cast<int>(command.position.x) + (index < 4 ? kNoSurvivorSegmentOriginX[index] : 0);
        const int y = static_cast<int>(command.position.y) + (index < 4 ? kNoSurvivorSegmentOriginY[index] : 5);
        draw_texture_if_available(renderer, textures[index], x, y);
    }
}


constexpr std::array<std::uint8_t, 6> kFinaleSparkBaseColors {{
    0x10, 0x20, 0x60, 0x90, 0xa0, 0xb0,
}};

constexpr std::array<std::array<SDL_Color, 12>, 6> kFinaleSparkColors {{
    std::array<SDL_Color, 12> {{
        {238, 238, 238, 255}, {222, 222, 222, 255}, {210, 210, 210, 255},
        {194, 194, 194, 255}, {182, 182, 182, 255}, {170, 170, 170, 255},
        {153, 153, 153, 255}, {141, 141, 141, 255}, {125, 125, 125, 255},
        {113, 113, 113, 255}, {101, 101, 101, 255}, {85, 85, 85, 255},
    }},
    std::array<SDL_Color, 12> {{
        {255, 0, 0, 255}, {238, 0, 0, 255}, {226, 0, 0, 255},
        {214, 0, 0, 255}, {202, 0, 0, 255}, {190, 0, 0, 255},
        {178, 0, 0, 255}, {165, 0, 0, 255}, {153, 0, 0, 255},
        {137, 0, 0, 255}, {125, 0, 0, 255}, {113, 0, 0, 255},
    }},
    std::array<SDL_Color, 12> {{
        {0, 255, 0, 255}, {0, 238, 0, 255}, {0, 226, 0, 255},
        {0, 214, 0, 255}, {4, 202, 0, 255}, {4, 190, 0, 255},
        {4, 178, 0, 255}, {4, 165, 0, 255}, {4, 153, 0, 255},
        {4, 137, 0, 255}, {4, 125, 0, 255}, {4, 113, 0, 255},
    }},
    std::array<SDL_Color, 12> {{
        {0, 0, 255, 255}, {0, 0, 238, 255}, {0, 0, 226, 255},
        {0, 0, 214, 255}, {0, 0, 202, 255}, {0, 0, 190, 255},
        {0, 0, 178, 255}, {0, 0, 165, 255}, {0, 0, 153, 255},
        {0, 0, 137, 255}, {0, 0, 125, 255}, {0, 0, 113, 255},
    }},
    std::array<SDL_Color, 12> {{
        {242, 218, 255, 255}, {230, 186, 255, 255}, {218, 157, 255, 255},
        {210, 125, 255, 255}, {202, 93, 255, 255}, {190, 64, 255, 255},
        {182, 32, 255, 255}, {170, 0, 255, 255}, {153, 0, 230, 255},
        {129, 0, 206, 255}, {117, 0, 182, 255}, {97, 0, 157, 255},
    }},
    std::array<SDL_Color, 12> {{
        {255, 218, 255, 255}, {255, 186, 255, 255}, {255, 157, 255, 255},
        {255, 125, 255, 255}, {255, 93, 255, 255}, {255, 64, 255, 255},
        {255, 32, 255, 255}, {255, 0, 255, 255}, {226, 0, 230, 255},
        {202, 0, 206, 255}, {182, 0, 182, 255}, {157, 0, 157, 255},
    }},
}};

int fixed_to_pixel(std::int32_t value)
{
    if (value >= 0) {
        return value >> 16;
    }
    return -static_cast<int>((-static_cast<std::int64_t>(value) + 0xffff) >> 16);
}

void draw_finale_particles(SDL_Renderer* renderer, std::uint32_t target_tick,
                           void* particle_texture, std::uint32_t seed)
{
    const auto state = original_finale_particle_frame(seed, target_tick);
    const auto& actors = state.actors;

    // 2e5e draws stars in layer 1, then sparks in layer 2, each in slot order.
    for (const auto kind : {FinaleParticleKind::Star, FinaleParticleKind::Spark}) {
        for (const auto& actor : actors) {
            if (!actor.active || actor.kind != kind) {
                continue;
            }
            const int x = fixed_to_pixel(actor.x);
            const int y = fixed_to_pixel(actor.y);
            if (actor.kind == FinaleParticleKind::Star) {
                draw_texture_if_available(renderer, particle_texture, x, y);
                continue;
            }
            if (x < 0 || x >= kLogicalWidth || y < 0 || y >= kLogicalHeight) {
                continue;
            }
            const auto base = std::find(kFinaleSparkBaseColors.begin(),
                                        kFinaleSparkBaseColors.end(), actor.base_color);
            if (base == kFinaleSparkBaseColors.end()) {
                continue;
            }
            const auto color_group = static_cast<std::size_t>(
                std::distance(kFinaleSparkBaseColors.begin(), base));
            const auto shade = static_cast<std::size_t>(std::min(actor.fade / 4, 11));
            const auto color = kFinaleSparkColors[color_group][shade];
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }

}

void draw_palette_banner_shadow(SDL_Renderer* renderer, int left, int right)
{
    const auto palette = load_original_resource(0x09b);
    const auto remap = load_original_resource(0x09c);
    if (!palette || palette->size() != 768 || !remap || remap->size() != 256) return;
    int width = 0;
    int height = 0;
    if (auto* target = SDL_GetRenderTarget(renderer)) {
        SDL_QueryTexture(target, nullptr, nullptr, &width, &height);
    } else {
        SDL_GetRendererOutputSize(renderer, &width, &height);
    }
    if (width <= 0 || height <= 0) return;
    const float scale = std::min(width / 320.0f, height / 200.0f);
    const int origin_x = static_cast<int>((width - 320 * scale) / 2);
    const int origin_y = static_cast<int>((height - 200 * scale) / 2);
    SDL_Rect region {origin_x + static_cast<int>(left * scale), origin_y + static_cast<int>(60 * scale),
                     static_cast<int>((right - left) * scale), static_cast<int>(22 * scale)};
    if (region.w < 1 || region.h < 1) return;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(region.w) * region.h * 4);
    if (SDL_RenderReadPixels(renderer, &region, SDL_PIXELFORMAT_RGBA32, pixels.data(), region.w * 4) != 0) return;
    const auto color = [&](std::size_t index, std::size_t channel) {
        return static_cast<std::uint8_t>((*palette)[index * 3 + channel] * 255 / 63);
    };
    // 01db/0602 remaps the current page through resource 09c. Copying static
    // sky pixels here incorrectly erases particles passing under the shadow.
    for (int y = 60; y < 82; ++y) {
        for (int x = left; x < (y < 80 ? left + 2 : right); ++x) {
            const auto offset = (static_cast<std::size_t>((y - 60) * scale) * region.w +
                                 static_cast<std::size_t>((x - left) * scale)) * 4;
            for (std::size_t index = 0; index < 256; ++index) {
                if (pixels[offset] == color(index, 0) && pixels[offset + 1] == color(index, 1) &&
                    pixels[offset + 2] == color(index, 2)) {
                    const auto mapped = (*remap)[index];
                    SDL_SetRenderDrawColor(renderer, color(mapped, 0), color(mapped, 1), color(mapped, 2), 255);
                    SDL_RenderDrawPoint(renderer, x, y);
                    break;
                }
            }
        }
    }
}

void draw_finale_win_banner(SDL_Renderer* renderer, const FontTextures& font_textures)
{
    draw_palette_banner_shadow(renderer, 126, 190);
    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_Rect fill {130, 61, 59, 17};
    SDL_RenderFillRect(renderer, &fill);

    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, 129, 58, 191, 58);
    SDL_RenderDrawLine(renderer, 191, 59, 191, 78);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawLine(renderer, 129, 59, 190, 59);
    SDL_RenderDrawLine(renderer, 129, 60, 129, 78);
    SDL_RenderDrawLine(renderer, 190, 59, 190, 78);
    SDL_RenderDrawLine(renderer, 129, 78, 190, 78);
    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_RenderDrawLine(renderer, 128, 59, 128, 79);
    SDL_RenderDrawLine(renderer, 130, 60, 189, 60);
    SDL_RenderDrawLine(renderer, 189, 60, 189, 77);
    SDL_RenderDrawLine(renderer, 128, 79, 190, 79);
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, 128, 58);
    SDL_RenderDrawPoint(renderer, 191, 79);

    constexpr int kOriginalBannerSpaceWidth = 5;
    draw_text_tinted_with_space(
        renderer, font_textures, 135, 67, "YOU WON!", 1,
        SDL_Color {157, 157, 255, 255}, kOriginalBannerSpaceWidth);
    draw_text_tinted_with_space(
        renderer, font_textures, 136, 66, "YOU WON!", 1,
        SDL_Color {0, 0, 0, 255}, kOriginalBannerSpaceWidth);
}



void draw_level4_milestone_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 10>& pizza_textures,
    const std::array<void*, 3>& bunker_special_textures,
    void* player_base_texture)
{
    const auto frame = original_pizza_presenter_frame(world);
    for (const auto& command : frame.draws) {
        if (command.sprite == 0x1df) {
            draw_bunker_base(renderer, player_base_texture);
        } else if (command.sprite >= 0x1e2 && command.sprite <= 0x1e4) {
            const auto index = command.sprite - 0x1e2;
            draw_texture_if_available(renderer, bunker_special_textures[index],
                command.x + kBunkerSpecialOriginX[index], command.y + kBunkerSpecialOriginY[index]);
        } else if (command.sprite >= 0x2d7 && command.sprite <= 0x2df) {
            constexpr std::array<int, 9> kOriginX {{2, 0, 1, 0, 1, 1, 1, 0, 2}};
            const auto index = command.sprite - 0x2d7;
            draw_texture_if_available(renderer, pizza_textures[index],
                command.x + kOriginX[index], command.y + (command.sprite == 0x2df ? 1 : 0));
        }
    }
}

void draw_level8_milestone_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 0x2a>& helicopter_textures,
    const std::array<void*, 10>& trooper_textures,
    const std::array<void*, 13>& lowering_textures,
    const std::array<void*, 3>& bunker_special_textures,
    void* player_base_texture)
{
    const auto frame = original_helicopter_presenter_frame(world);
    for (const auto& command : frame.draws) {
        if (command.sprite == 0x1df) {
            draw_bunker_base(renderer, player_base_texture);
        } else if (command.sprite >= 0x1e2 && command.sprite <= 0x1e4) {
            const auto index = command.sprite - 0x1e2;
            draw_texture_if_available(renderer, bunker_special_textures[index],
                command.x + kBunkerSpecialOriginX[index], command.y + kBunkerSpecialOriginY[index]);
        } else if (command.sprite >= 0x2cd && command.sprite <= 0x2d6) {
            constexpr std::array<int, 10> kOriginX {{1, 1, 1, 0, 0, 1, 2, 0, 0, 0}};
            const auto index = command.sprite - 0x2cd;
            draw_texture_if_available(renderer, trooper_textures[index],
                command.x + kOriginX[index], command.y);
        } else if (command.sprite >= 0x2e1 && command.sprite <= 0x30a) {
            draw_texture_if_available(renderer, helicopter_textures[command.sprite - 0x2e1],
                command.x + (command.sprite == 0x2e1 ? 5 : 0),
                command.y + (command.sprite == 0x2e1 ? 1 : 0));
        } else if (command.sprite >= 0x105 && command.sprite <= 0x110) {
            const auto index = command.sprite - 0x105;
            draw_texture_if_available(renderer, lowering_textures[index],
                command.x + kParatrooperLandingOriginX[index],
                command.y + kParatrooperLandingOriginY[index]);
        }
    }
}

void draw_milestone_intermission_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    const std::array<void*, 10>& pizza_textures,
    const std::array<void*, 0x2a>& helicopter_textures,
    const std::array<void*, 10>& trooper_textures,
    const std::array<void*, 13>& lowering_textures,
    const std::array<void*, 3>& bunker_special_textures,
    void* player_base_texture)
{
    if (world.milestone_intermission == MilestoneIntermission::Level4Pizza) {
        draw_level4_milestone_presenter(
            renderer, world, pizza_textures, bunker_special_textures,
            player_base_texture);
    } else if (world.milestone_intermission == MilestoneIntermission::Level8Helicopter) {
        draw_level8_milestone_presenter(
            renderer, world, helicopter_textures, trooper_textures,
            lowering_textures, bunker_special_textures, player_base_texture);
    }
}

void draw_finale_presenter(
    SDL_Renderer* renderer,
    const WorldState& world,
    void* vehicle_texture,
    void* vehicle_female_texture,
    const std::array<void*, 6>& vehicle_animation_textures,
    const std::array<void*, 10>& trooper_textures,
    const std::array<void*, 3>& door_textures,
    void* bunker_texture,
    void* particle_texture,
    const FontTextures& font_textures)
{
    for (const auto& command : original_finale_presenter_frame(world).draws) {
        auto x = command.x;
        auto y = command.y;
        void* texture = nullptr;
        if (command.sprite == 0x1df) {
            draw_bunker_base(renderer, bunker_texture);
            continue;
        }
        if (command.sprite >= 0x1e2 && command.sprite <= 0x1e4) {
            const auto index = command.sprite - 0x1e2;
            texture = door_textures[index];
            x += kBunkerSpecialOriginX[index];
            y += kBunkerSpecialOriginY[index];
        } else if (command.sprite >= 0x2cd && command.sprite <= 0x2d0) {
            const auto index = command.sprite - 0x2cd;
            constexpr std::array<int, 4> kOriginX {{1, 1, 1, 0}};
            texture = trooper_textures[index];
            x += kOriginX[index];
        } else if (command.sprite == 0x30b) {
            texture = vehicle_texture;
            y += 3;
        } else if (command.sprite == 0x312) {
            texture = vehicle_female_texture;
        } else if (command.sprite >= 0x30c && command.sprite <= 0x311) {
            texture = vehicle_animation_textures[command.sprite - 0x30c];
        }
        draw_texture_if_available(renderer, texture, x, y);
    }
    if (world.finale_presenter_frame > presenter_timing::finale_particle_start(world)) {
        draw_finale_particles(renderer,
            world.finale_presenter_frame - presenter_timing::finale_particle_start(world),
            particle_texture, world.finale_particle_rng_seed);
    }
    draw_finale_win_banner(renderer, font_textures);
}

int control_panel_glyph_sprite_for_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return 0x002 + (c - 'A');
    }
    if (c >= '0' && c <= '9') {
        return 0x01c + (c - '0');
    }
    return 0;
}

int control_panel_text_width(std::string_view text)
{
    return text.empty() ? 0 : static_cast<int>(text.size()) * 0x11;
}

constexpr std::array<int, 0x26> kControlPanelGlyphOriginX {{
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 7, 0, 1, 0, 0, 0, 1, 0, 0,
}};
constexpr std::array<int, 0x26> kControlPanelGlyphOriginY {{
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0,
}};

void draw_control_panel_text(SDL_Renderer* renderer,
                             const ControlPanelGlyphTextures& glyph_textures,
                             int x, int y, std::string_view text,
                             SDL_Color color)
{
    int cursor_x = x;
    for (char c : text) {
        const int sprite_id = control_panel_glyph_sprite_for_char(c);
        if (sprite_id > 0) {
            auto* texture = static_cast<SDL_Texture*>(glyph_textures[static_cast<std::size_t>(sprite_id)]);
            if (texture != nullptr) {
                int texture_width = 0;
                int texture_height = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);
                SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(texture, color.a);
                SDL_Rect destination {
                    cursor_x + kControlPanelGlyphOriginX[static_cast<std::size_t>(sprite_id)],
                    y + kControlPanelGlyphOriginY[static_cast<std::size_t>(sprite_id)],
                    texture_width,
                    texture_height,
                };
                SDL_RenderCopy(renderer, texture, nullptr, &destination);
                SDL_SetTextureColorMod(texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(texture, 255);
            }
        }
        cursor_x += 0x11;
    }
}

void draw_bottom_hint(SDL_Renderer* renderer, const FontTextures& font_textures, std::string_view text)
{
    constexpr int kTextScale = 1;
    constexpr int kOriginalHintSpaceWidth = 5;
    const int text_x =
        (kLogicalWidth - text_width_with_space(font_textures, text, kTextScale,
                                               kOriginalHintSpaceWidth)) /
        2;
    draw_text_tinted_with_space(renderer, font_textures, text_x - 1, 187, text,
                                kTextScale, SDL_Color {157, 157, 255, 255},
                                kOriginalHintSpaceWidth);
    draw_text_tinted_with_space(renderer, font_textures, text_x, 186, text,
                                kTextScale, SDL_Color {0, 0, 0, 255},
                                kOriginalHintSpaceWidth);
}

using LedGlyph = std::array<std::uint8_t, 7>;

LedGlyph led_glyph_for_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }

    switch (c) {
    case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
    case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    case 'C': return {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
    case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    case 'G': return {0x0f, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0f};
    case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11};
    case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
    case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    case '?': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

int led_text_width(std::string_view text, int scale)
{
    int width = 0;
    for (char c : text) {
        width += (c == ' ') ? 4 * scale : 6 * scale;
    }
    return std::max(0, width - scale);
}

void draw_led_text(SDL_Renderer* renderer, int x, int y, std::string_view text, int scale,
                   SDL_Color lit, SDL_Color unlit)
{
    int cursor_x = x;
    for (char c : text) {
        if (c == ' ') {
            cursor_x += 4 * scale;
            continue;
        }

        const auto glyph = led_glyph_for_char(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                const bool enabled = (glyph[static_cast<std::size_t>(row)] & (1u << (4 - col))) != 0;
                const auto color = enabled ? lit : unlit;
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                SDL_Rect segment {cursor_x + col * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &segment);
            }
        }
        cursor_x += 6 * scale;
    }
}

void draw_fade_overlay(SDL_Renderer* renderer, std::uint8_t alpha)
{
    if (alpha == 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_Rect overlay {0, 0, kLogicalWidth, kLogicalHeight};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

std::uint8_t fade_alpha_for_world(const WorldState& world)
{
    int alpha = 0;
    if (world.screen_fade_frames_remaining > 0) {
        alpha = std::max(alpha, (world.screen_fade_frames_remaining * 255) / kScreenFadeFrames);
    }

    int remaining = 0;
    switch (world.screen) {
    case Screen::Title:
        remaining = world.title_frames_remaining;
        break;
    case Screen::Credits:
        remaining = world.credits_frames_remaining;
        break;
    case Screen::AttractInterlude:
        remaining = world.attract_interlude_frames_remaining;
        break;
    case Screen::HighScores:
        remaining = world.high_score_frames_remaining;
        break;
    default:
        remaining = kScreenFadeFrames + 1;
        break;
    }

    if (remaining > 0 && remaining <= kScreenFadeFrames) {
        alpha = std::max(alpha, ((kScreenFadeFrames - remaining) * 255) / kScreenFadeFrames);
    }
    return static_cast<std::uint8_t>(std::clamp(alpha, 0, 255));
}

void draw_banner_styled(SDL_Renderer* renderer, const FontTextures& font_textures, std::string_view text,
                        SDL_Color fill, SDL_Color border_color, SDL_Color text_color,
                        int y = 56)
{
    constexpr int scale = 2;
    const int rendered_text_width = text_width(font_textures, text, scale);
    const int box_width = rendered_text_width + 14;
    SDL_Rect box {(kLogicalWidth - box_width) / 2, y, box_width, 22};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_Rect border {box.x - 1, box.y - 1, box.w + 2, box.h + 2};
    SDL_RenderDrawRect(renderer, &border);
    draw_text_tinted(renderer, font_textures, box.x + 7, box.y + 4, text, scale, text_color);
}

void draw_banner(SDL_Renderer* renderer, const FontTextures& font_textures, std::string_view text)
{
    constexpr int scale = 1;
    const int rendered_text_width = text_width(font_textures, text, scale);
    const int box_width = rendered_text_width + 14;
    SDL_Rect box {(kLogicalWidth - box_width) / 2, 59, box_width, 22};

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_Rect shadow {box.x + 2, box.y + 2, box.w, box.h};
    SDL_RenderFillRect(renderer, &shadow);

    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_RenderFillRect(renderer, &box);

    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_Rect outer {box.x - 1, box.y - 1, box.w + 2, box.h + 2};
    SDL_RenderDrawRect(renderer, &outer);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawRect(renderer, &box);

    draw_masked_text(renderer, font_textures, box.x + 8, box.y + 7, text, scale,
                     SDL_Color {0, 0, 137, 255});
    draw_masked_text(renderer, font_textures, box.x + 7, box.y + 6, text, scale,
                     SDL_Color {0, 0, 28, 255});
}

void draw_level_entry_page_residue(SDL_Renderer* renderer, void* gameplay_texture)
{
    if (gameplay_texture == nullptr) {
        return;
    }

    // The banner blitter retains the left and lower edges from the page twelve
    // scanlines earlier. These are copies from the user-supplied gameplay plane,
    // not captured replacement pixels.
    SDL_Rect left_source {108, 48, 2, 20};
    SDL_Rect left_destination {108, 60, 2, 20};
    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(gameplay_texture),
                   &left_source, &left_destination);
    SDL_Rect lower_source {108, 68, 101, 2};
    SDL_Rect lower_destination {108, 80, 101, 2};
    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(gameplay_texture),
                   &lower_source, &lower_destination);

    SDL_SetRenderDrawColor(renderer, 0, 0, 125, 255);
    SDL_RenderDrawPoint(renderer, 109, 67);
    SDL_SetRenderDrawColor(renderer, 0, 0, 153, 255);
    for (int x = 110; x <= 206; x += 8) {
        SDL_RenderDrawPoint(renderer, x, 81);
    }
}


void draw_terminal_banner_page_residue(SDL_Renderer* renderer, void* gameplay_texture)
{
    if (gameplay_texture == nullptr) {
        return;
    }

    SDL_Rect left_source {120, 48, 2, 20};
    SDL_Rect left_destination {120, 60, 2, 20};
    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(gameplay_texture),
                   &left_source, &left_destination);
    SDL_Rect lower_source {120, 68, 77, 2};
    SDL_Rect lower_destination {120, 80, 77, 2};
    SDL_RenderCopy(renderer, static_cast<SDL_Texture*>(gameplay_texture),
                   &lower_source, &lower_destination);

    SDL_SetRenderDrawColor(renderer, 0, 0, 125, 255);
    SDL_RenderDrawPoint(renderer, 121, 69);
    SDL_SetRenderDrawColor(renderer, 0, 0, 137, 255);
    SDL_RenderDrawPoint(renderer, 121, 79);
    SDL_SetRenderDrawColor(renderer, 0, 0, 153, 255);
    for (int x = 126; x <= 190; x += 8) {
        SDL_RenderDrawPoint(renderer, x, 81);
    }
}

void draw_intermission_banner(SDL_Renderer* renderer, const FontTextures& font_textures)
{
    constexpr SDL_Rect fill {122, 61, 76, 17};

    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_RenderFillRect(renderer, &fill);

    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, 121, 58, 200, 58);
    SDL_RenderDrawLine(renderer, 200, 58, 200, 78);

    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawLine(renderer, 121, 59, 199, 59);
    SDL_RenderDrawLine(renderer, 121, 59, 121, 78);
    SDL_RenderDrawLine(renderer, 199, 59, 199, 78);
    SDL_RenderDrawLine(renderer, 121, 78, 199, 78);

    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_RenderDrawLine(renderer, 120, 59, 120, 79);
    SDL_RenderDrawLine(renderer, 122, 60, 198, 60);
    SDL_RenderDrawLine(renderer, 198, 60, 198, 77);
    SDL_RenderDrawLine(renderer, 120, 79, 199, 79);

    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, 120, 58);
    SDL_RenderDrawPoint(renderer, 200, 79);

    draw_masked_text(renderer, font_textures, 127, 67, "INTERMISSION", 1,
                     SDL_Color {157, 157, 255, 255});
    draw_masked_text(renderer, font_textures, 128, 66, "INTERMISSION", 1,
                     SDL_Color {0, 0, 0, 255});
}

void draw_terminal_game_over_banner(SDL_Renderer* renderer, const FontTextures& font_textures)
{
    constexpr int scale = 1;
    constexpr std::string_view text = "GAME OVER!";
    SDL_Rect box {124, 61, 72, 17};

    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, 123, 58, 198, 58);
    SDL_RenderDrawLine(renderer, 198, 58, 198, 78);
    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_RenderDrawLine(renderer, 122, 59, 122, 79);
    SDL_RenderDrawLine(renderer, 124, 60, 196, 60);
    SDL_RenderDrawLine(renderer, 196, 60, 196, 77);
    SDL_RenderDrawLine(renderer, 122, 79, 197, 79);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawLine(renderer, 123, 59, 197, 59);
    SDL_RenderDrawLine(renderer, 123, 59, 123, 78);
    SDL_RenderDrawLine(renderer, 197, 59, 197, 78);
    SDL_RenderDrawLine(renderer, 123, 78, 197, 78);
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, 122, 58);
    SDL_RenderDrawPoint(renderer, 198, 79);

    constexpr int kOriginalBannerSpaceWidth = 5;
    draw_text_tinted_with_space(renderer, font_textures, 129, 67, text, scale,
                                SDL_Color {157, 157, 255, 255},
                                kOriginalBannerSpaceWidth);
    draw_text_tinted_with_space(renderer, font_textures, 130, 66, text, scale,
                                SDL_Color {0, 0, 0, 255},
                                kOriginalBannerSpaceWidth);
}

enum class WaveBannerPanel { LevelEntry, Complete };

void draw_level_entry_banner(SDL_Renderer* renderer,
                             const FontTextures& font_textures,
                             std::string_view text,
                             WaveBannerPanel panel)
{
    constexpr int scale = 1;
    const int rendered_text_width = text_width(font_textures, text, scale);
    const bool complete = panel == WaveBannerPanel::Complete;
    const int original_width = complete ? internals::high_score_name_width(text) + 10 : 0;
    const int nominal_width = complete ? original_width + 1 : rendered_text_width + 14;
    const int box_width = nominal_width + (complete ? 1 : 0);
    SDL_Rect box {complete ? (kLogicalWidth - original_width) / 2 :
                  (kLogicalWidth - nominal_width) / 2 + 1,
                  61, box_width, 17};

    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_RenderFillRect(renderer, &box);

    // The level-entry presenter uses a five-line asymmetric bevel.
    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_RenderDrawLine(renderer, box.x - 2, box.y - 2, box.x - 2, box.y + box.h);
    SDL_RenderDrawLine(renderer, box.x, box.y - 1, box.x + box.w - 2, box.y - 1);
    SDL_RenderDrawLine(renderer, box.x + box.w - 2, box.y, box.x + box.w - 2,
                       box.y + box.h - 1);
    SDL_RenderDrawLine(renderer, box.x - 2, box.y + box.h + 1,
                       box.x + box.w - 1, box.y + box.h + 1);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawLine(renderer, box.x - 1, box.y - 2,
                       box.x + box.w - 1, box.y - 2);
    SDL_RenderDrawLine(renderer, box.x - 1, box.y - 1, box.x - 1, box.y + box.h - 1);
    SDL_RenderDrawLine(renderer, box.x + box.w - 1, box.y - 1,
                       box.x + box.w - 1, box.y + box.h);
    SDL_RenderDrawLine(renderer, box.x - 1, box.y + box.h,
                       box.x + box.w - 1, box.y + box.h);
    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, box.x - 1, box.y - 3, box.x + box.w, box.y - 3);
    SDL_RenderDrawLine(renderer, box.x + box.w, box.y - 2,
                       box.x + box.w, box.y + box.h);
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, box.x - 2, box.y - 3);
    SDL_RenderDrawPoint(renderer, box.x + box.w, box.y + box.h + 1);

    constexpr int kOriginalBannerSpaceWidth = 5;
    draw_text_tinted_with_space(renderer, font_textures, box.x + 5, box.y + 6,
                                text, scale, SDL_Color {157, 157, 255, 255},
                                kOriginalBannerSpaceWidth);
    draw_text_tinted_with_space(renderer, font_textures, box.x + 6, box.y + 5,
                                text, scale, SDL_Color {0, 0, 0, 255},
                                kOriginalBannerSpaceWidth);
}

#if defined(NITERAID_ENABLE_SDL2)
void draw_confirmation_prompt_shadow(SDL_Renderer* renderer, int outer_x, int inner_width)
{
    int output_width = 0;
    int output_height = 0;
    if (SDL_GetRendererOutputSize(renderer, &output_width, &output_height) != 0 ||
        output_width <= 0 || output_height <= 0) {
        return;
    }

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(output_width) * static_cast<std::size_t>(output_height) * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(),
                             output_width * 4) != 0) {
        return;
    }

    const auto remap_point = [&](int x, int y) {
        const int source_x = std::clamp(x * output_width / kLogicalWidth, 0, output_width - 1);
        const int source_y = std::clamp(y * output_height / kLogicalHeight, 0, output_height - 1);
        const auto offset =
            (static_cast<std::size_t>(source_y) * static_cast<std::size_t>(output_width) +
             static_cast<std::size_t>(source_x)) *
            4;
        const std::uint8_t r = pixels[offset];
        const std::uint8_t g = pixels[offset + 1];
        const std::uint8_t b = pixels[offset + 2];

        SDL_Color mapped {};
        bool changed = true;
        if (r == 255 && g == 0 && b == 0) {
            mapped = {214, 0, 0, 255};
        } else if (r == 113 && g == 113 && b == 113) {
            mapped = {56, 56, 56, 255};
        } else if (r == 56 && g == 56 && b == 56) {
            mapped = {32, 32, 32, 255};
        } else if ((r == 64 && g == 64 && b == 0) ||
                   (r == 89 && g == 85 && b == 0)) {
            mapped = {40, 32, 12, 255};
        } else {
            changed = false;
        }
        if (changed) {
            SDL_SetRenderDrawColor(renderer, mapped.r, mapped.g, mapped.b, mapped.a);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    };

    // DrawPrompt's VGA plane mask darkens two columns on the left and two
    // rows below the modal instead of painting a conventional alpha shadow.
    for (int y = 60; y <= 79; ++y) {
        remap_point(outer_x - 2, y);
        remap_point(outer_x - 1, y);
    }
    for (int y = 80; y <= 81; ++y) {
        for (int x = outer_x - 2; x <= outer_x + inner_width + 2; ++x) {
            remap_point(x, y);
        }
    }
}
#endif

void draw_confirmation_prompt(SDL_Renderer* renderer, const FontTextures& font_textures,
                              ConfirmationPromptAction action)
{
    if (action == ConfirmationPromptAction::None) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_Rect banner {2, 181, 315, 17};
    SDL_RenderFillRect(renderer, &banner);
    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_RenderDrawLine(renderer, 0, 181, 0, 199);
    SDL_RenderDrawLine(renderer, 317, 181, 317, 197);
    SDL_RenderDrawLine(renderer, 0, 199, 318, 199);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_RenderDrawLine(renderer, 1, 181, 1, 198);
    SDL_RenderDrawLine(renderer, 318, 181, 318, 198);
    SDL_RenderDrawLine(renderer, 1, 198, 318, 198);
    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, 319, 181, 319, 198);
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, 319, 199);

    const int inner_width = action == ConfirmationPromptAction::QuitToDos ? 83 : 159;
    SDL_Rect inner {(kLogicalWidth - inner_width) / 2, 61, inner_width, 17};
    const int outer_x = inner.x - 2;

    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_Rect outer {outer_x, 59, inner.w + 4, 21};
    SDL_RenderFillRect(renderer, &outer);
    SDL_SetRenderDrawColor(renderer, 113, 113, 113, 255);
    SDL_Rect middle {outer_x + 1, 59, inner.w + 3, 20};
    SDL_RenderFillRect(renderer, &middle);
    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_Rect inner_border {inner.x, 60, inner.w + 1, 18};
    SDL_RenderFillRect(renderer, &inner_border);
    SDL_SetRenderDrawColor(renderer, 186, 190, 255, 255);
    SDL_RenderFillRect(renderer, &inner);
    SDL_SetRenderDrawColor(renderer, 153, 153, 153, 255);
    SDL_RenderDrawLine(renderer, outer_x + 1, 58, outer_x + inner.w + 4, 58);
    SDL_RenderDrawLine(renderer, outer_x + inner.w + 4, 59,
                       outer_x + inner.w + 4, 78);
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderDrawPoint(renderer, outer_x, 58);
    SDL_RenderDrawPoint(renderer, outer_x + inner.w + 4, 79);

    const std::string_view question_text =
        action == ConfirmationPromptAction::QuitToDos ? "QUIT TO DOS" : "STOP YOUR CURRENT GAME";
    constexpr int kQuestionSpaceWidth = 5;
    draw_text_tinted_with_space(renderer, font_textures, inner.x + 5, inner.y + 6,
                                question_text, 1, SDL_Color {157, 157, 255, 255},
                                kQuestionSpaceWidth);
    draw_text_tinted_with_space(renderer, font_textures, inner.x + 6, inner.y + 5,
                                question_text, 1, SDL_Color {0, 0, 0, 255},
                                kQuestionSpaceWidth);
    const int question_mark_x = inner.x + inner.w - 10;
    constexpr std::array<SDL_Point, 10> kQuestionMarkPixels {{
        {-1, 1},
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 1},
        {3, 2}, {2, 3}, {2, 4}, {2, 6},
    }};
    SDL_SetRenderDrawColor(renderer, 157, 157, 255, 255);
    for (const auto& pixel : kQuestionMarkPixels) {
        SDL_RenderDrawPoint(renderer, question_mark_x - 1 + pixel.x,
                           inner.y + 6 + pixel.y);
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for (const auto& pixel : kQuestionMarkPixels) {
        SDL_RenderDrawPoint(renderer, question_mark_x + pixel.x,
                           inner.y + 5 + pixel.y);
    }

#if defined(NITERAID_ENABLE_SDL2)
    draw_confirmation_prompt_shadow(renderer, outer_x, inner.w);
#endif

    constexpr std::string_view hint = "PRESS 'Y' TO CONFIRM, 'N' TO BACK OUT";
    constexpr int kOriginalHintSpaceWidth = 5;
    draw_text_tinted_with_space(renderer, font_textures, 56, 187, hint, 1,
                                SDL_Color {157, 157, 255, 255},
                                kOriginalHintSpaceWidth);
    draw_text_tinted_with_space(renderer, font_textures, 57, 186, hint, 1,
                                SDL_Color {0, 0, 0, 255},
                                kOriginalHintSpaceWidth);
}

int high_score_sprite_for_character(unsigned char character)
{
    constexpr int kBase = 0x53;
    if (character >= 0x21 && character <= 0x40) {
        return kBase + 0x1c + (character - 0x21);
    }
    if (character >= 'A' && character <= 'Z') {
        return kBase + (character - 'A');
    }
    if (character >= 0x5b && character <= 0x60) {
        return kBase + 0x3c + (character - 0x5b);
    }
    if (character >= 'a' && character <= 'z') {
        return kBase + (character - 'a');
    }
    if (character >= 0x7b && character <= 0x7f) {
        return kBase + 0x42 + (character - 0x7b);
    }
    return 0;
}

int high_score_sprite_origin_y(int sprite_id)
{
    switch (sprite_id) {
    case 0x070:
    case 0x071:
    case 0x073:
    case 0x078:
    case 0x079:
        return 1;
    case 0x088:
    case 0x089:
    case 0x08b:
    case 0x092:
    case 0x098:
        return 2;
    case 0x07b:
        return 3;
    case 0x07a:
    case 0x07c:
        return 6;
    case 0x093:
        return 7;
    default:
        return 0;
    }
}

void draw_high_scores(SDL_Renderer* renderer, const FontTextures& font_textures, const WorldState& world)
{
    static constexpr std::array<int, 6> kRowY {{51, 74, 97, 120, 143, 166}};

    const auto draw_name = [&](int x, int y, std::string_view text) {
        int cursor_x = x;
        for (const unsigned char c : text) {
            if (c == ' ') {
                cursor_x += 5;
                continue;
            }
            const int sprite_id = high_score_sprite_for_character(c);
            auto* texture =
                sprite_id > 0 ? static_cast<SDL_Texture*>(font_textures[sprite_id]) : nullptr;
            if (texture == nullptr) {
                continue;
            }
            int width = 0;
            int height = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
            SDL_Rect destination {
                cursor_x,
                y + high_score_sprite_origin_y(sprite_id),
                width,
                height,
            };
            SDL_RenderCopy(renderer, texture, nullptr, &destination);
            cursor_x += width;
        }
        return cursor_x;
    };

    const auto draw_number = [&](int value, int field_width, int x, int y) {
        const std::string text = std::to_string(value);
        int cursor_x = x + 1 + std::max(0, field_width - static_cast<int>(text.size())) * 9;
        for (char c : text) {
            if (c < '0' || c > '9') {
                cursor_x += 9;
                continue;
            }
            auto* texture = static_cast<SDL_Texture*>(font_textures[0x049 + (c - '0')]);
            if (texture != nullptr) {
                int width = 0;
                int height = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
                const int origin_x = c == '1' ? 2 : 0;
                SDL_Rect destination {cursor_x + origin_x, y, width, height};
                SDL_RenderCopy(renderer, texture, nullptr, &destination);
            }
            cursor_x += 9;
        }
    };

    for (std::size_t index = 0; index < world.high_scores.size(); ++index) {
        const auto& entry = world.high_scores[index];
        const int row_y = kRowY[index];
        int cursor_x = draw_name(21, row_y + 1, entry.name);

#if defined(NITERAID_REMAKE)
        if (world.screen == Screen::HighScoreEntry && entry.highlighted &&
            world.high_score_gamepad_active) {
            const auto selected =
                static_cast<unsigned char>(world.high_score_gamepad_character);
            const int sprite_id = selected == ' '
                ? high_score_sprite_for_character(0x7f)
                : high_score_sprite_for_character(selected);
            auto* texture = sprite_id > 0
                ? static_cast<SDL_Texture*>(font_textures[sprite_id])
                : nullptr;
            if (texture != nullptr) {
                int width = 0;
                int height = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
                SDL_SetTextureColorMod(texture, 255, 210, 64);
                SDL_Rect destination {
                    cursor_x,
                    row_y + 1 + high_score_sprite_origin_y(sprite_id),
                    width,
                    height,
                };
                SDL_RenderCopy(renderer, texture, nullptr, &destination);
                SDL_SetTextureColorMod(texture, 255, 255, 255);
                cursor_x += selected == ' ' ? 5 : width;
            }
        }
#endif

        int rank = 0;
        if (entry.stat_c >= 12) {
            rank = 3;
        } else if (entry.stat_c >= 8) {
            rank = 2;
        } else if (entry.stat_c >= 4) {
            rank = 1;
        }
        if (rank != 0) {
            render_texture_at(renderer, font_textures[0x040 + index], 143, row_y - 6);
            render_texture_at(renderer, font_textures[0x045 + rank], 143, row_y - 6);
        }

        draw_number(std::max(0, entry.score), 5, 179, row_y + 2);
        draw_number(entry.stat_a, 4, 230, row_y + 2);
        draw_number(entry.stat_b, 3, 272, row_y + 2);

        if (world.screen == Screen::HighScoreEntry && entry.highlighted &&
            (world.frame_tick / 30) % 2 == 0) {
            render_texture_at(
                renderer,
                font_textures[high_score_sprite_for_character(0x7f)],
                cursor_x,
                row_y + 1);
        }
    }
}

#endif

}  // namespace

void Renderer::refresh_combined_input()
{
    const bool gamepad_left =
        gamepad_dpad_left_ || gamepad_axis_x_ < -kGamepadStickDeadzone;
    const bool gamepad_right =
        gamepad_dpad_right_ || gamepad_axis_x_ > kGamepadStickDeadzone;
    const bool gamepad_up =
        gamepad_dpad_up_ || gamepad_axis_y_ < -kGamepadStickDeadzone;
    const bool gamepad_down =
        gamepad_dpad_down_ || gamepad_axis_y_ > kGamepadStickDeadzone;

    input_.move_left = keyboard_left_ || gamepad_left;
    input_.move_right = keyboard_right_ || gamepad_right;
    input_.move_up = keyboard_up_ || gamepad_up;
    input_.move_down = keyboard_down_ || gamepad_down;
    input_.fire = keyboard_fire_ || mouse_fire_ || gamepad_fire_button_ ||
                  gamepad_right_trigger_ > kGamepadTriggerThreshold;
    input_.start = keyboard_start_ || gamepad_start_button_;
}

void Renderer::update_gamepad_name_repeat(std::uint64_t now_ms)
{
    const bool up =
        gamepad_dpad_up_ || gamepad_axis_y_ < -kGamepadStickDeadzone;
    const bool down =
        gamepad_dpad_down_ || gamepad_axis_y_ > kGamepadStickDeadzone;
    const int direction = up == down ? 0 : (up ? 1 : -1);

    if (direction == 0) {
        gamepad_name_repeat_direction_ = 0;
        gamepad_name_repeat_at_ms_ = 0;
        return;
    }

    if (direction != gamepad_name_repeat_direction_) {
        gamepad_name_repeat_direction_ = direction;
        gamepad_name_repeat_at_ms_ = now_ms + kGamepadRepeatDelayMs;
        input_.gamepad_name_up = direction > 0;
        input_.gamepad_name_down = direction < 0;
        return;
    }

    if (now_ms >= gamepad_name_repeat_at_ms_) {
        input_.gamepad_name_up = direction > 0;
        input_.gamepad_name_down = direction < 0;
        gamepad_name_repeat_at_ms_ = now_ms + kGamepadRepeatIntervalMs;
    }
}

void Renderer::open_gamepad(std::int32_t device_id)
{
#if defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL3)
    if (gamepad_ == nullptr) {
        gamepad_ = SDL_OpenGamepad(static_cast<SDL_JoystickID>(device_id));
    }
#elif defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL2)
    if (gamepad_ == nullptr && SDL_IsGameController(device_id) == SDL_TRUE) {
        gamepad_ = SDL_GameControllerOpen(device_id);
    }
#else
    (void)device_id;
#endif
}

void Renderer::open_first_gamepad()
{
#if defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL3)
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads != nullptr && count > 0) {
        open_gamepad(static_cast<std::int32_t>(gamepads[0]));
    }
    SDL_free(gamepads);
#elif defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL2)
    for (int index = 0; index < SDL_NumJoysticks() && gamepad_ == nullptr; ++index) {
        open_gamepad(index);
    }
#endif
}

void Renderer::close_gamepad()
{
#if defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL3)
    if (gamepad_ != nullptr) {
        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(gamepad_));
        gamepad_ = nullptr;
    }
#elif defined(NITERAID_REMAKE) && defined(NITERAID_ENABLE_SDL2)
    if (gamepad_ != nullptr) {
        SDL_GameControllerClose(static_cast<SDL_GameController*>(gamepad_));
        gamepad_ = nullptr;
    }
#endif
    gamepad_dpad_left_ = false;
    gamepad_dpad_right_ = false;
    gamepad_dpad_up_ = false;
    gamepad_dpad_down_ = false;
    gamepad_fire_button_ = false;
    gamepad_start_button_ = false;
    gamepad_axis_x_ = 0;
    gamepad_axis_y_ = 0;
    gamepad_right_trigger_ = 0;
    gamepad_name_repeat_direction_ = 0;
    gamepad_name_repeat_at_ms_ = 0;
    refresh_combined_input();
}

Renderer::Renderer(bool debug_hitboxes, bool modern_presentation, bool crt_filter)
    : debug_hitboxes_(debug_hitboxes),
      modern_presentation_(modern_presentation),
      crt_filter_(crt_filter)
{
#if defined(NITERAID_ENABLE_SDL3)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) == 0) {
        window_ = SDL_CreateWindow(kWindowTitle, kLogicalWidth * kWindowScale,
                                   kLogicalHeight * kWindowScale, 0);
        if (window_ != nullptr) {
            renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window_), nullptr);
        }
        if (renderer_ == nullptr) {
            if (window_ != nullptr) {
                SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
                window_ = nullptr;
            }
            SDL_Quit();
        } else {
            SDL_StartTextInput(static_cast<SDL_Window*>(window_));
            open_first_gamepad();
        }
    }
#elif defined(NITERAID_ENABLE_SDL2)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0) {
#if defined(NITERAID_PORTMASTER)
        constexpr std::uint32_t window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
        constexpr std::uint32_t window_flags = 0;
#endif
        window_ = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   kLogicalWidth * kWindowScale, kLogicalHeight * kWindowScale,
                                   window_flags);
        if (window_ != nullptr) {
            renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window_), -1,
                                           SDL_RENDERER_ACCELERATED);
#if defined(NITERAID_PORTMASTER)
            if (renderer_ == nullptr) {
                renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window_), -1,
                                               SDL_RENDERER_SOFTWARE);
            }
#endif
            if (renderer_ != nullptr) {
                auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
                SDL_RenderSetLogicalSize(sdl_renderer, kLogicalWidth, kLogicalHeight);
                SDL_RenderSetIntegerScale(sdl_renderer, SDL_TRUE);
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
            }
        }
        if (renderer_ == nullptr) {
            if (window_ != nullptr) {
                SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
                window_ = nullptr;
            }
            SDL_Quit();
        } else {
            SDL_StartTextInput();
            open_first_gamepad();
            auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
#if defined(NITERAID_REMAKE)
            if (SDL_RenderTargetSupported(sdl_renderer) == SDL_TRUE) {
                // Keep the target geometry stable when starting in original
                // presentation or toggling F12 in either direction.
                constexpr int scene_scale = kWindowScale;
                remake_scene_texture_ = SDL_CreateTexture(
                    sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                    kLogicalWidth * scene_scale, kLogicalHeight * scene_scale);
                if (remake_scene_texture_ != nullptr) {
                    SDL_SetTextureBlendMode(
                        static_cast<SDL_Texture*>(remake_scene_texture_), SDL_BLENDMODE_NONE);
                }
            }
#endif
            title_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/title.bmp");
            credits_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/credits.bmp");
            attract_interlude_texture_ =
                load_bmp_texture(sdl_renderer, "assets/fullscreen/attract_interlude.bmp");
            gameplay_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/gameplay.bmp");
            if (const auto palette = load_original_resource(0x09b)) gameplay_palette_ = *palette;
            for (std::size_t digit = 0; digit < hud_digit_textures_.size(); ++digit) {
                const auto path = sprite_asset_path(0x210 + static_cast<int>(digit) * 6);
                hud_digit_textures_[digit] = load_bmp_texture(sdl_renderer, path, true);
                if (const auto asset = load_original_image(path)) {
                    hud_digit_origin_x_[digit] = asset->origin_x;
                    hud_digit_origin_y_[digit] = asset->origin_y;
                }
            }
            high_score_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/high_score.bmp");
            control_panel_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/control_panel.bmp");
            control_panel_audio_default_texture_ = load_bmp_texture(
                sdl_renderer, "assets/fullscreen/control_panel_audio_default_09b.bmp");
            control_panel_selector_texture_ = load_bmp_texture(
                sdl_renderer, "assets/sprites/sprite_029.bmp", true);
            transition_texture_ = load_bmp_texture(sdl_renderer, "assets/fullscreen/transition.bmp");
            if (auto image = load_shareware_ending_image(
                    shareware_ending::kBaseResource)) {
                shareware_ending_base_texture_ =
                    create_indexed_texture(sdl_renderer, *image);
                for (std::uint16_t frame = 0;
                     frame < shareware_ending::kFrameCount; ++frame) {
                    if (auto frame_image = load_shareware_ending_image(
                            static_cast<std::uint16_t>(
                                shareware_ending::kFirstFrameResource + frame))) {
                        shareware_ending_frame_textures_[frame] =
                            create_indexed_texture(sdl_renderer, *frame_image);
                    }
                }
            }
            aircraft_d_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_116.bmp", true);
            aircraft_d_reverse_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_115.bmp", true);
            aircraft_a_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_13a.bmp", true);
            aircraft_a_reverse_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_139.bmp", true);
            aircraft_c_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_15b.bmp", true);
            aircraft_c_reverse_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_15c.bmp", true);
            aircraft_b_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_17a.bmp", true);
            aircraft_b_reverse_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_17b.bmp", true);
            player_base_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1df.bmp", true);
            projectile_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e7.bmp", true);
            paratrooper_body_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_0f0.bmp", true);
            paratrooper_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_0f7.bmp", true);
            grounded_invader_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_111.bmp", true);
            landed_invader_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_0f8.bmp", true);
            for (int index = 0; index < 4; ++index) {
                assault_invader_left_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x0fb + index), true);
                assault_invader_right_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x101 + index), true);
            }
            muzzle_flash_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e6.bmp", true);
            player_fire_overlay_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e0.bmp", true);
            player_mount_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e1.bmp", true);
            player_pivot_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e2.bmp", true);
            player_frozen_aim_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_1e5.bmp", true);
            for (std::size_t index = 0; index < finale_drop_textures_.size(); ++index) {
                const auto path = sprite_asset_path(0x268 + static_cast<int>(index));
                finale_drop_textures_[index] = load_bmp_texture(sdl_renderer, path, true);
                if (const auto asset = load_original_image(path)) {
                    finale_drop_origin_x_[index] = asset->origin_x;
                    finale_drop_origin_y_[index] = asset->origin_y;
                }
            }
            finale_backdrop_texture_ = load_bmp_texture(sdl_renderer, "assets/sprites/sprite_28d.bmp", true);
            for (int index = 0; index < static_cast<int>(finale_controller_textures_.size()); ++index) {
                const auto sprite_id = 0x28e + index;
                finale_controller_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(survivor_ufo_textures_.size()); ++index) {
                const auto sprite_id = 0x294 + index;
                survivor_ufo_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(survivor_joke_textures_.size()); ++index) {
                const auto sprite_id = 0x2b4 + index;
                survivor_joke_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0;
                 index < static_cast<int>(no_survivor_flyby_textures_.size()); ++index) {
                no_survivor_flyby_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x260 + index), true);
            }
            survivor_retained_trooper_texture_ =
                load_bmp_texture(sdl_renderer, sprite_asset_path(0x2ca), true);
            finale_vehicle_texture_ =
                load_bmp_texture(sdl_renderer, sprite_asset_path(0x30b), true);
            finale_vehicle_female_texture_ =
                load_bmp_texture(sdl_renderer, sprite_asset_path(0x312), true);
            for (int index = 0;
                 index < static_cast<int>(finale_vehicle_animation_textures_.size()); ++index) {
                finale_vehicle_animation_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x30c + index), true);
            }
            for (int index = 0; index < static_cast<int>(finale_trooper_textures_.size()); ++index) {
                finale_trooper_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x2cd + index), true);
            }
            finale_particle_texture_ =
                load_bmp_texture(sdl_renderer, sprite_asset_path(0x313), true);
            for (int index = 0;
                 index < static_cast<int>(milestone_pizza_textures_.size()); ++index) {
                milestone_pizza_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x2d7 + index), true);
            }
            for (int index = 0;
                 index < static_cast<int>(milestone_helicopter_textures_.size()); ++index) {
                milestone_helicopter_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x2e1 + index), true);
            }
            for (int index = 0; index < static_cast<int>(credits_overlay_textures_.size()); ++index) {
                credits_overlay_textures_[index] =
                    load_bmp_texture(
                        sdl_renderer,
                        fullscreen_sequence_asset_path("credits_overlay", index),
                        true);
            }
            for (int index = 0; index < static_cast<int>(attract_interlude_overlay_textures_.size()); ++index) {
                attract_interlude_overlay_textures_[index] =
                    load_bmp_texture(sdl_renderer,
                                     fullscreen_sequence_asset_path("attract_interlude_overlay", index),
                                     true);
            }
            for (int index = 0; index < static_cast<int>(bunker_special_effect_textures_.size()); ++index) {
                const auto sprite_id = 0x1e2 + index;
                bunker_special_effect_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0;
                 index < static_cast<int>(terminal_game_over_strip_textures_.size());
                 ++index) {
                terminal_game_over_strip_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x24c + index));
            }
            terminal_game_over_ground_texture_ =
                load_bmp_texture(sdl_renderer, sprite_asset_path(0x207));
            death_flash_ground_texture_ = load_bmp_texture(
                sdl_renderer, "assets/sprites/sprite_207_pal_09d.bmp");
            for (int index = 0;
                 index < static_cast<int>(terminal_game_over_flag_textures_.size());
                 ++index) {
                terminal_game_over_flag_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(0x251 + index), true);
            }
            terminal_game_over_banner_texture_ =
                load_bmp_texture(sdl_renderer, "assets/sprites/terminal_game_over_banner_09b.bmp");
            overrun_assault_residue_textures_[0] =
                load_bmp_texture(sdl_renderer,
                                 "assets/sprites/overrun_assault_residue_start_09b.bmp",
                                 true);
            overrun_assault_residue_textures_[1] =
                load_bmp_texture(sdl_renderer,
                                 "assets/sprites/overrun_assault_residue_mid_09b.bmp",
                                 true);
            overrun_assault_residue_textures_[2] =
                load_bmp_texture(sdl_renderer,
                                 "assets/sprites/overrun_assault_residue_late_09b.bmp",
                                 true);
            level_entry_residue_texture_ =
                load_bmp_texture(sdl_renderer,
                                 "assets/sprites/level_entry_residue_09b.bmp",
                                 true);
            for (int index = 0; index < static_cast<int>(player_aim_textures_.size()); ++index) {
                const auto sprite_id = 0x1e8 + index;
                player_aim_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(paratrooper_frame_textures_.size()); ++index) {
                const auto sprite_id = 0x0f0 + index;
                paratrooper_frame_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(paratrooper_deploy_textures_.size()); ++index) {
                const auto sprite_id = 0x0e6 + index;
                paratrooper_deploy_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(paratrooper_no_chute_textures_.size()); ++index) {
                const auto sprite_id = 0x111 + index;
                paratrooper_no_chute_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(paratrooper_landing_textures_.size()); ++index) {
                const auto sprite_id = 0x105 + index;
                paratrooper_landing_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(aircraft_d_rotor_textures_.size()); ++index) {
                const auto sprite_id = 0x117 + index;
                aircraft_d_rotor_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(aircraft_a_rotor_textures_.size()); ++index) {
                const auto sprite_id = 0x13b + index;
                aircraft_a_rotor_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    aircraft_a_rotor_origin_x_[index] = asset->origin_x;
                    aircraft_a_rotor_origin_y_[index] = asset->origin_y;
                }
            }
            for (int index = 0; index < static_cast<int>(aircraft_c_rotor_textures_.size()); ++index) {
                const auto sprite_id = 0x15d + index;
                aircraft_c_rotor_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(aircraft_b_rotor_textures_.size()); ++index) {
                const auto sprite_id = 0x17c + index;
                aircraft_b_rotor_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int index = 0; index < static_cast<int>(smart_bomb_armed_textures_.size()); ++index) {
                const auto sprite_id = 0x1cd + index;
                smart_bomb_armed_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    smart_bomb_armed_origin_x_[index] = asset->origin_x;
                    smart_bomb_armed_origin_y_[index] = asset->origin_y;
                }
            }
            for (int index = 0; index < static_cast<int>(smart_bomb_fall_textures_.size()); ++index) {
                const auto sprite_id = 0x1d5 + index;
                smart_bomb_fall_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    smart_bomb_fall_origin_x_[index] = asset->origin_x;
                    smart_bomb_fall_origin_y_[index] = asset->origin_y;
                }
            }
            for (int index = 0; index < static_cast<int>(aircraft_shell_textures_.size()); ++index) {
                const auto sprite_id = 0x1be + index;
                aircraft_shell_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    aircraft_shell_origin_x_[index] = asset->origin_x;
                    aircraft_shell_origin_y_[index] = asset->origin_y;
                }
            }
            for (int index = 0; index < static_cast<int>(enemy_death_textures_.size()); ++index) {
                const auto sprite_id = 0x1c7 + index;
                enemy_death_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    enemy_death_origin_x_[index] = asset->origin_x;
                    enemy_death_origin_y_[index] = asset->origin_y;
                }
            }
            for (int index = 0; index < static_cast<int>(aircraft_fragment_textures_.size()); ++index) {
                const auto sprite_id = 0x121 + index;
                aircraft_fragment_textures_[index] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
                if (const auto asset = load_original_image(sprite_asset_path(sprite_id))) {
                    aircraft_fragment_origin_x_[index] = asset->origin_x;
                    aircraft_fragment_origin_y_[index] = asset->origin_y;
                }
            }
            for (int sprite_id = 0x002; sprite_id <= 0x025; ++sprite_id) {
                control_panel_glyph_textures_[static_cast<std::size_t>(sprite_id)] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id), true);
            }
            for (int sprite_id = 0x026; sprite_id <= 0x035; ++sprite_id) {
                control_panel_widget_textures_[static_cast<std::size_t>(sprite_id)] =
                    load_bmp_texture(sdl_renderer, sprite_asset_path(sprite_id),
                                     sprite_id != 0x027 && sprite_id != 0x028);
            }
            for (int sprite_id = 0; sprite_id < static_cast<int>(font_textures_.size()); ++sprite_id) {
                font_textures_[sprite_id] = load_bmp_texture(sdl_renderer, font_asset_path(sprite_id), true);
                font_mask_textures_[sprite_id] =
                    load_font_foreground_mask_texture(sdl_renderer, font_asset_path(sprite_id));
            }
        }
    }
#endif
}

Renderer::~Renderer()
{
    close_gamepad();
#if defined(NITERAID_ENABLE_SDL3)
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
    }
    if (renderer_ != nullptr || window_ != nullptr) {
        SDL_Quit();
    }
#elif defined(NITERAID_ENABLE_SDL2)
    destroy_textures(std::array {
        title_texture_,
        credits_texture_,
        attract_interlude_texture_,
        gameplay_texture_,
        high_score_texture_,
        control_panel_texture_,
        control_panel_audio_default_texture_,
        control_panel_selector_texture_,
        transition_texture_,
        shareware_ending_base_texture_,
        aircraft_d_texture_,
        aircraft_d_reverse_texture_,
        aircraft_a_texture_,
        aircraft_a_reverse_texture_,
        aircraft_b_texture_,
        aircraft_b_reverse_texture_,
        aircraft_c_texture_,
        aircraft_c_reverse_texture_,
        player_base_texture_,
        projectile_texture_,
        paratrooper_body_texture_,
        paratrooper_texture_,
        grounded_invader_texture_,
        landed_invader_texture_,
        muzzle_flash_texture_,
        player_fire_overlay_texture_,
        player_mount_texture_,
        player_pivot_texture_,
        player_frozen_aim_texture_,
        finale_backdrop_texture_,
        survivor_retained_trooper_texture_,
        finale_vehicle_texture_,
        finale_vehicle_female_texture_,
        finale_particle_texture_,
        terminal_game_over_ground_texture_,
        death_flash_ground_texture_,
        terminal_game_over_banner_texture_,
        level_entry_residue_texture_,
    });
    destroy_textures(assault_invader_left_textures_);
    destroy_textures(shareware_ending_frame_textures_);
    destroy_textures(assault_invader_right_textures_);
    destroy_textures(finale_controller_textures_);
    destroy_textures(credits_overlay_textures_);
    destroy_textures(attract_interlude_overlay_textures_);
    destroy_textures(bunker_special_effect_textures_);
    destroy_textures(survivor_ufo_textures_);
    destroy_textures(survivor_joke_textures_);
    destroy_textures(no_survivor_flyby_textures_);
    destroy_textures(finale_vehicle_animation_textures_);
    destroy_textures(finale_trooper_textures_);
    destroy_textures(milestone_pizza_textures_);
    destroy_textures(milestone_helicopter_textures_);
    destroy_textures(terminal_game_over_strip_textures_);
    destroy_textures(terminal_game_over_flag_textures_);
    destroy_textures(overrun_assault_residue_textures_);
    destroy_textures(paratrooper_landing_textures_);
    destroy_textures(player_aim_textures_);
    destroy_textures(paratrooper_frame_textures_);
    destroy_textures(paratrooper_deploy_textures_);
    destroy_textures(paratrooper_no_chute_textures_);
    destroy_textures(finale_drop_textures_);
    destroy_textures(aircraft_d_rotor_textures_);
    destroy_textures(aircraft_a_rotor_textures_);
    destroy_textures(aircraft_c_rotor_textures_);
    destroy_textures(aircraft_b_rotor_textures_);
    destroy_textures(smart_bomb_armed_textures_);
    destroy_textures(smart_bomb_fall_textures_);
    destroy_textures(aircraft_shell_textures_);
    destroy_textures(enemy_death_textures_);
    destroy_textures(aircraft_fragment_textures_);
    destroy_textures(control_panel_glyph_textures_);
    destroy_textures(control_panel_widget_textures_);
    destroy_textures(font_textures_);
    destroy_textures(font_mask_textures_);
    destroy_textures(hud_digit_textures_);
#if defined(NITERAID_REMAKE)
    if (remake_scene_texture_ != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(remake_scene_texture_));
        remake_scene_texture_ = nullptr;
    }
#endif
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
    }
    if (renderer_ != nullptr || window_ != nullptr) {
        SDL_Quit();
    }
#endif
}

#if defined(NITERAID_REMAKE)
void Renderer::prepare_remake_frame(const WorldState& world) const
{
    if (!modern_presentation_) {
        return;
    }

    const bool level_restarted =
        remake_level_seen_ &&
        (world.current_level != remake_debris_level_ ||
         (world.screen == Screen::Gameplay && world.frame_tick < remake_previous_world_tick_));
    if (!remake_level_seen_ || level_restarted) {
        remake_level_seen_ = true;
        remake_debris_level_ = world.current_level;
        remake_persistent_debris_.clear();
        remake_debris_slots_.clear();
        remake_shake_seconds_ = 0.0f;
        remake_shake_source_seen_ = false;
        remake_previous_death_flash_frames_ = 0;
    }
    remake_previous_world_tick_ = world.frame_tick;

    bool new_large_explosion = false;
    for (const auto& object : world.objects) {
        if (object.active && !object.pending_destroy &&
            object.type == ObjectType::EnemyDeath && object.timer <= 1) {
            new_large_explosion = true;
            break;
        }
    }
    if (new_large_explosion &&
        (!remake_shake_source_seen_ || remake_shake_source_tick_ != world.frame_tick)) {
        remake_shake_source_seen_ = true;
        remake_shake_source_tick_ = world.frame_tick;
        remake_shake_seconds_ = std::max(
            remake_shake_seconds_,
            18.0f / static_cast<float>(kEnhancedPresentationRateHz));
    }
    if (world.death_flash_frames_remaining > 0 &&
        remake_previous_death_flash_frames_ == 0) {
        remake_shake_seconds_ = std::max(
            remake_shake_seconds_,
            24.0f / static_cast<float>(kEnhancedPresentationRateHz));
    }
    remake_previous_death_flash_frames_ = world.death_flash_frames_remaining;

    if (world.screen != Screen::Gameplay) {
        return;
    }

    if (remake_debris_slots_.size() < world.objects.size()) {
        remake_debris_slots_.resize(world.objects.size());
    }
    constexpr std::size_t kMaximumPersistentDebris = 96;
    for (std::size_t index = 0; index < remake_debris_slots_.size(); ++index) {
        auto& slot = remake_debris_slots_[index];
        const bool is_fragment =
            index < world.objects.size() &&
            world.objects[index].active &&
            !world.objects[index].pending_destroy &&
            world.objects[index].type == ObjectType::AircraftDebris &&
            world.objects[index].has_dropped_payload &&
            world.objects[index].sprite_id >= 0x121 &&
            world.objects[index].sprite_id <= 0x15a;
        if (!is_fragment) {
            slot.active = false;
            continue;
        }

        const auto& fragment = world.objects[index];
        const bool crossed_ground =
            fragment.position.y >= kRemakeDebrisSettlementLine &&
            (!slot.active || slot.previous_y < kRemakeDebrisSettlementLine);
        if (crossed_ground) {
            const int animated_sprite =
                std::clamp(fragment.sprite_id + (fragment.frame % 8), 0x121, 0x15a);
            const int ground_layer =
                static_cast<int>(remake_persistent_debris_.size() % 4);
            const double settled_x = std::clamp(fragment.position.x, 2.0, 316.0);
            constexpr float kMaximumFragmentWidth = 32.0f;
            constexpr float kMaximumFragmentHeight = 20.0f;
            const float settled_y =
                static_cast<float>(187 - ground_layer) - kMaximumFragmentHeight;
            if (!remake_debris_overlaps_bunker(
                    settled_x, settled_y,
                    kMaximumFragmentWidth, kMaximumFragmentHeight)) {
                if (remake_persistent_debris_.size() >= kMaximumPersistentDebris) {
                    remake_persistent_debris_.erase(remake_persistent_debris_.begin());
                }
                remake_persistent_debris_.push_back({
                    animated_sprite,
                    settled_x,
                    ground_layer,
                });
            }
        }
        slot.active = true;
        slot.previous_y = fragment.position.y;
    }
}

void Renderer::draw_remake_persistent_debris(const WorldState& world) const
{
    if (!modern_presentation_ ||
        (world.screen != Screen::Gameplay &&
         world.screen != Screen::Intermission &&
         world.screen != Screen::GameOver &&
         world.screen != Screen::Finale)) {
        return;
    }

#if defined(NITERAID_ENABLE_SDL2)
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    for (const auto& debris : remake_persistent_debris_) {
        if (debris.sprite_id < 0x121 || debris.sprite_id > 0x15a) {
            continue;
        }
        void* texture =
            aircraft_fragment_textures_[static_cast<std::size_t>(debris.sprite_id - 0x121)];
        if (texture == nullptr) {
            continue;
        }
        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                         &texture_width, &texture_height);
        const int bottom = 187 - debris.ground_layer;
        SDL_Rect destination {
            std::clamp(static_cast<int>(std::lround(debris.x)), 0,
                       std::max(0, kLogicalWidth - texture_width)),
            bottom - texture_height,
            texture_width,
            texture_height,
        };
        if (remake_debris_overlaps_bunker(
                static_cast<float>(destination.x),
                static_cast<float>(destination.y),
                static_cast<float>(destination.w),
                static_cast<float>(destination.h))) {
            continue;
        }
        SDL_SetTextureColorMod(static_cast<SDL_Texture*>(texture), 135, 116, 98);
        SDL_SetTextureAlphaMod(static_cast<SDL_Texture*>(texture), 255);
        render_texture_at(sdl_renderer, texture, destination, false);
        SDL_SetTextureColorMod(static_cast<SDL_Texture*>(texture), 255, 255, 255);
        SDL_SetTextureAlphaMod(static_cast<SDL_Texture*>(texture), 255);
    }
#else
    (void)world;
#endif
}

void Renderer::composite_remake_frame(float presentation_delta_seconds) const
{
#if defined(NITERAID_ENABLE_SDL2)
    if (remake_scene_texture_ == nullptr) {
        return;
    }

    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    SDL_RenderSetScale(sdl_renderer, 1.0f, 1.0f);
    SDL_SetRenderTarget(sdl_renderer, nullptr);
    SDL_SetRenderDrawColor(sdl_renderer, 4, 6, 12, 255);
    SDL_RenderClear(sdl_renderer);

    int offset_x = 0;
    int offset_y = 0;
    constexpr float kMaximumShakeSeconds =
        24.0f / static_cast<float>(kEnhancedPresentationRateHz);
    if (remake_shake_seconds_ > 0.0f) {
        const int amplitude = std::max(
            1, static_cast<int>(std::ceil(7.0f *
                                         remake_shake_seconds_ / kMaximumShakeSeconds)));
        std::uint32_t hash =
            0x9e3779b9u ^ (remake_presentation_serial_ * 0x85ebca6bu) ^
            (static_cast<std::uint32_t>(remake_shake_seconds_ * 100000.0f) * 0xc2b2ae35u);
        hash ^= hash >> 16;
        const int span = amplitude * 2 + 1;
        offset_x = static_cast<int>(hash % static_cast<std::uint32_t>(span)) - amplitude;
        hash = hash * 1664525u + 1013904223u;
        offset_y = static_cast<int>(hash % static_cast<std::uint32_t>(span)) - amplitude;
        remake_shake_seconds_ =
            std::max(0.0f, remake_shake_seconds_ - presentation_delta_seconds);
    }
    ++remake_presentation_serial_;

    SDL_Rect destination {offset_x, offset_y, kLogicalWidth, kLogicalHeight};
    SDL_RenderCopy(sdl_renderer, static_cast<SDL_Texture*>(remake_scene_texture_),
                   nullptr, &destination);

    if (modern_presentation_ && crt_filter_) {
        SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(sdl_renderer, &previous_blend);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 34);
        for (int y = 1; y < kLogicalHeight; y += 2) {
            SDL_RenderDrawLine(sdl_renderer, 0, y, kLogicalWidth - 1, y);
        }

        for (int edge = 0; edge < 8; ++edge) {
            const auto alpha = static_cast<std::uint8_t>(5 + (8 - edge) * 2);
            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, alpha);
            SDL_Rect top {edge, edge, kLogicalWidth - edge * 2, 1};
            SDL_Rect bottom {
                edge, kLogicalHeight - 1 - edge,
                kLogicalWidth - edge * 2, 1,
            };
            SDL_Rect left {edge, edge, 1, kLogicalHeight - edge * 2};
            SDL_Rect right {
                kLogicalWidth - 1 - edge, edge, 1,
                kLogicalHeight - edge * 2,
            };
            SDL_RenderFillRect(sdl_renderer, &top);
            SDL_RenderFillRect(sdl_renderer, &bottom);
            SDL_RenderFillRect(sdl_renderer, &left);
            SDL_RenderFillRect(sdl_renderer, &right);
        }
        SDL_SetRenderDrawBlendMode(sdl_renderer, previous_blend);
    }
#endif
}
#endif

bool Renderer::is_running() const
{
    return running_;
}

bool Renderer::is_interactive() const
{
    return renderer_ != nullptr;
}

bool Renderer::modern_presentation_enabled() const
{
    return modern_presentation_;
}

const InputState& Renderer::input_state() const
{
    return input_;
}

#if defined(NITERAID_REMAKE)
void Renderer::toggle_modern_presentation()
{
    modern_presentation_ = !modern_presentation_;
    remake_muzzle_afterglow_seconds_ = 0.0f;
    remake_muzzle_source_seen_ = false;
    remake_persistent_debris_.clear();
    remake_debris_slots_.clear();
    remake_shake_seconds_ = 0.0f;
    remake_shake_source_seen_ = false;
    remake_previous_death_flash_frames_ = 0;
    remake_level_seen_ = false;
    remake_previous_world_tick_ = 0;
    remake_presentation_serial_ = 0;
}
#endif

void Renderer::pump_events()
{
    input_.accept = false;
    input_.name_submit = false;
    input_.name_delete = false;
    input_.backspace = false;
    input_.start_repeat = false;
    input_.menu_key_action = MenuKeyAction::None;
    input_.menu_navigation_up_repeat = false;
    input_.menu_navigation_down_repeat = false;
    input_.text_input.clear();
    input_.menu = false;
    input_.escape = false;
    input_.decline = false;
    input_.mouse_moved = false;
    input_.gamepad_accept = false;
    input_.gamepad_back = false;
    input_.gamepad_start = false;
    input_.gamepad_name_up = false;
    input_.gamepad_name_down = false;
    input_.level_warp_digit = -1;

#if defined(NITERAID_ENABLE_SDL3)
    if (renderer_ == nullptr) {
        return;
    }

    SDL_Event event {};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running_ = false;
        }
#if defined(NITERAID_REMAKE)
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            open_gamepad(static_cast<std::int32_t>(event.gdevice.which));
            continue;
        }
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (gamepad_ != nullptr &&
                SDL_GetGamepadID(static_cast<SDL_Gamepad*>(gamepad_)) ==
                    event.gdevice.which) {
                close_gamepad();
                open_first_gamepad();
            }
            continue;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            const bool pressed = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            switch (event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH:
                if (pressed && !gamepad_fire_button_) {
                    input_.gamepad_accept = true;
                }
                gamepad_fire_button_ = pressed;
                break;
            case SDL_GAMEPAD_BUTTON_EAST:
                if (pressed) {
                    input_.gamepad_back = true;
                }
                break;
            case SDL_GAMEPAD_BUTTON_START:
                if (pressed && !gamepad_start_button_) {
                    input_.gamepad_start = true;
                }
                gamepad_start_button_ = pressed;
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                gamepad_dpad_left_ = pressed;
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                gamepad_dpad_right_ = pressed;
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                gamepad_dpad_up_ = pressed;
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                gamepad_dpad_down_ = pressed;
                break;
            default:
                break;
            }
            continue;
        }
        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            switch (event.gaxis.axis) {
            case SDL_GAMEPAD_AXIS_LEFTX:
                gamepad_axis_x_ = event.gaxis.value;
                break;
            case SDL_GAMEPAD_AXIS_LEFTY:
                gamepad_axis_y_ = event.gaxis.value;
                break;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
                gamepad_right_trigger_ = event.gaxis.value;
                break;
            default:
                break;
            }
            continue;
        }
#endif
        if (event.type == SDL_EVENT_TEXT_INPUT) {
            input_.text_input.append(event.text.text);
            continue;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            float logical_x = event.motion.x;
            float logical_y = event.motion.y;
            SDL_RenderCoordinatesFromWindow(static_cast<SDL_Renderer*>(renderer_),
                                            event.motion.x, event.motion.y,
                                            &logical_x, &logical_y);
            input_.mouse_x = logical_x;
            input_.mouse_y = logical_y;
            input_.mouse_moved = true;
            continue;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouse_fire_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                input_.mouse_primary = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                input_.mouse_secondary = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                input_.mouse_middle = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
            if (pressed) {
                input_.menu_key_action = menu_action_for_key(event.key.key);
            }
            if (pressed && event.key.repeat) {
                input_.menu_navigation_up_repeat |= event.key.key == SDLK_UP || event.key.key == SDLK_LEFT;
                input_.menu_navigation_down_repeat |= event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT;
            }
            switch (event.key.key) {
            case SDLK_LEFT:
                input_.name_delete |= pressed;
                keyboard_left_ = pressed;
                break;
            case SDLK_A:
                keyboard_left_ = pressed;
                break;
            case SDLK_RIGHT:
            case SDLK_D:
                keyboard_right_ = pressed;
                break;
            case SDLK_UP:
            case SDLK_W:
                keyboard_up_ = pressed;
                break;
            case SDLK_DOWN:
            case SDLK_S:
                keyboard_down_ = pressed;
                break;
            case SDLK_TAB:
                input_.menu = pressed;
                break;
            case SDLK_N:
                input_.decline = pressed;
                break;
            case SDLK_Y:
                input_.accept = pressed;
                break;
            case SDLK_SPACE:
                keyboard_fire_ = pressed;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                keyboard_start_ = pressed;
                input_.accept = pressed;
                input_.start_repeat |= pressed && event.key.repeat;
                // Preserve a complete tap queued between simulation updates.
                input_.name_submit |= pressed && !event.key.repeat;
                break;
            case SDLK_BACKSPACE:
                input_.backspace |= pressed;
                break;
            case SDLK_ESCAPE:
                input_.escape = pressed;
                break;
#if defined(NITERAID_REMAKE)
            case SDLK_F12:
                if (pressed && !event.key.repeat) {
                    toggle_modern_presentation();
                }
                break;
#endif
            case SDLK_LCTRL:
            case SDLK_RCTRL:
                input_.control_modifier = pressed;
                break;
            case SDLK_LALT:
            case SDLK_RALT:
                input_.alt_modifier = pressed;
                break;
            case SDLK_0:
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
            case SDLK_5:
            case SDLK_6:
            case SDLK_7:
            case SDLK_8:
            case SDLK_9:
                if (pressed && !event.key.repeat) {
                    input_.level_warp_digit =
                        static_cast<int>(event.key.key - SDLK_0);
                }
                break;
            default:
                break;
            }
        }
    }
    refresh_combined_input();
#if defined(NITERAID_REMAKE)
    update_gamepad_name_repeat(SDL_GetTicks());
#endif
#elif defined(NITERAID_ENABLE_SDL2)
    if (renderer_ == nullptr) {
        return;
    }

    SDL_Event event {};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running_ = false;
        }
#if defined(NITERAID_REMAKE)
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            open_gamepad(event.cdevice.which);
            continue;
        }
        if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (gamepad_ != nullptr) {
                auto* controller = static_cast<SDL_GameController*>(gamepad_);
                const auto instance_id =
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
                if (instance_id == event.cdevice.which) {
                    close_gamepad();
                    open_first_gamepad();
                }
            }
            continue;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN ||
            event.type == SDL_CONTROLLERBUTTONUP) {
            const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:
                if (pressed && !gamepad_fire_button_) {
                    input_.gamepad_accept = true;
                }
                gamepad_fire_button_ = pressed;
                break;
            case SDL_CONTROLLER_BUTTON_B:
                if (pressed) {
                    input_.gamepad_back = true;
                }
                break;
            case SDL_CONTROLLER_BUTTON_START:
                if (pressed && !gamepad_start_button_) {
                    input_.gamepad_start = true;
                }
                gamepad_start_button_ = pressed;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                gamepad_dpad_left_ = pressed;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                gamepad_dpad_right_ = pressed;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                gamepad_dpad_up_ = pressed;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                gamepad_dpad_down_ = pressed;
                break;
            default:
                break;
            }
            continue;
        }
        if (event.type == SDL_CONTROLLERAXISMOTION) {
            switch (event.caxis.axis) {
            case SDL_CONTROLLER_AXIS_LEFTX:
                gamepad_axis_x_ = event.caxis.value;
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
                gamepad_axis_y_ = event.caxis.value;
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                gamepad_right_trigger_ = event.caxis.value;
                break;
            default:
                break;
            }
            continue;
        }
#endif
        if (event.type == SDL_TEXTINPUT) {
            input_.text_input.append(event.text.text);
            continue;
        }
        if (event.type == SDL_MOUSEMOTION) {
            float fx = 0.0f;
            float fy = 0.0f;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            SDL_RenderWindowToLogical(static_cast<SDL_Renderer*>(renderer_),
                                      event.motion.x, event.motion.y, &fx, &fy);
#else
            int output_width = kLogicalWidth;
            int output_height = kLogicalHeight;
            SDL_GetRendererOutputSize(static_cast<SDL_Renderer*>(renderer_),
                                      &output_width, &output_height);
            const float scale = std::max(
                1.0f,
                std::floor(std::min(static_cast<float>(output_width) / kLogicalWidth,
                                    static_cast<float>(output_height) / kLogicalHeight)));
            const float offset_x =
                (static_cast<float>(output_width) - kLogicalWidth * scale) * 0.5f;
            const float offset_y =
                (static_cast<float>(output_height) - kLogicalHeight * scale) * 0.5f;
            fx = (static_cast<float>(event.motion.x) - offset_x) / scale;
            fy = (static_cast<float>(event.motion.y) - offset_y) / scale;
#endif
            input_.mouse_x = fx;
            input_.mouse_y = fy;
            input_.mouse_moved = true;
            continue;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouse_fire_ = event.type == SDL_MOUSEBUTTONDOWN;
                input_.mouse_primary = event.type == SDL_MOUSEBUTTONDOWN;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                input_.mouse_secondary = event.type == SDL_MOUSEBUTTONDOWN;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                input_.mouse_middle = event.type == SDL_MOUSEBUTTONDOWN;
            }
            continue;
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            const bool pressed = event.type == SDL_KEYDOWN;
            if (pressed) {
                input_.menu_key_action = menu_action_for_key(event.key.keysym.sym);
            }
            if (pressed && event.key.repeat != 0) {
                input_.menu_navigation_up_repeat |= event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_LEFT;
                input_.menu_navigation_down_repeat |= event.key.keysym.sym == SDLK_DOWN || event.key.keysym.sym == SDLK_RIGHT;
            }
            switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                input_.name_delete |= pressed;
                keyboard_left_ = pressed;
                break;
            case SDLK_a:
                keyboard_left_ = pressed;
                break;
            case SDLK_RIGHT:
            case SDLK_d:
                keyboard_right_ = pressed;
                break;
            case SDLK_UP:
            case SDLK_w:
                keyboard_up_ = pressed;
                break;
            case SDLK_DOWN:
            case SDLK_s:
                keyboard_down_ = pressed;
                break;
            case SDLK_TAB:
                input_.menu = pressed;
                break;
            case SDLK_n:
                input_.decline = pressed;
                break;
            case SDLK_y:
                input_.accept = pressed;
                break;
            case SDLK_SPACE:
                keyboard_fire_ = pressed;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                keyboard_start_ = pressed;
                input_.accept = pressed;
                input_.start_repeat |= pressed && event.key.repeat != 0;
                input_.name_submit |= pressed && event.key.repeat == 0;
                break;
            case SDLK_BACKSPACE:
                input_.backspace |= pressed;
                break;
            case SDLK_ESCAPE:
                input_.escape = pressed;
                break;
#if defined(NITERAID_REMAKE)
            case SDLK_F12:
                if (pressed && event.key.repeat == 0) {
                    toggle_modern_presentation();
                }
                break;
#endif
            case SDLK_LCTRL:
            case SDLK_RCTRL:
                input_.control_modifier = pressed;
                break;
            case SDLK_LALT:
            case SDLK_RALT:
                input_.alt_modifier = pressed;
                break;
            case SDLK_0:
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
            case SDLK_5:
            case SDLK_6:
            case SDLK_7:
            case SDLK_8:
            case SDLK_9:
                if (pressed && event.key.repeat == 0) {
                    input_.level_warp_digit =
                        static_cast<int>(event.key.keysym.sym - SDLK_0);
                }
                break;
            default:
                break;
            }
        }
    }
    refresh_combined_input();
#if defined(NITERAID_REMAKE)
#if SDL_VERSION_ATLEAST(2, 0, 18)
    update_gamepad_name_repeat(SDL_GetTicks64());
#else
    update_gamepad_name_repeat(SDL_GetTicks());
#endif
#endif
#endif
}

void Renderer::render(const WorldState& world, float presentation_delta_seconds) const
{
    if (renderer_ != nullptr) {
        if (world.screen == Screen::GameOver && world.overrun_interrupt_residue &&
            world.overrun_retained_page) {
            auto retained = world;
            retained.objects = world.overrun_retained_page->objects;
            retained.frame_tick = world.overrun_retained_page->gameplay_tick;
            render_sdl(retained, presentation_delta_seconds);
            return;
        }
        if (world.screen == Screen::Gameplay && world.player_dead &&
            (world.death_flash_frames_remaining > 0 || world.death_palette_restored)) {
            // Palette-only changes retain the last gameplay page, including
            // draw callbacks whose animation uses the frozen gameplay clock.
            auto retained = world;
            retained.frame_tick = world.transition_started_tick;
            if (world.fatal_retained_page) {
                retained.objects = world.fatal_retained_page->objects;
                retained.frame_tick = world.fatal_retained_page->gameplay_tick;
            }
            render_sdl(retained, presentation_delta_seconds);
            return;
        }
        render_sdl(world, presentation_delta_seconds);
        return;
    }

    render_console(world);
}

void Renderer::present() const
{
#if defined(NITERAID_ENABLE_SDL2) || defined(NITERAID_ENABLE_SDL3)
    if (renderer_ != nullptr) {
        SDL_RenderPresent(static_cast<SDL_Renderer*>(renderer_));
    }
#endif
}

void Renderer::show_faithful_startup_card(const char* capture_path)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (renderer_ == nullptr) {
        return;
    }
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
    auto* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        kExitCardWidth, kExitCardHeight);
    if (texture == nullptr) {
        return;
    }
    SDL_SetWindowTitle(sdl_window, "Night Raid v1.1");
    SDL_RenderSetLogicalSize(sdl_renderer, kExitCardWidth, kTextModeDisplayHeight);
    SDL_RenderSetIntegerScale(sdl_renderer, SDL_FALSE);

    constexpr std::array stages {
        StartupCardStage::sound_spinner_tab,
        StartupCardStage::sound_spinner_bell,
        StartupCardStage::sound_spinner_dot,
        StartupCardStage::graphics_spinner,
        StartupCardStage::complete,
    };
    constexpr std::array<std::uint32_t, 5> stage_duration_ms {{90, 90, 90, 120, 2800}};
    const bool capture_only = capture_path != nullptr && *capture_path != '\0';
    const std::size_t first_stage = capture_only ? stages.size() - 1 : 0;
    for (std::size_t index = first_stage; index < stages.size() && running_; ++index) {
        const auto pixels = rasterize_text_mode_page(
            faithful_startup_card_page(stages[index]));
        SDL_UpdateTexture(texture, nullptr, pixels.data(),
                          kExitCardWidth * static_cast<int>(sizeof(std::uint32_t)));
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, texture, nullptr, nullptr);
        if (capture_only) {
            save_frame_bmp(capture_path);
        }
        SDL_RenderPresent(sdl_renderer);
        if (capture_only) {
            break;
        }

        const auto deadline = SDL_GetTicks64() + stage_duration_ms[index];
        while (running_ && SDL_GetTicks64() < deadline) {
            SDL_Event event {};
            while (SDL_PollEvent(&event) != 0) {
                if (event.type == SDL_QUIT) {
                    running_ = false;
                }
            }
            SDL_Delay(5);
        }
    }
    SDL_DestroyTexture(texture);
    SDL_SetWindowTitle(sdl_window, kWindowTitle);
    SDL_RenderSetLogicalSize(sdl_renderer, kLogicalWidth, kLogicalHeight);
    SDL_RenderSetIntegerScale(sdl_renderer, SDL_TRUE);
#elif defined(NITERAID_ENABLE_SDL3)
    if (renderer_ == nullptr) {
        return;
    }
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
    auto* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        kExitCardWidth, kExitCardHeight);
    if (texture == nullptr) {
        return;
    }
    SDL_SetWindowTitle(sdl_window, "Night Raid v1.1");
    SDL_SetRenderLogicalPresentation(
        sdl_renderer, kExitCardWidth, kTextModeDisplayHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    constexpr std::array stages {
        StartupCardStage::sound_spinner_tab,
        StartupCardStage::sound_spinner_bell,
        StartupCardStage::sound_spinner_dot,
        StartupCardStage::graphics_spinner,
        StartupCardStage::complete,
    };
    constexpr std::array<std::uint64_t, 5> stage_duration_ms {{90, 90, 90, 120, 2800}};
    const bool capture_only = capture_path != nullptr && *capture_path != '\0';
    const std::size_t first_stage = capture_only ? stages.size() - 1 : 0;
    for (std::size_t index = first_stage; index < stages.size() && running_; ++index) {
        const auto pixels = rasterize_text_mode_page(
            faithful_startup_card_page(stages[index]));
        SDL_UpdateTexture(texture, nullptr, pixels.data(),
                          kExitCardWidth * static_cast<int>(sizeof(std::uint32_t)));
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);
        if (capture_only) {
            break;
        }

        const auto deadline = SDL_GetTicks() + stage_duration_ms[index];
        while (running_ && SDL_GetTicks() < deadline) {
            SDL_Event event {};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running_ = false;
                }
            }
            SDL_Delay(5);
        }
    }
    SDL_DestroyTexture(texture);
    SDL_SetWindowTitle(sdl_window, kWindowTitle);
    SDL_SetRenderLogicalPresentation(
        sdl_renderer, kLogicalWidth, kLogicalHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);
#else
    (void)capture_path;
#endif
}

void Renderer::show_faithful_exit_card(bool wait_for_acknowledgement,
                                       const char* capture_path) const
{
#if defined(NITERAID_ENABLE_SDL2)
    if (renderer_ == nullptr) {
        return;
    }
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
    const auto pixels = rasterize_text_mode_page(faithful_exit_card_page());
    auto* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
        kExitCardWidth, kExitCardHeight);
    if (texture == nullptr) {
        return;
    }
    SDL_UpdateTexture(texture, nullptr, pixels.data(),
                      kExitCardWidth * static_cast<int>(sizeof(std::uint32_t)));
    SDL_SetWindowTitle(sdl_window, "Thanks for playing Night Raid!");
    SDL_RenderSetLogicalSize(sdl_renderer, kExitCardWidth, kTextModeDisplayHeight);
    SDL_RenderSetIntegerScale(sdl_renderer, SDL_FALSE);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, texture, nullptr, nullptr);
    if (capture_path != nullptr && *capture_path != '\0') {
        save_frame_bmp(capture_path);
    }
    SDL_RenderPresent(sdl_renderer);

    if (wait_for_acknowledgement) {
        SDL_Event event {};
        bool acknowledged = false;
        while (!acknowledged && SDL_WaitEvent(&event) != 0) {
            acknowledged = event.type == SDL_QUIT ||
                           event.type == SDL_MOUSEBUTTONDOWN ||
                           event.type == SDL_CONTROLLERBUTTONDOWN ||
                           (event.type == SDL_KEYDOWN && event.key.repeat == 0);
        }
    }
    SDL_DestroyTexture(texture);
#elif defined(NITERAID_ENABLE_SDL3)
    if (renderer_ == nullptr) {
        return;
    }
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
    const auto pixels = rasterize_text_mode_page(faithful_exit_card_page());
    auto* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
        kExitCardWidth, kExitCardHeight);
    if (texture == nullptr) {
        return;
    }
    SDL_UpdateTexture(texture, nullptr, pixels.data(),
                      kExitCardWidth * static_cast<int>(sizeof(std::uint32_t)));
    SDL_SetWindowTitle(sdl_window, "Thanks for playing Night Raid!");
    SDL_SetRenderLogicalPresentation(
        sdl_renderer, kExitCardWidth, kTextModeDisplayHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
    (void)capture_path;
    SDL_RenderPresent(sdl_renderer);

    if (wait_for_acknowledgement) {
        SDL_Event event {};
        bool acknowledged = false;
        while (!acknowledged && SDL_WaitEvent(&event)) {
            acknowledged = event.type == SDL_EVENT_QUIT ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                           (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat);
        }
    }
    SDL_DestroyTexture(texture);
#else
    (void)wait_for_acknowledgement;
    (void)capture_path;
    std::cout << "Thanks for playing Night Raid!" << std::endl;
#endif
}

bool Renderer::save_frame_bmp(const char* path) const
{
#if defined(NITERAID_ENABLE_SDL2)
    if (renderer_ == nullptr || path == nullptr || *path == '\0') {
        return false;
    }

    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(sdl_renderer, &width, &height) != 0 ||
        width <= 0 || height <= 0) {
        return false;
    }

    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return false;
    }

    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 255));
    // Explicit output bounds preserve offsets when SDL clips to a letterboxed viewport.
    const SDL_Rect capture_rect {0, 0, width, height};
    const int read_result =
        SDL_RenderReadPixels(sdl_renderer, &capture_rect, SDL_PIXELFORMAT_RGBA32,
                             surface->pixels, surface->pitch);
    if (read_result != 0) {
        SDL_FreeSurface(surface);
        return false;
    }

    const std::filesystem::path out_path(path);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }
    const bool ok = SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return ok;
#else
    (void)path;
    return false;
#endif
}

void Renderer::render_console(const WorldState& world) const
{
    if (!announced_console_mode_) {
        std::cout << "SDL renderer unavailable; using console fallback." << std::endl;
        const_cast<Renderer*>(this)->announced_console_mode_ = true;
    }

    if (world.frame_tick != 0 && world.frame_tick - last_console_frame_ < 15) {
        return;
    }

    const_cast<Renderer*>(this)->last_console_frame_ = world.frame_tick;

    std::cout << "[frame=" << world.frame_tick << "] "
              << "screen=" << to_string(world.screen)
              << " gameplay=" << to_string(world.gameplay_state)
              << " level=" << world.current_level
              << " score=" << world.scores.score
              << " kills=" << world.scores.enemy_kills
              << " bunker_resolutions=" << world.scores.grounded_invader_resolutions
              << " waves_exhausted=" << (world.waves_exhausted ? "yes" : "no")
              << " objects=" << world.objects.size()
              << " prompt=" << static_cast<int>(world.confirmation_prompt);

    for (std::size_t bank = 0; bank < world.wave_banks.size(); ++bank) {
        std::cout << " bank" << bank << "=" << world.wave_banks[bank].remaining_spawns;
    }

    if (world.finale_spawn_budget_total > 0) {
        std::cout << " finale=" << world.finale_spawn_budget_remaining << "/"
                  << world.finale_spawn_budget_total;
    }

    std::cout << std::endl;
}

void Renderer::render_sdl(const WorldState& world, float presentation_delta_seconds) const
{
#if defined(NITERAID_ENABLE_SDL3)
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
    SDL_SetWindowTitle(sdl_window,
                       (std::string("Night Raid  Level ") + std::to_string(world.current_level + 1) +
                        "  Score " + std::to_string(world.scores.score))
                           .c_str());
    SDL_SetRenderDrawColor(sdl_renderer, 8, 12, 24, 255);
    SDL_RenderClear(sdl_renderer);

    if (world.screen == Screen::Title) {
        SDL_SetRenderDrawColor(sdl_renderer, 230, 210, 120, 255);
        SDL_FRect rect {40.0f, 40.0f, 240.0f, 40.0f};
        SDL_RenderFillRect(sdl_renderer, &rect);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 240, 180, 255);
        SDL_FRect accent {60.0f, 100.0f, 200.0f, 12.0f};
        SDL_RenderFillRect(sdl_renderer, &accent);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 12, 18, 40, 255);
        SDL_FRect sky {0.0f, 0.0f, 320.0f, 160.0f};
        SDL_RenderFillRect(sdl_renderer, &sky);

        SDL_SetRenderDrawColor(sdl_renderer, 30, 24, 12, 255);
        SDL_FRect ground {0.0f, 160.0f, 320.0f, 40.0f};
        SDL_RenderFillRect(sdl_renderer, &ground);

        for (const auto& object : world.objects) {
            if (!object.active) {
                continue;
            }

            if (object.type == ObjectType::WaveController ||
                object.type == ObjectType::Presenter) {
                continue;
            }

            if (world.survivor_intermission_active &&
                object.type == ObjectType::LandedInvader) {
                continue;
            }

            if (object.type == ObjectType::PlayerCannon) {
                SDL_SetRenderDrawColor(sdl_renderer, 96, 150, 96, 255);
                SDL_FRect bunker {
                    static_cast<float>(object.position.x - 13.0f),
                    static_cast<float>(object.position.y - 6.0f),
                    26.0f,
                    12.0f,
                };
                SDL_RenderFillRect(sdl_renderer, &bunker);

                SDL_SetRenderDrawColor(sdl_renderer, 52, 36, 20, 255);
                SDL_FRect bunker_door {static_cast<float>(object.position.x - 6.0f),
                                       static_cast<float>(object.position.y - 1.0f), 12.0f, 9.0f};
                SDL_RenderFillRect(sdl_renderer, &bunker_door);

                if (bunker_door_is_open(world)) {
                    if (bunker_door_spill_is_visible(world)) {
                        draw_bunker_door_spill_pattern(sdl_renderer);
                    } else {
                        SDL_SetRenderDrawColor(sdl_renderer, 236, 188, 64, 255);
                        SDL_FRect open_door {163.0f, 172.0f, 5.0f, 7.0f};
                        SDL_RenderFillRect(sdl_renderer, &open_door);
                    }
                }

                SDL_SetRenderDrawColor(sdl_renderer, 170, 220, 170, 255);
                const auto angle = aim_angle_radians(object.frame);
                const auto base_x = object.position.x;
                const auto base_y = object.position.y - 8.0f;
                const auto tip_x = base_x + std::cos(angle) * 22.0f;
                const auto tip_y = base_y - std::sin(angle) * 22.0f;
                SDL_RenderLine(sdl_renderer, static_cast<float>(base_x), static_cast<float>(base_y),
                               static_cast<float>(tip_x), static_cast<float>(tip_y));

                continue;
            }

            switch (object.type) {
            case ObjectType::PlayerProjectile:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 120, 255);
                break;
            case ObjectType::Aircraft:
                if (aircraft_bucket(object.aircraft_variant) == "BigPlane") {
                    SDL_SetRenderDrawColor(sdl_renderer, 120, 190, 255, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 130, 100, 255);
                }
                break;
            case ObjectType::EnemyDeath:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 200, 110, 255);
                break;
            case ObjectType::AircraftDebris:
                SDL_SetRenderDrawColor(sdl_renderer, 230, 120, 80, 255);
                break;
            case ObjectType::SmartBomb:
                SDL_SetRenderDrawColor(sdl_renderer, 220, 80, 80, 255);
                break;
            case ObjectType::Paratrooper:
                if (object.parachute_lost) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 190, 120, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
                }
                break;
            case ObjectType::GroundedTransition:
                if (object.parachute_lost) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 170, 90, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 170, 255);
                }
                break;
            case ObjectType::LandedInvader:
                if (object.assaulting) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 150, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 190, 120, 255);
                }
                break;
            case ObjectType::ResolutionParticle:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 120, 255);
                break;
            case ObjectType::FinaleController:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 120, 180, 255);
                break;
            default:
                SDL_SetRenderDrawColor(sdl_renderer, 180, 180, 180, 255);
                break;
            }

            if (object.type == ObjectType::Aircraft) {
                const double aircraft_center_x = object.position.x +
                    aircraft_width_for_variant(object.aircraft_variant) * 0.5f;
                const float body_width = object.extent.x * 1.45f;
                const float body_height = 6.0f;
                const float wing_width = object.extent.x * 0.85f;
                const float wing_height = 5.0f;
                const float tail_width = std::max(6.0f, object.extent.x * 0.25f);
                const float nose_width = std::max(5.0f, object.extent.x * 0.18f);
                const double body_x = aircraft_center_x - body_width * 0.5f;
                const double visual_center_y = object.position.y + object.extent.y + 1.0f;
                const double body_y = std::max(0.0, visual_center_y - 3.0f);
                const double nose_x =
                    object.direction > 0 ? body_x + body_width - 2.0f : body_x - nose_width + 2.0f;
                const double tail_x =
                    object.direction > 0 ? body_x - tail_width + 2.0f : body_x + body_width - 2.0f;
                SDL_FRect body {static_cast<float>(body_x), static_cast<float>(body_y),
                                body_width, body_height};
                SDL_RenderFillRect(sdl_renderer, &body);
                SDL_SetRenderDrawColor(sdl_renderer, 240, 240, 250, 255);
                SDL_FRect wing {static_cast<float>(aircraft_center_x - wing_width * 0.5f),
                                static_cast<float>(std::max(0.0, visual_center_y - 5.0f)),
                                wing_width,
                                wing_height};
                SDL_RenderFillRect(sdl_renderer, &wing);
                if (aircraft_bucket(object.aircraft_variant) == "BigPlane") {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 250, 180, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 170, 255);
                }
                SDL_FRect nose {static_cast<float>(nose_x),
                                static_cast<float>(std::max(0.0, visual_center_y - 1.0f)), nose_width, 3.0f};
                SDL_FRect tail {static_cast<float>(tail_x),
                                static_cast<float>(std::max(0.0, visual_center_y - 2.0f)), tail_width, 3.0f};
                SDL_RenderFillRect(sdl_renderer, &nose);
                SDL_RenderFillRect(sdl_renderer, &tail);
                continue;
            }

            if (object.type == ObjectType::EnemyDeath || object.type == ObjectType::AircraftDebris) {
                const float radius = std::max(2.0f, 8.0f - static_cast<float>(object.frame));
                if (object.type == ObjectType::AircraftDebris) {
                    SDL_SetRenderDrawColor(sdl_renderer, 230, 120, 80, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 214, 96, 255);
                }
                SDL_FRect core {
                    static_cast<float>(object.position.x - radius * 0.5f),
                    static_cast<float>(object.position.y - radius * 0.5f),
                    radius,
                    radius,
                };
                SDL_RenderFillRect(sdl_renderer, &core);
                SDL_SetRenderDrawColor(sdl_renderer, 230, 82, 52, 255);
                SDL_RenderLine(sdl_renderer, static_cast<float>(object.position.x - radius),
                               static_cast<float>(object.position.y),
                               static_cast<float>(object.position.x + radius),
                               static_cast<float>(object.position.y));
                SDL_RenderLine(sdl_renderer, static_cast<float>(object.position.x),
                               static_cast<float>(object.position.y - radius),
                               static_cast<float>(object.position.x),
                               static_cast<float>(object.position.y + radius));
                continue;
            }

            if (object.type == ObjectType::LandedInvader) {
                SDL_FRect body {
                    static_cast<float>(object.position.x - 2.0f),
                    static_cast<float>(object.position.y - 6.0f),
                    4.0f,
                    6.0f,
                };
                SDL_RenderFillRect(sdl_renderer, &body);
                SDL_SetRenderDrawColor(sdl_renderer, 245, 220, 180, 255);
                SDL_FRect head {static_cast<float>(object.position.x - 1.0f),
                                static_cast<float>(object.position.y - 8.0f), 2.0f, 2.0f};
                SDL_RenderFillRect(sdl_renderer, &head);
                continue;
            }

            if (object.type == ObjectType::ResolutionParticle) {
                SDL_FRect spark {static_cast<float>(object.position.x - 1.0f),
                                 static_cast<float>(object.position.y - 1.0f), 2.0f, 2.0f};
                SDL_RenderFillRect(sdl_renderer, &spark);
                continue;
            }

            SDL_FRect rect {
                static_cast<float>(object.position.x - object.extent.x),
                static_cast<float>(object.position.y - object.extent.y),
                object.extent.x * 2.0f,
                object.extent.y * 2.0f,
            };
            SDL_RenderFillRect(sdl_renderer, &rect);
        }

        if (debug_hitboxes_) {
            SDL_SetRenderDrawColor(sdl_renderer, 255, 0, 0, 255);
            for (const auto& object : world.objects) {
                if (!object.active || object.pending_destroy) {
                    continue;
                }
                const auto bounds = internals::collision_bounds_for_object(object);
                SDL_FRect hitbox {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right - bounds.left),
                    static_cast<float>(bounds.bottom - bounds.top),
                };
                SDL_RenderRect(sdl_renderer, &hitbox);
            }
        }

        if (world.muzzle_flash_frames_remaining > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 255, 245, 130, 255);
            SDL_FRect flash {static_cast<float>(world.muzzle_flash_position.x - 2.0f),
                             static_cast<float>(world.muzzle_flash_position.y - 2.0f),
                             4.0f,
                             4.0f};
            SDL_RenderFillRect(sdl_renderer, &flash);
        }

        for (std::size_t bank = 0; bank < world.wave_banks.size(); ++bank) {
            const auto width = static_cast<float>(std::min<std::uint16_t>(
                world.wave_banks[bank].remaining_spawns, 20));
            if (width <= 0.0f) {
                continue;
            }
            SDL_SetRenderDrawColor(sdl_renderer, 90, 160, 120, 255);
            SDL_FRect meter {8.0f, 182.0f + static_cast<float>(bank) * 3.0f, width * 4.0f, 2.0f};
            SDL_RenderFillRect(sdl_renderer, &meter);
        }

        if (world.finale_spawn_budget_total > 0) {
            const auto remaining_width = static_cast<float>(world.finale_spawn_budget_remaining) * 3.0f;
            const auto total_width = static_cast<float>(world.finale_spawn_budget_total) * 3.0f;
            SDL_SetRenderDrawColor(sdl_renderer, 70, 50, 70, 255);
            SDL_FRect total {220.0f, 8.0f, total_width, 6.0f};
            SDL_RenderFillRect(sdl_renderer, &total);
            SDL_SetRenderDrawColor(sdl_renderer, 255, 120, 180, 255);
            SDL_FRect remaining {220.0f, 8.0f, remaining_width, 6.0f};
            SDL_RenderFillRect(sdl_renderer, &remaining);
        }

        if (world.screen == Screen::Intermission || overrun_intermission_banner_is_visible(world)) {
            SDL_SetRenderDrawColor(sdl_renderer, 222, 218, 188, 255);
            SDL_FRect box {112.0f, 56.0f, 96.0f, 22.0f};
            SDL_RenderFillRect(sdl_renderer, &box);
        } else if (world.screen == Screen::Gameplay && world.level_banner_frames_remaining > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 222, 218, 188, 255);
            SDL_FRect box {96.0f, 56.0f, 128.0f, 22.0f};
            SDL_RenderFillRect(sdl_renderer, &box);
        } else if (game_over_banner_is_visible(world)) {
            SDL_SetRenderDrawColor(sdl_renderer, 222, 218, 188, 255);
            SDL_FRect box {112.0f, 56.0f, 96.0f, 22.0f};
            SDL_RenderFillRect(sdl_renderer, &box);
        }

        if (world.death_flash_frames_remaining > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
            SDL_FRect flash {0.0f, 0.0f, 320.0f, 200.0f};
            SDL_RenderFillRect(sdl_renderer, &flash);
        }
    }

#elif defined(NITERAID_ENABLE_SDL2)
    auto* sdl_renderer = static_cast<SDL_Renderer*>(renderer_);
    auto* sdl_window = static_cast<SDL_Window*>(window_);
#if defined(NITERAID_REMAKE)
    if (modern_presentation_) {
        prepare_remake_frame(world);
    }
    if (remake_scene_texture_ != nullptr) {
        SDL_SetRenderTarget(
            sdl_renderer, static_cast<SDL_Texture*>(remake_scene_texture_));
        SDL_RenderSetScale(
            sdl_renderer,
            static_cast<float>(kWindowScale),
            static_cast<float>(kWindowScale));
    } else {
        SDL_SetRenderTarget(sdl_renderer, nullptr);
        SDL_RenderSetLogicalSize(sdl_renderer, kLogicalWidth, kLogicalHeight);
    }
#endif
#if defined(NITERAID_REMAKE)
    const char* presentation_label =
        modern_presentation_ ? " [Enhanced]" : " [Original Presentation]";
#else
    constexpr const char* presentation_label = "";
#endif
    SDL_SetWindowTitle(sdl_window,
                       (std::string(kWindowTitle) + presentation_label + "  Level " +
                        std::to_string(world.current_level + 1) +
                        "  Score " + std::to_string(world.scores.score))
                           .c_str());
    SDL_SetRenderDrawColor(sdl_renderer, 8, 12, 24, 255);
    SDL_RenderClear(sdl_renderer);

        if (world.screen == Screen::Title) {
            render_texture(sdl_renderer, title_texture_);
            if (title_texture_ == nullptr) {
            SDL_SetRenderDrawColor(sdl_renderer, 230, 210, 120, 255);
            SDL_Rect rect {40, 40, 240, 40};
            SDL_RenderFillRect(sdl_renderer, &rect);
            SDL_SetRenderDrawColor(sdl_renderer, 255, 240, 180, 255);
            SDL_Rect accent {60, 100, 200, 12};
            SDL_RenderFillRect(sdl_renderer, &accent);
            SDL_SetRenderDrawColor(sdl_renderer, 54, 50, 70, 255);
            draw_text(sdl_renderer, font_textures_, 88, 54, "NITE RAID", 2);
            }
        } else if (world.screen == Screen::Credits) {
            const int elapsed = std::max(0, kCreditsScreenFrames - world.credits_frames_remaining);
            const auto frame_index = attract_overlay_frame_index<kCreditsOverlayFrameCount>(elapsed);
            render_texture(sdl_renderer, credits_texture_);
            if (credits_overlay_textures_[frame_index] != nullptr) {
                render_texture(sdl_renderer, credits_overlay_textures_[frame_index]);
            }
            if (credits_texture_ == nullptr && credits_overlay_textures_[frame_index] == nullptr) {
                SDL_SetRenderDrawColor(sdl_renderer, 220, 220, 200, 255);
                SDL_RenderClear(sdl_renderer);
                SDL_SetRenderDrawColor(sdl_renderer, 54, 50, 70, 255);
                draw_text(sdl_renderer, font_textures_, 110, 60, "PRESENTED BY", 1);
                draw_text(sdl_renderer, font_textures_, 88, 100, "SOFTWARE CREATIONS", 1);
            }
        } else if (world.screen == Screen::AttractInterlude) {
            const int elapsed =
                std::max(0, kAttractInterludeScreenFrames - world.attract_interlude_frames_remaining);
            const auto frame_index =
                attract_overlay_frame_index<kAttractInterludeOverlayFrameCount>(elapsed);
            render_texture(sdl_renderer, attract_interlude_texture_);
            if (attract_interlude_overlay_textures_[frame_index] != nullptr) {
                render_texture(sdl_renderer, attract_interlude_overlay_textures_[frame_index]);
            }
            if (attract_interlude_texture_ == nullptr &&
                attract_interlude_overlay_textures_[frame_index] == nullptr) {
                SDL_SetRenderDrawColor(sdl_renderer, 28, 0, 28, 255);
                SDL_RenderClear(sdl_renderer);
            }
        } else if (world.screen == Screen::ControlPanel) {
            render_texture(sdl_renderer, control_panel_texture_);
            if (control_panel_texture_ == nullptr) {
                SDL_SetRenderDrawColor(sdl_renderer, 96, 96, 32, 255);
                SDL_RenderClear(sdl_renderer);
                SDL_SetRenderDrawColor(sdl_renderer, 222, 218, 188, 255);
                draw_text(sdl_renderer, font_textures_, 100, 12, "CONTROL PANEL", 1);
            }

            // Row top borders sampled from plane 0x01: top dark line of each
            // LED strip in the BMP. Strip interior is 17 px tall.
            static constexpr int kRowTopY[6] = {31, 51, 71, 110, 130, 150};
            static constexpr int kStripTextX = 74;
            const bool voice_row_enabled =
                world.audio_config_words[3] == 2 || world.audio_config_words[3] == 3;
            const auto displayed_option = [&](int row) {
                return world.control_panel_pending_option_row == row ? world.control_panel_pending_option_value :
                    world.audio_config_words[3 + row];
            };
            const int sound_value = std::clamp(static_cast<int>(displayed_option(0)), 0, 3);
            const int voice_value = std::clamp(static_cast<int>(displayed_option(1)), 0, 2);
            const int music_value =
                audio_internals::music_enabled(displayed_option(2)) ? 1 : 0;
            // 1480:04ca uses the live row's compact choice index, not the device
            // enum, and places disabled rows at x=2 before the blitter origin.
            const int sound_choice = sound_value -
                (sound_value >= 2 && world.audio_config_words[config_internals::kSourceHardware] == 0 ? 1 : 0);
            const int sound_widget = 0x02f + sound_choice;
            render_texture_at(sdl_renderer,
                control_panel_widget_textures_[static_cast<std::size_t>(sound_widget)],
                42, sound_widget == 0x032 ? 32 : 33);
            if (voice_row_enabled) {
                render_texture_at(sdl_renderer,
                    control_panel_widget_textures_[static_cast<std::size_t>(0x02f + voice_value)],
                    42, 53);
            } else {
                render_texture_at(sdl_renderer, control_panel_widget_textures_[0x02a], 14, 53);
            }
            render_texture_at(sdl_renderer,
                control_panel_widget_textures_[static_cast<std::size_t>(0x02d + music_value)],
                45, 75);
            if (world.control_panel_from_gameplay) {
                // Gameplay exchanges STOP/BACK relative to the title panel before
                // the selector is drawn (RunControlPanelLoop, 1480:0f7c).
                render_texture_at(sdl_renderer, control_panel_widget_textures_[0x028], 7, 111);
                render_texture_at(sdl_renderer, control_panel_widget_textures_[0x027], 7, 131);
            }
            for (int action = 0; action < 3; ++action) {
                const int row = action + 3;
                const int widget = world.control_panel_pending_action_row == row &&
                    world.control_panel_action_frames_remaining > 0 ? 0x02c : 0x02b;
                render_texture_at(sdl_renderer, control_panel_widget_textures_[widget],
                                  46, 112 + action * 20);
            }

            if (control_panel_selector_texture_ != nullptr) {
                const int top = kRowTopY[std::clamp(world.control_panel_row, 0, 5)];
                SDL_Rect destination {24, top + 7, 30, 16};
                SDL_RenderCopy(
                    sdl_renderer,
                    static_cast<SDL_Texture*>(control_panel_selector_texture_),
                    nullptr,
                    &destination);
            }

            static constexpr std::array<const char*, 4> kSoundLabels {{
                "OFF", "PC SPEAKER", "SOUND SOURCE", "SOUNDBLASTER"
            }};
            static constexpr std::array<const char*, 3> kVoiceLabels {{
                "ONE VOICE", "FOUR VOICES", "EIGHT VOICES"
            }};
            static constexpr std::array<const char*, 2> kMusicLabels {{
                "OFF", "SOUNDBLASTER"
            }};
            static constexpr std::array<const char*, 3> kTitleActionLabels {{
                "START GAME", "BACK TO DEMO", "QUIT TO DOS"
            }};
            static constexpr std::array<const char*, 3> kGameplayActionLabels {{
                "STOP GAME", "BACK TO GAME", "QUIT TO DOS"
            }};
            for (int row = 0; row < 6; ++row) {
                const char* label = nullptr;
                bool enabled = true;
                if (row < 3) {
                    if (row == 0) {
                        label = kSoundLabels[static_cast<std::size_t>(sound_value)];
                    } else if (row == 1) {
                        enabled = voice_row_enabled;
                        label = enabled ? kVoiceLabels[static_cast<std::size_t>(voice_value)] : "";
                    } else {
                        label = kMusicLabels[static_cast<std::size_t>(music_value)];
                    }
                } else {
                    label = world.control_panel_from_gameplay
                                ? kGameplayActionLabels[row - 3]
                                : kTitleActionLabels[row - 3];
                }

                const int text_y = kRowTopY[row] + 2;
                if (enabled) {
                    draw_control_panel_text(
                        sdl_renderer, control_panel_glyph_textures_, kStripTextX, text_y, label,
                        SDL_Color {255, 0, 0, 255});
                }
            }

            static constexpr std::array<const char*, 6> kTitleStatus {{
                "CHANGE THE DEVICE USED FOR SOUND EFFECTS",
                "CHANGE THE NUMBER OF SIMULTANEOUS SOUND EFFECTS",
                "TURN MUSIC ON OR OFF",
                "START A NEW GAME",
                "RETURN TO THE DEMO LOOP",
                "QUIT NIGHT RAID, AND RETURN TO DOS",
            }};
            static constexpr std::array<const char*, 6> kGameplayStatus {{
                "CHANGE THE DEVICE USED FOR SOUND EFFECTS",
                "SOUND EFFECT VOICES NOT AVAILABLE",
                "TURN MUSIC ON OR OFF",
                "STOP PLAYING YOUR CURRENT GAME",
                "RETURN TO YOUR CURRENT GAME",
                "QUIT NIGHT RAID, AND RETURN TO DOS",
            }};
            const auto& status_labels =
                world.control_panel_from_gameplay ? kGameplayStatus : kTitleStatus;
            const char* status = status_labels[static_cast<std::size_t>(world.control_panel_row)];
            if (world.control_panel_initial_hint) {
                status = "ARROW KEYS MOVE CURSOR, ENTER SELECTS";
            } else if (world.control_panel_option_feedback && world.control_panel_row < 3) {
                static constexpr std::array<const char*, 4> kSoundFeedback {{
                    "SOUND EFFECTS NOW OFF", "NOW USING THE PC SPEAKER FOR SOUND EFFECTS",
                    "NOW USING THE SOUND SOURCE FOR SOUND EFFECTS",
                    "NOW USING THE SOUNDBLASTER FOR SOUND EFFECTS",
                }};
                static constexpr std::array<const char*, 3> kVoiceFeedback {{
                    "NO MORE THAN ONE SOUND EFFECT PLAYING AT ONCE",
                    "FOUR SIMULTANEOUS SOUNDS EFFECTS POSSIBLE",
                    "EIGHT SIMULTANEOUS SOUNDS EFFECTS POSSIBLE",
                }};
                status = world.control_panel_row == 0 ? kSoundFeedback[sound_value] :
                         world.control_panel_row == 1 ? kVoiceFeedback[voice_value] :
                         music_value != 0 ? "MUSIC NOW ON" : "MUSIC NOW OFF";
            } else if (world.control_panel_row == 1 && voice_row_enabled) {
                status = "CHANGE THE NUMBER OF SIMULTANEOUS SOUND EFFECTS";
            }
            draw_bottom_hint(sdl_renderer, font_mask_textures_, status);
        } else if (world.screen == Screen::HighScoreEntry || world.screen == Screen::HighScores) {
            render_texture(sdl_renderer, high_score_texture_);
            if (high_score_texture_ == nullptr) {
                SDL_SetRenderDrawColor(sdl_renderer, 8, 12, 24, 255);
                SDL_RenderClear(sdl_renderer);
            }
            draw_high_scores(sdl_renderer, font_textures_, world);
        } else if (world.screen == Screen::SharewareEnding) {
            render_texture(sdl_renderer, shareware_ending_base_texture_);
            if (shareware_ending_base_texture_ == nullptr) {
                SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                SDL_RenderClear(sdl_renderer);
            }
            const auto frame = std::min<std::size_t>(
                world.shareware_ending_frame,
                shareware_ending_frame_textures_.size() - 1);
            render_texture_at(
                sdl_renderer, shareware_ending_frame_textures_[frame],
                shareware_ending::kFrameX, shareware_ending::kFrameY);
        } else {
        render_texture(sdl_renderer, gameplay_texture_);
        if (gameplay_texture_ == nullptr) {
            SDL_SetRenderDrawColor(sdl_renderer, 12, 18, 40, 255);
        SDL_Rect sky {0, 0, 320, 160};
        SDL_RenderFillRect(sdl_renderer, &sky);

        SDL_SetRenderDrawColor(sdl_renderer, 30, 24, 12, 255);
        SDL_Rect ground {0, 160, 320, 40};
        SDL_RenderFillRect(sdl_renderer, &ground);
        }

#if defined(NITERAID_REMAKE)
        draw_remake_persistent_debris(world);
#endif

        // 2e5e traverses layers 0..4, preserving object-slot order within each.
        std::vector<const Object*> draw_objects;
        draw_objects.reserve(world.objects.size());
        for (const auto& object : world.objects) {
            // 5008 draws terminal strips/HUD/banner directly, without 2e5e's
            // gameplay actors, including any surviving landed invaders.
            if (object.active && !terminal_game_over_presenter_is_active(world)) {
                draw_objects.push_back(&object);
            }
        }
        std::stable_sort(draw_objects.begin(), draw_objects.end(), [](const Object* a, const Object* b) {
            return internals::original_gameplay_draw_layer(*a) < internals::original_gameplay_draw_layer(*b);
        });
        for (const auto* draw_object : draw_objects) {
            const auto& object = *draw_object;
            if (!object.active) {
                continue;
            }

            if (object.type == ObjectType::WaveController ||
                object.type == ObjectType::Presenter) {
                continue;
            }

            if (world.survivor_intermission_active &&
                object.type == ObjectType::LandedInvader) {
                continue;
            }

            if (object.type == ObjectType::PlayerCannon) {
                if (terminal_game_over_presenter_is_active(world)) {
                    continue;
                }
#if defined(NITERAID_REMAKE)
                if (modern_presentation_ && world.screen == Screen::Gameplay) {
                    const float pulse = remake_light_pulse(world.frame_tick, object.frame);
                    draw_additive_glow(
                        sdl_renderer, 160.0f, 176.0f, 24.0f,
                        SDL_Color {
                            84, 142, 255,
                            static_cast<std::uint8_t>(48.0f * pulse),
                        });
                }
#endif
                draw_bunker_base(sdl_renderer, player_base_texture_);

                if (bunker_door_is_open(world)) {
                    if (bunker_door_spill_is_visible(world)) {
                        draw_bunker_door_spill_pattern(sdl_renderer);
                    } else {
                        draw_open_bunker_door(sdl_renderer);
                    }
                }

                if (world.muzzle_flash_frames_remaining > 0 && player_fire_overlay_texture_ != nullptr) {
                    SDL_Rect overlay {0x94 + 2, 0xa6 + 3, 22, 9};
                    render_texture_at(sdl_renderer, player_fire_overlay_texture_, overlay, false);
                }

                if (world.muzzle_flash_frames_remaining > 0) {
                    SDL_Rect flash {static_cast<int>(std::floor(world.muzzle_flash_position.x)),
                                    static_cast<int>(std::floor(world.muzzle_flash_position.y)),
                                    5,
                                    5};
                    if (muzzle_flash_texture_ != nullptr) {
                        render_texture_at(sdl_renderer, muzzle_flash_texture_, flash, false);
                    } else {
                        SDL_SetRenderDrawColor(sdl_renderer, 255, 245, 130, 255);
                        SDL_RenderFillRect(sdl_renderer, &flash);
                    }
                }

                if (player_mount_texture_ != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(player_mount_texture_), nullptr,
                                     nullptr, &texture_width, &texture_height);
                    SDL_Rect mount {0x88, 0xb1, texture_width, texture_height};
                    render_texture_at(sdl_renderer, player_mount_texture_, mount, false);
                }

                if (player_pivot_texture_ != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(player_pivot_texture_), nullptr,
                                     nullptr, &texture_width, &texture_height);
                    SDL_Rect pivot {0x9d + 6, 0xac, texture_width, texture_height};
                    render_texture_at(sdl_renderer, player_pivot_texture_, pivot, false);
                }

                const int aim_sprite_id =
                    std::clamp(0x1e8 + ((object.frame - 2) / 4), 0x1e8, 0x206);
                const int aim_origin_index = aim_sprite_id - 0x1e8;
                // 5008 retains the page drawn with 4dec set, not the stored aim.
                const bool normal_wave_clear = world.screen == Screen::Gameplay && world.transition_armed;
                const bool frozen_aim = (world.transition_freeze && !normal_wave_clear) ||
                    (world.screen == Screen::GameOver && overrun_intermission_banner_is_visible(world));
                void* aim_texture = frozen_aim
                                         ? player_frozen_aim_texture_
                                         : player_aim_textures_[aim_origin_index];
                if (aim_texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(aim_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    const int origin_x = frozen_aim ? 8 : kPlayerAimOriginX[aim_origin_index];
                    const int origin_y = frozen_aim ? 0 : kPlayerAimOriginY[aim_origin_index];
                    SDL_Rect aim {0x94 + origin_x, 0x97 + origin_y, texture_width, texture_height};
                    render_texture_at(sdl_renderer, aim_texture, aim, false);
                }

                if (bunker_door_is_open(world) && bunker_door_spill_is_visible(world)) {
                    draw_bunker_door_spill_pattern(sdl_renderer);
                }

                // Reconstruct the static bunker first; the layer-1 door actor
                // must then precede all layer-2 troopers, including approach.
                if (world.bunker_native_choreography) {
                    const int index = internals::bunker_door_sprite_index(world);
                    if (index >= 0) {
                        draw_texture_if_available(sdl_renderer, bunker_special_effect_textures_[index],
                            157 + kBunkerSpecialOriginX[index], 172 + kBunkerSpecialOriginY[index]);
                    }
                }

                continue;
            }

            switch (object.type) {
            case ObjectType::PlayerProjectile:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 120, 255);
                break;
            case ObjectType::Aircraft:
                if (aircraft_bucket(object.aircraft_variant) == "BigPlane") {
                    SDL_SetRenderDrawColor(sdl_renderer, 120, 190, 255, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 130, 100, 255);
                }
                break;
            case ObjectType::EnemyDeath:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 200, 110, 255);
                break;
            case ObjectType::AircraftDebris:
                SDL_SetRenderDrawColor(sdl_renderer, 230, 120, 80, 255);
                break;
            case ObjectType::SmartBomb:
                SDL_SetRenderDrawColor(sdl_renderer, 220, 80, 80, 255);
                break;
            case ObjectType::Paratrooper:
                if (object.parachute_lost) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 190, 120, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
                }
                break;
            case ObjectType::GroundedTransition:
                if (object.parachute_lost) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 170, 90, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 170, 255);
                }
                break;
            case ObjectType::LandedInvader:
                if (object.assaulting) {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 150, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 190, 120, 255);
                }
                break;
            case ObjectType::ResolutionParticle:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 120, 255);
                break;
            case ObjectType::FinaleController:
                SDL_SetRenderDrawColor(sdl_renderer, 255, 120, 180, 255);
                break;
            default:
                SDL_SetRenderDrawColor(sdl_renderer, 180, 180, 180, 255);
                break;
            }

            if (object.type == ObjectType::PlayerProjectile && projectile_texture_ != nullptr) {
                const int anchor_y = fixed_high_word(object.position.y);
                if (!modern_presentation_ && (anchor_y < 0 || anchor_y >= kLogicalHeight)) {
                    continue;
                }
                SDL_Rect projectile {
                    static_cast<int>(modern_presentation_ ? std::round(object.position.x) : std::floor(object.position.x)),
                    static_cast<int>(modern_presentation_ ? std::round(object.position.y) : std::floor(object.position.y)),
                    3,
                    3,
                };
#if defined(NITERAID_REMAKE)
                if (modern_presentation_ && world.screen == Screen::Gameplay) {
                    const float pulse = remake_light_pulse(world.frame_tick, object.timer);
                    draw_additive_glow(
                        sdl_renderer, object.position.x, object.position.y,
                        4.0f + pulse * 1.5f,
                        SDL_Color {
                            255, 190, 54,
                            static_cast<std::uint8_t>(138.0f * pulse),
                        });
                }
#endif
                render_moving_texture_at(
                    sdl_renderer, projectile_texture_, projectile, object.position,
                    modern_presentation_);
                continue;
            }

            if (object.type == ObjectType::FinaleController && object.sprite_id >= 0x28e &&
                object.sprite_id <= 0x293) {
                if (finale_backdrop_texture_ != nullptr) {
                    SDL_Rect backdrop {0x108, 0x1b, 25, 23};
                    render_texture_at(sdl_renderer, finale_backdrop_texture_, backdrop, false);
                }
                void* finale_controller_texture = finale_controller_textures_[object.sprite_id - 0x28e];
                if (finale_controller_texture == nullptr) {
                    continue;
                }
                int texture_width = 0;
                int texture_height = 0;
                SDL_QueryTexture(static_cast<SDL_Texture*>(finale_controller_texture), nullptr, nullptr,
                                 &texture_width, &texture_height);
                SDL_Rect controller {
                    static_cast<int>(std::lround(object.position.x)),
                    static_cast<int>(std::lround(object.position.y)),
                    texture_width,
                    texture_height,
                };
                render_moving_texture_at(
                    sdl_renderer, finale_controller_texture, controller, object.position,
                    modern_presentation_);
                continue;
            }

            if (object.type == ObjectType::Aircraft ||
                (object.type == ObjectType::AircraftDebris && !object.has_dropped_payload)) {
                void* aircraft_texture = nullptr;
                void* rotor_texture = nullptr;
                SDL_Point rotor_offset {0, 0};
                SDL_Point rotor_origin {0, 0};
                int body_origin_x = 0;
                const int rotor_frame = internals::aircraft_rotor_frame(object, world.frame_tick);
                switch (object.aircraft_variant) {
                case AircraftVariant::D:
                    if (object.direction > 0) {
                        rotor_texture = aircraft_d_rotor_textures_[5 + rotor_frame];
                        aircraft_texture = aircraft_d_reverse_texture_;
                        rotor_offset = {0x38, 8};
                    } else {
                        aircraft_texture = aircraft_d_texture_;
                        rotor_texture = aircraft_d_rotor_textures_[rotor_frame];
                        rotor_offset = {0x0d, 8};
                        body_origin_x = kLeftAircraftBodyOriginX;
                    }
                    break;
                case AircraftVariant::A: {
                    const int rotor_index = (object.direction > 0 ? internals::kAircraftARotorFrames : 0) +
                        rotor_frame;
                    if (rotor_index >= 0 && rotor_index < static_cast<int>(aircraft_a_rotor_textures_.size())) {
                        rotor_texture = aircraft_a_rotor_textures_[rotor_index];
                        rotor_origin = {aircraft_a_rotor_origin_x_[rotor_index],
                                        aircraft_a_rotor_origin_y_[rotor_index]};
                    }
                    if (object.direction > 0) {
                        aircraft_texture = aircraft_a_reverse_texture_;
                        rotor_offset = {0x26, 5};
                    } else {
                        aircraft_texture = aircraft_a_texture_;
                        rotor_offset = {0, 5};
                        body_origin_x = kLeftAircraftBodyOriginX;
                    }
                    break;
                }
                case AircraftVariant::B:
                    rotor_texture = aircraft_b_rotor_textures_[rotor_frame];
                    if (object.direction > 0) {
                        aircraft_texture = aircraft_b_reverse_texture_;
                        rotor_offset = {8, -1};
                    } else {
                        aircraft_texture = aircraft_b_texture_;
                        rotor_offset = {0, -1};
                    }
                    break;
                case AircraftVariant::C:
                    rotor_texture = aircraft_c_rotor_textures_[rotor_frame];
                    if (object.direction > 0) {
                        aircraft_texture = aircraft_c_reverse_texture_;
                        rotor_offset = {9, -1};
                    } else {
                        aircraft_texture = aircraft_c_texture_;
                        rotor_offset = {-5, -1};
                    }
                    break;
                }

                if (aircraft_texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(aircraft_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect destination {
                        static_cast<int>(std::lround(object.position.x)) + body_origin_x,
                        static_cast<int>(std::lround(object.position.y)),
                        texture_width,
                        texture_height,
                    };
#if defined(NITERAID_REMAKE)
                    if (modern_presentation_ && world.screen == Screen::Gameplay) {
                        draw_remake_aircraft_light(
                            sdl_renderer, destination, object, world.frame_tick);
                    }
#endif
                    render_moving_texture_at(
                        sdl_renderer, aircraft_texture, destination, object.position,
                        modern_presentation_);
                    if (rotor_texture != nullptr) {
                        int rotor_width = 0;
                        int rotor_height = 0;
                        SDL_QueryTexture(static_cast<SDL_Texture*>(rotor_texture), nullptr, nullptr,
                                         &rotor_width, &rotor_height);
                        SDL_Rect rotor {
                            destination.x - body_origin_x + rotor_offset.x + rotor_origin.x,
                            destination.y + rotor_offset.y + rotor_origin.y,
                            rotor_width,
                            rotor_height,
                        };
                        render_moving_texture_at(
                            sdl_renderer, rotor_texture, rotor, object.position,
                            modern_presentation_);
                    }
                    continue;
                }

                const int body_width = static_cast<int>(std::lround(object.extent.x * 1.45f));
                const int body_height = 6;
                const int wing_width = static_cast<int>(std::lround(object.extent.x * 0.85f));
                const int wing_height = 5;
                const int tail_width = std::max(6, static_cast<int>(std::lround(object.extent.x * 0.25f)));
                const int nose_width = std::max(5, static_cast<int>(std::lround(object.extent.x * 0.18f)));
                const int body_x = static_cast<int>(std::lround(object.position.x - body_width * 0.5f));
                const int visual_center_y =
                    static_cast<int>(std::lround(object.position.y + object.extent.y + 1.0f));
                const int body_y = std::max(0, visual_center_y - 3);
                const int nose_x = object.direction > 0 ? body_x + body_width - 2 : body_x - nose_width + 2;
                const int tail_x = object.direction > 0 ? body_x - tail_width + 2 : body_x + body_width - 2;
                SDL_Rect body {body_x, body_y, body_width, body_height};
                SDL_RenderFillRect(sdl_renderer, &body);
                SDL_SetRenderDrawColor(sdl_renderer, 240, 240, 250, 255);
                SDL_Rect wing {static_cast<int>(std::lround(object.position.x - wing_width * 0.5f)),
                               std::max(0, visual_center_y - 5),
                               wing_width,
                               wing_height};
                SDL_RenderFillRect(sdl_renderer, &wing);
                if (aircraft_bucket(object.aircraft_variant) == "BigPlane") {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 250, 180, 255);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 170, 255);
                }
                SDL_Rect nose {nose_x, std::max(0, visual_center_y - 1), nose_width, 3};
                SDL_Rect tail {tail_x, std::max(0, visual_center_y - 2), tail_width, 3};
                SDL_RenderFillRect(sdl_renderer, &nose);
                SDL_RenderFillRect(sdl_renderer, &tail);
                continue;
            }

            if (object.type == ObjectType::EnemyDeath) {
                const bool aircraft_shell = object.aircraft_variant == AircraftVariant::D &&
                    !object.has_dropped_payload;
                const int frame = aircraft_shell
                    ? std::clamp(object.frame - (object.frame > 5 ? 1 : 0), 0, 8)
                    : std::clamp(object.frame - (object.frame > 3 ? 1 : 0), 0, 5);
                void* texture = aircraft_shell ? aircraft_shell_textures_[frame] : enemy_death_textures_[frame];
                if (texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    const int draw_x_offset = aircraft_shell
                                                  ? (object.direction > 0 ? 0x2c : 0x1c) - kAircraftShellDrawOffsetX[frame]
                                                  : object.has_dropped_payload
                                                  ? (object.finale_drop ? -18 : -15)
                                                  : (object.direction > 0 ? 0 : -9);
                    const int draw_y_offset = aircraft_shell ? 0 : object.has_dropped_payload
                                                  ? (object.finale_drop ? -20 : -10)
                                                  : -13;
                    const int origin_x = aircraft_shell
                        ? aircraft_shell_origin_x_[frame] : enemy_death_origin_x_[frame];
                    const int origin_y = aircraft_shell
                        ? aircraft_shell_origin_y_[frame] : enemy_death_origin_y_[frame];
                    SDL_Rect destination {
                        static_cast<int>(modern_presentation_
                            ? std::round(object.position.x) : std::floor(object.position.x)) + draw_x_offset + origin_x,
                        static_cast<int>(modern_presentation_
                            ? std::round(object.position.y) : std::floor(object.position.y)) + draw_y_offset + origin_y,
                        texture_width,
                        texture_height,
                    };
                    render_moving_texture_at(
                        sdl_renderer, texture, destination, object.position,
                        modern_presentation_);
                    continue;
                }
            }

            if (object.type == ObjectType::AircraftDebris) {
#if defined(NITERAID_REMAKE)
                if (modern_presentation_ && object.has_dropped_payload &&
                    object.position.y >= kRemakeDebrisSettlementLine) {
                    continue;
                }
#endif
                if (object.has_dropped_payload && object.sprite_id >= 0x121 && object.sprite_id <= 0x15a) {
                    const int sprite_id = object.sprite_id + (object.frame % 8);
                    const int sprite_index = sprite_id - 0x121;
                    void* texture = aircraft_fragment_textures_[sprite_index];
                    if (texture != nullptr) {
                        int texture_width = 0;
                        int texture_height = 0;
                        SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                                         &texture_width, &texture_height);
                        SDL_Rect destination {
                            static_cast<int>(modern_presentation_
                                ? std::round(object.position.x) : std::floor(object.position.x)) + aircraft_fragment_origin_x_[sprite_index],
                            static_cast<int>(modern_presentation_
                                ? std::round(object.position.y) : std::floor(object.position.y)) + aircraft_fragment_origin_y_[sprite_index],
                            texture_width,
                            texture_height,
                        };
#if defined(NITERAID_REMAKE)
                        if (modern_presentation_ &&
                            remake_debris_overlaps_bunker(
                                static_cast<float>(destination.x),
                                static_cast<float>(destination.y),
                                static_cast<float>(destination.w),
                                static_cast<float>(destination.h))) {
                            continue;
                        }
#endif
                        render_moving_texture_at(
                            sdl_renderer, texture, destination, object.position,
                            modern_presentation_);
                        continue;
                    }
                }

                const int radius = std::max(2, 8 - object.frame);
                const int x = static_cast<int>(std::lround(object.position.x));
                const int y = static_cast<int>(std::lround(object.position.y));
                SDL_SetRenderDrawColor(sdl_renderer, 230, 120, 80, 255);
                SDL_Rect core {x - radius / 2, y - radius / 2, radius, radius};
                SDL_RenderFillRect(sdl_renderer, &core);
                continue;
            }

            if (object.type == ObjectType::SmartBomb) {
                if (object.extent.y < 6.0f) {
                    const int base_index = object.direction == 0 ? 0 : 4;
                    const int overlay_index = base_index + 1 + (object.frame + 2) % 3;
                    void* base_texture = smart_bomb_armed_textures_[base_index];
                    void* overlay_texture = smart_bomb_armed_textures_[overlay_index];
                    if (base_texture == nullptr) {
                        continue;
                    }
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(base_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect bomb {
                        fixed_high_word(object.position.x),
                        fixed_high_word(object.position.y),
                        texture_width,
                        texture_height,
                    };
                    render_texture_at(sdl_renderer, base_texture, bomb, false);
                    if (overlay_texture != nullptr) {
                        int overlay_width = 0;
                        int overlay_height = 0;
                        SDL_QueryTexture(static_cast<SDL_Texture*>(overlay_texture), nullptr, nullptr,
                                         &overlay_width, &overlay_height);
                        const int overlay_x_offset = object.direction == 0 ? -0x0d : 0x12;
                        SDL_Rect overlay {
                            bomb.x + overlay_x_offset + smart_bomb_armed_origin_x_[overlay_index],
                            bomb.y + smart_bomb_armed_origin_y_[overlay_index],
                            overlay_width,
                            overlay_height,
                        };
                        render_texture_at(sdl_renderer, overlay_texture, overlay, false);
                    }
                    continue;
                }

                const int sprite_base = object.direction == 0 ? 0 : 5;
                const int sprite_frame = sprite_base + std::clamp(object.frame, 0, 4);
                void* fall_texture = smart_bomb_fall_textures_[sprite_frame];
                if (fall_texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(fall_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect bomb {
                        fixed_high_word(object.position.x) + smart_bomb_fall_origin_x_[sprite_frame],
                        fixed_high_word(object.position.y) + smart_bomb_fall_origin_y_[sprite_frame],
                        texture_width,
                        texture_height,
                    };
                    render_texture_at(sdl_renderer, fall_texture, bomb, false);
                }
                continue;
            }

            if (object.type == ObjectType::Paratrooper && object.finale_drop) {
                const auto index = static_cast<std::size_t>(internals::finale_drop_sprite_id(object) - 0x268);
                auto* texture = static_cast<SDL_Texture*>(finale_drop_textures_[index]);
                if (texture != nullptr) {
                    int width = 0, height = 0;
                    SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
                    SDL_Rect drop {
                        fixed_high_word(object.position.x) + finale_drop_origin_x_[index],
                        fixed_high_word(object.position.y) + finale_drop_origin_y_[index], width, height};
#if defined(NITERAID_REMAKE)
                    if (modern_presentation_ && world.screen == Screen::Gameplay) {
                        draw_remake_paratrooper_light(sdl_renderer, drop, object, world.frame_tick);
                    }
#endif
                    render_moving_texture_at(sdl_renderer, texture, drop, object.position, modern_presentation_);
                }
                continue;
            }

            if ((object.type == ObjectType::Paratrooper ||
                 object.type == ObjectType::GroundedTransition) &&
                object.parachute_lost) {
                const int frame = std::clamp(object.frame, 0, 3);
                void* texture = paratrooper_no_chute_textures_[frame];
                if (texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect falling {
                        modern_presentation_
                            ? static_cast<int>(std::lround(object.position.x)) +
                                  internals::kNoChuteFrameOriginX[frame]
                            : fixed_high_word(object.position.x) +
                                  internals::kNoChuteFrameOriginX[frame],
                        modern_presentation_
                            ? static_cast<int>(std::lround(object.position.y)) +
                                  internals::kNoChuteFrameOriginY[frame]
                            : fixed_high_word(object.position.y) +
                                  internals::kNoChuteFrameOriginY[frame],
                        texture_width,
                        texture_height,
                    };
                    render_moving_texture_at(
                        sdl_renderer, texture, falling, object.position,
                        modern_presentation_);
                    continue;
                }
            }

            if (object.type == ObjectType::Paratrooper && !object.parachute_lost) {
                // 0633/06e6/071f draw a whole sprite at the stored high words.
                const WorldPosition draw_position = object.position;
                const int x = modern_presentation_
                                  ? static_cast<int>(std::lround(draw_position.x))
                                  : fixed_high_word(draw_position.x);
                const int y = modern_presentation_
                                  ? static_cast<int>(std::lround(draw_position.y))
                                  : fixed_high_word(draw_position.y);
                if (object.assault_stage == 0) {
                    const int family_offset = object.direction > 0 ? 5 : 0;
                    const int deploy_frame = std::clamp(object.frame, 0, 4);
                    const int texture_index = family_offset + deploy_frame;
                    void* texture = paratrooper_deploy_textures_[texture_index];
                    if (texture != nullptr) {
                        int texture_width = 0;
                        int texture_height = 0;
                        SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                                         &texture_width, &texture_height);
                        SDL_Rect body {
                            x + kParatrooperDeployOriginX[texture_index],
                            y + kParatrooperDeployOriginY[texture_index],
                            texture_width,
                            texture_height,
                        };
#if defined(NITERAID_REMAKE)
                        if (modern_presentation_ && world.screen == Screen::Gameplay) {
                            draw_remake_paratrooper_light(
                                sdl_renderer, body, object, world.frame_tick);
                        }
#endif
                        render_moving_texture_at(
                            sdl_renderer, texture, body, draw_position,
                            modern_presentation_);
                        continue;
                    }
                }

                const int landing_index = y - kLandingOverlayFirstY;
                if (landing_index >= 0 &&
                    landing_index < static_cast<int>(kParatrooperLandingOriginX.size())) {
                    draw_texture_if_available(
                        sdl_renderer, paratrooper_landing_textures_[landing_index],
                        x - (kLandingOverlayInitialXOffset - landing_index) +
                            kParatrooperLandingOriginX[landing_index],
                        kLandingOverlayGroundY + kParatrooperLandingOriginY[landing_index]);
                }

                if (paratrooper_frame_textures_[0] != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(paratrooper_frame_textures_[0]),
                                     nullptr, nullptr, &texture_width, &texture_height);
                    const int origin_x = kParatrooperFrameOriginX[0];
                    const int origin_y = kParatrooperFrameOriginY[0];
                    SDL_Rect body {x + origin_x, y + origin_y, texture_width, texture_height};
#if defined(NITERAID_REMAKE)
                    if (modern_presentation_ && world.screen == Screen::Gameplay) {
                        draw_remake_paratrooper_light(
                            sdl_renderer, body, object, world.frame_tick);
                    }
#endif
                    render_moving_texture_at(
                        sdl_renderer, paratrooper_frame_textures_[0], body,
                        draw_position, modern_presentation_);
                } else {
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 190, 120, 255);
                    SDL_Rect body {x - 2, y + 2, 4, 7};
                    SDL_RenderFillRect(sdl_renderer, &body);
                }

                continue;
            }

            if (object.type == ObjectType::GroundedTransition) {
                const int frame = std::clamp(object.frame, 0, 7);
                void* frame_texture = paratrooper_frame_textures_[frame];
                if (frame_texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(frame_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect grounded {
                        (modern_presentation_
                             ? static_cast<int>(std::lround(object.position.x))
                             : fixed_high_word(object.position.x)) +
                            kParatrooperFrameOriginX[frame],
                        (modern_presentation_
                             ? static_cast<int>(std::lround(object.position.y))
                             : fixed_high_word(object.position.y)) +
                            kParatrooperFrameOriginY[frame],
                        texture_width,
                        texture_height,
                    };
                    render_moving_texture_at(
                        sdl_renderer, frame_texture, grounded, object.position,
                        modern_presentation_);
                    continue;
                }

                if (grounded_invader_texture_ != nullptr) {
                    SDL_Rect grounded {
                        static_cast<int>(std::lround(object.position.x + 1.0f)),
                        static_cast<int>(std::lround(object.position.y + 8.0f)),
                        5,
                        7,
                    };
                    render_texture_at(sdl_renderer, grounded_invader_texture_, grounded, false);
                    continue;
                }
                continue;
            }

            if (object.type == ObjectType::LandedInvader) {
                if (!internals::bunker_assault_actor_draws_on_world_layer(object)) {
                    continue;
                }
                void* invader_texture = landed_invader_texture_;
                int origin_x = kParatrooperFrameOriginX[8];
                int origin_y = kParatrooperFrameOriginY[8];
                if (object.assaulting) {
                    const int assault_frame = overrun_assault_visual_frame(world, object);
                    if (object.sprite_id == 0x0fb) {
                        invader_texture = assault_invader_left_textures_[assault_frame];
                        origin_x = kAssaultLeftOriginX[assault_frame];
                        origin_y = kAssaultLeftOriginY[assault_frame];
                    } else if (object.sprite_id == 0x101) {
                        invader_texture = assault_invader_right_textures_[assault_frame];
                        origin_x = kAssaultRightOriginX[assault_frame];
                        origin_y = kAssaultRightOriginY[assault_frame];
                    }
                }

                if (invader_texture != nullptr) {
                    int texture_width = 0;
                    int texture_height = 0;
                    SDL_QueryTexture(static_cast<SDL_Texture*>(invader_texture), nullptr, nullptr,
                                     &texture_width, &texture_height);
                    SDL_Rect invader {
                        static_cast<int>(std::lround(object.position.x + origin_x)) +
                            overrun_assault_visual_x_offset(world, object),
                        static_cast<int>(std::lround(object.position.y + origin_y)),
                        texture_width,
                        texture_height,
                    };
                    render_moving_texture_at(
                        sdl_renderer, invader_texture, invader, object.position,
                        modern_presentation_);
                    continue;
                }

                SDL_Rect body {
                    static_cast<int>(std::lround(object.position.x - 2.0f)),
                    static_cast<int>(std::lround(object.position.y - 6.0f)),
                    4,
                    6,
                };
                SDL_RenderFillRect(sdl_renderer, &body);
                SDL_SetRenderDrawColor(sdl_renderer, 245, 220, 180, 255);
                SDL_Rect head {
                    static_cast<int>(std::lround(object.position.x - 1.0f)),
                    static_cast<int>(std::lround(object.position.y - 8.0f)),
                    2,
                    2,
                };
                SDL_RenderFillRect(sdl_renderer, &head);
                continue;
            }

            if (object.type == ObjectType::ResolutionParticle) {
                if (gameplay_palette_.size() != 768) continue;
                const auto offset = static_cast<std::size_t>(std::clamp(object.sprite_id, 0, 255)) * 3;
                SDL_SetRenderDrawColor(sdl_renderer,
                    static_cast<std::uint8_t>(gameplay_palette_[offset] * 255u / 63u),
                    static_cast<std::uint8_t>(gameplay_palette_[offset + 1] * 255u / 63u),
                    static_cast<std::uint8_t>(gameplay_palette_[offset + 2] * 255u / 63u), 255);
                SDL_Rect spark {
                    fixed_high_word(object.position.x),
                    fixed_high_word(object.position.y),
                    1,
                    1,
                };
                SDL_RenderFillRect(sdl_renderer, &spark);
                continue;
            }

            SDL_Rect rect {
                static_cast<int>(object.position.x - object.extent.x),
                static_cast<int>(object.position.y - object.extent.y),
                static_cast<int>(object.extent.x * 2.0f),
                static_cast<int>(object.extent.y * 2.0f),
            };
            SDL_RenderFillRect(sdl_renderer, &rect);
        }

        if (debug_hitboxes_) {
            SDL_SetRenderDrawColor(sdl_renderer, 255, 0, 0, 255);
            for (const auto& object : world.objects) {
                if (!object.active || object.pending_destroy) {
                    continue;
                }
                const auto bounds = internals::collision_bounds_for_object(object);
                SDL_Rect hitbox {
                    static_cast<int>(std::lround(bounds.left)),
                    static_cast<int>(std::lround(bounds.top)),
                    static_cast<int>(std::lround(bounds.right - bounds.left)),
                    static_cast<int>(std::lround(bounds.bottom - bounds.top)),
                };
                SDL_RenderDrawRect(sdl_renderer, &hitbox);
            }
        }

        if (!world.bunker_native_choreography && bunker_special_actor_is_visible(world)) {
            const int frame = world.bunker_special_effect_frames_remaining > 0
                                  ? std::clamp((12 - world.bunker_special_effect_frames_remaining) / 4, 0, 2)
                                  : 3;
            const int sprite_index = world.bunker_native_choreography
                ? internals::bunker_door_sprite_index(world)
                : world.bunker_special_effect_frames_remaining > 0 ? 2 - frame
                                                                 : (world.bunker_explosion_frames_remaining > 0 ? 1 : 0);
            void* texture = bunker_special_effect_textures_[sprite_index];
            if (texture != nullptr) {
                int texture_width = 0;
                int texture_height = 0;
                SDL_QueryTexture(static_cast<SDL_Texture*>(texture), nullptr, nullptr,
                                 &texture_width, &texture_height);
                SDL_Rect effect {
                    0x9d + kBunkerSpecialOriginX[sprite_index],
                    0xac + kBunkerSpecialOriginY[sprite_index],
                    texture_width,
                    texture_height,
                };
                render_texture_at(sdl_renderer, texture, effect, false);
            }
        }

        if (world.survivor_intermission_active) {
            draw_survivor_intermission_presenter(
                sdl_renderer, world, finale_controller_textures_, survivor_ufo_textures_,
                survivor_joke_textures_, survivor_retained_trooper_texture_,
                finale_backdrop_texture_,
                modern_presentation_);
        } else if (world.no_survivor_intermission_active) {
            draw_no_survivor_flyby_presenter(
                sdl_renderer, world, no_survivor_flyby_textures_);
        } else if (world.milestone_intermission != MilestoneIntermission::None) {
            draw_milestone_intermission_presenter(
                sdl_renderer, world, milestone_pizza_textures_,
                milestone_helicopter_textures_, finale_trooper_textures_,
                paratrooper_landing_textures_, bunker_special_effect_textures_,
                player_base_texture_);
        }

        if (world.screen == Screen::Finale) {
            draw_finale_presenter(
                sdl_renderer, world, finale_vehicle_texture_,
                finale_vehicle_female_texture_,
                finale_vehicle_animation_textures_, finale_trooper_textures_,
                bunker_special_effect_textures_, player_base_texture_,
                finale_particle_texture_, font_mask_textures_);
        }

        if (world.bunker_native_choreography && world.bunker_assault_active) {
            for (const auto& object : world.objects) {
                if (!object.active || object.pending_destroy || object.type != ObjectType::LandedInvader ||
                    !internals::bunker_assault_actor_draws_in_doorway(object)) continue;
                const auto frame = object.frame % 4;
                draw_texture_if_available(sdl_renderer, assault_invader_right_textures_[frame],
                    static_cast<int>(object.position.x) + kAssaultRightOriginX[frame],
                    static_cast<int>(object.position.y) + kAssaultRightOriginY[frame] +
                        (world.bunker_assault_direct_draw ? 1 : 0));
                // Layer-1 door precedes 40b2's trooper then bunker redraw.
                draw_bunker_base(sdl_renderer, player_base_texture_);
            }
        }

        if (!world.bunker_native_choreography && bunker_door_is_open(world) && bunker_door_spill_is_visible(world)) {
            draw_bunker_door_spill_pattern(sdl_renderer);
        } else if (!world.bunker_native_choreography && bunker_door_is_open(world) &&
                   bunker_assault_entrant_is_in_doorway(world)) {
            draw_open_bunker_door(sdl_renderer);
        }

        if (!world.bunker_native_choreography && world.bunker_assault_active && world.bunker_explosion_frames_remaining == 0) {
            for (const auto& object : world.objects) {
                if (!object.active || object.pending_destroy ||
                    object.type != ObjectType::LandedInvader ||
                    !internals::bunker_assault_actor_draws_in_doorway(object)) {
                    continue;
                }
                void* invader_texture = landed_invader_texture_;
                int origin_x = kParatrooperFrameOriginX[8];
                int origin_y = kParatrooperFrameOriginY[8];
                if (object.assaulting) {
                    const int assault_frame = overrun_assault_visual_frame(world, object);
                    if (object.sprite_id == 0x0fb) {
                        invader_texture = assault_invader_left_textures_[assault_frame];
                        origin_x = kAssaultLeftOriginX[assault_frame];
                        origin_y = kAssaultLeftOriginY[assault_frame];
                    } else if (object.sprite_id == 0x101) {
                        invader_texture = assault_invader_right_textures_[assault_frame];
                        origin_x = kAssaultRightOriginX[assault_frame];
                        origin_y = kAssaultRightOriginY[assault_frame];
                    }
                }
                if (invader_texture == nullptr) {
                    continue;
                }
                int texture_width = 0;
                int texture_height = 0;
                SDL_QueryTexture(static_cast<SDL_Texture*>(invader_texture), nullptr, nullptr,
                                 &texture_width, &texture_height);
                SDL_Rect invader {
                    static_cast<int>(std::lround(object.position.x + origin_x)) +
                        overrun_assault_visual_x_offset(world, object),
                    static_cast<int>(std::lround(object.position.y + origin_y)),
                    texture_width,
                    texture_height,
                };
                SDL_Rect doorway_clip {163, 0, kLogicalWidth - 163, kLogicalHeight};
#if defined(NITERAID_ENABLE_SDL3)
                SDL_SetRenderClipRect(sdl_renderer, &doorway_clip);
#else
                SDL_RenderSetClipRect(sdl_renderer, &doorway_clip);
#endif
                render_texture_at(sdl_renderer, invader_texture, invader, false);
#if defined(NITERAID_ENABLE_SDL3)
                SDL_SetRenderClipRect(sdl_renderer, nullptr);
#else
                SDL_RenderSetClipRect(sdl_renderer, nullptr);
#endif
            }
        }

        const int assault_residue_index = overrun_assault_residue_index(world);
        if (!world.bunker_native_choreography && assault_residue_index >= 0 &&
            overrun_assault_residue_textures_[static_cast<std::size_t>(assault_residue_index)] != nullptr) {
            render_texture_at(
                sdl_renderer,
                overrun_assault_residue_textures_[static_cast<std::size_t>(assault_residue_index)],
                kOverrunAssaultResidueX[static_cast<std::size_t>(assault_residue_index)],
                kOverrunAssaultResidueY[static_cast<std::size_t>(assault_residue_index)]);
        }

#if defined(NITERAID_REMAKE)
        if (modern_presentation_) {
            if (world.screen != Screen::Gameplay) {
                remake_muzzle_afterglow_seconds_ = 0.0f;
                remake_muzzle_source_seen_ = false;
            } else if (world.muzzle_flash_frames_remaining > 0 &&
                       (!remake_muzzle_source_seen_ ||
                        remake_muzzle_source_tick_ != world.frame_tick)) {
                remake_muzzle_source_seen_ = true;
                remake_muzzle_source_tick_ = world.frame_tick;
                remake_muzzle_afterglow_position_ = world.muzzle_flash_position;
                remake_muzzle_afterglow_seconds_ =
                    12.0f / static_cast<float>(kEnhancedPresentationRateHz);
            }
            draw_remake_effects(
                sdl_renderer, world, enemy_death_textures_,
                remake_muzzle_afterglow_position_, remake_muzzle_afterglow_seconds_);
            remake_muzzle_afterglow_seconds_ = std::max(
                0.0f, remake_muzzle_afterglow_seconds_ - presentation_delta_seconds);
        }
#endif

        if (terminal_game_over_presenter_is_active(world)) {
            draw_terminal_game_over_presenter(
                sdl_renderer, world, terminal_game_over_strip_textures_,
                terminal_game_over_ground_texture_, terminal_game_over_flag_textures_);
        }

        if (world.screen == Screen::Gameplay || world.screen == Screen::Intermission ||
            world.screen == Screen::GameOver || world.screen == Screen::Finale) {
            const bool particles_reach_hud = std::any_of(
                world.objects.begin(), world.objects.end(), [](const Object& object) {
                    return object.active && !object.pending_destroy &&
                           object.type == ObjectType::ResolutionParticle && object.position.y >= 188.0f;
                });
            const bool full_gameplay_hud = world.screen == Screen::Gameplay &&
                std::any_of(draw_objects.begin(), draw_objects.end(), [](const Object* object) {
                    return object->type != ObjectType::WaveController &&
                           object->type != ObjectType::Presenter &&
                           internals::collision_bounds_for_object(*object).bottom >=
                               kHudFullRedrawBottom;
                });
            if ((full_gameplay_hud || world.screen == Screen::Finale ||
                 particles_reach_hud) && gameplay_texture_ != nullptr) {
                // 2c64 draws resource 207 when the largest actor bottom reaches 187.
                SDL_Rect hud {0, 188, kLogicalWidth, 12};
                SDL_RenderCopy(sdl_renderer, static_cast<SDL_Texture*>(gameplay_texture_), &hud, &hud);
            } else if (world.screen == Screen::Gameplay && gameplay_texture_ != nullptr) {
                // Otherwise 2c64 redraws only the score and counter strips (208/209).
                SDL_Rect score {57, 191, 48, 8};
                SDL_Rect counters {221, 191, 80, 8};
                SDL_RenderCopy(sdl_renderer, static_cast<SDL_Texture*>(gameplay_texture_), &score, &score);
                SDL_RenderCopy(sdl_renderer, static_cast<SDL_Texture*>(gameplay_texture_), &counters, &counters);
            }
            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
            constexpr std::array<SDL_Rect, 5> kScoreCells {{
                {57, 191, 8, 8}, {66, 191, 8, 8}, {75, 191, 8, 8},
                {84, 191, 8, 8}, {93, 191, 7, 8},
            }};
            draw_hud_digits_right_aligned(
                sdl_renderer, std::to_string(std::clamp(world.scores.score, 0, 99999)),
                kScoreCells, hud_digit_textures_, hud_digit_origin_x_, hud_digit_origin_y_);

            constexpr std::array<SDL_Rect, 3> kKillCells {{
                {273, 191, 8, 8}, {282, 191, 8, 8}, {291, 191, 7, 8},
            }};
            draw_hud_digits_right_aligned(
                sdl_renderer, std::to_string(std::min<int>(world.scores.enemy_kills, 999)),
                kKillCells, hud_digit_textures_, hud_digit_origin_x_, hud_digit_origin_y_);

            constexpr std::array<SDL_Rect, 4> kLandedCells {{
                {221, 191, 8, 8}, {230, 191, 8, 8}, {239, 191, 8, 8},
                {248, 191, 7, 8},
            }};
            draw_hud_digits_right_aligned(
                sdl_renderer,
                std::to_string(std::min<int>(world.scores.grounded_invader_resolutions, 9999)),
                kLandedCells, hud_digit_textures_, hud_digit_origin_x_, hud_digit_origin_y_);
        }

        if (world.screen != Screen::Finale && world.finale_spawn_budget_total > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 70, 50, 70, 255);
            SDL_Rect total {132, 188, static_cast<int>(world.finale_spawn_budget_total) * 2, 4};
            SDL_RenderFillRect(sdl_renderer, &total);
            SDL_SetRenderDrawColor(sdl_renderer, 255, 120, 180, 255);
            SDL_Rect remaining {132, 188, static_cast<int>(world.finale_spawn_budget_remaining) * 2, 4};
            SDL_RenderFillRect(sdl_renderer, &remaining);
        }

        if (world.screen == Screen::Gameplay && world.level_banner_frames_remaining > 0) {
            const auto text = std::string("ASSAULT WAVE ") +
                std::to_string(world.current_level + 1);
            // 03f6 adds two spaces before 01db centers the original glyph advances.
            const int original_width = internals::high_score_name_width(text) + 10;
            const int box_x = (kLogicalWidth - original_width) / 2;
            draw_palette_banner_shadow(sdl_renderer, box_x - 4,
                                       box_x + original_width + 1);
            draw_level_entry_banner(sdl_renderer, font_mask_textures_,
                                    text, WaveBannerPanel::LevelEntry);
        }

        if (world.screen == Screen::Gameplay && world.transition_armed) {
            const auto text = std::string("ASSAULT WAVE ") +
                std::to_string(world.current_level + 1) + " COMPLETE";
            const int original_width = internals::high_score_name_width(text) + 10;
            const int box_x = (kLogicalWidth - original_width) / 2;
            draw_palette_banner_shadow(sdl_renderer, box_x - 4,
                                       box_x + original_width + 1);
            draw_level_entry_banner(sdl_renderer, font_mask_textures_, text,
                                    WaveBannerPanel::Complete);
        }

        if (world.screen == Screen::Intermission || overrun_intermission_banner_is_visible(world)) {
            draw_palette_banner_shadow(sdl_renderer, 118, 199);
            draw_intermission_banner(sdl_renderer, font_mask_textures_);
        }

        if (game_over_banner_is_visible(world)) {
            if (terminal_game_over_presenter_is_active(world)) {
                if (terminal_game_over_banner_texture_ != nullptr) {
                    render_texture_at(sdl_renderer, terminal_game_over_banner_texture_, 120, 57);
                } else {
                    draw_terminal_banner_page_residue(sdl_renderer, gameplay_texture_);
                    draw_terminal_game_over_banner(sdl_renderer, font_mask_textures_);
                }
            } else {
                draw_banner(sdl_renderer, font_mask_textures_, "GAME OVER");
            }
        }

        if (world.death_flash_frames_remaining > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
            SDL_Rect flash {0, 0, kLogicalWidth, kLogicalHeight};
            SDL_RenderFillRect(sdl_renderer, &flash);
            render_texture_at(sdl_renderer, death_flash_ground_texture_, 0, 0xbc);
        }

    }

    draw_fade_overlay(sdl_renderer, fade_alpha_for_world(world));

    if (world.confirmation_prompt != ConfirmationPromptAction::None) {
        draw_confirmation_prompt(sdl_renderer, font_mask_textures_, world.confirmation_prompt);
    }

#if defined(NITERAID_REMAKE)
    if (remake_scene_texture_ != nullptr) {
        composite_remake_frame(presentation_delta_seconds);
    }
#endif

#else
    (void)world;
#endif
}

}  // namespace niteraid
