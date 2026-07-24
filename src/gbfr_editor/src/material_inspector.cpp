#include "material_inspector.hpp"
#include "mmat_param_layouts.generated.hpp"
#include "texture_gallery.hpp"

#include <gbfr/render/preview_renderer.hpp>

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {
std::string hex32(std::uint32_t value) {
    char text[11]{};
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}

const char* shader_type_name(std::uint8_t value) {
    switch (value) {
    case 0: return "player_silhouette?";
    case 1: return "player_silhouette2?";
    case 2: return "Eye?";
    case 3: return "Face?";
    case 4: return "Hair?";
    case 5: return "Metal";
    case 6: return "Skin";
    case 7: return "elementallookdev";
    case 8: return "flowmap";
    case 9: return "foliage";
    case 10: return "glowingground";
    case 11: return "glowingmountain";
    case 12: return "ice";
    case 13: return "ice_2layer";
    case 14: return "lavafall";
    case 15: return "lucilius";
    case 17: return "plantbillboard";
    case 18: return "plantmiddleview";
    case 19: return "plantshake";
    case 20: return "skycloud";
    case 21: return "uberenv";
    case 22: return "uberenv_layer2";
    case 23: return "uberenv_layer2_plantpivotpainter";
    case 24: return "uberenv_layer3";
    case 25: return "uberenv_layer4";
    case 26: return "uberenv_plantpivotpainter?";
    case 27: return "uberenvtextureless?";
    case 28: return "water_lake";
    case 29: return "grid";
    default: return "未知 / 不支持";
    }
}

const char* shader_sub_type_name(std::uint8_t value) {
    switch (value) {
    case 0: return "PBS / anime";
    case 1: return "未知";
    case 3: return "outline (skinning)";
    case 4: return "shadow";
    case 5: return "shadow skinning mask";
    case 6: return "pointlight shadow mask";
    case 7: return "regular / silhouette?";
    case 8: return "forward?";
    default: return "未知";
    }
}

const char* shadow_type_name(std::uint8_t value) {
    switch (value) {
    case 0: return "NoShadow";
    case 1: return "ShadowEnable_Unk1";
    case 2: return "ShadowEnable_AlphaBlend";
    case 3: return "ShadowEnable_NoAlphaBlend";
    default: return "Unknown";
    }
}

const char* parameter_meaning(const gbfr::MaterialShaderParameter& parameter) {
    switch (parameter.hash) {
    case 0x06CFE5A4u: return "自发光强度";
    case 0x24C1ABA9u: return "启用丢弃遮罩";
    case 0x372C03F0u: return "tsubasa 第四阶段机关特化参数";
    case 0x3C966EE3u: return "冰材质自发光开关 0";
    case 0x49D8C1B9u: return "启用描边";
    case 0x53F49792u: return "角色材质 pass key 位 0x4（社区推测与 Alpha 相关）";
    case 0x60F31A22u: return "使用 Albedo Alpha Clip";
    case 0x6C5CB9ACu: return "使用细节法线";
    case 0x7920C84Fu: return "使用抖动贴图";
    case 0x8E6B4C53u: return "使用立方体贴图反射";
    case 0x920821E1u: return "启用布尔遮罩";
    case 0x98EBBEC2u: return "摆动幅度";
    case 0x9F1DA064u: return "启用 Container 路径";
    case 0xB460A0F0u: return "使用深度淡化";
    case 0xCA06A6B6u: return "使用冰材质自发光";
    case 0xD94F2821u: return "双面材质";
    case 0xE208C4C4u: return "使用颜色噪声";
    case 0x11664BFCu: return "按第 5 号骨骼计算面部参考中心";
    case 0x56346692u: return "面部参考中心的骨骼局部偏移";
    case 0x8B8038FCu: return "选择角色材质的有效 Shader subtype 13 路径";
    case 0x92339519u: return "启用冰 / 晶体模型专用资源路径";
    case 0xBAEF6920u: return "右眼材质标记";
    case 0xE56343C0u: return "左眼材质标记";
    case 0x037BE4E5u: return "UberEnv / 植被禁用背面剔除（双面光栅路径）";
    case 0x0A05A26Fu: return "Foliage 专用参数";
    case 0x2AEDA6ADu: return "Elemental 第三管线模板开关（现有样本均关闭）";
    case 0x2B5C866Cu: return "Elemental 稀有附加描述符开关";
    case 0x4298F7E4u: return "Sky / Cloud 管线 permutation 位 0x2";
    case 0x93D9F63Au: return "Elemental 7/11 管线模板开关";
    case 0x9C83F56Fu: return "Metal 备用管线 / 资源描述符开关";
    case 0xA6EB1B34u: return "启用角色根位置相关的运行时方向数据";
    case 0xAB261CFAu: return "UberEnv 管线 permutation 位 0x10";
    case 0xAC6F995Du: return "能量护盾控制的遗迹材质组索引（0 / 1..4 / 5 汇总）";
    case 0xC5BD3DEDu: return "UberEnv 管线 permutation 位 0x100";
    case 0xC9762248u: return "UberEnv 管线 permutation 位 0x200";
    case 0xEB6F1AE7u: return "Foliage 实例世界变换首分量开关";
    default: return "尚未探明";
    }
}

const char* parameter_confidence(const gbfr::MaterialShaderParameter& parameter) {
    switch (parameter.hash) {
    case 0x06CFE5A4u: case 0x24C1ABA9u: case 0x372C03F0u: case 0x3C966EE3u:
    case 0x49D8C1B9u: case 0x60F31A22u: case 0x6C5CB9ACu: case 0x7920C84Fu:
    case 0x8E6B4C53u: case 0x920821E1u: case 0x98EBBEC2u: case 0x9F1DA064u:
    case 0xCA06A6B6u: case 0xD94F2821u: case 0xE208C4C4u:
        return "A：Shader RDEF";
    case 0xB460A0F0u:
        return "A：正式 schema";
    case 0x11664BFCu: case 0x2AEDA6ADu: case 0x2B5C866Cu: case 0x56346692u:
    case 0x8B8038FCu: case 0x92339519u: case 0x93D9F63Au: case 0x9C83F56Fu:
    case 0xA6EB1B34u: case 0xAC6F995Du: case 0xBAEF6920u: case 0xE56343C0u:
        return "B/C：运行时 + 样本";
    case 0x53F49792u:
        return "B/C：pass key + 社区推测";
    case 0xEB6F1AE7u:
        return "B：实例缓冲写入行为";
    case 0xAB261CFAu: case 0xC5BD3DEDu: case 0xC9762248u:
        return "B：管线 permutation 行为";
    case 0x4298F7E4u:
        return "B：管线 permutation 行为";
    case 0x037BE4E5u:
        return "B/C：光栅状态 + 样本";
    case 0x0A05A26Fu:
        return "D：少量样本";
    default:
        return "未知";
    }
}

const char* reflected_parameter_name(std::uint32_t hash) {
    switch (hash) {
    case 0x06CFE5A4u: return "g_EmissivePower";
    case 0x24C1ABA9u: return "g_EnableDiscardMask";
    case 0x372C03F0u: return "g_tsubasa_Param0_4stGimmick";
    case 0x3C966EE3u: return "g_UseIceEmissive0";
    case 0x49D8C1B9u: return "g_EnableOutLine";
    case 0x60F31A22u: return "g_IsUseAlbedoAlphaClip";
    case 0x6C5CB9ACu: return "g_IsUseDetailNormal";
    case 0x7920C84Fu: return "g_IsUseDitherMap";
    case 0x8E6B4C53u: return "g_UseCubeMapReflection";
    case 0x920821E1u: return "g_EnableBooleanMask";
    case 0x98EBBEC2u: return "g_SwayAmplitude";
    case 0x9F1DA064u: return "g_ContainerUse";
    case 0xCA06A6B6u: return "g_UseIceEmissive";
    case 0xD94F2821u: return "g_TwoSided";
    case 0xE208C4C4u: return "g_UseColorNoise";
    default: return nullptr;
    }
}

const char* texture_meaning(const gbfr::MaterialTextureMap& texture) {
    if (texture.hash == 0x5A2C820Cu) return "描边纹理（推断）";
    if (texture.hash == 0x8A0507FBu) return "面部 Mask 5（推断）";
    if (texture.name.find("Albedo") != std::string::npos) return "基础色";
    if (texture.name.find("Normal") != std::string::npos || texture.name.find("Bump") != std::string::npos) return "法线 / 凹凸";
    if (texture.name.find("Mask") != std::string::npos) return "遮罩";
    if (texture.name.find("Emissive") != std::string::npos) return "自发光";
    if (texture.name == "g_LUT") return "颜色查找表";
    if (texture.name.find("Eye") != std::string::npos) return "眼部专用";
    return "用途未探明";
}

const char* inferred_texture_name(std::uint32_t hash) {
    switch (hash) {
    case 0x5A2C820Cu: return "g_OutlineTexture";
    case 0x8A0507FBu: return "g_Mask5";
    default: return nullptr;
    }
}

const char* texture_confidence(const gbfr::MaterialTextureMap& texture) {
    if (inferred_texture_name(texture.hash)) return "C：哈希预像 + 全量样本";
    char placeholder[11]{};
    std::snprintf(placeholder, sizeof(placeholder), "g_%08X", texture.hash);
    if (texture.name.empty() || texture.name == placeholder) return "D：仅原始哈希";
    return "A：RDEF / schema";
}

bool parameter_enabled(const gbfr::MaterialEntry& entry, std::uint32_t hash) {
    const auto found = std::find_if(entry.shader_parameters.begin(), entry.shader_parameters.end(),
        [&](const auto& parameter) { return parameter.hash == hash; });
    return found != entry.shader_parameters.end() && found->value_or_offset != 0;
}

void label_value(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(value);
}

std::string utf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}

std::filesystem::path find_unpack_root(std::filesystem::path path) {
    for(path=path.parent_path();!path.empty();path=path.parent_path()){
        if(path.filename()==L"unpack")return path;
        if(path==path.root_path())break;
    }
    return {};
}

std::filesystem::path resolve_texture_dds(const std::filesystem::path& unpack_root,const std::string& texture_name,bool prefer_granite) {
    if(unpack_root.empty()||texture_name.empty())return {};
    std::u8string encoded(reinterpret_cast<const char8_t*>(texture_name.data()),texture_name.size());
    encoded+=u8".dds";
    const std::filesystem::path filename(encoded);
    const auto granite2k=unpack_root/L"data/granite/2k"/filename,texture2k=unpack_root/L"data/texture/2k"/filename;
    const auto granite4k=unpack_root/L"data/granite/4k"/filename,texture4k=unpack_root/L"data/texture/4k"/filename;
    const std::array candidates=prefer_granite?std::array{granite2k,granite4k,texture2k,texture4k}:std::array{texture2k,texture4k,granite2k,granite4k};
    for(const auto& candidate:candidates)if(std::filesystem::is_regular_file(candidate))return candidate;
    return {};
}

void open_directory(const std::filesystem::path& path) {
    if(!path.empty()&&std::filesystem::is_directory(path))ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}

void texture_context_menu(const char* id,const std::filesystem::path& dds) {
    if(!ImGui::BeginPopupContextItem(id))return;
    ImGui::BeginDisabled(dds.empty());
    if(ImGui::MenuItem("打开 unpack 文件夹"))open_directory(dds.parent_path());
    ImGui::EndDisabled();
    if(dds.empty())ImGui::TextDisabled("工作区中未找到已解包 DDS");
    ImGui::EndPopup();
}
}

namespace gbfr::editor {
void MaterialInspector::set_asset(MaterialAsset asset, std::filesystem::path path) {
    asset_ = std::move(asset);
    path_ = std::move(path);
    unpack_root_=find_unpack_root(path_);
    selected_material_ = 0;
}

void MaterialInspector::clear() {
    asset_ = {};
    path_.clear();
    unpack_root_.clear();
    selected_material_ = 0;
}

void MaterialInspector::draw(PreviewRenderer& renderer,TextureGallery& texture_gallery) {
    ImGui::Text("%s | materials %zu | constant buffers %zu | float pool %zu",
                path_.filename().string().c_str(), asset_.entries.size(), asset_.constant_buffers.size(), asset_.shader_parameter_float_pool.size());
    if (asset_.legacy_schema) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, .42f, .30f, 1.0f));
        ImGui::TextWrapped("旧 A1/A2/A4 JSON：该格式会丢失 Endless Ragnarok 常量缓冲和材质状态，已禁止构建。请在快捷操作中从 source 重新解码。");
        ImGui::PopStyleColor();
    }
    if (asset_.entries.empty()) { ImGui::TextUnformatted("该 mmat 没有材质条目。"); return; }

    ImGui::BeginChild("mmat_material_list", ImVec2(235, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (std::size_t index = 0; index < asset_.entries.size(); ++index) {
        const auto& material = asset_.entries[index];
        const auto label = std::to_string(index) + "  " + hex32(material.material_name_hash) + "##mmat" + std::to_string(index);
        if (ImGui::Selectable(label.c_str(), selected_material_ == static_cast<int>(index))) selected_material_ = static_cast<int>(index);
        ImGui::SameLine();
        ImGui::TextDisabled("%u/%u", material.shader_type, material.shader_sub_type);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("mmat_material_detail", ImVec2(0, 0));
    const auto selected = static_cast<std::size_t>(std::clamp(selected_material_, 0, static_cast<int>(asset_.entries.size() - 1)));
    const auto& material = asset_.entries[selected];

    if (ImGui::BeginTabBar("mmat_tabs")) {
        if (ImGui::BeginTabItem("渲染状态")) {
            if (ImGui::BeginTable("mmat_overview", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("字段", ImGuiTableColumnFlags_WidthFixed, 185);
                ImGui::TableSetupColumn("值 / 当前意义");
                label_value("material hash", hex32(material.material_name_hash).c_str());
                const auto shader = std::to_string(material.shader_type) + " - " + shader_type_name(material.shader_type) + "（社区资料）";
                label_value("shader_type", shader.c_str());
                const auto subtype = std::to_string(material.shader_sub_type) + " - " + shader_sub_type_name(material.shader_sub_type) + "（社区资料）";
                label_value("shader_sub_type", subtype.c_str());
                const auto shadow = std::to_string(material.shadow_type) + " - " + shadow_type_name(material.shadow_type);
                label_value("shadow_type", shadow.c_str());
                label_value("ignore_alpha", material.ignore_alpha ? "true" : "false");
                label_value("g_TwoSided", parameter_enabled(material, two_sided_shader_parameter_id) ? "1" : "0 / 未设置");
                label_value("0x53F49792 pass key 0x4", parameter_enabled(material, enable_alpha_shader_parameter_id) ? "1（社区推测与 Alpha 相关）" : "0 / 未设置");
                label_value("bool9 / bool10 / bool12", (std::to_string(material.bool9) + " / " + std::to_string(material.bool10) + " / " + std::to_string(material.bool12) + "（意义未知）").c_str());
                ImGui::EndTable();
            }
            ImGui::Spacing();
            if (material.shadow_type == 2 && parameter_enabled(material, two_sided_shader_parameter_id))
                ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f), "注意：社区资料尚未确认透明与双面的完整组合；不要仅凭预览器结果改写字段。");
            if (material.ignore_alpha && parameter_enabled(material, enable_alpha_shader_parameter_id))
                ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f), "Alpha 参数已启用但 ignore_alpha=true，请与原版同槽材质逐字段比较。");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shader 参数")) {
            ImGui::TextDisabled("证据：A=RDEF/schema 命名；B=游戏运行时行为；C=全量样本推断；D=少量样本，待验证。");
            if (ImGui::BeginTable("mmat_parameters", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Hash / 名称", ImGuiTableColumnFlags_WidthFixed, 235);
                ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("值 / 偏移", ImGuiTableColumnFlags_WidthFixed, 85);
                ImGui::TableSetupColumn("浮点值", ImGuiTableColumnFlags_WidthFixed, 180);
                ImGui::TableSetupColumn("意义");
                ImGui::TableSetupColumn("证据", ImGuiTableColumnFlags_WidthFixed, 145);
                ImGui::TableHeadersRow();
                for (const auto& parameter : material.shader_parameters) {
                    ImGui::TableNextRow();
                    const auto* reflected_name = reflected_parameter_name(parameter.hash);
                    const auto* display_name = reflected_name ? reflected_name : (parameter.name.empty() ? "<unknown>" : parameter.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%s\n%s", display_name, hex32(parameter.hash).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(material_value_type_name(parameter.type));
                    ImGui::TableNextColumn(); ImGui::Text("%u", parameter.value_or_offset);
                    ImGui::TableNextColumn();
                    if (parameter.floating_values.empty()) ImGui::TextDisabled("-");
                    else for (std::size_t index = 0; index < parameter.floating_values.size(); ++index) { if (index) ImGui::SameLine(0, 4); ImGui::Text("%.7g", parameter.floating_values[index]); }
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(parameter_meaning(parameter));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(parameter_confidence(parameter));
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("贴图与 Granite")) {
            if (ImGui::BeginTable("mmat_textures",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,std::max(180.0f,ImGui::GetContentRegionAvail().y*.62f)))) {
                ImGui::TableSetupScrollFreeze(0,1);
                ImGui::TableSetupColumn("预览",ImGuiTableColumnFlags_WidthFixed,82);
                ImGui::TableSetupColumn("Shader map",ImGuiTableColumnFlags_WidthFixed,190);
                ImGui::TableSetupColumn("贴图名",ImGuiTableColumnFlags_WidthFixed,220);
                ImGui::TableSetupColumn("意义",ImGuiTableColumnFlags_WidthFixed,120);
                ImGui::TableSetupColumn("证据",ImGuiTableColumnFlags_WidthFixed,170);
                ImGui::TableSetupColumn("已解包路径",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for(std::size_t texture_index=0;texture_index<material.texture_maps.size();++texture_index){
                    const auto& texture=material.texture_maps[texture_index];
                    const auto dds=resolve_texture_dds(unpack_root_,texture.texture_name,material.granite.has_value());
                    ImGui::PushID(static_cast<int>(texture_index));
                    ImGui::TableNextRow(ImGuiTableRowFlags_None,74.0f);
                    ImGui::TableNextColumn();
                    if(const auto* thumbnail=dds.empty()?nullptr:texture_gallery.thumbnail(renderer,dds)){
                        const float width=static_cast<float>(thumbnail->width),height=static_cast<float>(thumbnail->height);
                        const float scale=std::min(64.0f/std::max(1.0f,width),64.0f/std::max(1.0f,height));
                        const ImVec2 size{width*scale,height*scale};
                        const float x=ImGui::GetCursorPosX();ImGui::SetCursorPosX(x+(68.0f-size.x)*.5f);
                        ImGui::Image(reinterpret_cast<ImTextureID>(thumbnail->image.Get()),size,ImVec2(0,1),ImVec2(1,0));
                    }else{
                        ImGui::Dummy(ImVec2(68,64));
                        const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRect(min,max,ImGui::GetColorU32(ImGuiCol_Border));
                        const char* state=dds.empty()?"未解包":"解码失败";const auto text_size=ImGui::CalcTextSize(state);
                        ImGui::GetWindowDrawList()->AddText({min.x+(max.x-min.x-text_size.x)*.5f,min.y+(max.y-min.y-text_size.y)*.5f},ImGui::GetColorU32(ImGuiCol_TextDisabled),state);
                    }
                    texture_context_menu("##texture_context_preview",dds);
                    const auto* inferred_name=inferred_texture_name(texture.hash);
                    ImGui::TableNextColumn();ImGui::TextWrapped("%s%s\n%s",inferred_name?inferred_name:(texture.name.empty()?"<unknown>":texture.name.c_str()),inferred_name?"（推断）":"",hex32(texture.hash).c_str());texture_context_menu("##texture_context_map",dds);
                    ImGui::TableNextColumn();ImGui::TextUnformatted(texture.texture_name.c_str());texture_context_menu("##texture_context_name",dds);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(texture_meaning(texture));texture_context_menu("##texture_context_meaning",dds);
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s",texture_confidence(texture));texture_context_menu("##texture_context_confidence",dds);
                    ImGui::TableNextColumn();
                    if(dds.empty())ImGui::TextDisabled("未找到");else ImGui::TextWrapped("%s",utf8(dds.lexically_relative(unpack_root_)).c_str());
                    texture_context_menu("##texture_context_path",dds);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::SeparatorText("Granite streaming");
            if (!material.granite) ImGui::TextUnformatted("未设置 granite_params：游戏将按普通 texture 路径加载。");
            else {
                ImGui::Text("tile set %u | unk4 %u | unk5 %u", material.granite->tile_set_number, material.granite->unknown4, material.granite->unknown5);
                for (std::size_t index = 0; index < material.granite->page_files.size(); ++index)
                    ImGui::BulletText("page[%zu] %s", index, material.granite->page_files[index].c_str());
                for (std::size_t index = 0; index < material.granite->layer_names.size(); ++index)
                    ImGui::BulletText("layer[%zu] -> %s (%s)", index,
                        material.granite->layer_names[index].empty() ? "<unknown>" : material.granite->layer_names[index].c_str(),
                        index < material.granite->layer_hashes.size() ? hex32(material.granite->layer_hashes[index]).c_str() : "<missing>");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("常量缓冲")) {
            const auto* layout = gbfr::editor::mmat_catalog::find(material.shader_type);
            const gbfr::MaterialConstantBuffer* param_buffer = nullptr;
            std::size_t param_buffer_index = 0;
            if (layout && !material.constant_buffer_indices.empty()) {
                param_buffer_index = material.constant_buffer_indices.front();
                if (param_buffer_index < asset_.constant_buffers.size() &&
                    asset_.constant_buffers[param_buffer_index].words.size() * sizeof(std::uint32_t) == layout->size)
                    param_buffer = &asset_.constant_buffers[param_buffer_index];
            }
            if (param_buffer) {
                ImGui::SeparatorText("ParamBuffer（Shader RDEF 精确匹配）");
                ImGui::TextWrapped("%s | buffer %zu | %u bytes", layout->shader, param_buffer_index, layout->size);
                const auto reflected_height = std::clamp(ImGui::GetContentRegionAvail().y * .55f, 180.0f, 480.0f);
                if (ImGui::BeginTable("mmat_reflected_param_buffer", 4,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, reflected_height))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("字段");
                    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthFixed, 260);
                    ImGui::TableHeadersRow();
                    for (const auto& field : layout->fields) {
                        const auto first_word = static_cast<std::size_t>(field.offset / sizeof(std::uint32_t));
                        const auto components = static_cast<std::size_t>(field.size / sizeof(std::uint32_t));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(field.name);
                        ImGui::TableNextColumn(); ImGui::Text("%u", field.offset);
                        ImGui::TableNextColumn();
                        using gbfr::editor::mmat_catalog::FieldType;
                        if (field.type == FieldType::boolean) ImGui::TextUnformatted("bool");
                        else if (field.type == FieldType::signed_integer) ImGui::TextUnformatted(components == 1 ? "int" : "int vector");
                        else if (field.type == FieldType::unsigned_integer) ImGui::TextUnformatted(components == 1 ? "uint" : "uint vector");
                        else ImGui::TextUnformatted(components == 1 ? "float" : "float vector");
                        ImGui::TableNextColumn();
                        if (first_word + components > param_buffer->words.size()) ImGui::TextDisabled("越界");
                        else if (field.type == FieldType::boolean)
                            ImGui::Text("%s (raw %u)", param_buffer->words[first_word] ? "true" : "false", param_buffer->words[first_word]);
                        else if (field.type == FieldType::signed_integer)
                            for (std::size_t component = 0; component < components; ++component) {
                                if (component) ImGui::SameLine(0, 5);
                                ImGui::Text("%d", static_cast<std::int32_t>(param_buffer->words[first_word + component]));
                            }
                        else if (field.type == FieldType::unsigned_integer)
                            for (std::size_t component = 0; component < components; ++component) {
                                if (component) ImGui::SameLine(0, 5);
                                ImGui::Text("%u", param_buffer->words[first_word + component]);
                            }
                        else for (std::size_t component = 0; component < components; ++component) {
                            if (component) ImGui::SameLine(0, 5);
                            ImGui::Text("%.7g", std::bit_cast<float>(param_buffer->words[first_word + component]));
                        }
                    }
                    ImGui::EndTable();
                }
            } else if (layout) {
                ImGui::TextDisabled("首个材质 buffer 与 %s 的 %u-byte ParamBuffer 不匹配，未进行字段解析。", layout->shader, layout->size);
            }
            ImGui::SeparatorText("原始 Buffer");
            ImGui::TextUnformatted("所有 buffer 原始位模式同时按 uint/hex/float 显示。材质引用：");
            ImGui::SameLine();
            for (std::size_t index = 0; index < material.constant_buffer_indices.size(); ++index) { if (index) ImGui::SameLine(0, 5); ImGui::Text("%u", material.constant_buffer_indices[index]); }
            if (ImGui::BeginTable("mmat_buffers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Buffer", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("Name hash", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Float reinterpret");
                ImGui::TableHeadersRow();
                for (std::size_t buffer_index = 0; buffer_index < asset_.constant_buffers.size(); ++buffer_index) {
                    const auto& buffer = asset_.constant_buffers[buffer_index];
                    for (std::size_t word_index = 0; word_index < buffer.words.size(); ++word_index) {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%zu%s", buffer_index,
                            std::find(material.constant_buffer_indices.begin(), material.constant_buffer_indices.end(), buffer_index) != material.constant_buffer_indices.end() ? " *" : "");
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(hex32(buffer.name_hash).c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%zu", word_index);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(hex32(buffer.words[word_index]).c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.9g", std::bit_cast<float>(buffer.words[word_index]));
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("文件信息")) {
            ImGui::Text("magic %u%s", asset_.magic, asset_.magic == 20230727u ? " (valid)" : " (unexpected)");
            ImGui::Text("root unk2 %u | bool3 %s | bool4 %s | bool5 %s", asset_.unknown2, asset_.bool3 ? "true" : "false", asset_.bool4 ? "true" : "false", asset_.bool5 ? "true" : "false");
            ImGui::SeparatorText("shader_param_float_data_pool");
            if(asset_.shader_parameter_float_pool.empty())ImGui::TextDisabled("空");
            else if(ImGui::BeginTable("mmat_float_pool",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV)){
                ImGui::TableSetupColumn("Index",ImGuiTableColumnFlags_WidthFixed,80);ImGui::TableSetupColumn("Float");ImGui::TableHeadersRow();
                for(std::size_t index=0;index<asset_.shader_parameter_float_pool.size();++index){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%zu",index);ImGui::TableNextColumn();ImGui::Text("%.9g",asset_.shader_parameter_float_pool[index]);}
                ImGui::EndTable();
            }
            ImGui::TextWrapped("当前阶段为无损检查与安全封回。未知常量缓冲只显示原始位模式，不提供会破坏数据的猜测性编辑。");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}
}
