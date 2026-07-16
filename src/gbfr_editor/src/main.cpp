#include <gbfr/core/log.hpp>
#include <gbfr/render/d3d11_context.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
gbfr::D3D11Context* g_d3d = nullptr;

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
    ImGui::TextUnformatted("No workspace loaded");
    ImGui::End();

    ImGui::Begin("Viewport");
    ImGui::TextUnformatted("Model preview will be initialized in M3");
    ImGui::End();

    ImGui::Begin("Inspector");
    ImGui::TextUnformatted("Select an asset or bone");
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

    gbfr::Log::write(gbfr::LogLevel::info, "M0 editor shell initialized");
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
