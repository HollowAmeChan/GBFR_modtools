#pragma once

#include <gbfr/core/workspace.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace gbfr::editor {

class TextureGallery {
public:
    using SelectCallback = std::function<void(std::size_t, const std::filesystem::path&, bool)>;

    void clear();
    void draw(const Workspace& workspace, PreviewRenderer& renderer,
              std::optional<std::size_t> selected_asset,
              const SelectCallback& on_select);

private:
    struct CacheEntry {
        TexturePreviewResource texture;
        bool failed{};
    };

    CacheEntry* find_or_load(PreviewRenderer& renderer,
                             const std::filesystem::path& path);

    std::array<char, 256> search_{};
    std::unordered_map<std::filesystem::path, CacheEntry> cache_;
    float thumbnail_size_{160.0f};
    int kind_filter_{};
};

}
