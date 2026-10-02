#pragma once

// Tier 3 DOSBox-X memory-diff support: serialize WorldState into a raw
// byte buffer arranged the same way as the original game's DAT_2730_*
// data segment, so the same schema (tools/compare/dat2730_schema.py)
// can parse both our reimpl's dump and DOSBox's memdump.
//
// The buffer is sparse — only fields the schema asks about are written.
// Everything else stays zero. The buffer is sized to fit every field
// the schema knows about plus a small margin.

#include "niteraid/game_types.hpp"

#include <cstddef>
#include <vector>

namespace niteraid {

// Covers the gameplay schema and CONFIG hardware words through 0xe1d1.
constexpr std::size_t kStateDumpSize = 0xe200;

std::vector<std::uint8_t> serialize_world_state_dat2730(const WorldState& world);

}  // namespace niteraid
