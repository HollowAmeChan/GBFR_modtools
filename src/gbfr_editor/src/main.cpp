#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/material.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/formats/animation.hpp>
#include <gbfr/render/d3d11_context.hpp>
#include <gbfr/render/preview_renderer.hpp>
#include "sop_inspector.hpp"
#include "texture_gallery.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <charconv>
#include <fstream>
#include <deque>
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
enum class AssetFunction { all, model, texture, ui_image, material, cloth };
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
bool g_loaded_texture_is_ui = false;
std::filesystem::path g_loaded_material;
std::filesystem::path g_loaded_sop;
gbfr::OrbitCamera g_camera;
gbfr::SkeletonAsset g_skeleton;
bool g_show_mesh = true;
gbfr::PreviewShadingMode g_preview_shading = gbfr::PreviewShadingMode::lit;
bool g_show_skeleton = true;
bool g_show_collisions = true;
bool g_show_cloth_links = true;
bool g_show_alpha_overlays = true;
std::vector<std::filesystem::path> g_motion_files;
std::optional<gbfr::AnimationClip> g_motion;
std::optional<gbfr::ClothSequenceAsset> g_cloth_sequence;
std::filesystem::path g_cloth_sequence_path;
std::array<char,128> g_motion_search{};
int g_selected_motion = -1;
float g_motion_frame = 0.0f;
float g_motion_speed = 1.0f;
float g_applied_motion_frame = -1.0f;
bool g_motion_playing = false;
bool g_motion_loop = true;
struct ClhFile { std::filesystem::path path; gbfr::ClhAsset data; int group_id{-1}; };
struct ClpFile { std::filesystem::path path; gbfr::ClpAsset data; };
std::vector<ClhFile> g_clh_files;
std::vector<ClpFile> g_clp_files;
std::unordered_map<std::string,std::string> g_bone_names;
gbfr::editor::SopInspector g_sop_inspector;
gbfr::editor::TextureGallery g_texture_gallery;
int g_selected_clh = 0;
int g_selected_clp = 0;
int g_selected_bone = -1;
int g_selected_collision = -1;
bool g_all_clp_groups = false;
bool g_all_bones = false;
bool g_reset_layout = true;
bool g_start_layout_built = false;
std::array<char,32768> g_minfo_path{};
std::array<char,32768> g_workspace_output_path{};
std::array<char,32768> g_workspace_path{};
HANDLE g_extract_process = nullptr;
HANDLE g_extract_output_read = nullptr;
std::filesystem::path g_extract_output;
std::deque<std::string> g_extract_console_lines;
std::string g_extract_console_pending;
bool g_extract_console_updated = false;
bool g_extract_console_auto_scroll = true;
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

int cloth_file_group_id(const std::filesystem::path& path) {
    const auto name=path.filename().wstring();const auto marker=name.rfind(L"_0_");if(marker==std::wstring::npos)return -1;
    const auto end=name.find(L"_cl",marker+3);if(end==std::wstring::npos)return -1;
    int value{};for(std::size_t i=marker+3;i<end;++i){if(name[i]<L'0'||name[i]>L'9')return -1;value=value*10+(name[i]-L'0');}return value;
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

enum class WorkspaceArea { source, unpack, build, external };

WorkspaceArea workspace_area(const std::filesystem::path& path) {
    if(!g_workspace||path.empty())return WorkspaceArea::external;
    const auto relative=path.lexically_relative(g_workspace->root());
    if(relative.empty()||relative.native().starts_with(L".."))return WorkspaceArea::external;
    const auto first=relative.begin();
    if(first==relative.end())return WorkspaceArea::external;
    if(*first==L"source")return WorkspaceArea::source;
    if(*first==L"unpack")return WorkspaceArea::unpack;
    if(*first==L"build")return WorkspaceArea::build;
    return WorkspaceArea::external;
}

const char* workspace_area_name(WorkspaceArea area) {
    switch(area){
    case WorkspaceArea::source:return "source";
    case WorkspaceArea::unpack:return "unpack";
    case WorkspaceArea::build:return "build";
    case WorkspaceArea::external:return "外部";
    }
    return "外部";
}

ImVec4 workspace_area_color(WorkspaceArea area) {
    switch(area){
    case WorkspaceArea::source:return ImVec4(.52f,.72f,.96f,1.0f);
    case WorkspaceArea::unpack:return ImVec4(.43f,.86f,.58f,1.0f);
    case WorkspaceArea::build:return ImVec4(.96f,.72f,.34f,1.0f);
    case WorkspaceArea::external:return ImVec4(.72f,.72f,.72f,1.0f);
    }
    return ImVec4(1,1,1,1);
}

void draw_preview_source(const char* label,const std::filesystem::path& path) {
    if(path.empty())return;
    const auto area=workspace_area(path);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f,4.0f);
    ImGui::TextColored(workspace_area_color(area),"[%s]",workspace_area_name(area));
    if(ImGui::IsItemHovered()){
        const auto relative=g_workspace?path.lexically_relative(g_workspace->root()):path;
        ImGui::SetTooltip("%s",utf8((relative.empty()?path:relative).wstring()).c_str());
    }
}

void draw_preview_source_inline(const char* label,const std::filesystem::path& path) {
    draw_preview_source(label,path);
    ImGui::SameLine(0.0f,12.0f);
}

bool load_workspace(const std::filesystem::path& path) {
    try {
        g_workspace = std::make_unique<gbfr::Workspace>(gbfr::Workspace::load(path));
        clear_motion_state();
        g_selected_asset.reset();
        g_texture_gallery.clear();
        g_asset_function=AssetFunction::all;g_asset_search.fill('\0');
        g_preview_mode=PreviewMode::none;g_loaded_model.reset();g_loaded_texture.clear();g_loaded_texture_is_ui=false;g_loaded_material.clear();g_loaded_sop.clear();
        g_skeleton.bones.clear(); g_clh_files.clear(); g_clp_files.clear();g_sop_inspector.clear();
        if(g_preview) g_preview->clear();
        const auto settings_directory=g_workspace->root()/L".gbfr";
        std::filesystem::create_directories(settings_directory);
        const auto settings_file=settings_directory/L"imgui_v4.ini";
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

std::filesystem::path workspace_output_path() {
    std::filesystem::path output=wide(g_workspace_output_path.data());
    if(output.empty()) return tool_root()/L"explore_output";
    std::error_code error;
    const auto absolute=std::filesystem::absolute(output,error);
    return (error?output:absolute).lexically_normal();
}

void choose_workspace_output_path() {
    const HRESULT com_result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    BROWSEINFOW dialog{};
    dialog.hwndOwner=GetActiveWindow();
    dialog.lpszTitle=L"选择工作区输出文件夹";
    dialog.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE|BIF_EDITBOX;
    if(const auto item=SHBrowseForFolderW(&dialog)) {
        wchar_t path[32768]{};
        if(SHGetPathFromIDListW(item,path)) set_path_input(g_workspace_output_path,path);
        CoTaskMemFree(item);
    }
    if(SUCCEEDED(com_result)) CoUninitialize();
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
    const auto script=tool_root()/L"scripts/workspace/explore_char.ps1";
    if(!std::filesystem::is_regular_file(script)) { g_start_status="找不到工作区生成脚本。"; return false; }
    const auto output=workspace_output_path();
    std::wstring command=L"powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File \""+script.wstring()+
        L"\" -MinfoPath \""+minfo.wstring()+L"\" -OutputPath \""+output.wstring()+L"\"";
    std::vector<wchar_t> mutable_command(command.begin(),command.end()); mutable_command.push_back(L'\0');
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE output_read=nullptr,output_write=nullptr;
    if(!CreatePipe(&output_read,&output_write,&security,0)||!SetHandleInformation(output_read,HANDLE_FLAG_INHERIT,0)) {
        const DWORD pipe_error=GetLastError();
        if(output_read)CloseHandle(output_read);if(output_write)CloseHandle(output_write);
        g_start_status="无法创建抽取控制台管道，错误码 "+std::to_string(pipe_error);return false;
    }
    HANDLE null_input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(null_input==INVALID_HANDLE_VALUE) {
        const DWORD input_error=GetLastError();CloseHandle(output_read);CloseHandle(output_write);
        g_start_status="无法准备抽取进程输入，错误码 "+std::to_string(input_error);return false;
    }
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=null_input;startup.hStdOutput=output_write;startup.hStdError=output_write;
    const auto cwd=tool_root().wstring();
    const bool started=CreateProcessW(nullptr,mutable_command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&startup,&process)!=FALSE;
    const DWORD launch_error=started?ERROR_SUCCESS:GetLastError();CloseHandle(output_write);CloseHandle(null_input);
    if(!started) { CloseHandle(output_read);g_start_status="无法启动资源抽取进程，错误码 "+std::to_string(launch_error); return false; }
    CloseHandle(process.hThread);g_extract_process=process.hProcess;g_extract_output_read=output_read;g_extract_output=output;
    g_extract_console_lines.clear();g_extract_console_pending.clear();g_extract_console_updated=true;
    g_start_status="正在抽取并生成工作区，可在下方控制台查看进度。";return true;
}

void append_extraction_console(const char* bytes,std::size_t size) {
    g_extract_console_pending.append(bytes,size);
    std::size_t newline{};
    while((newline=g_extract_console_pending.find('\n'))!=std::string::npos) {
        std::string line=g_extract_console_pending.substr(0,newline);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        g_extract_console_lines.push_back(std::move(line));
        g_extract_console_pending.erase(0,newline+1);
        while(g_extract_console_lines.size()>4000)g_extract_console_lines.pop_front();
        g_extract_console_updated=true;
    }
}

void drain_extraction_console(bool flush_pending=false) {
    if(g_extract_output_read) {
        for(;;) {
            DWORD available{};
            if(!PeekNamedPipe(g_extract_output_read,nullptr,0,nullptr,&available,nullptr)||available==0)break;
            std::array<char,4096> buffer{};DWORD read{};
            if(!ReadFile(g_extract_output_read,buffer.data(),std::min<DWORD>(available,static_cast<DWORD>(buffer.size())),&read,nullptr)||read==0)break;
            append_extraction_console(buffer.data(),read);
        }
    }
    if(flush_pending&&!g_extract_console_pending.empty()) {
        if(!g_extract_console_pending.empty()&&g_extract_console_pending.back()=='\r')g_extract_console_pending.pop_back();
        g_extract_console_lines.push_back(std::move(g_extract_console_pending));g_extract_console_pending.clear();g_extract_console_updated=true;
    }
}

void poll_workspace_extraction() {
    if(!g_extract_process) return;
    drain_extraction_console();
    DWORD exit_code{};
    if(!GetExitCodeProcess(g_extract_process,&exit_code)||exit_code==STILL_ACTIVE) return;
    drain_extraction_console(true);
    if(g_extract_output_read){CloseHandle(g_extract_output_read);g_extract_output_read=nullptr;}
    CloseHandle(g_extract_process); g_extract_process=nullptr;
    if(exit_code!=0) { g_extract_output.clear(); g_start_status="工作区生成失败，退出码 "+std::to_string(exit_code)+"。"; return; }
    const auto workspace=(g_extract_output.empty()?workspace_output_path():g_extract_output)/L"workspace.json";
    g_extract_output.clear();
    if(!std::filesystem::is_regular_file(workspace)) { g_start_status="抽取完成，但没有生成 workspace.json。"; return; }
    set_path_input(g_workspace_path,workspace);
    g_start_status="工作区生成完成。";
    load_workspace(workspace);
}

bool load_model_preview(std::size_t index,bool force=false);

void run_selected_asset_action(bool restore) {
    if (!g_workspace || !g_selected_asset) return;
    try {
        if (restore) g_workspace->restore_asset(*g_selected_asset);
        else g_workspace->build_asset(*g_selected_asset);
        if(restore)g_texture_gallery.clear();
        if(restore && g_workspace->assets()[*g_selected_asset].kind==gbfr::AssetKind::model) load_model_preview(*g_selected_asset,true);
        gbfr::Log::write(gbfr::LogLevel::info, restore ? "资源已从 source 恢复到 unpack" : "资源已从 unpack 封回 build");
    } catch (const std::exception& error) {
        gbfr::Log::write(gbfr::LogLevel::error, error.what());
    }
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
    g_motion_files.clear();g_motion.reset();g_cloth_sequence.reset();g_cloth_sequence_path.clear();g_motion_search.fill('\0');g_selected_motion=-1;g_motion_frame=0.0f;g_applied_motion_frame=-1.0f;g_motion_playing=false;
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
        const auto& motion_path=g_motion_files[static_cast<std::size_t>(index)];
        auto motion=gbfr::load_mot(motion_path);
        if(!g_preview->apply_animation(&motion,0.0f))throw std::runtime_error("动画姿态应用失败");
        g_cloth_sequence.reset();g_cloth_sequence_path.clear();
        if(g_workspace&&motion_path.parent_path().parent_path().filename()==L"pl"){
            const auto model_id=motion_path.parent_path().filename();
            const auto sequence_path=g_workspace->root()/L"unpack/data/pl"/model_id/(motion_path.stem().wstring()+L"_0_seq_edit_cloth.bxm.xml");
            if(std::filesystem::is_regular_file(sequence_path)){
                try{g_cloth_sequence=gbfr::load_cloth_sequence(sequence_path);g_cloth_sequence_path=sequence_path;}
                catch(const std::exception& error){gbfr::Log::write(gbfr::LogLevel::warning,std::string("cloth 动画覆盖读取失败：")+error.what());}
            }
        }
        g_motion=std::move(motion);g_selected_motion=index;g_motion_frame=0.0f;g_applied_motion_frame=0.0f;g_motion_playing=false;
        update_collision_debug();
        gbfr::Log::write(gbfr::LogLevel::info,"动画已加载："+g_motion->name+"，"+std::to_string(g_motion->frame_count)+" 帧");
        return true;
    } catch(const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::error,std::string("动画加载失败：")+error.what());return false; }
}

void reset_motion_pose() {
    g_motion.reset();g_cloth_sequence.reset();g_cloth_sequence_path.clear();g_selected_motion=-1;g_motion_frame=0.0f;g_applied_motion_frame=-1.0f;g_motion_playing=false;
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
            if(entry.alpha_masked)preview.alpha_mask=resolve_base_albedo(g_workspace->root(),entry.alpha_mask_name);
            preview.alpha_clipped=entry.alpha_clipped;
            preview.alpha_blended=entry.alpha_blended;
            if(!preview.albedo.empty()||(!preview.eye_conjunctiva.empty()&&!preview.eye_iris.empty()&&!preview.eye_highlight.empty()))++resolved_materials;
        }
        for(const auto& chunk:mesh.chunks) {
            if(chunk.material>=materials.entries.size()) throw std::runtime_error("minfo MaterialID 超出 0.mmat 条目范围");
            const auto submesh=chunk.submesh<info.submesh_names.size()?info.submesh_names[chunk.submesh]:std::string{"?"};
            gbfr::Log::write(gbfr::LogLevel::info,
                "LOD0 分段：submesh="+submesh+
                " material="+std::to_string(chunk.material)+
                " indices="+std::to_string(chunk.count)+
                " alpha="+(materials.entries[chunk.material].alpha_masked?"masked-overlay":materials.entries[chunk.material].alpha_blended?"blend":materials.entries[chunk.material].alpha_clipped?"clip":"none"));
        }
        if (!g_preview->load(mesh, skeleton, preview_materials, sop)) throw std::runtime_error("GPU 预览资源创建失败");
        g_sop_inspector.set_asset(sop,sop_path);
        g_loaded_material=material_json;
        g_loaded_sop=std::filesystem::is_regular_file(sop_path)?sop_path:std::filesystem::path{};
        g_skeleton=skeleton;g_loaded_model=key;g_loaded_texture.clear();g_preview_mode=PreviewMode::model;
        discover_motions(key);
        g_preview->frame(g_camera);
        g_clh_files.clear(); g_clp_files.clear(); g_selected_collision=-1; g_selected_bone=-1; g_selected_clh=0;g_selected_clp=0;g_all_clp_groups=false;
        const auto model_id=key.minfo.stem().wstring();
        const auto cloth_prefix=model_id+L"_";
        for(const auto& asset:g_workspace->assets()) if(asset.kind==gbfr::AssetKind::cloth&&asset.available&&asset.input.filename().wstring().starts_with(cloth_prefix)) {
            try { if(asset.subtype=="clh") g_clh_files.push_back({asset.input,gbfr::load_clh(asset.input),cloth_file_group_id(asset.input)}); else if(asset.subtype=="clp") g_clp_files.push_back({asset.input,gbfr::load_clp(asset.input)}); } catch(const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::warning,std::string("cloth 跳过：")+error.what()); }
        }
        std::sort(g_clh_files.begin(),g_clh_files.end(),[](const auto& left,const auto& right){return left.group_id<right.group_id;});
        std::sort(g_clp_files.begin(),g_clp_files.end(),[](const auto& left,const auto& right){return left.data.id<right.data.id;});
        if(!g_clp_files.empty()){const int mask=g_clp_files.front().data.collision_flags;const auto found=std::find_if(g_clh_files.begin(),g_clh_files.end(),[&](const auto& file){return file.group_id>=0&&file.group_id<31&&(mask&(1<<file.group_id));});if(found!=g_clh_files.end())g_selected_clh=static_cast<int>(std::distance(g_clh_files.begin(),found));}
        update_collision_debug();
        gbfr::Log::write(gbfr::LogLevel::info, "预览已加载：" + std::to_string(mesh.vertices.size()) + " 顶点，" + std::to_string(mesh.indices.size()/3) + " 三角形，" + std::to_string(mesh.chunks.size()) + " 材质分段，SOP " + std::to_string(sop.operations.size()) + " 操作，0.mmat 可见材质 " + std::to_string(resolved_materials) + "/" + std::to_string(materials.entries.size()));
        return true;
    } catch (const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::error, std::string("预览加载失败：") + error.what());return false; }
}

void preview_asset(std::size_t index) {
    if(!g_workspace||index>=g_workspace->assets().size()||!g_preview)return;
    const auto& asset=g_workspace->assets()[index];
    if(asset.kind==gbfr::AssetKind::model){load_model_preview(index);return;}
    if(asset.kind==gbfr::AssetKind::texture||asset.kind==gbfr::AssetKind::ui_image||asset.kind==gbfr::AssetKind::new_texture){
        if(!asset.available||asset.input.extension()!=L".dds"){g_preview_mode=PreviewMode::none;return;}
        g_loaded_texture_is_ui = asset.kind == gbfr::AssetKind::ui_image;
        if(g_loaded_texture==asset.input&&g_preview->texture_image()){g_preview_mode=PreviewMode::texture;return;}
        if(g_preview->load_texture_preview(asset.input)){g_loaded_texture=asset.input;g_preview_mode=PreviewMode::texture;gbfr::Log::write(gbfr::LogLevel::info,"DDS 预览已加载："+utf8(asset.input.filename().wstring()));}
        else {g_preview_mode=PreviewMode::none;gbfr::Log::write(gbfr::LogLevel::error,"DDS 预览加载失败："+utf8(asset.input.wstring()));}
        return;
    }
    g_preview_mode=PreviewMode::none;
}

void preview_gallery_texture(std::size_t asset_index,const std::filesystem::path& path,bool is_ui) {
    if(!g_preview||!std::filesystem::is_regular_file(path))return;
    g_selected_asset=asset_index;
    g_loaded_texture_is_ui=is_ui;
    if(g_loaded_texture==path&&g_preview->texture_image()){g_preview_mode=PreviewMode::texture;return;}
    if(g_preview->load_texture_preview(path)){g_loaded_texture=path;g_preview_mode=PreviewMode::texture;gbfr::Log::write(gbfr::LogLevel::info,"DDS 预览已加载："+utf8(path.filename().wstring()));}
    else {g_preview_mode=PreviewMode::none;gbfr::Log::write(gbfr::LogLevel::error,"DDS 预览加载失败："+utf8(path.wstring()));}
}

bool asset_matches_function(const gbfr::WorkspaceAsset& asset,AssetFunction function) {
    switch(function){
    case AssetFunction::all:return true;
    case AssetFunction::model:return asset.kind==gbfr::AssetKind::model;
    case AssetFunction::texture:return asset.kind==gbfr::AssetKind::texture||asset.kind==gbfr::AssetKind::new_texture;
    case AssetFunction::ui_image:return asset.kind==gbfr::AssetKind::ui_image;
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

int compare_ascii_case_insensitive(std::string left,std::string right) {
    const auto lower=[](unsigned char value){return static_cast<char>(std::tolower(value));};
    std::transform(left.begin(),left.end(),left.begin(),lower);std::transform(right.begin(),right.end(),right.begin(),lower);
    if(left<right)return -1;if(right<left)return 1;return 0;
}

int compare_natural_path(const std::filesystem::path& left,const std::filesystem::path& right) {
    const auto& a=left.native();const auto& b=right.native();
    if(gbfr::natural_less_case_insensitive(a,b))return -1;
    if(gbfr::natural_less_case_insensitive(b,a))return 1;
    return 0;
}

bool asset_matches_search(const gbfr::WorkspaceAsset& asset) {
    if(!g_asset_search[0])return true;
    const std::string query=g_asset_search.data();
    return contains_ascii_case_insensitive(utf8(asset.input.filename().wstring()),query)||
           contains_ascii_case_insensitive(utf8(asset.output.wstring()),query)||
           contains_ascii_case_insensitive(asset.subtype,query);
}

std::optional<gbfr::Vec3> bone_point(int id,const gbfr::Vec4& offset={}) {
    for(std::size_t i=0;i<g_skeleton.bones.size();++i)if(cloth_bone_id(g_skeleton.bones[i].name)==id){
        gbfr::Vec3 point{};
        if(g_preview&&g_preview->transform_bone_point(i,{offset.x,offset.y,offset.z},point))return point;
        const auto& rest=g_skeleton.bones[i].world_position;return gbfr::Vec3{rest.x+offset.x,rest.y+offset.y,rest.z+offset.z};
    }
    return std::nullopt;
}

gbfr::Vec3 add(gbfr::Vec3 a,gbfr::Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
gbfr::Vec3 subtract(gbfr::Vec3 a,gbfr::Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
gbfr::Vec3 multiply(gbfr::Vec3 value,float scale){return {value.x*scale,value.y*scale,value.z*scale};}
float length(gbfr::Vec3 value){return std::sqrt(value.x*value.x+value.y*value.y+value.z*value.z);}
gbfr::Vec3 cross(gbfr::Vec3 a,gbfr::Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
gbfr::Vec3 normalize(gbfr::Vec3 value){const float size=length(value);return size>1e-7f?multiply(value,1.0f/size):gbfr::Vec3{};}

void append_circle(std::vector<gbfr::Vec3>& lines,gbfr::Vec3 center,float radius,int plane) {
    constexpr int segments=20; constexpr float tau=6.283185307f;
    for(int i=0;i<segments;++i){const float a=tau*i/segments,b=tau*(i+1)/segments;gbfr::Vec3 p=center,q=center;const float ca=std::cos(a)*radius,sa=std::sin(a)*radius,cb=std::cos(b)*radius,sb=std::sin(b)*radius;if(plane==0){p.x+=ca;p.y+=sa;q.x+=cb;q.y+=sb;}else if(plane==1){p.x+=ca;p.z+=sa;q.x+=cb;q.z+=sb;}else{p.y+=ca;p.z+=sa;q.y+=cb;q.z+=sb;}lines.push_back(p);lines.push_back(q);}
}

void append_sphere(std::vector<gbfr::Vec3>& lines,gbfr::Vec3 center,float radius){for(int plane=0;plane<3;++plane)append_circle(lines,center,radius,plane);}

void append_capsule(std::vector<gbfr::Vec3>& lines,gbfr::Vec3 a,float radius_a,gbfr::Vec3 b,float radius_b) {
    const auto axis=normalize(subtract(b,a));if(length(axis)<1e-7f)return;
    const gbfr::Vec3 reference=std::abs(axis.y)<.8f?gbfr::Vec3{0,1,0}:gbfr::Vec3{1,0,0};
    const auto u=normalize(cross(axis,reference)),v=normalize(cross(axis,u));
    constexpr int segments=16;constexpr float tau=6.283185307f;
    for(int i=0;i<segments;++i){
        const float angle=tau*static_cast<float>(i)/segments;
        const auto radial=add(multiply(u,std::cos(angle)),multiply(v,std::sin(angle)));
        lines.push_back(add(a,multiply(radial,radius_a)));lines.push_back(add(b,multiply(radial,radius_b)));
    }
}

bool solver_group_visible(std::size_t index) {
    return g_all_clp_groups||(g_selected_clp>=0&&static_cast<std::size_t>(g_selected_clp)==index);
}

int active_collision_mask() {
    int mask{};for(std::size_t i=0;i<g_clp_files.size();++i)if(solver_group_visible(i))mask|=g_clp_files[i].data.collision_flags;return mask;
}

std::string collision_layer_list(int mask) {
    std::string result;for(int layer=0;layer<31;++layer)if(mask&(1<<layer)){if(!result.empty())result+=", ";result+=std::to_string(layer);}return result.empty()?"无":result;
}

bool cloth_event_has_collisions(const gbfr::ClothSequenceEvent& event) {
    return std::any_of(event.collision_ids.begin(),event.collision_ids.end(),[](int id){return id>=0;});
}

int cloth_sequence_file_mask(bool collisions_only) {
    if(!g_cloth_sequence)return 0;int mask{};
    for(const auto& event:g_cloth_sequence->events)if(event.file_id>=0&&event.file_id<31&&(!collisions_only||cloth_event_has_collisions(event)))mask|=1<<event.file_id;
    return mask;
}

std::string cloth_collision_id_list(const gbfr::ClothSequenceEvent& event) {
    std::string result;for(const int id:event.collision_ids)if(id>=0){if(!result.empty())result+=", ";result+=std::to_string(id);}return result.empty()?"-":result;
}

void update_collision_debug() {
    if(!g_preview)return;
    std::vector<gbfr::Vec3> collision_lines,longitudinal_lines,lateral_lines,polygon_lines;
    const int collision_mask=active_collision_mask();
    for(std::size_t file_index=0;file_index<g_clh_files.size();++file_index){
        const auto& file=g_clh_files[file_index];if(file.group_id<0||file.group_id>=31||!(collision_mask&(1<<file.group_id)))continue;
        const auto& collisions=file.data.collisions;
        struct Endpoint{gbfr::Vec3 center{};float radius{};const gbfr::ClothCollision* source{};};
        std::unordered_map<int,Endpoint> endpoints;
        for(const auto& collision:collisions){
            const auto first=bone_point(collision.p1,collision.offset1),second=bone_point(collision.p2,collision.offset2);if(!first||!second)continue;
            endpoints[collision.id]={add(multiply(*first,1.0f-collision.weight),multiply(*second,collision.weight)),collision.radius,&collision};
        }
        for(const auto& collision:collisions){
            const auto endpoint=endpoints.find(collision.id);if(endpoint==endpoints.end())continue;
            const auto linked=collision.capsule>=0?endpoints.find(collision.capsule):endpoints.end();
            bool visible=g_all_bones||g_selected_bone<0||collision.p1==g_selected_bone||collision.p2==g_selected_bone;
            if(!visible&&linked!=endpoints.end())visible=linked->second.source->p1==g_selected_bone||linked->second.source->p2==g_selected_bone;
            if(!visible)continue;
            append_sphere(collision_lines,endpoint->second.center,endpoint->second.radius);
            if(linked!=endpoints.end()){
                append_sphere(collision_lines,linked->second.center,linked->second.radius);
                append_capsule(collision_lines,linked->second.center,linked->second.radius,endpoint->second.center,endpoint->second.radius);
            }
        }
    }
    for(std::size_t file_index=0;file_index<g_clp_files.size();++file_index){
        if(!solver_group_visible(file_index))continue;const auto& file=g_clp_files[file_index];
        std::unordered_map<int,gbfr::Vec3> positions;
        for(const auto& node:file.data.nodes)if(const auto point=bone_point(node.bone))positions.emplace(node.bone,*point);
        const auto append_edge=[&](std::vector<gbfr::Vec3>& lines,const gbfr::ClothNode& node,int target){
            if(target<0||target==4095)return;const auto from=positions.find(node.bone),to=positions.find(target);if(from==positions.end()||to==positions.end())return;
            if(!g_all_bones&&g_selected_bone>=0&&node.bone!=g_selected_bone&&target!=g_selected_bone)return;
            lines.push_back(from->second);lines.push_back(to->second);
        };
        for(const auto& node:file.data.nodes){append_edge(longitudinal_lines,node,node.down);append_edge(lateral_lines,node,node.side);if(node.poly!=node.side)append_edge(polygon_lines,node,node.poly);}
    }
    g_preview->set_collision_lines(collision_lines);
    g_preview->set_cloth_lines(longitudinal_lines,lateral_lines,polygon_lines);
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
    ImGui::DockBuilderDockWindow("贴图库",center);
    ImGui::DockBuilderDockWindow("Viewport",center);
    ImGui::DockBuilderDockWindow("Inspector",right_top);
    ImGui::DockBuilderDockWindow("Skeleton & Cloth",right_bottom);
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
    ImGuiID start{},console{};
    ImGui::DockBuilderSplitNode(dockspace,ImGuiDir_Down,0.42f,&console,&start);
    ImGui::DockBuilderDockWindow("开始",start);
    ImGui::DockBuilderDockWindow("抽取控制台",console);
    ImGui::DockBuilderFinish(dockspace);
    g_start_layout_built=true;
}

void draw_motion_controls() {
    if(g_selected_motion>=0){
        draw_preview_source_inline("动画",g_motion_files[static_cast<std::size_t>(g_selected_motion)]);
        if(!g_cloth_sequence_path.empty())draw_preview_source("cloth 覆盖",g_cloth_sequence_path);
        else {ImGui::TextDisabled("cloth 覆盖 [无]");}
    }else ImGui::TextDisabled("动画 [静止姿态]");
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
        if(std::abs(g_motion_frame-g_applied_motion_frame)>1e-4f){if(g_preview->apply_animation(&*g_motion,g_motion_frame)){g_applied_motion_frame=g_motion_frame;update_collision_debug();}else{g_motion_playing=false;gbfr::Log::write(gbfr::LogLevel::error,"动画姿态更新失败");}}
    }
    ImGui::EndDisabled();
    const int base_mask=active_collision_mask();
    if(!g_cloth_sequence){ImGui::Text("基础 CLH: %s | 动画覆盖: 无",collision_layer_list(base_mask).c_str());return;}
    const int collision_mask=cloth_sequence_file_mask(true),event_mask=cloth_sequence_file_mask(false);
    const float current_time=g_motion_frame/60.0f;
    const auto triggered=std::count_if(g_cloth_sequence->events.begin(),g_cloth_sequence->events.end(),[&](const auto& event){return event.start_time<=current_time+1e-5f;});
    ImGui::Text("基础 CLH: %s | 动画引用 CLH: %s | FileId 事件: %s | 已触发 %d/%d",collision_layer_list(base_mask).c_str(),collision_layer_list(collision_mask).c_str(),collision_layer_list(event_mask).c_str(),static_cast<int>(triggered),static_cast<int>(g_cloth_sequence->events.size()));
    if(ImGui::CollapsingHeader("Cloth 动画覆盖")){
        ImGui::TextUnformatted(utf8(g_cloth_sequence_path.filename().wstring()).c_str());
        constexpr ImGuiTableFlags flags=ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp;
        if(ImGui::BeginTable("##cloth_sequence",10,flags,ImVec2(0,170.0f))){
            ImGui::TableSetupScrollFreeze(0,1);ImGui::TableSetupColumn("状态");ImGui::TableSetupColumn("时间");ImGui::TableSetupColumn("FileId");ImGui::TableSetupColumn("SeqFlag");ImGui::TableSetupColumn("LayerFlag");ImGui::TableSetupColumn("Collision IDs");ImGui::TableSetupColumn("Scale");ImGui::TableSetupColumn("淡入帧");ImGui::TableSetupColumn("地面偏移");ImGui::TableSetupColumn("地面淡入帧");ImGui::TableHeadersRow();
            for(const auto& event:g_cloth_sequence->events){
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(event.start_time<=current_time+1e-5f?"已触发":"等待");
                ImGui::TableNextColumn();ImGui::Text("%.3f",event.start_time);ImGui::TableNextColumn();ImGui::Text("%d",event.file_id);ImGui::TableNextColumn();ImGui::Text("%d",event.sequence_flag);
                ImGui::TableNextColumn();ImGui::Text("0x%08X",event.layer_flags);ImGui::TableNextColumn();ImGui::TextUnformatted(cloth_collision_id_list(event).c_str());ImGui::TableNextColumn();ImGui::Text("%.3f",event.scale_rate);ImGui::TableNextColumn();ImGui::Text("%d",event.fade_frames);
                ImGui::TableNextColumn();ImGui::Text("%.3f",event.floor_offset);ImGui::TableNextColumn();ImGui::Text("%d",event.floor_fade_frames);
            }
            ImGui::EndTable();
        }
    }
}

void return_to_start() {
    if(!g_imgui_ini.empty()) ImGui::SaveIniSettingsToDisk(g_imgui_ini.c_str());
    g_workspace.reset(); g_selected_asset.reset(); g_skeleton.bones.clear(); g_clh_files.clear(); g_clp_files.clear();g_sop_inspector.clear();
    g_preview_mode=PreviewMode::none;g_loaded_model.reset();g_loaded_texture.clear();g_loaded_texture_is_ui=false;g_loaded_material.clear();g_loaded_sop.clear();clear_motion_state();
    g_imgui_ini.clear(); ImGui::GetIO().IniFilename=nullptr; g_start_layout_built=false;
    g_texture_gallery.clear();
    if(g_preview) g_preview->clear();
}

void draw_start_screen() {
    ImGui::Begin("开始",nullptr,ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("GBFR Modtools");
    ImGui::SeparatorText("新建工作区");
    ImGui::TextUnformatted("原始 minfo");
    const float action_width=130.0f,browse_width=82.0f;
    ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-browse_width-8.0f));
    ImGui::BeginDisabled(g_extract_process!=nullptr);
    ImGui::InputText("##minfo_path",g_minfo_path.data(),g_minfo_path.size());
    ImGui::SameLine();
    if(ImGui::Button("选择...",ImVec2(browse_width,0))) choose_minfo_path();

    ImGui::TextUnformatted("工作区输出目录（留空使用默认目录）");
    ImGui::SetNextItemWidth(std::max(120.0f,ImGui::GetContentRegionAvail().x-action_width-browse_width-16.0f));
    ImGui::InputText("##workspace_output_path",g_workspace_output_path.data(),g_workspace_output_path.size());
    ImGui::SameLine();
    if(ImGui::Button("选择...##output",ImVec2(browse_width,0))) choose_workspace_output_path();
    ImGui::SameLine();
    if(ImGui::Button("抽取并生成",ImVec2(action_width,0))) {
        if(std::filesystem::exists(workspace_output_path())) ImGui::OpenPopup("覆盖现有工作区？");
        else start_workspace_extraction();
    }
    ImGui::EndDisabled();
    if(g_workspace_output_path[0]=='\0') ImGui::TextDisabled("默认：%s",utf8(workspace_output_path().wstring()).c_str());

    if(ImGui::BeginPopupModal("覆盖现有工作区？",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("目标目录已存在，将被完整重建：");
        ImGui::TextWrapped("%s",utf8(workspace_output_path().wstring()).c_str());
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

void draw_extraction_console() {
    ImGui::Begin("抽取控制台",nullptr,ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored(g_extract_process?ImVec4(.45f,.85f,.55f,1.0f):ImVec4(.65f,.65f,.65f,1.0f),g_extract_process?"抽取进行中":"未运行");
    ImGui::SameLine();ImGui::Checkbox("自动滚动",&g_extract_console_auto_scroll);
    ImGui::SameLine();
    if(ImGui::Button("清空")){g_extract_console_lines.clear();g_extract_console_pending.clear();g_extract_console_updated=false;}
    ImGui::Separator();
    ImGui::BeginChild("##extract_console",ImVec2(0,0),false,ImGuiWindowFlags_HorizontalScrollbar);
    if(g_extract_console_lines.empty()&&g_extract_console_pending.empty())ImGui::TextDisabled("抽取开始后将在这里显示实时输出。");
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(g_extract_console_lines.size()));
    while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i)ImGui::TextUnformatted(g_extract_console_lines[static_cast<std::size_t>(i)].c_str());
    if(g_extract_console_auto_scroll&&g_extract_console_updated)ImGui::SetScrollY(ImGui::GetScrollMaxY());
    g_extract_console_updated=false;
    ImGui::EndChild();
    ImGui::End();
}

void draw_editor_shell() {
    poll_workspace_extraction();
    const ImGuiID dockspace=ImGui::GetID("GBFRDockSpace");
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    if(!g_workspace) {
        if(!g_start_layout_built) build_start_dock_layout(dockspace);
        draw_start_screen();
        draw_extraction_console();
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
    if (g_workspace && ImGui::Button("刷新")) {g_workspace->refresh();g_texture_gallery.clear();}
    ImGui::SameLine();
    ImGui::Checkbox("只看修改", &g_changed_only);
    if (!g_workspace) {
        ImGui::Separator();
        ImGui::TextUnformatted("请先选择 unpack 中的 .minfo 文件。");
    } else {
        ImGui::Text("%s | 候选 %zu | 已修改 %zu | 缺失 %zu", g_workspace->character_id().c_str(),
                g_workspace->assets().size(), g_workspace->changed_count(), g_workspace->missing_count());
        const struct {AssetFunction function;const char* label;} filters[]={
            {AssetFunction::all,"全部"},{AssetFunction::model,"模型"},{AssetFunction::texture,"贴图"},{AssetFunction::ui_image,"UI-image"},
            {AssetFunction::material,"mmat"},{AssetFunction::cloth,"cloth"}};
        for(std::size_t i=0;i<std::size(filters);++i){
            const auto count=std::count_if(g_workspace->assets().begin(),g_workspace->assets().end(),[&](const auto& asset){return asset_matches_function(asset,filters[i].function);});
            const auto label=std::string(filters[i].label)+" ("+std::to_string(count)+")##asset_function_"+std::to_string(i);
            if(ImGui::RadioButton(label.c_str(),g_asset_function==filters[i].function))g_asset_function=filters[i].function;
            if(i+1<std::size(filters))ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##asset_search","按文件名、子类或输出路径过滤",g_asset_search.data(),g_asset_search.size());
        std::vector<std::size_t> visible_assets;
        for(std::size_t index=0;index<g_workspace->assets().size();++index){const auto& asset=g_workspace->assets()[index];if(g_changed_only&&!asset.changed)continue;if(!asset_matches_function(asset,g_asset_function)||!asset_matches_search(asset))continue;visible_assets.push_back(index);}
        const auto table_flags=ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable|ImGuiTableFlags_Sortable|ImGuiTableFlags_SortMulti|ImGuiTableFlags_SortTristate;
        if (ImGui::BeginTable("assets", 5, table_flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 80,0);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 90,1);
        ImGui::TableSetupColumn("子类", ImGuiTableColumnFlags_WidthFixed, 80,2);
        ImGui::TableSetupColumn("编辑输入 (unpack)", ImGuiTableColumnFlags_WidthStretch|ImGuiTableColumnFlags_DefaultSort,0,3);
        ImGui::TableSetupColumn("Mod 输出 (build)", ImGuiTableColumnFlags_WidthStretch,0,4);
        ImGui::TableHeadersRow();
        if(auto* specs=ImGui::TableGetSortSpecs();specs&&specs->SpecsCount>0){
            std::stable_sort(visible_assets.begin(),visible_assets.end(),[&](std::size_t left_index,std::size_t right_index){
                const auto& left=g_workspace->assets()[left_index];const auto& right=g_workspace->assets()[right_index];
                for(int spec_index=0;spec_index<specs->SpecsCount;++spec_index){const auto& spec=specs->Specs[spec_index];int order{};switch(spec.ColumnUserID){
                    case 0:{const int a=left.changed?0:left.available?1:2,b=right.changed?0:right.available?1:2;order=a<b?-1:a>b?1:0;break;}
                    case 1:order=compare_ascii_case_insensitive(gbfr::asset_kind_name(left.kind),gbfr::asset_kind_name(right.kind));break;
                    case 2:order=compare_ascii_case_insensitive(left.subtype,right.subtype);break;
                    case 3:order=compare_natural_path(left.input.filename(),right.input.filename());break;
                    case 4:order=compare_natural_path(left.output,right.output);break;
                    default:break;}
                    if(order!=0)return spec.SortDirection==ImGuiSortDirection_Ascending?order<0:order>0;
                }return left_index<right_index;
            });
            specs->SpecsDirty=false;
        }
        for (const auto index:visible_assets) {
            const auto& asset = g_workspace->assets()[index];
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
        if(g_loaded_model){
            ImGui::Text("当前预览：%s",utf8(g_loaded_model->minfo.stem().wstring()).c_str());
            draw_preview_source_inline("模型",g_loaded_model->minfo);
            draw_preview_source_inline("材质 JSON",g_loaded_material);
            draw_preview_source("DDS",g_workspace->root()/L"unpack/data");
            if(!g_loaded_sop.empty())draw_preview_source_inline("SOP",g_loaded_sop);
            else {ImGui::TextDisabled("SOP [无]");ImGui::SameLine(0.0f,12.0f);}
            if(!g_clp_files.empty())draw_preview_source("cloth",g_clp_files.front().path);
            else ImGui::TextDisabled("cloth [无]");
            ImGui::TextDisabled("预览不读取 build；build 仅作为最终 Mod 输出。");
        }
        ImGui::Checkbox("模型", &g_show_mesh); ImGui::SameLine();
        const char* modes[]={"无光照","柔和光照","线框"};int mode=static_cast<int>(g_preview_shading);ImGui::SetNextItemWidth(120);
        if(ImGui::Combo("显示模式",&mode,modes,3))g_preview_shading=static_cast<gbfr::PreviewShadingMode>(mode);ImGui::SameLine();
        ImGui::Checkbox("骨架", &g_show_skeleton); ImGui::SameLine();
        if(ImGui::Checkbox("碰撞体", &g_show_collisions)&&g_show_collisions)update_collision_debug(); ImGui::SameLine();
        if(ImGui::Checkbox("Cloth 连接", &g_show_cloth_links)&&g_show_cloth_links)update_collision_debug(); ImGui::SameLine();
        ImGui::Checkbox("透明覆盖",&g_show_alpha_overlays);ImGui::SameLine();
        if (g_preview && g_preview->has_model() && ImGui::Button("取景")) g_preview->frame(g_camera);
        if(!g_motion_files.empty())draw_motion_controls();
    }else if(g_preview_mode==PreviewMode::texture&&g_preview&&g_preview->texture_image()){
        ImGui::Text("当前预览：%s  |  %u x %u",utf8(g_loaded_texture.filename().wstring()).c_str(),g_preview->texture_width(),g_preview->texture_height());
        ImGui::SameLine(0.0f,16.0f);draw_preview_source("贴图",g_loaded_texture);
        ImGui::TextDisabled("预览不读取 build；build 仅作为最终 Mod 输出。");
    }
    ImVec2 available = ImGui::GetContentRegionAvail();
    if (g_preview&&g_preview_mode==PreviewMode::model&&available.x > 1 && available.y > 1) {
        g_preview->resize(static_cast<unsigned>(available.x), static_cast<unsigned>(available.y));
        g_preview->render(g_camera, g_show_mesh, g_preview_shading, g_show_skeleton, g_show_collisions, g_show_alpha_overlays, g_show_cloth_links);
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
        const ImVec2 uv_min = g_loaded_texture_is_ui ? ImVec2(0,0) : ImVec2(0,1);
        const ImVec2 uv_max = g_loaded_texture_is_ui ? ImVec2(1,1) : ImVec2(1,0);
        ImGui::Image(reinterpret_cast<ImTextureID>(g_preview->texture_image()),image_size,uv_min,uv_max);
    }else{
        ImGui::TextUnformatted("当前对象没有可用预览");
    }
    ImGui::End();

    g_texture_gallery.draw(*g_workspace,*g_preview,g_selected_asset,preview_gallery_texture);

    ImGui::Begin("Inspector");
    if (g_workspace && g_selected_asset) {
        const auto& asset = g_workspace->assets()[*g_selected_asset];
        ImGui::Text("%s / %s", gbfr::asset_kind_name(asset.kind), asset.subtype.c_str());
        ImGui::SeparatorText("文件路径");
        ImGui::TextColored(workspace_area_color(WorkspaceArea::unpack),"编辑输入 (unpack)");
        ImGui::TextWrapped("%s",utf8(asset.input.wstring()).c_str());
        if(!asset.source.empty()){
            ImGui::TextColored(workspace_area_color(WorkspaceArea::source),"原始基线 (source)");
            ImGui::TextWrapped("%s",utf8(asset.source.wstring()).c_str());
        }
        ImGui::TextColored(workspace_area_color(WorkspaceArea::build),"Mod 输出 (build)");
        ImGui::TextWrapped("%s",utf8(asset.output.wstring()).c_str());
        ImGui::Separator();
        if (asset.kind == gbfr::AssetKind::model) {
            if (ImGui::Button("重新加载预览")) load_model_preview(*g_selected_asset,true);
            if (ImGui::Button("从 unpack 复制到 build")) run_selected_asset_action(false);
            if (ImGui::Button("从 source 恢复 unpack")) run_selected_asset_action(true);
        } else if (asset.kind == gbfr::AssetKind::texture || asset.kind == gbfr::AssetKind::ui_image) {
            if (ImGui::Button("重新加载预览")) preview_asset(*g_selected_asset);
            if (ImGui::Button("封回 WTB 到 build")) run_selected_asset_action(false);
            if (ImGui::Button("从 source 恢复 DDS")) run_selected_asset_action(true);
        } else {
            ImGui::TextUnformatted("当前 C++ 版本尚未实现此类型的构建操作。");
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
        if(ImGui::BeginTabBar("skeleton_details")){
        if(ImGui::BeginTabItem("SOP 约束")){
            g_sop_inspector.draw(g_skeleton,g_bone_names,g_selected_bone);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Cloth 物理")){
        if(!g_clp_files.empty()){
            g_selected_clp=std::clamp(g_selected_clp,0,static_cast<int>(g_clp_files.size()-1));
            const auto& selected_group=g_clp_files[g_selected_clp].data;
            const auto current="CLP "+std::to_string(selected_group.id)+" | 节点 "+std::to_string(selected_group.nodes.size());
            if(ImGui::BeginCombo("求解组",current.c_str())){for(int i=0;i<static_cast<int>(g_clp_files.size());++i){const auto& group=g_clp_files[i].data;const auto label="CLP "+std::to_string(group.id)+" | 节点 "+std::to_string(group.nodes.size())+" | CLH "+collision_layer_list(group.collision_flags);if(ImGui::Selectable(label.c_str(),i==g_selected_clp)){g_selected_clp=i;g_all_clp_groups=false;g_selected_collision=-1;const int mask=group.collision_flags;const auto found=std::find_if(g_clh_files.begin(),g_clh_files.end(),[&](const auto& file){return file.group_id>=0&&file.group_id<31&&(mask&(1<<file.group_id));});if(found!=g_clh_files.end())g_selected_clh=static_cast<int>(std::distance(g_clh_files.begin(),found));update_collision_debug();}}ImGui::EndCombo();}
            if(ImGui::Checkbox("全部求解组",&g_all_clp_groups))update_collision_debug();ImGui::SameLine();if(ImGui::Checkbox("全部骨骼",&g_all_bones))update_collision_debug();
            const int mask=active_collision_mask();ImGui::Text("useCollisionFlags: %d | 使用 CLH: %s",mask,collision_layer_list(mask).c_str());
        } else ImGui::TextUnformatted("工作区没有可用 CLP XML。");
        if(!g_clh_files.empty()){
            ImGui::SeparatorText("CLH 碰撞层");
            g_selected_clh=std::clamp(g_selected_clh,0,static_cast<int>(g_clh_files.size()-1));
            const int preview_mask=active_collision_mask();const auto& current_layer=g_clh_files[g_selected_clh];
            const auto current="CLH "+std::to_string(current_layer.group_id)+" | "+std::to_string(current_layer.data.collisions.size())+" 端点";
            if(ImGui::BeginCombo("编辑层",current.c_str())){for(int i=0;i<static_cast<int>(g_clh_files.size());++i){const auto& layer=g_clh_files[i];const bool active=layer.group_id>=0&&layer.group_id<31&&(preview_mask&(1<<layer.group_id));const auto label="CLH "+std::to_string(layer.group_id)+" | "+std::to_string(layer.data.collisions.size())+" 端点"+(active?" | 当前使用":"");if(ImGui::Selectable(label.c_str(),i==g_selected_clh)){g_selected_clh=i;g_selected_collision=-1;}}ImGui::EndCombo();}
            const auto& active_layer=g_clh_files[g_selected_clh];const bool layer_active=active_layer.group_id>=0&&active_layer.group_id<31&&(preview_mask&(1<<active_layer.group_id));ImGui::SameLine();ImGui::TextUnformatted(layer_active?"当前求解组使用":"当前求解组未使用");
            auto& collisions=g_clh_files[g_selected_clh].data.collisions;
            const auto capsule_count=std::count_if(collisions.begin(),collisions.end(),[](const auto& collision){return collision.capsule>=0;});
            const auto invalid_capsules=std::count_if(collisions.begin(),collisions.end(),[&](const auto& collision){return collision.capsule>=0&&std::none_of(collisions.begin(),collisions.end(),[&](const auto& other){return other.id==collision.capsule;});});
            ImGui::Text("端点 %zu | 胶囊连接 %zu | 无效引用 %zu",collisions.size(),static_cast<std::size_t>(capsule_count),static_cast<std::size_t>(invalid_capsules));
            if(ImGui::BeginTable("collisions",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,220))){ImGui::TableSetupColumn("ID");ImGui::TableSetupColumn("附着 P1");ImGui::TableSetupColumn("附着 P2");ImGui::TableSetupColumn("半径");ImGui::TableSetupColumn("权重");ImGui::TableSetupColumn("形状");ImGui::TableHeadersRow();for(int i=0;i<static_cast<int>(collisions.size());++i){const auto& c=collisions[i];if(!g_all_bones&&g_selected_bone>=0&&c.p1!=g_selected_bone&&c.p2!=g_selected_bone)continue;ImGui::TableNextRow();ImGui::TableNextColumn();if(ImGui::Selectable((std::to_string(c.id)+"##collision"+std::to_string(i)).c_str(),g_selected_collision==i,ImGuiSelectableFlags_SpanAllColumns))g_selected_collision=i;ImGui::TableNextColumn();ImGui::Text("%d",c.p1);ImGui::TableNextColumn();ImGui::Text("%d",c.p2);ImGui::TableNextColumn();ImGui::Text("%.4f",c.radius);ImGui::TableNextColumn();ImGui::Text("%.3f",c.weight);ImGui::TableNextColumn();if(c.capsule<0)ImGui::TextUnformatted("球");else if(std::any_of(collisions.begin(),collisions.end(),[&](const auto& other){return other.id==c.capsule;}))ImGui::Text("胶囊 -> %d",c.capsule);else ImGui::Text("无效 -> %d",c.capsule);}ImGui::EndTable();}
            if(g_selected_collision>=0&&g_selected_collision<static_cast<int>(collisions.size())){auto& c=collisions[g_selected_collision];ImGui::SeparatorText("碰撞属性");ImGui::DragFloat("半径",&c.radius,.001f,.0f,10.f,"%.4f");ImGui::DragFloat("权重",&c.weight,.01f,-100.f,100.f,"%.3f");ImGui::DragFloat4("Offset 1",&c.offset1.x,.001f);ImGui::DragFloat4("Offset 2",&c.offset2.x,.001f);if(ImGui::Button("保存到 unpack"))save_selected_collision();ImGui::SameLine();if(ImGui::Button("刷新预览"))update_collision_debug();}
        } else ImGui::TextUnformatted("工作区没有可用 CLH XML。");
        if(!g_clp_files.empty()&&ImGui::CollapsingHeader("CLP 节点",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextColored(ImVec4(.3f,1.f,.22f,1),"Down 纵向");ImGui::SameLine();ImGui::TextColored(ImVec4(1.f,.25f,.8f,1),"Side 横向");ImGui::SameLine();ImGui::TextColored(ImVec4(1.f,.72f,.1f,1),"Poly 独立关系");
            if(ImGui::BeginTable("clp_nodes",10,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY,ImVec2(0,180))){const char* columns[]={"组","骨骼","Up","Down","Side","Poly","旋转限制","摩擦","重量","厚度"};for(const char* column:columns)ImGui::TableSetupColumn(column);ImGui::TableHeadersRow();for(std::size_t file_index=0;file_index<g_clp_files.size();++file_index){if(!solver_group_visible(file_index))continue;const auto& file=g_clp_files[file_index];for(const auto& node:file.data.nodes){if(!g_all_bones&&g_selected_bone>=0&&node.bone!=g_selected_bone&&node.up!=g_selected_bone&&node.down!=g_selected_bone&&node.side!=g_selected_bone&&node.poly!=g_selected_bone)continue;ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%d",file.data.id);ImGui::TableNextColumn();ImGui::Text("%d",node.bone);ImGui::TableNextColumn();ImGui::Text("%d",node.up);ImGui::TableNextColumn();ImGui::Text("%d",node.down);ImGui::TableNextColumn();ImGui::Text("%d",node.side);ImGui::TableNextColumn();ImGui::Text("%d",node.poly);ImGui::TableNextColumn();ImGui::Text("%.3f",node.rotation_limit);ImGui::TableNextColumn();ImGui::Text("%.3f",node.friction);ImGui::TableNextColumn();ImGui::Text("%.3f",node.weight);ImGui::TableNextColumn();ImGui::Text("%.3f",node.thickness);}}ImGui::EndTable();}
        }
        ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Begin("Log");
    for (const auto& entry : gbfr::Log::snapshot()) ImGui::TextUnformatted(entry.message.c_str());
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
    if(!g_sop_inspector.load_catalog(tool_root()/L"_lib/sop_operations_zh.json"))gbfr::Log::write(gbfr::LogLevel::warning,"SOP 操作目录加载失败，约束检查器将显示未知类型");

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
    if(g_extract_output_read){CloseHandle(g_extract_output_read);g_extract_output_read=nullptr;}
    if(g_extract_process) { CloseHandle(g_extract_process); g_extract_process=nullptr; }
    g_preview = nullptr;
    g_d3d = nullptr;
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
