#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbfr {
inline constexpr std::uint32_t albedo_texture_slot_id = 1059802457u;

struct MaterialEntry {
    std::string albedo_name;
};

struct MaterialAsset {
    std::vector<MaterialEntry> entries;
};

MaterialAsset load_mmat_json(const std::filesystem::path& path);
bool is_color_variant_texture(const std::string& name);
}
