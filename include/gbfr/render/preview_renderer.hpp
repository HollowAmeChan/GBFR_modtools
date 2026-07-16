#pragma once
#include <gbfr/formats/model.hpp>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>

namespace gbfr {
struct OrbitCamera { float yaw{3.14159f}, pitch{0.15f}, distance{4.0f}; Vec3 target{0,1,0}; };

class PreviewRenderer {
public:
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    bool load(const MeshAsset& mesh, const SkeletonAsset& skeleton,
              const std::vector<std::filesystem::path>& material_albedos = {});
    bool load_texture_preview(const std::filesystem::path& dds);
    void clear();
    void set_collision_lines(const std::vector<Vec3>& points);
    void resize(unsigned width, unsigned height);
    void render(const OrbitCamera& camera, bool show_mesh, bool wireframe, bool show_skeleton);
    void frame(OrbitCamera& camera) const;
    bool project(Vec3 world, const OrbitCamera& camera, Vec2& screen) const;
    ID3D11ShaderResourceView* image() const noexcept { return color_srv_.Get(); }
    ID3D11ShaderResourceView* texture_image() const noexcept { return texture_preview_srv_.Get(); }
    unsigned texture_width() const noexcept { return texture_width_; }
    unsigned texture_height() const noexcept { return texture_height_; }
    unsigned width() const noexcept { return width_; }
    unsigned height() const noexcept { return height_; }
    bool has_model() const noexcept { return index_count_ != 0; }
private:
    bool create_targets();
    struct DrawRange {
        unsigned first_index{};
        unsigned index_count{};
        std::uint8_t material{};
    };
    bool load_dds(const std::filesystem::path& path,
                  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output,
                  unsigned* width = nullptr, unsigned* height = nullptr);
    ID3D11Device* device_{};
    ID3D11DeviceContext* context_{};
    unsigned width_{1}, height_{1}, texture_width_{}, texture_height_{}, index_count_{}, line_vertex_count_{};
    unsigned bone_point_vertex_count_{}, collision_vertex_count_{};
    Vec3 bounds_min_{}, bounds_max_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color_, depth_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> color_rtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> color_srv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture_preview_srv_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_dsv_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_, indices_, lines_, bone_points_, constants_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> collision_lines_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> solid_, wire_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> overlay_depth_;
    std::vector<DrawRange> draw_ranges_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> material_albedos_;
};
}
