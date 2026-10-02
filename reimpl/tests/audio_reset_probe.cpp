#include "niteraid/audio.hpp"
#include "niteraid/ntr_assets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kFrames = 120;
constexpr int kResetFrame = 20;
constexpr int kBytesPerFrame = (44100 / 70) * 2;
constexpr int kPcSpeakerServiceRateHz = 140;
constexpr int kCaptureRateHz = 70;
constexpr int kPcSpeakerDivisorsPerCaptureFrame =
    kPcSpeakerServiceRateHz / kCaptureRateHz;
// Verified against the original archive in terminal-backend-audit-2026-09-11:
// 0x365 has 162 divisors (81 logical ticks), while 0x367 has 47 divisors
// (23.5 logical ticks) at 140 Hz.
constexpr std::size_t kTerminal365Divisors = 162;
constexpr std::size_t kTerminal367Divisors = 47;
using Pcm = std::vector<char>;

struct RenderOptions {
    int mode = 0;
    bool music = false;
    bool effect = false;
    bool reset = false;
    bool replacement = false;
    bool owner = false;
    bool hold_after_reset = false;
    bool stop_effect = false;
    int effect_stop_frame = kResetFrame;
    bool keep_owner_active = false;
    bool release_owner = false;
    int mode_after_reset = -1;
    int owner_start_frame = 0;
    std::uint16_t effect_sound_id = 0x369;
    std::uint16_t owner_sound_id = 0x335;
    // Matches WorldState::audio_config_words[4], preserving existing captures.
    std::uint16_t voice_config = 2;
};

Pcm render(const std::filesystem::path& output, const std::string& name,
           const RenderOptions& options)
{
    niteraid::Audio audio(true, true);
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.audio_config_words[3] = static_cast<std::uint16_t>(options.mode);
    world.audio_config_words[4] = options.voice_config;
    world.audio_config_words[5] = options.music ? 1 : 0;
    if (options.owner) {
        niteraid::Object object {};
        object.active = options.owner_start_frame == 0;
        object.type = niteraid::ObjectType::Aircraft;
        object.sound_id = options.owner_sound_id;
        world.objects.push_back(object);
    }
    for (int frame = 0; frame < kFrames; ++frame) {
        if (options.hold_after_reset && frame == kResetFrame + 1) {
            // Deliberately retain reset + replacement events. A hold must ignore both.
            audio.capture_hold(world, true);
            continue;
        }
        world.frame_tick = static_cast<std::uint32_t>(frame);
        if (options.owner && frame == options.owner_start_frame) world.objects.front().active = true;
        if (options.mode_after_reset >= 0 && frame >= kResetFrame) {
            world.audio_config_words[3] = static_cast<std::uint16_t>(options.mode_after_reset);
        }
        world.sound_events.clear();
        world.sound_stop_events.clear();
        world.reset_sound_effects = options.reset && frame == kResetFrame;
        if (frame == 0 && options.effect) world.sound_events.push_back(options.effect_sound_id);
        if (options.stop_effect && frame == options.effect_stop_frame) {
            world.sound_stop_events.push_back(options.effect_sound_id);
        }
        if (frame == kResetFrame && options.replacement) {
            world.sound_events.push_back(0x367);
        }
        if (frame == kResetFrame &&
            (options.reset || options.stop_effect || options.release_owner) &&
            !options.keep_owner_active) {
            for (auto& object : world.objects) object.active = false;
        }
        audio.capture_frame(world, true);
    }
    const auto wav = output / (name + ".wav");
    audio.write_capture_wav(wav.string().c_str());
    audio.write_capture_manifest((output / (name + ".json")).string().c_str());
    std::ifstream file(wav, std::ios::binary);
    Pcm bytes {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.size() != 44 + kFrames * kBytesPerFrame) {
        throw std::runtime_error("missing or incomplete PCM capture: " + name);
    }
    return Pcm(bytes.begin() + 44, bytes.end());
}

std::vector<bool> read_trace_presence(const std::filesystem::path& manifest,
                                      const char* field,
                                      std::uint16_t resource)
{
    std::ifstream file(manifest);
    if (!file) {
        throw std::runtime_error("missing audio trace: " + manifest.string());
    }

    const std::string marker = std::string("\"") + field + "\": [";
    std::vector<bool> presence;
    std::string line;
    while (std::getline(file, line)) {
        const auto marker_offset = line.find(marker);
        if (marker_offset == std::string::npos) {
            continue;
        }
        const auto list_begin = marker_offset + marker.size() - 1;
        const auto list_end = line.find(']', list_begin);
        if (list_end == std::string::npos) {
            throw std::runtime_error("malformed audio trace: " + manifest.string());
        }

        bool found = false;
        for (std::size_t cursor = list_begin + 1; cursor < list_end;) {
            while (cursor < list_end && (line[cursor] == ' ' || line[cursor] == ',')) {
                ++cursor;
            }
            if (cursor >= list_end) {
                break;
            }
            std::size_t value = 0;
            const auto number_begin = cursor;
            while (cursor < list_end && line[cursor] >= '0' && line[cursor] <= '9') {
                value = value * 10 + static_cast<std::size_t>(line[cursor] - '0');
                ++cursor;
            }
            if (cursor == number_begin) {
                throw std::runtime_error("malformed audio trace list: " + manifest.string());
            }
            found |= value == resource;
        }
        presence.push_back(found);
    }
    return presence;
}

std::size_t exhaustion_frame_for_divisors(std::size_t divisor_count)
{
    return (divisor_count + kPcSpeakerDivisorsPerCaptureFrame - 1) /
               kPcSpeakerDivisorsPerCaptureFrame -
           1;
}

bool pcm_has_signal(const Pcm& pcm, std::size_t first_frame, std::size_t last_frame)
{
    if (first_frame > last_frame || last_frame > kFrames) {
        return false;
    }
    const auto first_byte = first_frame * kBytesPerFrame;
    const auto last_byte = last_frame * kBytesPerFrame;
    return std::any_of(pcm.begin() + first_byte, pcm.begin() + last_byte,
                       [](char sample_byte) { return sample_byte != 0; });
}

std::optional<std::size_t> last_signal_sample(const Pcm& pcm)
{
    for (std::size_t sample = pcm.size() / 2; sample != 0; --sample) {
        if (pcm[(sample - 1) * 2] != 0 || pcm[(sample - 1) * 2 + 1] != 0) {
            return sample - 1;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> actual_exhaustion_frame(
    const std::filesystem::path& manifest, std::uint16_t resource)
{
    const auto active = read_trace_presence(manifest, "active_resource_sounds", resource);
    if (active.size() != kFrames || !active.front()) {
        return std::nullopt;
    }

    std::size_t frame = 1;
    while (frame < active.size() && active[frame]) {
        ++frame;
    }
    if (frame == active.size()) {
        return std::nullopt;
    }
    const auto exhausted_at = frame;
    for (; frame < active.size(); ++frame) {
        if (active[frame]) {
            return std::nullopt;
        }
    }
    return exhausted_at;
}

bool trace_maps_logical_to_resource(const std::filesystem::path& manifest,
                                    std::uint16_t logical,
                                    std::uint16_t resource)
{
    const auto logical_events = read_trace_presence(manifest, "logical_events", logical);
    const auto resource_events = read_trace_presence(manifest, "resource_events", resource);
    if (logical_events.size() != kFrames || resource_events.size() != kFrames) {
        return false;
    }
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        if (logical_events[frame] != (frame == 0) || resource_events[frame] != (frame == 0)) return false;
    }
    return true;
}

bool one_shot_resource_completes_owner_free(const Pcm& pcm,
                                            const std::filesystem::path& manifest,
                                            std::uint16_t logical,
                                            std::uint16_t resource,
                                            std::size_t& exhaustion_frame)
{
    if (!trace_maps_logical_to_resource(manifest, logical, resource)) {
        return false;
    }
    const auto actual_frame = actual_exhaustion_frame(manifest, resource);
    if (!actual_frame || *actual_frame == 0 || *actual_frame >= kFrames) {
        return false;
    }
    const auto last_sample = last_signal_sample(pcm);
    if (!last_sample || *last_sample >= static_cast<std::size_t>(kFrames * kBytesPerFrame / 2)) {
        return false;
    }
    const auto last_signal_frame = *last_sample / static_cast<std::size_t>(kBytesPerFrame / 2);
    if (last_signal_frame > *actual_frame ||
        !pcm_has_signal(pcm, 0, *actual_frame + 1) ||
        pcm_has_signal(pcm, *actual_frame + 1, kFrames)) {
        return false;
    }

    const auto owners = read_trace_presence(manifest, "owner_resource_sounds", resource);
    if (owners.size() != kFrames ||
        std::any_of(owners.begin(), owners.end(), [](bool present) { return present; })) {
        return false;
    }
    exhaustion_frame = *actual_frame;
    return true;
}

std::optional<bool> verify_manifest_voice_config(
    const std::filesystem::path& manifest, std::uint16_t expected)
{
    std::ifstream file(manifest);
    if (!file) {
        throw std::runtime_error("missing audio trace: " + manifest.string());
    }

    const std::string marker = "\"voice_config\":";
    std::size_t found = 0;
    bool matches = true;
    std::string line;
    while (std::getline(file, line)) {
        const auto marker_offset = line.find(marker);
        if (marker_offset == std::string::npos) {
            continue;
        }
        const auto value_begin = marker_offset + marker.size();
        std::size_t value = 0;
        std::size_t cursor = value_begin;
        while (cursor < line.size() &&
               (line[cursor] == ' ' || line[cursor] == '\t')) {
            ++cursor;
        }
        const auto number_begin = cursor;
        while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
            value = value * 10 + static_cast<std::size_t>(line[cursor] - '0');
            ++cursor;
        }
        if (cursor == number_begin) {
            throw std::runtime_error("malformed voice configuration trace: " + manifest.string());
        }
        ++found;
        matches &= value == expected;
    }
    if (!found) {
        return std::nullopt;
    }
    return matches && found == kFrames;
}

bool terminal_resource_stops_at(const Pcm& pcm,
                               const std::filesystem::path& manifest,
                               std::uint16_t resource,
                               std::size_t exhaustion_frame,
                               bool owned)
{
    const auto active = read_trace_presence(manifest, "active_resource_sounds", resource);
    if (active.size() != kFrames || exhaustion_frame == 0 || exhaustion_frame >= active.size()) {
        return false;
    }
    for (std::size_t frame = 0; frame < exhaustion_frame; ++frame) {
        if (!active[frame]) {
            return false;
        }
    }
    for (std::size_t frame = exhaustion_frame; frame < active.size(); ++frame) {
        if (active[frame]) {
            return false;
        }
    }
    // Trace activity is recorded after mixing. The exhaustion frame still
    // contains the final samples; silence begins at the resource's sample end.
    const auto divisors = resource == 0x365 ? kTerminal365Divisors : kTerminal367Divisors;
    const auto sample_end = divisors * 44100 / kPcSpeakerServiceRateHz;
    if (!pcm_has_signal(pcm, 0, exhaustion_frame) ||
        !std::all_of(pcm.begin() + sample_end * 2, pcm.end(),
                     [](char sample_byte) { return sample_byte == 0; })) {
        return false;
    }

    const auto owners = read_trace_presence(manifest, "owner_resource_sounds", resource);
    if (owners.size() != kFrames) {
        return false;
    }
    for (const auto owner_present : owners) {
        if (owner_present != owned) {
            return false;
        }
    }
    return true;
}

bool same_tail(const Pcm& a, const Pcm& b)
{
    const auto offset = kResetFrame * kBytesPerFrame;
    return std::equal(a.begin() + offset, a.end(), b.begin() + offset);
}

bool same_tail_from(const Pcm& a, const Pcm& b, std::size_t first_frame)
{
    const auto offset = first_frame * kBytesPerFrame;
    return std::equal(a.begin() + offset, a.end(), b.begin() + offset);
}

bool same_frame(const Pcm& a, const Pcm& b, std::size_t frame)
{
    const auto first_byte = frame * kBytesPerFrame;
    const auto last_byte = first_byte + kBytesPerFrame;
    return std::equal(a.begin() + first_byte, a.begin() + last_byte,
                      b.begin() + first_byte);
}

bool retains_submitted_tail(const Pcm& stopped, const Pcm& control, std::uint32_t rate)
{
    // Nominal device block boundaries need not coincide with 70 Hz frames.
    const std::size_t start_sample = kResetFrame * kBytesPerFrame / 2;
    const std::size_t block_phase = 100 * 44100;
    const auto completed = start_sample * rate / block_phase;
    const auto end_sample = ((completed + 1) * block_phase + rate - 1) / rate;
    return end_sample * 2 < stopped.size() && stopped.size() == control.size() &&
        std::equal(stopped.begin() + start_sample * 2, stopped.begin() + end_sample * 2,
                   control.begin() + start_sample * 2) &&
        std::all_of(stopped.begin() + end_sample * 2, stopped.end(), [](char byte) { return byte == 0; });
}

bool trace_resource_absent_from(const std::filesystem::path& manifest,
                                const char* field,
                                std::uint16_t resource,
                                std::size_t first_frame)
{
    const auto presence = read_trace_presence(manifest, field, resource);
    return presence.size() == kFrames && first_frame <= presence.size() &&
           std::none_of(presence.begin() + first_frame, presence.end(),
                        [](bool present) { return present; });
}

bool terminal_pulse_carrier_is_continuous(const Pcm& pcm)
{
    // Original resource 367, words 8..11, is four copies of divisor 2100.
    // Check the real mixed WAV with an integer phase oracle, not the decoder.
    constexpr std::size_t start = 8 * 315;
    constexpr std::uint64_t period = 2100ull * 44100;
    for (std::size_t sample = 0; sample < 4 * 315; ++sample) {
        const auto byte = (start + sample) * 2;
        const auto raw = static_cast<unsigned char>(pcm[byte]) |
                         (static_cast<unsigned char>(pcm[byte + 1]) << 8);
        const int value = raw < 0x8000 ? raw : raw - 0x10000;
        const bool positive = ((sample * 1'193'182ull) % period) < period / 2;
        if (value == 0 || (value > 0) != positive) return false;
    }
    return true;
}

bool same_prefix(const Pcm& a, const Pcm& b)
{
    const auto offset = kResetFrame * kBytesPerFrame;
    return std::equal(a.begin(), a.begin() + offset, b.begin());
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3) throw std::runtime_error("usage: niteraid_audio_reset_probe GRAPHICS.NTR NEW_OUTPUT_DIR");
        if (!niteraid::configure_original_asset_archive(argv[1])) {
            throw std::runtime_error("cannot load supplied original archive");
        }
        niteraid::set_original_archive_only(true);
        const std::filesystem::path output(argv[2]);
        if (!std::filesystem::create_directories(output)) {
            throw std::runtime_error("output must be a new directory");
        }
        int failures = 0;
        const auto check = [&](bool passed, const std::string& name) {
            std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
            failures += !passed;
        };
        for (int mode : {1, 2, 3}) {
            const std::string prefix = "mode" + std::to_string(mode) + "-";
            const auto capture = [&](const char* name, RenderOptions options) {
                return render(output, prefix + name, options);
            };
            const auto silence = capture("silence", RenderOptions {.mode = mode});
            const auto transient = capture("transient", RenderOptions {.mode = mode, .effect = true});
            const auto reset = capture("reset", RenderOptions {.mode = mode, .effect = true, .reset = true});
            const auto replacement = capture("replacement", RenderOptions {
                .mode = mode, .effect = true, .reset = true, .replacement = true});
            const auto fresh = capture("fresh", RenderOptions {.mode = mode, .replacement = true});
            const auto music = capture("music", RenderOptions {.mode = mode, .music = true});
            const auto music_reset = capture("music-reset", RenderOptions {
                .mode = mode, .music = true, .effect = true, .reset = true});
            const auto owner_reset = capture("owner-reset", RenderOptions {
                .mode = mode, .effect = true, .reset = true, .owner = true});
            const auto stopped = render(output, prefix + "stopped", RenderOptions {
                .mode = mode, .effect = true, .stop_effect = true});
            const auto owner_reset_active = render(output, prefix + "owner-reset-active",
                                                   RenderOptions {.mode = mode, .reset = true,
                                                                  .owner = true, .keep_owner_active = true});
            const auto owner_release = render(output, prefix + "owner-release",
                                              RenderOptions {.mode = mode, .owner = true,
                                                             .release_owner = true});
            std::optional<Pcm> owner_control;
            if (mode == 2 || mode == 3) {
                owner_control = render(output, prefix + "owner-control",
                                       RenderOptions {.mode = mode, .owner = true});
            }
            check(!same_tail(transient, silence), prefix + "transient control is still audible after reset boundary");
            check(std::equal(reset.begin(), reset.begin() + kResetFrame * kBytesPerFrame,
                             transient.begin()), prefix + "no premature truncation");
            check(same_tail(reset, silence), prefix + "unowned effects cleared");
            check(same_tail(owner_reset, silence), prefix + "owned and unowned effects cleared together");
            check(!same_tail(fresh, silence), prefix + "replacement control is audible");
            check(same_tail(replacement, fresh), prefix + "same-frame replacement starts after reset");
            check(!same_tail(music, silence), prefix + "music control is audible");
            check(same_tail(music_reset, music), prefix + "music continues sample-exactly without restart");
            const auto held = render(output, prefix + "held-reset", RenderOptions {
                .mode = mode, .music = true, .effect = true, .reset = true,
                .replacement = true, .owner = true, .hold_after_reset = true,
                .stop_effect = true});
            const auto advanced = render(output, prefix + "advanced-reset", RenderOptions {
                .mode = mode, .music = true, .effect = true, .reset = true,
                .replacement = true, .owner = true});
            if (mode == 2 || mode == 3) {
                // Original 140d -> 0a80 removes the voice without flushing the submitted buffer.
                check(same_tail_from(stopped, silence, kResetFrame + 1),
                      prefix + "standalone stop clears the active effect after its submitted block");
                check(retains_submitted_tail(stopped, transient, mode == 2 ? 7000 : 7042),
                      prefix + "standalone stop retains the submitted block at the stop frame");
                check(owner_control &&
                          same_tail_from(owner_release, silence, kResetFrame + 1) &&
                          retains_submitted_tail(owner_release, *owner_control, mode == 2 ? 7000 : 7042),
                      prefix + "owner release retains only the submitted block at the stop frame");
            } else {
                check(same_tail(stopped, silence), prefix + "standalone stop event clears the active effect");
            }
            check(!same_prefix(owner_reset_active, silence), prefix + "owner remains audible before reset");
            check(same_tail(owner_reset_active, silence),
                  prefix + "SFX reset clears voice without rebinding unchanged owner");
            if (mode != 2 && mode != 3) {
                check(same_tail(owner_release, silence), prefix + "owner release stops its bound sound");
            }
                check(held == advanced, prefix + "hold advances PCM without replaying reset, replacement or music");
        }

        const auto terminal365_unowned = render(
            output, "mode1-terminal-365-unowned",
            RenderOptions {.mode = 1, .effect = true, .effect_sound_id = 0x365});
        const auto terminal365_owned = render(
            output, "mode1-terminal-365-owned",
            RenderOptions {.mode = 1, .owner = true, .owner_sound_id = 0x365});
        const auto terminal367_unowned = render(
            output, "mode1-terminal-367-unowned",
            RenderOptions {.mode = 1, .effect = true, .effect_sound_id = 0x367});
        const auto terminal367_owned = render(
            output, "mode1-terminal-367-owned",
            RenderOptions {.mode = 1, .owner = true, .owner_sound_id = 0x367});
        check(terminal_resource_stops_at(
                  terminal365_unowned,
                  output / "mode1-terminal-365-unowned.json",
                  0x365,
                  exhaustion_frame_for_divisors(kTerminal365Divisors),
                  false),
              "mode1 logical 365 unowned voice exhausts at its 162-divisor boundary");
        check(terminal_resource_stops_at(
                  terminal365_owned,
                  output / "mode1-terminal-365-owned.json",
                  0x365,
                  exhaustion_frame_for_divisors(kTerminal365Divisors),
                  true),
              "mode1 logical 365 owned voice exhausts at its 162-divisor boundary");
        check(terminal_resource_stops_at(
                  terminal367_unowned,
                  output / "mode1-terminal-367-unowned.json",
                  0x367,
                  exhaustion_frame_for_divisors(kTerminal367Divisors),
                  false),
              "mode1 logical 367 unowned voice exhausts at its 47-divisor boundary");
        check(terminal_resource_stops_at(
                  terminal367_owned,
                  output / "mode1-terminal-367-owned.json",
                  0x367,
                  exhaustion_frame_for_divisors(kTerminal367Divisors),
                  true),
              "mode1 logical 367 owned voice exhausts at its 47-divisor boundary");
        check(terminal_pulse_carrier_is_continuous(terminal367_unowned),
              "mode1 logical 367 unowned repeated PIT words keep carrier phase");
        check(terminal_pulse_carrier_is_continuous(terminal367_owned),
              "mode1 logical 367 owned repeated PIT words keep carrier phase");

        const auto mode2_serial367 = render(
            output, "mode2-digital-serial-367",
            RenderOptions {.mode = 2, .effect = true, .effect_sound_id = 0x367,
                           .voice_config = 0});
        const auto mode2_composite367 = render(
            output, "mode2-digital-composite-367",
            RenderOptions {.mode = 2, .effect = true, .effect_sound_id = 0x367,
                           .voice_config = 2});
        const auto serial_manifest = output / "mode2-digital-serial-367.json";
        const auto composite_manifest = output / "mode2-digital-composite-367.json";
        check(trace_maps_logical_to_resource(serial_manifest, 0x367, 0x368),
              "mode2 digital serial logical 367 maps to resource 368");
        check(trace_maps_logical_to_resource(composite_manifest, 0x367, 0x368),
              "mode2 digital composite logical 367 maps to resource 368");
        for (const auto& manifest : {serial_manifest, composite_manifest}) {
            const auto active = read_trace_presence(manifest, "active_resource_sounds", 0x368);
            const auto rates = read_trace_presence(manifest, "active_source_sample_rates", 7000);
            const auto size = manifest == serial_manifest ? 6195 : 6200;
            const auto lengths = read_trace_presence(manifest, "active_source_sample_counts", size);
            const auto prefix = manifest == serial_manifest ? "mode2 serial " : "mode2 composite ";
            check(active.size() == kFrames && active.front() && rates == active,
                  std::string(prefix) + "voice uses its own 7000 Hz source clock");
            check(active.size() == kFrames && active.front() && lengths == active,
                  std::string(prefix) + "voice uses the recovered payload length");
        }
        check(pcm_has_signal(mode2_serial367, 0, kFrames),
              "mode2 digital serial logical 367 is audible");
        check(pcm_has_signal(mode2_composite367, 0, kFrames),
              "mode2 digital composite logical 367 is audible");
        check(!pcm_has_signal(mode2_composite367, 0, 1),
              "mode2 composite idle start submits a silent 100-byte block");
        check(pcm_has_signal(mode2_composite367, 1, 2),
              "mode2 composite first prepared resource block follows the silent block");
        check(pcm_has_signal(mode2_serial367, 0, 1),
              "mode2 serial starts its declared payload without composite pre-roll");
        std::size_t serial_exhaustion_frame = 0;
        std::size_t composite_exhaustion_frame = 0;
        check(one_shot_resource_completes_owner_free(
                  mode2_serial367, serial_manifest, 0x367, 0x368, serial_exhaustion_frame),
              "mode2 digital serial resource 368 completes owner-free");
        check(one_shot_resource_completes_owner_free(
                  mode2_composite367, composite_manifest, 0x367, 0x368,
                  composite_exhaustion_frame),
              "mode2 digital composite resource 368 completes owner-free");
        const auto report_exhaustion = [&](const char* name,
                                           const std::filesystem::path& manifest) {
            const auto frame = actual_exhaustion_frame(manifest, 0x368);
            if (frame) {
                std::cout << "INFO " << name << " resource 368 exhaustion at capture frame "
                          << *frame << '\n';
            } else {
                std::cout << "INFO " << name << " resource 368 exhaustion not observed\n";
            }
        };
        report_exhaustion("mode2 digital serial", serial_manifest);
        report_exhaustion("mode2 digital composite", composite_manifest);
        const auto report_voice_config = [&](const char* name,
                                             const std::filesystem::path& manifest,
                                             std::uint16_t expected) {
            const auto verification = verify_manifest_voice_config(manifest, expected);
            check(verification.value_or(false), std::string(name) + " manifest voice_config is " +
                                                    std::to_string(expected));
        };
        report_voice_config("mode2 digital serial", serial_manifest, 0);
        report_voice_config("mode2 digital composite", composite_manifest, 2);

        const auto mode2_singlepulse_config1 = render(
            output, "mode2-digital-singlepulse-config1-367",
            RenderOptions {.mode = 2, .effect = true, .effect_sound_id = 0x367,
                           .voice_config = 1});
        const auto mode2_singlepulse_config1_manifest =
            output / "mode2-digital-singlepulse-config1-367.json";
        check(mode2_singlepulse_config1 == mode2_composite367,
              "mode2 voice_config 1 single pulse PCM matches voice_config 2");
        report_voice_config("mode2 single pulse config1",
                            mode2_singlepulse_config1_manifest, 1);

        RenderOptions mode2_stop367_options {
            .mode = 2, .effect = true, .voice_config = 2};
        mode2_stop367_options.effect_sound_id = 0x367;
        mode2_stop367_options.stop_effect = true;
        mode2_stop367_options.effect_stop_frame = 1;
        const auto mode2_stop367 = render(
            output, "mode2-digital-stop-367", mode2_stop367_options);
        const auto mode2_stop367_manifest = output / "mode2-digital-stop-367.json";
        check(trace_maps_logical_to_resource(mode2_stop367_manifest, 0x367, 0x368),
              "mode2 stopped logical 367 does not retrigger before completion");
        const auto stop_logical =
            read_trace_presence(mode2_stop367_manifest, "logical_stop_events", 0x367);
        const auto stop_resource =
            read_trace_presence(mode2_stop367_manifest, "resource_stop_events", 0x368);
        check(stop_logical.size() == kFrames && stop_resource.size() == kFrames &&
                  !stop_logical[0] && stop_logical[1] &&
                  !stop_resource[0] && stop_resource[1] &&
                  std::none_of(stop_logical.begin() + 2, stop_logical.end(),
                               [](bool present) { return present; }) &&
                  std::none_of(stop_resource.begin() + 2, stop_resource.end(),
                               [](bool present) { return present; }),
              "mode2 logical 367 stop is submitted exactly once at frame 1");
        check(!pcm_has_signal(mode2_stop367, 0, 1),
              "mode2 stopped composite logical 367 keeps frame 0 silent");
        check(same_frame(mode2_stop367, mode2_composite367, 1),
              "mode2 stopped composite logical 367 retains the submitted frame 1 block");
        check(!pcm_has_signal(mode2_stop367, 2, kFrames),
              "mode2 stopped composite logical 367 is silent after frame 2");
        check(trace_resource_absent_from(
                  mode2_stop367_manifest, "active_resource_sounds", 0x368, 1),
              "mode2 stopped resource 368 has no active trace after stop");
        check(trace_resource_absent_from(
                  mode2_stop367_manifest, "owner_resource_sounds", 0x368, 0),
              "mode2 stopped resource 368 remains owner-free");

        const auto mode3_serial367 = render(
            output, "mode3-digital-serial-367",
            RenderOptions {.mode = 3, .effect = true, .effect_sound_id = 0x367,
                           .voice_config = 0});
        const auto mode3_composite367 = render(
            output, "mode3-digital-composite-367",
            RenderOptions {.mode = 3, .effect = true, .effect_sound_id = 0x367,
                           .voice_config = 2});
        const auto mode3_serial_manifest = output / "mode3-digital-serial-367.json";
        const auto mode3_composite_manifest =
            output / "mode3-digital-composite-367.json";
        check(trace_maps_logical_to_resource(mode3_serial_manifest, 0x367, 0x368),
              "mode3 digital serial logical 367 maps to resource 368");
        check(trace_maps_logical_to_resource(mode3_composite_manifest, 0x367, 0x368),
              "mode3 digital composite logical 367 maps to resource 368");
        for (const auto& manifest : {mode3_serial_manifest, mode3_composite_manifest}) {
            const auto active = read_trace_presence(manifest, "active_resource_sounds", 0x368);
            const auto rates = read_trace_presence(manifest, "active_source_sample_rates", 7042);
            const auto size = manifest == mode3_serial_manifest ? 6195 : 6200;
            const auto lengths = read_trace_presence(manifest, "active_source_sample_counts", size);
            const auto prefix = manifest == mode3_serial_manifest ? "mode3 serial " : "mode3 composite ";
            check(active.size() == kFrames && active.front() && rates == active,
                  std::string(prefix) + "voice uses its recovered 7042 Hz source clock");
            check(active.size() == kFrames && active.front() && lengths == active,
                  std::string(prefix) + "voice uses the recovered payload length");
        }
        check(pcm_has_signal(mode3_serial367, 0, kFrames),
              "mode3 digital serial logical 367 is audible");
        check(pcm_has_signal(mode3_composite367, 0, kFrames),
              "mode3 digital composite logical 367 is audible");
        // Original round-nine capture submits silence first. At nominal 7042 Hz
        // this covers 627 output samples, not an entire 70 Hz capture frame.
        constexpr std::size_t sb_silent_samples = (100 * 44100 + 7042 - 1) / 7042;
        check(std::all_of(mode3_composite367.begin(),
                          mode3_composite367.begin() + sb_silent_samples * 2,
                          [](char byte) { return byte == 0; }),
              "mode3 composite idle start submits one silent 100-byte block");
        check(pcm_has_signal(mode3_serial367, 0, 1),
              "mode3 serial starts its declared payload without composite pre-roll");
        std::size_t mode3_serial_exhaustion_frame = 0;
        std::size_t mode3_composite_exhaustion_frame = 0;
        check(one_shot_resource_completes_owner_free(
                  mode3_serial367, mode3_serial_manifest, 0x367, 0x368,
                  mode3_serial_exhaustion_frame),
              "mode3 digital serial resource 368 completes owner-free without retrigger");
        check(one_shot_resource_completes_owner_free(
                  mode3_composite367, mode3_composite_manifest, 0x367, 0x368,
                  mode3_composite_exhaustion_frame),
              "mode3 digital composite resource 368 completes owner-free without retrigger");
        report_exhaustion("mode3 digital serial", mode3_serial_manifest);
        report_exhaustion("mode3 digital composite", mode3_composite_manifest);
        report_voice_config("mode3 digital serial", mode3_serial_manifest, 0);
        report_voice_config("mode3 digital composite", mode3_composite_manifest, 2);

        for (int mode : {0, 1, 2, 3}) {
            for (int next_mode : {0, 1, 2, 3}) {
                if (mode == next_mode) continue;
                const auto prefix = "switch-" + std::to_string(mode) + "-" +
                                    std::to_string(next_mode) + "-";
                const auto switched = render(
                    output, prefix + "reset", RenderOptions {
                        .mode = mode, .reset = true, .owner = true,
                        .keep_owner_active = true, .mode_after_reset = next_mode});
                // Compare new playback at the same sample boundary, not a
                // destination sound that has already played for 20 frames.
                const auto fresh = render(
                    output, prefix + "fresh", RenderOptions {
                        .mode = next_mode, .owner = true, .keep_owner_active = true,
                        .owner_start_frame = kResetFrame});
                const auto silent = render(
                    output, prefix + "silent", RenderOptions {.mode = next_mode});
                check(same_tail(fresh, silent) == (next_mode == 0),
                      prefix + "destination control has expected audibility");
                check(same_tail(switched, fresh),
                      prefix + "reset plus mode change rebinds unchanged owner");
            }
        }
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
