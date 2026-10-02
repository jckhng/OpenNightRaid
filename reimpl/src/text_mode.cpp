#include "niteraid/text_mode.hpp"

#include "../third_party/pcface/font8x16.hpp"

namespace niteraid {

namespace {

struct ExitCardRgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

constexpr std::array<ExitCardRgb, 16> kCgaPalette {{
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xaa},
    {0x00, 0xaa, 0x00}, {0x00, 0xaa, 0xaa},
    {0xaa, 0x00, 0x00}, {0xaa, 0x00, 0xaa},
    {0xaa, 0x55, 0x00}, {0xaa, 0xaa, 0xaa},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xff},
    {0x55, 0xff, 0x55}, {0x55, 0xff, 0xff},
    {0xff, 0x55, 0x55}, {0xff, 0x55, 0xff},
    {0xff, 0xff, 0x55}, {0xff, 0xff, 0xff},
}};

std::uint32_t exit_card_pixel(ExitCardRgb color)
{
    return (static_cast<std::uint32_t>(color.red) << 24) |
           (static_cast<std::uint32_t>(color.green) << 16) |
           (static_cast<std::uint32_t>(color.blue) << 8) | 0xffu;
}

} // namespace

std::vector<std::uint32_t> rasterize_text_mode_page(const ExitCardPage& page)
{
    constexpr std::uint8_t kLineGraphicsFirst = 0xc0;
    constexpr std::uint8_t kLineGraphicsLast = 0xdf;
    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(kTextModeRasterWidth) * kTextModeRasterHeight);

    for (std::size_t index = 0; index < page.size(); ++index) {
        const auto& item = page[index];
        const auto foreground = exit_card_pixel(kCgaPalette[item.attribute & 0x0f]);
        const auto background = exit_card_pixel(kCgaPalette[(item.attribute >> 4) & 0x07]);
        const int x = static_cast<int>(index % kExitCardColumns) * kTextModeCellWidth;
        const int y = static_cast<int>(index / kExitCardColumns) * kTextModeCellHeight;
        const auto& glyph = text_font::kVgaFont8x16[item.character];
        const bool extend_line = item.character >= kLineGraphicsFirst &&
                                 item.character <= kLineGraphicsLast;
        for (int row = 0; row < kTextModeCellHeight; ++row) {
            const auto offset = static_cast<std::size_t>(y + row) * kTextModeRasterWidth + x;
            for (int column = 0; column < kTextModeGlyphWidth; ++column) {
                pixels[offset + column] = (glyph[row] & (0x80u >> column)) ?
                    foreground : background;
            }
            // VGA repeats the eighth dot only for line-graphics characters.
            pixels[offset + kTextModeGlyphWidth] = (extend_line && (glyph[row] & 1u)) ?
                foreground : background;
        }
    }
    return pixels;
}

} // namespace niteraid

