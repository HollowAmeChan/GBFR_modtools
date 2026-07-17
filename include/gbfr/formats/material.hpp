#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbfr {
inline constexpr std::uint32_t albedo_texture_slot_id = 1059802457u;
inline constexpr std::uint32_t eye_highlight_texture_slot_id = 11758192u;
inline constexpr std::uint32_t eye_mask_texture_slot_id = 518210566u;
inline constexpr std::uint32_t eye_iris_texture_slot_id = 1668946419u;
inline constexpr std::uint32_t eye_conjunctiva_texture_slot_id = 2933610414u;
inline constexpr std::uint32_t face_mask2_texture_slot_id = 1631042067u;
inline constexpr std::uint32_t enable_alpha_shader_parameter_id = 0x53F49792u;

struct MaterialEntry {
    std::string albedo_name;
    std::string eye_highlight_name;
    std::string eye_mask_name;
    std::string eye_iris_name;
    std::string eye_conjunctiva_name;
    std::string alpha_mask_name;
    bool alpha_clipped{};
    bool alpha_blended{};
    bool alpha_masked{};
};

struct MaterialAsset {
    std::vector<MaterialEntry> entries;
};

MaterialAsset load_mmat_json(const std::filesystem::path& path);
bool is_color_variant_texture(const std::string& name);
}
