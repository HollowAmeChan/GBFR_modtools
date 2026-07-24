#pragma once

#include <gbfr/formats/material.hpp>

#include <filesystem>

namespace gbfr { class PreviewRenderer; }

namespace gbfr::editor {
class TextureGallery;

class MaterialInspector {
public:
    void set_asset(MaterialAsset asset, std::filesystem::path path);
    void clear();
    void draw(PreviewRenderer& renderer, TextureGallery& texture_gallery);

private:
    MaterialAsset asset_;
    std::filesystem::path path_;
    std::filesystem::path unpack_root_;
    int selected_material_{};
};
}
