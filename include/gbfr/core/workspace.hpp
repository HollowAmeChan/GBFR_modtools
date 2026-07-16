#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace gbfr {
enum class AssetKind { texture, material, cloth, model, new_texture };

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
    std::vector<std::pair<std::filesystem::path, std::string>> monitored_inputs;
};

class Workspace {
public:
    static Workspace load(const std::filesystem::path& manifest_or_json);
    void refresh();
    void build_model(std::size_t index);
    void restore_model(std::size_t index);
    const std::filesystem::path& root() const noexcept { return root_; }
    const std::string& character_id() const noexcept { return character_id_; }
    const std::vector<WorkspaceAsset>& assets() const noexcept { return assets_; }
    std::size_t missing_count() const noexcept;
    std::size_t changed_count() const noexcept;

private:
    std::filesystem::path resolve(const std::string& relative) const;
    std::filesystem::path root_;
    std::string character_id_;
    std::vector<WorkspaceAsset> assets_;
};

std::string sha256_file(const std::filesystem::path& path);
const char* asset_kind_name(AssetKind kind) noexcept;
}
