#include "material_inspector.hpp"
#include "imgui_texture_view.hpp"
#include "mmat_param_layouts.generated.hpp"
#include "texture_gallery.hpp"

#include <gbfr/formats/material_variants.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

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
    case 2: return "Eye";
    case 3: return "Face";
    case 4: return "Hair";
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
    case 0x53F49792u: return "开启角色 Albedo Alpha 丢弃模式（A=0 丢弃，中间值抖动裁切）";
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
    case 0x8B8038FCu: return "选择角色管线状态表第 13 路径（视觉语义未确认）";
    case 0x92339519u: return "启用冰 / 晶体模型专用资源路径";
    case 0xBAEF6920u: return "右眼材质标记";
    case 0xE56343C0u: return "左眼材质标记";
    case 0x037BE4E5u: return "UberEnv / 植被禁用背面剔除（双面光栅路径）";
    case 0x0A05A26Fu: return "Foliage 兼容 / 作者字段候选（当前 EXE 未发现消费者）";
    case 0x2AEDA6ADu: return "Elemental 第三管线模板开关（现有样本均关闭）";
    case 0x2B5C866Cu: return "Elemental 稀有附加描述符开关";
    case 0x4298F7E4u: return "Sky / Cloud 管线 permutation 位 0x2";
    case 0x93D9F63Au: return "Elemental 7/11 管线模板开关";
    case 0x9C83F56Fu: return "Metal 运行时自发光第一乘数：在结构 +0x08 / +0x0C 两套值间选择";
    case 0xA6EB1B34u: return "Metal 阴影类型 3 的方向驱动 Alpha 裁切阈值（标称范围 0.2..1.0）";
    case 0xAB261CFAu: return "UberEnv 管线 permutation 位 0x10";
    case 0xAC6F995Du: return "能量护盾控制的遗迹材质组索引（0 / 1..4 / 5 汇总）";
    case 0xC5BD3DEDu: return "UberEnv 管线 permutation 位 0x100";
    case 0xC9762248u: return "UberEnv 管线 permutation 位 0x200";
    case 0xEB6F1AE7u: return "Foliage 实例世界变换首分量开关";
    default: return "尚未探明";
    }
}

bool parameter_is_boolean(std::uint32_t hash) {
    switch(hash){
    case 0x24C1ABA9u:case 0x3C966EE3u:case 0x49D8C1B9u:case 0x53F49792u:
    case 0x60F31A22u:case 0x6C5CB9ACu:case 0x7920C84Fu:case 0x8E6B4C53u:
    case 0x920821E1u:case 0x9F1DA064u:case 0xB460A0F0u:case 0xCA06A6B6u:
    case 0xD94F2821u:case 0xE208C4C4u:return true;
    default:return false;
    }
}

bool input_text_value(const char* id,std::string& value) {
    std::array<char,512> buffer{};std::snprintf(buffer.data(),buffer.size(),"%s",value.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(!ImGui::InputText(id,buffer.data(),buffer.size()))return false;
    value=buffer.data();return true;
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
    case 0x92339519u: case 0x93D9F63Au:
    case 0xAC6F995Du: case 0xBAEF6920u: case 0xE56343C0u:
        return "B/C：运行时 + 样本";
    case 0x8B8038FCu:
        return "B：EXE 管线状态表 + 全量样本";
    case 0xA6EB1B34u:
        return "B：CPU 写入 + DXBC 消费 + 角色全量样本";
    case 0x9C83F56Fu:
        return "B：CPU 选择 + DXBC 自发光分支消费";
    case 0x53F49792u:
        return "B：EXE 选择 _5discard + DXBC discard";
    case 0xEB6F1AE7u:
        return "B：实例缓冲写入行为";
    case 0xAB261CFAu: case 0xC5BD3DEDu: case 0xC9762248u:
        return "B：管线 permutation 行为";
    case 0x4298F7E4u:
        return "B：管线 permutation 行为";
    case 0x037BE4E5u:
        return "B/C：光栅状态 + 样本";
    case 0x0A05A26Fu:
        return "D：少量样本 + 当前 EXE 无消费者";
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
    case 0x53F49792u: return "Cutout 丢弃模式";
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

const gbfr::MaterialShaderParameter* find_parameter(const gbfr::MaterialEntry& entry, std::uint32_t hash) {
    const auto found = std::find_if(entry.shader_parameters.begin(), entry.shader_parameters.end(),
        [&](const auto& parameter) { return parameter.hash == hash; });
    return found == entry.shader_parameters.end() ? nullptr : &*found;
}

bool is_player_character_path(const std::filesystem::path& path) {
    bool after_model = false;
    for (const auto& component : path) {
        const auto name = component.wstring();
        if (after_model) return name == L"pl" || name == L"fp" || name == L"wp";
        after_model = name == L"model";
    }
    return false;
}

const char* character_param_buffer_meaning(std::string_view name) {
    if (name == "g_VariationMulAlbedoColor") return "配色变体的 Albedo 乘色";
    if (name == "g_VariationMulAlbedoColor2") return "第二组配色变体的 Albedo 乘色";
    if (name == "g_VariationMulRoughness") return "配色变体的粗糙度倍率";
    if (name == "g_VariationMulRoughness2") return "第二组配色变体的粗糙度倍率";
    if (name == "g_VariationEnable") return "配色变体开关";
    if (name == "g_Roughness") return "粗糙度";
    if (name == "g_EnableOutLine") return "描边开关";
    if (name == "g_EnableForwardLight") return "前向光照开关";
    if (name == "g_EnableHatching") return "排线阴影开关";
    if (name == "g_HatchingColor") return "排线阴影颜色";
    if (name == "g_EnableRimLight") return "边缘光开关";
    if (name == "g_RimLightIntensity") return "边缘光强度";
    if (name == "g_EnableEmissive") return "自发光开关";
    if (name == "g_EmissiveIntensity") return "自发光强度";
    if (name == "g_EnableSpecularColor") return "高光颜色开关";
    if (name == "g_EnableDiscardMask") return "discard mask 开关";
    if (name == "g_EnableBooleanMask") return "布尔遮罩开关";
    if (name == "g_EnableFlatNormal") return "平面法线开关";
    if (name == "g_AnisoWidth") return "头发各向异性高光宽度";
    if (name == "g_UseBlendFaceColor") return "混合面部颜色开关";
    if (name == "g_WrinkleColor") return "皱纹颜色";
    if (name == "g_cheekLowColor") return "脸颊暗部颜色";
    if (name == "g_cheekHighColor") return "脸颊亮部颜色";
    if (name == "g_AngleLerpWidth") return "面部角度过渡宽度";
    if (name == "g_AngleBiasA" || name == "g_AngleBiasC") return "面部角度偏置";
    if (name == "g_IsMouth") return "嘴部材质标记";
    if (name == "g_IsTooth") return "牙齿材质标记";
    if (name == "g_UseJointPos") return "使用骨骼位置开关";
    if (name == "g_ParallaxBias") return "眼球视差偏置";
    if (name == "g_PupilScaleHeight" || name == "g_PupilScaleWidth") return "瞳孔缩放";
    if (name == "g_UseMask1ForIrisEmissive") return "使用 Mask1 控制虹膜自发光";
    return "-";
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
    const std::filesystem::path base(encoded);
    const std::array filenames={base.wstring()+L".dds",base.wstring()+L"_0.dds"};
    const std::array directories=prefer_granite?
        std::array{unpack_root/L"data/granite/2k",unpack_root/L"data/granite/4k",unpack_root/L"data/texture/2k",unpack_root/L"data/texture/4k"}:
        std::array{unpack_root/L"data/texture/2k",unpack_root/L"data/texture/4k",unpack_root/L"data/granite/2k",unpack_root/L"data/granite/4k"};
    for(const auto& directory:directories)for(const auto& filename:filenames){const auto candidate=directory/filename;if(std::filesystem::is_regular_file(candidate))return candidate;}
    return {};
}

std::vector<std::string> workspace_texture_names(const std::filesystem::path& unpack_root) {
    std::vector<std::string> result;
    if(unpack_root.empty())return result;
    for(const auto& relative:{L"data/texture/2k",L"data/texture/4k",L"data/granite/2k",L"data/granite/4k"}){
        const auto directory=unpack_root/relative;if(!std::filesystem::is_directory(directory))continue;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(directory)){
            if(!entry.is_regular_file()||_wcsicmp(entry.path().extension().c_str(),L".dds")!=0)continue;
            auto stem=entry.path().stem().wstring();if(stem.size()>2&&stem.ends_with(L"_0"))stem.resize(stem.size()-2);
            result.push_back(utf8(std::filesystem::path(stem)));
        }
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}

struct TextureMapOption { const char* label; const char* json_name; std::uint32_t hash; };
constexpr std::array texture_map_options{
    TextureMapOption{"基础色", "g_AlbedoMap", 0x3F2B4D59u},
    TextureMapOption{"法线", "g_NormalMap", 0xADBA7C37u},
    TextureMapOption{"Mask 1", "g_Mask1", 0x847A6CBDu},
    TextureMapOption{"Mask 2", "g_Mask2", 0x6137BA13u},
    TextureMapOption{"Mask 3", "g_Mask3", 0x35091AFAu},
    TextureMapOption{"Mask 4", "g_Mask4", 0x393263EFu},
    TextureMapOption{"描边遮罩", "g_5A2C820C", 0x5A2C820Cu},
    TextureMapOption{"Mask 5", "g_8A0507FB", 0x8A0507FBu},
    TextureMapOption{"眼部高光", "g_EyeHighLightTexture", 0x00B36A70u},
    TextureMapOption{"虹膜", "g_EyeIrisTexture", 0x637A19F3u},
    TextureMapOption{"眼白", "g_EyeWhiteTexture", 0xAEDB57AEu}
};

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
void MaterialInspector::set_asset(MaterialAsset asset, std::filesystem::path path, std::size_t selected_material) {
    if(path_==path&&!path.empty()&&dirty_){
        selected_material_=asset_.entries.empty()?0:static_cast<int>(std::min(selected_material,asset_.entries.size()-1));
        return;
    }
    if(dirty_&&!save_document())return;
    asset_ = std::move(asset);
    path_ = std::move(path);
    unpack_root_=find_unpack_root(path_);
    selected_material_ = asset_.entries.empty() ? 0 : static_cast<int>(std::min(selected_material, asset_.entries.size() - 1));
    dirty_=false;
    edit_status_.clear();
    load_document();
    refresh_texture_names();
}

void MaterialInspector::clear() {
    if(dirty_&&!save_document())return;
    asset_ = {};
    document_ = {};
    path_.clear();
    unpack_root_.clear();
    texture_names_.clear();
    selected_material_ = 0;
    dirty_=false;
    file_changed_=false;
    edit_status_.clear();
}

bool MaterialInspector::load_document() {
    try{
        std::ifstream input(path_);if(!input)throw std::runtime_error("无法打开 mmat JSON");
        input>>document_;
        return true;
    }catch(const std::exception& error){document_={};edit_status_=std::string("读取编辑文档失败：")+error.what();return false;}
}

bool MaterialInspector::save_document() {
    if(!dirty_)return true;
    try{
        if(path_.empty()||document_.empty())throw std::runtime_error("没有可保存的 mmat JSON");
        auto temporary=path_;temporary+=L".edit.tmp";
        {std::ofstream output(temporary,std::ios::trunc);if(!output)throw std::runtime_error("无法创建临时文件");output<<document_.dump(2)<<'\n';if(!output)throw std::runtime_error("写入临时文件失败");}
        {nlohmann::json validation;std::ifstream input(temporary);if(!input)throw std::runtime_error("无法验证临时文件");input>>validation;}
        auto validated_asset=load_mmat_json(temporary);
        if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            const auto error=GetLastError();std::filesystem::remove(temporary);throw std::runtime_error("替换 mmat JSON 失败（Win32 "+std::to_string(error)+"）");
        }
        asset_=std::move(validated_asset);selected_material_=asset_.entries.empty()?0:std::clamp(selected_material_,0,static_cast<int>(asset_.entries.size()-1));
        dirty_=false;file_changed_=true;edit_status_="已保存到 unpack："+utf8(path_.filename());return true;
    }catch(const std::exception& error){edit_status_=std::string("保存失败：")+error.what();return false;}
}

bool MaterialInspector::discard_changes() {
    try{
        asset_=load_mmat_json(path_);if(!load_document())return false;
        selected_material_=asset_.entries.empty()?0:std::clamp(selected_material_,0,static_cast<int>(asset_.entries.size()-1));
        dirty_=false;edit_status_="已放弃未保存修改";return true;
    }catch(const std::exception& error){edit_status_=std::string("重新加载失败：")+error.what();return false;}
}

bool MaterialInspector::propagate_selected_material_settings() {
    if(asset_.legacy_schema||document_.empty()||asset_.entries.empty()){
        edit_status_="批量覆盖失败：当前不是可编辑的新 schema MMAT";return false;
    }
    if(!save_document())return false;
    try{
        const auto selected=static_cast<std::size_t>(std::clamp(selected_material_,0,static_cast<int>(asset_.entries.size()-1)));
        const auto updated=propagate_mmat_json(path_,selected);
        file_changed_=true;
        edit_status_="已将当前 MMAT JSON 完整覆盖到 "+std::to_string(updated)+" 个配色 JSON";
        return true;
    }catch(const std::exception& error){edit_status_=std::string("批量覆盖失败：")+error.what();return false;}
}

bool MaterialInspector::consume_file_changed() noexcept {
    const bool result=file_changed_;file_changed_=false;return result;
}

bool MaterialInspector::save_changes() {
    return save_document();
}

void MaterialInspector::reload_if_open(const std::filesystem::path& path) {
    if(path_!=path)return;
    dirty_=false;
    discard_changes();
}

void MaterialInspector::refresh_texture_names() {
    texture_names_=workspace_texture_names(unpack_root_);
}

void MaterialInspector::draw_quick_actions() {
    const bool available=!asset_.legacy_schema&&!document_.empty()&&!asset_.entries.empty()&&
                          !adjacent_mmat_variant_jsons(path_).empty();
    ImGui::BeginDisabled(!available);
    if(ImGui::Button("完整覆盖 JSON 到 0~10"))ImGui::OpenPopup("批量覆盖配色材质设置");
    ImGui::EndDisabled();
    if(ImGui::BeginPopupModal("批量覆盖配色材质设置",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const auto target_count=adjacent_mmat_variant_jsons(path_).size();
        ImGui::Text("将当前 MMAT JSON 完整覆盖到同目录其余 %zu 个配色 JSON。",target_count);
        ImGui::TextUnformatted("目标文件的全部材质、贴图、Granite、常量和配色字段都会被替换。");
        ImGui::Spacing();
        if(ImGui::Button("确认覆盖")){propagate_selected_material_settings();ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("取消"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void MaterialInspector::draw(PreviewRenderer& renderer,TextureGallery& texture_gallery,const Workspace& workspace) {
    ImGui::Text("%s | materials %zu | constant buffers %zu | float pool %zu",
                path_.filename().string().c_str(), asset_.entries.size(), asset_.constant_buffers.size(), asset_.shader_parameter_float_pool.size());
    ImGui::SameLine(0,14);
    if(dirty_)ImGui::TextColored(ImVec4(1.0f,.72f,.18f,1.0f),"未保存");else ImGui::TextColored(ImVec4(.35f,.85f,.48f,1.0f),"已保存");
    ImGui::SameLine(0,14);
    ImGui::BeginDisabled(!dirty_||asset_.legacy_schema||document_.empty());
    if(ImGui::Button("保存到 unpack"))save_document();
    ImGui::SameLine();if(ImGui::Button("放弃修改"))discard_changes();
    ImGui::EndDisabled();
    if(dirty_&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S)&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))save_document();
    if(!edit_status_.empty()){ImGui::SameLine(0,14);ImGui::TextDisabled("%s",edit_status_.c_str());}
    if (asset_.legacy_schema) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, .42f, .30f, 1.0f));
        ImGui::TextWrapped("旧 A1/A2/A4 JSON：该格式会丢失 Endless Ragnarok 常量缓冲和材质状态，已禁止构建。请在快捷操作中从 source 重新解码。");
        ImGui::PopStyleColor();
    }
    if (asset_.entries.empty()) { ImGui::TextUnformatted("该 mmat 没有材质条目。"); return; }

    ImGui::BeginChild("mmat_material_list", ImVec2(280, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
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
    auto& material = asset_.entries[selected];
    nlohmann::json* source_material=nullptr;
    if(!asset_.legacy_schema&&document_.contains("materials")&&document_["materials"].is_array()&&selected<document_["materials"].size()&&document_["materials"][selected].is_object())source_material=&document_["materials"][selected];
    const bool editable=source_material!=nullptr;
    const auto set_buffer_word=[&](std::size_t buffer_index,std::size_t word_index,std::uint32_t value){
        if(buffer_index>=asset_.constant_buffers.size()||word_index>=asset_.constant_buffers[buffer_index].words.size()||!document_.contains("constant_buffers")||!document_["constant_buffers"].is_array()||buffer_index>=document_["constant_buffers"].size())return;
        auto& source_buffer=document_["constant_buffers"][buffer_index]["buffer"];if(!source_buffer.is_array()||word_index>=source_buffer.size())return;
        asset_.constant_buffers[buffer_index].words[word_index]=value;source_buffer[word_index]=value;dirty_=true;edit_status_.clear();
    };
    const bool player_character = is_player_character_path(path_);
    const ImGuiTableFlags resizable_table_flags = ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    const ImGuiTableFlags resizable_scrolling_table_flags = resizable_table_flags | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTabBar("mmat_tabs")) {
        if (ImGui::BeginTabItem("渲染状态")) {
            if (ImGui::BeginTable("mmat_overview", 3, resizable_table_flags)) {
                ImGui::TableSetupColumn("字段", ImGuiTableColumnFlags_WidthFixed, 210);
                ImGui::TableSetupColumn("值",ImGuiTableColumnFlags_WidthFixed,260);
                ImGui::TableSetupColumn("当前意义",ImGuiTableColumnFlags_WidthFixed,520);ImGui::TableHeadersRow();
                const auto uint_row=[&](const char* label,const char* id,auto& value,const char* key,const char* meaning){
                    ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(!editable);
                    using Value=std::remove_reference_t<decltype(value)>;const auto type=sizeof(Value)==1?ImGuiDataType_U8:sizeof(Value)==2?ImGuiDataType_U16:ImGuiDataType_U32;
                    if(ImGui::InputScalar(id,type,&value,nullptr,nullptr,nullptr)){(*source_material)[key]=value;dirty_=true;edit_status_.clear();}ImGui::EndDisabled();ImGui::TableNextColumn();ImGui::TextWrapped("%s",meaning);
                };
                const auto bool_row=[&](const char* label,const char* id,bool& value,const char* key,const char* meaning){
                    ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();ImGui::BeginDisabled(!editable);
                    if(ImGui::Checkbox(id,&value)){(*source_material)[key]=value;dirty_=true;edit_status_.clear();}ImGui::EndDisabled();ImGui::TableNextColumn();ImGui::TextWrapped("%s",meaning);
                };
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("material hash");ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(!editable);
                if(ImGui::InputScalar("##material_hash",ImGuiDataType_U32,&material.material_name_hash,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal)){(*source_material)["unique_material_name_hash_maybe"]=material.material_name_hash;dirty_=true;edit_status_.clear();}ImGui::EndDisabled();ImGui::TableNextColumn();ImGui::TextUnformatted(hex32(material.material_name_hash).c_str());
                uint_row("shader_type","##shader_type",material.shader_type,"shader_type",shader_type_name(material.shader_type));
                uint_row("shader_sub_type","##shader_sub_type",material.shader_sub_type,"shader_sub_type",shader_sub_type_name(material.shader_sub_type));
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("shadow_type");ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(!editable);
                if(ImGui::BeginCombo("##shadow_type",shadow_type_name(material.shadow_type))){const char* names[]={"NoShadow","ShadowEnable_Unk1","ShadowEnable_AlphaBlend","ShadowEnable_NoAlphaBlend"};for(std::uint8_t value=0;value<4;++value)if(ImGui::Selectable(names[value],material.shadow_type==value)){material.shadow_type=value;(*source_material)["shadow_type"]=names[value];dirty_=true;edit_status_.clear();}ImGui::EndCombo();}ImGui::EndDisabled();ImGui::TableNextColumn();ImGui::TextWrapped("%s",shadow_type_name(material.shadow_type));
                bool_row("ignore_alpha","##ignore_alpha",material.ignore_alpha,"ignore_alpha","忽略材质 Alpha");
                bool_row("bool9","##bool9",material.bool9,"bool9","角色完整样本通常为 false");
                bool_row("bool10","##bool10",material.bool10,"bool10","角色完整样本通常为 true");
                bool_row("bool12","##bool12",material.bool12,"bool12","角色样本中与 shadow_type=3 同现");
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("g_TwoSided");ImGui::TableNextColumn();if(find_parameter(material,two_sided_shader_parameter_id))ImGui::TextUnformatted(parameter_enabled(material,two_sided_shader_parameter_id)?"1":"0");else ImGui::TextDisabled("未设置");ImGui::TableNextColumn();ImGui::TextUnformatted("在 Shader 参数页编辑");
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("Cutout 丢弃模式 (0x53F49792)");ImGui::TableNextColumn();ImGui::TextUnformatted(parameter_enabled(material,enable_alpha_shader_parameter_id)?"开启":"关闭 / 未设置");ImGui::TableNextColumn();ImGui::TextUnformatted("开启时按 Albedo Alpha 丢弃像素；关闭时不进入丢弃管线");
                ImGui::EndTable();
            }
            ImGui::Spacing();
            const auto draw_material_warning=[](const char* message){
                ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.0f,.72f,.26f,1.0f));
                ImGui::TextWrapped("%s",message);
                ImGui::PopStyleColor();
            };
            if (material.shadow_type == 2 && parameter_enabled(material, two_sided_shader_parameter_id))
                draw_material_warning("当前同时启用了透明混合和 g_TwoSided，这套角色材质组合没有原版用例。若只是制作 A0 Cutout，请使用 ignore_alpha=false 和 Cutout 丢弃模式=开启，并关闭 g_TwoSided。");
            if (player_character) {
                const auto* alpha_key = find_parameter(material, enable_alpha_shader_parameter_id);
                if(material.shader_type!=2){
                    if(!alpha_key)
                        draw_material_warning("缺少角色 Cutout 丢弃模式参数。建议从同类型的原版材质槽复制 0x53F49792；需要丢弃透明像素时设为 1，不需要时设为 0。");
                    else if(material.ignore_alpha&&alpha_key->value_or_offset!=0)
                        draw_material_warning("Cutout 设置互相冲突：丢弃模式已开启，但 ignore_alpha 仍为 true。要裁掉 Albedo 的透明像素，请把 ignore_alpha 改为 false；不需要 Cutout 就关闭丢弃模式。");
                    else if(!material.ignore_alpha&&alpha_key->value_or_offset==0)
                        draw_material_warning("Albedo Alpha 当前不会裁掉像素：ignore_alpha 已关闭，但 Cutout 丢弃模式仍为 0。要使用 A0 Cutout，请把 0x53F49792 改为 1。");
                }
                if(material.shadow_type==3&&!material.bool12)
                    draw_material_warning("特殊阴影裁切配置不完整：shadow_type=3 时应开启 bool12。若只需要普通 A0 Cutout，请改用 shadow_type=1、bool12=false。");
                else if(material.shadow_type!=3&&material.bool12)
                    draw_material_warning("bool12 在当前阴影模式下是多余的。普通材质请关闭 bool12；只有使用 shadow_type=3 的特殊方向裁切时才应开启。");
                if (material.shader_type == 5) {
                    const auto* directional_alpha = find_parameter(material, 0xA6EB1B34u);
                    const bool special_directional_cutout=material.shadow_type==3&&material.bool12;
                    if(!directional_alpha)
                        draw_material_warning("缺少 Metal 方向裁切参数 0xA6EB1B34。普通 Cutout 材质应保留该参数并设为 0；仅 shadow_type=3、bool12=true 时设为 1。");
                    else if(!special_directional_cutout&&directional_alpha->value_or_offset!=0)
                        draw_material_warning("普通 Cutout 多开了“方向裁切”(0xA6EB1B34)。它可能让透明边缘随模型方向变化；请将它关闭。普通 Cutout 只需要 ignore_alpha=false 和 Cutout 丢弃模式=1。");
                    else if(special_directional_cutout&&directional_alpha->value_or_offset==0)
                        draw_material_warning("特殊方向裁切没有完整开启：当前 shadow_type=3、bool12=true，但“方向裁切”(0xA6EB1B34)仍为 0。要保留这套特殊效果请将它开启，否则改回普通 Cutout 设置。");
                }
                if (material.bool9 || !material.bool10)
                    draw_material_warning("角色基础开关被改动：原版角色材质使用 bool9=false、bool10=true。除非正在复刻一个已验证的原版材质槽，否则建议恢复这两个值。");
                if (material.shadow_type != 2 && parameter_enabled(material, two_sided_shader_parameter_id))
                    draw_material_warning("角色 Shader 没有已确认的 g_TwoSided 用法，这个参数通常不能解决角色背面不显示。建议关闭它，并在模型网格中制作需要显示的背面。");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shader 参数")) {
            ImGui::TextDisabled("证据：A=RDEF/schema 命名；B=游戏运行时行为；C=全量样本推断；D=少量样本，待验证。");
            if (ImGui::BeginTable("mmat_parameters", 6, resizable_scrolling_table_flags, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Hash / 名称", ImGuiTableColumnFlags_WidthFixed, 300);
                ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("值 / 偏移", ImGuiTableColumnFlags_WidthFixed, 120);
                ImGui::TableSetupColumn("浮点值", ImGuiTableColumnFlags_WidthFixed, 260);
                ImGui::TableSetupColumn("意义", ImGuiTableColumnFlags_WidthFixed, 520);
                ImGui::TableSetupColumn("证据", ImGuiTableColumnFlags_WidthFixed, 360);
                ImGui::TableHeadersRow();
                for (std::size_t parameter_index=0;parameter_index<material.shader_parameters.size();++parameter_index) {
                    auto& parameter=material.shader_parameters[parameter_index];
                    nlohmann::json* source_parameter=nullptr;
                    if(editable&&source_material->contains("shader_params")&&(*source_material)["shader_params"].is_array()&&parameter_index<(*source_material)["shader_params"].size())source_parameter=&(*source_material)["shader_params"][parameter_index];
                    ImGui::PushID(static_cast<int>(parameter_index));
                    ImGui::TableNextRow();
                    const auto* reflected_name = reflected_parameter_name(parameter.hash);
                    const auto* display_name = reflected_name ? reflected_name : (parameter.name.empty() ? "<unknown>" : parameter.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%s\n%s", display_name, hex32(parameter.hash).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(material_value_type_name(parameter.type));
                    ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(source_parameter==nullptr);
                    if(parameter.type==MaterialValueType::u8&&parameter_is_boolean(parameter.hash)){
                        bool value=parameter.value_or_offset!=0;if(ImGui::Checkbox("##parameter_bool",&value)){parameter.value_or_offset=value?1u:0u;(*source_parameter)["value_or_offset"]=parameter.value_or_offset;dirty_=true;edit_status_.clear();}
                    }else if(parameter.type==MaterialValueType::u8||parameter.type==MaterialValueType::u16||parameter.type==MaterialValueType::unknown){
                        if(ImGui::InputScalar("##parameter_integer",ImGuiDataType_U16,&parameter.value_or_offset)){if(parameter.type==MaterialValueType::u8)parameter.value_or_offset=std::min<std::uint16_t>(parameter.value_or_offset,255u);(*source_parameter)["value_or_offset"]=parameter.value_or_offset;dirty_=true;edit_status_.clear();}
                    }else ImGui::Text("offset %u",parameter.value_or_offset);
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    if (parameter.floating_values.empty()) ImGui::TextDisabled("-");
                    else for (std::size_t component = 0; component < parameter.floating_values.size(); ++component) {
                        if(component)ImGui::SameLine(0,4);ImGui::SetNextItemWidth(std::max(64.0f,170.0f/static_cast<float>(parameter.floating_values.size())));
                        ImGui::BeginDisabled(!editable);const auto id="##parameter_float_"+std::to_string(component);
                        if(ImGui::InputFloat(id.c_str(),&parameter.floating_values[component],0,0,"%.7g")){
                            const auto pool_index=static_cast<std::size_t>(parameter.value_or_offset)+component;
                            if(pool_index<asset_.shader_parameter_float_pool.size()&&document_.contains("shader_param_float_data_pool")&&document_["shader_param_float_data_pool"].is_array()&&pool_index<document_["shader_param_float_data_pool"].size()){
                                asset_.shader_parameter_float_pool[pool_index]=parameter.floating_values[component];document_["shader_param_float_data_pool"][pool_index]=parameter.floating_values[component];dirty_=true;edit_status_.clear();
                            }
                        }ImGui::EndDisabled();
                    }
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(parameter_meaning(parameter));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(parameter_confidence(parameter));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("贴图与 Granite")) {
            const auto& texture_names=texture_names_;
            std::optional<std::size_t> delete_texture;
            if (ImGui::BeginTable("mmat_textures",7,resizable_scrolling_table_flags,ImVec2(0,std::max(180.0f,ImGui::GetContentRegionAvail().y*.58f)))) {
                ImGui::TableSetupScrollFreeze(0,1);
                ImGui::TableSetupColumn("预览",ImGuiTableColumnFlags_WidthFixed,90);
                ImGui::TableSetupColumn("Shader map",ImGuiTableColumnFlags_WidthFixed,240);
                ImGui::TableSetupColumn("贴图名",ImGuiTableColumnFlags_WidthFixed,300);
                ImGui::TableSetupColumn("意义",ImGuiTableColumnFlags_WidthFixed,200);
                ImGui::TableSetupColumn("证据",ImGuiTableColumnFlags_WidthFixed,280);
                ImGui::TableSetupColumn("已解包路径",ImGuiTableColumnFlags_WidthFixed,420);
                ImGui::TableSetupColumn("操作",ImGuiTableColumnFlags_WidthFixed,115);
                ImGui::TableHeadersRow();
                for(std::size_t texture_index=0;texture_index<material.texture_maps.size();++texture_index){
                    auto& texture=material.texture_maps[texture_index];
                    nlohmann::json* source_texture=nullptr;
                    if(editable&&source_material->contains("texture_maps")&&(*source_material)["texture_maps"].is_array()&&texture_index<(*source_material)["texture_maps"].size())source_texture=&(*source_material)["texture_maps"][texture_index];
                    const auto dds=resolve_texture_dds(unpack_root_,texture.texture_name,material.granite.has_value());
                    ImGui::PushID(static_cast<int>(texture_index));
                    ImGui::TableNextRow(ImGuiTableRowFlags_None,74.0f);
                    ImGui::TableNextColumn();
                    if(const auto* thumbnail=dds.empty()?nullptr:texture_gallery.thumbnail(renderer,dds)){
                        const float width=static_cast<float>(thumbnail->width),height=static_cast<float>(thumbnail->height);
                        const float scale=std::min(64.0f/std::max(1.0f,width),64.0f/std::max(1.0f,height));
                        const ImVec2 size{width*scale,height*scale};
                        const float x=ImGui::GetCursorPosX();ImGui::SetCursorPosX(x+(68.0f-size.x)*.5f);
                        image_on_checkerboard(reinterpret_cast<ImTextureID>(thumbnail->image.Get()),size,8.0f);
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
                    ImGui::TableNextColumn();ImGui::BeginDisabled(source_texture==nullptr);
                    if(input_text_value("##texture_name",texture.texture_name)){(*source_texture)["texture_name"]=texture.texture_name;dirty_=true;edit_status_.clear();}
                    ImGui::EndDisabled();texture_context_menu("##texture_context_name",dds);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(texture_meaning(texture));texture_context_menu("##texture_context_meaning",dds);
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s",texture_confidence(texture));texture_context_menu("##texture_context_confidence",dds);
                    ImGui::TableNextColumn();
                    if(dds.empty())ImGui::TextDisabled("未找到");else ImGui::TextWrapped("%s",utf8(dds.lexically_relative(unpack_root_)).c_str());
                    texture_context_menu("##texture_context_path",dds);
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(source_texture==nullptr);
                    if(ImGui::BeginCombo("##workspace_texture", "选择引用")){
                        for(const auto& name:texture_names)if(ImGui::Selectable(name.c_str(),texture.texture_name==name)){
                            texture.texture_name=name;(*source_texture)["texture_name"]=name;dirty_=true;edit_status_.clear();
                        }
                        ImGui::EndCombo();
                    }
                    if(ImGui::Button("删除行"))delete_texture=texture_index;
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if(delete_texture&&editable&&source_material->contains("texture_maps")&&(*source_material)["texture_maps"].is_array()){
                auto& source_maps=(*source_material)["texture_maps"];
                if(*delete_texture<source_maps.size()&&*delete_texture<material.texture_maps.size()){
                    source_maps.erase(source_maps.begin()+static_cast<std::ptrdiff_t>(*delete_texture));
                    material.texture_maps.erase(material.texture_maps.begin()+static_cast<std::ptrdiff_t>(*delete_texture));
                    dirty_=true;edit_status_.clear();
                }
            }
            ImGui::SeparatorText("添加贴图引用");
            new_texture_map_option_=std::clamp(new_texture_map_option_,0,static_cast<int>(texture_map_options.size()-1));
            const auto& new_map=texture_map_options[static_cast<std::size_t>(new_texture_map_option_)];
            ImGui::SetNextItemWidth(220);
            if(ImGui::BeginCombo("Shader map##new_texture_map",new_map.label)){
                for(int option=0;option<static_cast<int>(texture_map_options.size());++option){
                    const auto& candidate=texture_map_options[static_cast<std::size_t>(option)];
                    const auto label=std::string(candidate.label)+" ("+candidate.json_name+")";
                    if(ImGui::Selectable(label.c_str(),option==new_texture_map_option_))new_texture_map_option_=option;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();ImGui::SetNextItemWidth(330);
            ImGui::InputTextWithHint("贴图名##new_texture_name","例如 fp1400_face_lod0_outline",new_texture_name_.data(),new_texture_name_.size());
            ImGui::SameLine();
            if(ImGui::BeginCombo("##new_texture_choice","从工作区选择")){
                for(const auto& name:texture_names)if(ImGui::Selectable(name.c_str()))std::snprintf(new_texture_name_.data(),new_texture_name_.size(),"%s",name.c_str());
                ImGui::EndCombo();
            }
            const bool duplicate_map=std::any_of(material.texture_maps.begin(),material.texture_maps.end(),[&](const auto& texture){return texture.hash==new_map.hash;});
            ImGui::SameLine();ImGui::BeginDisabled(!editable||new_texture_name_[0]=='\0'||duplicate_map);
            if(ImGui::Button("添加引用")){
                if(!source_material->contains("texture_maps")||!(*source_material)["texture_maps"].is_array())(*source_material)["texture_maps"]=nlohmann::json::array();
                const std::string texture_name=new_texture_name_.data();
                (*source_material)["texture_maps"].push_back({{"shader_map_name_hash",new_map.json_name},{"texture_name",texture_name}});
                material.texture_maps.push_back({new_map.hash,new_map.json_name,texture_name});
                new_texture_name_.fill('\0');dirty_=true;edit_status_.clear();
            }
            ImGui::EndDisabled();
            if(duplicate_map){ImGui::SameLine();ImGui::TextDisabled("当前材质已有该 Shader map；请直接修改现有行的贴图名。");}
            ImGui::SeparatorText("Granite streaming");
            if (!material.granite) ImGui::TextUnformatted("未设置 granite_params：游戏将按普通 texture 路径加载。");
            else {
                auto& granite=*material.granite;nlohmann::json* source_granite=nullptr;
                if(editable&&source_material->contains("granite_params")&&(*source_material)["granite_params"].is_object())source_granite=&(*source_material)["granite_params"];
                const auto granite_u8=[&](const char* label,const char* id,std::uint8_t& value,const char* key){ImGui::TextUnformatted(label);ImGui::SameLine();ImGui::SetNextItemWidth(75);ImGui::BeginDisabled(source_granite==nullptr);if(ImGui::InputScalar(id,ImGuiDataType_U8,&value)){(*source_granite)[key]=value;dirty_=true;edit_status_.clear();}ImGui::EndDisabled();};
                granite_u8("tile set","##granite_tile",granite.tile_set_number,"tile_set_number");ImGui::SameLine(0,16);granite_u8("unk4","##granite_unk4",granite.unknown4,"unk4");ImGui::SameLine(0,16);granite_u8("unk5","##granite_unk5",granite.unknown5,"unk5");
                if(ImGui::BeginTable("##granite_fields",3,resizable_table_flags)){
                    ImGui::TableSetupColumn("类型",ImGuiTableColumnFlags_WidthFixed,90);ImGui::TableSetupColumn("索引",ImGuiTableColumnFlags_WidthFixed,65);ImGui::TableSetupColumn("值",ImGuiTableColumnFlags_WidthFixed,520);ImGui::TableHeadersRow();
                    for(std::size_t index=0;index<granite.page_files.size();++index){ImGui::PushID(static_cast<int>(index));ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("page");ImGui::TableNextColumn();ImGui::Text("%zu",index);ImGui::TableNextColumn();ImGui::BeginDisabled(source_granite==nullptr);if(input_text_value("##granite_page",granite.page_files[index])){(*source_granite)["page_file"][index]=granite.page_files[index];dirty_=true;edit_status_.clear();}ImGui::EndDisabled();ImGui::PopID();}
                    for(std::size_t index=0;index<granite.layer_names.size();++index){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("layer");ImGui::TableNextColumn();ImGui::Text("%zu",index);ImGui::TableNextColumn();ImGui::Text("%s (%s)",granite.layer_names[index].empty()?"<unknown>":granite.layer_names[index].c_str(),index<granite.layer_hashes.size()?hex32(granite.layer_hashes[index]).c_str():"<missing>");}
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("常量缓冲")) {
            const auto* layout = gbfr::editor::mmat_catalog::find(material.shader_type);
            gbfr::MaterialConstantBuffer* param_buffer = nullptr;
            std::size_t param_buffer_index = 0;
            if (layout && !material.constant_buffer_indices.empty()) {
                param_buffer_index = material.constant_buffer_indices.front();
                if (param_buffer_index < asset_.constant_buffers.size() &&
                    asset_.constant_buffers[param_buffer_index].words.size() * sizeof(std::uint32_t) == layout->size)
                    param_buffer = &asset_.constant_buffers[param_buffer_index];
            }
            if (param_buffer) {
                ImGui::SeparatorText("ParamBuffer（字段 A / 绑定 C）");
                ImGui::TextWrapped("%s | buffer %zu | %u bytes", layout->shader, param_buffer_index, layout->size);
                ImGui::TextDisabled("字段名：A - DXBC RDEF；绑定：%s", layout->binding_evidence);
                const auto reflected_height = std::clamp(ImGui::GetContentRegionAvail().y * .55f, 180.0f, 480.0f);
                if (ImGui::BeginTable("mmat_reflected_param_buffer", 5,
                    resizable_scrolling_table_flags, ImVec2(0, reflected_height))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("字段", ImGuiTableColumnFlags_WidthFixed, 260);
                    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthFixed, 380);
                    ImGui::TableSetupColumn("用途（RDEF 名称直译）", ImGuiTableColumnFlags_WidthFixed, 360);
                    ImGui::TableHeadersRow();
                    for (const auto& field : layout->fields) {
                        ImGui::PushID(field.name);
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
                        else if (field.type == FieldType::boolean){
                            bool value=param_buffer->words[first_word]!=0;ImGui::BeginDisabled(!editable);if(ImGui::Checkbox("##field_bool",&value))set_buffer_word(param_buffer_index,first_word,value?1u:0u);ImGui::EndDisabled();
                        }else for(std::size_t component=0;component<components;++component){
                            if(component)ImGui::SameLine(0,4);ImGui::SetNextItemWidth(std::max(58.0f,245.0f/static_cast<float>(components)));ImGui::BeginDisabled(!editable);
                            const auto id="##field_"+std::to_string(component);
                            if(field.type==FieldType::signed_integer){auto value=static_cast<std::int32_t>(param_buffer->words[first_word+component]);if(ImGui::InputScalar(id.c_str(),ImGuiDataType_S32,&value))set_buffer_word(param_buffer_index,first_word+component,static_cast<std::uint32_t>(value));}
                            else if(field.type==FieldType::unsigned_integer){auto value=param_buffer->words[first_word+component];if(ImGui::InputScalar(id.c_str(),ImGuiDataType_U32,&value))set_buffer_word(param_buffer_index,first_word+component,value);}
                            else{auto value=std::bit_cast<float>(param_buffer->words[first_word+component]);if(ImGui::InputFloat(id.c_str(),&value,0,0,"%.7g"))set_buffer_word(param_buffer_index,first_word+component,std::bit_cast<std::uint32_t>(value));}
                            ImGui::EndDisabled();
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("%s", material.shader_type >= 2 && material.shader_type <= 6 ? character_param_buffer_meaning(field.name) : "-");
                        ImGui::PopID();
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
            if (ImGui::BeginTable("mmat_buffers", 5, resizable_scrolling_table_flags, ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Buffer", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Name hash", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthFixed, 75);
                ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Float reinterpret", ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableHeadersRow();
                for (std::size_t buffer_index = 0; buffer_index < asset_.constant_buffers.size(); ++buffer_index) {
                    auto& buffer = asset_.constant_buffers[buffer_index];
                    for (std::size_t word_index = 0; word_index < buffer.words.size(); ++word_index) {
                        ImGui::PushID(static_cast<int>(buffer_index*10000+word_index));
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%zu%s", buffer_index,
                            std::find(material.constant_buffer_indices.begin(), material.constant_buffer_indices.end(), buffer_index) != material.constant_buffer_indices.end() ? " *" : "");
                        ImGui::TableNextColumn();if(word_index==0){ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(!editable);if(ImGui::InputScalar("##buffer_hash",ImGuiDataType_U32,&buffer.name_hash,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal)){document_["constant_buffers"][buffer_index]["unk_unique_param_name_hash"]=buffer.name_hash;dirty_=true;edit_status_.clear();}ImGui::EndDisabled();}else ImGui::TextDisabled("\"");
                        ImGui::TableNextColumn(); ImGui::Text("%zu", word_index);
                        ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);auto raw=buffer.words[word_index];ImGui::BeginDisabled(!editable);if(ImGui::InputScalar("##buffer_raw",ImGuiDataType_U32,&raw,nullptr,nullptr,"%08X",ImGuiInputTextFlags_CharsHexadecimal))set_buffer_word(buffer_index,word_index,raw);ImGui::EndDisabled();
                        ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);auto floating=std::bit_cast<float>(buffer.words[word_index]);ImGui::BeginDisabled(!editable);if(ImGui::InputFloat("##buffer_float",&floating,0,0,"%.9g"))set_buffer_word(buffer_index,word_index,std::bit_cast<std::uint32_t>(floating));ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("文件信息")) {
            ImGui::Text("magic %u%s", asset_.magic, asset_.magic == 20230727u ? " (valid)" : " (unexpected)");
            ImGui::BeginDisabled(!editable);
            ImGui::SetNextItemWidth(90);if(ImGui::InputScalar("root unk2",ImGuiDataType_U8,&asset_.unknown2)){document_["unk2"]=asset_.unknown2;dirty_=true;edit_status_.clear();}
            ImGui::SameLine();if(ImGui::Checkbox("bool3",&asset_.bool3)){document_["bool3"]=asset_.bool3;dirty_=true;edit_status_.clear();}
            ImGui::SameLine();if(ImGui::Checkbox("bool4",&asset_.bool4)){document_["bool4"]=asset_.bool4;dirty_=true;edit_status_.clear();}
            ImGui::SameLine();if(ImGui::Checkbox("bool5",&asset_.bool5)){document_["bool5"]=asset_.bool5;dirty_=true;edit_status_.clear();}
            ImGui::EndDisabled();
            ImGui::SeparatorText("shader_param_float_data_pool");
            if(asset_.shader_parameter_float_pool.empty())ImGui::TextDisabled("空");
            else if(ImGui::BeginTable("mmat_float_pool",2,resizable_table_flags)){
                ImGui::TableSetupColumn("Index",ImGuiTableColumnFlags_WidthFixed,90);ImGui::TableSetupColumn("Float",ImGuiTableColumnFlags_WidthFixed,240);ImGui::TableHeadersRow();
                for(std::size_t index=0;index<asset_.shader_parameter_float_pool.size();++index){ImGui::PushID(static_cast<int>(index));ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%zu",index);ImGui::TableNextColumn();ImGui::SetNextItemWidth(-FLT_MIN);ImGui::BeginDisabled(!editable);if(ImGui::InputFloat("##float_pool",&asset_.shader_parameter_float_pool[index],0,0,"%.9g")){document_["shader_param_float_data_pool"][index]=asset_.shader_parameter_float_pool[index];for(auto& entry:asset_.entries)for(auto& parameter:entry.shader_parameters)if(index>=parameter.value_or_offset&&index<static_cast<std::size_t>(parameter.value_or_offset)+parameter.floating_values.size())parameter.floating_values[index-parameter.value_or_offset]=asset_.shader_parameter_float_pool[index];dirty_=true;edit_status_.clear();}ImGui::EndDisabled();ImGui::PopID();}
                ImGui::EndTable();
            }
            ImGui::TextWrapped("原始常量缓冲编辑按 32 位位模式写回；修改未知字段前请保留原版对照。保存不会删除尚未解析的 JSON 字段。");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}
}
