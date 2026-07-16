#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/material.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/formats/animation.hpp>
#include <gbfr/render/preview_renderer.hpp>
#include <nlohmann/json.hpp>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

int main() {
    gbfr::Log::clear();
    gbfr::Log::write(gbfr::LogLevel::info, "smoke");
    if (gbfr::Log::snapshot().size() != 1) return 1;
    if(!gbfr::is_color_variant_texture("pl1400_body_lod0_c01_albd")||
       gbfr::is_color_variant_texture("pl1400_body_lod0_albd"))return 14;

    const fs::path test_temp = fs::current_path() / L".gbfr_test_temp";
    const fs::path root = test_temp / L"workspace";
    fs::remove_all(test_temp);
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
        for(std::size_t i=1;i<pl1400.assets().size();++i)if(_wcsicmp(pl1400.assets()[i-1].input.filename().c_str(),pl1400.assets()[i].input.filename().c_str())>0)return 32;
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
        preview.set_collision_lines({{0.0f,0.0f,0.0f},{0.0f,1.0f,0.0f}});
        for(const auto mode:{gbfr::PreviewShadingMode::unlit,gbfr::PreviewShadingMode::lit,gbfr::PreviewShadingMode::wireframe})preview.render(camera,true,mode,true,true);
        preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,false);
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
           fp_materials.entries[1].eye_highlight_name!="fp1400_r_eye_lod0_eyeh"||
           fp_materials.entries[2].alpha_blended||!fp_materials.entries[3].alpha_blended||!fp_materials.entries[4].alpha_blended)return 19;
        std::vector<gbfr::PreviewMaterialTextures> fp_preview_materials(fp_materials.entries.size());
        const auto granite_4k=integration.parent_path()/L"unpack/data/granite/4k";
        for(std::size_t i=0;i<fp_preview_materials.size();++i)fp_preview_materials[i].alpha_blended=fp_materials.entries[i].alpha_blended;
        for(std::size_t i=0;i<2;++i) {
            const auto& entry=fp_materials.entries[i];auto& material=fp_preview_materials[i];
            material.eye_conjunctiva=granite_4k/fs::path(entry.eye_conjunctiva_name+".dds");
            material.eye_iris=granite_4k/fs::path(entry.eye_iris_name+".dds");
            material.eye_highlight=granite_4k/fs::path(entry.eye_highlight_name+".dds");
        }
        for(std::size_t i=2;i<=4;++i)fp_preview_materials[i].albedo=granite_4k/L"fp1400_face_lod0_albd.dds";
        if(!preview.load(fp_mesh,fp_skeleton,fp_preview_materials))return 20;
        if(!preview.visible_bone_count()||preview.visible_bone_count()>=fp_skeleton.bones.size())return 40;
        const auto fp_rest_bones=preview.bone_positions();
        gbfr::AnimationClip empty_face_pose;empty_face_pose.frame_count=1;
        if(!preview.apply_animation(&empty_face_pose,0.0f)||preview.bone_positions().size()!=fp_rest_bones.size())return 37;
        for(std::size_t i=0;i<fp_rest_bones.size();++i){const auto& a=fp_rest_bones[i];const auto& b=preview.bone_positions()[i];if(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)>1e-4f)return 38;}
        if(!preview.apply_animation(nullptr,0.0f))return 39;
        const auto face_motion_root=integration.parent_path()/L"source/data/fp/fp1400";
        std::size_t face_motion_count{};
        for(const auto& entry:fs::directory_iterator(face_motion_root))if(entry.path().extension()==L".mot"){
            const auto clip=gbfr::load_mot(entry.path());++face_motion_count;
            for(const auto& track:clip.tracks)if(!std::isfinite(track.sample(static_cast<float>(clip.frame_count-1)*.5f)))return 33;
        }
        if(face_motion_count!=80)return 34;
        const auto expression=gbfr::load_mot(face_motion_root/L"fp1400_a000.mot");
        const auto face_rest_hash=preview.vertex_pose_hash();
        if(!preview.apply_animation(&expression,0.0f))return 35;
        if(preview.vertex_pose_hash()==face_rest_hash||!preview.apply_animation(nullptr,0.0f)||preview.vertex_pose_hash()!=face_rest_hash)return 36;
        preview.frame(camera);preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,true);context->Flush();
        if(FAILED(device->GetDeviceRemovedReason()))return 21;
        const auto cloth_root=integration.parent_path()/L"unpack/data/pl/pl1400/cloth";
        const auto clh_path=cloth_root/L"pl1400_0_0_clh.bxm.xml",clp_path=cloth_root/L"pl1400_0_0_clp.bxm.xml";
        const auto clh=gbfr::load_clh(clh_path);const auto clp=gbfr::load_clp(clp_path);
        if(clh.collisions.size()!=8||clp.nodes.size()!=60)return 9;
        const auto editable=test_temp/L"gbfr_clh_test.xml";fs::copy_file(clh_path,editable,fs::copy_options::overwrite_existing);auto collision=clh.collisions.front();collision.radius=.123f;gbfr::save_clh_collision(editable,collision);if(std::abs(gbfr::load_clh(editable).collisions.front().radius-.123f)>.0001f)return 10;fs::remove(editable);
        const auto motion_root=integration.parent_path()/L"source/data/pl/pl1400";
        const auto idle=gbfr::load_mot(motion_root/L"pl1400_0000.mot");
        if(idle.version!=0x20200619u||idle.frame_count!=86||idle.tracks.size()!=219||idle.name!="pl1400_0000")return 22;
        const auto first_motion_track=std::find_if(idle.tracks.begin(),idle.tracks.end(),[](const auto& track){return track.bone_id==0&&track.property==0;});
        if(first_motion_track==idle.tracks.end()||first_motion_track->compression!=1||std::abs(first_motion_track->sample(1.0f)-(-.000534661114f))>1e-7f)return 23;
        if(!preview.load(mesh,skeleton,preview_materials))return 27;
        const auto rest_bones=preview.bone_positions();
        for(std::size_t i=0;i<rest_bones.size();++i){const auto& actual=rest_bones[i];const auto& expected=skeleton.bones[i].world_position;if(std::abs(actual.x-expected.x)+std::abs(actual.y-expected.y)+std::abs(actual.z-expected.z)>1e-4f)return 31;}
        if(!preview.apply_animation(&idle,42.0f)||preview.bone_positions().size()!=skeleton.bones.size())return 30;
        bool pose_changed=false;for(std::size_t i=0;i<rest_bones.size();++i){const auto& a=rest_bones[i];const auto& b=preview.bone_positions()[i];if(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)>1e-5f){pose_changed=true;break;}}
        if(!pose_changed)return 28;
        preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,true);
        if(!preview.apply_animation(nullptr,0.0f))return 29;
        std::size_t motion_count{},track_count{};std::array<bool,9> compression_types{};
        for(const auto& entry:fs::directory_iterator(motion_root))if(entry.path().extension()==L".mot"){
            const auto clip=gbfr::load_mot(entry.path());++motion_count;track_count+=clip.tracks.size();
            for(const auto& track:clip.tracks){if(track.compression<0||track.compression>8)return 24;compression_types[static_cast<std::size_t>(track.compression)]=true;if(!std::isfinite(track.sample(static_cast<float>(clip.frame_count-1)*.5f)))return 25;}
        }
        if(motion_count!=524||track_count!=248401||!std::all_of(compression_types.begin(),compression_types.end(),[](bool value){return value;}))return 26;
    }
    const auto corrupt = test_temp / L"gbfr_corrupt.skeleton";
    std::ofstream(corrupt, std::ios::binary | std::ios::trunc) << "bad";
    try { (void)gbfr::load_skeleton(corrupt); return 8; } catch (const std::exception&) {}
    fs::remove_all(test_temp);
    return 0;
}
