#include <gbfr/formats/animation.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
class BinaryView {
public:
    explicit BinaryView(const fs::path& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open MOT file");
        const auto size = input.tellg();
        if (size < 0) throw std::runtime_error("Cannot get MOT file size");
        bytes_.resize(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes_.data()), size);
    }

    template<class T> T read(std::size_t offset, const char* label) const {
        require(offset, sizeof(T), label);
        T value{};
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return value;
    }

    std::uint16_t read_be_u16(std::size_t offset, const char* label) const {
        require(offset, 2, label);
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes_[offset]) << 8) |
                                          static_cast<std::uint16_t>(bytes_[offset + 1]));
    }

    std::string fixed_string(std::size_t offset, std::size_t length) const {
        require(offset, length, "MOT name");
        const auto* begin = reinterpret_cast<const char*>(bytes_.data() + offset);
        const auto* end = std::find(begin, begin + length, '\0');
        return {begin, end};
    }

    void require(std::size_t offset, std::size_t length, const char* label) const {
        if (offset > bytes_.size() || length > bytes_.size() - offset)
            throw std::runtime_error(std::string(label) + " outside MOT file at offset " + std::to_string(offset));
    }

private:
    std::vector<std::byte> bytes_;
};

float pg_half_to_float(std::uint16_t value) {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
    const std::uint32_t source_exponent = (value & 0x7e00u) >> 9;
    const std::uint32_t source_mantissa = value & 0x01ffu;
    if (!source_exponent && !source_mantissa) return std::bit_cast<float>(sign);
    if (source_exponent == 63) {
        const auto result = sign | 0x7f800000u | (source_mantissa << 14);
        return std::bit_cast<float>(result);
    }
    const auto exponent = source_exponent + 80u;
    return std::bit_cast<float>(sign | (exponent << 23) | (source_mantissa << 14));
}

void validate_keys(const gbfr::AnimationTrack& track) {
    for (std::size_t i = 0; i < track.keys.size(); ++i) {
        const auto& key = track.keys[i];
        if (!std::isfinite(key.value) || !std::isfinite(key.in_tangent) || !std::isfinite(key.out_tangent))
            throw std::runtime_error("Non-finite MOT key value");
        if (i && key.frame < track.keys[i - 1].frame)
            throw std::runtime_error("MOT keyframes are not ordered");
    }
}
}

namespace gbfr {
float AnimationTrack::sample(float frame) const {
    if (keys.empty()) return 0.0f;
    if (curve == AnimationCurveKind::constant || frame <= keys.front().frame) return keys.front().value;
    if (frame >= keys.back().frame) return keys.back().value;
    const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
        [](float value, const AnimationKey& key) { return value < key.frame; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const float span = static_cast<float>(b.frame - a.frame);
    if (span <= 0.0f) return b.value;
    const float t = (frame - a.frame) / span;
    if (curve == AnimationCurveKind::linear) return a.value + (b.value - a.value) * t;
    const float t2 = t * t, t3 = t2 * t;
    return (2.0f*t3 - 3.0f*t2 + 1.0f)*a.value +
           (t3 - 2.0f*t2 + t)*a.out_tangent +
           (-2.0f*t3 + 3.0f*t2)*b.value +
           (t3 - t2)*b.in_tangent;
}

float AnimationClip::duration_seconds(float frames_per_second) const {
    if (frames_per_second <= 0.0f || frame_count <= 1) return 0.0f;
    return static_cast<float>(frame_count - 1) / frames_per_second;
}

AnimationClip load_mot(const fs::path& path) {
    BinaryView view(path);
    if (view.read<std::uint32_t>(0, "MOT magic") != 0x00746f6du)
        throw std::runtime_error("Invalid MOT magic");
    AnimationClip clip;
    clip.version = view.read<std::uint32_t>(4, "MOT version");
    clip.flags = view.read<std::uint16_t>(8, "MOT flags");
    const auto signed_frames = view.read<std::int16_t>(10, "MOT frame count");
    if (signed_frames <= 0) throw std::runtime_error("Invalid MOT frame count");
    clip.frame_count = static_cast<std::uint16_t>(signed_frames);
    const auto records_offset = view.read<std::uint32_t>(12, "MOT records offset");
    const auto records_count = view.read<std::uint32_t>(16, "MOT records count");
    clip.unknown = view.read<std::uint32_t>(20, "MOT unknown");
    clip.name = view.fixed_string(24, 20);
    if (records_count > 1'000'000u) throw std::runtime_error("Unreasonable MOT record count");
    view.require(records_offset, static_cast<std::size_t>(records_count) * 12, "MOT record table");
    clip.tracks.reserve(records_count);

    for (std::uint32_t index = 0; index < records_count; ++index) {
        const std::size_t record_offset = records_offset + static_cast<std::size_t>(index) * 12;
        AnimationTrack track;
        track.bone_id = view.read<std::int16_t>(record_offset, "MOT bone id");
        track.property = view.read<std::int8_t>(record_offset + 2, "MOT property");
        track.compression = view.read<std::int8_t>(record_offset + 3, "MOT compression");
        const auto key_count = view.read<std::int16_t>(record_offset + 4, "MOT key count");
        track.unknown = view.read<std::uint16_t>(record_offset + 6, "MOT track unknown");
        if (key_count < 0) throw std::runtime_error("Negative MOT key count");

        if (track.compression == 0 || track.compression == -1) {
            track.curve = AnimationCurveKind::constant;
            track.keys.push_back({0, view.read<float>(record_offset + 8, "MOT constant")});
            validate_keys(track);
            clip.tracks.push_back(std::move(track));
            continue;
        }

        const auto relative_data = view.read<std::uint32_t>(record_offset + 8, "MOT data offset");
        const std::size_t data_offset = record_offset + relative_data;
        const auto count = static_cast<std::size_t>(key_count);
        auto add_linear = [&](std::uint16_t frame, float value) { track.keys.push_back({frame, value}); };
        auto add_hermite = [&](std::uint16_t frame, float value, float in_tangent, float out_tangent) {
            track.keys.push_back({frame, value, in_tangent, out_tangent});
        };

        switch (track.compression) {
        case 1:
            track.curve = AnimationCurveKind::linear;
            view.require(data_offset, count * 4, "MOT float curve");
            for (std::size_t i = 0; i < count; ++i) add_linear(static_cast<std::uint16_t>(i), view.read<float>(data_offset + i*4, "MOT float key"));
            break;
        case 2: {
            track.curve = AnimationCurveKind::linear;
            view.require(data_offset, 8 + count*2, "MOT u16 curve");
            const float base=view.read<float>(data_offset,"MOT u16 base"), step=view.read<float>(data_offset+4,"MOT u16 step");
            for (std::size_t i=0;i<count;++i) add_linear(static_cast<std::uint16_t>(i),base+step*view.read<std::uint16_t>(data_offset+8+i*2,"MOT u16 key"));
            break;
        }
        case 3: {
            track.curve = AnimationCurveKind::linear;
            view.require(data_offset, 4 + count, "MOT u8 curve");
            const float base=pg_half_to_float(view.read<std::uint16_t>(data_offset,"MOT u8 base"));
            const float step=pg_half_to_float(view.read<std::uint16_t>(data_offset+2,"MOT u8 step"));
            for (std::size_t i=0;i<count;++i) add_linear(static_cast<std::uint16_t>(i),base+step*view.read<std::uint8_t>(data_offset+4+i,"MOT u8 key"));
            break;
        }
        case 4:
            track.curve = AnimationCurveKind::hermite;
            view.require(data_offset, count*16, "MOT spline curve");
            for(std::size_t i=0;i<count;++i){const auto p=data_offset+i*16;add_hermite(view.read<std::uint16_t>(p,"MOT spline frame"),view.read<float>(p+4,"MOT spline value"),view.read<float>(p+8,"MOT spline in"),view.read<float>(p+12,"MOT spline out"));}
            break;
        case 5: {
            track.curve = AnimationCurveKind::hermite;
            view.require(data_offset, 24 + count*8, "MOT u16 spline curve");
            float base[6]{};for(std::size_t i=0;i<6;++i)base[i]=view.read<float>(data_offset+i*4,"MOT u16 spline base");
            for(std::size_t i=0;i<count;++i){const auto p=data_offset+24+i*8;add_hermite(view.read<std::uint16_t>(p,"MOT u16 spline frame"),base[0]+base[1]*view.read<std::uint16_t>(p+2,"MOT u16 spline value"),base[2]+base[3]*view.read<std::uint16_t>(p+4,"MOT u16 spline in"),base[4]+base[5]*view.read<std::uint16_t>(p+6,"MOT u16 spline out"));}
            break;
        }
        case 6:
        case 7: {
            track.curve = AnimationCurveKind::hermite;
            view.require(data_offset, 12 + count*4, "MOT u8 spline curve");
            float base[6]{};for(std::size_t i=0;i<6;++i)base[i]=pg_half_to_float(view.read<std::uint16_t>(data_offset+i*2,"MOT u8 spline base"));
            std::uint32_t absolute_frame{};
            for(std::size_t i=0;i<count;++i){const auto p=data_offset+12+i*4;const auto encoded=view.read<std::uint8_t>(p,"MOT u8 spline frame");absolute_frame=track.compression==7?absolute_frame+encoded:encoded;if(absolute_frame>std::numeric_limits<std::uint16_t>::max())throw std::runtime_error("MOT frame overflow");add_hermite(static_cast<std::uint16_t>(absolute_frame),base[0]+base[1]*view.read<std::uint8_t>(p+1,"MOT u8 spline value"),base[2]+base[3]*view.read<std::uint8_t>(p+2,"MOT u8 spline in"),base[4]+base[5]*view.read<std::uint8_t>(p+3,"MOT u8 spline out"));}
            break;
        }
        case 8: {
            track.curve = AnimationCurveKind::hermite;
            view.require(data_offset, 12 + count*5, "MOT long spline curve");
            float base[6]{};for(std::size_t i=0;i<6;++i)base[i]=pg_half_to_float(view.read<std::uint16_t>(data_offset+i*2,"MOT long spline base"));
            for(std::size_t i=0;i<count;++i){const auto p=data_offset+12+i*5;add_hermite(view.read_be_u16(p,"MOT long spline frame"),base[0]+base[1]*view.read<std::uint8_t>(p+2,"MOT long spline value"),base[2]+base[3]*view.read<std::uint8_t>(p+3,"MOT long spline in"),base[4]+base[5]*view.read<std::uint8_t>(p+4,"MOT long spline out"));}
            break;
        }
        default:
            throw std::runtime_error("Unsupported MOT compression type " + std::to_string(track.compression));
        }
        validate_keys(track);
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}
}
