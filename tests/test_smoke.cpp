#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/material.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/render/preview_renderer.hpp>
#include <nlohmann/json.hpp>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <cmath>

namespace fs = std::filesystem;

int main() {
    gbfr::Log::clear();
    gbfr::Log::write(gbfr::LogLevel::info, "smoke");
    if (gbfr::Log::snapshot().size() != 1) return 1;
    if(!gbfr::is_color_variant_texture("pl1400_body_lod0_c01_albd")||
       gbfr::is_color_variant_texture("pl1400_body_lod0_albd"))return 14;

    const fs::path root = fs::temp_directory_path() / L"gbfr_workspace_test";
    fs::remove_all(root);
    fs::create_directories(root / L"source");
    fs::create_directories(root / L"unpack");
    {
        std::ofstream(root / L"source/model.mmesh", std::ios::binary) << "baseline";
        std::ofstream(root / L"unpack/model.mmesh", std::ios::binary) << "baseline";
    }
    const auto hash = gbfr::sha256_file(root / L"source/model.mmesh");
    {
        std::ofstream json(root / L"workspace.json");
        json << "{\"Version\":1,\"CharacterId\":\"test\",\"ModelFiles\":[{"
                "\"Source\":\"source/model.mmesh\",\"SourceSha256\":\"" << hash << "\","
                "\"Input\":\"unpack/model.mmesh\",\"Output\":\"build/model.mmesh\","
                "\"BaselineSha256\":\"" << hash << "\",\"FileType\":\"mmesh\"}]}";
    }
    auto workspace = gbfr::Workspace::load(root / L"workspace.json");
    if (workspace.assets().size() != 1 || workspace.changed_count() != 0) return 2;
    std::ofstream(root / L"unpack/model.mmesh", std::ios::binary | std::ios::trunc) << "edited";
    workspace.refresh();
    if (workspace.changed_count() != 1) return 3;
    workspace.build_model(0);
    if (gbfr::sha256_file(root / L"build/model.mmesh") != gbfr::sha256_file(root / L"unpack/model.mmesh")) return 4;
    workspace.restore_model(0);
    if (workspace.changed_count() != 0) return 5;
    fs::remove_all(root);

    const fs::path integration = fs::path(GBFR_SOURCE_DIR) / L"explore_output/workspace.json";
    if (fs::is_regular_file(integration)) {
        const auto pl1400 = gbfr::Workspace::load(integration);
        nlohmann::json document;
        std::ifstream(integration) >> document;
        const auto expected_assets=document.value("Textures",nlohmann::json::array()).size()+
            document.value("Materials",nlohmann::json::array()).size()+
            document.value("ClothFiles",nlohmann::json::array()).size()+
            document.value("ModelFiles",nlohmann::json::array()).size()+
            document.value("NewTextures",nlohmann::json::array()).size();
        if (pl1400.assets().size() != expected_assets) return 6;
        const auto model_root = integration.parent_path() / L"unpack/data/model/pl/pl1400";
        const auto minfo = gbfr::load_minfo(model_root / L"pl1400.minfo");
        const auto skeleton = gbfr::load_skeleton(model_root / L"pl1400.skeleton");
        const auto mesh = gbfr::load_mmesh(integration.parent_path() / L"unpack/data/model_streaming/lod0/pl1400.mmesh", minfo);
        if (mesh.vertices.size() != minfo.vertex_count || mesh.indices.size() != minfo.index_count || skeleton.bones.empty()) return 7;
        const auto materials=gbfr::load_mmat_json(model_root/L"vars/0.mmat.json");
        if(materials.entries.size()!=11||materials.entries[0].albedo_name!="pl1400_body01_lod0_albd")return 11;
        for(const auto& entry:materials.entries)if(gbfr::is_color_variant_texture(entry.albedo_name))return 12;
        for(const auto& chunk:mesh.chunks)if(chunk.material>=materials.entries.size()||chunk.offset+chunk.count>mesh.indices.size())return 13;
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 15;
        gbfr::PreviewRenderer preview;
        const auto dds=integration.parent_path()/L"unpack/data/granite/2k/pl1400_body01_lod0_albd.dds";
        if(!preview.initialize(device.Get(),context.Get())||!preview.load_texture_preview(dds)||!preview.texture_image()||!preview.texture_width()||!preview.texture_height())return 16;
        std::vector<gbfr::PreviewMaterialTextures> preview_materials(materials.entries.size());for(auto& material:preview_materials)material.albedo=dds;
        gbfr::OrbitCamera camera;
        if(!preview.load(mesh,skeleton,preview_materials))return 17;
        preview.resize(320,320);preview.frame(camera);
        for(const auto mode:{gbfr::PreviewShadingMode::unlit,gbfr::PreviewShadingMode::lit,gbfr::PreviewShadingMode::wireframe})preview.render(camera,true,mode,true);
        context->Flush();
        if(FAILED(device->GetDeviceRemovedReason()))return 18;
        const auto fp_root=integration.parent_path()/L"unpack/data/model/fp/fp1400";
        const auto fp_minfo=gbfr::load_minfo(fp_root/L"fp1400.minfo");
        const auto fp_skeleton=gbfr::load_skeleton(fp_root/L"fp1400.skeleton");
        const auto fp_mesh=gbfr::load_mmesh(integration.parent_path()/L"unpack/data/model_streaming/lod0/fp1400.mmesh",fp_minfo);
        const auto fp_materials=gbfr::load_mmat_json(fp_root/L"vars/0.mmat.json");
        if(fp_materials.entries.size()!=7||
           fp_materials.entries[0].eye_conjunctiva_name!="fp1400_l_eye_lod0_conj"||
           fp_materials.entries[0].eye_iris_name!="fp1400_l_eye_lod0_iris"||
           fp_materials.entries[0].eye_highlight_name!="fp1400_l_eye_lod0_eyeh"||
           fp_materials.entries[1].eye_conjunctiva_name!="fp1400_r_eye_lod0_conj"||
           fp_materials.entries[1].eye_iris_name!="fp1400_r_eye_lod0_iris"||
           fp_materials.entries[1].eye_highlight_name!="fp1400_r_eye_lod0_eyeh")return 19;
        std::vector<gbfr::PreviewMaterialTextures> fp_preview_materials(fp_materials.entries.size());
        const auto granite_4k=integration.parent_path()/L"unpack/data/granite/4k";
        for(std::size_t i=0;i<2;++i) {
            const auto& entry=fp_materials.entries[i];auto& material=fp_preview_materials[i];
            material.eye_conjunctiva=granite_4k/fs::path(entry.eye_conjunctiva_name+".dds");
            material.eye_iris=granite_4k/fs::path(entry.eye_iris_name+".dds");
            material.eye_highlight=granite_4k/fs::path(entry.eye_highlight_name+".dds");
        }
        if(!preview.load(fp_mesh,fp_skeleton,fp_preview_materials))return 20;
        preview.frame(camera);preview.render(camera,true,gbfr::PreviewShadingMode::lit,true);context->Flush();
        if(FAILED(device->GetDeviceRemovedReason()))return 21;
        const auto cloth_root=integration.parent_path()/L"unpack/data/pl/pl1400/cloth";
        const auto clh_path=cloth_root/L"pl1400_0_0_clh.bxm.xml",clp_path=cloth_root/L"pl1400_0_0_clp.bxm.xml";
        const auto clh=gbfr::load_clh(clh_path);const auto clp=gbfr::load_clp(clp_path);
        if(clh.collisions.size()!=8||clp.nodes.size()!=60)return 9;
        const auto editable=fs::temp_directory_path()/L"gbfr_clh_test.xml";fs::copy_file(clh_path,editable,fs::copy_options::overwrite_existing);auto collision=clh.collisions.front();collision.radius=.123f;gbfr::save_clh_collision(editable,collision);if(std::abs(gbfr::load_clh(editable).collisions.front().radius-.123f)>.0001f)return 10;fs::remove(editable);
    }
    const auto corrupt = fs::temp_directory_path() / L"gbfr_corrupt.skeleton";
    std::ofstream(corrupt, std::ios::binary | std::ios::trunc) << "bad";
    try { (void)gbfr::load_skeleton(corrupt); return 8; } catch (const std::exception&) {}
    fs::remove(corrupt);
    return 0;
}
