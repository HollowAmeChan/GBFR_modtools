#pragma once

#include <gbfr/render/preview_renderer.hpp>
#include <DirectXMath.h>

#include <cstdint>

namespace gbfr::render_detail {
struct GpuVertex {
    float position[3], normal[3], uv[2];
    std::uint16_t joints[4];
    float weights[4];
};

struct SceneConstants {
    DirectX::XMFLOAT4X4 view_projection;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT4 light;
    unsigned textured, eye_material, lighting, alpha_blended;
    unsigned alpha_masked, alpha_clipped, skinning_enabled;
    float alpha_threshold;
};

struct BoneConstants {
    DirectX::XMFLOAT4X4 skin[PreviewRenderer::max_skin_bones];
};

static_assert(sizeof(GpuVertex) == 56);
static_assert(sizeof(SceneConstants) == 128);
static_assert(sizeof(BoneConstants) == 32768);
}
