#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace niteraid {

constexpr std::size_t kExitCardColumns = 80;
constexpr std::size_t kExitCardRows = 25;

struct TextModeCell {
    std::uint8_t character = ' ';
    std::uint8_t attribute = 0x07;
};

using ExitCardPage = std::array<TextModeCell, kExitCardColumns * kExitCardRows>;

enum class StartupCardStage : std::uint8_t {
    sound_spinner_tab,
    sound_spinner_bell,
    sound_spinner_dot,
    graphics_spinner,
    complete,
};

[[nodiscard]] const ExitCardPage& faithful_exit_card_page();
[[nodiscard]] const ExitCardPage& faithful_startup_card_page(StartupCardStage stage);

}  // namespace niteraid
