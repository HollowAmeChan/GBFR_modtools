#include <gbfr/render/preview_renderer.hpp>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
namespace fs = std::filesystem;
namespace {
struct GpuVertex { float position[3], normal[3], uv[2]; };
struct Constants { XMFLOAT4X4 view_projection; XMFLOAT4 color; XMFLOAT4 light; unsigned textured; unsigned eye_material; unsigned lighting; float padding; };

const char* shader_source = R"(
cbuffer Scene : register(b0) { float4x4 viewProjection; float4 color; float4 light; uint textured; uint eyeMaterial; uint lighting; };
struct VSIn { float3 position:POSITION; float3 normal:NORMAL; float2 uv:TEXCOORD0; };
struct VSOut { float4 position:SV_POSITION; float3 normal:NORMAL; float2 uv:TEXCOORD0; };
VSOut VSMain(VSIn v) { VSOut o; o.position=mul(float4(v.position,1),viewProjection); o.normal=v.normal; o.uv=v.uv; return o; }
Texture2D primaryTexture:register(t0); Texture2D irisTexture:register(t1); Texture2D highlightTexture:register(t2); SamplerState linearSampler:register(s0);
float4 PSMain(VSOut i):SV_TARGET {
    float2 uv=float2(i.uv.x,1.0-i.uv.y);
    float4 primary=textured?primaryTexture.Sample(linearSampler,uv):color;
    float3 base=primary.rgb;
    if(eyeMaterial) {
        float4 iris=irisTexture.Sample(linearSampler,uv);
        float4 highlight=highlightTexture.Sample(linearSampler,uv);
        base=lerp(float3(.94,.92,.90),primary.rgb,primary.a);
        base=lerp(base,iris.rgb,iris.a);
        base=lerp(base,highlight.rgb,highlight.a);
    }
    if(!lighting) return float4(base,color.a);
    float halfLambert=dot(normalize(i.normal),normalize(light.xyz))*.5+.5;
    float shade=halfLambert>.72?1.04:(halfLambert>.43?.86:.68);
    return float4(saturate(base*shade+float3(.025,.03,.035)),color.a);
}
)";

bool compile(const char* entry,const char* target,ComPtr<ID3DBlob>& output) {
    ComPtr<ID3DBlob> errors;
    return SUCCEEDED(D3DCompile(shader_source,std::strlen(shader_source),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&output,&errors));
}

template<class T> bool create_buffer(ID3D11Device* device,const std::vector<T>& data,UINT bind,ComPtr<ID3D11Buffer>& output) {
    if(data.empty()) return false;
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=static_cast<UINT>(data.size()*sizeof(T)); desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA initial{data.data()}; return SUCCEEDED(device->CreateBuffer(&desc,&initial,&output));
}
}

namespace gbfr {
bool PreviewRenderer::initialize(ID3D11Device* device,ID3D11DeviceContext* context) {
    device_=device; context_=context;
    ComPtr<ID3DBlob> vs,ps;
    if(!compile("VSMain","vs_5_0",vs)||!compile("PSMain","ps_5_0",ps)) return false;
    if(FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_shader_))||
       FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_shader_))) return false;
    D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(device_->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&input_layout_))) return false;
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device_->CreateBuffer(&cb,nullptr,&constants_))) return false;
    D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD=D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd,&sampler_);
    D3D11_RASTERIZER_DESC rd{}; rd.CullMode=D3D11_CULL_NONE; rd.FillMode=D3D11_FILL_SOLID; rd.DepthClipEnable=TRUE; device_->CreateRasterizerState(&rd,&solid_); rd.FillMode=D3D11_FILL_WIREFRAME; device_->CreateRasterizerState(&rd,&wire_);
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    if(FAILED(device_->CreateDepthStencilState(&depth,&overlay_depth_)))return false;
    return create_targets();
}

bool PreviewRenderer::create_targets() {
    color_.Reset(); color_rtv_.Reset(); color_srv_.Reset(); depth_.Reset(); depth_dsv_.Reset();
    D3D11_TEXTURE2D_DESC td{}; td.Width=width_; td.Height=height_; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS; td.SampleDesc.Count=1; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
    if(FAILED(device_->CreateTexture2D(&td,nullptr,&color_))||FAILED(device_->CreateRenderTargetView(color_.Get(),&rtv,&color_rtv_))||FAILED(device_->CreateShaderResourceView(color_.Get(),&srv,&color_srv_))) return false;
    td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    return SUCCEEDED(device_->CreateTexture2D(&td,nullptr,&depth_))&&SUCCEEDED(device_->CreateDepthStencilView(depth_.Get(),nullptr,&depth_dsv_));
}

void PreviewRenderer::resize(unsigned width,unsigned height) { width=std::max(1u,width); height=std::max(1u,height); if(width==width_&&height==height_) return; width_=width;height_=height;create_targets(); }

bool PreviewRenderer::load(const MeshAsset& mesh,const SkeletonAsset& skeleton,const std::vector<PreviewMaterialTextures>& materials) {
    clear();
    std::vector<GpuVertex> vertices; vertices.reserve(mesh.vertices.size());
    bounds_min_={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()}; bounds_max_={-bounds_min_.x,-bounds_min_.y,-bounds_min_.z};
    for(const auto& v:mesh.vertices) { vertices.push_back({{v.position.x,v.position.y,v.position.z},{v.normal.x,v.normal.y,v.normal.z},{v.uv.x,v.uv.y}}); bounds_min_.x=std::min(bounds_min_.x,v.position.x);bounds_min_.y=std::min(bounds_min_.y,v.position.y);bounds_min_.z=std::min(bounds_min_.z,v.position.z);bounds_max_.x=std::max(bounds_max_.x,v.position.x);bounds_max_.y=std::max(bounds_max_.y,v.position.y);bounds_max_.z=std::max(bounds_max_.z,v.position.z); }
    if(!create_buffer(device_,vertices,D3D11_BIND_VERTEX_BUFFER,vertices_)||!create_buffer(device_,mesh.indices,D3D11_BIND_INDEX_BUFFER,indices_)) return false;
    index_count_=static_cast<unsigned>(mesh.indices.size());
    draw_ranges_.reserve(mesh.chunks.size());
    for(const auto& chunk:mesh.chunks) {
        if(!chunk.count) continue;
        if(chunk.offset>index_count_||chunk.count>index_count_-chunk.offset) return false;
        draw_ranges_.push_back({chunk.offset,chunk.count,chunk.material});
    }
    if(draw_ranges_.empty()) draw_ranges_.push_back({0,index_count_,0});

    materials_.resize(materials.size());
    for(std::size_t i=0;i<materials.size();++i) {
        auto& gpu=materials_[i];const auto& source=materials[i];
        if(!source.albedo.empty()&&!load_dds(source.albedo,gpu.primary)) return false;
        if(!source.eye_conjunctiva.empty()&&!source.eye_iris.empty()&&!source.eye_highlight.empty()) {
            gpu.eye=load_dds(source.eye_conjunctiva,gpu.primary)&&load_dds(source.eye_iris,gpu.iris)&&load_dds(source.eye_highlight,gpu.highlight);
            if(!gpu.eye) return false;
        }
    }

    std::vector<GpuVertex> lines;
    for(std::size_t i=0;i<skeleton.bones.size();++i) { const auto& b=skeleton.bones[i]; if(b.parent==0xffff||b.parent>=skeleton.bones.size()) continue; const auto& p=skeleton.bones[b.parent].world_position; lines.push_back({{p.x,p.y,p.z},{0,1,0},{0,0}});lines.push_back({{b.world_position.x,b.world_position.y,b.world_position.z},{0,1,0},{0,0}}); }
    line_vertex_count_=static_cast<unsigned>(lines.size()); if(!lines.empty()) create_buffer(device_,lines,D3D11_BIND_VERTEX_BUFFER,lines_);
    const float dx=bounds_max_.x-bounds_min_.x,dy=bounds_max_.y-bounds_min_.y,dz=bounds_max_.z-bounds_min_.z;
    const float marker=std::max(.001f,std::sqrt(dx*dx+dy*dy+dz*dz)*.004f);std::vector<GpuVertex> points;points.reserve(skeleton.bones.size()*6);
    for(const auto& bone:skeleton.bones){const auto p=bone.world_position;points.push_back({{p.x-marker,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x+marker,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y-marker,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y+marker,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z-marker},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z+marker},{0,1,0},{0,0}});}
    bone_point_vertex_count_=static_cast<unsigned>(points.size());if(!points.empty())create_buffer(device_,points,D3D11_BIND_VERTEX_BUFFER,bone_points_);
    return true;
}

bool PreviewRenderer::load_texture_preview(const fs::path& dds) {
    ComPtr<ID3D11ShaderResourceView> texture;
    unsigned width{},height{};
    if(!load_dds(dds,texture,&width,&height,true)) return false;
    texture_preview_srv_=std::move(texture);texture_width_=width;texture_height_=height;
    return true;
}

void PreviewRenderer::clear() {
    index_count_=0; line_vertex_count_=0; bone_point_vertex_count_=0; collision_vertex_count_=0;
    vertices_.Reset(); indices_.Reset(); lines_.Reset(); bone_points_.Reset(); collision_lines_.Reset();
    draw_ranges_.clear(); materials_.clear();
    texture_preview_srv_.Reset();texture_width_=0;texture_height_=0;
}

void PreviewRenderer::set_collision_lines(const std::vector<Vec3>& points) {
    std::vector<GpuVertex> lines; lines.reserve(points.size());
    for (const auto& p : points) lines.push_back({{p.x,p.y,p.z},{0,1,0},{0,0}});
    collision_lines_.Reset(); collision_vertex_count_=static_cast<unsigned>(lines.size());
    if(!lines.empty()) create_buffer(device_,lines,D3D11_BIND_VERTEX_BUFFER,collision_lines_);
}

bool PreviewRenderer::load_dds(const fs::path& path,ComPtr<ID3D11ShaderResourceView>& output,unsigned* output_width,unsigned* output_height,bool display_encoded) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate); if(!stream) return false; const auto file_size=stream.tellg(); if(file_size<148) return false;
    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));stream.seekg(0);stream.read(reinterpret_cast<char*>(bytes.data()),file_size);
    auto u32=[&](std::size_t p){std::uint32_t v{};std::memcpy(&v,bytes.data()+p,4);return v;};
    if(std::memcmp(bytes.data(),"DDS ",4)!=0||std::memcmp(bytes.data()+84,"DX10",4)!=0) return false;
    const UINT height=u32(12),width=u32(16),mips=std::max(1u,u32(28)); const auto format=static_cast<DXGI_FORMAT>(u32(128));
    UINT block_bytes{};
    switch(format){
    case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:case DXGI_FORMAT_BC4_UNORM:case DXGI_FORMAT_BC4_SNORM:block_bytes=8;break;
    case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC2_UNORM_SRGB:case DXGI_FORMAT_BC3_UNORM:case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_UNORM:case DXGI_FORMAT_BC5_SNORM:case DXGI_FORMAT_BC7_UNORM:case DXGI_FORMAT_BC7_UNORM_SRGB:block_bytes=16;break;
    default:return false;
    }
    DXGI_FORMAT resource_format=format,view_format=format;
    if(format==DXGI_FORMAT_BC1_UNORM_SRGB){resource_format=DXGI_FORMAT_BC1_TYPELESS;if(display_encoded)view_format=DXGI_FORMAT_BC1_UNORM;}
    else if(format==DXGI_FORMAT_BC2_UNORM_SRGB){resource_format=DXGI_FORMAT_BC2_TYPELESS;if(display_encoded)view_format=DXGI_FORMAT_BC2_UNORM;}
    else if(format==DXGI_FORMAT_BC3_UNORM_SRGB){resource_format=DXGI_FORMAT_BC3_TYPELESS;if(display_encoded)view_format=DXGI_FORMAT_BC3_UNORM;}
    else if(format==DXGI_FORMAT_BC7_UNORM_SRGB){resource_format=DXGI_FORMAT_BC7_TYPELESS;if(display_encoded)view_format=DXGI_FORMAT_BC7_UNORM;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=mips;desc.ArraySize=1;desc.Format=resource_format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> levels;levels.reserve(mips);std::size_t offset=148;UINT w=width,h=height;
    for(UINT i=0;i<mips;++i){const UINT row=std::max(1u,(w+3)/4)*block_bytes;const UINT size=row*std::max(1u,(h+3)/4);if(offset+size>bytes.size())return false;levels.push_back({bytes.data()+offset,row,size});offset+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);}
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=view_format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=mips;
    ComPtr<ID3D11Texture2D> texture;if(FAILED(device_->CreateTexture2D(&desc,levels.data(),&texture))||FAILED(device_->CreateShaderResourceView(texture.Get(),&view,&output)))return false;
    if(output_width)*output_width=width;if(output_height)*output_height=height;return true;
}

void PreviewRenderer::frame(OrbitCamera& camera) const { camera.target={(bounds_min_.x+bounds_max_.x)*.5f,(bounds_min_.y+bounds_max_.y)*.5f,(bounds_min_.z+bounds_max_.z)*.5f};const float dx=bounds_max_.x-bounds_min_.x,dy=bounds_max_.y-bounds_min_.y,dz=bounds_max_.z-bounds_min_.z;camera.distance=std::max(0.2f,std::sqrt(dx*dx+dy*dy+dz*dz)*0.85f); }

bool PreviewRenderer::project(Vec3 world,const OrbitCamera& camera,Vec2& screen) const {
    const float cp=std::cos(camera.pitch),sp=std::sin(camera.pitch),cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);const XMVECTOR target=XMVectorSet(camera.target.x,camera.target.y,camera.target.z,1);const XMVECTOR eye=target+XMVectorSet(camera.distance*cp*sy,camera.distance*sp,camera.distance*cp*cy,0);const auto view=XMMatrixLookAtLH(eye,target,XMVectorSet(0,1,0,0));const auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,static_cast<float>(width_)/height_,std::max(.001f,camera.distance*.001f),std::max(100.f,camera.distance*20));const auto point=XMVector3Project(XMVectorSet(world.x,world.y,world.z,1),0,0,static_cast<float>(width_),static_cast<float>(height_),0,1,projection,view,XMMatrixIdentity());XMFLOAT3 p{};XMStoreFloat3(&p,point);screen={p.x,p.y};return p.z>=0&&p.z<=1;
}

void PreviewRenderer::render(const OrbitCamera& camera,bool show_mesh,PreviewShadingMode shading,bool show_skeleton) {
    const float clear[4]={0.12f,0.13f,0.145f,1};context_->OMSetRenderTargets(1,color_rtv_.GetAddressOf(),depth_dsv_.Get());context_->ClearRenderTargetView(color_rtv_.Get(),clear);context_->ClearDepthStencilView(depth_dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};context_->RSSetViewports(1,&viewport);
    const float cp=std::cos(camera.pitch),sp=std::sin(camera.pitch),cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);XMVECTOR target=XMVectorSet(camera.target.x,camera.target.y,camera.target.z,1);XMVECTOR eye=target+XMVectorSet(camera.distance*cp*sy,camera.distance*sp,camera.distance*cp*cy,0);
    const auto vp=XMMatrixLookAtLH(eye,target,XMVectorSet(0,1,0,0))*XMMatrixPerspectiveFovLH(XM_PIDIV4,static_cast<float>(width_)/height_,std::max(.001f,camera.distance*.001f),std::max(100.f,camera.distance*20));
    Constants constants{};XMStoreFloat4x4(&constants.view_projection,XMMatrixTranspose(vp));constants.color={.72f,.76f,.82f,1};constants.light={-.28f,.82f,.48f,0};constants.lighting=shading==PreviewShadingMode::lit?1u:0u;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    auto upload_constants=[&](){if(SUCCEEDED(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(constants_.Get(),0);}};
    UINT stride=sizeof(GpuVertex),offset=0;context_->IASetInputLayout(input_layout_.Get());context_->VSSetShader(vertex_shader_.Get(),nullptr,0);context_->PSSetShader(pixel_shader_.Get(),nullptr,0);context_->VSSetConstantBuffers(0,1,constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,constants_.GetAddressOf());context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
    if(show_mesh&&index_count_){
        context_->OMSetDepthStencilState(nullptr,0);
        const bool wireframe=shading==PreviewShadingMode::wireframe;context_->RSSetState(wireframe?wire_.Get():solid_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->IASetVertexBuffers(0,1,vertices_.GetAddressOf(),&stride,&offset);context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R32_UINT,0);
        for(const auto& draw:draw_ranges_){
            GpuMaterialTextures* material=!wireframe&&draw.material<materials_.size()?&materials_[draw.material]:nullptr;
            ID3D11ShaderResourceView* textures[3]={material?material->primary.Get():nullptr,material?material->iris.Get():nullptr,material?material->highlight.Get():nullptr};
            constants.textured=textures[0]?1u:0u;constants.eye_material=material&&material->eye?1u:0u;upload_constants();context_->PSSetShaderResources(0,3,textures);context_->DrawIndexed(draw.index_count,draw.first_index,0);
        }
    }
    context_->OMSetDepthStencilState(overlay_depth_.Get(),0);
    constants.lighting=0;constants.eye_material=0;
    if(show_skeleton&&line_vertex_count_){constants.color={1,.55f,.08f,1};constants.textured=0;upload_constants();context_->RSSetState(solid_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,lines_.GetAddressOf(),&stride,&offset);context_->Draw(line_vertex_count_,0);}
    if(show_skeleton&&bone_point_vertex_count_){constants.color={1,.88f,.24f,1};constants.textured=0;upload_constants();context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,bone_points_.GetAddressOf(),&stride,&offset);context_->Draw(bone_point_vertex_count_,0);}
    if(collision_vertex_count_){constants.color={.05f,.9f,.85f,1};constants.textured=0;upload_constants();context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,collision_lines_.GetAddressOf(),&stride,&offset);context_->Draw(collision_vertex_count_,0);}
    context_->OMSetDepthStencilState(nullptr,0);
    ID3D11ShaderResourceView* none[3]={};context_->PSSetShaderResources(0,3,none);
}
}
