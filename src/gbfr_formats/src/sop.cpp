#include <gbfr/formats/sop.hpp>

#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;
namespace {
class BinaryView {
public:
    explicit BinaryView(const fs::path& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open SOP file");
        const auto size = input.tellg();
        if (size < 0) throw std::runtime_error("Cannot get SOP file size");
        bytes_.resize(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes_.data()), size);
    }

    template<class T> T read(std::size_t offset, const char* label) const {
        if (offset > bytes_.size() || sizeof(T) > bytes_.size() - offset)
            throw std::runtime_error(std::string(label) + " outside SOP file at offset " + std::to_string(offset));
        T value{};
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return value;
    }

    bool starts_with(const char* magic, std::size_t length) const {
        return bytes_.size() >= length && std::memcmp(bytes_.data(), magic, length) == 0;
    }

    std::size_t size() const noexcept { return bytes_.size(); }

private:
    std::vector<std::byte> bytes_;
};
}

namespace gbfr {
float SopProperty::floating() const noexcept {
    return std::bit_cast<float>(raw_value);
}

const SopProperty* SopOperation::find(std::uint32_t hash) const noexcept {
    for (const auto& property : properties) if (property.hash == hash) return &property;
    return nullptr;
}

SopProperty* SopOperation::find(std::uint32_t hash) noexcept {
    for (auto& property : properties) if (property.hash == hash) return &property;
    return nullptr;
}

SopOperation make_swing_twist_operation(std::uint32_t target_bone,
                                        std::uint32_t source_bone,
                                        SopAxis axis,
                                        float swing_rate,
                                        float twist_rate) {
    const float axis_x = axis == SopAxis::x ? 1.0f : 0.0f;
    const float axis_y = axis == SopAxis::y ? 1.0f : 0.0f;
    const float axis_z = axis == SopAxis::z ? 1.0f : 0.0f;
    const auto floating = [](std::uint32_t hash, float value) {
        return SopProperty{hash, SopPropertyType::floating, std::bit_cast<std::uint32_t>(value)};
    };
    SopOperation operation;
    operation.type_hash = sop_swing_twist_operation;
    operation.metadata = 0x00090101u;
    operation.target_bone = target_bone;
    operation.source_bone = source_bone;
    operation.properties = {
        {sop_common_zero_property, SopPropertyType::integer, 0u},
        floating(sop_axis_x_property, axis_x),
        floating(sop_axis_y_property, axis_y),
        floating(sop_axis_z_property, axis_z),
        floating(sop_twist_rate_property, twist_rate),
        floating(sop_swing_rate_property, swing_rate),
        floating(sop_offset_x_property, 0.0f),
        floating(sop_offset_y_property, 0.0f),
        floating(sop_offset_z_property, 0.0f),
    };
    return operation;
}

SopAsset load_sop(const fs::path& path) {
    BinaryView view(path);
    if (!view.starts_with("sop\0", 4)) throw std::runtime_error("Invalid SOP magic");

    SopAsset result;
    result.version = view.read<std::uint32_t>(4, "SOP version");
    if (result.version != sop_version_20200309) throw std::runtime_error("Unsupported SOP version");
    const auto operation_count = view.read<std::uint32_t>(8, "SOP operation count");
    if (operation_count > 100'000u) throw std::runtime_error("Unreasonable SOP operation count");
    const std::size_t table_end = 12 + static_cast<std::size_t>(operation_count) * 4;
    if (table_end > view.size()) throw std::runtime_error("SOP offset table outside file");

    std::vector<std::uint32_t> offsets(operation_count);
    for (std::uint32_t index = 0; index < operation_count; ++index) {
        offsets[index] = view.read<std::uint32_t>(12 + static_cast<std::size_t>(index) * 4, "SOP operation offset");
        if (offsets[index] < table_end || offsets[index] >= view.size() ||
            (index && offsets[index] <= offsets[index - 1]))
            throw std::runtime_error("Invalid SOP operation offset");
    }

    result.operations.reserve(operation_count);
    for (std::uint32_t index = 0; index < operation_count; ++index) {
        const std::size_t begin = offsets[index];
        const std::size_t end = index + 1 < operation_count ? offsets[index + 1] : view.size();
        if (end < begin || end - begin < 24 || (end - begin - 24) % 12)
            throw std::runtime_error("Invalid SOP operation length");

        SopOperation operation;
        operation.type_hash = view.read<std::uint32_t>(begin, "SOP operation type");
        operation.metadata = view.read<std::uint32_t>(begin + 4, "SOP operation metadata");
        if (view.read<std::uint32_t>(begin + 8, "SOP target key") != sop_target_bone_property ||
            view.read<std::uint32_t>(begin + 16, "SOP source key") != sop_source_bone_property)
            throw std::runtime_error("SOP operation has invalid target/source fields");
        operation.target_bone = view.read<std::uint32_t>(begin + 12, "SOP target bone");
        operation.source_bone = view.read<std::uint32_t>(begin + 20, "SOP source bone");

        const auto property_count = static_cast<std::size_t>((operation.metadata >> 16) & 0xffu);
        if (property_count != (end - begin - 24) / 12)
            throw std::runtime_error("SOP property count does not match record length");
        operation.properties.reserve(property_count);
        for (std::size_t property_index = 0; property_index < property_count; ++property_index) {
            const auto offset = begin + 24 + property_index * 12;
            SopProperty property;
            property.hash = view.read<std::uint32_t>(offset, "SOP property hash");
            const auto type = view.read<std::uint32_t>(offset + 4, "SOP property type");
            if (type > static_cast<std::uint32_t>(SopPropertyType::floating))
                throw std::runtime_error("Unsupported SOP property type");
            property.type = static_cast<SopPropertyType>(type);
            property.raw_value = view.read<std::uint32_t>(offset + 8, "SOP property value");
            operation.properties.push_back(property);
        }
        result.operations.push_back(std::move(operation));
    }
    return result;
}

void save_sop(const fs::path& path, const SopAsset& asset) {
    if (asset.version != sop_version_20200309) throw std::runtime_error("Unsupported SOP version");
    if (asset.operations.size() > 100'000u) throw std::runtime_error("Unreasonable SOP operation count");

    std::size_t byte_count = 12 + asset.operations.size() * 4;
    for (const auto& operation : asset.operations) {
        if (operation.properties.size() > 0xffu) throw std::runtime_error("SOP operation has too many properties");
        byte_count += 24 + operation.properties.size() * 12;
    }
    if (byte_count > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("SOP file is too large");

    std::vector<std::byte> bytes(byte_count);
    const auto write_u32 = [&](std::size_t offset, std::uint32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    std::memcpy(bytes.data(), "sop\0", 4);
    write_u32(4, asset.version);
    write_u32(8, static_cast<std::uint32_t>(asset.operations.size()));

    std::size_t operation_offset = 12 + asset.operations.size() * 4;
    for (std::size_t index = 0; index < asset.operations.size(); ++index) {
        const auto& operation = asset.operations[index];
        write_u32(12 + index * 4, static_cast<std::uint32_t>(operation_offset));
        write_u32(operation_offset, operation.type_hash);
        const auto metadata = (operation.metadata & ~0x00ff0000u) |
            (static_cast<std::uint32_t>(operation.properties.size()) << 16);
        write_u32(operation_offset + 4, metadata);
        write_u32(operation_offset + 8, sop_target_bone_property);
        write_u32(operation_offset + 12, operation.target_bone);
        write_u32(operation_offset + 16, sop_source_bone_property);
        write_u32(operation_offset + 20, operation.source_bone);
        for (std::size_t property_index = 0; property_index < operation.properties.size(); ++property_index) {
            const auto& property = operation.properties[property_index];
            const auto type = static_cast<std::uint32_t>(property.type);
            if (type > static_cast<std::uint32_t>(SopPropertyType::floating))
                throw std::runtime_error("Unsupported SOP property type");
            const auto property_offset = operation_offset + 24 + property_index * 12;
            write_u32(property_offset, property.hash);
            write_u32(property_offset + 4, type);
            write_u32(property_offset + 8, property.raw_value);
        }
        operation_offset += 24 + operation.properties.size() * 12;
    }

    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot create temporary SOP file");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            fs::remove(temporary);
            throw std::runtime_error("Cannot write temporary SOP file");
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        fs::remove(temporary);
        throw std::runtime_error("Cannot replace SOP file (Win32 " + std::to_string(error) + ")");
    }
}
}
