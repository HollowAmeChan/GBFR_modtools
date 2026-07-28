#include <gbfr/core/workspace.hpp>
#include <gbfr/render/preview_renderer.hpp>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::uint32_t read_u32(const std::vector<unsigned char>& bytes,std::size_t offset) {
    if(offset+4>bytes.size())return 0;
    return static_cast<std::uint32_t>(bytes[offset])|
        (static_cast<std::uint32_t>(bytes[offset+1])<<8)|
        (static_cast<std::uint32_t>(bytes[offset+2])<<16)|
        (static_cast<std::uint32_t>(bytes[offset+3])<<24);
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
}

int main(int argc,char** argv) {
    const auto root=fs::temp_directory_path()/L"gbfr_texture_test";
    fs::remove_all(root);fs::create_directories(root);
    std::vector<unsigned char> bytes(128+4*4*4,0);
    const auto put_u32=[&](std::size_t offset,std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes[offset+shift/8]=static_cast<unsigned char>((value>>shift)&0xff);};
    bytes[0]='D';bytes[1]='D';bytes[2]='S';bytes[3]=' ';
    put_u32(4,124);put_u32(8,0x0000100f);put_u32(12,4);put_u32(16,4);put_u32(20,16);put_u32(28,1);
    put_u32(76,32);put_u32(80,0x41);put_u32(88,32);put_u32(92,0x000000ff);put_u32(96,0x0000ff00);put_u32(100,0x00ff0000);put_u32(104,0xff000000);put_u32(108,0x1000);
    for(std::size_t offset=128;offset<bytes.size();offset+=4){bytes[offset]=255;bytes[offset+3]=255;}
    const auto path=root/L"manual_rgba.dds";
    {std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}

    Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
    gbfr::PreviewRenderer preview;
    const auto executable=fs::absolute(fs::path(argv[0]));
    if(!preview.initialize(device.Get(),context.Get(),executable.parent_path()/L"preview.hlsl"))return 2;
    if(!preview.load_texture_preview(path)||!preview.texture_image()||preview.texture_width()!=4||preview.texture_height()!=4||
       preview.texture_info().mip_count!=1||preview.texture_info().format!="R8G8B8A8_UNORM"||preview.texture_info().compression!="未压缩 RGBA8")return 3;
    if(!build_and_reload(path,root/L"synthetic_workspace",preview)||preview.texture_width()!=4||preview.texture_height()!=4)return 4;
    if(argc>1){
        const auto source=fs::absolute(fs::path(argv[1]));
        if(!preview.load_texture_preview(source))return 5;
        const auto width=preview.texture_width(),height=preview.texture_height();
        if(!build_and_reload(source,root/L"actual_workspace",preview)||preview.texture_width()!=width||preview.texture_height()!=height)return 6;
    }
    fs::remove_all(root);
    return 0;
}
