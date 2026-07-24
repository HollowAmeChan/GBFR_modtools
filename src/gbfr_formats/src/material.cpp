#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using json = nlohmann::json;

std::uint32_t enum_hash(const json& value, std::initializer_list<std::pair<std::string_view, std::uint32_t>> known) {
    if (value.is_number_unsigned() || value.is_number_integer()) return value.get<std::uint32_t>();
    if (!value.is_string()) return 0;
    const auto name = value.get<std::string>();
    const auto found = std::find_if(known.begin(), known.end(), [&](const auto& item) { return item.first == name; });
    if (found != known.end()) return found->second;
    if (name.size() >= 10 && name[0] == 'g' && name[1] == '_') {
        std::uint32_t embedded{};
        const auto parsed = std::from_chars(name.data() + 2, name.data() + 10, embedded, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == name.data() + 10) return embedded;
    }
    return 0;
}

std::string enum_name(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    return {};
}

std::uint32_t parameter_hash(const json& value) {
    return enum_hash(value, {
        {"g_EmissivePower", 0x06CFE5A4u}, {"g_EnableDiscardMask", 0x24C1ABA9u},
        {"g_UseIceEmissive0", 0x3C966EE3u}, {"g_EnableOutLine", 0x49D8C1B9u},
        {"g_53F49792_EnableAlpha_GUESSED", 0x53F49792u},
        {"g_IsUseAlbedoAlphaClip", 0x60F31A22u}, {"g_IsUseDetailNormal", 0x6C5CB9ACu},
        {"g_IsUseDitherMap", 0x7920C84Fu}, {"g_EnableBooleanMask", 0x920821E1u},
        {"g_SwayAmplitude", 0x98EBBEC2u}, {"g_ContainerUse", 0x9F1DA064u},
        {"g_IsUseDepthFade", 0xB460A0F0u}, {"g_UseIceEmissive", 0xCA06A6B6u},
        {"g_TwoSided", 0xD94F2821u}, {"g_UseColorNoise", 0xE208C4C4u}
    });
}

std::uint32_t texture_hash(const json& value) {
    return enum_hash(value, {
        {"g_AlbedoMap",0x3F2B4D59u},{"g_AlbedoMapFar",0x7847F758u},{"g_AlbedoMapMiddle",0x56C35C30u},
        {"g_AlbedoTex",0xE9AEA597u},{"g_AlbedoTex0",0x7D82DDEAu},{"g_AlbedoTex1",0x8FC0A070u},
        {"g_AlbedoTex2",0x19615C52u},{"g_AlbedoTex3",0xA697D782u},{"g_AreaMaskMap",0xD52525E5u},
        {"g_Base0Map",0x9EE04147u},{"g_Base1Map",0x46C247DBu},{"g_BottomErosion0Map",0xAD237ACFu},
        {"g_BottomErosion1Map",0x47DA21A1u},{"g_BumpMap",0xE19336DEu},{"g_BumpMaskMap",0x707A6889u},
        {"g_BumpNormalMap",0x9B7115C3u},{"g_Color0Map",0x7AF0C744u},{"g_Color1Map",0xC5089B10u},
        {"g_ContainerMap",0x6C92581Eu},{"g_DetailNormalMap",0x71F4A50Eu},{"g_DitherMap",0x0C914331u},
        {"g_EmissiveMap",0x5CDF6E8Fu},{"g_EyeHighLightTexture",0x00B36A70u},{"g_EyeIrisTexture",0x637A19F3u},
        {"g_EyeWhiteTexture",0xAEDB57AEu},{"g_FlowMap",0x983C09F6u},{"g_IBLTexture",0x330CF7B7u},
        {"g_Large0Map",0xC56364D9u},{"g_Large1Map",0x9FEF4F43u},{"g_Layer1Map",0x7373F664u},
        {"g_Layer2Map",0x3779219Eu},{"g_LowDetailMap",0xF8E10DF2u},{"g_LUT",0x69DF53A1u},
        {"g_Mask0Map",0xDB972A87u},{"g_Mask1",0x847A6CBDu},{"g_Mask1Map",0x42904E14u},
        {"g_Mask2",0x6137BA13u},{"g_Mask2Map",0x2D04F715u},{"g_Mask3",0x35091AFAu},{"g_Mask4",0x393263EFu},
        {"g_MaskMap",0x63C1ED71u},{"g_MaskMap1",0x3DCC2032u},{"g_MaskTex",0xD19EA412u},
        {"g_MaskTex0",0xAE860AB0u},{"g_MaskTex1",0x3069DB65u},{"g_MaskTex2",0xEDD2D2AFu},
        {"g_MaskTex3",0x4CC0E7B6u},{"g_Middle0Map",0x38054382u},{"g_Middle1Map",0xB70FDCD5u},
        {"g_MROEMap",0x4905E4E4u},{"g_MROMap",0x7852D3FEu},{"g_Noise0Map",0x451D0F3Au},
        {"g_Noise1Map",0x62F4BBF8u},{"g_NoiseGradationMap",0xB21CCD8Bu},{"g_NoiseMap",0x7159CBC3u},
        {"g_NoiseMaskMap",0x010A5EFAu},{"g_NormalMap",0xADBA7C37u},{"g_NormalMap1",0x1470B2FBu},
        {"g_NormalTex",0xE752FF91u},{"g_NormalTex0",0xB55D7961u},{"g_NormalTex1",0xFB542B74u},
        {"g_NormalTex2",0x295ED71Au},{"g_NormalTex3",0x82A0AA5Au},{"g_OffsetMask0Map",0x6ECBBABDu},
        {"g_OffsetMask1Map",0xAD768A4Fu},{"g_ParallaxTexture",0x1EE34406u},{"g_SideErosion0Map",0x1EC206C2u},
        {"g_SideErosion1Map",0xE21BFE74u},{"g_SideNoiseMap",0xA7D31F31u},{"g_SparkleNormalMap",0x64E256E8u},
        {"g_UberColorNoiseMap",0xC19A4B09u},{"g_WindMaskMap",0x1DD2F116u}
    });
}

gbfr::MaterialValueType parse_value_type(const json& value) {
    if (value.is_string()) {
        const auto name = value.get<std::string>();
        if (name == "U8") return gbfr::MaterialValueType::u8;
        if (name == "U16") return gbfr::MaterialValueType::u16;
        if (name == "F32") return gbfr::MaterialValueType::f32;
        if (name == "Vec2") return gbfr::MaterialValueType::vec2;
        if (name == "Vec3") return gbfr::MaterialValueType::vec3;
        if (name == "Vec4") return gbfr::MaterialValueType::vec4;
    }
    const auto numeric = value.is_number() ? value.get<int>() : -1;
    if (numeric >= 0 && numeric <= 5) return static_cast<gbfr::MaterialValueType>(numeric);
    return gbfr::MaterialValueType::unknown;
}

std::size_t float_component_count(gbfr::MaterialValueType type) {
    switch (type) {
    case gbfr::MaterialValueType::f32: return 1;
    case gbfr::MaterialValueType::vec2: return 2;
    case gbfr::MaterialValueType::vec3: return 3;
    case gbfr::MaterialValueType::vec4: return 4;
    default: return 0;
    }
}

void assign_preview_texture(gbfr::MaterialEntry& entry, std::uint32_t hash, const std::string& name) {
    if (name.empty() || gbfr::is_color_variant_texture(name)) return;
    switch (hash) {
    case gbfr::albedo_texture_slot_id: entry.albedo_name = name; break;
    case gbfr::eye_highlight_texture_slot_id: entry.eye_highlight_name = name; break;
    case gbfr::eye_mask_texture_slot_id: entry.eye_mask_name = name; break;
    case gbfr::eye_iris_texture_slot_id: entry.eye_iris_name = name; break;
    case gbfr::eye_conjunctiva_texture_slot_id: entry.eye_conjunctiva_name = name; break;
    case gbfr::face_mask2_texture_slot_id: entry.alpha_mask_name = name; break;
    default: break;
    }
}

gbfr::MaterialAsset load_legacy(const json& document) {
    gbfr::MaterialAsset result;
    result.legacy_schema = true;
    result.magic = document.value("Magic", 0u);
    const auto& entries = document.at("Entries1");
    result.entries.reserve(entries.size());
    for (const auto& source_entry : entries) {
        gbfr::MaterialEntry entry;
        bool alpha_enabled{};
        for (const auto& parameter : source_entry.value("A1", json::array())) {
            const auto hash = parameter.value("ID", 0u);
            const bool enabled = parameter.value("ID2", 0u) != 0u;
            if (hash == gbfr::enable_alpha_shader_parameter_id) alpha_enabled = enabled;
        }
        for (const auto& texture : source_entry.value("A2", json::array()))
            assign_preview_texture(entry, texture.value("ID", 0u), texture.value("Name", std::string{}));
        const bool subtype5 = alpha_enabled && !entry.albedo_name.empty() && source_entry.value("A7", 0u) == 5u;
        if (subtype5) for (const auto& index : source_entry.value("A3", json::array()))
            if (index.is_number_unsigned() && index.get<std::uint32_t>() == 6u) entry.alpha_masked = !entry.alpha_mask_name.empty();
        entry.alpha_blended = entry.alpha_masked;
        entry.alpha_clipped = alpha_enabled && !entry.albedo_name.empty() && !entry.alpha_blended;
        result.entries.push_back(std::move(entry));
    }
    return result;
}
}

namespace gbfr {
bool is_color_variant_texture(const std::string& name) {
    for (std::size_t i = 0; i + 3 < name.size(); ++i) {
        if (name[i] != '_' || name[i + 1] != 'c' || !std::isdigit(static_cast<unsigned char>(name[i + 2]))) continue;
        std::size_t end = i + 3;
        while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
        if (end < name.size() && name[end] == '_') return true;
    }
    return false;
}

MaterialAsset load_mmat_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open mmat JSON");
    json document;
    input >> document;
    if (document.contains("Entries1") && document["Entries1"].is_array()) return load_legacy(document);
    const auto entries = document.find("materials");
    if (entries == document.end() || !entries->is_array()) throw std::runtime_error("mmat JSON has no materials array (GBFRDataTools 2.0.0 schema required)");

    MaterialAsset result;
    result.magic = document.value("magic", 0u);
    result.unknown2 = static_cast<std::uint8_t>(document.value("unk2", 0u));
    result.bool3 = document.value("bool3", false);
    result.bool4 = document.value("bool4", false);
    result.bool5 = document.value("bool5", false);
    result.shader_parameter_float_pool = document.value("shader_param_float_data_pool", std::vector<float>{});
    for (const auto& source_buffer : document.value("constant_buffers", json::array())) {
        MaterialConstantBuffer buffer;
        buffer.words = source_buffer.value("buffer", std::vector<std::uint32_t>{});
        buffer.name_hash = source_buffer.value("unk_unique_param_name_hash", 0u);
        result.constant_buffers.push_back(std::move(buffer));
    }

    result.entries.reserve(entries->size());
    for (const auto& source_entry : *entries) {
        MaterialEntry entry;
        entry.material_name_hash = source_entry.value("unique_material_name_hash_maybe", 0u);
        entry.shader_type = static_cast<std::uint8_t>(source_entry.value("shader_type", 0u));
        entry.shader_sub_type = static_cast<std::uint8_t>(source_entry.value("shader_sub_type", 0u));
        const auto shadow = source_entry.value("shadow_type", json{});
        if (shadow.is_number()) entry.shadow_type = shadow.get<std::uint8_t>();
        else if (shadow.is_string()) {
            const auto name = shadow.get<std::string>();
            entry.shadow_type = name == "ShadowEnable_Unk1" ? 1u : name == "ShadowEnable_AlphaBlend" ? 2u : name == "ShadowEnable_NoAlphaBlend" ? 3u : 0u;
        }
        entry.bool9 = source_entry.value("bool9", false);
        entry.bool10 = source_entry.value("bool10", false);
        entry.ignore_alpha = source_entry.value("ignore_alpha", false);
        entry.bool12 = source_entry.value("bool12", false);
        entry.constant_buffer_indices = source_entry.value("constant_buffer_indices", std::vector<std::uint16_t>{});

        bool alpha_enabled{};
        for (const auto& source_parameter : source_entry.value("shader_params", json::array())) {
            MaterialShaderParameter parameter;
            const auto& hash_value = source_parameter.at("param_hash");
            parameter.name = enum_name(hash_value);
            parameter.hash = parameter_hash(hash_value);
            parameter.type = parse_value_type(source_parameter.value("value_type", json{}));
            parameter.value_or_offset = static_cast<std::uint16_t>(source_parameter.value("value_or_offset", 0u));
            const auto components = float_component_count(parameter.type);
            if (components && parameter.value_or_offset <= result.shader_parameter_float_pool.size() &&
                components <= result.shader_parameter_float_pool.size() - parameter.value_or_offset) {
                const auto begin = result.shader_parameter_float_pool.begin() + parameter.value_or_offset;
                parameter.floating_values.assign(begin, begin + static_cast<std::ptrdiff_t>(components));
            }
            if (parameter.hash == enable_alpha_shader_parameter_id) alpha_enabled = parameter.value_or_offset != 0;
            entry.shader_parameters.push_back(std::move(parameter));
        }
        for (const auto& source_texture : source_entry.value("texture_maps", json::array())) {
            MaterialTextureMap texture;
            const auto& hash_value = source_texture.at("shader_map_name_hash");
            texture.name = enum_name(hash_value);
            texture.hash = texture_hash(hash_value);
            texture.texture_name = source_texture.value("texture_name", std::string{});
            assign_preview_texture(entry, texture.hash, texture.texture_name);
            entry.texture_maps.push_back(std::move(texture));
        }
        if (const auto granite = source_entry.find("granite_params"); granite != source_entry.end() && granite->is_object()) {
            MaterialGraniteInfo info;
            info.page_files = granite->value("page_file", std::vector<std::string>{});
            for (const auto& layer : granite->value("layer_to_shader_map_name_hash", json::array())) {
                info.layer_hashes.push_back(texture_hash(layer));
                info.layer_names.push_back(enum_name(layer));
            }
            info.unknown4 = static_cast<std::uint8_t>(granite->value("unk4", 0u));
            info.unknown5 = static_cast<std::uint8_t>(granite->value("unk5", 0u));
            info.tile_set_number = static_cast<std::uint8_t>(granite->value("tile_set_number", 0u));
            entry.granite = std::move(info);
        }
        const bool subtype5 = alpha_enabled && !entry.albedo_name.empty() && entry.shader_sub_type == 5u;
        if (subtype5 && std::find(entry.constant_buffer_indices.begin(), entry.constant_buffer_indices.end(), 6u) != entry.constant_buffer_indices.end())
            entry.alpha_masked = !entry.alpha_mask_name.empty();
        entry.alpha_blended = entry.shadow_type == 2u || entry.alpha_masked;
        entry.alpha_clipped = alpha_enabled && !entry.albedo_name.empty() && !entry.alpha_blended;
        result.entries.push_back(std::move(entry));
    }
    return result;
}

const char* material_value_type_name(MaterialValueType type) noexcept {
    switch (type) {
    case MaterialValueType::u8: return "U8";
    case MaterialValueType::u16: return "U16";
    case MaterialValueType::f32: return "F32";
    case MaterialValueType::vec2: return "Vec2";
    case MaterialValueType::vec3: return "Vec3";
    case MaterialValueType::vec4: return "Vec4";
    default: return "Unknown";
    }
}
}
