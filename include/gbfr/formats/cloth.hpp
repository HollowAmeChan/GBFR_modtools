#pragma once
#include <gbfr/formats/model.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gbfr {
struct ClothCollision { int id{}, p1{}, p2{}, capsule{-1}; float weight{}, radius{}; Vec4 offset1{}, offset2{}; bool battle_disabled{}, idle_disabled{}; };
struct ClothNode { int bone{}, up{}, down{}, side{}, poly{}, fix{}; float rotation_limit{}, friction{}, weight{}, thickness{}, wind_area{}; Vec4 offset{}; };
struct ClhAsset { std::vector<ClothCollision> collisions; };
struct ClpAsset { int id{}, collision_flags{}; std::vector<ClothNode> nodes; };
struct ClothSequenceEvent {
    std::uint32_t layer_flags{0xffffffffu};
    float start_time{}, scale_rate{}, floor_offset{};
    int sequence_flag{}, file_id{}, fade_frames{}, floor_fade_frames{};
    std::array<int,6> collision_ids{-1,-1,-1,-1,-1,-1};
};
struct ClothSequenceAsset { std::vector<ClothSequenceEvent> events; };

ClhAsset load_clh(const std::filesystem::path& path);
ClpAsset load_clp(const std::filesystem::path& path);
ClothSequenceAsset load_cloth_sequence(const std::filesystem::path& path);
void save_clh_collision(const std::filesystem::path& path, const ClothCollision& collision);
}
