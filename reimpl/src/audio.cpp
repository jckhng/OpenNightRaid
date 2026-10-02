#include "niteraid/audio.hpp"
#include "niteraid/audio_internals.hpp"
#include "niteraid/ntr_assets.hpp"
#include "niteraid/pc_speaker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#if defined(NITERAID_ENABLE_SDL2)
#include <SDL.h>
#endif

namespace niteraid {

namespace {

constexpr int kSampleRate = 44100;
constexpr float kSoundBlasterRate = static_cast<float>(
    audio_internals::sound_blaster_sample_rate(0x72));
constexpr float kPcSpeakerGain = 0.18f;
constexpr int kMusicEventRate = 140;
constexpr float kOplMusicGain = 8.00f;
constexpr float kDecodedSfxGain = 1.00f;
constexpr std::size_t kMusicBodyOffset = 0x168;
constexpr std::size_t kMusicSetupFirstOffset = 0x0003;
constexpr std::size_t kMusicSetupEndOffset = kMusicBodyOffset;
struct DecodedSfx {
    std::shared_ptr<const std::vector<float>> samples;
    float sample_rate = static_cast<float>(kSampleRate);
    std::size_t replacement_length = 0;
    std::uint16_t priority = 0;
    bool looping = false;
};

struct MusicRenderState {
    std::vector<std::uint8_t> data {};
    std::size_t offset = 0;
    int wait_samples = 0;
    std::uint8_t pending_delay = 0;
    opl3_chip opl {};
};

void write_pcm16_wav(const std::filesystem::path& path,
                     const std::vector<float>& source_samples)
{
    std::vector<std::int16_t> samples(source_samples.size());
    std::transform(source_samples.begin(), source_samples.end(), samples.begin(), [](float sample) {
        return static_cast<std::int16_t>(
            std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    });

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }

    const std::uint32_t data_size =
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riff_size = 36u + data_size;
    const std::uint32_t byte_rate = kSampleRate * sizeof(std::int16_t);
    const std::uint16_t audio_format = 1;
    const std::uint16_t channels = 1;
    const std::uint16_t bits_per_sample = 16;
    const std::uint16_t block_align = sizeof(std::int16_t);
    const std::uint32_t fmt_size = 16;
    const std::uint32_t sample_rate = kSampleRate;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char*>(&fmt_size), sizeof(fmt_size));
    out.write(reinterpret_cast<const char*>(&audio_format), sizeof(audio_format));
    out.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    out.write(reinterpret_cast<const char*>(&sample_rate), sizeof(sample_rate));
    out.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    out.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    out.write(reinterpret_cast<const char*>(&bits_per_sample), sizeof(bits_per_sample));
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    out.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(data_size));
}

std::uint16_t read_u16_le(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::size_t resource_declared_size(const std::vector<std::uint8_t>& bytes)
{
    return static_cast<std::size_t>(read_u16_le(bytes, 0)) |
           (static_cast<std::size_t>(read_u16_le(bytes, 2)) << 16u);
}

std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::optional<std::vector<std::uint8_t>> read_decoded_sfx_resource(std::uint16_t sound_id)
{
    char file_name[32] {};
    std::snprintf(file_name, sizeof(file_name), "sprite_%03x.bin", sound_id);

    std::vector<std::filesystem::path> candidates {
        std::filesystem::path("reimpl") / "assets" / "audio" / file_name,
        std::filesystem::path("assets") / "audio" / file_name,
        std::filesystem::path("tmp-ghidra") / "graphics-decoded" / file_name,
        std::filesystem::path("..") / ".." / "tmp-ghidra" / "graphics-decoded" / file_name,
    };
#if defined(NITERAID_ENABLE_SDL2)
    if (char* base_path = SDL_GetBasePath()) {
        candidates.push_back(std::filesystem::path(base_path) / "assets" / "audio" / file_name);
        SDL_free(base_path);
    }
#endif

    if (!original_archive_only()) {
        for (const auto& candidate : candidates) {
            if (auto bytes = read_file_bytes(candidate)) {
                return bytes;
            }
        }
    }
    return load_original_resource(sound_id);
}

std::optional<std::vector<std::uint8_t>> read_decoded_audio_resource(std::uint16_t resource_id)
{
    return read_decoded_sfx_resource(resource_id);
}

std::optional<DecodedSfx> decode_digital_sfx(std::uint16_t sound_id, bool sound_source,
                                          bool composite)
{
    const auto key = static_cast<std::uint32_t>(sound_id) |
                     (sound_source ? 0x10000u : 0u) |
                     (!composite ? 0x20000u : 0u);
    static std::unordered_map<std::uint32_t, std::optional<DecodedSfx>> cache;
    if (const auto iter = cache.find(key); iter != cache.end()) {
        return iter->second;
    }

    auto bytes = read_decoded_sfx_resource(sound_id);
    if (!bytes || bytes->size() < 10) {
        cache.emplace(key, std::nullopt);
        return std::nullopt;
    }

    // The original software mixer consumes fixed 100-byte DMA blocks. The
    // archive keeps the final padded block after the declared length. Reading
    // that block into the mixer does not guarantee submission to the device.
    const auto declared_size = resource_declared_size(*bytes);
    const auto stored_payload_size = bytes->size() - 10;
    // Both 061d (Sound Source) and 03d4 (SB) pass the declared byte count
    // directly to the device in serial mode. Padding belongs to the mixer.
    const auto payload_size = audio_internals::digital_payload_size(
        declared_size, stored_payload_size, composite);
    const auto priority = read_u16_le(*bytes, 8);
    if (payload_size == 0) {
        cache.emplace(key, std::nullopt);
        return std::nullopt;
    }

    std::vector<float> samples(payload_size);
    for (std::size_t index = 0; index < payload_size; ++index) {
        samples[index] =
            static_cast<float>(static_cast<int>((*bytes)[10 + index]) - 128) /
            128.0f * kDecodedSfxGain;
    }

    auto [iter, inserted] = cache.emplace(
        key,
        DecodedSfx {
            std::make_shared<const std::vector<float>>(std::move(samples)),
            sound_source ? static_cast<float>(audio_internals::kSoundSourceSampleRate) : kSoundBlasterRate,
            declared_size,
            priority,
            audio_internals::resource_repeats(read_u16_le(*bytes, 4))});
    (void)inserted;
    return iter->second;
}

std::optional<DecodedSfx> decode_pc_speaker_sfx(std::uint16_t sound_id)
{
    static std::unordered_map<std::uint16_t, std::optional<DecodedSfx>> cache;
    if (const auto iter = cache.find(sound_id); iter != cache.end()) {
        return iter->second;
    }

    auto bytes = read_decoded_sfx_resource(sound_id);
    if (!bytes || bytes->size() < 10) {
        cache.emplace(sound_id, std::nullopt);
        return std::nullopt;
    }

    // 1ffb:0724 rejects odd byte lengths, divides the declared length by two,
    // and consumes 16-bit PIT divisors from resource + 5 words.
    const auto declared_size = resource_declared_size(*bytes);
    const auto payload_size = std::min(declared_size, bytes->size() - 10);
    if ((payload_size & 1u) != 0 || payload_size == 0) {
        cache.emplace(sound_id, std::nullopt);
        return std::nullopt;
    }

    constexpr std::size_t samples_per_divisor = kSampleRate / kMusicEventRate;
    const auto divisor_count = payload_size / 2;
    std::vector<float> samples(divisor_count * samples_per_divisor, 0.0f);
    audio_internals::PcSpeakerOscillator oscillator;
    for (std::size_t divisor_index = 0; divisor_index < divisor_count; ++divisor_index) {
        const auto divisor = read_u16_le(*bytes, 10 + divisor_index * 2);
        oscillator.program(divisor);
        const auto first_sample = divisor_index * samples_per_divisor;
        for (std::size_t index = 0; index < samples_per_divisor; ++index) {
            samples[first_sample + index] = oscillator.sample() * kPcSpeakerGain;
        }
    }

    auto [iter, inserted] = cache.emplace(
        sound_id,
        DecodedSfx {
            std::make_shared<const std::vector<float>>(std::move(samples)),
            static_cast<float>(kSampleRate),
            declared_size,
            read_u16_le(*bytes, 8),
            audio_internals::resource_repeats(read_u16_le(*bytes, 4))});
    (void)inserted;
    return iter->second;
}

std::optional<std::vector<float>> render_music_resource(std::uint16_t music_id, std::uint32_t frames)
{
    auto bytes = read_decoded_audio_resource(music_id);
    if (!bytes || bytes->size() <= kMusicBodyOffset) {
        return std::nullopt;
    }

    MusicRenderState state {};
    state.data = std::move(*bytes);
    state.offset = kMusicBodyOffset;
    state.wait_samples = 0;
    state.pending_delay = 0;
    OPL3_Reset(&state.opl, kSampleRate);
    OPL3_WriteReg(&state.opl, 0x01, 0x20);
    OPL3_WriteReg(&state.opl, 0x08, 0x00);
    for (std::size_t offset = kMusicSetupFirstOffset;
         offset + 2 < state.data.size() && offset + 2 < kMusicSetupEndOffset;
         offset += 3) {
        OPL3_WriteReg(&state.opl, state.data[offset], state.data[offset + 1]);
    }

    const auto sample_count =
        static_cast<std::size_t>(frames) * kSampleRate / kSimulationRateHz;
    std::vector<float> samples(sample_count);
    for (auto& sample : samples) {
        while (state.wait_samples <= 0 && state.offset + 2 < state.data.size()) {
            const auto reg = state.data[state.offset++];
            const auto value = state.data[state.offset++];
            const auto delay = state.data[state.offset++];
            OPL3_WriteReg(&state.opl, reg, value);
            if (audio_internals::music_stream_loop_marker(delay)) {
                state.offset = kMusicBodyOffset;
                state.pending_delay = 0;
                continue;
            }
            if (state.pending_delay != 0) {
                state.wait_samples += std::max(1, static_cast<int>(state.pending_delay)) *
                                      (kSampleRate / kMusicEventRate);
            }
            state.pending_delay = delay;
        }

        if (state.offset + 2 >= state.data.size() && state.wait_samples <= 0) {
            state.offset = kMusicBodyOffset;
            state.pending_delay = 0;
        }
        if (state.wait_samples > 0) {
            --state.wait_samples;
        }

        std::int16_t stereo[2] {};
        OPL3_GenerateResampled(&state.opl, stereo);
        const float mono = (static_cast<float>(stereo[0]) + static_cast<float>(stereo[1])) /
                           (2.0f * 32768.0f);
        sample = std::clamp(mono * kOplMusicGain, -0.90f, 0.90f);
    }
    return samples;
}

}  // namespace

Audio::Audio(bool enabled, bool deterministic_capture)
    : deterministic_capture_(deterministic_capture)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled) {
        return;
    }
    if (deterministic_capture_) {
        enabled_ = true;
        return;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return;
    }

    SDL_AudioSpec desired {};
    desired.freq = kSampleRate;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = &Audio::audio_callback;
    desired.userdata = this;

    SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (device == 0) {
        return;
    }

    device_ = device;
    enabled_ = true;
    SDL_PauseAudioDevice(device, 0);
#else
    (void)enabled;
#endif
}

Audio::~Audio()
{
#if defined(NITERAID_ENABLE_SDL2)
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
    }
#endif
}

void Audio::lock_device()
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!deterministic_capture_ && device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
#endif
}

void Audio::unlock_device()
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!deterministic_capture_ && device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
#endif
}

void Audio::play_events(const WorldState& world)
{
    update_music(world);
    const auto sound_mode = world.audio_config_words[3];
    const bool source_composite = (sound_mode == 2 || sound_mode == 3) && world.audio_config_words[4] != 0;
    lock_device();
    if (digital_composite_ != source_composite) {
        active_.clear();
        object_sound_slots_.clear();
        digital_running_ = false;
        digital_composite_ = source_composite;
    }
    digital_sample_rate_ = sound_mode == 2 ? audio_internals::kSoundSourceSampleRate
                                          : static_cast<std::uint32_t>(kSoundBlasterRate);
    unlock_device();
    if (world.reset_sound_effects) {
        // 1ffb:13d5 resets SFX voices, not the separately owned OPL module.
        // Object +6 ownership survives this mixer reset until its owner changes.
        silence_sound_effects(sound_mode, false);
    }
    if (!audio_internals::screen_allows_sound_effects(world.screen)) {
        silence_sound_effects(sound_mode, true);
        return;
    }
    const auto voice_limit = (sound_mode == 2 || sound_mode == 3)
                                 ? audio_internals::configured_voice_limit(
                                       world.audio_config_words[4])
                                 : 1;
    update_owned_sounds(world, sound_mode, voice_limit);
    enforce_voice_limit(voice_limit);
    if (sound_mode == 0) {
        return;
    }
    for (const auto sound_id : world.sound_stop_events) {
        stop_sound(sound_id, sound_mode);
    }
    for (const auto sound_id : world.sound_events) {
        play_sound(sound_id, sound_mode, voice_limit);
    }
}

void Audio::stop_all()
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }
    lock_device();
    active_.clear();
    digital_running_ = false;
    object_sound_slots_.clear();
    music_.reset();
    current_music_id_ = 0;
    unlock_device();
#endif
}

void Audio::update_owned_sounds(const WorldState& world, std::uint16_t sound_mode,
                                std::size_t voice_limit)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    std::vector<std::uint16_t> next_slots(world.objects.size(), 0);
    for (std::size_t index = 0; index < world.objects.size(); ++index) {
        const auto& object = world.objects[index];
        if (object.active && !object.pending_destroy) {
            next_slots[index] = object.sound_id;
        }
    }

    if (sound_mode == 0 || sound_mode != object_sound_mode_) {
        lock_device();
        active_.clear();
        digital_running_ = false;
        unlock_device();
        object_sound_slots_.assign(next_slots.size(), 0);
        object_sound_mode_ = sound_mode;
    }

    if (sound_mode == 0) {
        return;
    }

    const auto slot_count = std::max(object_sound_slots_.size(), next_slots.size());
    for (std::size_t index = 0; index < slot_count; ++index) {
        const auto previous_id =
            index < object_sound_slots_.size() ? object_sound_slots_[index] : 0;
        const auto next_id = index < next_slots.size() ? next_slots[index] : 0;
        if (previous_id != 0 && previous_id != next_id) {
            stop_sound(previous_id, sound_mode);
        }
    }
    for (std::size_t index = 0; index < slot_count; ++index) {
        const auto previous_id =
            index < object_sound_slots_.size() ? object_sound_slots_[index] : 0;
        const auto next_id = index < next_slots.size() ? next_slots[index] : 0;
        if (next_id != 0 && previous_id != next_id) {
            play_sound(next_id, sound_mode, voice_limit);
        }
    }

    object_sound_slots_ = std::move(next_slots);
#else
    (void)world;
    (void)sound_mode;
    (void)voice_limit;
#endif
}

void Audio::write_diagnostic_wav(const WorldState& world, const char* path, std::uint32_t frames)
{
#if defined(NITERAID_ENABLE_SDL2)
    const auto desired = music_for_world(world);
    if (!desired) {
        return;
    }
    auto rendered = render_music_resource(*desired, frames);
    if (!rendered || rendered->empty()) {
        return;
    }
    write_pcm16_wav(path, *rendered);
#else
    (void)world;
    (void)path;
    (void)frames;
#endif
}

void Audio::capture_frame(const WorldState& world, bool record)
{
    capture_interval(world, record, true);
}

void Audio::capture_hold(const WorldState& world, bool record)
{
    capture_interval(world, record, false);
}

void Audio::capture_interval(const WorldState& world, bool record, bool apply_events)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }
    if (apply_events) play_events(world);
    constexpr std::size_t samples_per_frame = kSampleRate / kSimulationRateHz;
    std::array<float, samples_per_frame> frame {};
    mix_samples(frame.data(), frame.size());
    if (record) {
        captured_samples_.insert(captured_samples_.end(), frame.begin(), frame.end());
        CaptureFrameTrace trace {};
        trace.capture_frame = static_cast<std::uint32_t>(capture_trace_.size());
        trace.frame_tick = world.frame_tick;
        trace.screen = world.screen;
        trace.gameplay_state = world.gameplay_state;
        trace.sound_mode = world.audio_config_words[3];
        trace.voice_config = world.audio_config_words[4];
        trace.events_applied = apply_events;
        if (apply_events) {
            trace.logical_events = world.sound_events;
            trace.logical_stop_events = world.sound_stop_events;
            trace.reset_sound_effects = world.reset_sound_effects;
        }
        trace.resource_events.reserve(world.sound_events.size());
        for (const auto logical_id : trace.logical_events) {
            trace.resource_events.push_back(
                audio_internals::backend_sound_resource(logical_id, trace.sound_mode));
        }
        trace.resource_stop_events.reserve(world.sound_stop_events.size());
        for (const auto logical_id : trace.logical_stop_events) {
            trace.resource_stop_events.push_back(
                audio_internals::backend_sound_resource(logical_id, trace.sound_mode));
        }
        if (const auto desired_music = music_for_world(world)) {
            trace.music_resource = *desired_music;
        }
        for (const auto logical_id : object_sound_slots_) {
            if (logical_id == 0) {
                continue;
            }
            trace.owner_logical_sounds.push_back(logical_id);
            trace.owner_resource_sounds.push_back(
                audio_internals::backend_sound_resource(logical_id, trace.sound_mode));
        }
        for (const auto& voice : active_) {
            trace.active_resource_sounds.push_back(voice.resource_id);
            trace.active_source_sample_counts.push_back(static_cast<std::uint32_t>(voice.samples->size()));
            trace.active_source_sample_rates.push_back(static_cast<std::uint32_t>(voice.sample_rate));
            if (!voice.looping) {
                continue;
            }
            trace.owner_logical_loops.push_back(voice.logical_id);
            trace.owner_resource_loops.push_back(voice.resource_id);
            if (audio_internals::is_aircraft_sound_logical_id(voice.logical_id)) {
                trace.aircraft_logical_loops.push_back(voice.logical_id);
                trace.aircraft_resource_loops.push_back(voice.resource_id);
            }
        }
        capture_trace_.push_back(std::move(trace));
    }
#else
    (void)world;
    (void)record;
    (void)apply_events;
#endif
}

void Audio::write_capture_manifest(const char* path) const
{
#if defined(NITERAID_ENABLE_SDL2)
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }

    out << "{\n";
    out << "  \"format\": \"niteraid-mixed-audio-trace-v1\",\n";
    out << "  \"sample_rate\": " << kSampleRate << ",\n";
    out << "  \"fps\": " << kSimulationRateHz << ",\n";
    out << "  \"captured_samples\": " << captured_samples_.size() << ",\n";
    out << "  \"frames\": [\n";
    for (std::size_t index = 0; index < capture_trace_.size(); ++index) {
        const auto& trace = capture_trace_[index];
        out << "    {\"capture_frame\": " << trace.capture_frame;
        out << ", \"frame_tick\": " << trace.frame_tick;
        out << ", \"events_applied\": " << (trace.events_applied ? "true" : "false");
        out << ", \"screen\": \"" << to_string(trace.screen) << "\"";
        out << ", \"gameplay_state\": \"" << to_string(trace.gameplay_state) << "\"";
        out << ", \"sound_mode\": " << trace.sound_mode;
        out << ", \"voice_config\": " << trace.voice_config;
        out << ", \"music_resource\": " << trace.music_resource;
        out << ", \"reset_sound_effects\": " << (trace.reset_sound_effects ? "true" : "false");
        const auto write_ids = [&out](const char* name, const auto& ids) {
            out << ", \"" << name << "\": [";
            for (std::size_t id_index = 0; id_index < ids.size(); ++id_index) {
                if (id_index != 0) {
                    out << ", ";
                }
                out << ids[id_index];
            }
            out << "]";
        };
        write_ids("logical_events", trace.logical_events);
        write_ids("resource_events", trace.resource_events);
        write_ids("logical_stop_events", trace.logical_stop_events);
        write_ids("resource_stop_events", trace.resource_stop_events);
        write_ids("active_resource_sounds", trace.active_resource_sounds);
        write_ids("active_source_sample_counts", trace.active_source_sample_counts);
        write_ids("active_source_sample_rates", trace.active_source_sample_rates);
        write_ids("aircraft_logical_loops", trace.aircraft_logical_loops);
        write_ids("aircraft_resource_loops", trace.aircraft_resource_loops);
        write_ids("owner_logical_sounds", trace.owner_logical_sounds);
        write_ids("owner_resource_sounds", trace.owner_resource_sounds);
        write_ids("owner_logical_loops", trace.owner_logical_loops);
        write_ids("owner_resource_loops", trace.owner_resource_loops);
        out << "}";
        if (index + 1 != capture_trace_.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
#else
    (void)path;
#endif
}

void Audio::write_capture_wav(const char* path) const
{
#if defined(NITERAID_ENABLE_SDL2)
    write_pcm16_wav(path, captured_samples_);
#else
    (void)path;
#endif
}

std::optional<std::uint16_t> Audio::music_for_world(const WorldState& world)
{
    if (!audio_internals::music_enabled(world.audio_config_words[5])) {
        return std::nullopt;
    }

    switch (world.screen) {
    case Screen::Title:
    case Screen::Credits:
    case Screen::AttractInterlude:
    case Screen::HighScores:
        return 0x319;
    case Screen::ControlPanel:
        return 0x31a;
    case Screen::HighScoreEntry:
        return 0x318;
    case Screen::Finale:
        return 0x31d;
    case Screen::SharewareEnding:
        return std::nullopt;
    case Screen::Gameplay:
        if (audio_internals::bunker_assault_suppresses_music(world)) {
            return std::nullopt;
        }
        if (world.current_level >= WorldState::kFinalGameplayLevel) {
            return 0x31d;
        }
        return static_cast<std::uint16_t>(0x31e + std::min<std::uint16_t>(world.current_level, 8));
    case Screen::Intermission:
        if (world.milestone_intermission == MilestoneIntermission::Level4Pizza) {
            return 0x31b;
        }
        if (world.milestone_intermission == MilestoneIntermission::Level8Helicopter) {
            return 0x31c;
        }
        // 16c8:4e52 loads milestone modules only after the regular
        // survivor/flyby owner returns.
        return std::nullopt;
    case Screen::GameOver:
        return std::nullopt;
    }

    return std::nullopt;
}

void Audio::update_music(const WorldState& world)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    const auto desired = music_for_world(world);
    if (!desired) {
        stop_music();
        return;
    }
    if (!music_ || current_music_id_ != *desired) {
        start_music(*desired);
    }
#else
    (void)world;
#endif
}

void Audio::play_sound(std::uint16_t sound_id, std::uint16_t sound_mode,
                       std::size_t voice_limit)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    const auto resource_id = audio_internals::backend_sound_resource(sound_id, sound_mode);
    std::optional<DecodedSfx> decoded;
    if (sound_mode == 1) {
        decoded = decode_pc_speaker_sfx(sound_id);
    } else {
        decoded = decode_digital_sfx(resource_id, sound_mode == 2, voice_limit > 1);
    }
    if (!decoded) {
        return;
    }
    if (!decoded->samples || decoded->samples->empty()) {
        return;
    }

    lock_device();
    voice_limit = std::max<std::size_t>(1, voice_limit);
    const bool source_idle = active_.empty();
    while (active_.size() >= voice_limit) {
        const auto lowest = std::min_element(
            active_.begin(), active_.end(), &Audio::voice_is_weaker);
        if (lowest != active_.end() && lowest->priority <= decoded->priority) {
            active_.erase(lowest);
        } else {
            unlock_device();
            return;
        }
    }
    active_.push_back(
        ActiveVoice {
            decoded->samples,
            0.0,
            decoded->sample_rate,
            decoded->replacement_length,
            decoded->priority,
            sound_id,
            resource_id,
            decoded->looping});
    if (digital_composite_ && source_idle) {
        // 0865 -> 032e clears both buffers; 0a13 submits silence before
        // 0343 prepares the first resource block. This is one shared queue,
        // not a per-voice delay (voices may join an already running stream).
        digital_queued_.fill(0.0f);
        digital_phase_ = 0;
        digital_running_ = true;
        prepare_digital_block();
    }
    unlock_device();
#else
    (void)sound_id;
    (void)sound_mode;
    (void)voice_limit;
#endif
}

void Audio::stop_sound(std::uint16_t sound_id, std::uint16_t sound_mode)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    const auto resource_id = audio_internals::backend_sound_resource(sound_id, sound_mode);
    lock_device();
    const auto voice = std::find_if(active_.begin(), active_.end(), [resource_id](const ActiveVoice& active) {
        return active.resource_id == resource_id;
    });
    if (voice != active_.end()) {
        active_.erase(voice);
        // Composite 140d -> 0a80 only removes the slot. Both devices keep
        // its submitted block until completion; a mixer reset cancels it.
    }
    unlock_device();
#else
    (void)sound_id;
    (void)sound_mode;
#endif
}

void Audio::silence_sound_effects(std::uint16_t sound_mode, bool clear_owner_slots)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    lock_device();
    active_.clear();
    digital_running_ = false;
    unlock_device();
    if (clear_owner_slots) {
        object_sound_slots_.clear();
        object_sound_mode_ = sound_mode;
    }
#else
    (void)sound_mode;
    (void)clear_owner_slots;
#endif
}

void Audio::enforce_voice_limit(std::size_t voice_limit)
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }

    voice_limit = std::max<std::size_t>(1, voice_limit);
    lock_device();
    while (active_.size() > voice_limit) {
        const auto lowest = std::min_element(
            active_.begin(), active_.end(), &Audio::voice_is_weaker);
        active_.erase(lowest);
    }
    unlock_device();
#else
    (void)voice_limit;
#endif
}

std::size_t Audio::remaining_source_samples(const ActiveVoice& voice)
{
    if (!voice.samples || voice.samples->empty()) {
        return 0;
    }

    const auto sample_count = voice.samples->size();
    double position = voice.source_position;
    if (voice.looping) {
        position = std::fmod(position, static_cast<double>(sample_count));
    }
    const auto declared_length = std::min(sample_count, voice.replacement_length);
    return audio_internals::software_mixer_remaining_length(
        declared_length, static_cast<std::size_t>(position));
}

bool Audio::voice_is_weaker(const ActiveVoice& lhs, const ActiveVoice& rhs)
{
    return audio_internals::mixer_voice_is_weaker(
        lhs.priority, remaining_source_samples(lhs),
        rhs.priority, remaining_source_samples(rhs));
}

void Audio::start_music(std::uint16_t music_id)
{
#if defined(NITERAID_ENABLE_SDL2)
    auto bytes = read_decoded_audio_resource(music_id);
    if (!bytes || bytes->size() <= kMusicBodyOffset) {
        stop_music();
        return;
    }

    lock_device();
    music_.emplace();
    auto& next = *music_;
    next.data = std::move(*bytes);
    next.offset = kMusicBodyOffset;
    OPL3_Reset(&next.opl, kSampleRate);
    OPL3_WriteReg(&next.opl, 0x01, 0x20);
    OPL3_WriteReg(&next.opl, 0x08, 0x00);
    for (std::size_t offset = kMusicSetupFirstOffset;
         offset + 2 < next.data.size() && offset + 2 < kMusicSetupEndOffset;
         offset += 3) {
        OPL3_WriteReg(&next.opl, next.data[offset], next.data[offset + 1]);
    }

    current_music_id_ = music_id;
    unlock_device();
#else
    (void)music_id;
#endif
}

void Audio::stop_music()
{
#if defined(NITERAID_ENABLE_SDL2)
    if (!enabled_) {
        return;
    }
    lock_device();
    music_.reset();
    current_music_id_ = 0;
    unlock_device();
#endif
}

float Audio::render_music_sample()
{
    if (!music_ || music_->data.size() <= kMusicBodyOffset) {
        return 0.0f;
    }
    auto& music = *music_;
    while (music.wait_samples <= 0) {
        if (music.offset + 2 >= music.data.size()) {
            // The original player restarts the module body, not an arbitrary
            // fixed-duration PCM render. Preserve the live OPL register state.
            music.offset = kMusicBodyOffset;
            music.pending_delay = 0;
        }

        const auto reg = music.data[music.offset++];
        const auto value = music.data[music.offset++];
        const auto delay = music.data[music.offset++];
        OPL3_WriteReg(&music.opl, reg, value);
        if (audio_internals::music_stream_loop_marker(delay)) {
            music.offset = kMusicBodyOffset;
            music.pending_delay = 0;
            continue;
        }
        if (music.pending_delay != 0) {
            music.wait_samples += std::max(1, static_cast<int>(music.pending_delay)) *
                                  (kSampleRate / kMusicEventRate);
        }
        music.pending_delay = delay;
    }
    --music.wait_samples;

    std::int16_t stereo[2] {};
    OPL3_GenerateResampled(&music.opl, stereo);
    const float mono = (static_cast<float>(stereo[0]) + static_cast<float>(stereo[1])) /
                       (2.0f * 32768.0f);
    return std::clamp(mono * kOplMusicGain, -0.90f, 0.90f);
}

void Audio::audio_callback(void* userdata, std::uint8_t* stream, int len)
{
#if defined(NITERAID_ENABLE_SDL2)
    auto* self = static_cast<Audio*>(userdata);
    auto* out = reinterpret_cast<float*>(stream);
    const auto sample_count = static_cast<std::size_t>(len) / sizeof(float);
    self->mix_samples(out, sample_count);
#else
    (void)userdata;
    (void)stream;
    (void)len;
#endif
}

void Audio::prepare_digital_block()
{
    digital_prepared_.fill(0.0f);
    for (const auto& voice : active_) {
        if (!voice.samples) continue;
        const auto start = static_cast<std::size_t>(voice.source_position);
        for (std::size_t index = 0; index < digital_prepared_.size(); ++index) {
            if (start + index < voice.samples->size()) {
                digital_prepared_[index] += (*voice.samples)[start + index];
            }
        }
    }
    for (auto& sample : digital_prepared_) {
        sample = std::clamp(sample, -1.0f, 127.0f / 128.0f);
    }
}

float Audio::render_digital_sample()
{
    if (!digital_running_) return 0.0f;
    const auto index = digital_phase_ / kSampleRate;
    const auto next = std::min<std::size_t>(index + 1, digital_queued_.size() - 1);
    const float fraction = static_cast<float>(digital_phase_ % kSampleRate) / kSampleRate;
    const float result = digital_queued_[index] +
        (digital_queued_[next] - digital_queued_[index]) * fraction;
    // Nominal device clock only. Physical FIFO occupancy/refill latency is
    // not emulated here and must not be inferred from this queue model.
    digital_phase_ += digital_sample_rate_;
    if (digital_phase_ >= kSampleRate * digital_queued_.size()) {
        digital_phase_ -= kSampleRate * static_cast<std::uint32_t>(digital_queued_.size());
        for (auto& voice : active_) {
            voice.source_position += digital_queued_.size();
            if (voice.looping && voice.source_position >= voice.replacement_length) {
                voice.source_position = 0;
            }
        }
        active_.erase(std::remove_if(active_.begin(), active_.end(), [](const ActiveVoice& voice) {
            return !voice.looping && voice.source_position >= voice.replacement_length;
        }), active_.end());
        // 0b29 updates/removes voices before calling 0a13. When the final
        // voice ends, its already-prepared last block is never submitted.
        if (active_.empty()) {
            digital_running_ = false;
        } else {
            digital_queued_ = digital_prepared_;
            prepare_digital_block();
        }
    }
    return result;
}

void Audio::mix_samples(float* out, std::size_t sample_count)
{
#if defined(NITERAID_ENABLE_SDL2)
    std::fill(out, out + sample_count, 0.0f);

    for (std::size_t output_index = 0; output_index < sample_count; ++output_index) {
        if (digital_composite_) {
            out[output_index] = std::clamp(render_music_sample() + render_digital_sample(), -1.0f, 1.0f);
            continue;
        }
        float sfx_mix = 0.0f;
        for (auto& voice : active_) {
            if (!voice.samples || voice.samples->empty() ||
                (!voice.looping &&
                 voice.source_position >= static_cast<double>(voice.samples->size()))) {
                continue;
            }

            const auto sample_count_for_voice = voice.samples->size();
            double position = voice.source_position;
            if (voice.looping) {
                position = std::fmod(position, static_cast<double>(sample_count_for_voice));
            }
            const auto left = std::min<std::size_t>(
                sample_count_for_voice - 1, static_cast<std::size_t>(position));
            const auto right = voice.looping
                                   ? (left + 1) % sample_count_for_voice
                                   : std::min<std::size_t>(sample_count_for_voice - 1, left + 1);
            const float fraction =
                static_cast<float>(position - static_cast<double>(left));
            const float sample =
                (*voice.samples)[left] +
                ((*voice.samples)[right] - (*voice.samples)[left]) * fraction;
            sfx_mix += sample;
            voice.source_position +=
                static_cast<double>(voice.sample_rate) / static_cast<double>(kSampleRate);
        }

        sfx_mix = std::clamp(sfx_mix, -1.0f, 127.0f / 128.0f);
        out[output_index] =
            std::clamp(render_music_sample() + sfx_mix, -1.0f, 1.0f);
    }

    active_.erase(
        std::remove_if(active_.begin(), active_.end(), [](const ActiveVoice& voice) {
            return !voice.looping &&
                   (!voice.samples ||
                    voice.source_position >= static_cast<double>(voice.samples->size()));
        }),
        active_.end());
#else
    (void)out;
    (void)sample_count;
#endif
}

}  // namespace niteraid
