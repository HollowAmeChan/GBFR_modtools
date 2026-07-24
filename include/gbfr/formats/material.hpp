#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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
inline constexpr std::uint32_t two_sided_shader_parameter_id = 0xD94F2821u;

enum class MaterialValueType { u8, u16, f32, vec2, vec3, vec4, unknown };

struct MaterialShaderParameter {
    std::uint32_t hash{};
    std::string name;
    MaterialValueType type{MaterialValueType::unknown};
    std::uint16_t value_or_offset{};
    std::vector<float> floating_values;
};

struct MaterialTextureMap {
    std::uint32_t hash{};
    std::string name;
    std::string texture_name;
};

struct MaterialGraniteInfo {
    std::vector<std::string> page_files;
    std::vector<std::uint32_t> layer_hashes;
    std::vector<std::string> layer_names;
    std::uint8_t unknown4{};
    std::uint8_t unknown5{};
    std::uint8_t tile_set_number{};
};

struct MaterialConstantBuffer {
    std::vector<std::uint32_t> words;
    std::uint32_t name_hash{};
};

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
    std::vector<MaterialShaderParameter> shader_parameters;
    std::vector<MaterialTextureMap> texture_maps;
    std::vector<std::uint16_t> constant_buffer_indices;
    std::optional<MaterialGraniteInfo> granite;
    std::uint32_t material_name_hash{};
    std::uint8_t shader_type{};
    std::uint8_t shader_sub_type{};
    std::uint8_t shadow_type{};
    bool bool9{};
    bool bool10{};
    bool ignore_alpha{};
    bool bool12{};
};

struct MaterialAsset {
    std::uint32_t magic{};
    std::vector<MaterialEntry> entries;
    std::vector<MaterialConstantBuffer> constant_buffers;
    std::vector<float> shader_parameter_float_pool;
    std::uint8_t unknown2{};
    bool bool3{};
    bool bool4{};
    bool bool5{};
    bool legacy_schema{};
};

MaterialAsset load_mmat_json(const std::filesystem::path& path);
bool is_color_variant_texture(const std::string& name);
const char* material_value_type_name(MaterialValueType type) noexcept;
}
