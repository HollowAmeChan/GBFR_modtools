#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::string utf8(const fs::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}

std::vector<unsigned char> read_file(const fs::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)return {};
    const auto size=input.tellg();
    if(size<=0)return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),size);
    return input?bytes:std::vector<unsigned char>{};
}

json type_json(ID3D11ShaderReflectionType* type) {
    D3D11_SHADER_TYPE_DESC desc{};
    if(!type||FAILED(type->GetDesc(&desc)))return nullptr;
    json result={{"class",desc.Class},{"type",desc.Type},{"rows",desc.Rows},{"columns",desc.Columns},
                 {"elements",desc.Elements},{"members",desc.Members},{"offset",desc.Offset}};
    if(desc.Name)result["name"]=desc.Name;
    if(desc.Members){
        result["member_list"]=json::array();
        for(UINT index=0;index<desc.Members;++index){
            auto member=type->GetMemberTypeByIndex(index);
            auto item=type_json(member);
            if(const char* name=type->GetMemberTypeName(index))item["member_name"]=name;
            result["member_list"].push_back(std::move(item));
        }
    }
    return result;
}

json reflect_shader(const fs::path& path,const fs::path& root) {
    json result={{"file",utf8(path.lexically_relative(root))},{"size",fs::file_size(path)}};
    const auto bytes=read_file(path);
    if(bytes.empty()){result["error"]="empty or unreadable";return result;}
    ID3D11ShaderReflection* reflection{};
    const auto status=D3DReflect(bytes.data(),bytes.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&reflection));
    if(FAILED(status)||!reflection){result["error"]="D3DReflect failed";result["hresult"]=status;return result;}
    D3D11_SHADER_DESC desc{};
    if(FAILED(reflection->GetDesc(&desc))){reflection->Release();result["error"]="GetDesc failed";return result;}
    result["creator"]=desc.Creator?desc.Creator:"";
    result["version"]=desc.Version;
    result["instruction_count"]=desc.InstructionCount;
    result["constant_buffers"]=json::array();
    for(UINT buffer_index=0;buffer_index<desc.ConstantBuffers;++buffer_index){
        auto buffer=reflection->GetConstantBufferByIndex(buffer_index);D3D11_SHADER_BUFFER_DESC buffer_desc{};
        if(!buffer||FAILED(buffer->GetDesc(&buffer_desc)))continue;
        json buffer_json={{"name",buffer_desc.Name?buffer_desc.Name:""},{"type",buffer_desc.Type},
                          {"size",buffer_desc.Size},{"flags",buffer_desc.uFlags},{"variables",json::array()}};
        for(UINT variable_index=0;variable_index<buffer_desc.Variables;++variable_index){
            auto variable=buffer->GetVariableByIndex(variable_index);D3D11_SHADER_VARIABLE_DESC variable_desc{};
            if(!variable||FAILED(variable->GetDesc(&variable_desc)))continue;
            json variable_json={{"name",variable_desc.Name?variable_desc.Name:""},{"offset",variable_desc.StartOffset},
                                {"size",variable_desc.Size},{"flags",variable_desc.uFlags}};
            variable_json["type"]=type_json(variable->GetType());
            buffer_json["variables"].push_back(std::move(variable_json));
        }
        result["constant_buffers"].push_back(std::move(buffer_json));
    }
    result["resources"]=json::array();
    for(UINT index=0;index<desc.BoundResources;++index){
        D3D11_SHADER_INPUT_BIND_DESC bind{};if(FAILED(reflection->GetResourceBindingDesc(index,&bind)))continue;
        result["resources"].push_back({{"name",bind.Name?bind.Name:""},{"type",bind.Type},{"bind_point",bind.BindPoint},
            {"bind_count",bind.BindCount},{"flags",bind.uFlags},{"return_type",bind.ReturnType},
            {"dimension",bind.Dimension},{"samples",bind.NumSamples}});
    }
    reflection->Release();
    return result;
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=3){std::wcerr<<L"Usage: gbfr_shader_reflect <data/shader> <output.jsonl>\n";return 2;}
    const fs::path root=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
    if(!fs::is_directory(root)){std::wcerr<<L"Shader root not found: "<<root<<L'\n';return 3;}
    std::vector<fs::path> files;
    for(const auto& entry:fs::recursive_directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        auto extension=entry.path().extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
        if(extension==L".pso"||extension==L".vso"||extension==L".cso"||extension==L".hso"||extension==L".dso")files.push_back(entry.path());
    }
    std::sort(files.begin(),files.end());
    fs::create_directories(output.parent_path());
    std::ofstream stream(output,std::ios::trunc);
    if(!stream){std::wcerr<<L"Cannot write: "<<output<<L'\n';return 4;}
    std::size_t failures{};
    for(const auto& file:files){const auto item=reflect_shader(file,root);if(item.contains("error"))++failures;stream<<item.dump()<<'\n';}
    std::cout<<"reflected="<<files.size()<<" failures="<<failures<<" output="<<utf8(output)<<'\n';
    return failures?5:0;
}
