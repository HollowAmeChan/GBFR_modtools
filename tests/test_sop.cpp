#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/sop.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
bool near(float left, float right) {
    return std::abs(left - right) < 0.00001f;
}
}

int wmain(int argc, wchar_t** argv) {
    const auto root = fs::temp_directory_path() / L"gbfr_sop_workflow_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"source/data/model/pl/pl9999");

    gbfr::SopAsset original;
    original.version = gbfr::sop_version_20200309;
    original.operations.push_back(gbfr::make_swing_twist_operation(0xa50u, 0x00eu, gbfr::SopAxis::y, 0.5f, 0.0f));
    const auto source = root / L"source/data/model/pl/pl9999/pl9999.sop";
    gbfr::save_sop(source, original);

    const auto roundtrip = gbfr::load_sop(source);
    if (roundtrip.version != gbfr::sop_version_20200309 || roundtrip.operations.size() != 1) return 1;
    const auto& operation = roundtrip.operations.front();
    if (operation.type_hash != gbfr::sop_swing_twist_operation || operation.metadata != 0x00090101u ||
        operation.target_bone != 0xa50u || operation.source_bone != 0x00eu || operation.properties.size() != 9) return 2;
    if (!near(operation.find(gbfr::sop_axis_x_property)->floating(), 0.0f) ||
        !near(operation.find(gbfr::sop_axis_y_property)->floating(), 1.0f) ||
        !near(operation.find(gbfr::sop_axis_z_property)->floating(), 0.0f) ||
        !near(operation.find(gbfr::sop_swing_rate_property)->floating(), 0.5f) ||
        !near(operation.find(gbfr::sop_twist_rate_property)->floating(), 0.0f)) return 3;

    nlohmann::json manifest = {
        {"Version", 1},
        {"CharacterId", "pl9999"},
    };
    {
        std::ofstream output(root / L"workspace.json");
        output << manifest.dump(2);
    }

    auto workspace = gbfr::Workspace::load(root);
    std::size_t sop_index = workspace.assets().size();
    for (std::size_t index = 0; index < workspace.assets().size(); ++index) {
        const auto& asset = workspace.assets()[index];
        if (asset.kind == gbfr::AssetKind::model && asset.subtype == "sop") {
            sop_index = index;
            break;
        }
    }
    if (sop_index == workspace.assets().size()) return 4;
    const auto input = root / L"unpack/data/model/pl/pl9999/pl9999.sop";
    const auto build = root / L"build/data/model/pl/pl9999/pl9999.sop";
    if (!fs::is_regular_file(input) || workspace.assets()[sop_index].changed) return 5;

    auto edited = gbfr::load_sop(input);
    edited.operations.push_back(gbfr::make_swing_twist_operation(0xa53u, 0x012u, gbfr::SopAxis::y, 0.6f, 0.0f));
    gbfr::save_sop(input, edited);
    workspace.refresh();
    if (!workspace.assets()[sop_index].changed) return 6;
    workspace.build_asset(sop_index);
    if (!fs::is_regular_file(build) || gbfr::load_sop(build).operations.size() != 2) return 7;

    workspace.restore_asset(sop_index);
    if (workspace.assets()[sop_index].changed || gbfr::load_sop(input).operations.size() != 1) return 8;

    if (argc > 1) {
        const fs::path sample = argv[1];
        const auto sample_asset = gbfr::load_sop(sample);
        const auto rebuilt = root / L"sample_roundtrip.sop";
        gbfr::save_sop(rebuilt, sample_asset);
        if (gbfr::sha256_file(sample) != gbfr::sha256_file(rebuilt)) return 9;
    }

    fs::remove_all(root, ec);
    return 0;
}
