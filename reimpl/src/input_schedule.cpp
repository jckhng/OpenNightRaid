#include "niteraid/input_schedule.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace niteraid {
namespace {

constexpr std::string_view kHeader = "niteraid-input-schedule-v1";

std::uint32_t decimal(const std::string& text)
{
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) {
        throw std::runtime_error("input schedule index is not decimal");
    }
    const auto value = std::stoull(text);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("input schedule index exceeds DWORD range");
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

InputSchedule InputSchedule::load(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open input schedule: " + path.string());
    }
    return parse(input);
}

InputSchedule InputSchedule::parse(std::istream& input)
{
    InputSchedule schedule;
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("missing input schedule header");
    }
    std::istringstream header(line);
    std::string magic, count_text, extra;
    if (!(header >> magic >> count_text) || header >> extra || magic != kHeader) {
        throw std::runtime_error("invalid input schedule header");
    }
    schedule.count_ = decimal(count_text);
    if (schedule.count_ == 0) {
        throw std::runtime_error("input schedule count must be positive");
    }

    std::array<bool, static_cast<std::size_t>(Key::Count)> declared_held {};
    std::optional<std::uint32_t> previous_index;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string index_text, qcode, action;
        if (!(row >> index_text >> qcode >> action) || row >> extra) {
            throw std::runtime_error("invalid input schedule event");
        }
        const auto index = decimal(index_text);
        if (index >= schedule.count_ || (previous_index && index < *previous_index)) {
            throw std::runtime_error("input schedule event is outside ordered updates");
        }
        constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> names = {
            "left", "right", "up", "down", "spc", "esc", "ret", "tab"};
        const auto found = std::find(names.begin(), names.end(), qcode);
        if (found == names.end() || (action != "down" && action != "up")) {
            throw std::runtime_error("unsupported input schedule event");
        }
        const auto key_index = static_cast<std::size_t>(found - names.begin());
        const bool pressed = action == "down";
        if (declared_held[key_index] == pressed) {
            throw std::runtime_error("repeated press or unmatched release in input schedule");
        }
        declared_held[key_index] = pressed;
        schedule.events_.push_back({index, static_cast<Key>(key_index), pressed});
        previous_index = index;
    }
    if (!input.eof()) {
        throw std::runtime_error("failed reading input schedule");
    }
    return schedule;
}

void InputSchedule::advance(std::uint32_t index)
{
    if (index >= count_ || (last_index_ && index != *last_index_ + 1) ||
        (!last_index_ && index != 0)) {
        throw std::runtime_error("input schedule update index is not contiguous");
    }
    while (cursor_ < events_.size() && events_[cursor_].index == index) {
        const auto& event = events_[cursor_++];
        held_[static_cast<std::size_t>(event.key)] = event.pressed;
    }
    last_index_ = index;
}

InputState InputSchedule::apply(InputState input) const
{
    input.move_left |= held_[static_cast<std::size_t>(Key::Left)];
    input.move_right |= held_[static_cast<std::size_t>(Key::Right)];
    input.move_up |= held_[static_cast<std::size_t>(Key::Up)];
    input.move_down |= held_[static_cast<std::size_t>(Key::Down)];
    input.fire |= held_[static_cast<std::size_t>(Key::Space)];
    input.escape |= held_[static_cast<std::size_t>(Key::Escape)];
    input.start |= held_[static_cast<std::size_t>(Key::Return)];
    input.accept |= held_[static_cast<std::size_t>(Key::Return)];
    input.menu |= held_[static_cast<std::size_t>(Key::Tab)];
    return input;
}

}  // namespace niteraid
