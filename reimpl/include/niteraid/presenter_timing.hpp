#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "niteraid/game_types.hpp"
#include "niteraid/audio_internals.hpp"

namespace niteraid::presenter_timing {

// RunGameplayWrapper (16c8:5008) blocks on each terminal sound. Sound Blaster
// consumes the declared DMA payload in 100-byte blocks at the DSP's 0x72 time
// constant, so the recovered 0x366/0x368 payloads occupy 205/62 timer ticks.
inline constexpr std::uint32_t kOriginalTimerHz = 70;
inline constexpr std::uint32_t kSoundBlasterTimeConstantDivisor = 256 - 0x72;
inline constexpr std::uint32_t kSoundBlasterClockHz = 1'000'000;
inline constexpr std::uint32_t kTerminalPreludeDmaBytes = 20'600;
inline constexpr std::uint32_t kTerminalPulseDmaBytes = 6'200;

constexpr std::uint32_t sound_blaster_dma_timer_ticks(std::uint32_t bytes)
{
    const auto numerator =
        bytes * kOriginalTimerHz * kSoundBlasterTimeConstantDivisor;
    return (numerator + kSoundBlasterClockHz - 1) / kSoundBlasterClockHz;
}

inline constexpr std::uint32_t kTerminalPreludePlaybackOriginalTicks =
    sound_blaster_dma_timer_ticks(kTerminalPreludeDmaBytes);
inline constexpr std::uint32_t kTerminalPulsePlaybackOriginalTicks =
    sound_blaster_dma_timer_ticks(kTerminalPulseDmaBytes);
inline constexpr std::uint32_t kTerminalPostPreludeWaitOriginalTicks = 0x23;
inline constexpr std::uint32_t kTerminalPreludeStartOriginalTick = 21;
inline constexpr std::uint32_t kTerminalPulseFirstOriginalTick =
    kTerminalPreludeStartOriginalTick +
    kTerminalPreludePlaybackOriginalTicks +
    kTerminalPostPreludeWaitOriginalTicks;
inline constexpr std::array<std::uint32_t, 3> kTerminalPulseOriginalTicks {{
    kTerminalPulseFirstOriginalTick,
    kTerminalPulseFirstOriginalTick + kTerminalPulsePlaybackOriginalTicks,
    kTerminalPulseFirstOriginalTick + 2 * kTerminalPulsePlaybackOriginalTicks,
}};
inline constexpr std::uint32_t kTerminalExplosionSoundOriginalTick =
    kTerminalPulseOriginalTicks.back() + kTerminalPulsePlaybackOriginalTicks;

// Values recovered as original timer ticks now map directly onto the 70 Hz
// logical simulation. Legacy values measured in 60 Hz captures are rescaled.
inline constexpr std::uint32_t kOverrunRetainedPageVideoFrames =
    legacy_video_frames_to_simulation_frames(390);
inline constexpr std::uint32_t kTerminalPreludeStartVideoFrame =
    original_timer_ticks_to_video_frames(kTerminalPreludeStartOriginalTick);
// Live Sound Blaster capture shows that blocking calls advance on playback
// completion rather than accumulating rounded waits. These retain the exact
// wall-clock boundaries validated in the former 60 Hz capture timeline.
inline constexpr std::array<std::uint32_t, 3> kTerminalPulseVideoFrames {{
    static_cast<std::uint32_t>(legacy_video_frames_to_simulation_frames(224)),
    static_cast<std::uint32_t>(legacy_video_frames_to_simulation_frames(276)),
    static_cast<std::uint32_t>(legacy_video_frames_to_simulation_frames(329)),
}};
inline constexpr std::uint32_t kTerminalExplosionSoundVideoFrame =
    legacy_video_frames_to_simulation_frames(384);

struct TerminalAudioTiming {
    std::uint32_t prelude;
    std::array<std::uint32_t, 3> pulses;
    std::uint32_t explosion;
    std::uint32_t retained;
};

constexpr TerminalAudioTiming terminal_audio_timing(const WorldState& world)
{
    if (world.audio_config_words[3] == 1) {
        // 2156:01de services PIT words twice per hardware tick. Original
        // 5008 completion capture: 162 words -> 81 ticks, then the authored
        // 35-tick wait and three 47-word pulses -> 23, 24, 23 ticks. Keep
        // the shared service phase instead of rounding every pulse upward.
        const auto first = kTerminalPreludeStartVideoFrame + 81 + kTerminalPostPreludeWaitOriginalTicks;
        const auto explosion = first + (3 * 47) / 2;
        const auto palette_tail = kOverrunRetainedPageVideoFrames - kTerminalExplosionSoundVideoFrame;
        return {kTerminalPreludeStartVideoFrame, {first, first + 47 / 2, first + 47},
                explosion, explosion + palette_tail};
    }
    if (world.audio_config_words[3] == 0) {
        // 5008 -> 1468 -> 1384: disabled sound completes immediately. Only
        // the explicit 510c..5129 wait remains; all three pulses then return
        // without waiting. Preserve the existing entry/palette approximation
        // separately, rather than importing a DOS capture's loading cost.
        const auto pulse = kTerminalPreludeStartVideoFrame + kTerminalPostPreludeWaitOriginalTicks;
        const auto palette_tail = kOverrunRetainedPageVideoFrames - kTerminalExplosionSoundVideoFrame;
        return {kTerminalPreludeStartVideoFrame, {pulse, pulse, pulse}, pulse, pulse + palette_tail};
    }
    return {kTerminalPreludeStartVideoFrame, kTerminalPulseVideoFrames,
            kTerminalExplosionSoundVideoFrame, kOverrunRetainedPageVideoFrames};
}

// Original inclusive waits are 7/70 s and 8/70 s.
inline constexpr std::uint32_t kExplosionVideoFramesEach =
    original_timer_ticks_to_video_frames(7);
inline constexpr std::uint32_t kFlagVideoFramesEach =
    original_timer_ticks_to_video_frames(8);
inline constexpr std::uint32_t kExplosionFrameCount = 5;
inline constexpr std::uint32_t kFlagRaiseFrameCount = 3;
inline constexpr std::uint32_t kFlagWaveFrameCount = 12;
inline constexpr std::uint32_t kTerminalExplosionVideoFrames =
    kExplosionFrameCount * kExplosionVideoFramesEach;
inline constexpr std::uint32_t kTerminalFlagWaveStartVideoFrame =
    kTerminalExplosionVideoFrames + kFlagRaiseFrameCount * kFlagVideoFramesEach;

inline std::optional<std::uint32_t> terminal_presenter_frame(const WorldState& world)
{
    if (world.screen != Screen::GameOver || world.overrun_interrupt_residue || !world.player_dead) {
        return std::nullopt;
    }
    const auto elapsed = world.frame_tick - world.transition_started_tick;
    const auto retained = terminal_audio_timing(world).retained;
    if (world.bunker_white_flag && elapsed < retained) {
        return std::nullopt;
    }
    const bool has_overrun_hold = world.bunker_white_flag && world.bunker_assault_entries >= 3;
    return elapsed - (has_overrun_hold ? retained : 0u);
}

constexpr std::uint32_t terminal_presenter_draw(std::uint32_t frame)
{
    // Keep a monotonic draw ordinal across repeated flag-wave cycles.
    return frame < kTerminalExplosionVideoFrames
        ? frame / kExplosionVideoFramesEach
        : kExplosionFrameCount + (frame - kTerminalExplosionVideoFrames) / kFlagVideoFramesEach;
}

constexpr bool terminal_exit_poll_frame(std::uint32_t frame)
{
    // 5008:5303 tests the keyboard latch only after all three raise waits,
    // then after each complete waving-frame wait, never inside either loop.
    return frame >= kTerminalFlagWaveStartVideoFrame &&
           (frame - kTerminalFlagWaveStartVideoFrame) % kFlagVideoFramesEach == 0;
}

inline constexpr std::uint32_t kNoSurvivorFlybyVideoFrames =
    original_timer_ticks_to_video_frames(0x221);

// Level-13 winning presenter (4ae6 -> 4513), composite-enabled completed draws.
// 3999's 80/100 counters include the update where the old counter was zero.
inline constexpr std::uint32_t kFinaleSetupVideoFrames = 0;
inline constexpr std::uint32_t kFinaleIngressVideoFrames =
    original_timer_ticks_to_video_frames(81);
inline constexpr std::uint32_t kFinaleWalkStartVideoFrame = 159;
inline constexpr std::uint32_t kFinaleWalkVideoFrames =
    original_timer_ticks_to_video_frames(54 * 8);
inline constexpr std::uint32_t kFinaleEgressStartVideoFrame = 663;
inline constexpr std::uint32_t kFinaleEgressVideoFrames =
    original_timer_ticks_to_video_frames(101);
inline constexpr std::uint32_t kFinaleParticleStartVideoFrame =
    kFinaleEgressStartVideoFrame + kFinaleEgressVideoFrames;
inline constexpr std::uint32_t kFinaleParticleSpawnCount = 60;
inline constexpr std::uint32_t kFinaleParticleSpawnVideoFrames =
    original_timer_ticks_to_video_frames(0x23);
// 4ae6 removes the actors immediately after creating star 60, not after it bursts.
inline constexpr std::uint32_t kFinalePresenterVideoFrames =
    kFinaleParticleStartVideoFrame + 59 * kFinaleParticleSpawnVideoFrames;

constexpr std::uint32_t finale_serial_wait_extra(const WorldState& world)
{
    // 40ce: serial waits are 9 + 141 + 36, composite waits are 36 + 36.
    return audio_internals::composite_effects_enabled(world) ? 0 : 114;
}

constexpr std::uint32_t finale_egress_start(const WorldState& world)
{
    return kFinaleEgressStartVideoFrame + finale_serial_wait_extra(world);
}

constexpr std::uint32_t finale_particle_start(const WorldState& world)
{
    return kFinaleParticleStartVideoFrame + finale_serial_wait_extra(world);
}

constexpr std::uint32_t finale_presenter_frames(const WorldState& world)
{
    return kFinalePresenterVideoFrames + finale_serial_wait_extra(world);
}


static_assert(kExplosionVideoFramesEach == 7);
static_assert(kFlagVideoFramesEach == 8);
static_assert(kTerminalPreludePlaybackOriginalTicks == 205);
static_assert(kTerminalPulsePlaybackOriginalTicks == 62);
static_assert(kTerminalPreludeStartVideoFrame == 21);
static_assert(kTerminalPulseVideoFrames[0] == 261);
static_assert(kTerminalPulseVideoFrames[1] == 322);
static_assert(kTerminalPulseVideoFrames[2] == 384);
static_assert(kTerminalExplosionSoundVideoFrame == 448);
static_assert(kNoSurvivorFlybyVideoFrames == 545);
static_assert(kFinaleIngressVideoFrames == 81);
static_assert(kFinaleWalkVideoFrames == 432);
static_assert(kFinaleEgressVideoFrames == 101);
static_assert(kFinaleParticleSpawnVideoFrames == 35);

}  // namespace niteraid::presenter_timing
