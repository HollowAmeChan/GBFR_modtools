#include <gbfr/formats/material_variants.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
void write_json(const fs::path& path,const json& document) {
    std::ofstream output(path,std::ios::trunc);
    output<<document.dump(2)<<'\n';
}

json read_json(const fs::path& path) {
    std::ifstream input(path);
    json document;
    input>>document;
    return document;
}

json fixture(const char* texture,std::uint32_t hash,bool source) {
    return {
        {"magic",20230727},
        {"materials",json::array({{
            {"shader_params",json::array({
                {{"param_hash","g_EnableOutLine"},{"value_or_offset",source?1:0},{"value_type","U8"}},
                {{"param_hash","g_56346692"},{"value_or_offset",source?0:2},{"value_type","Vec2"}}
            })},
            {"texture_maps",json::array({{{"shader_map_name_hash","g_AlbedoMap"},{"texture_name",texture}}})},
            {"constant_buffer_indices",json::array({0})},
            {"granite_params",{{"page_file",json::array({std::string(texture)+"_page"})},
                               {"layer_to_shader_map_name_hash",json::array({"g_AlbedoMap"})}}},
            {"unique_material_name_hash_maybe",hash},
            {"shader_type",5},{"shader_sub_type",7},
            {"shadow_type",source?"ShadowEnable_AlphaBlend":"ShadowEnable_Unk1"},
            {"bool9",source},{"bool10",true},{"ignore_alpha",!source},{"bool12",source}
        }})},
        {"constant_buffers",json::array({{{"buffer",json::array({source?99u:42u})},
                                           {"unk_unique_param_name_hash",source?9u:4u}}})},
        {"shader_param_float_data_pool",source?json::array({1.25f,2.5f}):json::array({9.0f,8.0f,7.0f,6.0f})}
    };
}
}

int main() {
    const auto root=fs::current_path()/L".gbfr_material_variants_test_temp";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto source_path=root/L"5.mmat.json";
    const auto target0_path=root/L"0.mmat.json";
    const auto target10_path=root/L"10.mmat.json";
    write_json(source_path,fixture("source_albd",111u,true));
    write_json(target0_path,fixture("target0_albd",222u,false));
    write_json(target10_path,fixture("target10_albd",333u,false));

    if(gbfr::adjacent_mmat_variant_jsons(source_path).size()!=2)return 1;
    if(gbfr::propagate_mmat_material_render_settings(source_path,0)!=2)return 2;
    const auto updated=read_json(target0_path);
    const auto& material=updated["materials"][0];
    if(material["shadow_type"]!="ShadowEnable_AlphaBlend"||material["ignore_alpha"]!=false||
       material["bool9"]!=true||material["bool12"]!=true)return 3;
    if(material["shader_params"][0]["value_or_offset"]!=1||
       material["shader_params"][1]["value_or_offset"]!=2)return 4;
    if(updated["shader_param_float_data_pool"]!=json::array({9.0f,8.0f,1.25f,2.5f}))return 5;
    if(material["texture_maps"][0]["texture_name"]!="target0_albd"||
       material["granite_params"]["page_file"][0]!="target0_albd_page"||
       material["unique_material_name_hash_maybe"]!=222u||
       updated["constant_buffers"][0]["buffer"][0]!=42u)return 6;

    const auto before_failed_batch=updated;
    auto incompatible=read_json(target10_path);
    incompatible["materials"][0]["shader_sub_type"]=8;
    write_json(target10_path,incompatible);
    auto changed_source=read_json(source_path);
    changed_source["materials"][0]["shadow_type"]="NoShadow";
    write_json(source_path,changed_source);
    bool rejected{};
    try{gbfr::propagate_mmat_material_render_settings(source_path,0);}catch(const std::exception&){rejected=true;}
    if(!rejected||read_json(target0_path)!=before_failed_batch)return 7;

    fs::remove_all(root);
    return 0;
}
