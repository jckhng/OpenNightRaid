#pragma once

#include "niteraid/exit_card.hpp"

#include <cstdint>
#include <vector>

namespace niteraid {

constexpr int kTextModeGlyphWidth = 8;
constexpr int kTextModeCellWidth = 9;
constexpr int kTextModeCellHeight = 16;
constexpr int kTextModeRasterWidth = kExitCardColumns * kTextModeCellWidth;
constexpr int kTextModeRasterHeight = kExitCardRows * kTextModeCellHeight;
constexpr int kTextModeDisplayHeight = kTextModeRasterWidth * 3 / 4;

[[nodiscard]] std::vector<std::uint32_t> rasterize_text_mode_page(const ExitCardPage& page);

} // namespace niteraid
