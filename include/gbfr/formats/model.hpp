#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbfr {
struct Vec2 { float x{}, y{}; };
struct Vec3 { float x{}, y{}, z{}; };
struct Vec4 { float x{}, y{}, z{}, w{}; };
struct Bone { std::string name; std::uint16_t parent{0xffff}; Vec3 position{}; Vec4 rotation{0,0,0,1}; Vec3 scale{1,1,1}; Vec3 world_position{}; };
struct SkeletonAsset { std::uint32_t magic{}; std::vector<Bone> bones; };
struct MeshBufferLocator { std::uint64_t offset{}, size{}; };
struct LodChunk { std::uint32_t offset{}, count{}; std::uint8_t submesh{}, material{}; };
struct ModelInfoAsset {
    std::uint32_t magic{}, vertex_count{}, index_count{};
    std::uint8_t buffer_types{};
    std::vector<MeshBufferLocator> buffers;
    std::vector<LodChunk> chunks;
    std::vector<std::string> submesh_names;
    std::vector<std::uint32_t> materials;
    std::vector<std::uint16_t> bones_to_weight_indices;
};
struct Vertex { Vec3 position{}, normal{}, tangent{}; Vec2 uv{}; std::array<std::uint16_t,4> joints{}; std::array<float,4> weights{}; };
struct MeshAsset { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; std::vector<LodChunk> chunks; };

SkeletonAsset load_skeleton(const std::filesystem::path& path);
ModelInfoAsset load_minfo(const std::filesystem::path& path);
MeshAsset load_mmesh(const std::filesystem::path& path, const ModelInfoAsset& info);
}
