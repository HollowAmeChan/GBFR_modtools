#include <gbfr/core/workspace.hpp>

#include <nlohmann/json.hpp>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
}

namespace gbfr {
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
    for (const auto& record : document.value("Textures", json::array())) {
        const auto slots=record.value("Slots",json::array()); if(slots.empty()) continue;
        const auto& first=slots.front(); append(AssetKind::texture,"WTB / "+std::to_string(slots.size())+" 槽",required_string(first,"Path"),required_string(record,"Source"),required_string(record,"Output"),required_string(first,"BaselineSha256"),record.value("SourceSha256",""));
        auto& asset=result.assets_.back();
        for(std::size_t i=1;i<slots.size();++i) asset.monitored_inputs.emplace_back(result.resolve(required_string(slots[i],"Path")),required_string(slots[i],"BaselineSha256"));
    }
    for (const auto& record : document.value("Materials", json::array()))
        append(AssetKind::material, "mmat", required_string(record, "Json"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), record.value("SourceSha256", ""));
    for (const auto& record : document.value("ClothFiles", json::array()))
        append(AssetKind::cloth, record.value("Category", "cloth"), required_string(record, "Xml"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), record.value("SourceSha256", ""));
    for (const auto& record : document.value("ModelFiles", json::array()))
        append(AssetKind::model, record.value("FileType", "model"), required_string(record, "Input"), required_string(record, "Source"), required_string(record, "Output"), required_string(record, "BaselineSha256"), required_string(record, "SourceSha256"));
    for (const auto& record : document.value("NewTextures", json::array()))
        append(AssetKind::new_texture, "texture", required_string(record, "Input"), "", required_string(record, "Output"), required_string(record, "BaselineSha256"), "");
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

void Workspace::restore_model(std::size_t index) {
    if (index >= assets_.size() || assets_[index].kind != AssetKind::model) throw std::runtime_error("Selected asset is not a native model file");
    auto& asset = assets_[index];
    if (!fs::is_regular_file(asset.source) || sha256_file(asset.source) != asset.source_sha256) throw std::runtime_error("Model source baseline is missing or changed");
    copy_atomic(asset.source, asset.input);
    if (sha256_file(asset.input) != asset.baseline_sha256) throw std::runtime_error("Restored model does not match workspace baseline");
    refresh();
}

std::size_t Workspace::missing_count() const noexcept { return std::count_if(assets_.begin(), assets_.end(), [](const auto& a) { return !a.available; }); }
std::size_t Workspace::changed_count() const noexcept { return std::count_if(assets_.begin(), assets_.end(), [](const auto& a) { return a.changed; }); }
const char* asset_kind_name(AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::texture: return "贴图槽";
    case AssetKind::material: return "mmat";
    case AssetKind::cloth: return "cloth";
    case AssetKind::model: return "模型";
    case AssetKind::new_texture: return "新贴图";
    }
    return "未知";
}
}
