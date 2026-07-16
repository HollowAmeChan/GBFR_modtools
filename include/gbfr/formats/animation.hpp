#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbfr {
enum class AnimationCurveKind : std::uint8_t { constant, linear, hermite };

struct AnimationKey {
    std::uint16_t frame{};
    float value{};
    float in_tangent{};
    float out_tangent{};
};

struct AnimationTrack {
    std::int16_t bone_id{-1};
    std::int8_t property{};
    std::int8_t compression{};
    std::uint16_t unknown{};
    AnimationCurveKind curve{AnimationCurveKind::constant};
    std::vector<AnimationKey> keys;

    float sample(float frame) const;
};

struct AnimationClip {
    std::uint32_t version{};
    std::uint16_t flags{};
    std::uint16_t frame_count{};
    std::uint32_t unknown{};
    std::string name;
    std::vector<AnimationTrack> tracks;

    float duration_seconds(float frames_per_second = 60.0f) const;
};

AnimationClip load_mot(const std::filesystem::path& path);
}
