#include <gbfr/core/log.hpp>
#include <gbfr/core/workspace.hpp>
#include <gbfr/formats/model.hpp>
#include <gbfr/formats/material.hpp>
#include <gbfr/formats/cloth.hpp>
#include <gbfr/formats/animation.hpp>
#include <gbfr/formats/sop.hpp>
#include <gbfr/render/preview_renderer.hpp>
#include <nlohmann/json.hpp>
#include <wrl/client.h>
#include <DirectXMath.h>

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
    {
        nlohmann::json bone_names;
        std::ifstream(fs::path(GBFR_ROOT_DIR)/L"_lib/humanoid_bone_names.json")>>bone_names;
        if(bone_names.value("_830",std::string{})!="Brow_01_L"||bone_names.value("_840",std::string{})!="Lid Top_00_L"||
           bone_names.value("_8c2",std::string{})!="Tongue Tip"||bone_names.value("_820",std::string{})!="Mouth Inside Bottom")return 78;
    }
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 82;
        gbfr::PreviewRenderer shader_smoke;
        if(!shader_smoke.initialize(device.Get(),context.Get(),fs::path(GBFR_ROOT_DIR)/L"assets/shaders/preview.hlsl"))return 83;
    }

    const fs::path test_temp = fs::current_path() / L".gbfr_test_temp";
    const fs::path root = test_temp / L"workspace";
    fs::remove_all(test_temp);
    fs::remove_all(root);
    fs::create_directories(root / L"source");
    fs::create_directories(root / L"unpack");
    {
        std::vector<unsigned char> bytes(84,0);
        const auto put_u16=[&](std::size_t offset,std::uint16_t value){bytes[offset]=static_cast<unsigned char>(value);bytes[offset+1]=static_cast<unsigned char>(value>>8);};
        for(std::uint16_t i=0;i<8;++i)put_u16(32+i*2,i);
        for(std::size_t i=0;i<8;++i)put_u16(48+i*2,static_cast<std::uint16_t>(65535-i*4096));
        bytes[64]=255;bytes[65]=128;bytes[66]=0;bytes[67]=64;put_u16(68,0x3c00);put_u16(70,0x4000);
        const auto path=test_temp/L"v2_channels.mmesh";
        std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        gbfr::ModelInfoAsset info;info.bones_to_weight_indices={10,11,12,13,14,15,16,17};
        gbfr::ModelLodAsset lod;lod.vertex_count=1;lod.index_count=3;lod.buffer_types=127;
        lod.buffers={{0,32},{32,8},{40,8},{48,8},{56,8},{64,4},{68,4},{72,12}};info.lods.push_back(lod);
        const auto mesh=gbfr::load_mmesh(path,info);
        if(mesh.influence_count!=8||!mesh.has_color||!mesh.has_uv1||mesh.vertices[0].joints[7]!=17||
           std::abs(mesh.vertices[0].weights[4]-static_cast<float>(65535-4*4096)/65535.0f)>1e-6f||
           std::abs(mesh.vertices[0].color.y-128.0f/255.0f)>1e-6f||mesh.vertices[0].uv1.x!=1.0f||mesh.vertices[0].uv1.y!=2.0f)return 84;
    }
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

    const fs::path wtb_root = test_temp / L"wtb_workspace";
    fs::create_directories(wtb_root / L"source");
    fs::create_directories(wtb_root / L"unpack");
    auto put_u32 = [](std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset + shift / 8] = static_cast<unsigned char>((value >> shift) & 0xff);
    };
    std::vector<unsigned char> wtb(0x1000, 0);
    wtb[0] = 'W'; wtb[1] = 'T'; wtb[2] = 'B';
    put_u32(wtb, 4, 3); put_u32(wtb, 8, 1); put_u32(wtb, 12, 0x20); put_u32(wtb, 16, 0x40); put_u32(wtb, 20, 0x60); put_u32(wtb, 24, 0x80);
    const std::vector<unsigned char> baseline_dds{'D','D','S',' ',1,2,3,4};
    put_u32(wtb, 0x20, 0x1000); put_u32(wtb, 0x40, static_cast<std::uint32_t>(baseline_dds.size()));
    wtb.insert(wtb.end(), baseline_dds.begin(), baseline_dds.end());
    std::ofstream(wtb_root / L"source/ui.wtb", std::ios::binary).write(reinterpret_cast<const char*>(wtb.data()), static_cast<std::streamsize>(wtb.size()));
    std::ofstream(wtb_root / L"unpack/ui_0.dds", std::ios::binary).write(reinterpret_cast<const char*>(baseline_dds.data()), static_cast<std::streamsize>(baseline_dds.size()));
    const auto wtb_hash = gbfr::sha256_file(wtb_root / L"source/ui.wtb");
    const auto dds_hash = gbfr::sha256_file(wtb_root / L"unpack/ui_0.dds");
    {
        std::ofstream json(wtb_root / L"workspace.json");
        json << "{\"Version\":1,\"CharacterId\":\"ui\",\"UIImages\":[{"
                "\"Source\":\"source/ui.wtb\",\"SourceSha256\":\"" << wtb_hash << "\","
                "\"Output\":\"build/ui.wtb\",\"Category\":\"角色立绘\",\"Slots\":[{"
                "\"Index\":0,\"Path\":\"unpack/ui_0.dds\",\"BaselineSha256\":\"" << dds_hash << "\"}]}]}";
    }
    auto wtb_workspace = gbfr::Workspace::load(wtb_root / L"workspace.json");
    if (wtb_workspace.assets().size() != 1 || wtb_workspace.assets()[0].kind != gbfr::AssetKind::ui_image) return 72;
    std::ofstream(wtb_root / L"unpack/ui_0.dds", std::ios::binary | std::ios::trunc) << "DDS edited payload";
    wtb_workspace.refresh();
    if (!wtb_workspace.assets()[0].changed) return 73;
    wtb_workspace.build_asset(0);
    if (!fs::is_regular_file(wtb_root / L"build/ui.wtb")) return 74;
    if (gbfr::sha256_file(wtb_root / L"build/ui.wtb") == wtb_hash) return 76;
    wtb_workspace.restore_asset(0);
    if (gbfr::sha256_file(wtb_root / L"unpack/ui_0.dds") != dds_hash) return 75;
    fs::remove_all(wtb_root);

    const fs::path material_root = test_temp / L"material_workspace";
    fs::create_directories(material_root / L"unpack");
    nlohmann::json material_fixture={{"Magic",20230727},{"Entries1",nlohmann::json::array({{{"A4",{{"Unk",nlohmann::json::array({"hash"})}}}}})}};
    { std::ofstream stream(material_root/L"unpack/0.mmat.json"); stream << material_fixture.dump(2) << '\n'; }
    const auto material_fixture_hash=gbfr::sha256_file(material_root/L"unpack/0.mmat.json");
    { std::ofstream manifest(material_root/L"workspace.json"); manifest << "{\"Version\":1,\"CharacterId\":\"material\",\"Materials\":[{\"Json\":\"unpack/0.mmat.json\",\"Source\":\"source/0.mmat\",\"Output\":\"build/0.mmat\",\"BaselineSha256\":\"" << material_fixture_hash << "\",\"SourceSha256\":\"\"}]}"; }
    auto material_workspace=gbfr::Workspace::load(material_root/L"workspace.json");
    if(material_workspace.assets().size()!=1||material_workspace.material_a4_count(0)!=1||material_workspace.remove_material_a4(0)!=1||material_workspace.material_a4_count(0)!=0||!material_workspace.assets()[0].changed)return 77;
    fs::remove_all(material_root);

    const fs::path integration = fs::path(GBFR_ROOT_DIR) / L"explore_output/workspace.json";
    if(fs::is_regular_file(integration)){
        nlohmann::json current;std::ifstream(integration)>>current;
        const auto id=current.value("CharacterId",std::string{});
        const auto current_root=integration.parent_path()/L"unpack/data/model/pl"/fs::path(id);
        const auto current_minfo=current_root/fs::path(id+".minfo");
        if(fs::is_regular_file(current_minfo)){
            const auto info=gbfr::load_minfo(current_minfo);
            if(info.lods.empty()||info.vertex_count!=info.lods.front().vertex_count||info.chunks.size()!=info.lods.front().chunks.size())return 79;
            for(std::size_t lod=0;lod<info.lods.size();++lod){
                const auto path=integration.parent_path()/L"unpack/data/model_streaming"/fs::path(L"lod"+std::to_wstring(lod))/fs::path(std::wstring(id.begin(),id.end())+L".mmesh");
                if(!fs::is_regular_file(path))continue;
                const auto mesh=gbfr::load_mmesh(path,info,lod);
                if(mesh.vertices.size()!=info.lods[lod].vertex_count||mesh.indices.size()!=info.lods[lod].index_count||mesh.buffer_types!=info.lods[lod].buffer_types)return 80;
                if(mesh.influence_count!=((mesh.buffer_types&16)?8:((mesh.buffer_types&8)?4:0))||mesh.has_color!=static_cast<bool>(mesh.buffer_types&32)||mesh.has_uv1!=static_cast<bool>(mesh.buffer_types&64))return 81;
            }
            const auto skeleton_path=current_root/fs::path(id+".skeleton");
            const auto mesh_path=integration.parent_path()/L"unpack/data/model_streaming/lod0"/fs::path(id+".mmesh");
            const auto motion_path=integration.parent_path()/L"source/data/pl"/fs::path(id)/fs::path(id+"_0a23.mot");
            if(fs::is_regular_file(skeleton_path)&&fs::is_regular_file(mesh_path)){
                Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
                if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 85;
                gbfr::PreviewRenderer preview;
                if(!preview.initialize(device.Get(),context.Get(),fs::path(GBFR_ROOT_DIR)/L"assets/shaders/preview.hlsl"))return 86;
                const auto mesh=gbfr::load_mmesh(mesh_path,info);
                const auto skeleton=gbfr::load_skeleton(skeleton_path);
                if(!preview.load(mesh,skeleton))return 87;
                preview.resize(256,256);gbfr::OrbitCamera camera;preview.frame(camera);
                preview.render(camera,true,gbfr::PreviewShadingMode::lit,false,false);const auto without_skeleton=preview.render_target_hash();
                preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,false);const auto with_skeleton=preview.render_target_hash();
                if(!without_skeleton||!with_skeleton||without_skeleton==with_skeleton)return 90;
                if(fs::is_regular_file(motion_path)){
                    const auto clip=gbfr::load_mot(motion_path);
                    const float test_frame=clip.frame_count?std::min(310.0f,static_cast<float>(clip.frame_count-1)):0.0f;
                    if(!preview.apply_animation(&clip,test_frame))return 88;
                    for(const auto& p:preview.bone_positions())if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z)>10.0f)return 91;
                    const auto sop_path=integration.parent_path()/L"source/data/model/pl"/fs::path(id)/fs::path(id+".sop");
                    if(fs::is_regular_file(sop_path)&&(!preview.load(mesh,skeleton,{},gbfr::load_sop(sop_path))||!preview.apply_animation(&clip,test_frame)))return 89;
                }
            }
        }
    }
    const fs::path pl1400_sample = integration.parent_path() / L"unpack/data/model/pl/pl1400/pl1400.minfo";
    if (fs::is_regular_file(integration) && fs::is_regular_file(pl1400_sample)) {
        const auto pl1400 = gbfr::Workspace::load(integration);
        nlohmann::json document;
        std::ifstream(integration) >> document;
        const auto expected_assets=document.value("Textures",nlohmann::json::array()).size()+
            document.value("UIImages",nlohmann::json::array()).size()+
            document.value("Materials",nlohmann::json::array()).size()+
            document.value("ClothFiles",nlohmann::json::array()).size()+
            document.value("ModelFiles",nlohmann::json::array()).size()+
            document.value("NewTextures",nlohmann::json::array()).size();
        if (pl1400.assets().size() != expected_assets) return 6;
        for(std::size_t i=1;i<pl1400.assets().size();++i)if(gbfr::natural_less_case_insensitive(pl1400.assets()[i].input.filename().native(),pl1400.assets()[i-1].input.filename().native()))return 32;
        if(!gbfr::natural_less_case_insensitive(L"2.mmat.json",L"10.mmat.json")||gbfr::natural_less_case_insensitive(L"10.mmat.json",L"2.mmat.json"))return 45;
        const auto model_root = integration.parent_path() / L"unpack/data/model/pl/pl1400";
        const auto minfo = gbfr::load_minfo(model_root / L"pl1400.minfo");
        const auto skeleton = gbfr::load_skeleton(model_root / L"pl1400.skeleton");
        const auto sop_path=integration.parent_path()/L"source/data/model/pl/pl1400/pl1400.sop";
        const auto sop=gbfr::load_sop(sop_path);
        nlohmann::json sop_catalog;
        {std::ifstream input(fs::path(GBFR_ROOT_DIR)/L"_lib/sop_operations_zh.json");if(!input)return 59;input>>sop_catalog;}
        const auto mesh = gbfr::load_mmesh(integration.parent_path() / L"unpack/data/model_streaming/lod0/pl1400.mmesh", minfo);
        if (mesh.vertices.size() != minfo.vertex_count || mesh.indices.size() != minfo.index_count || skeleton.bones.empty()) return 7;
        if(sop.version!=gbfr::sop_version_20200309||sop.operations.size()!=101)return 52;
        const auto swing_twist_count=std::count_if(sop.operations.begin(),sop.operations.end(),[](const auto& operation){return operation.type_hash==gbfr::sop_swing_twist_operation;});
        const auto twist_count=std::count_if(sop.operations.begin(),sop.operations.end(),[](const auto& operation){return operation.type_hash==gbfr::sop_twist_operation;});
        if(swing_twist_count!=16||twist_count!=22)return 53;
        if(sop_catalog.value("SchemaVersion",0)!=1||!sop_catalog.contains("Operations")||sop_catalog["Operations"].size()!=7)return 60;
        for(const auto& operation:sop.operations){const auto operation_hash="0x"+[] (std::uint32_t value){char text[9]{};constexpr char digits[]="0123456789ABCDEF";for(int i=0;i<8;++i)text[7-i]=digits[(value>>(i*4))&0xfu];return std::string(text,8);}(operation.type_hash);if(std::none_of(sop_catalog["Operations"].begin(),sop_catalog["Operations"].end(),[&](const auto& item){return item.value("Hash",std::string{})==operation_hash;}))return 61;}
        const auto& first_sop=sop.operations.front();
        if(first_sop.target_bone!=0xA12u||first_sop.source_bone!=0x12u||first_sop.type_hash!=gbfr::sop_swing_twist_operation||
           !first_sop.find(gbfr::sop_axis_y_property)||!first_sop.find(gbfr::sop_twist_rate_property)||!first_sop.find(gbfr::sop_swing_rate_property))return 54;
        if(skeleton.bones[0].parent!=0xffff||skeleton.bones[1].parent!=0||std::abs(skeleton.bones[1].world_position.y-.82156992f)>1e-6f)return 43;
        const auto materials=gbfr::load_mmat_json(model_root/L"vars/0.mmat.json");
        if(materials.entries.size()!=11||materials.entries[0].albedo_name!="pl1400_body01_lod0_albd")return 11;
        for(const auto& entry:materials.entries)if(gbfr::is_color_variant_texture(entry.albedo_name))return 12;
        for(const auto& chunk:mesh.chunks)if(chunk.material>=materials.entries.size()||chunk.offset+chunk.count>mesh.indices.size())return 13;
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 15;
        gbfr::PreviewRenderer preview;
        const auto dds=integration.parent_path()/L"unpack/data/granite/2k/pl1400_body01_lod0_albd.dds";
        const auto shader_file=fs::path(GBFR_ROOT_DIR)/L"assets/shaders/preview.hlsl";
        if(!preview.initialize(device.Get(),context.Get(),shader_file)||!preview.load_texture_preview(dds)||!preview.texture_image()||!preview.texture_width()||!preview.texture_height())return 16;
        gbfr::TexturePreviewResource thumbnail;
        if(!preview.load_texture_thumbnail(dds,thumbnail,128)||!thumbnail.image||std::max(thumbnail.width,thumbnail.height)>128)return 77;
        std::vector<gbfr::PreviewMaterialTextures> preview_materials(materials.entries.size());for(auto& material:preview_materials)material.albedo=dds;
        gbfr::OrbitCamera camera;
        if(!preview.load(mesh,skeleton,preview_materials,sop))return 17;
        preview.resize(320,320);preview.frame(camera);
        preview.set_collision_lines({{0.0f,0.0f,0.0f},{0.0f,1.0f,0.0f}});
        preview.set_cloth_lines({{0.0f,0.0f,0.0f},{0.0f,.5f,0.0f}},{{0.0f,.5f,0.0f},{.5f,.5f,0.0f}},{{.5f,.5f,0.0f},{.5f,0.0f,0.0f}});
        for(const auto mode:{gbfr::PreviewShadingMode::unlit,gbfr::PreviewShadingMode::lit,gbfr::PreviewShadingMode::wireframe})preview.render(camera,true,mode,true,true);
        preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,false);
        context->Flush();
        if(FAILED(device->GetDeviceRemovedReason()))return 18;
        const auto fp_root=integration.parent_path()/L"unpack/data/model/fp/fp1400";
        const auto fp_minfo=gbfr::load_minfo(fp_root/L"fp1400.minfo");
        const auto fp_skeleton=gbfr::load_skeleton(fp_root/L"fp1400.skeleton");
        const auto fp_mesh=gbfr::load_mmesh(integration.parent_path()/L"unpack/data/model_streaming/lod0/fp1400.mmesh",fp_minfo);
        const auto fp_materials=gbfr::load_mmat_json(fp_root/L"vars/0.mmat.json");
        if(fp_skeleton.bones[0].parent!=0xffff||fp_skeleton.bones[1].parent!=0||std::abs(fp_skeleton.bones[1].world_position.y-1.13228768f)>1e-6f||std::abs(fp_skeleton.bones[78].world_position.y-1.20115548f)>1e-4f)return 44;
        if(fp_materials.entries.size()!=7||
           fp_materials.entries[0].eye_conjunctiva_name!="fp1400_l_eye_lod0_conj"||
           fp_materials.entries[0].eye_iris_name!="fp1400_l_eye_lod0_iris"||
           fp_materials.entries[0].eye_highlight_name!="fp1400_l_eye_lod0_eyeh"||
           fp_materials.entries[1].eye_conjunctiva_name!="fp1400_r_eye_lod0_conj"||
           fp_materials.entries[1].eye_iris_name!="fp1400_r_eye_lod0_iris"||
           fp_materials.entries[1].eye_highlight_name!="fp1400_r_eye_lod0_eyeh"||
           fp_materials.entries[3].alpha_mask_name!="fp1400_face_lod0_msk2"||
           fp_materials.entries[4].alpha_mask_name!="fp1400_face_lod0_msk2"||
           !fp_materials.entries[2].alpha_clipped||fp_materials.entries[2].alpha_blended||
           !fp_materials.entries[3].alpha_clipped||fp_materials.entries[3].alpha_blended||fp_materials.entries[3].alpha_masked||
           fp_materials.entries[4].alpha_clipped||!fp_materials.entries[4].alpha_blended||!fp_materials.entries[4].alpha_masked)return 19;
        std::vector<gbfr::PreviewMaterialTextures> fp_preview_materials(fp_materials.entries.size());
        const auto granite_4k=integration.parent_path()/L"unpack/data/granite/4k";
        for(std::size_t i=0;i<fp_preview_materials.size();++i){fp_preview_materials[i].alpha_clipped=fp_materials.entries[i].alpha_clipped;fp_preview_materials[i].alpha_blended=fp_materials.entries[i].alpha_blended;if(fp_materials.entries[i].alpha_blended&&!fp_materials.entries[i].alpha_mask_name.empty())fp_preview_materials[i].alpha_mask=granite_4k/fs::path(fp_materials.entries[i].alpha_mask_name+".dds");}
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
        preview.frame(camera);preview.render(camera,true,gbfr::PreviewShadingMode::lit,false,false);const auto face_rest_render_hash=preview.render_target_hash();if(!face_rest_render_hash)return 46;
        const auto face_motion_root=integration.parent_path()/L"source/data/fp/fp1400";
        std::size_t face_motion_count{};
        for(const auto& entry:fs::directory_iterator(face_motion_root))if(entry.path().extension()==L".mot"){
            const auto clip=gbfr::load_mot(entry.path());++face_motion_count;
            for(const auto& track:clip.tracks)if(!std::isfinite(track.sample(static_cast<float>(clip.frame_count-1)*.5f)))return 33;
        }
        if(face_motion_count!=80)return 34;
        const auto expression=gbfr::load_mot(face_motion_root/L"fp1400_a000.mot");
        const auto face_rest_hash=preview.pose_hash();
        if(!preview.apply_animation(&expression,0.0f))return 35;
        const auto expression_002b=gbfr::load_mot(face_motion_root/L"fp1400_002b.mot");
        if(!preview.apply_animation(&expression_002b,0.0f)||preview.pose_hash()==face_rest_hash)return 41;
        {const auto& rest=fp_rest_bones[70];const auto& posed=preview.bone_positions()[70];if(posed.y<1.0f||std::abs(rest.x-posed.x)+std::abs(rest.y-posed.y)+std::abs(rest.z-posed.z)<1e-4f)return 47;}
        preview.render(camera,true,gbfr::PreviewShadingMode::lit,false,false);const auto face_animated_render_hash=preview.render_target_hash();if(!face_animated_render_hash||face_animated_render_hash==face_rest_render_hash)return 48;
        {int view_index{};for(const float yaw:{-.65f,0.0f,.65f}){camera.yaw=yaw;preview.render(camera,true,gbfr::PreviewShadingMode::lit,false,false,false);const auto without_overlay=preview.render_target_hash();preview.render(camera,true,gbfr::PreviewShadingMode::lit,false,false,true);const auto with_overlay=preview.render_target_hash();if(!without_overlay||!with_overlay||without_overlay==with_overlay)return 49+view_index;++view_index;}}
        camera.yaw=0.0f;
        if(!preview.apply_animation(&expression,0.0f))return 42;
        if(preview.pose_hash()==face_rest_hash||!preview.apply_animation(nullptr,0.0f)||preview.pose_hash()!=face_rest_hash)return 36;
        preview.frame(camera);preview.render(camera,true,gbfr::PreviewShadingMode::lit,true,true);context->Flush();
        if(FAILED(device->GetDeviceRemovedReason()))return 21;
        const auto cloth_root=integration.parent_path()/L"unpack/data/pl/pl1400/cloth";
        const auto clh_path=cloth_root/L"pl1400_0_0_clh.bxm.xml",clp_path=cloth_root/L"pl1400_0_0_clp.bxm.xml";
        const auto clh=gbfr::load_clh(clh_path);const auto clp=gbfr::load_clp(clp_path);
        if(clh.collisions.size()!=8||clp.nodes.size()!=60||clp.id!=0||clp.collision_flags!=18)return 9;
        const auto cloth_sequence_root=integration.parent_path()/L"unpack/data/pl/pl1400";
        const auto cloth_sequence=gbfr::load_cloth_sequence(cloth_sequence_root/L"pl1400_0002_0_seq_edit_cloth.bxm.xml");
        if(cloth_sequence.events.size()!=8||cloth_sequence.events[0].file_id!=0||cloth_sequence.events[0].collision_ids[0]!=0||cloth_sequence.events[0].collision_ids[1]!=1||cloth_sequence.events[1].file_id!=5||cloth_sequence.events[0].layer_flags!=0xffffffffu||std::abs(cloth_sequence.events.back().start_time-12.450001f)>.0001f)return 74;
        std::size_t cloth_sequence_count{},cloth_event_count{};
        for(const auto& entry:fs::directory_iterator(cloth_sequence_root))if(entry.is_regular_file()&&entry.path().filename().string().find("_seq_edit_cloth.bxm.xml")!=std::string::npos){const auto sequence=gbfr::load_cloth_sequence(entry.path());++cloth_sequence_count;cloth_event_count+=sequence.events.size();if(!std::is_sorted(sequence.events.begin(),sequence.events.end(),[](const auto& left,const auto& right){return left.start_time<right.start_time;}))return 75;}
        if(cloth_sequence_count!=43||cloth_event_count!=229)return 76;
        if(clh.collisions[0].capsule!=-1||clh.collisions[1].capsule!=clh.collisions[0].id||clh.collisions[2].capsule!=clh.collisions[1].id||clh.collisions[3].capsule!=clh.collisions[2].id)return 62;
        if(std::any_of(clh.collisions.begin(),clh.collisions.end(),[](const auto& collision){return collision.p1!=collision.p2||collision.weight!=0.0f;}))return 63;
        const auto clp_grid=gbfr::load_clp(cloth_root/L"pl1400_0_2_clp.bxm.xml");
        const auto cross_link=std::find_if(clp_grid.nodes.begin(),clp_grid.nodes.end(),[](const auto& node){return node.side!=4095;});
        if(cross_link==clp_grid.nodes.end()||cross_link->side!=cross_link->poly)return 64;
        constexpr std::array expected_collision_masks{18,97,4,2,1,8,128,0};
        std::size_t collision_count{},blended_attachment_count{},capsule_count{},longitudinal_count{},side_count{},poly_count{};
        std::array<std::vector<int>,8> cloth_bones;
        for(int group=0;group<8;++group){
            const auto suffix=std::to_wstring(group);
            const auto group_clh=gbfr::load_clh(cloth_root/(L"pl1400_0_"+suffix+L"_clh.bxm.xml"));
            const auto group_clp=gbfr::load_clp(cloth_root/(L"pl1400_0_"+suffix+L"_clp.bxm.xml"));
            if(group_clp.id!=group||group_clp.collision_flags!=expected_collision_masks[static_cast<std::size_t>(group)])return 72;
            collision_count+=group_clh.collisions.size();
            for(const auto& item:group_clh.collisions){
                if(item.p1!=item.p2){if(item.weight==0.0f)return 67;++blended_attachment_count;}
                if(item.capsule>=0){if(std::none_of(group_clh.collisions.begin(),group_clh.collisions.end(),[&](const auto& target){return target.id==item.capsule;}))return 68;++capsule_count;}
            }
            for(const auto& node:group_clp.nodes){cloth_bones[static_cast<std::size_t>(group)].push_back(node.bone);if(node.down!=4095)++longitudinal_count;if(node.side!=4095)++side_count;if(node.poly!=4095)++poly_count;}
        }
        if(collision_count!=112||blended_attachment_count!=21||capsule_count!=46||longitudinal_count!=141||side_count!=52||poly_count!=52)return 69;
        const auto clh7=gbfr::load_clh(cloth_root/L"pl1400_0_7_clh.bxm.xml");
        const auto references_group2=[&](int bone){const auto& group=cloth_bones[2];return std::find(group.begin(),group.end(),bone)!=group.end();};
        if(std::count_if(clh7.collisions.begin(),clh7.collisions.end(),[&](const auto& item){return references_group2(item.p1)||references_group2(item.p2);})<10)return 73;
        const auto editable=test_temp/L"gbfr_clh_test.xml";fs::copy_file(clh_path,editable,fs::copy_options::overwrite_existing);auto collision=clh.collisions.front();collision.radius=.123f;gbfr::save_clh_collision(editable,collision);if(std::abs(gbfr::load_clh(editable).collisions.front().radius-.123f)>.0001f)return 10;fs::remove(editable);
        const auto motion_root=integration.parent_path()/L"source/data/pl/pl1400";
        const auto idle=gbfr::load_mot(motion_root/L"pl1400_0000.mot");
        if(idle.version!=0x20200619u||idle.frame_count!=86||idle.tracks.size()!=219||idle.name!="pl1400_0000")return 22;
        const auto first_motion_track=std::find_if(idle.tracks.begin(),idle.tracks.end(),[](const auto& track){return track.bone_id==0&&track.property==0;});
        if(first_motion_track==idle.tracks.end()||first_motion_track->compression!=1||std::abs(first_motion_track->sample(1.0f)-(-.000534661114f))>1e-7f)return 23;
        if(!preview.load(mesh,skeleton,preview_materials,sop))return 27;
        const auto rest_bones=preview.bone_positions();
        {gbfr::Vec3 transformed{};if(!preview.transform_bone_point(0,{},transformed)||std::abs(transformed.x-rest_bones[0].x)+std::abs(transformed.y-rest_bones[0].y)+std::abs(transformed.z-rest_bones[0].z)>1e-5f)return 65;}
        {const gbfr::Vec3 local{.031f,-.017f,.043f};gbfr::Vec3 transformed{};if(!preview.transform_bone_point(0,local,transformed))return 70;const auto& root_bone=skeleton.bones[0];const auto matrix=DirectX::XMMatrixScaling(root_bone.scale.x,root_bone.scale.y,root_bone.scale.z)*DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(root_bone.rotation.x,root_bone.rotation.y,root_bone.rotation.z,root_bone.rotation.w))*DirectX::XMMatrixTranslation(root_bone.position.x,root_bone.position.y,root_bone.position.z);DirectX::XMFLOAT3 expected{};DirectX::XMStoreFloat3(&expected,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(local.x,local.y,local.z,1),matrix));if(std::abs(transformed.x-expected.x)+std::abs(transformed.y-expected.y)+std::abs(transformed.z-expected.z)>1e-5f)return 71;}
        for(std::size_t i=0;i<rest_bones.size();++i){const auto& actual=rest_bones[i];const auto& expected=skeleton.bones[i].world_position;if(std::abs(actual.x-expected.x)+std::abs(actual.y-expected.y)+std::abs(actual.z-expected.z)>1e-4f)return 31;}
        if(!preview.apply_animation(&idle,42.0f)||preview.bone_positions().size()!=skeleton.bones.size())return 30;
        {gbfr::Vec3 transformed{};if(!preview.transform_bone_point(0,{},transformed)||std::abs(transformed.x-preview.bone_positions()[0].x)+std::abs(transformed.y-preview.bone_positions()[0].y)+std::abs(transformed.z-preview.bone_positions()[0].z)>1e-5f)return 66;}
        if(preview.applied_sop_operation_count()<30)return 55;
        const auto sop_pose_hash=preview.pose_hash();
        if(!preview.load(mesh,skeleton,preview_materials)||!preview.apply_animation(&idle,42.0f)||preview.applied_sop_operation_count()!=0||preview.pose_hash()==sop_pose_hash)return 57;
        if(!preview.load(mesh,skeleton,preview_materials,sop)||!preview.apply_animation(&idle,42.0f)||preview.pose_hash()!=sop_pose_hash)return 58;
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
    const auto corrupt_sop=test_temp/L"gbfr_corrupt.sop";
    std::ofstream(corrupt_sop,std::ios::binary|std::ios::trunc)<<"sop";
    try{(void)gbfr::load_sop(corrupt_sop);return 56;}catch(const std::exception&){}
    fs::remove_all(test_temp);
    return 0;
}
