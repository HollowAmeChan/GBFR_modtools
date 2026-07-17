#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <stdexcept>

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

    nlohmann::json document;
    input >> document;
    const auto entries = document.find("Entries1");
    if (entries == document.end() || !entries->is_array()) throw std::runtime_error("mmat JSON has no Entries1 array");

    MaterialAsset result;
    result.entries.reserve(entries->size());
    for (const auto& source_entry : *entries) {
        MaterialEntry entry;
        bool alpha_enabled{};
        const auto parameters = source_entry.find("A1");
        if (parameters != source_entry.end() && parameters->is_array()) {
            for (const auto& parameter : *parameters) {
                const auto id = parameter.value("ID", 0u);
                const bool enabled = parameter.value("ID2", 0u) != 0u;
                if (id == enable_alpha_shader_parameter_id) alpha_enabled = enabled;
            }
        }
        const auto textures = source_entry.find("A2");
        if (textures != source_entry.end() && textures->is_array()) {
            for (const auto& texture : *textures) {
                const auto name = texture.value("Name", std::string{});
                if (name.empty() || is_color_variant_texture(name)) continue;
                switch(texture.value("ID", 0u)) {
                case albedo_texture_slot_id: entry.albedo_name=name;break;
                case eye_highlight_texture_slot_id: entry.eye_highlight_name=name;break;
                case eye_mask_texture_slot_id: entry.eye_mask_name=name;break;
                case eye_iris_texture_slot_id: entry.eye_iris_name=name;break;
                case eye_conjunctiva_texture_slot_id: entry.eye_conjunctiva_name=name;break;
                case face_mask2_texture_slot_id: entry.alpha_mask_name=name;break;
                default:break;
                }
            }
        }
        // A7 is shader_sub_type, not a generic render group. Alpha-enabled face
        // entries are either depth-writing clips or the masked surface overlay.
        const bool subtype5 = alpha_enabled && !entry.albedo_name.empty() && source_entry.value("A7", 0u) == 5u;
        const auto constant_buffers = source_entry.find("A3");
        if (subtype5 && constant_buffers != source_entry.end() && constant_buffers->is_array()) {
            // Face overlay slot 6 is the eyebrow/eyelash layer. Other subtype-5
            // layers (notably the mouth interior) use albedo alpha without msk2.B.
            for (const auto& index : *constant_buffers) {
                if (index.is_number_unsigned() && index.get<std::uint32_t>() == 6u) {
                    entry.alpha_masked = !entry.alpha_mask_name.empty();
                    break;
                }
            }
        }
        entry.alpha_blended = entry.alpha_masked;
        entry.alpha_clipped = alpha_enabled && !entry.albedo_name.empty() && !entry.alpha_blended;
        result.entries.push_back(std::move(entry));
    }
    return result;
}
}
