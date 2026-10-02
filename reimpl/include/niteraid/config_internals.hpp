#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace niteraid::config_internals {

using Words = std::array<std::uint16_t, 8>;

inline constexpr std::size_t kOplHardware = 0;
inline constexpr std::size_t kSourceHardware = 1;
inline constexpr std::size_t kBlasterHardware = 2;
inline constexpr std::size_t kSoundMode = 3;
inline constexpr std::size_t kVoiceCount = 4;
inline constexpr std::size_t kMusicEnabled = 5;
inline constexpr std::size_t kMouseHardware = 6;
inline constexpr std::size_t kMousePreference = 7;

// Virtual DOS capabilities match the reference SB/OPL/mouse profile, not host devices.
inline constexpr Words kDefaultWords {{1, 0, 1, 3, 2, 1, 1, 1}};

constexpr Words restore_preferences(Words live, const Words& saved)
{
    // 1480:01dd retains detection words and gates saved options by hardware identity.
    live[kMusicEnabled] = saved[kOplHardware] == live[kOplHardware]
        ? saved[kMusicEnabled] : static_cast<std::uint16_t>(live[kOplHardware] != 0);
    if (saved[kSourceHardware] == live[kSourceHardware] &&
        saved[kBlasterHardware] == live[kBlasterHardware]) {
        live[kSoundMode] = saved[kSoundMode];
        live[kVoiceCount] = saved[kVoiceCount];
    } else {
        live[kSoundMode] = live[kBlasterHardware] != 0 ? 3 : live[kSourceHardware] != 0 ? 2 : 1;
        live[kVoiceCount] = 2;
    }
    if (live[kMouseHardware] == 0) {
        live[kMousePreference] = 0;
        return live;
    }
    live[kMousePreference] = saved[kMouseHardware] == 0 ? 1 : saved[kMousePreference];
    return live;
}

} // namespace niteraid::config_internals
