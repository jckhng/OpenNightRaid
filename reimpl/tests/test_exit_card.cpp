#include "niteraid/exit_card.hpp"
#include "niteraid/text_mode.hpp"
#include "test_harness.hpp"

#include <cstdint>

namespace {

std::uint32_t page_checksum(const niteraid::ExitCardPage& page)
{
    std::uint32_t checksum = 2166136261u;
    for (const auto& cell : page) {
        checksum = (checksum ^ cell.character) * 16777619u;
        checksum = (checksum ^ cell.attribute) * 16777619u;
    }
    return checksum;
}

std::uint32_t raster_checksum(const niteraid::ExitCardPage& page)
{
    std::uint32_t checksum = 2166136261u;
    for (const auto pixel : niteraid::rasterize_text_mode_page(page)) {
        for (int shift = 0; shift < 32; shift += 8) {
            checksum = (checksum ^ ((pixel >> shift) & 0xffu)) * 16777619u;
        }
    }
    return checksum;
}

}  // namespace

TEST_CASE("faithful exit card matches the captured VGA text page")
{
    const auto& page = niteraid::faithful_exit_card_page();
    CHECK_EQ(page.size(), 2000u);
    CHECK_EQ(page_checksum(page), 0xa2d5d5c4u);
}

TEST_CASE("faithful startup card stages match captured VGA text pages")
{
    using niteraid::StartupCardStage;
    constexpr std::array stages {
        StartupCardStage::sound_spinner_tab,
        StartupCardStage::sound_spinner_bell,
        StartupCardStage::sound_spinner_dot,
        StartupCardStage::graphics_spinner,
        StartupCardStage::complete,
    };
    constexpr std::array<std::uint32_t, 5> checksums {{
        0x1329ba11u, 0x50a18fcbu, 0x60fe80c1u, 0x85d2b95du, 0xd8139733u,
    }};

    for (std::size_t index = 0; index < stages.size(); ++index) {
        const auto& page = niteraid::faithful_startup_card_page(stages[index]);
        CHECK_EQ(page.size(), 2000u);
        CHECK_EQ(page_checksum(page), checksums[index]);
    }
}

TEST_CASE("text mode uses native VGA scanlines rather than stretched 8x8 glyphs")
{
    niteraid::ExitCardPage page {};
    page[0] = {'A', 0x0f};
    page[1] = {'_', 0x0f};
    page[2] = {0x01, 0x0f};
    constexpr std::array<std::array<std::uint8_t, 16>, 3> expected {{
        {{0x00, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe,
          0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00}},
        {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00}},
        {{0x00, 0x00, 0x7e, 0x81, 0xa5, 0x81, 0x81, 0xbd,
          0x99, 0x81, 0x81, 0x7e, 0x00, 0x00, 0x00, 0x00}},
    }};
    const auto pixels = niteraid::rasterize_text_mode_page(page);
    for (std::size_t cell = 0; cell < expected.size(); ++cell) {
        for (int row = 0; row < niteraid::kTextModeCellHeight; ++row) {
            std::uint8_t bits = 0;
            for (int column = 0; column < 8; ++column) {
                const auto offset = row * niteraid::kTextModeRasterWidth +
                    cell * niteraid::kTextModeCellWidth + column;
                if (pixels[offset] == 0xffffffffu) {
                    bits |= 0x80u >> column;
                }
            }
            CHECK_EQ(bits, expected[cell][row]);
        }
    }
}

TEST_CASE("VGA text cells extend box lines but leave the ninth ASCII column blank")
{
    CHECK_EQ(niteraid::kTextModeCellWidth, 9);
    CHECK_EQ(niteraid::kTextModeRasterWidth, 720);
    CHECK_EQ(niteraid::kTextModeRasterHeight, 400);
    if (niteraid::kTextModeCellWidth != 9) {
        return;
    }
    niteraid::ExitCardPage page {};
    page[0] = {0xc4, 0x0f};
    page[1] = {0xc4, 0x0f};
    page[2] = {'_', 0x0f};
    page[3] = {0xb0, 0x0f};
    page.back() = {0xdb, 0x1e};
    const auto pixels = niteraid::rasterize_text_mode_page(page);
    for (int column = 0; column < 18; ++column) {
        CHECK_EQ(pixels[7 * niteraid::kTextModeRasterWidth + column], 0xffffffffu);
    }
    CHECK_EQ(pixels[13 * niteraid::kTextModeRasterWidth + 26], 0x000000ffu);
    CHECK_EQ(pixels[35], 0x000000ffu);
    CHECK_EQ(pixels.back(), 0xffff55ffu);
}

TEST_CASE("complete text rasters match the captured BIOS font and original exit screenshot")
{
    CHECK_EQ(raster_checksum(niteraid::faithful_startup_card_page(
        niteraid::StartupCardStage::complete)), 0x59f5836au);
    CHECK_EQ(raster_checksum(niteraid::faithful_exit_card_page()), 0xac11205bu);
    niteraid::ExitCardPage glyphs {};
    for (std::size_t index = 0; index < 256; ++index) {
        glyphs[index] = {static_cast<std::uint8_t>(index), 0x0f};
    }
    CHECK_EQ(raster_checksum(glyphs), 0x8d339cedu);
}
