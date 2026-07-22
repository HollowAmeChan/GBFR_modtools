#include <gbfr/core/workspace.hpp>

#include <nlohmann/json.hpp>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cstdint>

namespace fs = std::filesystem;

namespace {
using json = nlohmann::json;

std::string required_string(const json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string()) throw std::runtime_error(std::string("Missing string field: ") + key);
    return object[key].get<std::string>();
}

void copy_atomic(const fs::path& source, const fs::path& destination) {
    fs::create_directories(destination.parent_path());
    fs::path temporary = destination;
    temporary += L".tmp";
    std::error_code ec;
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("Copy failed: " + ec.message());
    fs::remove(destination, ec);
    ec.clear();
    fs::rename(temporary, destination, ec);
    if (ec) {
        fs::remove(temporary);
        throw std::runtime_error("Atomic replace failed: " + ec.message());
    }
}

std::uint32_t read_u32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("WTB table extends beyond file");
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write_u32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("WTB table extends beyond file");
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset + shift / 8] = static_cast<unsigned char>((value >> shift) & 0xff);
}

std::vector<unsigned char> read_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open WTB/DDS file: " + path.string());
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream), {});
}

void write_wtb_from_slots(const fs::path& source, const fs::path& destination,
                          const std::vector<std::pair<unsigned, fs::path>>& slots) {
    auto template_bytes = read_bytes(source);
    if (template_bytes.size() < 32 || template_bytes[0] != 'W' || template_bytes[1] != 'T' || template_bytes[2] != 'B' || template_bytes[3] != 0)
        throw std::runtime_error("Not a supported WTB source: " + source.string());
    const auto count = read_u32(template_bytes, 4);
    const auto offset_table = read_u32(template_bytes, 12);
    const auto size_table = read_u32(template_bytes, 16);
    if (!count || count > 4096 || offset_table + count * 4 > template_bytes.size() || size_table + count * 4 > template_bytes.size())
        throw std::runtime_error("Invalid WTB tables: " + source.string());

    std::vector<std::vector<unsigned char>> payloads(count);
    std::uint32_t first_offset = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = read_u32(template_bytes, offset_table + i * 4);
        const auto size = read_u32(template_bytes, size_table + i * 4);
        if (offset && size && (!first_offset || offset < first_offset)) first_offset = offset;
    }
    if (!first_offset) first_offset = 0x1000;
    for (const auto& [index, path] : slots) {
        if (index >= count) throw std::runtime_error("WTB slot index is out of range");
        payloads[index] = read_bytes(path);
        if (payloads[index].size() < 4 || payloads[index][0] != 'D' || payloads[index][1] != 'D' || payloads[index][2] != 'S' || payloads[index][3] != ' ')
            throw std::runtime_error("WTB slot is not a DDS payload: " + path.string());
    }

    std::vector<unsigned char> output(template_bytes.begin(), template_bytes.begin() + std::min<std::size_t>(first_offset, template_bytes.size()));
    output.resize(first_offset, 0);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (payloads[i].empty()) {
            write_u32(output, offset_table + i * 4, 0);
            write_u32(output, size_table + i * 4, 0);
            continue;
        }
        const auto aligned = (output.size() + 0xfff) & ~std::size_t(0xfff);
        output.resize(aligned, 0);
        write_u32(output, offset_table + i * 4, static_cast<std::uint32_t>(output.size()));
        write_u32(output, size_table + i * 4, static_cast<std::uint32_t>(payloads[i].size()));
        output.insert(output.end(), payloads[i].begin(), payloads[i].end());
    }
    fs::path temporary = destination;
    temporary += L".tmp";
    fs::create_directories(destination.parent_path());
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot create WTB output: " + destination.string());
    stream.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
    stream.close();
    std::error_code ec;
    fs::remove(destination, ec);
    fs::rename(temporary, destination, ec);
    if (ec) { fs::remove(temporary); throw std::runtime_error("Atomic WTB replace failed: " + ec.message()); }
}

void restore_wtb_slots(const fs::path& source, const std::vector<std::pair<unsigned, fs::path>>& slots) {
    const auto bytes = read_bytes(source);
    if (bytes.size() < 32 || bytes[0] != 'W' || bytes[1] != 'T' || bytes[2] != 'B' || bytes[3] != 0) throw std::runtime_error("Not a supported WTB source");
    const auto count = read_u32(bytes, 4), offset_table = read_u32(bytes, 12), size_table = read_u32(bytes, 16);
    if (!count || count > 4096 || offset_table + count * 4 > bytes.size() || size_table + count * 4 > bytes.size()) throw std::runtime_error("Invalid WTB tables");
    for (const auto& [index, path] : slots) {
        if (index >= count) throw std::runtime_error("WTB slot index is out of range");
        const auto offset = read_u32(bytes, offset_table + index * 4), size = read_u32(bytes, size_table + index * 4);
        if (!offset || !size || static_cast<std::size_t>(offset) + size > bytes.size()) throw std::runtime_error("Invalid WTB slot payload");
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot restore WTB slot: " + path.string());
        stream.write(reinterpret_cast<const char*>(bytes.data() + offset), size);
    }
}
}

namespace gbfr {
bool natural_less_case_insensitive(std::wstring_view left,std::wstring_view right) {
    std::size_t i{},j{};
    while(i<left.size()&&j<right.size()){
        if(std::iswdigit(left[i])&&std::iswdigit(right[j])){
            const auto left_begin=i,right_begin=j;
            while(i<left.size()&&std::iswdigit(left[i]))++i;
            while(j<right.size()&&std::iswdigit(right[j]))++j;
            auto left_number=left_begin,right_number=right_begin;
            while(left_number<i&&left[left_number]==L'0')++left_number;
            while(right_number<j&&right[right_number]==L'0')++right_number;
            const auto left_digits=i-left_number,right_digits=j-right_number;
            if(left_digits!=right_digits)return left_digits<right_digits;
            for(std::size_t digit=0;digit<left_digits;++digit)if(left[left_number+digit]!=right[right_number+digit])return left[left_number+digit]<right[right_number+digit];
            const auto left_width=i-left_begin,right_width=j-right_begin;
            if(left_width!=right_width)return left_width<right_width;
            continue;
        }
        const auto a=std::towlower(left[i]),b=std::towlower(right[j]);
        if(a!=b)return a<b;
        ++i;++j;
    }
    return i==left.size()&&j!=right.size();
}

std::string sha256_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open file for SHA-256");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD object_size{}, result_size{};
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    auto check = [](NTSTATUS status, const char* action) {
        if (status < 0) throw std::runtime_error(std::string("BCrypt failure: ") + action);
    };
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "open SHA-256");
    try {
        check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &result_size, 0), "get object size");
        object.resize(object_size);
        check(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0), "create hash");
        std::vector<char> buffer(64 * 1024);
        while (stream) {
            stream.read(buffer.data(), buffer.size());
            const auto count = static_cast<ULONG>(stream.gcount());
            if (count) check(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0), "hash data");
        }
        check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0), "finish hash");
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest) output << std::setw(2) << static_cast<int>(byte);
    return output.str();
}

Workspace Workspace::load(const fs::path& selected) {
    fs::path absolute = fs::absolute(selected);
    fs::path root = fs::is_directory(absolute) ? absolute : absolute.parent_path();
    fs::path workspace_path = fs::is_directory(absolute) ? root / L"workspace.json" :
        (absolute.filename() == L"workspace.json" ? absolute : root / L"workspace.json");
    std::ifstream stream(workspace_path);
    if (!stream) throw std::runtime_error("workspace.json not found");
    json document;
    stream >> document;
    if (document.value("Version", 0) != 1) throw std::runtime_error("Only workspace Version 1 is supported");

    Workspace result;
    result.root_ = fs::weakly_canonical(root);
    result.character_id_ = document.value("CharacterId", std::string{});
    auto append = [&](AssetKind kind, std::string subtype, const std::string& input, const std::string& source,
                      const std::string& output, const std::string& baseline, const std::string& source_hash) {
        WorkspaceAsset asset{kind, std::move(subtype), result.resolve(input), source.empty() ? fs::path{} : result.resolve(source),
                             result.resolve(output), baseline, source_hash};
        asset.monitored_inputs.emplace_back(asset.input, baseline);
        result.assets_.push_back(std::move(asset));
    };
    auto load_wtb_records = [&](const char* key, AssetKind kind, const char* default_subtype) {
    for (const auto& record : document.value(key, json::array())) {
        const auto slots=record.value("Slots",json::array()); if(slots.empty()) continue;
        const auto& first=slots.front(); append(kind,record.value("Category", std::string(default_subtype)) + " / " + std::to_string(slots.size()) + " 槽",required_string(first,"Path"),required_string(record,"Source"),required_string(record,"Output"),required_string(first,"BaselineSha256"),record.value("SourceSha256",""));
        auto& asset=result.assets_.back();
        for(std::size_t slot_index=0;slot_index<slots.size();++slot_index) {
            const auto& slot=slots[slot_index];
            asset.wtb_slots.emplace_back(slot.value("Index",0u),result.resolve(required_string(slot,"Path")));
            if(slot_index>0) asset.monitored_inputs.emplace_back(result.resolve(required_string(slot,"Path")),required_string(slot,"BaselineSha256"));
        }
    }};
    load_wtb_records("Textures", AssetKind::texture, "WTB");
    load_wtb_records("UIImages", AssetKind::ui_image, "UI-image");
    for (const auto& record : document.value("Materials", json::array()))
        append(AssetKind::material, "mmat", required_string(record, "Json"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), record.value("SourceSha256", ""));
    for (const auto& record : document.value("ClothFiles", json::array()))
        append(AssetKind::cloth, record.value("Category", "cloth"), required_string(record, "Xml"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), record.value("SourceSha256", ""));
    for (const auto& record : document.value("ModelFiles", json::array()))
        append(AssetKind::model, record.value("FileType", "model"), required_string(record, "Input"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), required_string(record, "SourceSha256"));
    for (const auto& record : document.value("NewTextures", json::array()))
        append(AssetKind::new_texture, "texture", required_string(record, "Input"), "", required_string(record, "Output"), required_string(record, "BaselineSha256"), "");
    std::stable_sort(result.assets_.begin(), result.assets_.end(), [](const auto& left, const auto& right) {
        const auto left_name = left.input.filename().native();
        const auto right_name = right.input.filename().native();
        if(natural_less_case_insensitive(left_name,right_name))return true;
        if(natural_less_case_insensitive(right_name,left_name))return false;
        return natural_less_case_insensitive(left.input.native(),right.input.native());
    });
    result.refresh();
    return result;
}

fs::path Workspace::resolve(const std::string& relative) const {
    fs::path candidate = fs::weakly_canonical(root_ / fs::u8path(relative));
    const auto& root_text = root_.native();
    const auto& candidate_text = candidate.native();
    if (candidate_text.size() <= root_text.size() || _wcsnicmp(candidate_text.c_str(), root_text.c_str(), root_text.size()) != 0 ||
        (candidate_text[root_text.size()] != L'\\' && candidate_text[root_text.size()] != L'/'))
        throw std::runtime_error("Workspace path escapes root: " + relative);
    return candidate;
}

void Workspace::refresh() {
    for (auto& asset : assets_) {
        asset.available = false; asset.changed = false;
        for(const auto& [path,baseline]:asset.monitored_inputs) if(fs::is_regular_file(path)){asset.available=true;if(sha256_file(path)!=baseline)asset.changed=true;}
    }
}

void Workspace::build_model(std::size_t index) {
    if (index >= assets_.size() || assets_[index].kind != AssetKind::model) throw std::runtime_error("Selected asset is not a native model file");
    auto& asset = assets_[index];
    if (!fs::is_regular_file(asset.input)) throw std::runtime_error("Model input is missing");
    if (!fs::is_regular_file(asset.source) || sha256_file(asset.source) != asset.source_sha256) throw std::runtime_error("Model source baseline is missing or changed");
    copy_atomic(asset.input, asset.output);
}

void Workspace::build_asset(std::size_t index) {
    if (index >= assets_.size()) throw std::runtime_error("Selected asset is invalid");
    if (assets_[index].kind == AssetKind::model) { build_model(index); return; }
    auto& asset = assets_[index];
    if (asset.wtb_slots.empty() || asset.source.empty()) throw std::runtime_error("Selected asset has no packable WTB source");
    if (!fs::is_regular_file(asset.source) || sha256_file(asset.source) != asset.source_sha256) throw std::runtime_error("WTB source baseline is missing or changed");
    write_wtb_from_slots(asset.source, asset.output, asset.wtb_slots);
}

void Workspace::restore_model(std::size_t index) {
    if (index >= assets_.size() || assets_[index].kind != AssetKind::model) throw std::runtime_error("Selected asset is not a native model file");
    auto& asset = assets_[index];
    if (!fs::is_regular_file(asset.source) || sha256_file(asset.source) != asset.source_sha256) throw std::runtime_error("Model source baseline is missing or changed");
    copy_atomic(asset.source, asset.input);
    if (sha256_file(asset.input) != asset.baseline_sha256) throw std::runtime_error("Restored model does not match workspace baseline");
    refresh();
}

void Workspace::restore_asset(std::size_t index) {
    if (index >= assets_.size()) throw std::runtime_error("Selected asset is invalid");
    if (assets_[index].kind == AssetKind::model) { restore_model(index); return; }
    auto& asset = assets_[index];
    if (asset.wtb_slots.empty() || asset.source.empty()) throw std::runtime_error("Selected asset has no restorable WTB source");
    if (!fs::is_regular_file(asset.source) || sha256_file(asset.source) != asset.source_sha256) throw std::runtime_error("WTB source baseline is missing or changed");
    restore_wtb_slots(asset.source, asset.wtb_slots);
    refresh();
}

std::size_t Workspace::missing_count() const noexcept { return std::count_if(assets_.begin(), assets_.end(), [](const auto& a) { return !a.available; }); }
std::size_t Workspace::changed_count() const noexcept { return std::count_if(assets_.begin(), assets_.end(), [](const auto& a) { return a.changed; }); }
const char* asset_kind_name(AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::texture: return "贴图槽";
    case AssetKind::ui_image: return "UI-image";
    case AssetKind::material: return "mmat";
    case AssetKind::cloth: return "cloth";
    case AssetKind::model: return "模型";
    case AssetKind::new_texture: return "新贴图";
    }
    return "未知";
}
}
