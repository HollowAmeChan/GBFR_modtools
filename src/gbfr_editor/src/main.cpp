#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/material.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/formats/animation.hpp>
#include <gbfr/render/d3d11_context.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <charconv>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <array>
#include <cctype>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
gbfr::D3D11Context* g_d3d = nullptr;
gbfr::PreviewRenderer* g_preview = nullptr;
std::unique_ptr<gbfr::Workspace> g_workspace;
std::string g_imgui_ini;
std::optional<std::size_t> g_selected_asset;
bool g_changed_only = false;
enum class AssetFunction { all, model, texture, material, cloth };
enum class PreviewMode { none, model, texture };
struct ModelPreviewKey {
    std::filesystem::path minfo;
    std::filesystem::path skeleton;
    std::filesystem::path mesh;
    bool operator==(const ModelPreviewKey&) const = default;
};
AssetFunction g_asset_function = AssetFunction::all;
std::array<char,256> g_asset_search{};
PreviewMode g_preview_mode = PreviewMode::none;
std::optional<ModelPreviewKey> g_loaded_model;
std::filesystem::path g_loaded_texture;
gbfr::OrbitCamera g_camera;
gbfr::SkeletonAsset g_skeleton;
bool g_show_mesh = true;
gbfr::PreviewShadingMode g_preview_shading = gbfr::PreviewShadingMode::lit;
bool g_show_skeleton = true;
bool g_show_collisions = true;
std::vector<std::filesystem::path> g_motion_files;
std::optional<gbfr::AnimationClip> g_motion;
std::array<char,128> g_motion_search{};
int g_selected_motion = -1;
float g_motion_frame = 0.0f;
float g_motion_speed = 1.0f;
float g_applied_motion_frame = -1.0f;
bool g_motion_playing = false;
bool g_motion_loop = true;
struct ClhFile { std::filesystem::path path; gbfr::ClhAsset data; };
struct ClpFile { std::filesystem::path path; gbfr::ClpAsset data; };
std::vector<ClhFile> g_clh_files;
std::vector<ClpFile> g_clp_files;
std::unordered_map<std::string,std::string> g_bone_names;
int g_selected_clh = 0;
int g_selected_bone = -1;
int g_selected_collision = -1;
bool g_all_clh_files = false;
bool g_all_bones = false;
bool g_reset_layout = true;
bool g_start_layout_built = false;
std::array<char,32768> g_minfo_path{};
std::array<char,32768> g_workspace_path{};
HANDLE g_extract_process = nullptr;
std::string g_start_status;

void clear_motion_state();

std::filesystem::path tool_root() {
    wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768);
    auto path=std::filesystem::path(module).parent_path();
    if(path.filename()==L"Debug"||path.filename()==L"Release"||path.filename()==L"RelWithDebInfo") path=path.parent_path().parent_path().parent_path();
    return path;
}

std::filesystem::path preview_shader_file() {
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    const auto installed=std::filesystem::path(module).parent_path()/L"shaders/preview.hlsl";
    if(std::filesystem::is_regular_file(installed))return installed;
    return tool_root()/L"assets/shaders/preview.hlsl";
}

int cloth_bone_id(const std::string& name) {
    if(name.size()<2||name[0]!='_') return -1; int value{}; const auto result=std::from_chars(name.data()+1,name.data()+name.size(),value,16); return result.ec==std::errc{}&&result.ptr==name.data()+name.size()?value:-1;
}

std::string bone_display(const std::string& name) {
    const auto found=g_bone_names.find(name); return found==g_bone_names.end()?name:found->second+" ("+name+")";
}

void load_bone_names() {
    try { std::ifstream input(tool_root()/L"_lib/humanoid_bone_names.json"); nlohmann::json json;input>>json;for(auto it=json.begin();it!=json.end();++it)g_bone_names[it.key()]=it.value().get<std::string>(); } catch(...) {}
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string output(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::wstring wide(const std::string& value) {
    if(value.empty()) return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring output(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),output.data(),size);
    return output;
}

void set_path_input(std::array<char,32768>& input,const std::filesystem::path& path) {
    const auto value=utf8(path.wstring());
    strncpy_s(input.data(),input.size(),value.c_str(),_TRUNCATE);
}

bool load_workspace(const std::filesystem::path& path) {
    try {
        g_workspace = std::make_unique<gbfr::Workspace>(gbfr::Workspace::load(path));
        clear_motion_state();
        g_selected_asset.reset();
        g_asset_function=AssetFunction::all;g_asset_search.fill('\0');
        g_preview_mode=PreviewMode::none;g_loaded_model.reset();g_loaded_texture.clear();
        g_skeleton.bones.clear(); g_clh_files.clear(); g_clp_files.clear();
        if(g_preview) g_preview->clear();
        const auto settings_directory=g_workspace->root()/L".gbfr";
        std::filesystem::create_directories(settings_directory);
        const auto settings_file=settings_directory/L"imgui_v3.ini";
        g_imgui_ini=utf8(settings_file.wstring());
        ImGui::GetIO().IniFilename=g_imgui_ini.c_str();
        if(std::filesystem::is_regular_file(settings_file)) {
            ImGui::LoadIniSettingsFromDisk(g_imgui_ini.c_str());
            g_reset_layout=false;
        } else g_reset_layout=true;
        g_start_layout_built=false;
        gbfr::Log::write(gbfr::LogLevel::info, "工作区已加载：" + utf8(g_workspace->root().wstring()));
        return true;
    } catch (const std::exception& error) {
        gbfr::Log::write(gbfr::LogLevel::error, std::string("工作区加载失败：") + error.what());
        return false;
    }
}

void choose_minfo_path() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.lpstrFilter=L"GBFR Model Info (*.minfo)\0*.minfo\0All files\0*.*\0";
    dialog.lpstrFile=path;
    dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(GetOpenFileNameW(&dialog)) set_path_input(g_minfo_path,path);
}

void choose_workspace() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.lpstrFilter = L"GBFR Workspace\0manifest.md;workspace.json\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog)) load_workspace(path);
}

void choose_workspace_path() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.lpstrFilter=L"GBFR Workspace (workspace.json)\0workspace.json\0JSON files\0*.json\0";
    dialog.lpstrFile=path;
    dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(GetOpenFileNameW(&dialog)) set_path_input(g_workspace_path,path);
}

bool start_workspace_extraction() {
    if(g_extract_process) return false;
    const std::filesystem::path minfo=wide(g_minfo_path.data());
    if(!std::filesystem::is_regular_file(minfo)||minfo.extension()!=L".minfo") { g_start_status="请选择有效的原始 .minfo 文件。"; return false; }
    const auto script=tool_root()/L"explore_char.ps1";
    if(!std::filesystem::is_regular_file(script)) { g_start_status="找不到 explore_char.ps1。"; return false; }
    std::wstring command=L"powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File \""+script.wstring()+L"\" \""+minfo.wstring()+L"\"";
    std::vector<wchar_t> mutable_command(command.begin(),command.end()); mutable_command.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    const auto cwd=tool_root().wstring();
    if(!CreateProcessW(nullptr,mutable_command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&startup,&process)) { g_start_status="无法启动资源抽取进程，错误码 "+std::to_string(GetLastError()); return false; }
    CloseHandle(process.hThread); g_extract_process=process.hProcess; g_start_status="正在抽取并生成工作区，请等待..."; return true;
}

void poll_workspace_extraction() {
    if(!g_extract_process) return;
    DWORD exit_code{};
    if(!GetExitCodeProcess(g_extract_process,&exit_code)||exit_code==STILL_ACTIVE) return;
    CloseHandle(g_extract_process); g_extract_process=nullptr;
    if(exit_code!=0) { g_start_status="工作区生成失败，退出码 "+std::to_string(exit_code)+"。"; return; }
    const auto workspace=tool_root()/L"explore_output/workspace.json";
    if(!std::filesystem::is_regular_file(workspace)) { g_start_status="抽取完成，但没有生成 workspace.json。"; return; }
    set_path_input(g_workspace_path,workspace);
    g_start_status="工作区生成完成。";
    load_workspace(workspace);
}

bool load_model_preview(std::size_t index,bool force=false);

void run_selected_model_action(bool restore) {
    if (!g_workspace || !g_selected_asset) return;
    try {
        if (restore) g_workspace->restore_model(*g_selected_asset);
        else g_workspace->build_model(*g_selected_asset);
        if(restore) load_model_preview(*g_selected_asset,true);
        gbfr::Log::write(gbfr::LogLevel::info, restore ? "模型文件已恢复" : "模型文件已写入 build");
    } catch (const std::exception& error) {
        gbfr::Log::write(gbfr::LogLevel::error, error.what());
    }
}

void launch_legacy_builder() {
    if (!g_workspace) return;
    const auto launcher = tool_root() / L"_lib" / L"launch_workspace_builder.vbs";
    const std::wstring arguments = L"\"" + launcher.wstring() + L"\" \"" + (g_workspace->root() / L"manifest.md").wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"wscript.exe", arguments.c_str(), nullptr, SW_HIDE);
}

void update_collision_debug();

std::filesystem::path resolve_base_albedo(const std::filesystem::path& workspace_root,const std::string& texture_name) {
    if(texture_name.empty()||gbfr::is_color_variant_texture(texture_name)) return {};
    const auto unpack=workspace_root/L"unpack";
    const std::array directories={unpack/L"data/granite/2k",unpack/L"data/texture/2k",unpack/L"data/granite/4k",unpack/L"data/texture/4k"};
    const auto wide_name=wide(texture_name);
    const std::array filenames={wide_name+L".dds",wide_name+L"_0.dds"};
    for(const auto& directory:directories) for(const auto& filename:filenames) {
        const auto candidate=directory/filename;
        if(std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

void clear_motion_state() {
    g_motion_files.clear();g_motion.reset();g_motion_search.fill('\0');g_selected_motion=-1;g_motion_frame=0.0f;g_applied_motion_frame=-1.0f;g_motion_playing=false;
}

void discover_motions(const ModelPreviewKey& key) {
    clear_motion_state();
    if(!g_workspace)return;
    const auto model_id=key.minfo.stem().wstring();
    if(model_id.size()<2)return;
    const auto prefix=model_id.substr(0,2);
    if(prefix!=L"pl"&&prefix!=L"fp")return;
    const auto directory=g_workspace->root()/L"source/data"/prefix/model_id;
    if(!std::filesystem::is_directory(directory))return;
    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().extension()==L".mot")g_motion_files.push_back(entry.path());
    std::sort(g_motion_files.begin(),g_motion_files.end(),[](const auto& a,const auto& b){return gbfr::natural_less_case_insensitive(a.filename().native(),b.filename().native());});
}

bool select_motion(int index) {
    if(!g_preview||index<0||index>=static_cast<int>(g_motion_files.size()))return false;
    try {
        auto motion=gbfr::load_mot(g_motion_files[static_cast<std::size_t>(index)]);
        if(!g_preview->apply_animation(&motion,0.0f))throw std::runtime_error("动画姿态应用失败");
        g_motion=std::move(motion);g_selected_motion=index;g_motion_frame=0.0f;g_applied_motion_frame=0.0f;g_motion_playing=false;
        update_collision_debug();
        gbfr::Log::write(gbfr::LogLevel::info,"动画已加载："+g_motion->name+"，"+std::to_string(g_motion->frame_count)+" 帧");
        return true;
    } catch(const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::error,std::string("动画加载失败：")+error.what());return false; }
}

void reset_motion_pose() {
    g_motion.reset();g_selected_motion=-1;g_motion_frame=0.0f;g_applied_motion_frame=-1.0f;g_motion_playing=false;
    if(g_preview&&g_preview->apply_animation(nullptr,0.0f)) {
        if(g_show_collisions)update_collision_debug();
    }
}

ModelPreviewKey resolve_model_preview_key(const gbfr::WorkspaceAsset& selected) {
    if(!g_workspace||selected.kind!=gbfr::AssetKind::model) throw std::runtime_error("选中项不是模型文件");
    ModelPreviewKey key;
    const auto stem=selected.input.stem();
    for(const auto& asset:g_workspace->assets()) {
        if(asset.kind!=gbfr::AssetKind::model||asset.input.stem()!=stem) continue;
        if(asset.subtype=="minfo") key.minfo=asset.input;
        else if(asset.subtype=="skeleton") key.skeleton=asset.input;
        else if(asset.subtype=="mmesh") key.mesh=asset.input;
    }
    if(key.minfo.empty()) throw std::runtime_error("找不到同名 minfo");
    if(key.skeleton.empty()) throw std::runtime_error("找不到同名 skeleton");
    if(key.mesh.empty()) throw std::runtime_error("找不到同名 LOD0 mmesh");
    return key;
}

bool load_model_preview(std::size_t index,bool force) {
    if (!g_workspace || index>=g_workspace->assets().size() || !g_preview) return false;
    const auto& selected = g_workspace->assets()[index];
    if (selected.kind != gbfr::AssetKind::model) return false;
    try {
        const auto key=resolve_model_preview_key(selected);
        if(!force&&g_loaded_model&&*g_loaded_model==key&&g_preview->has_model()) {g_preview_mode=PreviewMode::model;return true;}
        const auto info = gbfr::load_minfo(key.minfo);
        const auto skeleton=gbfr::load_skeleton(key.skeleton);
        const auto mesh = gbfr::load_mmesh(key.mesh, info);
        gbfr::SopAsset sop;
        auto sop_path=g_workspace->root()/L"source/data"/key.minfo.lexically_relative(g_workspace->root()/L"unpack/data");
        sop_path.replace_extension(L".sop");
        if(std::filesystem::is_regular_file(sop_path))sop=gbfr::load_sop(sop_path);
        const auto material_json=key.minfo.parent_path()/L"vars/0.mmat.json";
        if(!std::filesystem::is_regular_file(material_json)) throw std::runtime_error("找不到 vars/0.mmat.json");
        const auto materials=gbfr::load_mmat_json(material_json);
        std::vector<gbfr::PreviewMaterialTextures> preview_materials(materials.entries.size());
        std::size_t resolved_materials{};
        for(std::size_t i=0;i<materials.entries.size();++i) {
            const auto& entry=materials.entries[i];auto& preview=preview_materials[i];
            preview.albedo=resolve_base_albedo(g_workspace->root(),entry.albedo_name);
            preview.eye_conjunctiva=resolve_base_albedo(g_workspace->root(),entry.eye_conjunctiva_name);
            preview.eye_iris=resolve_base_albedo(g_workspace->root(),entry.eye_iris_name);
            preview.eye_highlight=resolve_base_albedo(g_workspace->root(),entry.eye_highlight_name);
            preview.eye_mask=resolve_base_albedo(g_workspace->root(),entry.eye_mask_name);
            if(entry.alpha_blended)preview.alpha_mask=resolve_base_albedo(g_workspace->root(),entry.alpha_mask_name);
            preview.alpha_blended=entry.alpha_blended;
            if(!preview.albedo.empty()||(!preview.eye_conjunctiva.empty()&&!preview.eye_iris.empty()&&!preview.eye_highlight.empty()))++resolved_materials;
        }
        for(const auto& chunk:mesh.chunks) if(chunk.material>=materials.entries.size()) throw std::runtime_error("minfo MaterialID 超出 0.mmat 条目范围");
        if (!g_preview->load(mesh, skeleton, preview_materials, sop)) throw std::runtime_error("GPU 预览资源创建失败");
        g_skeleton=skeleton;g_loaded_model=key;g_loaded_texture.clear();g_preview_mode=PreviewMode::model;
        discover_motions(key);
        g_preview->frame(g_camera);
        g_clh_files.clear(); g_clp_files.clear(); g_selected_collision=-1; g_selected_bone=-1; g_selected_clh=0;
        const auto model_id=key.minfo.stem().wstring();
        const auto cloth_prefix=model_id+L"_";
        for(const auto& asset:g_workspace->assets()) if(asset.kind==gbfr::AssetKind::cloth&&asset.available&&asset.input.filename().wstring().starts_with(cloth_prefix)) {
            try { if(asset.subtype=="clh") g_clh_files.push_back({asset.input,gbfr::load_clh(asset.input)}); else if(asset.subtype=="clp") g_clp_files.push_back({asset.input,gbfr::load_clp(asset.input)}); } catch(const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::warning,std::string("cloth 跳过：")+error.what()); }
        }
        update_collision_debug();
        gbfr::Log::write(gbfr::LogLevel::info, "预览已加载：" + std::to_string(mesh.vertices.size()) + " 顶点，" + std::to_string(mesh.indices.size()/3) + " 三角形，" + std::to_string(mesh.chunks.size()) + " 材质分段，SOP " + std::to_string(sop.operations.size()) + " 操作，0.mmat 可见材质 " + std::to_string(resolved_materials) + "/" + std::to_string(materials.entries.size()));
        return true;
    } catch (const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::error, std::string("预览加载失败：") + error.what());return false; }
}

void preview_asset(std::size_t index) {
    if(!g_workspace||index>=g_workspace->assets().size()||!g_preview)return;
    const auto& asset=g_workspace->assets()[index];
    if(asset.kind==gbfr::AssetKind::model){load_model_preview(index);return;}
    if(asset.kind==gbfr::AssetKind::texture||asset.kind==gbfr::AssetKind::new_texture){
        if(!asset.available||asset.input.extension()!=L".dds"){g_preview_mode=PreviewMode::none;return;}
        if(g_loaded_texture==asset.input&&g_preview->texture_image()){g_preview_mode=PreviewMode::texture;return;}
        if(g_preview->load_texture_preview(asset.input)){g_loaded_texture=asset.input;g_preview_mode=PreviewMode::texture;gbfr::Log::write(gbfr::LogLevel::info,"DDS 预览已加载："+utf8(asset.input.filename().wstring()));}
        else {g_preview_mode=PreviewMode::none;gbfr::Log::write(gbfr::LogLevel::error,"DDS 预览加载失败："+utf8(asset.input.wstring()));}
        return;
    }
    g_preview_mode=PreviewMode::none;
}

bool asset_matches_function(const gbfr::WorkspaceAsset& asset,AssetFunction function) {
    switch(function){
    case AssetFunction::all:return true;
    case AssetFunction::model:return asset.kind==gbfr::AssetKind::model;
    case AssetFunction::texture:return asset.kind==gbfr::AssetKind::texture||asset.kind==gbfr::AssetKind::new_texture;
    case AssetFunction::material:return asset.kind==gbfr::AssetKind::material;
    case AssetFunction::cloth:return asset.kind==gbfr::AssetKind::cloth;
    }
    return false;
}

bool contains_ascii_case_insensitive(std::string value,std::string query) {
    const auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};
    std::transform(value.begin(),value.end(),value.begin(),lower);std::transform(query.begin(),query.end(),query.begin(),lower);
    return value.find(query)!=std::string::npos;
}

bool asset_matches_search(const gbfr::WorkspaceAsset& asset) {
    if(!g_asset_search[0])return true;
    const std::string query=g_asset_search.data();
    return contains_ascii_case_insensitive(utf8(asset.input.filename().wstring()),query)||
           contains_ascii_case_insensitive(utf8(asset.output.wstring()),query)||
           contains_ascii_case_insensitive(asset.subtype,query);
}

std::optional<gbfr::Vec3> collision_point(int id,const gbfr::Vec4& offset) {
    const auto* pose=g_preview?&g_preview->bone_positions():nullptr;
    for(std::size_t i=0;i<g_skeleton.bones.size();++i) if(cloth_bone_id(g_skeleton.bones[i].name)==id) {const auto& p=pose&&i<pose->size()?(*pose)[i]:g_skeleton.bones[i].world_position;return gbfr::Vec3{p.x+offset.x,p.y+offset.y,p.z+offset.z};}
    return std::nullopt;
}

void append_circle(std::vector<gbfr::Vec3>& lines,gbfr::Vec3 center,float radius,int plane) {
    constexpr int segments=20; constexpr float tau=6.283185307f;
    for(int i=0;i<segments;++i){const float a=tau*i/segments,b=tau*(i+1)/segments;gbfr::Vec3 p=center,q=center;const float ca=std::cos(a)*radius,sa=std::sin(a)*radius,cb=std::cos(b)*radius,sb=std::sin(b)*radius;if(plane==0){p.x+=ca;p.y+=sa;q.x+=cb;q.y+=sb;}else if(plane==1){p.x+=ca;p.z+=sa;q.x+=cb;q.z+=sb;}else{p.y+=ca;p.z+=sa;q.y+=cb;q.z+=sb;}lines.push_back(p);lines.push_back(q);}
}

void update_collision_debug() {
    if(!g_preview)return;std::vector<gbfr::Vec3> lines;
    for(std::size_t file_index=0;file_index<g_clh_files.size();++file_index){if(!g_all_clh_files&&static_cast<int>(file_index)!=g_selected_clh)continue;for(const auto& c:g_clh_files[file_index].data.collisions){if(!g_all_bones&&g_selected_bone>=0&&c.p1!=g_selected_bone&&c.p2!=g_selected_bone)continue;const auto p=collision_point(c.p1,c.offset1),q=collision_point(c.p2,c.offset2);if(!p||!q)continue;lines.push_back(*p);lines.push_back(*q);append_circle(lines,*p,c.radius,0);append_circle(lines,*p,c.radius,1);append_circle(lines,*p,c.radius,2);if(c.p1!=c.p2||c.offset1.x!=c.offset2.x||c.offset1.y!=c.offset2.y||c.offset1.z!=c.offset2.z){append_circle(lines,*q,c.radius,0);append_circle(lines,*q,c.radius,1);append_circle(lines,*q,c.radius,2);}}}g_preview->set_collision_lines(lines);
}

void save_selected_collision() {
    if(g_selected_clh<0||g_selected_clh>=static_cast<int>(g_clh_files.size())||g_selected_collision<0||g_selected_collision>=static_cast<int>(g_clh_files[g_selected_clh].data.collisions.size()))return;
    try{auto& file=g_clh_files[g_selected_clh];gbfr::save_clh_collision(file.path,file.data.collisions[g_selected_collision]);if(g_workspace)g_workspace->refresh();gbfr::Log::write(gbfr::LogLevel::info,"CLH 碰撞已写回 unpack");update_collision_debug();}catch(const std::exception& error){gbfr::Log::write(gbfr::LogLevel::error,std::string("CLH 保存失败：")+error.what());}
}

LRESULT WINAPI window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return true;
    switch (message) {
    case WM_SIZE:
        if (g_d3d && wparam != SIZE_MINIMIZED) g_d3d->resize(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void build_default_dock_layout(ImGuiID dockspace) {
    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspace,viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace,viewport->WorkSize);

    ImGuiID center=dockspace,left{},right{},bottom{},right_top{},right_bottom{};
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.24f,&left,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.38f,&right,&center);
    ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.23f,&bottom,&center);
    ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,.32f,&right_top,&right_bottom);

    ImGui::DockBuilderDockWindow("Workspace",left);
    ImGui::DockBuilderDockWindow("Viewport",center);
    ImGui::DockBuilderDockWindow("Inspector",right_top);
    ImGui::DockBuilderDockWindow("Skeleton & Cloth",right_bottom);
    ImGui::DockBuilderDockWindow("Migration Coverage",bottom);
    ImGui::DockBuilderDockWindow("Log",bottom);
    ImGui::DockBuilderFinish(dockspace);
    g_reset_layout=false;
}

void build_start_dock_layout(ImGuiID dockspace) {
    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspace,viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace,viewport->WorkSize);
    ImGui::DockBuilderDockWindow("开始",dockspace);
    ImGui::DockBuilderFinish(dockspace);
    g_start_layout_built=true;
}

void draw_motion_controls() {
    const std::string current=g_selected_motion>=0?utf8(g_motion_files[static_cast<std::size_t>(g_selected_motion)].filename().wstring()):"静止姿态";
    ImGui::SetNextItemWidth(220.0f);
    if(ImGui::BeginCombo("##motion",current.c_str())){
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##motion_search","筛选动画名称",g_motion_search.data(),g_motion_search.size());
        if(ImGui::Selectable("静止姿态",g_selected_motion<0))reset_motion_pose();
        const std::string filter=g_motion_search.data();
        for(int i=0;i<static_cast<int>(g_motion_files.size());++i){
            const auto name=utf8(g_motion_files[static_cast<std::size_t>(i)].filename().wstring());
            if(!filter.empty()&&name.find(filter)==std::string::npos)continue;
            if(ImGui::Selectable(name.c_str(),i==g_selected_motion))select_motion(i);
        }
        ImGui::EndCombo();
    }
    const bool available=g_motion.has_value();
    ImGui::SameLine();ImGui::BeginDisabled(!available);
    if(ImGui::Button(g_motion_playing?"暂停":"播放"))g_motion_playing=!g_motion_playing;
    ImGui::SameLine();if(ImGui::Button("回到开头")){g_motion_playing=false;g_motion_frame=0.0f;}
    ImGui::SameLine();ImGui::Checkbox("循环",&g_motion_loop);
    ImGui::SameLine();ImGui::SetNextItemWidth(90.0f);ImGui::SliderFloat("速度",&g_motion_speed,.1f,2.0f,"%.1fx");
    if(available){
        const float frame_count=static_cast<float>(std::max(1u,static_cast<unsigned>(g_motion->frame_count)));
        const float end=frame_count-1.0f;
        if(g_motion_playing&&end>0.0f){g_motion_frame+=ImGui::GetIO().DeltaTime*60.0f*g_motion_speed;if(g_motion_frame>=frame_count){if(g_motion_loop)g_motion_frame=std::fmod(g_motion_frame,frame_count);else{g_motion_frame=end;g_motion_playing=false;}}}
        ImGui::SetNextItemWidth(-110.0f);if(ImGui::SliderFloat("##motion_frame",&g_motion_frame,0.0f,end,"帧 %.1f"))g_motion_playing=false;
        ImGui::SameLine();ImGui::Text("%.2f / %.2f 秒",g_motion_frame/60.0f,g_motion->duration_seconds());
        if(std::abs(g_motion_frame-g_applied_motion_frame)>1e-4f){if(g_preview->apply_animation(&*g_motion,g_motion_frame)){g_applied_motion_frame=g_motion_frame;if(g_show_collisions)update_collision_debug();}else{g_motion_playing=false;gbfr::Log::write(gbfr::LogLevel::error,"动画姿态更新失败");}}
    }
    ImGui::EndDisabled();
}

void return_to_start() {
    if(!g_imgui_ini.empty()) ImGui::SaveIniSettingsToDisk(g_imgui_ini.c_str());
    g_workspace.reset(); g_selected_asset.reset(); g_skeleton.bones.clear(); g_clh_files.clear(); g_clp_files.clear();
    g_preview_mode=PreviewMode::none;g_loaded_model.reset();g_loaded_texture.clear();clear_motion_state();
    g_imgui_ini.clear(); ImGui::GetIO().IniFilename=nullptr; g_start_layout_built=false;
    if(g_preview) g_preview->clear();
}

void draw_start_screen() {
    ImGui::Begin("开始",nullptr,ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("GBFR Modtools");
    ImGui::SeparatorText("新建工作区");
    ImGui::TextUnformatted("原始 minfo");
    const float action_width=130.0f,browse_width=82.0f;
    ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-action_width-browse_width-16.0f));
    ImGui::InputText("##minfo_path",g_minfo_path.data(),g_minfo_path.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(g_extract_process!=nullptr);
    if(ImGui::Button("选择...",ImVec2(browse_width,0))) choose_minfo_path();
    ImGui::SameLine();
    if(ImGui::Button("抽取并生成",ImVec2(action_width,0))) {
        if(std::filesystem::is_directory(tool_root()/L"explore_output")) ImGui::OpenPopup("覆盖现有工作区？");
        else start_workspace_extraction();
    }
    ImGui::EndDisabled();
    ImGui::Text("输出：%s",utf8((tool_root()/L"explore_output").wstring()).c_str());

    if(ImGui::BeginPopupModal("覆盖现有工作区？",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("现有 explore_output 将被完整替换。");
        if(ImGui::Button("继续生成",ImVec2(110,0))) { ImGui::CloseCurrentPopup(); start_workspace_extraction(); }
        ImGui::SameLine();
        if(ImGui::Button("取消",ImVec2(90,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("编辑现有工作区");
    ImGui::TextUnformatted("workspace.json");
    ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-action_width-browse_width-16.0f));
    ImGui::InputText("##workspace_path",g_workspace_path.data(),g_workspace_path.size());
    ImGui::SameLine();
    if(ImGui::Button("选择...##workspace",ImVec2(browse_width,0))) choose_workspace_path();
    ImGui::SameLine();
    if(ImGui::Button("开启编辑",ImVec2(action_width,0))) {
        const std::filesystem::path workspace=wide(g_workspace_path.data());
        if(!std::filesystem::is_regular_file(workspace)||workspace.filename()!=L"workspace.json") g_start_status="请选择有效的 workspace.json。";
        else load_workspace(workspace);
    }
    if(!g_start_status.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s",g_start_status.c_str()); }
    ImGui::End();
}

void draw_editor_shell() {
    poll_workspace_extraction();
    const ImGuiID dockspace=ImGui::GetID("GBFRDockSpace");
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    if(!g_workspace) {
        if(!g_start_layout_built) build_start_dock_layout(dockspace);
        draw_start_screen();
        return;
    }
    if(g_reset_layout) build_default_dock_layout(dockspace);

    ImGui::Begin("Workspace");
    if (ImGui::Button("开始页")) return_to_start();
    ImGui::SameLine();
    if (ImGui::Button("打开工作区...")) choose_workspace();
    ImGui::SameLine();
    if (ImGui::Button("重置布局")) g_reset_layout=true;
    ImGui::SameLine();
    if (g_workspace && ImGui::Button("刷新")) g_workspace->refresh();
    ImGui::SameLine();
    if (g_workspace && ImGui::Button("旧版构建器")) launch_legacy_builder();
    ImGui::SameLine();
    ImGui::Checkbox("只看修改", &g_changed_only);
    if (!g_workspace) {
        ImGui::Separator();
        ImGui::TextUnformatted("请先选择 unpack 中的 .minfo 文件。");
    } else {
        ImGui::Text("%s | 候选 %zu | 已修改 %zu | 缺失 %zu", g_workspace->character_id().c_str(),
                g_workspace->assets().size(), g_workspace->changed_count(), g_workspace->missing_count());
        const struct {AssetFunction function;const char* label;} filters[]={
            {AssetFunction::all,"全部"},{AssetFunction::model,"模型"},{AssetFunction::texture,"贴图"},
            {AssetFunction::material,"mmat"},{AssetFunction::cloth,"cloth"}};
        for(std::size_t i=0;i<std::size(filters);++i){
            const auto count=std::count_if(g_workspace->assets().begin(),g_workspace->assets().end(),[&](const auto& asset){return asset_matches_function(asset,filters[i].function);});
            const auto label=std::string(filters[i].label)+" ("+std::to_string(count)+")##asset_function_"+std::to_string(i);
            if(ImGui::RadioButton(label.c_str(),g_asset_function==filters[i].function))g_asset_function=filters[i].function;
            if(i+1<std::size(filters))ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##asset_search","按文件名、子类或输出路径过滤",g_asset_search.data(),g_asset_search.size());
        if (ImGui::BeginTable("assets", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("子类", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("输入", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("build", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < g_workspace->assets().size(); ++index) {
            const auto& asset = g_workspace->assets()[index];
            if (g_changed_only && !asset.changed) continue;
            if (!asset_matches_function(asset,g_asset_function)||!asset_matches_search(asset)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string id = (asset.changed ? "已修改##" : asset.available ? "未修改##" : "缺失##") + std::to_string(index);
            if (ImGui::Selectable(id.c_str(), g_selected_asset == index, ImGuiSelectableFlags_SpanAllColumns)) {g_selected_asset = index;preview_asset(index);}
            ImGui::TableNextColumn(); ImGui::TextUnformatted(gbfr::asset_kind_name(asset.kind));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(asset.subtype.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(utf8(asset.input.filename().wstring()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", utf8(asset.input.wstring()).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(utf8(asset.output.lexically_relative(g_workspace->root()).wstring()).c_str());
        }
            ImGui::EndTable();
        }
    }
    ImGui::End();

    ImGui::Begin("Viewport");
    if(g_preview_mode==PreviewMode::model){
        ImGui::Checkbox("模型", &g_show_mesh); ImGui::SameLine();
        const char* modes[]={"无光照","柔和光照","线框"};int mode=static_cast<int>(g_preview_shading);ImGui::SetNextItemWidth(120);
        if(ImGui::Combo("显示模式",&mode,modes,3))g_preview_shading=static_cast<gbfr::PreviewShadingMode>(mode);ImGui::SameLine();
        ImGui::Checkbox("骨架", &g_show_skeleton); ImGui::SameLine();
        if(ImGui::Checkbox("碰撞体", &g_show_collisions)&&g_show_collisions)update_collision_debug(); ImGui::SameLine();
        if (g_preview && g_preview->has_model() && ImGui::Button("取景")) g_preview->frame(g_camera);
        if(!g_motion_files.empty())draw_motion_controls();
    }else if(g_preview_mode==PreviewMode::texture&&g_preview&&g_preview->texture_image()){
        ImGui::Text("%s  |  %u x %u",utf8(g_loaded_texture.filename().wstring()).c_str(),g_preview->texture_width(),g_preview->texture_height());
    }
    ImVec2 available = ImGui::GetContentRegionAvail();
    if (g_preview&&g_preview_mode==PreviewMode::model&&available.x > 1 && available.y > 1) {
        g_preview->resize(static_cast<unsigned>(available.x), static_cast<unsigned>(available.y));
        g_preview->render(g_camera, g_show_mesh, g_preview_shading, g_show_skeleton, g_show_collisions, true);
        const ImVec2 image_origin=ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(g_preview->image()), available);
        if (ImGui::IsItemHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0) g_camera.distance = std::max(.02f, g_camera.distance * std::pow(.88f, io.MouseWheel));
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                if (io.KeyShift) {
                    const float scale=g_camera.distance*.82842712f/std::max(1.0f,available.y);
                    const float sy=std::sin(g_camera.yaw),cy=std::cos(g_camera.yaw),sp=std::sin(g_camera.pitch),cp=std::cos(g_camera.pitch);
                    const gbfr::Vec3 right{-cy,0,sy},up{-sp*sy,cp,-sp*cy};
                    g_camera.target.x+=(right.x*io.MouseDelta.x-up.x*io.MouseDelta.y)*scale;
                    g_camera.target.y+=(right.y*io.MouseDelta.x-up.y*io.MouseDelta.y)*scale;
                    g_camera.target.z+=(right.z*io.MouseDelta.x-up.z*io.MouseDelta.y)*scale;
                } else if (io.KeyCtrl) {
                    g_camera.distance=std::max(.02f,g_camera.distance*std::pow(1.01f,io.MouseDelta.y));
                } else {
                    g_camera.yaw+=io.MouseDelta.x*.008f;
                    g_camera.pitch=std::clamp(g_camera.pitch+io.MouseDelta.y*.008f,-1.5f,1.5f);
                }
            }
            if (g_show_skeleton && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const float mx=io.MousePos.x-image_origin.x,my=io.MousePos.y-image_origin.y;float best=144.0f;int picked=-1;
                const auto& pose=g_preview->bone_positions();for(std::size_t i=0;i<g_skeleton.bones.size();++i){const auto& bone=g_skeleton.bones[i];const auto world=i<pose.size()?pose[i]:bone.world_position;gbfr::Vec2 point{};if(!g_preview->project(world,g_camera,point))continue;const float dx=point.x-mx,dy=point.y-my,distance=dx*dx+dy*dy;if(distance<best){best=distance;picked=cloth_bone_id(bone.name);}}
                if(picked>=0){g_selected_bone=picked;g_all_bones=false;update_collision_debug();}
            }
        }
    }else if(g_preview&&g_preview_mode==PreviewMode::texture&&g_preview->texture_image()&&available.x>1&&available.y>1){
        const float width=static_cast<float>(g_preview->texture_width()),height=static_cast<float>(g_preview->texture_height());
        const float scale=std::min(available.x/width,available.y/height);const ImVec2 image_size{width*scale,height*scale};
        const ImVec2 cursor=ImGui::GetCursorPos();ImGui::SetCursorPos({cursor.x+(available.x-image_size.x)*.5f,cursor.y+(available.y-image_size.y)*.5f});
        ImGui::Image(reinterpret_cast<ImTextureID>(g_preview->texture_image()),image_size,ImVec2(0,1),ImVec2(1,0));
    }else{
        ImGui::TextUnformatted("当前对象没有可用预览");
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    if (g_workspace && g_selected_asset) {
        const auto& asset = g_workspace->assets()[*g_selected_asset];
        ImGui::Text("%s / %s", gbfr::asset_kind_name(asset.kind), asset.subtype.c_str());
        ImGui::TextWrapped("%s", utf8(asset.input.wstring()).c_str());
        ImGui::Separator();
        if (asset.kind == gbfr::AssetKind::model) {
            if (ImGui::Button("重新加载预览")) load_model_preview(*g_selected_asset,true);
            ImGui::SameLine();
            if (ImGui::Button("写入 build")) run_selected_model_action(false);
            ImGui::SameLine();
            if (ImGui::Button("恢复 unpack")) run_selected_model_action(true);
        } else {
            ImGui::TextUnformatted("此类型继续由旧版构建器处理。M5 前保持兼容入口。");
        }
    } else ImGui::TextUnformatted("选择一个资源");
    ImGui::End();

    ImGui::Begin("Skeleton & Cloth");
    if(g_skeleton.bones.empty()) ImGui::TextUnformatted("加载一个模型预览后显示骨架与 cloth。");
    else {
        const float left=std::max(220.0f,ImGui::GetContentRegionAvail().x*.34f);
        ImGui::BeginChild("bones",ImVec2(left,0),ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX);
        ImGui::Text("骨骼 %zu",g_skeleton.bones.size());
        for(const auto& bone:g_skeleton.bones){const int id=cloth_bone_id(bone.name);const auto label=bone_display(bone.name)+"##bone"+std::to_string(id);if(ImGui::Selectable(label.c_str(),g_selected_bone==id)){g_selected_bone=id;g_all_bones=false;update_collision_debug();}}
        ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("cloth",ImVec2(0,0));
        if(!g_clh_files.empty()){
            g_selected_clh=std::clamp(g_selected_clh,0,static_cast<int>(g_clh_files.size()-1));
            const auto current=utf8(g_clh_files[g_selected_clh].path.filename().wstring());
            if(ImGui::BeginCombo("CLH 文件",current.c_str())){for(int i=0;i<static_cast<int>(g_clh_files.size());++i){const auto name=utf8(g_clh_files[i].path.filename().wstring());if(ImGui::Selectable(name.c_str(),i==g_selected_clh)){g_selected_clh=i;g_selected_collision=-1;update_collision_debug();}}ImGui::EndCombo();}
            if(ImGui::Checkbox("全部 CLH",&g_all_clh_files))update_collision_debug();ImGui::SameLine();if(ImGui::Checkbox("全部骨骼",&g_all_bones))update_collision_debug();ImGui::SameLine();ImGui::Text("CLP %zu",g_clp_files.size());
            auto& collisions=g_clh_files[g_selected_clh].data.collisions;
            if(ImGui::BeginTable("collisions",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,220))){ImGui::TableSetupColumn("ID");ImGui::TableSetupColumn("P1");ImGui::TableSetupColumn("P2");ImGui::TableSetupColumn("半径");ImGui::TableSetupColumn("权重");ImGui::TableSetupColumn("Capsule");ImGui::TableHeadersRow();for(int i=0;i<static_cast<int>(collisions.size());++i){const auto& c=collisions[i];if(!g_all_bones&&g_selected_bone>=0&&c.p1!=g_selected_bone&&c.p2!=g_selected_bone)continue;ImGui::TableNextRow();ImGui::TableNextColumn();if(ImGui::Selectable((std::to_string(c.id)+"##collision"+std::to_string(i)).c_str(),g_selected_collision==i,ImGuiSelectableFlags_SpanAllColumns))g_selected_collision=i;ImGui::TableNextColumn();ImGui::Text("%d",c.p1);ImGui::TableNextColumn();ImGui::Text("%d",c.p2);ImGui::TableNextColumn();ImGui::Text("%.4f",c.radius);ImGui::TableNextColumn();ImGui::Text("%.3f",c.weight);ImGui::TableNextColumn();ImGui::Text("%d",c.capsule);}ImGui::EndTable();}
            if(g_selected_collision>=0&&g_selected_collision<static_cast<int>(collisions.size())){auto& c=collisions[g_selected_collision];ImGui::SeparatorText("碰撞属性");ImGui::DragFloat("半径",&c.radius,.001f,.0f,10.f,"%.4f");ImGui::DragFloat("权重",&c.weight,.01f,-100.f,100.f,"%.3f");ImGui::DragFloat4("Offset 1",&c.offset1.x,.001f);ImGui::DragFloat4("Offset 2",&c.offset2.x,.001f);if(ImGui::Button("保存到 unpack"))save_selected_collision();ImGui::SameLine();if(ImGui::Button("刷新预览"))update_collision_debug();}
        } else ImGui::TextUnformatted("工作区没有可用 CLH XML。");
        if(!g_clp_files.empty()&&ImGui::CollapsingHeader("CLP 节点",ImGuiTreeNodeFlags_DefaultOpen)){
            if(ImGui::BeginTable("clp_nodes",8,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,180))){const char* columns[]={"文件","骨骼","Up","Down","旋转限制","摩擦","重量","厚度"};for(const char* column:columns)ImGui::TableSetupColumn(column);ImGui::TableHeadersRow();for(const auto& file:g_clp_files)for(const auto& node:file.data.nodes){if(!g_all_bones&&g_selected_bone>=0&&node.bone!=g_selected_bone)continue;ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(utf8(file.path.filename().wstring()).c_str());ImGui::TableNextColumn();ImGui::Text("%d",node.bone);ImGui::TableNextColumn();ImGui::Text("%d",node.up);ImGui::TableNextColumn();ImGui::Text("%d",node.down);ImGui::TableNextColumn();ImGui::Text("%.3f",node.rotation_limit);ImGui::TableNextColumn();ImGui::Text("%.3f",node.friction);ImGui::TableNextColumn();ImGui::Text("%.3f",node.weight);ImGui::TableNextColumn();ImGui::Text("%.3f",node.thickness);}ImGui::EndTable();}
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Begin("Log");
    for (const auto& entry : gbfr::Log::snapshot()) ImGui::TextUnformatted(entry.message.c_str());
    ImGui::End();

    ImGui::Begin("Migration Coverage");
    if(ImGui::BeginTable("coverage",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV)){
        ImGui::TableSetupColumn("功能");ImGui::TableSetupColumn("处理端");ImGui::TableSetupColumn("状态");ImGui::TableHeadersRow();
        const char* rows[][3]={{"工作区列表 / SHA-256 / 状态","C++","原生"},{"minfo / skeleton / mmesh","C++","原生"},{"模型构建与恢复","C++","原生"},{"D3D11 网格 / DDS / 骨架预览","C++","原生"},{"MOT + SOP deform 动画求值","C++","核心操作"},{"CLH / CLP 查看与 CLH 编辑","C++","原生"},{"texture 封回与恢复","PowerShell + _lib","兼容入口"},{"mmat 编码 / A4 快捷编辑","PowerShell + _lib","兼容入口"},{"cloth BXM 编码与恢复","PowerShell + _lib","兼容入口"},{"新建 .texture","PowerShell + nier_cli","兼容入口"}};
        for(const auto& row:rows){ImGui::TableNextRow();for(const char* cell:row){ImGui::TableNextColumn();ImGui::TextUnformatted(cell);}}
        ImGui::EndTable();
    }
    if(g_workspace&&ImGui::Button("打开旧版构建器"))launch_legacy_builder();
    ImGui::End();
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, window_proc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                   L"GBFRModtoolsWindow", nullptr};
    RegisterClassExW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"GBFR Modtools C++", WS_OVERLAPPEDWINDOW,
                                100, 100, 1500, 900, nullptr, nullptr, wc.hInstance, nullptr);

    gbfr::D3D11Context d3d;
    g_d3d = &d3d;
    if (!d3d.initialize(window)) return 1;
    gbfr::PreviewRenderer preview;
    if (!preview.initialize(d3d.device(), d3d.context(), preview_shader_file())) return 2;
    g_preview = &preview;
    load_bone_names();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename=nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(d3d.device(), d3d.context());

    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, nullptr,
                                     io.Fonts->GetGlyphRangesChineseFull());
    }

    int argument_count{};
    if (wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count)) {
        if (argument_count > 1) {
            const std::filesystem::path selected=arguments[1];
            if(selected.extension()==L".minfo") set_path_input(g_minfo_path,selected);
            else if(selected.filename()==L"workspace.json") load_workspace(selected);
        }
        LocalFree(arguments);
    }
    gbfr::Log::write(gbfr::LogLevel::info, "原生编辑器已初始化");
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    bool running = true;
    while (running) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_editor_shell();
        ImGui::Render();

        d3d.begin_frame(0.08f, 0.09f, 0.10f);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        d3d.present();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    d3d.shutdown();
    if(g_extract_process) { CloseHandle(g_extract_process); g_extract_process=nullptr; }
    g_preview = nullptr;
    g_d3d = nullptr;
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
