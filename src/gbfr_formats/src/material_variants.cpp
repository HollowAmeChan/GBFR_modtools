#include <gbfr/formats/material_variants.hpp>

#include <gbfr/formats/material.hpp>

#include <nlohmann/json.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
using json = nlohmann::json;

std::string utf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}

std::size_t shader_parameter_components(const json& value_type) {
    if(value_type.is_number_integer()||value_type.is_number_unsigned()){
        const auto value=value_type.get<int>();
        return value>=2&&value<=5?static_cast<std::size_t>(value-1):0;
    }
    if(!value_type.is_string())return 0;
    const auto name=value_type.get<std::string>();
    if(name=="F32")return 1;
    if(name=="Vec2")return 2;
    if(name=="Vec3")return 3;
    if(name=="Vec4")return 4;
    return 0;
}

const json* matching_shader_parameter(const json& parameters,const json& source) {
    if(!parameters.is_array()||!source.contains("param_hash")||!source.contains("value_type"))return nullptr;
    for(const auto& candidate:parameters){
        if(candidate.is_object()&&candidate.value("param_hash",json{})==source["param_hash"]&&
           candidate.value("value_type",json{})==source["value_type"])return &candidate;
    }
    return nullptr;
}

void copy_shader_parameters(const json& source_document,const json& source_material,
                            json& target_document,json& target_material) {
    const auto source_parameters=source_material.value("shader_params",json::array());
    if(!source_parameters.is_array())throw std::runtime_error("源材质 shader_params 不是数组");
    const auto target_parameters=target_material.value("shader_params",json::array());
    auto copied_parameters=source_parameters;
    for(auto& copied:copied_parameters){
        if(!copied.is_object())throw std::runtime_error("源材质包含无效 Shader 参数");
        const auto components=shader_parameter_components(copied.value("value_type",json{}));
        if(!components)continue;
        if(!source_document.contains("shader_param_float_data_pool")||
           !source_document["shader_param_float_data_pool"].is_array())
            throw std::runtime_error("源材质缺少浮点参数池");
        const auto source_offset=copied.value("value_or_offset",std::numeric_limits<std::size_t>::max());
        const auto& source_pool=source_document["shader_param_float_data_pool"];
        if(source_offset>source_pool.size()||components>source_pool.size()-source_offset)
            throw std::runtime_error("源材质浮点参数越界");

        if(!target_document.contains("shader_param_float_data_pool"))
            target_document["shader_param_float_data_pool"]=json::array();
        auto& target_pool=target_document["shader_param_float_data_pool"];
        if(!target_pool.is_array())throw std::runtime_error("目标材质浮点参数池不是数组");
        auto target_offset=std::numeric_limits<std::size_t>::max();
        if(const auto* existing=matching_shader_parameter(target_parameters,copied))
            target_offset=existing->value("value_or_offset",std::numeric_limits<std::size_t>::max());
        if(target_offset>target_pool.size()||components>target_pool.size()-target_offset){
            target_offset=target_pool.size();
            if(target_offset>std::numeric_limits<std::uint16_t>::max())
                throw std::runtime_error("目标材质浮点参数池超过 ushort 索引范围");
            for(std::size_t component=0;component<components;++component)target_pool.push_back(source_pool[source_offset+component]);
        }else{
            for(std::size_t component=0;component<components;++component)target_pool[target_offset+component]=source_pool[source_offset+component];
        }
        copied["value_or_offset"]=target_offset;
    }
    target_material["shader_params"]=std::move(copied_parameters);
}

void copy_material_render_settings(const json& source_document,const json& source_material,
                                   json& target_document,json& target_material) {
    copy_shader_parameters(source_document,source_material,target_document,target_material);
    constexpr std::array keys{"shader_type","shader_sub_type","shadow_type","bool9","bool10","ignore_alpha","bool12"};
    for(const auto* key:keys){
        if(source_material.contains(key))target_material[key]=source_material[key];
        else target_material.erase(key);
    }
}

void remove_file_noexcept(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path,ignored);
}
}

namespace gbfr {
std::vector<std::filesystem::path> adjacent_mmat_variant_jsons(const std::filesystem::path& current) {
    std::vector<std::filesystem::path> result;
    if(current.empty())return result;
    for(unsigned variant=0;variant<=10;++variant){
        const auto candidate=current.parent_path()/(std::to_wstring(variant)+L".mmat.json");
        if(candidate!=current&&std::filesystem::is_regular_file(candidate))result.push_back(candidate);
    }
    return result;
}

std::size_t propagate_mmat_material_render_settings(const std::filesystem::path& current,
                                                    std::size_t material_index) {
    json source_document;
    {std::ifstream input(current);if(!input)throw std::runtime_error("无法读取当前 MMAT JSON");input>>source_document;}
    if(!source_document.contains("materials")||!source_document["materials"].is_array())
        throw std::runtime_error("当前 JSON 不是新 schema MMAT");
    if(material_index>=source_document["materials"].size()||
       !source_document["materials"][material_index].is_object())
        throw std::runtime_error("当前材质槽不存在");
    const auto& source_material=source_document["materials"][material_index];
    const auto source_shader_type=source_material.value("shader_type",-1);
    const auto source_shader_sub_type=source_material.value("shader_sub_type",-1);
    const auto targets=adjacent_mmat_variant_jsons(current);
    if(targets.empty())throw std::runtime_error("同一 vars 目录没有其他 0~10.mmat.json");

    struct StagedFile { std::filesystem::path target; std::filesystem::path temporary; std::filesystem::path backup; };
    std::vector<StagedFile> staged;
    staged.reserve(targets.size());
    const auto suffix=L".sync."+std::to_wstring(GetCurrentProcessId());
    try{
        for(const auto& target:targets){
            json target_document;
            {std::ifstream input(target);if(!input)throw std::runtime_error("无法读取 "+utf8(target.filename()));input>>target_document;}
            if(!target_document.contains("materials")||!target_document["materials"].is_array())
                throw std::runtime_error(utf8(target.filename())+" 不是新 schema MMAT JSON");
            auto& materials=target_document["materials"];
            if(material_index>=materials.size()||!materials[material_index].is_object())
                throw std::runtime_error(utf8(target.filename())+" 缺少材质槽 "+std::to_string(material_index));
            auto& target_material=materials[material_index];
            if(target_material.value("shader_type",-1)!=source_shader_type||
               target_material.value("shader_sub_type",-1)!=source_shader_sub_type)
                throw std::runtime_error(utf8(target.filename())+" 的同槽 Shader 类型不同，已停止覆盖");
            copy_material_render_settings(source_document,source_material,target_document,target_material);

            StagedFile file{target,target.wstring()+suffix+L".tmp",target.wstring()+suffix+L".bak"};
            remove_file_noexcept(file.temporary);remove_file_noexcept(file.backup);
            staged.push_back(std::move(file));
            const auto& staged_file=staged.back();
            {std::ofstream output(staged_file.temporary,std::ios::trunc);if(!output)throw std::runtime_error("无法创建 "+utf8(staged_file.temporary.filename()));output<<target_document.dump(2)<<'\n';if(!output)throw std::runtime_error("无法写入 "+utf8(staged_file.temporary.filename()));}
            load_mmat_json(staged_file.temporary);
        }
        for(const auto& file:staged){
            if(!CopyFileW(file.target.c_str(),file.backup.c_str(),FALSE))
                throw std::runtime_error("无法备份 "+utf8(file.target.filename())+"（Win32 "+std::to_string(GetLastError())+"）");
        }
    }catch(...){
        for(const auto& file:staged){remove_file_noexcept(file.temporary);remove_file_noexcept(file.backup);}
        throw;
    }

    std::size_t committed{};
    for(;committed<staged.size();++committed){
        const auto& file=staged[committed];
        if(!MoveFileExW(file.temporary.c_str(),file.target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            const auto write_error=GetLastError();bool rollback_ok=true;
            for(std::size_t index=0;index<committed;++index){
                const auto& previous=staged[index];
                if(!MoveFileExW(previous.backup.c_str(),previous.target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))rollback_ok=false;
            }
            for(const auto& cleanup:staged){remove_file_noexcept(cleanup.temporary);remove_file_noexcept(cleanup.backup);}
            throw std::runtime_error("替换 "+utf8(file.target.filename())+" 失败（Win32 "+std::to_string(write_error)+"）"+(rollback_ok?"，已回滚":"，且部分回滚失败"));
        }
    }
    for(const auto& file:staged)remove_file_noexcept(file.backup);
    return staged.size();
}
}
