#pragma once

#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace gbfr { class PreviewRenderer; }

namespace gbfr::editor {
class TextureGallery;

class MaterialInspector {
public:
    void set_asset(MaterialAsset asset, std::filesystem::path path, std::size_t selected_material = 0);
    void clear();
    void draw(PreviewRenderer& renderer, TextureGallery& texture_gallery);
    bool consume_file_changed() noexcept;
    bool save_changes();
    void reload_if_open(const std::filesystem::path& path);

private:
    bool load_document();
    bool save_document();
    bool discard_changes();

    MaterialAsset asset_;
    nlohmann::json document_;
    std::filesystem::path path_;
    std::filesystem::path unpack_root_;
    int selected_material_{};
    bool dirty_{};
    bool file_changed_{};
    std::string edit_status_;
};
}
