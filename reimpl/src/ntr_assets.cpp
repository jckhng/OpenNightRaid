#include "niteraid/ntr_assets.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace niteraid {
namespace {

constexpr std::size_t kRecordSize = 14;
constexpr std::size_t kRegisteredArchiveSize = 596269;
constexpr std::uint16_t kRegisteredResourceCount = 875;
constexpr std::size_t kSharewareArchiveSize = 791301;
constexpr std::uint16_t kSharewareResourceCount = 819;
constexpr int kCompiledSpriteStride = 420;
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 200;
constexpr std::int16_t kTransparent = -1;

struct ResourceRecord {
    std::uint16_t codec = 0;
    std::uint32_t payload_offset = 0;
    std::uint16_t decoded_size = 0;
    std::uint16_t stored_size = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("truncated GRAPHICS.NTR word");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

int signed_i8(std::uint8_t value)
{
    return value >= 0x80 ? static_cast<int>(value) - 0x100 : value;
}

bool is_raw_sprite(std::uint16_t id)
{
    switch (id) {
    case 0x026:
    case 0x027:
    case 0x028:
    case 0x207:
    case 0x24c:
    case 0x24d:
    case 0x24e:
    case 0x24f:
    case 0x250:
        return true;
    default:
        return false;
    }
}

class NtrArchive {
public:
    explicit NtrArchive(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open GRAPHICS.NTR");
        }
        data_ = std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        const auto resource_count = read_u16(data_, 0);
        if (data_.size() == kRegisteredArchiveSize &&
            resource_count == kRegisteredResourceCount) {
            edition_ = OriginalAssetEdition::Registered;
        } else if (data_.size() == kSharewareArchiveSize &&
                   resource_count == kSharewareResourceCount) {
            edition_ = OriginalAssetEdition::Shareware;
        } else {
            throw std::runtime_error("unsupported Night Raid graphics archive revision");
        }

        records_.reserve(resource_count);
        for (std::uint16_t id = 0; id < resource_count; ++id) {
            const auto offset = 2 + static_cast<std::size_t>(id) * kRecordSize;
            ResourceRecord record {};
            record.codec = read_u16(data_, offset);
            record.payload_offset = read_u16(data_, offset + 2) |
                                    (static_cast<std::uint32_t>(read_u16(data_, offset + 4)) << 16);
            record.decoded_size = read_u16(data_, offset + 6);
            record.stored_size = read_u16(data_, offset + 8);
            record.width = read_u16(data_, offset + 10);
            record.height = read_u16(data_, offset + 12);
            if (record.payload_offset + record.stored_size > data_.size()) {
                throw std::runtime_error("GRAPHICS.NTR resource exceeds archive bounds");
            }
            records_.push_back(record);
        }
    }

    std::vector<std::uint8_t> decode(std::uint16_t id) const
    {
        const auto physical_id = original_resource_id_for_edition(edition_, id);
        if (!physical_id || *physical_id >= records_.size()) {
            throw std::runtime_error("graphics archive resource id is unavailable in this edition");
        }
        return decode_physical(*physical_id);
    }

    std::vector<std::uint8_t> decode_physical(std::uint16_t id) const
    {
        if (id >= records_.size()) {
            throw std::runtime_error("graphics archive physical resource id is unavailable");
        }
        const auto& record = records_[id];
        const auto begin = data_.begin() + record.payload_offset;
        std::vector<std::uint8_t> payload(begin, begin + record.stored_size);
        if (record.codec == 0) {
            if (payload.size() != record.decoded_size) {
                throw std::runtime_error("invalid raw GRAPHICS.NTR resource size");
            }
            return payload;
        }

        std::vector<std::uint8_t> output;
        output.reserve(record.decoded_size);
        std::size_t source = 0;
        if (record.codec == 1) {
            while (output.size() < record.decoded_size) {
                if (source >= payload.size()) {
                    throw std::runtime_error("truncated GRAPHICS.NTR codec-1 stream");
                }
                auto control = payload[source++];
                for (int bit = 0; bit < 8 && output.size() < record.decoded_size; ++bit) {
                    if ((control & 1) != 0) {
                        if (source >= payload.size()) {
                            throw std::runtime_error("truncated GRAPHICS.NTR codec-1 literal");
                        }
                        output.push_back(payload[source++]);
                    } else {
                        if (source + 2 > payload.size()) {
                            throw std::runtime_error("truncated GRAPHICS.NTR codec-1 backreference");
                        }
                        const auto token = static_cast<std::uint16_t>(payload[source]) |
                                           static_cast<std::uint16_t>(payload[source + 1] << 8);
                        source += 2;
                        const auto distance = static_cast<std::size_t>(token & 0x0fff);
                        const auto length = static_cast<std::size_t>((token >> 12) + 2);
                        if (distance == 0 || distance > output.size()) {
                            throw std::runtime_error("invalid GRAPHICS.NTR codec-1 backreference");
                        }
                        auto copy = output.size() - distance;
                        for (std::size_t index = 0;
                             index < length && output.size() < record.decoded_size; ++index) {
                            output.push_back(output[copy++]);
                        }
                    }
                    control >>= 1;
                }
            }
        } else if (record.codec == 2) {
            while (true) {
                if (source >= payload.size()) {
                    throw std::runtime_error("missing GRAPHICS.NTR codec-2 terminator");
                }
                const auto control = payload[source++];
                if (control == 0) {
                    break;
                }
                if ((control & 0x80) != 0) {
                    if (source >= payload.size()) {
                        throw std::runtime_error("truncated GRAPHICS.NTR codec-2 repeat");
                    }
                    output.insert(output.end(), control & 0x7f, payload[source++]);
                } else {
                    if (source + control > payload.size()) {
                        throw std::runtime_error("truncated GRAPHICS.NTR codec-2 literal");
                    }
                    output.insert(output.end(), payload.begin() + source,
                                  payload.begin() + source + control);
                    source += control;
                }
            }
        } else {
            throw std::runtime_error("unsupported GRAPHICS.NTR codec");
        }

        if (output.size() != record.decoded_size) {
            throw std::runtime_error("invalid decoded GRAPHICS.NTR resource size");
        }
        return output;
    }

    IndexedAssetImage physical_raw_image(std::uint16_t id,
                                         std::uint16_t palette_id) const
    {
        const auto& record = records_.at(id);
        const auto decoded = decode_physical(id);
        const auto palette = decode_physical(palette_id);
        if (record.width == 0 || record.height == 0 ||
            decoded.size() != static_cast<std::size_t>(record.width) * record.height ||
            palette.size() != 768) {
            throw std::runtime_error("invalid physical GRAPHICS.NTR raw image");
        }

        IndexedAssetImage image {};
        image.width = record.width;
        image.height = record.height;
        image.palette = palette;
        image.pixels.assign(decoded.begin(), decoded.end());
        return image;
    }

    IndexedAssetImage sprite(std::uint16_t id, std::uint16_t palette_id) const
    {
        const auto physical_id = original_resource_id_for_edition(edition_, id);
        if (!physical_id) {
            throw std::runtime_error("sprite is unavailable in this archive edition");
        }
        const auto& record = records_.at(*physical_id);
        const auto palette = decode(palette_id);
        if (palette.size() != 768) {
            throw std::runtime_error("invalid GRAPHICS.NTR palette");
        }
        const auto decoded = decode(id);
        if (is_raw_sprite(id)) {
            if (decoded.size() != static_cast<std::size_t>(record.width) * record.height) {
                throw std::runtime_error("invalid raw GRAPHICS.NTR sprite size");
            }
            IndexedAssetImage image {};
            image.width = record.width;
            image.height = record.height;
            image.palette = palette;
            image.pixels.reserve(decoded.size());
            for (const auto pixel : decoded) {
                image.pixels.push_back(id == 0x026 && pixel == 0 ? kTransparent : pixel);
            }
            return image;
        }

        const auto writes = interpret_blitter(decoded);
        if (writes.empty()) {
            throw std::runtime_error("compiled GRAPHICS.NTR sprite produced no pixels");
        }
        int min_x = std::numeric_limits<int>::max();
        int min_y = std::numeric_limits<int>::max();
        int max_x = std::numeric_limits<int>::min();
        int max_y = std::numeric_limits<int>::min();
        for (const auto& [offset, value] : writes) {
            (void)value;
            const int x = offset % kCompiledSpriteStride;
            const int y = offset / kCompiledSpriteStride;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }

        IndexedAssetImage image {};
        image.width = std::max(max_x - min_x + 1, static_cast<int>(record.width));
        image.height = std::max(max_y - min_y + 1, static_cast<int>(record.height));
        image.origin_x = min_x;
        image.origin_y = min_y;
        image.palette = palette;
        image.pixels.assign(static_cast<std::size_t>(image.width) * image.height, kTransparent);
        for (const auto& [offset, value] : writes) {
            const int x = offset % kCompiledSpriteStride - min_x;
            const int y = offset / kCompiledSpriteStride - min_y;
            if (x >= 0 && x < image.width && y >= 0 && y < image.height) {
                image.pixels[static_cast<std::size_t>(y) * image.width + x] = value;
            }
        }
        return image;
    }

    IndexedAssetImage fullscreen(std::uint16_t plane_id, std::uint16_t palette_id) const
    {
        const auto plane = decode(plane_id);
        const auto palette = decode(palette_id);
        if (plane.size() != kScreenWidth * kScreenHeight || palette.size() != 768) {
            throw std::runtime_error("invalid GRAPHICS.NTR fullscreen resource");
        }
        IndexedAssetImage image {};
        image.width = kScreenWidth;
        image.height = kScreenHeight;
        image.palette = palette;
        image.pixels.assign(plane.begin(), plane.end());
        return image;
    }

    const std::vector<IndexedAssetImage>& overlay(std::uint16_t overlay_id,
                                                  std::uint16_t plane_id,
                                                  std::uint16_t palette_id)
    {
        if (const auto found = overlay_cache_.find(overlay_id); found != overlay_cache_.end()) {
            return found->second;
        }
        const auto overlay_data = decode(overlay_id);
        auto image = fullscreen(plane_id, palette_id);
        const auto frame_count = read_u16(overlay_data, 0);
        std::size_t frame_offset = 2 + static_cast<std::size_t>(frame_count) * 4;
        std::vector<IndexedAssetImage> frames;
        frames.reserve(frame_count);
        for (std::uint16_t frame = 0; frame < frame_count; ++frame) {
            const auto frame_size = read_u16(overlay_data, 2 + static_cast<std::size_t>(frame) * 4);
            if (frame_offset + frame_size > overlay_data.size()) {
                throw std::runtime_error("GRAPHICS.NTR overlay frame exceeds resource");
            }
            apply_overlay_frame(overlay_data, frame_offset, frame_size, image.pixels);
            frames.push_back(image);
            frame_offset += frame_size;
        }
        return overlay_cache_.emplace(overlay_id, std::move(frames)).first->second;
    }

    [[nodiscard]] OriginalAssetEdition edition() const
    {
        return edition_;
    }

private:
    static void write_byte(std::map<int, std::uint8_t>& writes, int offset, int value)
    {
        writes[offset] = static_cast<std::uint8_t>(value);
    }

    static void write_word(std::map<int, std::uint8_t>& writes, int offset, int value)
    {
        write_byte(writes, offset, value);
        write_byte(writes, offset + 1, value >> 8);
    }

    static std::map<int, std::uint8_t> interpret_blitter(const std::vector<std::uint8_t>& code)
    {
        std::size_t position = 0;
        int ax = 0;
        int cx = 0;
        int di = 0;
        std::map<int, std::uint8_t> writes;
        const auto word = [&code](std::size_t offset) { return read_u16(code, offset); };
        while (position < code.size()) {
            const auto opcode = code[position++];
            if (opcode == 0xcb) {
                return writes;
            }
            if (opcode == 0xb0) {
                ax = (ax & 0xff00) | code.at(position++);
            } else if (opcode == 0xb8) {
                ax = word(position);
                position += 2;
            } else if (opcode == 0xb1) {
                cx = (cx & 0xff00) | code.at(position++);
            } else if (opcode == 0x83) {
                const auto modrm = code.at(position++);
                const int immediate = signed_i8(code.at(position++));
                if (modrm == 0xc7) {
                    di += immediate;
                } else if (modrm == 0xef) {
                    di -= immediate;
                } else {
                    throw std::runtime_error("unsupported compiled sprite opcode 83");
                }
            } else if (opcode == 0x81) {
                const auto modrm = code.at(position++);
                const int immediate = word(position);
                position += 2;
                if (modrm == 0xc7) {
                    di += immediate;
                } else if (modrm == 0xef) {
                    di -= immediate;
                } else {
                    throw std::runtime_error("unsupported compiled sprite opcode 81");
                }
            } else if (opcode == 0x88 || opcode == 0x89 || opcode == 0xc6) {
                const auto modrm = code.at(position++);
                int displacement = 0;
                if (modrm == 0x87) {
                    displacement = word(position);
                    position += 2;
                } else if (modrm == 0x47) {
                    displacement = signed_i8(code.at(position++));
                } else {
                    throw std::runtime_error("unsupported compiled sprite destination");
                }
                if (opcode == 0x88) {
                    write_byte(writes, displacement, ax);
                } else if (opcode == 0x89) {
                    write_word(writes, displacement, ax);
                } else {
                    write_byte(writes, displacement, code.at(position++));
                }
            } else if (opcode == 0xaa) {
                write_byte(writes, di++, ax);
            } else if (opcode == 0xab) {
                write_word(writes, di, ax);
                di += 2;
            } else if (opcode == 0xf3) {
                const auto repeated = code.at(position++);
                if (repeated == 0xaa) {
                    while (cx-- > 0) {
                        write_byte(writes, di++, ax);
                    }
                } else if (repeated == 0xab) {
                    while (cx-- > 0) {
                        write_word(writes, di, ax);
                        di += 2;
                    }
                } else {
                    throw std::runtime_error("unsupported compiled sprite repeat opcode");
                }
                cx = 0;
            } else {
                throw std::runtime_error("unsupported compiled sprite opcode");
            }
        }
        throw std::runtime_error("compiled sprite has no return");
    }

    static void apply_overlay_frame(const std::vector<std::uint8_t>& code,
                                    std::size_t first, std::size_t size,
                                    std::vector<std::int16_t>& pixels)
    {
        const auto end = first + size;
        std::size_t pc = first;
        std::uint16_t ax = 0;
        std::uint16_t cx = 0;
        std::uint16_t si = 0;
        std::uint16_t di = 0;
        const auto put8 = [&pixels](std::uint16_t offset, std::uint8_t value) {
            if (offset < pixels.size()) {
                pixels[offset] = value;
            }
        };
        const auto put16 = [&put8](std::uint16_t offset, std::uint16_t value) {
            put8(offset, static_cast<std::uint8_t>(value));
            put8(static_cast<std::uint16_t>(offset + 1), static_cast<std::uint8_t>(value >> 8));
        };
        while (pc < end) {
            const auto opcode = code.at(pc++);
            if (opcode == 0xcb) {
                return;
            }
            if (opcode == 0x33) {
                if (code.at(pc++) != 0xc9) {
                    throw std::runtime_error("unsupported overlay xor opcode");
                }
                cx = 0;
            } else if (opcode == 0x81) {
                const auto modrm = code.at(pc++);
                const auto immediate = read_u16(code, pc);
                pc += 2;
                if (modrm == 0xc6) si = static_cast<std::uint16_t>(si + immediate);
                else if (modrm == 0xc7) di = static_cast<std::uint16_t>(di + immediate);
                else if (modrm == 0xee) si = static_cast<std::uint16_t>(si - immediate);
                else if (modrm == 0xef) di = static_cast<std::uint16_t>(di - immediate);
                else throw std::runtime_error("unsupported overlay opcode 81");
            } else if (opcode == 0x8b) {
                if (code.at(pc++) != 0xdf) throw std::runtime_error("unsupported overlay mov");
            } else if (opcode == 0xb1) {
                cx = static_cast<std::uint16_t>((cx & 0xff00) | code.at(pc++));
            } else if (opcode == 0xb0) {
                ax = static_cast<std::uint16_t>((ax & 0xff00) | code.at(pc++));
            } else if (opcode == 0xb8) {
                ax = read_u16(code, pc);
                pc += 2;
            } else if (opcode == 0xb9) {
                cx = read_u16(code, pc);
                pc += 2;
            } else if (opcode == 0xaa) {
                put8(di++, static_cast<std::uint8_t>(ax));
            } else if (opcode == 0xab) {
                put16(di, ax);
                di = static_cast<std::uint16_t>(di + 2);
            } else if (opcode == 0xa4) {
                put8(di++, code.at(first + si++));
            } else if (opcode == 0xf3) {
                const auto repeated = code.at(pc++);
                if (repeated == 0xab) {
                    while (cx-- > 0) {
                        put16(di, ax);
                        di = static_cast<std::uint16_t>(di + 2);
                    }
                } else if (repeated == 0xa5) {
                    while (cx-- > 0) {
                        put16(di, read_u16(code, first + si));
                        si = static_cast<std::uint16_t>(si + 2);
                        di = static_cast<std::uint16_t>(di + 2);
                    }
                } else {
                    throw std::runtime_error("unsupported overlay repeat opcode");
                }
                cx = 0;
            } else {
                throw std::runtime_error("unsupported overlay opcode");
            }
        }
        throw std::runtime_error("overlay frame has no return");
    }

    std::vector<std::uint8_t> data_ {};
    std::vector<ResourceRecord> records_ {};
    std::unordered_map<std::uint16_t, std::vector<IndexedAssetImage>> overlay_cache_ {};
    OriginalAssetEdition edition_ = OriginalAssetEdition::None;
};

std::mutex archive_mutex;
std::unique_ptr<NtrArchive> archive;
bool archive_only = false;

std::optional<std::pair<std::uint16_t, std::uint16_t>> fullscreen_ids(std::string_view name)
{
    if (name == "control_panel.bmp") return std::pair {std::uint16_t {0x001}, std::uint16_t {0x000}};
    if (name == "credits.bmp") return std::pair {std::uint16_t {0x037}, std::uint16_t {0x036}};
    if (name == "title.bmp") return std::pair {std::uint16_t {0x03a}, std::uint16_t {0x039}};
    if (name == "high_score.bmp") return std::pair {std::uint16_t {0x03f}, std::uint16_t {0x03e}};
    if (name == "gameplay.bmp") return std::pair {std::uint16_t {0x0e5}, std::uint16_t {0x09b}};
    if (name == "attract_interlude.bmp" || name == "transition.bmp") {
        return std::pair {std::uint16_t {0x03c}, std::uint16_t {0x03b}};
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::uint16_t> original_resource_id_for_edition(
    OriginalAssetEdition edition, std::uint16_t id)
{
    if (edition == OriginalAssetEdition::Registered) {
        return id < kRegisteredResourceCount ? std::optional {id} : std::nullopt;
    }
    if (edition != OriginalAssetEdition::Shareware) {
        return std::nullopt;
    }

    if (id <= 0x15a) return id;
    if (id >= 0x1be && id <= 0x267) return static_cast<std::uint16_t>(id - 0x63);
    if (id >= 0x28d && id <= 0x2e0) return static_cast<std::uint16_t>(id - 0x88);
    if (id >= 0x318 && id <= 0x31b) return static_cast<std::uint16_t>(id - 0x23);
    if (id >= 0x31e && id <= 0x321) return static_cast<std::uint16_t>(id - 0x25);
    if (id >= 0x327 && id <= 0x348) return static_cast<std::uint16_t>(id - 0x2a);
    if (id >= 0x357 && id <= 0x36a) return static_cast<std::uint16_t>(id - 0x38);
    return std::nullopt;
}

std::filesystem::path find_original_asset_archive(
    const std::filesystem::path& executable, const std::filesystem::path& working_directory,
    ArchiveSearchScope scope)
{
    const auto find_in = [](const std::filesystem::path& directory) -> std::filesystem::path {
        if (directory.empty()) return {};
        for (const auto* name : {"GRAPHICS.NTR", "graphics.ntr", "GRAPHICS.NRD", "graphics.nrd"}) {
            const auto candidate = directory / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        }
        return {};
    };
    const auto executable_dir = executable.parent_path();
    // An installed edition beside the executable always wins, including shareware.
    if (auto path = find_in(executable_dir); !path.empty()) {
        return path;
    }
    if (scope == ArchiveSearchScope::AdjacentOnly) {
        return {};
    }
    if (auto path = find_in(working_directory); !path.empty()) return path;

    // Development builds otherwise find old loose BMPs but miss newly recovered
    // archive-only resources. Only probe a source checkout, not arbitrary parents.
    for (auto root : {executable_dir, working_directory}) {
        for (int depth = 0; depth < 6 && !root.empty(); ++depth) {
            std::error_code error;
            if (std::filesystem::is_regular_file(root / "reimpl" / "src" / "app.cpp", error)) {
                if (auto path = find_in(root / "original" / "NITERAID"); !path.empty()) return path;
            }
            const auto parent = root.parent_path();
            if (parent == root) break;
            root = parent;
        }
    }
    return {};
}

bool configure_original_asset_archive(const std::filesystem::path& path)
{
    std::lock_guard lock(archive_mutex);
    try {
        archive = std::make_unique<NtrArchive>(path);
        return true;
    } catch (...) {
        archive.reset();
        return false;
    }
}

bool original_asset_archive_available()
{
    std::lock_guard lock(archive_mutex);
    return archive != nullptr;
}

OriginalAssetEdition original_asset_edition()
{
    std::lock_guard lock(archive_mutex);
    return archive != nullptr ? archive->edition() : OriginalAssetEdition::None;
}

void set_original_archive_only(bool enabled)
{
    std::lock_guard lock(archive_mutex);
    archive_only = enabled;
}

bool original_archive_only()
{
    std::lock_guard lock(archive_mutex);
    return archive_only;
}

std::optional<std::vector<std::uint8_t>> load_original_resource(std::uint16_t resource_id)
{
    std::lock_guard lock(archive_mutex);
    if (archive == nullptr) {
        return std::nullopt;
    }
    try {
        return archive->decode(resource_id);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<IndexedAssetImage> load_shareware_ending_image(
    std::uint16_t physical_resource_id)
{
    std::lock_guard lock(archive_mutex);
    if (archive == nullptr || archive->edition() != OriginalAssetEdition::Shareware ||
        physical_resource_id < 0x25a || physical_resource_id > 0x2f4) {
        return std::nullopt;
    }
    try {
        return archive->physical_raw_image(physical_resource_id, 0x259);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<IndexedAssetImage> load_original_image(std::string_view relative_path)
{
    std::lock_guard lock(archive_mutex);
    if (archive == nullptr) {
        return std::nullopt;
    }
    try {
        const auto filename = std::filesystem::path(relative_path).filename().string();
        if (const auto ids = fullscreen_ids(filename)) {
            return archive->fullscreen(ids->first, ids->second);
        }

        unsigned int sprite_id = 0;
        if (std::sscanf(filename.c_str(), "sprite_%x.bmp", &sprite_id) == 1) {
            const auto palette_id = filename.find("_pal_09d") != std::string::npos ? 0x09d : 0x09b;
            return archive->sprite(static_cast<std::uint16_t>(sprite_id), palette_id);
        }

        unsigned int frame = 0;
        if (std::sscanf(filename.c_str(), "credits_overlay_%u.bmp", &frame) == 1) {
            const auto& frames = archive->overlay(0x038, 0x037, 0x036);
            if (frame < frames.size()) return frames[frame];
        }
        if (std::sscanf(filename.c_str(), "attract_interlude_overlay_%u.bmp", &frame) == 1) {
            const auto& frames = archive->overlay(0x03d, 0x03c, 0x03b);
            if (frame < frames.size()) return frames[frame];
        }
    } catch (...) {
    }
    return std::nullopt;
}

}  // namespace niteraid
