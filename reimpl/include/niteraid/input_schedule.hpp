#pragma once

#include "niteraid/game_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <vector>

namespace niteraid {

class InputSchedule {
public:
    static InputSchedule load(const std::filesystem::path& path);
    static InputSchedule parse(std::istream& input);

    [[nodiscard]] std::uint32_t count() const { return count_; }
    void advance(std::uint32_t index);
    [[nodiscard]] InputState apply(InputState input) const;

private:
    enum class Key : std::uint8_t { Left, Right, Up, Down, Space, Escape, Return, Tab, Count };
    struct Event {
        std::uint32_t index;
        Key key;
        bool pressed;
    };

    std::uint32_t count_ = 0;
    std::vector<Event> events_ {};
    std::array<bool, static_cast<std::size_t>(Key::Count)> held_ {};
    std::size_t cursor_ = 0;
    std::optional<std::uint32_t> last_index_ {};
};

}  // namespace niteraid
