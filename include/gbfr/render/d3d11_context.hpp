#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace gbfr {
class D3D11Context {
public:
    bool initialize(HWND window);
    void resize(unsigned width, unsigned height);
    void begin_frame(float r, float g, float b);
    void present();
    void shutdown();

    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }

private:
    bool create_render_target();
    void destroy_render_target();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_;
};
}
