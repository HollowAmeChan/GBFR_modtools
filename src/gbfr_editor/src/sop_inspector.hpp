#pragma once

#include <gbfr/formats/model.hpp>
#include <gbfr/formats/sop.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace gbfr::editor {
struct SopOperationDescription {
    std::string name;
    std::string category;
    std::string discovery;
    std::string discovery_label;
    std::string runtime;
    std::string runtime_label;
    std::string purpose;
};

class SopInspector {
public:
    bool load_catalog(const std::filesystem::path& path);
    void set_asset(SopAsset asset, std::filesystem::path path);
    void clear();
    void draw(const SkeletonAsset& skeleton,
              const std::unordered_map<std::string, std::string>& bone_names,
              int& selected_bone);

private:
    const SopOperationDescription* describe(std::uint32_t type_hash) const noexcept;

    std::unordered_map<std::uint32_t, SopOperationDescription> catalog_;
    SopAsset asset_;
    std::filesystem::path path_;
    std::array<char, 192> search_{};
    int status_filter_{};
    int selected_operation_{-1};
    bool selected_bone_only_{};
};
}
