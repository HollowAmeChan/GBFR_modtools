#pragma once
#include <gbfr/formats/model.hpp>
#include <filesystem>
#include <vector>

namespace gbfr {
struct ClothCollision { int id{}, p1{}, p2{}, capsule{-1}; float weight{}, radius{}; Vec4 offset1{}, offset2{}; bool battle_disabled{}, idle_disabled{}; };
struct ClothNode { int bone{}, up{}, down{}, side{}, poly{}, fix{}; float rotation_limit{}, friction{}, weight{}, thickness{}, wind_area{}; Vec4 offset{}; };
struct ClhAsset { std::vector<ClothCollision> collisions; };
struct ClpAsset { std::vector<ClothNode> nodes; };

ClhAsset load_clh(const std::filesystem::path& path);
ClpAsset load_clp(const std::filesystem::path& path);
void save_clh_collision(const std::filesystem::path& path, const ClothCollision& collision);
}
