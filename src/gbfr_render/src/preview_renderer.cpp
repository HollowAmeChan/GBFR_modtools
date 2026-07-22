#include <gbfr/render/preview_renderer.hpp>
#include "preview_gpu_types.hpp"
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
using gbfr::render_detail::BoneConstants;
using gbfr::render_detail::GpuVertex;
using gbfr::render_detail::SceneConstants;
namespace {
bool compile(const fs::path& file,const char* entry,const char* target,ComPtr<ID3DBlob>& output) {
    ComPtr<ID3DBlob> errors;
    const auto result=D3DCompileFromFile(file.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&output,&errors);
    if(FAILED(result)&&errors)OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    return SUCCEEDED(result);
}

template<class T> bool create_buffer(ID3D11Device* device,const std::vector<T>& data,UINT bind,ComPtr<ID3D11Buffer>& output) {
    if(data.empty()) return false;
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=static_cast<UINT>(data.size()*sizeof(T)); desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA initial{data.data()}; return SUCCEEDED(device->CreateBuffer(&desc,&initial,&output));
}
}

namespace gbfr {
bool PreviewRenderer::initialize(ID3D11Device* device,ID3D11DeviceContext* context,const fs::path& shader_file) {
    device_=device; context_=context;
    ComPtr<ID3DBlob> vs,ps;
    if(!compile(shader_file,"VSMain","vs_5_0",vs)||!compile(shader_file,"PSMain","ps_5_0",ps)) return false;
    if(FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_shader_))||
       FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_shader_))) return false;
    D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R16G16B16A16_UINT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",1,DXGI_FORMAT_R16G16B16A16_UINT,0,64,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,72,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,88,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(device_->CreateInputLayout(elements,9,vs->GetBufferPointer(),vs->GetBufferSize(),&input_layout_))) return false;
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(SceneConstants); cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device_->CreateBuffer(&cb,nullptr,&constants_))) return false;
    cb.ByteWidth=sizeof(BoneConstants);
    if(FAILED(device_->CreateBuffer(&cb,nullptr,&bones_)))return false;
    D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD=D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd,&sampler_);
    D3D11_RASTERIZER_DESC rd{}; rd.CullMode=D3D11_CULL_BACK; rd.FillMode=D3D11_FILL_SOLID; rd.DepthClipEnable=TRUE;
    if(FAILED(device_->CreateRasterizerState(&rd,&solid_)))return false;
    rd.CullMode=D3D11_CULL_NONE;rd.FillMode=D3D11_FILL_WIREFRAME;if(FAILED(device_->CreateRasterizerState(&rd,&wire_)))return false;
    rd.FillMode=D3D11_FILL_SOLID;rd.DepthBias=-10000;rd.SlopeScaledDepthBias=-2.0f;
    if(FAILED(device_->CreateRasterizerState(&rd,&alpha_overlay_raster_)))return false;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    if(FAILED(device_->CreateDepthStencilState(&depth,&overlay_depth_)))return false;
    depth.DepthEnable=TRUE;depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(device_->CreateDepthStencilState(&depth,&alpha_depth_)))return false;
    D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device_->CreateBlendState(&blend,&alpha_blend_)))return false;
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

bool PreviewRenderer::load(const MeshAsset& mesh,const SkeletonAsset& skeleton,const std::vector<PreviewMaterialTextures>& materials,const SopAsset& sop) {
    clear();
    if(skeleton.bones.empty()||skeleton.bones.size()>max_skin_bones)return false;
    skeleton_=skeleton;sop_=sop;
    visible_bones_.assign(skeleton.bones.size(),false);
    for(const auto& vertex:mesh.vertices)for(std::size_t influence=0;influence<vertex.weights.size();++influence){
        if(vertex.weights[influence]<=0.0f||vertex.joints[influence]>=skeleton.bones.size())continue;
        auto index=static_cast<std::size_t>(vertex.joints[influence]);
        while(index<skeleton.bones.size()&&!visible_bones_[index]){
            visible_bones_[index]=true;
            const auto parent=skeleton.bones[index].parent;
            if(parent==0xffff||parent>=skeleton.bones.size())break;
            index=parent;
        }
    }
    visible_bone_count_=static_cast<std::size_t>(std::count(visible_bones_.begin(),visible_bones_.end(),true));
    std::vector<GpuVertex> vertices; vertices.reserve(mesh.vertices.size());
    bounds_min_={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()}; bounds_max_={-bounds_min_.x,-bounds_min_.y,-bounds_min_.z};
    for(const auto& v:mesh.vertices) {
        GpuVertex vertex;
        vertex.position[0]=v.position.x;vertex.position[1]=v.position.y;vertex.position[2]=v.position.z;
        vertex.normal[0]=v.normal.x;vertex.normal[1]=v.normal.y;vertex.normal[2]=v.normal.z;
        vertex.uv[0]=v.uv.x;vertex.uv[1]=v.uv.y;vertex.uv1[0]=v.uv1.x;vertex.uv1[1]=v.uv1.y;
        vertex.color[0]=v.color.x;vertex.color[1]=v.color.y;vertex.color[2]=v.color.z;vertex.color[3]=v.color.w;
        std::copy(v.joints.begin(),v.joints.end(),vertex.joints);std::copy(v.weights.begin(),v.weights.end(),vertex.weights);
        vertices.push_back(vertex);
        bounds_min_.x=std::min(bounds_min_.x,v.position.x);bounds_min_.y=std::min(bounds_min_.y,v.position.y);bounds_min_.z=std::min(bounds_min_.z,v.position.z);bounds_max_.x=std::max(bounds_max_.x,v.position.x);bounds_max_.y=std::max(bounds_max_.y,v.position.y);bounds_max_.z=std::max(bounds_max_.z,v.position.z);
    }
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
        gpu.alpha_clipped=source.alpha_clipped;
        gpu.alpha_blended=source.alpha_blended;
        if(!source.albedo.empty()&&!load_dds(source.albedo,gpu.primary)) return false;
        if(!source.alpha_mask.empty()){gpu.alpha_masked=load_dds(source.alpha_mask,gpu.alpha_mask);if(!gpu.alpha_masked)return false;}
        if(!source.eye_conjunctiva.empty()&&!source.eye_iris.empty()&&!source.eye_highlight.empty()) {
            gpu.eye=load_dds(source.eye_conjunctiva,gpu.primary)&&load_dds(source.eye_iris,gpu.iris)&&load_dds(source.eye_highlight,gpu.highlight);
            if(!gpu.eye) return false;
        }
    }

    std::vector<GpuVertex> lines;
    for(std::size_t i=0;i<skeleton.bones.size();++i) { const auto& b=skeleton.bones[i]; if(!visible_bones_[i]||b.parent==0xffff||b.parent>=skeleton.bones.size()) continue; const auto& p=skeleton.bones[b.parent].world_position; lines.push_back({{p.x,p.y,p.z},{0,1,0},{0,0}});lines.push_back({{b.world_position.x,b.world_position.y,b.world_position.z},{0,1,0},{0,0}}); }
    line_vertex_count_=static_cast<unsigned>(lines.size()); if(!lines.empty()) create_buffer(device_,lines,D3D11_BIND_VERTEX_BUFFER,lines_);
    const float dx=bounds_max_.x-bounds_min_.x,dy=bounds_max_.y-bounds_min_.y,dz=bounds_max_.z-bounds_min_.z;
    bone_marker_size_=std::max(.001f,std::sqrt(dx*dx+dy*dy+dz*dz)*.004f);std::vector<GpuVertex> points;points.reserve(skeleton.bones.size()*6);
    for(std::size_t i=0;i<skeleton.bones.size();++i){if(!visible_bones_[i])continue;const auto p=skeleton.bones[i].world_position;points.push_back({{p.x-bone_marker_size_,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x+bone_marker_size_,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y-bone_marker_size_,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y+bone_marker_size_,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z-bone_marker_size_},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z+bone_marker_size_},{0,1,0},{0,0}});}
    bone_point_vertex_count_=static_cast<unsigned>(points.size());if(!points.empty())create_buffer(device_,points,D3D11_BIND_VERTEX_BUFFER,bone_points_);
    return apply_animation(nullptr,0.0f);
}

bool PreviewRenderer::load_texture_preview(const fs::path& dds) {
    ComPtr<ID3D11ShaderResourceView> texture;
    unsigned width{},height{};
    if(!load_dds(dds,texture,&width,&height,true)) return false;
    texture_preview_srv_=std::move(texture);texture_width_=width;texture_height_=height;
    return true;
}

bool PreviewRenderer::load_texture_thumbnail(const fs::path& dds,TexturePreviewResource& output,unsigned maximum_dimension) {
    TexturePreviewResource loaded;
    if(!load_dds(dds,loaded.image,&loaded.width,&loaded.height,true,std::max(1u,maximum_dimension)))return false;
    output=std::move(loaded);
    return true;
}

void PreviewRenderer::clear() {
    index_count_=0; line_vertex_count_=0; bone_point_vertex_count_=0; collision_vertex_count_=0;
    cloth_longitudinal_vertex_count_=0;cloth_lateral_vertex_count_=0;cloth_polygon_vertex_count_=0;
    vertices_.Reset(); indices_.Reset(); lines_.Reset(); bone_points_.Reset(); collision_lines_.Reset();
    cloth_longitudinal_lines_.Reset();cloth_lateral_lines_.Reset();cloth_polygon_lines_.Reset();
    draw_ranges_.clear(); materials_.clear();
    skeleton_={};sop_={};animated_bone_positions_.clear();animated_bone_world_.clear();visible_bones_.clear();visible_bone_count_=0;applied_sop_operation_count_=0;pose_hash_=0;
    texture_preview_srv_.Reset();texture_width_=0;texture_height_=0;
}

void PreviewRenderer::set_collision_lines(const std::vector<Vec3>& points) {
    std::vector<GpuVertex> lines; lines.reserve(points.size());
    for (const auto& p : points) lines.push_back({{p.x,p.y,p.z},{0,1,0},{0,0}});
    collision_lines_.Reset(); collision_vertex_count_=static_cast<unsigned>(lines.size());
    if(!lines.empty()) create_buffer(device_,lines,D3D11_BIND_VERTEX_BUFFER,collision_lines_);
}

void PreviewRenderer::set_cloth_lines(const std::vector<Vec3>& longitudinal,const std::vector<Vec3>& lateral,const std::vector<Vec3>& polygon) {
    const auto upload=[&](const std::vector<Vec3>& points,ComPtr<ID3D11Buffer>& buffer,unsigned& count){
        std::vector<GpuVertex> lines;lines.reserve(points.size());
        for(const auto& point:points)lines.push_back({{point.x,point.y,point.z},{0,1,0},{0,0}});
        buffer.Reset();count=static_cast<unsigned>(lines.size());
        if(!lines.empty())create_buffer(device_,lines,D3D11_BIND_VERTEX_BUFFER,buffer);
    };
    upload(longitudinal,cloth_longitudinal_lines_,cloth_longitudinal_vertex_count_);
    upload(lateral,cloth_lateral_lines_,cloth_lateral_vertex_count_);
    upload(polygon,cloth_polygon_lines_,cloth_polygon_vertex_count_);
}

bool PreviewRenderer::transform_bone_point(std::size_t bone_index,Vec3 local,Vec3& output) const {
    if(bone_index>=animated_bone_world_.size())return false;
    const auto& m=animated_bone_world_[bone_index];
    output={local.x*m[0]+local.y*m[4]+local.z*m[8]+m[12],
            local.x*m[1]+local.y*m[5]+local.z*m[9]+m[13],
            local.x*m[2]+local.y*m[6]+local.z*m[10]+m[14]};
    return true;
}

bool PreviewRenderer::load_dds(const fs::path& path,ComPtr<ID3D11ShaderResourceView>& output,unsigned* output_width,unsigned* output_height,bool display_encoded,unsigned maximum_dimension) {
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
    std::size_t offset=148;UINT selected_width=width,selected_height=height,first_mip=0;
    while(maximum_dimension&&first_mip+1<mips&&std::max(selected_width,selected_height)>maximum_dimension){const UINT row=std::max(1u,(selected_width+3)/4)*block_bytes;offset+=row*std::max(1u,(selected_height+3)/4);selected_width=std::max(1u,selected_width/2);selected_height=std::max(1u,selected_height/2);++first_mip;}
    const UINT selected_mips=maximum_dimension?1u:mips-first_mip;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=selected_width;desc.Height=selected_height;desc.MipLevels=selected_mips;desc.ArraySize=1;desc.Format=resource_format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> levels;levels.reserve(selected_mips);UINT w=selected_width,h=selected_height;
    for(UINT i=0;i<selected_mips;++i){const UINT row=std::max(1u,(w+3)/4)*block_bytes;const UINT size=row*std::max(1u,(h+3)/4);if(offset+size>bytes.size())return false;levels.push_back({bytes.data()+offset,row,size});offset+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);}
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=view_format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=selected_mips;
    ComPtr<ID3D11Texture2D> texture;if(FAILED(device_->CreateTexture2D(&desc,levels.data(),&texture))||FAILED(device_->CreateShaderResourceView(texture.Get(),&view,&output)))return false;
    if(output_width)*output_width=selected_width;if(output_height)*output_height=selected_height;return true;
}

void PreviewRenderer::frame(OrbitCamera& camera) const { camera.target={(bounds_min_.x+bounds_max_.x)*.5f,(bounds_min_.y+bounds_max_.y)*.5f,(bounds_min_.z+bounds_max_.z)*.5f};const float dx=bounds_max_.x-bounds_min_.x,dy=bounds_max_.y-bounds_min_.y,dz=bounds_max_.z-bounds_min_.z;camera.distance=std::max(0.2f,std::sqrt(dx*dx+dy*dy+dz*dz)*0.85f); }

bool PreviewRenderer::project(Vec3 world,const OrbitCamera& camera,Vec2& screen) const {
    const float cp=std::cos(camera.pitch),sp=std::sin(camera.pitch),cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);const XMVECTOR target=XMVectorSet(camera.target.x,camera.target.y,camera.target.z,1);const XMVECTOR eye=target+XMVectorSet(camera.distance*cp*sy,camera.distance*sp,camera.distance*cp*cy,0);const auto view=XMMatrixLookAtLH(eye,target,XMVectorSet(0,1,0,0));const auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,static_cast<float>(width_)/height_,std::max(.001f,camera.distance*.001f),std::max(100.f,camera.distance*20));const auto point=XMVector3Project(XMVectorSet(world.x,world.y,world.z,1),0,0,static_cast<float>(width_),static_cast<float>(height_),0,1,projection,view,XMMatrixIdentity());XMFLOAT3 p{};XMStoreFloat3(&p,point);screen={p.x,p.y};return p.z>=0&&p.z<=1;
}

void PreviewRenderer::render(const OrbitCamera& camera,bool show_mesh,PreviewShadingMode shading,bool show_skeleton,bool show_collisions,bool show_alpha_overlays,bool show_cloth_links) {
    const float clear[4]={0.12f,0.13f,0.145f,1};context_->OMSetRenderTargets(1,color_rtv_.GetAddressOf(),depth_dsv_.Get());context_->ClearRenderTargetView(color_rtv_.Get(),clear);context_->ClearDepthStencilView(depth_dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};context_->RSSetViewports(1,&viewport);
    const float cp=std::cos(camera.pitch),sp=std::sin(camera.pitch),cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);XMVECTOR target=XMVectorSet(camera.target.x,camera.target.y,camera.target.z,1);XMVECTOR eye=target+XMVectorSet(camera.distance*cp*sy,camera.distance*sp,camera.distance*cp*cy,0);
    const auto vp=XMMatrixLookAtLH(eye,target,XMVectorSet(0,1,0,0))*XMMatrixPerspectiveFovLH(XM_PIDIV4,static_cast<float>(width_)/height_,std::max(.001f,camera.distance*.001f),std::max(100.f,camera.distance*20));
    SceneConstants constants{};XMStoreFloat4x4(&constants.view_projection,XMMatrixTranspose(vp));constants.color={.72f,.76f,.82f,1};constants.light={-.28f,.82f,.48f,0};constants.lighting=shading==PreviewShadingMode::lit?1u:0u;constants.alpha_threshold=.2f;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    auto upload_constants=[&](){if(SUCCEEDED(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(constants_.Get(),0);}};
    UINT stride=sizeof(GpuVertex),offset=0;context_->IASetInputLayout(input_layout_.Get());context_->VSSetShader(vertex_shader_.Get(),nullptr,0);context_->PSSetShader(pixel_shader_.Get(),nullptr,0);context_->VSSetConstantBuffers(0,1,constants_.GetAddressOf());context_->VSSetConstantBuffers(1,1,bones_.GetAddressOf());context_->PSSetConstantBuffers(0,1,constants_.GetAddressOf());context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
    if(show_mesh&&index_count_){
        constants.skinning_enabled=1;
        const bool wireframe=shading==PreviewShadingMode::wireframe;context_->RSSetState(wireframe?wire_.Get():solid_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->IASetVertexBuffers(0,1,vertices_.GetAddressOf(),&stride,&offset);context_->IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R32_UINT,0);
        auto draw_pass=[&](int pass){
            for(const auto& draw:draw_ranges_){
                GpuMaterialTextures* material=!wireframe&&draw.material<materials_.size()?&materials_[draw.material]:nullptr;
                const bool alpha=material&&material->alpha_blended;
                const bool masked=material&&material->alpha_masked;
                if(pass==0&&alpha)continue;
                if(pass==1&&(!alpha||masked))continue;
                if(pass==2&&(!alpha||!masked))continue;
                ID3D11ShaderResourceView* textures[4]={material?material->primary.Get():nullptr,material?material->iris.Get():nullptr,material?material->highlight.Get():nullptr,material?material->alpha_mask.Get():nullptr};
                constants.textured=textures[0]?1u:0u;constants.eye_material=material&&material->eye?1u:0u;constants.alpha_blended=alpha?1u:0u;constants.alpha_masked=material&&material->alpha_masked?1u:0u;constants.alpha_clipped=material&&material->alpha_clipped?1u:0u;upload_constants();context_->PSSetShaderResources(0,4,textures);context_->DrawIndexed(draw.index_count,draw.first_index,0);
            }
        };
        const float blend_factor[4]{};
        context_->OMSetBlendState(nullptr,blend_factor,0xffffffffu);context_->OMSetDepthStencilState(nullptr,0);
        if(wireframe)draw_pass(3);
        else{
            draw_pass(0);
            if(show_alpha_overlays){
                context_->OMSetBlendState(alpha_blend_.Get(),blend_factor,0xffffffffu);
                context_->OMSetDepthStencilState(alpha_depth_.Get(),0);context_->RSSetState(alpha_overlay_raster_.Get());draw_pass(2);
            }
            context_->RSSetState(solid_.Get());
            context_->OMSetBlendState(nullptr,blend_factor,0xffffffffu);
        }
    }
    context_->OMSetDepthStencilState(overlay_depth_.Get(),0);
    constants.lighting=0;constants.eye_material=0;constants.alpha_blended=0;constants.alpha_masked=0;constants.alpha_clipped=0;constants.skinning_enabled=0;
    if(show_skeleton&&line_vertex_count_){constants.color={1,.55f,.08f,1};constants.textured=0;upload_constants();context_->RSSetState(solid_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,lines_.GetAddressOf(),&stride,&offset);context_->Draw(line_vertex_count_,0);}
    if(show_skeleton&&bone_point_vertex_count_){constants.color={1,.88f,.24f,1};constants.textured=0;upload_constants();context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,bone_points_.GetAddressOf(),&stride,&offset);context_->Draw(bone_point_vertex_count_,0);}
    if(show_collisions&&collision_vertex_count_){constants.color={.05f,.9f,.85f,1};constants.textured=0;upload_constants();context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,collision_lines_.GetAddressOf(),&stride,&offset);context_->Draw(collision_vertex_count_,0);}
    const auto draw_cloth_lines=[&](ID3D11Buffer* buffer,unsigned count,const XMFLOAT4& color){if(!show_cloth_links||!count)return;constants.color=color;constants.textured=0;upload_constants();context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);context_->Draw(count,0);};
    draw_cloth_lines(cloth_longitudinal_lines_.Get(),cloth_longitudinal_vertex_count_,{.3f,1.f,.22f,1});
    draw_cloth_lines(cloth_lateral_lines_.Get(),cloth_lateral_vertex_count_,{1.f,.25f,.8f,1});
    draw_cloth_lines(cloth_polygon_lines_.Get(),cloth_polygon_vertex_count_,{1.f,.72f,.1f,1});
    context_->OMSetDepthStencilState(nullptr,0);
    ID3D11ShaderResourceView* none[4]={};context_->PSSetShaderResources(0,4,none);
}

std::uint64_t PreviewRenderer::render_target_hash() const {
    if(!color_||!device_||!context_)return 0;
    D3D11_TEXTURE2D_DESC desc{};color_->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging)))return 0;
    context_->OMSetRenderTargets(0,nullptr,nullptr);context_->CopyResource(staging.Get(),color_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return 0;
    std::uint64_t hash=1469598103934665603ull;
    for(UINT y=0;y<desc.Height;++y){const auto* row=static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch;for(UINT x=0;x<desc.Width*4;++x){hash^=row[x];hash*=1099511628211ull;}}
    context_->Unmap(staging.Get(),0);return hash;
}
}
