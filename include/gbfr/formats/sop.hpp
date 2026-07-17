#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gbfr {
inline constexpr std::uint32_t sop_version_20200309 = 0x20200309u;
inline constexpr std::uint32_t sop_target_bone_property = 0x5B0292DDu;
inline constexpr std::uint32_t sop_source_bone_property = 0x1B5B0525u;
inline constexpr std::uint32_t sop_swing_twist_operation = 0xB1FFF4E6u;
inline constexpr std::uint32_t sop_twist_operation = 0x61D80537u;
inline constexpr std::uint32_t sop_axis_x_property = 0x2E933545u;
inline constexpr std::uint32_t sop_axis_y_property = 0x599405D3u;
inline constexpr std::uint32_t sop_axis_z_property = 0xC09D5469u;
inline constexpr std::uint32_t sop_twist_rate_property = 0x72B10DA8u;
inline constexpr std::uint32_t sop_swing_rate_property = 0x9BE488F1u;
inline constexpr std::uint32_t sop_offset_x_property = 0x597EA425u;
inline constexpr std::uint32_t sop_offset_y_property = 0x2E7994B3u;
inline constexpr std::uint32_t sop_offset_z_property = 0xB770C509u;

enum class SopPropertyType : std::uint32_t { integer = 0, floating = 1 };

struct SopProperty {
    std::uint32_t hash{};
    SopPropertyType type{};
    std::uint32_t raw_value{};

    std::uint32_t integer() const noexcept { return raw_value; }
    float floating() const noexcept;
};

struct SopOperation {
    std::uint32_t type_hash{};
    std::uint32_t metadata{};
    std::uint32_t target_bone{};
    std::uint32_t source_bone{};
    std::vector<SopProperty> properties;

    const SopProperty* find(std::uint32_t hash) const noexcept;
};

struct SopAsset {
    std::uint32_t version{};
    std::vector<SopOperation> operations;
};

SopAsset load_sop(const std::filesystem::path& path);
}
