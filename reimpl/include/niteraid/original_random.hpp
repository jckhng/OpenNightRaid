#pragma once

#include <cstdint>
#include <bit>

namespace niteraid {

// Borland rand(), 1000:154b. Unsigned arithmetic preserves DOS overflow.
constexpr std::uint16_t original_random_next(std::uint32_t& seed)
{
    seed = seed * 0x015a4e35u + 1u;
    return static_cast<std::uint16_t>((seed >> 16u) & 0x7fffu);
}

// 16c8:5728 consumes a draw even for a zero-width range.
constexpr std::uint16_t original_random_bounded(std::uint32_t& seed, std::uint16_t limit)
{
    return static_cast<std::uint32_t>(original_random_next(seed)) * limit / 0x8000u;
}

constexpr std::uint32_t original_palette_random_seed(std::uint32_t seed = 1)
{
    // 1c4f:079c fills 2730:6986..6e06 with one rand() per word.
    for (int word = 0; word < (0x6e06 - 0x6986) / 2; ++word) original_random_next(seed);
    return seed;
}

constexpr std::int32_t original_random_fixed_value(
    std::uint16_t draw, std::uint16_t scale, std::int32_t offset)
{
    // 546a uses a wrapping long multiply followed by signed long division.
    // Widening the intermediate would remove the original negative scatter.
    const auto product = static_cast<std::uint32_t>(draw) * (static_cast<std::uint32_t>(scale) << 16u);
    return std::bit_cast<std::int32_t>(product) / 32767 - offset;
}

constexpr std::int32_t original_random_fixed(
    std::uint32_t& seed, std::uint16_t scale, std::int32_t offset)
{
    return original_random_fixed_value(original_random_next(seed), scale, offset);
}

}  // namespace niteraid
