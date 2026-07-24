#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    const auto root = fs::current_path() / L".gbfr_mmat_test_temp";
    fs::remove_all(root);
    fs::create_directories(root / L"unpack");

    nlohmann::json document = {
        {"magic", 20230727},
        {"materials", nlohmann::json::array({{
            {"shader_params", nlohmann::json::array({{
                {"param_hash", "g_53F49792_EnableAlpha_GUESSED"}, {"value_or_offset", 1}, {"value_type", "U8"}
            }})},
            {"texture_maps", nlohmann::json::array({{
                {"shader_map_name_hash", "g_AlbedoMap"}, {"texture_name", "test_albd"}
            }})},
            {"constant_buffer_indices", nlohmann::json::array({0})},
            {"granite_params", {{"page_file", nlohmann::json::array({"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"})},
                                {"layer_to_shader_map_name_hash", nlohmann::json::array({"g_AlbedoMap"})}}},
            {"unique_material_name_hash_maybe", 0x12345678u},
            {"shader_type", 5}, {"shader_sub_type", 7}, {"shadow_type", "ShadowEnable_AlphaBlend"}
        }})},
        {"constant_buffers", nlohmann::json::array({{{"buffer", nlohmann::json::array({0x3f800000u, 1u})},
                                                       {"unk_unique_param_name_hash", 0x87654321u}}})}
    };
    const auto json_path = root / L"unpack/0.mmat.json";
    { std::ofstream output(json_path); output << document.dump(2) << '\n'; }
    const auto baseline = gbfr::sha256_file(json_path);
    {
        std::ofstream manifest(root / L"workspace.json");
        manifest << "{\"Version\":1,\"CharacterId\":\"mmat\",\"Materials\":[{"
                 << "\"Json\":\"unpack/0.mmat.json\",\"Source\":\"source/0.mmat\","
                 << "\"Output\":\"build/0.mmat\",\"BaselineSha256\":\"" << baseline << "\",\"SourceSha256\":\"\"}]}";
    }

    const auto parsed = gbfr::load_mmat_json(json_path);
    if (parsed.legacy_schema || parsed.magic != 20230727u || parsed.entries.size() != 1 ||
        parsed.constant_buffers.size() != 1 || parsed.constant_buffers[0].words[0] != 0x3f800000u ||
        !parsed.entries[0].granite || parsed.entries[0].texture_maps[0].hash != gbfr::albedo_texture_slot_id) return 1;

    auto workspace = gbfr::Workspace::load(root / L"workspace.json");
    if (workspace.material_granite_count(0) != 1) return 2;
    workspace.build_asset(0);
    if (!fs::is_regular_file(root / L"build/0.mmat") || fs::file_size(root / L"build/0.mmat") == 0) return 3;
    if (workspace.remove_material_granite(0) != 1 || workspace.material_granite_count(0) != 0) return 4;

    { std::ofstream output(json_path, std::ios::trunc); output << "{\"Magic\":20230727,\"Entries1\":[]}"; }
    bool rejected{};
    try { workspace.build_asset(0); } catch (...) { rejected = true; }
    if (!rejected || !gbfr::load_mmat_json(json_path).legacy_schema) return 5;

    fs::remove_all(root);
    return 0;
}
