#pragma once

#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace gbfr { class PreviewRenderer; class Workspace; }

namespace gbfr::editor {
class TextureGallery;

class MaterialInspector {
public:
    void set_asset(MaterialAsset asset, std::filesystem::path path, std::size_t selected_material = 0);
    void clear();
    void draw(PreviewRenderer& renderer, TextureGallery& texture_gallery,
              const Workspace& workspace);
    bool consume_file_changed() noexcept;
    bool save_changes();
    void reload_if_open(const std::filesystem::path& path);
    void refresh_texture_names();

private:
    bool load_document();
    bool save_document();
    bool discard_changes();
    bool propagate_selected_material_settings();

    MaterialAsset asset_;
    nlohmann::json document_;
    std::filesystem::path path_;
    std::filesystem::path unpack_root_;
    int selected_material_{};
    bool dirty_{};
    bool file_changed_{};
    std::string edit_status_;
    std::vector<std::string> texture_names_;
    int new_texture_map_option_{6};
    std::array<char, 256> new_texture_name_{};
};
}
