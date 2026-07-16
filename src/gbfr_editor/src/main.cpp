#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/render/d3d11_context.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <imgui.h>
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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
gbfr::D3D11Context* g_d3d = nullptr;
gbfr::PreviewRenderer* g_preview = nullptr;
std::unique_ptr<gbfr::Workspace> g_workspace;
std::string g_imgui_ini;
std::optional<std::size_t> g_selected_asset;
bool g_changed_only = false;
gbfr::OrbitCamera g_camera;
gbfr::SkeletonAsset g_skeleton;
bool g_show_mesh = true;
bool g_wireframe = false;
bool g_show_skeleton = true;
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

std::filesystem::path tool_root() {
    wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768);
    auto path=std::filesystem::path(module).parent_path();
    if(path.filename()==L"Debug"||path.filename()==L"Release"||path.filename()==L"RelWithDebInfo") path=path.parent_path().parent_path().parent_path();
    return path;
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

void load_workspace(const std::filesystem::path& path) {
    try {
        g_workspace = std::make_unique<gbfr::Workspace>(gbfr::Workspace::load(path));
        g_selected_asset.reset();
        const auto settings_directory=g_workspace->root()/L".gbfr";
        std::filesystem::create_directories(settings_directory);
        g_imgui_ini=utf8((settings_directory/L"imgui.ini").wstring());
        ImGui::GetIO().IniFilename=g_imgui_ini.c_str();
        if(std::filesystem::is_regular_file(settings_directory/L"imgui.ini"))ImGui::LoadIniSettingsFromDisk(g_imgui_ini.c_str());
        gbfr::Log::write(gbfr::LogLevel::info, "工作区已加载：" + utf8(g_workspace->root().wstring()));
    } catch (const std::exception& error) {
        gbfr::Log::write(gbfr::LogLevel::error, std::string("工作区加载失败：") + error.what());
    }
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

void run_selected_model_action(bool restore) {
    if (!g_workspace || !g_selected_asset) return;
    try {
        if (restore) g_workspace->restore_model(*g_selected_asset);
        else g_workspace->build_model(*g_selected_asset);
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

void load_selected_preview() {
    if (!g_workspace || !g_selected_asset || !g_preview) return;
    const auto& selected = g_workspace->assets()[*g_selected_asset];
    if (selected.kind != gbfr::AssetKind::model) return;
    try {
        std::filesystem::path minfo;
        if (selected.subtype == "minfo") minfo = selected.input;
        else {
            const auto stem = selected.input.stem();
            for (const auto& asset : g_workspace->assets()) if (asset.kind == gbfr::AssetKind::model && asset.subtype == "minfo" && asset.input.stem() == stem) { minfo = asset.input; break; }
        }
        if (minfo.empty()) throw std::runtime_error("找不到同名 minfo");
        const auto stem = minfo.stem();
        const auto skeleton_path = minfo.parent_path() / (stem.wstring() + L".skeleton");
        std::filesystem::path mesh_path;
        for (const auto& asset : g_workspace->assets()) if (asset.kind == gbfr::AssetKind::model && asset.subtype == "mmesh" && asset.input.stem() == stem) { mesh_path = asset.input; break; }
        if (mesh_path.empty()) throw std::runtime_error("找不到同名 LOD0 mmesh");
        const auto info = gbfr::load_minfo(minfo);
        g_skeleton = gbfr::load_skeleton(skeleton_path);
        const auto mesh = gbfr::load_mmesh(mesh_path, info);
        std::filesystem::path albedo;
        const auto granite = g_workspace->root() / L"unpack/data/granite/2k";
        if (std::filesystem::is_directory(granite)) for (const auto& file : std::filesystem::recursive_directory_iterator(granite)) {
            const auto name = file.path().filename().wstring();
            if (file.is_regular_file() && file.path().extension() == L".dds" && name.starts_with(stem.wstring() + L"_") && name.find(L"_albd") != std::wstring::npos) { albedo = file.path(); break; }
        }
        if (!g_preview->load(mesh, g_skeleton, albedo)) throw std::runtime_error("GPU 预览资源创建失败");
        g_preview->frame(g_camera);
        g_clh_files.clear(); g_clp_files.clear(); g_selected_collision=-1; g_selected_bone=-1; g_selected_clh=0;
        for(const auto& asset:g_workspace->assets()) if(asset.kind==gbfr::AssetKind::cloth&&asset.available) {
            try { if(asset.subtype=="clh") g_clh_files.push_back({asset.input,gbfr::load_clh(asset.input)}); else if(asset.subtype=="clp") g_clp_files.push_back({asset.input,gbfr::load_clp(asset.input)}); } catch(const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::warning,std::string("cloth 跳过：")+error.what()); }
        }
        update_collision_debug();
        gbfr::Log::write(gbfr::LogLevel::info, "预览已加载：" + std::to_string(mesh.vertices.size()) + " 顶点，" + std::to_string(mesh.indices.size()/3) + " 三角形，" + std::to_string(g_skeleton.bones.size()) + " 骨骼");
    } catch (const std::exception& error) { gbfr::Log::write(gbfr::LogLevel::error, std::string("预览加载失败：") + error.what()); }
}

gbfr::Vec3 collision_point(int id,const gbfr::Vec4& offset) {
    for(const auto& bone:g_skeleton.bones) if(cloth_bone_id(bone.name)==id) return {bone.world_position.x+offset.x,bone.world_position.y+offset.y,bone.world_position.z+offset.z};
    return {offset.x,offset.y,offset.z};
}

void append_circle(std::vector<gbfr::Vec3>& lines,gbfr::Vec3 center,float radius,int plane) {
    constexpr int segments=20; constexpr float tau=6.283185307f;
    for(int i=0;i<segments;++i){const float a=tau*i/segments,b=tau*(i+1)/segments;gbfr::Vec3 p=center,q=center;const float ca=std::cos(a)*radius,sa=std::sin(a)*radius,cb=std::cos(b)*radius,sb=std::sin(b)*radius;if(plane==0){p.x+=ca;p.y+=sa;q.x+=cb;q.y+=sb;}else if(plane==1){p.x+=ca;p.z+=sa;q.x+=cb;q.z+=sb;}else{p.y+=ca;p.z+=sa;q.y+=cb;q.z+=sb;}lines.push_back(p);lines.push_back(q);}
}

void update_collision_debug() {
    if(!g_preview)return;std::vector<gbfr::Vec3> lines;
    for(std::size_t file_index=0;file_index<g_clh_files.size();++file_index){if(!g_all_clh_files&&static_cast<int>(file_index)!=g_selected_clh)continue;for(const auto& c:g_clh_files[file_index].data.collisions){if(!g_all_bones&&g_selected_bone>=0&&c.p1!=g_selected_bone&&c.p2!=g_selected_bone)continue;const auto p=collision_point(c.p1,c.offset1),q=collision_point(c.p2,c.offset2);lines.push_back(p);lines.push_back(q);append_circle(lines,p,c.radius,0);append_circle(lines,p,c.radius,1);append_circle(lines,p,c.radius,2);if(c.p1!=c.p2||c.offset1.x!=c.offset2.x||c.offset1.y!=c.offset2.y||c.offset1.z!=c.offset2.z){append_circle(lines,q,c.radius,0);append_circle(lines,q,c.radius,1);append_circle(lines,q,c.radius,2);}}}g_preview->set_collision_lines(lines);
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

void draw_editor_shell() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::Begin("Workspace");
    if (ImGui::Button("打开工作区...")) choose_workspace();
    ImGui::SameLine();
    if (g_workspace && ImGui::Button("刷新")) g_workspace->refresh();
    ImGui::SameLine();
    if (g_workspace && ImGui::Button("旧版构建器")) launch_legacy_builder();
    ImGui::SameLine();
    ImGui::Checkbox("只看修改", &g_changed_only);
    if (!g_workspace) {
        ImGui::TextUnformatted("请选择 manifest.md 或 workspace.json");
        ImGui::End();
        return;
    }

    ImGui::Text("%s | 候选 %zu | 已修改 %zu | 缺失 %zu", g_workspace->character_id().c_str(),
                g_workspace->assets().size(), g_workspace->changed_count(), g_workspace->missing_count());
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
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string id = (asset.changed ? "已修改##" : asset.available ? "未修改##" : "缺失##") + std::to_string(index);
            if (ImGui::Selectable(id.c_str(), g_selected_asset == index, ImGuiSelectableFlags_SpanAllColumns)) g_selected_asset = index;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(gbfr::asset_kind_name(asset.kind));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(asset.subtype.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(utf8(asset.input.filename().wstring()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", utf8(asset.input.wstring()).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(utf8(asset.output.lexically_relative(g_workspace->root()).wstring()).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();

    ImGui::Begin("Viewport");
    ImGui::Checkbox("网格", &g_show_mesh); ImGui::SameLine();
    ImGui::Checkbox("线框", &g_wireframe); ImGui::SameLine();
    ImGui::Checkbox("骨架", &g_show_skeleton); ImGui::SameLine();
    if (g_preview && g_preview->has_model() && ImGui::Button("取景")) g_preview->frame(g_camera);
    ImVec2 available = ImGui::GetContentRegionAvail();
    if (g_preview && available.x > 1 && available.y > 1) {
        g_preview->resize(static_cast<unsigned>(available.x), static_cast<unsigned>(available.y));
        g_preview->render(g_camera, g_show_mesh, g_wireframe, g_show_skeleton);
        const ImVec2 image_origin=ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(g_preview->image()), available);
        if (ImGui::IsItemHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) { g_camera.yaw += io.MouseDelta.x * .008f; g_camera.pitch = std::clamp(g_camera.pitch + io.MouseDelta.y * .008f, -1.5f, 1.5f); }
            if (io.MouseWheel != 0) g_camera.distance = std::max(.02f, g_camera.distance * std::pow(.88f, io.MouseWheel));
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { const float scale = g_camera.distance * .0015f; g_camera.target.x -= io.MouseDelta.x * scale; g_camera.target.y += io.MouseDelta.y * scale; }
            if (g_show_skeleton && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const float mx=io.MousePos.x-image_origin.x,my=io.MousePos.y-image_origin.y;float best=144.0f;int picked=-1;
                for(const auto& bone:g_skeleton.bones){gbfr::Vec2 point{};if(!g_preview->project(bone.world_position,g_camera,point))continue;const float dx=point.x-mx,dy=point.y-my,distance=dx*dx+dy*dy;if(distance<best){best=distance;picked=cloth_bone_id(bone.name);}}
                if(picked>=0){g_selected_bone=picked;g_all_bones=false;update_collision_debug();}
            }
        }
    }
    ImGui::End();

    ImGui::Begin("Inspector");
    if (g_workspace && g_selected_asset) {
        const auto& asset = g_workspace->assets()[*g_selected_asset];
        ImGui::Text("%s / %s", gbfr::asset_kind_name(asset.kind), asset.subtype.c_str());
        ImGui::TextWrapped("%s", utf8(asset.input.wstring()).c_str());
        ImGui::Separator();
        if (asset.kind == gbfr::AssetKind::model) {
            if (ImGui::Button("加载预览")) load_selected_preview();
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
        const char* rows[][3]={{"工作区列表 / SHA-256 / 状态","C++","原生"},{"minfo / skeleton / mmesh","C++","原生"},{"模型构建与恢复","C++","原生"},{"D3D11 网格 / DDS / 骨架预览","C++","原生"},{"CLH / CLP 查看与 CLH 编辑","C++","原生"},{"texture 封回与恢复","PowerShell + _lib","兼容入口"},{"mmat 编码 / A4 快捷编辑","PowerShell + _lib","兼容入口"},{"cloth BXM 编码与恢复","PowerShell + _lib","兼容入口"},{"新建 .texture","PowerShell + nier_cli","兼容入口"}};
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
    if (!preview.initialize(d3d.device(), d3d.context())) return 2;
    g_preview = &preview;
    load_bone_names();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(d3d.device(), d3d.context());

    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, nullptr,
                                     io.Fonts->GetGlyphRangesChineseFull());
    }

    int argument_count{};
    if (wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count)) {
        if (argument_count > 1) load_workspace(arguments[1]);
        LocalFree(arguments);
    }
    if (!g_workspace) {
        const auto candidate = tool_root() / L"explore_output" / L"workspace.json";
        if (std::filesystem::is_regular_file(candidate)) load_workspace(candidate);
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
    g_preview = nullptr;
    g_d3d = nullptr;
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
