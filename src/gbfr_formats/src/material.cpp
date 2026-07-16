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
        const auto textures = source_entry.find("A2");
        if (textures != source_entry.end() && textures->is_array()) {
            for (const auto& texture : *textures) {
                if (texture.value("ID", 0u) != albedo_texture_slot_id) continue;
                const auto name = texture.value("Name", std::string{});
                if (name.empty() || is_color_variant_texture(name)) continue;
                entry.albedo_name = name;
                break;
            }
        }
        result.entries.push_back(std::move(entry));
    }
    return result;
}
}
