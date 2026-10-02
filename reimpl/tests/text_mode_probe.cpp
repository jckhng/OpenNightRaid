#include "niteraid/text_mode.hpp"

#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: text_mode_probe startup|exit|glyphs output.ppm\n";
        return 1;
    }
    niteraid::ExitCardPage page {};
    const std::string_view mode = argv[1];
    if (mode == "startup") {
        page = niteraid::faithful_startup_card_page(niteraid::StartupCardStage::complete);
    } else if (mode == "exit") {
        page = niteraid::faithful_exit_card_page();
    } else if (mode == "glyphs") {
        for (std::size_t index = 0; index < 256; ++index) {
            page[index] = {static_cast<std::uint8_t>(index), 0x0f};
        }
    } else {
        std::cerr << "Unknown text page: " << mode << '\n';
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    if (!output) {
        return 1;
    }
    output << "P6\n" << niteraid::kTextModeRasterWidth << ' '
           << niteraid::kTextModeRasterHeight << "\n255\n";
    for (const auto pixel : niteraid::rasterize_text_mode_page(page)) {
        const char rgb[] = {
            static_cast<char>(pixel >> 24), static_cast<char>(pixel >> 16),
            static_cast<char>(pixel >> 8),
        };
        output.write(rgb, sizeof(rgb));
    }
    return output ? 0 : 1;
}
