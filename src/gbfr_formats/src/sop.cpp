#include <gbfr/formats/sop.hpp>

#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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
}
