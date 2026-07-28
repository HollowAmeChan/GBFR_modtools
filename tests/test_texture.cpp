#include <gbfr/core/workspace.hpp>
#include <gbfr/core/log.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace {
void write_u32(std::vector<unsigned char>& bytes,std::size_t offset,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)bytes[offset+shift/8]=static_cast<unsigned char>((value>>shift)&0xff);
}

std::uint32_t read_u32(const std::vector<unsigned char>& bytes,std::size_t offset) {
    if(offset+4>bytes.size())return 0;
    return static_cast<std::uint32_t>(bytes[offset])|
        (static_cast<std::uint32_t>(bytes[offset+1])<<8)|
        (static_cast<std::uint32_t>(bytes[offset+2])<<16)|
        (static_cast<std::uint32_t>(bytes[offset+3])<<24);
}

std::vector<unsigned char> read_file(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),{}};
}

std::size_t dds_data_offset(const std::vector<unsigned char>& bytes) {
    return bytes.size()>=148&&bytes[84]=='D'&&bytes[85]=='X'&&bytes[86]=='1'&&bytes[87]=='0'?148:128;
}

bool rgba_pixels_equal(const std::vector<unsigned char>& left,const std::vector<unsigned char>& right,bool flip_right) {
    if(left.size()<128||right.size()<128)return false;
    const auto width=read_u32(left,16),height=read_u32(left,12);
    if(!width||!height||width!=read_u32(right,16)||height!=read_u32(right,12))return false;
    const auto left_offset=dds_data_offset(left),right_offset=dds_data_offset(right);
    if(left_offset+static_cast<std::size_t>(width)*height*4>left.size()||right_offset+static_cast<std::size_t>(width)*height*4>right.size())return false;
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x)for(std::uint32_t channel=0;channel<4;++channel){
        const auto left_index=left_offset+(static_cast<std::size_t>(y)*width+x)*4+channel;
        const auto right_y=flip_right?height-1-y:y;
        const auto right_index=right_offset+(static_cast<std::size_t>(right_y)*width+x)*4+channel;
        if(left[left_index]!=right[right_index])return false;
    }
    return true;
}

std::vector<unsigned char> first_wtb_payload(const fs::path& path) {
    const auto bytes=read_file(path);
    if(bytes.size()<32||bytes[0]!='W'||bytes[1]!='T'||bytes[2]!='B'||bytes[3]!=0)return {};
    const auto offsets=read_u32(bytes,12),sizes=read_u32(bytes,16);
    const auto offset=read_u32(bytes,offsets),size=read_u32(bytes,sizes);
    if(size<128||static_cast<std::size_t>(offset)+size>bytes.size())return {};
    return {bytes.begin()+offset,bytes.begin()+offset+size};
}

bool verify_existing_wtb_orientation(const fs::path& canonical_dds,const fs::path& root) {
    fs::create_directories(root/L"source");fs::create_directories(root/L"unpack");
    const auto canonical=read_file(canonical_dds);
    auto raw=canonical;
    const auto width=read_u32(raw,16),height=read_u32(raw,12);
    const auto offset=dds_data_offset(raw);
    for(std::uint32_t y=0;y<height/2;++y)for(std::uint32_t x=0;x<width*4;++x)
        std::swap(raw[offset+static_cast<std::size_t>(y)*width*4+x],raw[offset+static_cast<std::size_t>(height-1-y)*width*4+x]);

    std::vector<unsigned char> wtb(0x1000,0);wtb[0]='W';wtb[1]='T';wtb[2]='B';
    write_u32(wtb,4,3);write_u32(wtb,8,1);write_u32(wtb,12,0x20);write_u32(wtb,16,0x40);write_u32(wtb,20,0x60);write_u32(wtb,24,0x80);
    write_u32(wtb,0x20,0x1000);write_u32(wtb,0x40,static_cast<std::uint32_t>(raw.size()));wtb.insert(wtb.end(),raw.begin(),raw.end());
    const auto source=root/L"source/test.texture",input=root/L"unpack/test_0.dds";
    {std::ofstream output(source,std::ios::binary);output.write(reinterpret_cast<const char*>(wtb.data()),static_cast<std::streamsize>(wtb.size()));}
    fs::copy_file(canonical_dds,input,fs::copy_options::overwrite_existing);
    const auto source_hash=gbfr::sha256_file(source),input_hash=gbfr::sha256_file(input);
    {std::ofstream manifest(root/L"workspace.json");manifest<<"{\"Version\":1,\"CharacterId\":\"wtb_orientation\",\"Textures\":[{"
        "\"Source\":\"source/test.texture\",\"SourceSha256\":\""<<source_hash<<"\",\"Output\":\"build/test.texture\","
        "\"Slots\":[{\"Index\":0,\"Path\":\"unpack/test_0.dds\",\"BaselineSha256\":\""<<input_hash<<"\"}]}]}";}

    auto workspace=gbfr::Workspace::load(root/L"workspace.json");
    if(workspace.assets().size()!=1||!workspace.assets()[0].wtb_top_left_editing)return false;
    workspace.build_asset(0);
    if(gbfr::sha256_file(root/L"build/test.texture")==source_hash)return false;
    if(!rgba_pixels_equal(first_wtb_payload(root/L"build/test.texture"),canonical,true))return false;
    auto edited=canonical;edited[dds_data_offset(edited)]=static_cast<unsigned char>(edited[dds_data_offset(edited)]+1);
    {std::ofstream output(input,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(edited.data()),static_cast<std::streamsize>(edited.size()));}
    workspace.build_asset(0);
    if(!rgba_pixels_equal(first_wtb_payload(root/L"build/test.texture"),edited,true))return false;
    {std::ofstream damaged(input,std::ios::binary|std::ios::trunc);damaged<<"damaged";}
    workspace.restore_asset(0);
    return rgba_pixels_equal(read_file(input),canonical,false);
}

bool build_and_reload(const fs::path& source,const fs::path& root,gbfr::PreviewRenderer& preview) {
    const auto unpack=root/L"unpack/data/texture/2k";
    fs::create_directories(unpack);
    const auto input=unpack/source.filename();
    fs::copy_file(source,input,fs::copy_options::overwrite_existing);
    {std::ofstream manifest(root/L"workspace.json");manifest<<"{\"Version\":1,\"CharacterId\":\"texture_test\"}";}

    auto workspace=gbfr::Workspace::load(root/L"workspace.json");
    const auto asset=std::find_if(workspace.assets().begin(),workspace.assets().end(),[&](const auto& candidate){
        return candidate.kind==gbfr::AssetKind::new_texture&&candidate.input==fs::weakly_canonical(input);
    });
    if(asset==workspace.assets().end())return false;
    const auto output=asset->output;
    workspace.build_asset(static_cast<std::size_t>(std::distance(workspace.assets().begin(),asset)));

    std::ifstream stream(output,std::ios::binary);
    const std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(stream),{});
    if(bytes.size()<32||bytes[0]!='W'||bytes[1]!='T'||bytes[2]!='B'||bytes[3]!=0)return false;
    const auto count=read_u32(bytes,4),offsets=read_u32(bytes,12),sizes=read_u32(bytes,16);
    if(!count||offsets+4>bytes.size()||sizes+4>bytes.size())return false;
    const auto offset=read_u32(bytes,offsets),size=read_u32(bytes,sizes);
    if(size<128||static_cast<std::size_t>(offset)+size>bytes.size())return false;
    const auto extracted=root/L"packed_payload.dds";
    {std::ofstream payload(extracted,std::ios::binary);payload.write(reinterpret_cast<const char*>(bytes.data()+offset),size);}
    return preview.load_texture_preview(extracted)&&preview.texture_image();
}

bool build_workspace_textures(const fs::path& manifest) {
    auto workspace=gbfr::Workspace::load(manifest);
    std::size_t built=0;
    for(std::size_t index=0;index<workspace.assets().size();++index){
        const auto& asset=workspace.assets()[index];
        if(asset.kind!=gbfr::AssetKind::texture)continue;
        workspace.build_asset(index);
        ++built;
    }
    std::cout<<"built_workspace_textures="<<built<<'\n';
    return built>0;
}

std::array<unsigned char,4> preview_pixel(gbfr::PreviewRenderer& preview,ID3D11Device* device,ID3D11DeviceContext* context) {
    std::array<unsigned char,4> result{};
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;preview.texture_image()->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return result;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))return result;
    context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return result;
    std::copy_n(static_cast<const unsigned char*>(mapped.pData),4,result.begin());context->Unmap(staging.Get(),0);
    return result;
}
}

int main(int argc,char** argv) {
    const auto root=fs::temp_directory_path()/L"gbfr_texture_test";
    fs::remove_all(root);fs::create_directories(root);
    std::vector<unsigned char> bytes(128+4*4*4,0);
    bytes[0]='D';bytes[1]='D';bytes[2]='S';bytes[3]=' ';
    write_u32(bytes,4,124);write_u32(bytes,8,0x0000100f);write_u32(bytes,12,4);write_u32(bytes,16,4);write_u32(bytes,20,16);write_u32(bytes,28,1);
    write_u32(bytes,76,32);write_u32(bytes,80,0x41);write_u32(bytes,88,32);write_u32(bytes,92,0x000000ff);write_u32(bytes,96,0x0000ff00);write_u32(bytes,100,0x00ff0000);write_u32(bytes,104,0xff000000);write_u32(bytes,108,0x1000);
    for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x){const auto offset=128+(y*4+x)*4;bytes[offset]=static_cast<unsigned char>(32+y*48);bytes[offset+1]=static_cast<unsigned char>(16+x*32);bytes[offset+2]=8;bytes[offset+3]=64;}
    const auto path=root/L"manual_rgba.dds";
    {std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}

    Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
    gbfr::PreviewRenderer preview;
    const auto executable=fs::absolute(fs::path(argv[0]));
    if(!preview.initialize(device.Get(),context.Get(),executable.parent_path()/L"preview.hlsl"))return 2;
    if(!preview.load_texture_preview(path)||!preview.texture_image()||preview.texture_width()!=4||preview.texture_height()!=4||
       preview.texture_info().mip_count!=1||preview.texture_info().format!="R8G8B8A8_UNORM"||preview.texture_info().compression!="未压缩 RGBA8")return 3;
    const std::array channel_checks={
        std::pair{gbfr::TexturePreviewChannel::red,static_cast<unsigned char>(32)},
        std::pair{gbfr::TexturePreviewChannel::green,static_cast<unsigned char>(16)},
        std::pair{gbfr::TexturePreviewChannel::blue,static_cast<unsigned char>(8)},
        std::pair{gbfr::TexturePreviewChannel::alpha,static_cast<unsigned char>(64)}};
    for(const auto [channel,expected]:channel_checks){
        if(!preview.set_texture_preview_channel(channel))return 13;
        const auto pixel=preview_pixel(preview,device.Get(),context.Get());
        if(pixel[0]!=expected||pixel[1]!=expected||pixel[2]!=expected||pixel[3]!=255)return 14;
    }
    if(!preview.set_texture_preview_channel(gbfr::TexturePreviewChannel::normal)||preview.texture_preview_channel()!=gbfr::TexturePreviewChannel::normal)return 15;
    if(!build_and_reload(path,root/L"synthetic_workspace",preview)||preview.texture_width()!=4||preview.texture_height()!=4)return 4;
    if(!verify_existing_wtb_orientation(path,root/L"wtb_orientation_workspace"))return 7;
    if(argc>1){
        const auto source=fs::absolute(fs::path(argv[1]));
        if(!preview.load_texture_preview(source))return 5;
        const auto width=preview.texture_width(),height=preview.texture_height();
        for(const auto [channel,expected]:channel_checks){
            (void)expected;
            if(!preview.set_texture_preview_channel(channel)||!preview.texture_image())return 16;
        }
        if(!preview.set_texture_preview_channel(gbfr::TexturePreviewChannel::normal))return 17;
        if(!build_and_reload(source,root/L"actual_workspace",preview)||preview.texture_width()!=width||preview.texture_height()!=height)return 6;
    }
    if(argc>2&&!build_workspace_textures(fs::absolute(fs::path(argv[2]))))return 8;
    {
        gbfr::PreviewRenderer model_preview;
        if(!model_preview.initialize(device.Get(),context.Get(),executable.parent_path()/L"preview.hlsl"))return 9;
        const auto damaged=root/L"damaged_material.dds";
        {std::ofstream output(damaged,std::ios::binary);output<<"not a DDS";}
        gbfr::SkeletonAsset skeleton;skeleton.bones.push_back({"_000",0xffff,{0,0,0},{0,0,0,1},{1,1,1},{0,0,0}});
        gbfr::MeshAsset mesh;mesh.vertices.resize(3);mesh.indices={0,1,2};
        mesh.vertices[0].position={0,0,0};mesh.vertices[1].position={1,0,0};mesh.vertices[2].position={0,1,0};
        for(auto& vertex:mesh.vertices){vertex.joints[0]=0;vertex.weights[0]=1.0f;}
        gbfr::PreviewMaterialTextures material;material.albedo=damaged;
        gbfr::Log::clear();
        if(!model_preview.load(mesh,skeleton,{material})||!model_preview.has_model()||!model_preview.last_error().empty()){
            std::cerr<<"material fallback failed: "<<model_preview.last_error()<<'\n';
            return 10;
        }
        const auto log=gbfr::Log::snapshot();
        if(log.empty()||log.back().level!=gbfr::LogLevel::warning||log.back().message.find("damaged_material.dds")==std::string::npos)return 11;
        if(model_preview.load(mesh,{})||model_preview.last_error().empty())return 12;
    }
    fs::remove_all(root);
    return 0;
}
