#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

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
            {"texture_maps", nlohmann::json::array({
                {{"shader_map_name_hash", "g_AlbedoMap"}, {"texture_name", "test_albd"}},
                {{"shader_map_name_hash", "g_5A2C820C"}, {"texture_name", "custom_outline"}}
            })},
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
        !parsed.entries[0].granite || parsed.entries[0].texture_maps.size() != 2 ||
        parsed.entries[0].texture_maps[0].hash != gbfr::albedo_texture_slot_id ||
        parsed.entries[0].texture_maps[1].hash != 0x5A2C820Cu) return 1;

    auto workspace = gbfr::Workspace::load(root / L"workspace.json");
    if (workspace.material_granite_count(0) != 1) return 2;
    workspace.build_asset(0);
    if (!fs::is_regular_file(root / L"build/0.mmat") || fs::file_size(root / L"build/0.mmat") == 0) return 3;
    if (workspace.remove_material_granite(0) != 1 || workspace.material_granite_count(0) != 0) return 4;

    fs::create_directories(root / L"unpack/data/texture/2k");
    std::vector<unsigned char> manual_dds(128 + 16, 0);
    const auto put_u32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            manual_dds[offset + shift / 8] = static_cast<unsigned char>((value >> shift) & 0xff);
    };
    manual_dds[0] = 'D'; manual_dds[1] = 'D'; manual_dds[2] = 'S'; manual_dds[3] = ' ';
    put_u32(4, 124); put_u32(8, 0x00081007); put_u32(12, 4); put_u32(16, 4); put_u32(20, 16); put_u32(28, 1);
    put_u32(76, 32); put_u32(80, 4); manual_dds[84] = 'B'; manual_dds[85] = 'C'; manual_dds[86] = '5'; manual_dds[87] = 'U'; put_u32(108, 0x1000);
    const auto manual_dds_path = root / L"unpack/data/texture/2k/custom_outline_0.dds";
    { std::ofstream output(manual_dds_path, std::ios::binary); output.write(reinterpret_cast<const char*>(manual_dds.data()), static_cast<std::streamsize>(manual_dds.size())); }
    workspace.refresh();
    const auto manual_texture = std::find_if(workspace.assets().begin(), workspace.assets().end(), [](const auto& asset) {
        return asset.kind == gbfr::AssetKind::new_texture && asset.input.filename() == L"custom_outline_0.dds";
    });
    if (manual_texture == workspace.assets().end() || manual_texture->subtype != "手动 DDS / 2k" ||
        manual_texture->output != root / L"build/data/texture/2k/custom_outline.texture" || !manual_texture->available) return 6;
    const auto asset_count = workspace.assets().size();
    workspace.refresh();
    if (workspace.assets().size() != asset_count) return 7;
    const auto manual_index = static_cast<std::size_t>(std::distance(workspace.assets().begin(), manual_texture));
    workspace.build_asset(manual_index);
    if (!fs::is_regular_file(root / L"build/data/texture/2k/custom_outline.texture") ||
        fs::file_size(root / L"build/data/texture/2k/custom_outline.texture") == 0) return 8;

    { std::ofstream output(json_path, std::ios::trunc); output << "{\"Magic\":20230727,\"Entries1\":[]}"; }
    bool rejected{};
    try { workspace.build_asset(0); } catch (...) { rejected = true; }
    if (!rejected || !gbfr::load_mmat_json(json_path).legacy_schema) return 5;

    fs::remove_all(root);
    return 0;
}
