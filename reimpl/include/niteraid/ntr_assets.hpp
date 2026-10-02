#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace niteraid {

enum class OriginalAssetEdition : std::uint8_t {
    None,
    Registered,
    Shareware,
};

enum class ArchiveSearchScope {
    AdjacentOnly,
    Development,
};

struct IndexedAssetImage {
    int width = 0;
    int height = 0;
    int origin_x = 0;
    int origin_y = 0;
    std::vector<std::int16_t> pixels {};
    std::vector<std::uint8_t> palette {};
};

bool configure_original_asset_archive(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path find_original_asset_archive(
    const std::filesystem::path& executable, const std::filesystem::path& working_directory,
    ArchiveSearchScope scope = ArchiveSearchScope::AdjacentOnly);
[[nodiscard]] bool original_asset_archive_available();
[[nodiscard]] OriginalAssetEdition original_asset_edition();
[[nodiscard]] std::optional<std::uint16_t> original_resource_id_for_edition(
    OriginalAssetEdition edition, std::uint16_t registered_resource_id);
void set_original_archive_only(bool enabled);
[[nodiscard]] bool original_archive_only();
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
load_original_resource(std::uint16_t resource_id);
[[nodiscard]] std::optional<IndexedAssetImage>
load_shareware_ending_image(std::uint16_t physical_resource_id);
[[nodiscard]] std::optional<IndexedAssetImage>
load_original_image(std::string_view relative_path);

}  // namespace niteraid
