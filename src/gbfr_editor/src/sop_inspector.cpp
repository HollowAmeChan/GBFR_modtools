#include "sop_inspector.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {
int bone_name_id(const std::string& name) {
    if (name.size() < 2 || name.front() != '_') return -1;
    int value{};
    const auto result = std::from_chars(name.data() + 1, name.data() + name.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == name.data() + name.size() ? value : -1;
}

std::string bone_code(std::uint32_t id) {
    char value[16]{};
    const auto result = std::to_chars(value, value + sizeof(value), id, 16);
    return result.ec == std::errc{} ? "_" + std::string(value, result.ptr) : "_???";
}

std::string bone_display(std::uint32_t id, const std::unordered_map<std::string, std::string>& names) {
    const auto code = bone_code(id);
    const auto found = names.find(code);
    return found == names.end() ? code : found->second + " (" + code + ")";
}

bool contains_ascii_case_insensitive(std::string value, std::string query) {
    const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(value.begin(), value.end(), value.begin(), lower);
    std::transform(query.begin(), query.end(), query.begin(), lower);
    return value.find(query) != std::string::npos;
}

std::string hash_text(std::uint32_t value) {
    char text[11] = "0x00000000";
    constexpr char digits[] = "0123456789ABCDEF";
    for (int index = 0; index < 8; ++index) text[9 - index] = digits[(value >> (index * 4)) & 0xfu];
    return text;
}

bool discovery_matches(std::string_view discovery, int filter) {
    if (filter == 0) return true;
    if (filter == 1) return discovery == "confirmed" || discovery == "core_confirmed";
    if (filter == 2) return discovery == "partial";
    return discovery == "unknown";
}

struct BoneChoice {
    int id{};
    std::string label;
};

std::vector<BoneChoice> bone_choices(const gbfr::SkeletonAsset& skeleton,
                                     const std::unordered_map<std::string, std::string>& names) {
    std::vector<BoneChoice> result;
    for (const auto& bone : skeleton.bones) {
        const int id = bone_name_id(bone.name);
        if (id < 0 || std::any_of(result.begin(), result.end(), [id](const auto& item) { return item.id == id; })) continue;
        result.push_back({id, bone_display(static_cast<std::uint32_t>(id), names)});
    }
    return result;
}

bool bone_combo(const char* label, int& value, const std::vector<BoneChoice>& choices) {
    const auto current = std::find_if(choices.begin(), choices.end(), [value](const auto& item) { return item.id == value; });
    const char* preview = current == choices.end() ? "请选择骨骼" : current->label.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const auto& choice : choices) {
            if (ImGui::Selectable(choice.label.c_str(), value == choice.id)) {
                value = choice.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool editable_swing_twist(const gbfr::SopOperation& operation) {
    return operation.type_hash == gbfr::sop_swing_twist_operation &&
        operation.find(gbfr::sop_axis_x_property) && operation.find(gbfr::sop_axis_y_property) &&
        operation.find(gbfr::sop_axis_z_property) && operation.find(gbfr::sop_swing_rate_property) &&
        operation.find(gbfr::sop_twist_rate_property);
}

void set_float(gbfr::SopOperation& operation, std::uint32_t hash, float value) {
    if (auto* property = operation.find(hash)) {
        property->type = gbfr::SopPropertyType::floating;
        property->raw_value = std::bit_cast<std::uint32_t>(value);
    }
}

int operation_axis(const gbfr::SopOperation& operation) {
    const float values[] = {
        operation.find(gbfr::sop_axis_x_property)->floating(),
        operation.find(gbfr::sop_axis_y_property)->floating(),
        operation.find(gbfr::sop_axis_z_property)->floating(),
    };
    return static_cast<int>(std::max_element(std::begin(values), std::end(values)) - std::begin(values));
}
}

namespace gbfr::editor {
bool SopInspector::load_catalog(const std::filesystem::path& path) {
    try {
        std::ifstream input(path);
        if (!input) return false;
        nlohmann::json document;
        input >> document;
        const auto operations = document.find("Operations");
        if (operations == document.end() || !operations->is_array()) return false;
        std::unordered_map<std::uint32_t, SopOperationDescription> parsed;
        for (const auto& item : *operations) {
            const auto hash = item.value("Hash", std::string{});
            if (hash.size() != 10 || hash.rfind("0x", 0) != 0) return false;
            std::uint32_t value{};
            const auto result = std::from_chars(hash.data() + 2, hash.data() + hash.size(), value, 16);
            if (result.ec != std::errc{} || result.ptr != hash.data() + hash.size() || parsed.contains(value)) return false;
            parsed.emplace(value, SopOperationDescription{
                item.value("Name", std::string{"未知操作"}), item.value("Category", std::string{"Unknown"}),
                item.value("Discovery", std::string{"unknown"}), item.value("DiscoveryLabel", std::string{"未探明"}),
                item.value("Runtime", std::string{"not_implemented"}), item.value("RuntimeLabel", std::string{"预览器未实现"}),
                item.value("Purpose", std::string{"目录中没有该操作的用途记录。"})});
        }
        catalog_ = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

void SopInspector::set_asset(SopAsset asset, std::filesystem::path path, bool editable) {
    const bool same_path = path_ == path;
    asset_ = std::move(asset);
    path_ = std::move(path);
    editable_ = editable;
    dirty_ = false;
    selected_operation_ = -1;
    search_.fill('\0');
    status_filter_ = 0;
    selected_bone_only_ = false;
    new_target_ = -1;
    new_source_ = -1;
    if (!same_path) {
        status_message_.clear();
        status_error_ = false;
    }
}

void SopInspector::clear() {
    asset_ = {};
    path_.clear();
    selected_operation_ = -1;
    search_.fill('\0');
    status_filter_ = 0;
    selected_bone_only_ = false;
    editable_ = false;
    dirty_ = false;
    new_target_ = -1;
    new_source_ = -1;
    status_message_.clear();
    status_error_ = false;
}

const SopOperationDescription* SopInspector::describe(std::uint32_t type_hash) const noexcept {
    const auto found = catalog_.find(type_hash);
    return found == catalog_.end() ? nullptr : &found->second;
}

bool SopInspector::draw(const SkeletonAsset& skeleton,
                        const std::unordered_map<std::string, std::string>& bone_names,
                        int& selected_bone) {
    bool saved = false;
    const auto choices = bone_choices(skeleton, bone_names);
    if (new_target_ < 0 && selected_bone >= 0) new_target_ = selected_bone;
    if (new_target_ < 0 && !choices.empty()) new_target_ = choices.front().id;
    if (new_source_ < 0 && !choices.empty()) new_source_ = choices.front().id;

    ImGui::Text("%s | 操作 %zu", path_.empty() ? "无 SOP" : path_.filename().string().c_str(), asset_.operations.size());
    if (!editable_) ImGui::TextColored(ImVec4(.95f, .68f, .22f, 1), "当前仅为 source 只读文件；请重新扫描工作区以生成 unpack 副本。");

    ImGui::BeginDisabled(!editable_ || !dirty_ || path_.empty());
    if (ImGui::Button("保存 SOP")) {
        try {
            save_sop(path_, asset_);
            dirty_ = false;
            status_message_ = "SOP 已保存，正在刷新模型预览。";
            status_error_ = false;
            saved = true;
        } catch (const std::exception& error) {
            status_message_ = std::string("保存失败：") + error.what();
            status_error_ = true;
        }
    }
    ImGui::EndDisabled();
    if (!status_message_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(status_error_ ? ImVec4(.95f, .35f, .32f, 1) : ImVec4(.35f, .82f, .48f, 1),
                           "%s", status_message_.c_str());
    }

    if (ImGui::CollapsingHeader("新增复制旋转约束", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(260);
        bone_combo("目标裙骨##new_target", new_target_, choices);
        ImGui::SetNextItemWidth(260);
        bone_combo("来源腿骨##new_source", new_source_, choices);
        const char* axes[] = {"X", "Y", "Z"};
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("旋转轴##new_axis", &new_axis_, axes, 3);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Swing 比例##new_swing", &new_swing_rate_, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Twist 比例##new_twist", &new_twist_rate_, 0.0f, 1.0f, "%.2f");
        if (ImGui::Button("完整复制 1:1")) new_swing_rate_ = new_twist_rate_ = 1.0f;
        ImGui::SameLine();
        ImGui::BeginDisabled(!editable_ || new_target_ < 0 || new_source_ < 0 || new_target_ == new_source_);
        if (ImGui::Button("添加约束")) {
            asset_.operations.push_back(make_swing_twist_operation(
                static_cast<std::uint32_t>(new_target_), static_cast<std::uint32_t>(new_source_),
                static_cast<SopAxis>(new_axis_), new_swing_rate_, new_twist_rate_));
            selected_operation_ = static_cast<int>(asset_.operations.size() - 1);
            selected_bone = new_target_;
            dirty_ = true;
            status_message_ = "约束已加入内存；点击保存 SOP 写入文件。";
            status_error_ = false;
        }
        ImGui::EndDisabled();
    }

    std::size_t confirmed{}, partial{}, unknown{}, runtime{};
    for (const auto& operation : asset_.operations) {
        const auto* info = describe(operation.type_hash);
        if (!info) { ++unknown; continue; }
        if (info->discovery == "confirmed" || info->discovery == "core_confirmed") ++confirmed;
        else if (info->discovery == "partial") ++partial;
        else ++unknown;
        if (info->runtime == "implemented_guarded") ++runtime;
    }
    ImGui::TextColored(ImVec4(.35f, .82f, .48f, 1), "已确认 %zu", confirmed); ImGui::SameLine();
    ImGui::TextColored(ImVec4(.95f, .68f, .22f, 1), "部分探明 %zu", partial); ImGui::SameLine();
    ImGui::TextColored(ImVec4(.95f, .35f, .32f, 1), "未探明 %zu", unknown); ImGui::SameLine();
    ImGui::Text("预览可执行 %zu", runtime);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##sop_search", "骨名、哈希、操作或作用", search_.data(), search_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    const char* statuses[] = {"全部状态", "已确认", "部分探明", "未探明"};
    ImGui::Combo("##sop_status", &status_filter_, statuses, 4);
    ImGui::SameLine();
    ImGui::Checkbox("只看选中骨", &selected_bone_only_);

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("sop_operations", 7, flags, ImVec2(0, std::max(180.0f, ImGui::GetContentRegionAvail().y * .48f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 38);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 155);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 145);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("作用", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("探明", ImGuiTableColumnFlags_WidthFixed, 105);
        ImGui::TableSetupColumn("预览", ImGuiTableColumnFlags_WidthFixed, 135);
        ImGui::TableHeadersRow();
        const std::string query = search_.data();
        for (std::size_t index = 0; index < asset_.operations.size(); ++index) {
            const auto& operation = asset_.operations[index];
            const auto* known = describe(operation.type_hash);
            const SopOperationDescription fallback{"未知操作", "Unknown", "unknown", "未探明",
                "not_implemented", "预览器未实现", "原始属性会被原样保留。"};
            const auto& info = known ? *known : fallback;
            if (!discovery_matches(info.discovery, status_filter_)) continue;
            if (selected_bone_only_ && selected_bone >= 0 && static_cast<std::uint32_t>(selected_bone) != operation.target_bone &&
                static_cast<std::uint32_t>(selected_bone) != operation.source_bone) continue;
            const auto target = bone_display(operation.target_bone, bone_names);
            const auto source = bone_display(operation.source_bone, bone_names);
            const auto hash = hash_text(operation.type_hash);
            if (!query.empty() && !contains_ascii_case_insensitive(target, query) &&
                !contains_ascii_case_insensitive(source, query) && !contains_ascii_case_insensitive(info.name, query) &&
                !contains_ascii_case_insensitive(info.purpose, query) && !contains_ascii_case_insensitive(hash, query)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto label = std::to_string(index) + "##sop_operation" + std::to_string(index);
            if (ImGui::Selectable(label.c_str(), selected_operation_ == static_cast<int>(index), ImGuiSelectableFlags_SpanAllColumns)) {
                selected_operation_ = static_cast<int>(index);
                selected_bone = static_cast<int>(operation.target_bone);
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(target.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(source.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s\n%s", info.name.c_str(), hash.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", info.purpose.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(info.discovery_label.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(info.runtime_label.c_str());
        }
        ImGui::EndTable();
    }

    if (selected_operation_ >= 0 && selected_operation_ < static_cast<int>(asset_.operations.size())) {
        auto& operation = asset_.operations[static_cast<std::size_t>(selected_operation_)];
        if (editable_swing_twist(operation)) {
            ImGui::SeparatorText("复制旋转参数");
            ImGui::BeginDisabled(!editable_);
            int target = static_cast<int>(operation.target_bone);
            int source = static_cast<int>(operation.source_bone);
            ImGui::SetNextItemWidth(260);
            if (bone_combo("目标骨##edit_target", target, choices)) { operation.target_bone = static_cast<std::uint32_t>(target); dirty_ = true; }
            ImGui::SetNextItemWidth(260);
            if (bone_combo("来源骨##edit_source", source, choices)) { operation.source_bone = static_cast<std::uint32_t>(source); dirty_ = true; }
            int axis = operation_axis(operation);
            const char* axes[] = {"X", "Y", "Z"};
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("旋转轴##edit_axis", &axis, axes, 3)) {
                set_float(operation, sop_axis_x_property, axis == 0 ? 1.0f : 0.0f);
                set_float(operation, sop_axis_y_property, axis == 1 ? 1.0f : 0.0f);
                set_float(operation, sop_axis_z_property, axis == 2 ? 1.0f : 0.0f);
                dirty_ = true;
            }
            float swing = operation.find(sop_swing_rate_property)->floating();
            float twist = operation.find(sop_twist_rate_property)->floating();
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderFloat("Swing 比例##edit_swing", &swing, 0.0f, 1.0f, "%.2f")) { set_float(operation, sop_swing_rate_property, swing); dirty_ = true; }
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderFloat("Twist 比例##edit_twist", &twist, 0.0f, 1.0f, "%.2f")) { set_float(operation, sop_twist_rate_property, twist); dirty_ = true; }
            if (ImGui::Button("删除这条约束")) {
                asset_.operations.erase(asset_.operations.begin() + selected_operation_);
                selected_operation_ = -1;
                dirty_ = true;
                status_message_ = "约束已删除；点击保存 SOP 写入文件。";
                status_error_ = false;
            }
            ImGui::EndDisabled();
        } else {
            ImGui::SeparatorText("原始属性（只读）");
            ImGui::Text("文件序号 %d | metadata %s | 属性 %zu", selected_operation_,
                        hash_text(operation.metadata).c_str(), operation.properties.size());
            if (ImGui::BeginTable("sop_properties", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("字段哈希"); ImGui::TableSetupColumn("类型");
                ImGui::TableSetupColumn("值"); ImGui::TableSetupColumn("Raw"); ImGui::TableHeadersRow();
                for (const auto& property : operation.properties) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(hash_text(property.hash).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(property.type == SopPropertyType::floating ? "float" : "integer");
                    ImGui::TableNextColumn();
                    if (property.type == SopPropertyType::floating) ImGui::Text("%.8g", property.floating());
                    else ImGui::Text("%u", property.integer());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(hash_text(property.raw_value).c_str());
                }
                ImGui::EndTable();
            }
        }
    }
    return saved;
}
}
