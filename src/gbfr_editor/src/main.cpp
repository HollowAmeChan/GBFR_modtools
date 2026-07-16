#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/render/d3d11_context.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <memory>
#include <optional>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
gbfr::D3D11Context* g_d3d = nullptr;
std::unique_ptr<gbfr::Workspace> g_workspace;
std::optional<std::size_t> g_selected_asset;
bool g_changed_only = false;

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
    const auto launcher = std::filesystem::current_path() / L"_lib" / L"launch_workspace_builder.vbs";
    const std::wstring arguments = L"\"" + launcher.wstring() + L"\" \"" + (g_workspace->root() / L"manifest.md").wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"wscript.exe", arguments.c_str(), nullptr, SW_HIDE);
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
    ImGui::TextUnformatted("Model preview will be initialized in M3");
    ImGui::End();

    ImGui::Begin("Inspector");
    if (g_workspace && g_selected_asset) {
        const auto& asset = g_workspace->assets()[*g_selected_asset];
        ImGui::Text("%s / %s", gbfr::asset_kind_name(asset.kind), asset.subtype.c_str());
        ImGui::TextWrapped("%s", utf8(asset.input.wstring()).c_str());
        ImGui::Separator();
        if (asset.kind == gbfr::AssetKind::model) {
            if (ImGui::Button("写入 build")) run_selected_model_action(false);
            ImGui::SameLine();
            if (ImGui::Button("恢复 unpack")) run_selected_model_action(true);
        } else {
            ImGui::TextUnformatted("此类型继续由旧版构建器处理。M5 前保持兼容入口。");
        }
    } else ImGui::TextUnformatted("选择一个资源");
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
        const auto candidate = std::filesystem::current_path() / L"explore_output" / L"workspace.json";
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
    g_d3d = nullptr;
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
