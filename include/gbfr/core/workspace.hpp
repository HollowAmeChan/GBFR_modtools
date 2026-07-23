#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gbfr {
bool natural_less_case_insensitive(std::wstring_view left, std::wstring_view right);

enum class AssetKind { texture, ui_image, material, cloth, model, new_texture, granite_texture };

struct WorkspaceAsset {
    AssetKind kind{};
    std::string subtype;
    std::filesystem::path input;
    std::filesystem::path source;
    std::filesystem::path output;
    std::string baseline_sha256;
    std::string source_sha256;
    bool available{};
    bool changed{};
    std::uint32_t texture_id{};
    std::vector<std::pair<std::filesystem::path, std::string>> monitored_inputs;
    std::vector<std::pair<unsigned, std::filesystem::path>> wtb_slots;
    std::string granite_hash;
    std::filesystem::path granite_gts;
};

class Workspace {
public:
    static Workspace load(const std::filesystem::path& manifest_or_json);
    void refresh();
    void build_model(std::size_t index);
    void restore_model(std::size_t index);
    void build_asset(std::size_t index);
    void restore_asset(std::size_t index);
    std::size_t material_a4_count(std::size_t index) const;
    std::size_t remove_material_a4(std::size_t index);
    const std::filesystem::path& root() const noexcept { return root_; }
    const std::string& character_id() const noexcept { return character_id_; }
    const std::vector<WorkspaceAsset>& assets() const noexcept { return assets_; }
    std::size_t missing_count() const noexcept;
    std::size_t changed_count() const noexcept;

private:
    std::filesystem::path resolve(const std::string& relative) const;
    void restore_granite_texture(std::size_t index);
    std::filesystem::path root_;
    std::filesystem::path game_data_root_;
    std::string character_id_;
    std::vector<WorkspaceAsset> assets_;
};

std::string sha256_file(const std::filesystem::path& path);
const char* asset_kind_name(AssetKind kind) noexcept;
}
