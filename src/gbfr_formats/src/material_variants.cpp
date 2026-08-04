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

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
using json = nlohmann::json;

std::string utf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
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

std::size_t propagate_mmat_json(const std::filesystem::path& current,
                                                    std::size_t material_index) {
    json source_document;
    {std::ifstream input(current);if(!input)throw std::runtime_error("无法读取当前 MMAT JSON");input>>source_document;}
    if(!source_document.contains("materials")||!source_document["materials"].is_array())
        throw std::runtime_error("当前 JSON 不是新 schema MMAT");
    if(material_index>=source_document["materials"].size()||
       !source_document["materials"][material_index].is_object())
        throw std::runtime_error("当前材质槽不存在");
    const auto targets=adjacent_mmat_variant_jsons(current);
    if(targets.empty())throw std::runtime_error("同一 vars 目录没有其他 0~10.mmat.json");

    struct StagedFile { std::filesystem::path target; std::filesystem::path temporary; std::filesystem::path backup; };
    std::vector<StagedFile> staged;
    staged.reserve(targets.size());
    const auto suffix=L".sync."+std::to_wstring(GetCurrentProcessId());
    try{
        for(const auto& target:targets){
            StagedFile file{target,target.wstring()+suffix+L".tmp",target.wstring()+suffix+L".bak"};
            remove_file_noexcept(file.temporary);remove_file_noexcept(file.backup);
            staged.push_back(std::move(file));
            const auto& staged_file=staged.back();
            {
                std::ofstream output(staged_file.temporary,std::ios::trunc);
                if(!output)throw std::runtime_error("无法创建 "+utf8(staged_file.temporary.filename()));
                output<<source_document.dump(2)<<'\n';
                if(!output)throw std::runtime_error("无法写入 "+utf8(staged_file.temporary.filename()));
            }
            load_mmat_json(staged_file.temporary);
        }
        for(const auto& file:staged){
            if(!CopyFileW(file.target.c_str(),file.backup.c_str(),FALSE))
                throw std::runtime_error("无法备份 "+utf8(file.target.filename()));
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
            throw std::runtime_error("替换 "+utf8(file.target.filename())+" 失败 (Win32 "+std::to_string(write_error)+")"+(rollback_ok?"，已回滚":"，且部分回滚失败"));
        }
    }
    for(const auto& file:staged)remove_file_noexcept(file.backup);
    return staged.size();
}
}
