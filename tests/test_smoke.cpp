#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    gbfr::Log::clear();
    gbfr::Log::write(gbfr::LogLevel::info, "smoke");
    if (gbfr::Log::snapshot().size() != 1) return 1;

    const fs::path root = fs::temp_directory_path() / L"gbfr_workspace_test";
    fs::remove_all(root);
    fs::create_directories(root / L"source");
    fs::create_directories(root / L"unpack");
    {
        std::ofstream(root / L"source/model.mmesh", std::ios::binary) << "baseline";
        std::ofstream(root / L"unpack/model.mmesh", std::ios::binary) << "baseline";
    }
    const auto hash = gbfr::sha256_file(root / L"source/model.mmesh");
    {
        std::ofstream json(root / L"workspace.json");
        json << "{\"Version\":1,\"CharacterId\":\"test\",\"ModelFiles\":[{"
                "\"Source\":\"source/model.mmesh\",\"SourceSha256\":\"" << hash << "\","
                "\"Input\":\"unpack/model.mmesh\",\"Output\":\"build/model.mmesh\","
                "\"BaselineSha256\":\"" << hash << "\",\"FileType\":\"mmesh\"}]}";
    }
    auto workspace = gbfr::Workspace::load(root / L"workspace.json");
    if (workspace.assets().size() != 1 || workspace.changed_count() != 0) return 2;
    std::ofstream(root / L"unpack/model.mmesh", std::ios::binary | std::ios::trunc) << "edited";
    workspace.refresh();
    if (workspace.changed_count() != 1) return 3;
    workspace.build_model(0);
    if (gbfr::sha256_file(root / L"build/model.mmesh") != gbfr::sha256_file(root / L"unpack/model.mmesh")) return 4;
    workspace.restore_model(0);
    if (workspace.changed_count() != 0) return 5;
    fs::remove_all(root);

    const fs::path integration = fs::path(GBFR_SOURCE_DIR) / L"explore_output/workspace.json";
    if (fs::is_regular_file(integration)) {
        const auto pl1400 = gbfr::Workspace::load(integration);
        if (pl1400.assets().size() != 188) return 6;
        const auto model_root = integration.parent_path() / L"unpack/data/model/pl/pl1400";
        const auto minfo = gbfr::load_minfo(model_root / L"pl1400.minfo");
        const auto skeleton = gbfr::load_skeleton(model_root / L"pl1400.skeleton");
        const auto mesh = gbfr::load_mmesh(integration.parent_path() / L"unpack/data/model_streaming/lod0/pl1400.mmesh", minfo);
        if (mesh.vertices.size() != minfo.vertex_count || mesh.indices.size() != minfo.index_count || skeleton.bones.empty()) return 7;
    }
    const auto corrupt = fs::temp_directory_path() / L"gbfr_corrupt.skeleton";
    std::ofstream(corrupt, std::ios::binary | std::ios::trunc) << "bad";
    try { (void)gbfr::load_skeleton(corrupt); return 8; } catch (const std::exception&) {}
    fs::remove(corrupt);
    return 0;
}
