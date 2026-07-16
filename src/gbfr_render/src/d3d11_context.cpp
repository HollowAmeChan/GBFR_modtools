#include <gbfr/render/d3d11_context.hpp>

namespace gbfr {
bool D3D11Context::initialize(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level{};
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, 2, D3D11_SDK_VERSION,
        &desc, swap_chain_.GetAddressOf(), device_.GetAddressOf(), &feature_level, context_.GetAddressOf());
    return SUCCEEDED(hr) && create_render_target();
}

bool D3D11Context::create_render_target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())))) return false;
    return SUCCEEDED(device_->CreateRenderTargetView(back_buffer.Get(), nullptr, render_target_.GetAddressOf()));
}

void D3D11Context::destroy_render_target() { render_target_.Reset(); }

void D3D11Context::resize(unsigned width, unsigned height) {
    if (!swap_chain_ || width == 0 || height == 0) return;
    destroy_render_target();
    swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    create_render_target();
}

void D3D11Context::begin_frame(float r, float g, float b) {
    const float color[4] = {r, g, b, 1.0f};
    ID3D11RenderTargetView* target = render_target_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->ClearRenderTargetView(target, color);
}

void D3D11Context::present() { swap_chain_->Present(1, 0); }

void D3D11Context::shutdown() {
    destroy_render_target();
    swap_chain_.Reset();
    context_.Reset();
    device_.Reset();
}
}
