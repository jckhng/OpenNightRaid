#include "niteraid/exit_card.hpp"

#include <algorithm>
#include <string_view>

namespace niteraid {

namespace {

constexpr std::uint8_t kFullBlock = 0xdb;
constexpr std::uint8_t kLowerHalfBlock = 0xdc;
constexpr std::uint8_t kUpperHalfBlock = 0xdf;
constexpr std::uint8_t kHorizontalLine = 0xc4;
constexpr std::uint8_t kVerticalLine = 0xb3;
constexpr std::uint8_t kTopJunction = 0xc2;
constexpr std::uint8_t kLeftJunction = 0xc3;
constexpr std::uint8_t kBottomJunction = 0xc1;
constexpr std::uint8_t kMiddleDot = 0xfa;
constexpr std::uint8_t kCheckMark = 0xfb;

TextModeCell& cell(ExitCardPage& page, std::size_t row, std::size_t column)
{
    return page[row * kExitCardColumns + column];
}

void set_attribute(ExitCardPage& page, std::size_t row,
                   std::size_t first_column, std::size_t last_column,
                   std::uint8_t attribute)
{
    for (std::size_t column = first_column; column <= last_column; ++column) {
        cell(page, row, column).attribute = attribute;
    }
}

void set_characters(ExitCardPage& page, std::size_t row,
                    std::size_t first_column, std::size_t last_column,
                    std::uint8_t character)
{
    for (std::size_t column = first_column; column <= last_column; ++column) {
        cell(page, row, column).character = character;
    }
}

void put_text(ExitCardPage& page, std::size_t row, std::size_t column,
              std::string_view text)
{
    for (char character : text) {
        cell(page, row, column++).character = static_cast<std::uint8_t>(character);
    }
}

void fill_cells(ExitCardPage& page, std::size_t row,
                std::size_t first_column, std::size_t last_column,
                std::uint8_t character, std::uint8_t attribute)
{
    for (std::size_t column = first_column; column <= last_column; ++column) {
        cell(page, row, column) = {character, attribute};
    }
}

ExitCardPage make_startup_card_page()
{
    ExitCardPage page {};
    for (auto& item : page) {
        item = {' ', 0x17};
    }
    cell(page, 6, 6).attribute = 0x18;

    fill_cells(page, 1, 22, 55, kLowerHalfBlock, 0x1f);
    cell(page, 1, 56) = {kLowerHalfBlock, 0x17};
    cell(page, 2, 22) = {kFullBlock, 0x0f};
    fill_cells(page, 2, 23, 55, ' ', 0x74);
    cell(page, 2, 56) = {kFullBlock, 0x08};
    fill_cells(page, 2, 57, 58, kUpperHalfBlock, 0x01);
    for (std::size_t row = 3; row <= 7; ++row) {
        cell(page, row, 22) = {kFullBlock, 0x0f};
        fill_cells(page, row, 23, 55, ' ', 0x74);
        cell(page, row, 56) = {
            kFullBlock, static_cast<std::uint8_t>(row == 6 ? 0x18 : 0x08)};
        fill_cells(page, row, 57, 58, ' ', 0x08);
    }
    fill_cells(page, 8, 22, 22, kUpperHalfBlock, 0x17);
    fill_cells(page, 8, 23, 23, kUpperHalfBlock, 0x18);
    fill_cells(page, 8, 24, 56, kUpperHalfBlock, 0x08);
    fill_cells(page, 8, 57, 58, ' ', 0x08);
    fill_cells(page, 9, 24, 58, kLowerHalfBlock, 0x01);
    cell(page, 9, 70).attribute = 0x18;

    fill_cells(page, 10, 15, 62, kLowerHalfBlock, 0x1f);
    cell(page, 10, 63) = {kLowerHalfBlock, 0x17};
    cell(page, 11, 15) = {kFullBlock, 0x0f};
    fill_cells(page, 11, 16, 62, kHorizontalLine, 0x78);
    cell(page, 11, 63) = {kFullBlock, 0x08};
    fill_cells(page, 11, 64, 65, kUpperHalfBlock, 0x01);
    cell(page, 12, 15) = {kFullBlock, 0x0f};
    cell(page, 12, 16).attribute = 0x74;
    set_attribute(page, 12, 17, 61, 0x71);
    cell(page, 12, 62).attribute = 0x74;
    cell(page, 12, 63) = {kFullBlock, 0x08};
    cell(page, 12, 64).attribute = 0x07;
    cell(page, 12, 65).attribute = 0x08;
    cell(page, 13, 15) = {kFullBlock, 0x0f};
    fill_cells(page, 13, 16, 31, kHorizontalLine, 0x78);
    cell(page, 13, 32) = {kTopJunction, 0x78};
    fill_cells(page, 13, 33, 62, kHorizontalLine, 0x78);
    cell(page, 13, 63) = {kFullBlock, 0x08};
    set_attribute(page, 13, 64, 65, 0x08);
    cell(page, 13, 72).attribute = 0x1f;
    for (std::size_t row = 14; row <= 18; ++row) {
        cell(page, row, 15) = {kFullBlock, 0x7f};
        set_attribute(page, row, 16, 31, 0x71);
        cell(page, row, 32) = {kVerticalLine, 0x78};
        set_attribute(page, row, 33, 62, 0x71);
        cell(page, row, 63) = {kFullBlock, 0x08};
        set_attribute(page, row, 64, 65, 0x08);
    }
    set_attribute(page, 14, 30, 31, 0x79);
    cell(page, 19, 15) = {kFullBlock, 0x1f};
    fill_cells(page, 19, 16, 31, kHorizontalLine, 0x78);
    cell(page, 19, 32) = {kBottomJunction, 0x78};
    fill_cells(page, 19, 33, 62, kHorizontalLine, 0x78);
    cell(page, 19, 63) = {kFullBlock, 0x08};
    set_attribute(page, 19, 64, 65, 0x08);
    cell(page, 20, 15) = {kUpperHalfBlock, 0x17};
    cell(page, 20, 16) = {kUpperHalfBlock, 0x18};
    fill_cells(page, 20, 17, 63, kUpperHalfBlock, 0x08);
    set_attribute(page, 20, 64, 65, 0x08);
    fill_cells(page, 21, 17, 65, kLowerHalfBlock, 0x01);

    put_text(page, 3, 32, "Night Raid v1.1");
    put_text(page, 5, 24, "Copyright 1992,1993, Argo Games");
    put_text(page, 6, 30, "All Rights Reserved.");
    put_text(page, 12, 26, "Please wait - initializing.");

    const auto put_status = [&](std::size_t row, std::string_view label,
                                std::size_t dot_column, std::string_view status) {
        put_text(page, row, 18, label);
        fill_cells(page, row, dot_column, 28, kMiddleDot, 0x71);
        cell(page, row, 29) = {kCheckMark, 0x71};
        put_text(page, row, 34, status);
    };
    put_status(14, "Files", 23, "Ok. 386 detected.");
    put_status(15, "Input", 23, "Mouse present.");
    put_status(16, "Memory", 24, "607k available.");
    put_status(17, "Sound", 23, "SoundBlaster detected.");
    put_status(18, "Graphics", 26, "Fast palette enabled.");

    return page;
}

ExitCardPage make_startup_stage(StartupCardStage stage)
{
    auto page = make_startup_card_page();
    if (stage == StartupCardStage::complete) {
        return page;
    }

    if (stage != StartupCardStage::graphics_spinner) {
        set_attribute(page, 17, 18, 29, 0x79);
        set_attribute(page, 17, 34, 60, 0x79);
        switch (stage) {
        case StartupCardStage::sound_spinner_tab:
            cell(page, 17, 29).character = 0x09;
            break;
        case StartupCardStage::sound_spinner_bell:
            cell(page, 17, 29).character = 0x07;
            break;
        case StartupCardStage::sound_spinner_dot:
            cell(page, 17, 29).character = 0xf9;
            break;
        default:
            break;
        }
        cell(page, 18, 29).character = 0x09;
        put_text(page, 18, 34, "                     ");
        return page;
    }

    set_attribute(page, 18, 18, 29, 0x79);
    set_attribute(page, 18, 34, 60, 0x79);
    cell(page, 18, 29).character = 0x09;
    return page;
}

void set_common_shadow(ExitCardPage& page, std::size_t row)
{
    set_attribute(page, row, 75, 77, 0x08);
    set_attribute(page, row, 78, 78, 0x17);
}

ExitCardPage make_exit_card_page()
{
    ExitCardPage page {};
    for (auto& item : page) {
        item = {' ', 0x19};
    }

    set_attribute(page, 24, 0, 79, 0x07);
    for (std::size_t row = 1; row <= 12; ++row) {
        set_attribute(page, row, 1, 1, 0x17);
    }
    for (std::size_t row = 13; row <= 22; ++row) {
        set_attribute(page, row, 1, 2, 0x17);
    }
    set_attribute(page, 23, 76, 76, 0x17);

    set_attribute(page, 1, 4, 74, 0x1f);
    set_attribute(page, 1, 75, 78, 0x17);

    set_attribute(page, 2, 4, 4, 0x0f);
    set_attribute(page, 2, 5, 74, 0x78);
    set_attribute(page, 2, 75, 75, 0x08);
    set_attribute(page, 2, 76, 77, 0x01);
    set_attribute(page, 2, 78, 78, 0x17);

    set_attribute(page, 3, 4, 4, 0x0f);
    set_attribute(page, 3, 5, 74, 0x71);
    set_attribute(page, 3, 75, 75, 0x08);
    set_attribute(page, 3, 76, 76, 0x07);
    set_attribute(page, 3, 77, 77, 0x08);
    set_attribute(page, 3, 78, 78, 0x17);

    set_attribute(page, 4, 4, 4, 0x0f);
    set_attribute(page, 4, 5, 74, 0x78);
    set_common_shadow(page, 4);

    for (std::size_t row = 5; row <= 9; ++row) {
        set_attribute(page, row, 4, 4, 0x7f);
        set_attribute(page, row, 5, 39, 0x71);
        set_attribute(page, row, 40, 40, 0x78);
        set_attribute(page, row, 41, 74, 0x74);
        set_common_shadow(page, row);
    }

    set_attribute(page, 10, 4, 4, 0x7f);
    set_attribute(page, 10, 5, 39, 0x71);
    set_attribute(page, 10, 40, 74, 0x78);
    set_common_shadow(page, 10);

    for (std::size_t row = 11; row <= 14; ++row) {
        set_attribute(page, row, 4, 4, 0x7f);
        set_attribute(page, row, 5, 39, 0x71);
        set_attribute(page, row, 40, 40, 0x78);
        set_attribute(page, row, 41, 73, 0x75);
        set_attribute(page, row, 74, 74, 0x71);
        set_common_shadow(page, row);
    }

    for (std::size_t row = 15; row <= 19; ++row) {
        set_attribute(page, row, 4, 4, 0x7f);
        set_attribute(page, row, 5, 39, 0x71);
        set_attribute(page, row, 40, 40, 0x78);
        set_common_shadow(page, row);
    }
    set_attribute(page, 15, 41, 60, 0x75);
    set_attribute(page, 15, 61, 74, 0x71);
    set_attribute(page, 16, 41, 70, 0x75);
    set_attribute(page, 16, 71, 74, 0x71);
    set_attribute(page, 17, 41, 68, 0x75);
    set_attribute(page, 17, 69, 74, 0x71);
    for (std::size_t row = 18; row <= 19; ++row) {
        set_attribute(page, row, 41, 41, 0x71);
        set_attribute(page, row, 42, 73, 0x75);
        set_attribute(page, row, 74, 74, 0x71);
    }

    set_attribute(page, 20, 4, 4, 0x1f);
    set_attribute(page, 20, 5, 74, 0x78);
    set_common_shadow(page, 20);
    set_attribute(page, 21, 4, 4, 0x17);
    set_attribute(page, 21, 5, 5, 0x18);
    set_attribute(page, 21, 6, 77, 0x08);
    set_attribute(page, 21, 78, 78, 0x17);
    set_attribute(page, 22, 4, 5, 0x17);
    set_attribute(page, 22, 6, 77, 0x01);
    set_attribute(page, 22, 78, 78, 0x17);

    set_characters(page, 1, 4, 75, kLowerHalfBlock);
    cell(page, 2, 4).character = kFullBlock;
    set_characters(page, 2, 5, 74, kHorizontalLine);
    cell(page, 2, 75).character = kFullBlock;
    set_characters(page, 2, 76, 77, kUpperHalfBlock);
    cell(page, 3, 4).character = kFullBlock;
    cell(page, 3, 75).character = kFullBlock;
    cell(page, 4, 4).character = kFullBlock;
    set_characters(page, 4, 5, 39, kHorizontalLine);
    cell(page, 4, 40).character = kTopJunction;
    set_characters(page, 4, 41, 74, kHorizontalLine);
    cell(page, 4, 75).character = kFullBlock;
    for (std::size_t row = 5; row <= 9; ++row) {
        cell(page, row, 4).character = kFullBlock;
        cell(page, row, 40).character = kVerticalLine;
        cell(page, row, 75).character = kFullBlock;
    }
    cell(page, 10, 4).character = kFullBlock;
    cell(page, 10, 40).character = kLeftJunction;
    set_characters(page, 10, 41, 74, kHorizontalLine);
    cell(page, 10, 75).character = kFullBlock;
    for (std::size_t row = 11; row <= 19; ++row) {
        cell(page, row, 4).character = kFullBlock;
        cell(page, row, 40).character = kVerticalLine;
        cell(page, row, 75).character = kFullBlock;
    }
    cell(page, 20, 4).character = kFullBlock;
    set_characters(page, 20, 5, 39, kHorizontalLine);
    cell(page, 20, 40).character = kBottomJunction;
    set_characters(page, 20, 41, 74, kHorizontalLine);
    cell(page, 20, 75).character = kFullBlock;
    set_characters(page, 21, 4, 75, kUpperHalfBlock);
    set_characters(page, 22, 6, 77, kLowerHalfBlock);

    put_text(page, 3, 25, "Thanks for playing Night Raid!");
    put_text(page, 5, 8, "Night Raid took months of work");
    put_text(page, 5, 69, "_");
    put_text(page, 6, 7, "by dedicated professionals.");
    put_text(page, 6, 43, "Be sure to look for HEXXAGON,");
    put_text(page, 7, 42, "Argo Games' incredible animated");
    put_text(page, 8, 8, "Please respect this by not");
    put_text(page, 8, 42, "strategy board game!");
    put_text(page, 9, 7, "giving copies of Night Raid to");
    put_text(page, 10, 7, "your friends or co-workers,");
    put_text(page, 11, 7, "even if you like them a whole");
    put_text(page, 11, 42, "Call the Software Creations BBS:");
    put_text(page, 12, 7, "lot...");
    cell(page, 12, 14).character = 0x01;
    put_text(page, 12, 42, "1200/2400 baud");
    put_text(page, 12, 61, "(508)365-2359");
    put_text(page, 13, 42, "2400-16.8k USR HST (508)368-4137");
    put_text(page, 14, 8, "Thanks from:");
    put_text(page, 14, 42, "2400-14.4k V.32");
    put_text(page, 14, 61, "(508)368-7036");
    put_text(page, 15, 9, "Jason Blochowiak,");
    put_text(page, 16, 9, "Don Glassford,");
    put_text(page, 17, 9, "Robert Prince,");
    put_text(page, 18, 9, "and Dan Linton.");
    for (std::size_t row = 16; row <= 18; ++row) {
        cell(page, row, 42).character = 0x07;
    }
    put_text(page, 16, 44, "Over 50 lines, and growing!");
    put_text(page, 17, 44, "Over 6 Gigs, and growing!");
    put_text(page, 18, 44, "Official distribution site for");
    put_text(page, 19, 42, "Argo, Apogee, and id software.");
    put_text(page, 23, 5,
             "Night Raid v1.1 - Copyright 1992, 1993 Argo Games. All Rights Reserved.");

    return page;
}

}  // namespace

const ExitCardPage& faithful_exit_card_page()
{
    static const ExitCardPage page = make_exit_card_page();
    return page;
}

const ExitCardPage& faithful_startup_card_page(StartupCardStage stage)
{
    static const std::array<ExitCardPage, 5> pages {{
        make_startup_stage(StartupCardStage::sound_spinner_tab),
        make_startup_stage(StartupCardStage::sound_spinner_bell),
        make_startup_stage(StartupCardStage::sound_spinner_dot),
        make_startup_stage(StartupCardStage::graphics_spinner),
        make_startup_stage(StartupCardStage::complete),
    }};
    return pages[static_cast<std::size_t>(stage)];
}

}  // namespace niteraid
