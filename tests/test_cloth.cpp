#include <gbfr/core/workspace.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace {
bool is_bxm(const fs::path& path) {
    std::array<unsigned char, 16> header{};
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    return stream.gcount() == static_cast<std::streamsize>(header.size()) &&
        ((header[0] == 'B' && header[1] == 'X' && header[2] == 'M' && header[3] == 0) ||
         (header[0] == 'X' && header[1] == 'M' && header[2] == 'L' && header[3] == 0));
}
}

int main() {
    const auto root = fs::current_path() / L".gbfr_cloth_test_temp";
    fs::remove_all(root);
    fs::create_directories(root / L"unpack");

    const std::array<std::pair<std::string, std::string>, 2> fixtures{{
        {"clp", R"xml(<CLOTH>
  <CLOTH_HEADER>
    <dataVersion_>2</dataVersion_>
    <id_>3</id_>
    <useCollisionFlags_>5</useCollisionFlags_>
  </CLOTH_HEADER>
  <CLOTH_WK_LIST>
    <CLOTH_WK>
      <no>3141</no>
      <noUp>4095</noUp>
      <noDown>4095</noDown>
      <offset>0.100000 0.000000 0.000000 1.000000</offset>
    </CLOTH_WK>
  </CLOTH_WK_LIST>
</CLOTH>
)xml"},
        {"clh", R"xml(<CLOTH_AT>
  <ClothCollision_LIST>
    <ClothCollision>
      <id_>0</id_>
      <p1>5</p1>
      <p2>5</p2>
      <weight>0.000000</weight>
      <radius>0.125000</radius>
      <offset1>0.000000 0.100000 0.000000 1.000000</offset1>
      <offset2>0.000000 0.100000 0.000000 1.000000</offset2>
      <capsule>-1</capsule>
    </ClothCollision>
  </ClothCollision_LIST>
</CLOTH_AT>
)xml"}
    }};

    nlohmann::json records = nlohmann::json::array();
    for (const auto& [category, xml] : fixtures) {
        const auto input = root / L"unpack" / fs::u8path(category + ".bxm.xml");
        { std::ofstream stream(input, std::ios::binary); stream << xml; }
        records.push_back({
            {"Category", category},
            {"Xml", "unpack/" + category + ".bxm.xml"},
            {"Source", "source/" + category + ".bxm"},
            {"Output", "build/" + category + ".bxm"},
            {"BaselineSha256", gbfr::sha256_file(input)},
            {"SourceSha256", ""}
        });
    }
    {
        std::ofstream manifest(root / L"workspace.json");
        manifest << nlohmann::json{
            {"Version", 1}, {"CharacterId", "cloth-test"}, {"ClothFiles", records}
        }.dump(2);
    }

    auto workspace = gbfr::Workspace::load(root / L"workspace.json");
    if (workspace.assets().size() != fixtures.size()) return 1;
    for (std::size_t index = 0; index < workspace.assets().size(); ++index) {
        workspace.build_asset(index);
        const auto& asset = workspace.assets()[index];
        if (asset.kind != gbfr::AssetKind::cloth) return 2;
        if (!is_bxm(asset.output)) return 5;
    }

    const auto clp = std::find_if(workspace.assets().begin(), workspace.assets().end(),
        [](const auto& asset) { return asset.subtype == "clp"; });
    if (clp == workspace.assets().end()) return 3;
    const auto clp_index = static_cast<std::size_t>(std::distance(workspace.assets().begin(), clp));
    const auto output_hash = gbfr::sha256_file(clp->output);
    { std::ofstream stream(clp->input, std::ios::trunc); stream << "<CLOTH>"; }
    bool rejected{};
    try { workspace.build_asset(clp_index); } catch (...) { rejected = true; }
    if (!rejected || gbfr::sha256_file(clp->output) != output_hash) return 4;

    fs::remove_all(root);
    return 0;
}
