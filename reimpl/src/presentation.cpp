#include "niteraid/presentation.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace niteraid {

Vec2 original_presenter_route_position(Vec2 from, Vec2 to, float updates,
                                       std::uint32_t duration, bool snap_to_pixels)
{
    if (duration == 0) {
        return to;
    }
    const auto coordinate = [updates, duration, snap_to_pixels](float start, float target) {
        const auto start_fixed = static_cast<std::int64_t>(std::llround(start * 65536.0));
        const auto target_fixed = static_cast<std::int64_t>(std::llround(target * 65536.0));
        const auto velocity = (target_fixed - start_fixed) / static_cast<std::int64_t>(duration);
        const double position = (static_cast<double>(start_fixed) + velocity *
                                 static_cast<double>(updates)) / 65536.0;
        // Snap before narrowing: float can round a 16.16 value up at x >= 256.
        return static_cast<float>(snap_to_pixels ? std::floor(position) : position);
    };
    return {coordinate(from.x, to.x), coordinate(from.y, to.y)};
}

namespace {

constexpr float kMaximumInterpolatedDisplacement = 12.0f;

bool screen_supports_interpolation(Screen screen)
{
    switch (screen) {
    case Screen::Gameplay:
    case Screen::Intermission:
    case Screen::GameOver:
    case Screen::Finale:
        return true;
    case Screen::SharewareEnding:
        return false;
    case Screen::Title:
    case Screen::Credits:
    case Screen::AttractInterlude:
    case Screen::ControlPanel:
    case Screen::HighScoreEntry:
    case Screen::HighScores:
        return false;
    }
    return false;
}

bool is_same_presented_object(const Object& previous, const Object& current)
{
    if (!previous.active || !current.active ||
        previous.pending_destroy || current.pending_destroy ||
        previous.presentation_id == 0 ||
        previous.presentation_id != current.presentation_id ||
        previous.type != current.type) {
        return false;
    }
    if (current.type == ObjectType::Aircraft &&
        previous.aircraft_variant != current.aircraft_variant) {
        return false;
    }
    if (current.type == ObjectType::SmartBomb &&
        (previous.extent.y < 6.0f) != (current.extent.y < 6.0f)) {
        return false;
    }

    const double delta_x = current.position.x - previous.position.x;
    const double delta_y = current.position.y - previous.position.y;
    return std::abs(delta_x) <= kMaximumInterpolatedDisplacement &&
           std::abs(delta_y) <= kMaximumInterpolatedDisplacement;
}

double interpolate(double previous, double current, float alpha)
{
    return previous + (current - previous) * alpha;
}

}  // namespace

WorldState make_presentation_world(const WorldState& previous,
                                   const WorldState& current,
                                   float alpha)
{
    WorldState presented = current;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    presented.presentation_alpha = 1.0f;

    if (!screen_supports_interpolation(current.screen) ||
        previous.screen != current.screen ||
        previous.gameplay_state != current.gameplay_state ||
        previous.frame_tick + 1 != current.frame_tick) {
        return presented;
    }
    presented.presentation_alpha = alpha;

    std::unordered_map<std::uint32_t, const Object*> previous_objects;
    previous_objects.reserve(previous.objects.size());
    for (const auto& object : previous.objects) {
        if (object.presentation_id != 0) {
            previous_objects.emplace(object.presentation_id, &object);
        }
    }

    for (auto& presented_object : presented.objects) {
        const auto prior = previous_objects.find(presented_object.presentation_id);
        if (prior == previous_objects.end() ||
            !is_same_presented_object(*prior->second, presented_object)) {
            continue;
        }

        presented_object.position.x =
            interpolate(prior->second->position.x, presented_object.position.x, alpha);
        presented_object.position.y =
            interpolate(prior->second->position.y, presented_object.position.y, alpha);
    }

    return presented;
}

}  // namespace niteraid
