#include <gbfr/formats/model.hpp>
#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
class BinaryView {
public:
    explicit BinaryView(const fs::path& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open binary file");
        const auto size = input.tellg();
        if (size < 0) throw std::runtime_error("Cannot get binary file size");
        bytes_.resize(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes_.data()), size);
    }
    template <class T> T read(std::size_t offset, const char* label) const {
        if (offset > bytes_.size() || sizeof(T) > bytes_.size() - offset)
            throw std::runtime_error(std::string(label) + " outside file at offset " + std::to_string(offset));
        T value{}; std::memcpy(&value, bytes_.data() + offset, sizeof(T)); return value;
    }
    std::string string(std::size_t offset) const {
        const auto length = read<std::uint32_t>(offset, "string length");
        if (offset + 4 > bytes_.size() || length > bytes_.size() - offset - 4) throw std::runtime_error("FlatBuffer string outside file");
        return {reinterpret_cast<const char*>(bytes_.data() + offset + 4), length};
    }
    std::size_t size() const noexcept { return bytes_.size(); }
private: std::vector<std::byte> bytes_;
};

class FlatView {
public:
    explicit FlatView(const fs::path& path) : data_(path) {}
    std::size_t root() const { return data_.read<std::uint32_t>(0, "root"); }
    std::size_t field(std::size_t table, std::size_t index, bool required = false) const {
        const auto distance = data_.read<std::int32_t>(table, "vtable distance");
        const auto signed_vtable = static_cast<std::int64_t>(table) - distance;
        if (!distance || signed_vtable < 0) throw std::runtime_error("Invalid FlatBuffer vtable");
        const auto vtable = static_cast<std::size_t>(signed_vtable);
        const auto length = data_.read<std::uint16_t>(vtable, "vtable length");
        const auto entry = vtable + 4 + index * 2;
        if (entry + 2 > vtable + length) { if (required) throw std::runtime_error("Required FlatBuffer field missing"); return 0; }
        const auto relative = data_.read<std::uint16_t>(entry, "field offset");
        if (!relative) { if (required) throw std::runtime_error("Required FlatBuffer field empty"); return 0; }
        return table + relative;
    }
    std::size_t indirect(std::size_t offset) const { return offset + data_.read<std::uint32_t>(offset, "indirect offset"); }
    std::size_t vector(std::size_t offset) const { return indirect(offset); }
    std::uint32_t vector_size(std::size_t offset) const { return data_.read<std::uint32_t>(offset, "vector size"); }
    std::size_t table_in_vector(std::size_t vector, std::size_t index) const { return indirect(vector + 4 + index * 4); }
    template <class T> T read(std::size_t offset, const char* label) const { return data_.read<T>(offset, label); }
    std::string string_field(std::size_t offset) const { return data_.string(indirect(offset)); }
private: BinaryView data_;
};

float half_to_float(std::uint16_t value) {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000) << 16;
    std::uint32_t exponent = (value >> 10) & 0x1f, mantissa = value & 0x3ff, result{};
    if (!exponent) {
        if (!mantissa) result = sign;
        else { exponent = 113; while (!(mantissa & 0x400)) { mantissa <<= 1; --exponent; } result = sign | (exponent << 23) | ((mantissa & 0x3ff) << 13); }
    } else if (exponent == 31) result = sign | 0x7f800000 | (mantissa << 13);
    else result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    return std::bit_cast<float>(result);
}

gbfr::Vec3 rotate(gbfr::Vec4 q, gbfr::Vec3 v) {
    const gbfr::Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
gbfr::Vec4 multiply(gbfr::Vec4 a, gbfr::Vec4 b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
}

namespace gbfr {
SkeletonAsset load_skeleton(const fs::path& path) {
    FlatView view(path); SkeletonAsset result; const auto root = view.root();
    if (const auto value=view.field(root,0)) result.magic=view.read<std::uint32_t>(value,"skeleton magic");
    const auto body=view.vector(view.field(root,1,true)); const auto count=view.vector_size(body);
    if (!count || count>65535) throw std::runtime_error("Invalid skeleton bone count");
    result.bones.reserve(count);
    for (std::uint32_t i=0;i<count;++i) {
        const auto table=view.table_in_vector(body,i); Bone bone;
        bone.parent=0; // ParentId defaults to bone 0 when the FlatBuffer field is omitted.
        if (const auto value=view.field(table,1)) bone.parent=view.read<std::uint16_t>(value,"bone parent");
        bone.name=view.string_field(view.field(table,2,true));
        if (const auto p=view.field(table,3)) bone.position={view.read<float>(p,"position x"),view.read<float>(p+4,"position y"),view.read<float>(p+8,"position z")};
        if (const auto q=view.field(table,4)) bone.rotation={view.read<float>(q,"quat x"),view.read<float>(q+4,"quat y"),view.read<float>(q+8,"quat z"),view.read<float>(q+12,"quat w")};
        if (const auto s=view.field(table,5)) bone.scale={view.read<float>(s,"scale x"),view.read<float>(s+4,"scale y"),view.read<float>(s+8,"scale z")};
        result.bones.push_back(std::move(bone));
    }
    std::vector<Vec4> rotations(count);
    for (std::size_t i=0;i<count;++i) {
        auto& bone=result.bones[i];
        if (bone.parent==0xffff) { bone.world_position=bone.position; rotations[i]=bone.rotation; }
        else { if (bone.parent>=i) throw std::runtime_error("Skeleton parent must precede child"); const auto& parent=result.bones[bone.parent]; const auto p=rotate(rotations[bone.parent],bone.position); bone.world_position={parent.world_position.x+p.x,parent.world_position.y+p.y,parent.world_position.z+p.z}; rotations[i]=multiply(rotations[bone.parent],bone.rotation); }
    }
    return result;
}

ModelInfoAsset load_minfo(const fs::path& path) {
    FlatView view(path); ModelInfoAsset result; const auto root=view.root();
    if (const auto value=view.field(root,0)) result.magic=view.read<std::uint32_t>(value,"minfo magic");
    const auto read_lods=[&](std::size_t root_field,std::vector<ModelLodAsset>& output,bool required) {
        const auto field=view.field(root,root_field,required);if(!field)return;
        const auto lods=view.vector(field);output.reserve(view.vector_size(lods));
        for(std::uint32_t lod_index=0;lod_index<view.vector_size(lods);++lod_index) {
            const auto table=view.table_in_vector(lods,lod_index);ModelLodAsset lod;
            if(const auto value=view.field(table,2))lod.vertex_count=view.read<std::uint32_t>(value,"vertex count");
            if(const auto value=view.field(table,3))lod.index_count=view.read<std::uint32_t>(value,"index count");
            if(const auto value=view.field(table,4))lod.buffer_types=view.read<std::uint8_t>(value,"buffer types");
            const auto buffers=view.vector(view.field(table,0,true));
            for(std::uint32_t i=0;i<view.vector_size(buffers);++i){const auto p=buffers+4+i*16;lod.buffers.push_back({view.read<std::uint64_t>(p,"buffer offset"),view.read<std::uint64_t>(p+8,"buffer size")});}
            if(const auto chunks_field=view.field(table,1)){const auto chunks=view.vector(chunks_field);for(std::uint32_t i=0;i<view.vector_size(chunks);++i){const auto p=chunks+4+i*12;lod.chunks.push_back({view.read<std::uint32_t>(p,"chunk offset"),view.read<std::uint32_t>(p+4,"chunk count"),view.read<std::uint8_t>(p+8,"submesh id"),view.read<std::uint8_t>(p+9,"material id")});}}
            if(!lod.vertex_count||lod.index_count%3||lod.buffers.empty())throw std::runtime_error("Invalid model LOD counts or buffers");
            output.push_back(std::move(lod));
        }
    };
    read_lods(1,result.lods,true);
    read_lods(2,result.shadow_lods,false);
    if(result.lods.empty())throw std::runtime_error("minfo has no LOD0");
    if (const auto field=view.field(root,4)) { const auto vector=view.vector(field); for (std::uint32_t i=0;i<view.vector_size(vector);++i) { const auto table=view.table_in_vector(vector,i); const auto name=view.field(table,0); result.submesh_names.push_back(name?view.string_field(name):std::string{}); } }
    if (const auto field=view.field(root,5)) { const auto vector=view.vector(field); for (std::uint32_t i=0;i<view.vector_size(vector);++i) { const auto table=view.table_in_vector(vector,i); const auto hash=view.field(table,0); result.materials.push_back(hash?view.read<std::uint32_t>(hash,"material hash"):0); } }
    if (const auto field=view.field(root,6)) { const auto vector=view.vector(field); for (std::uint32_t i=0;i<view.vector_size(vector);++i) result.bones_to_weight_indices.push_back(view.read<std::uint16_t>(vector+4+i*2,"bone map")); }
    // Keep the original LOD0 members as source-compatible aliases for callers that
    // only need the primary preview LOD.
    const auto& lod0=result.lods.front();
    result.vertex_count=lod0.vertex_count;result.index_count=lod0.index_count;result.buffer_types=lod0.buffer_types;
    result.buffers=lod0.buffers;result.chunks=lod0.chunks;
    return result;
}

MeshAsset load_mmesh(const fs::path& path,const ModelInfoAsset& info,std::size_t lod_index,bool shadow_lod) {
    BinaryView view(path);
    const auto& lods=shadow_lod?info.shadow_lods:info.lods;
    if(lod_index>=lods.size())throw std::runtime_error("Requested model LOD is not present in minfo");
    const auto& lod=lods[lod_index];
    auto buffer=[&](std::size_t i,std::uint64_t bytes)->const MeshBufferLocator& {if(i>=lod.buffers.size())throw std::runtime_error("Missing mmesh buffer locator");const auto& b=lod.buffers[i];if(b.size<bytes||b.offset>view.size()||bytes>view.size()-static_cast<std::size_t>(b.offset))throw std::runtime_error("mmesh buffer outside file");return b;};
    MeshAsset result;result.buffer_types=lod.buffer_types;result.vertices.resize(lod.vertex_count);const auto& main=buffer(0,static_cast<std::uint64_t>(lod.vertex_count)*32);
    for(std::uint32_t i=0;i<lod.vertex_count;++i){const auto p=static_cast<std::size_t>(main.offset)+i*32;auto& v=result.vertices[i];v.position={view.read<float>(p,"vertex x"),view.read<float>(p+4,"vertex y"),view.read<float>(p+8,"vertex z")};v.normal={half_to_float(view.read<std::uint16_t>(p+12,"normal x")),half_to_float(view.read<std::uint16_t>(p+14,"normal y")),half_to_float(view.read<std::uint16_t>(p+16,"normal z"))};v.tangent={half_to_float(view.read<std::uint16_t>(p+20,"tangent x")),half_to_float(view.read<std::uint16_t>(p+24,"tangent z")),half_to_float(view.read<std::uint16_t>(p+22,"tangent y"))};v.uv={half_to_float(view.read<std::uint16_t>(p+28,"uv x")),half_to_float(view.read<std::uint16_t>(p+30,"uv y"))};}
    std::size_t buffer_index=1;
    const auto read_joints=[&](std::size_t first){const auto& b=buffer(buffer_index++,static_cast<std::uint64_t>(lod.vertex_count)*8);for(std::uint32_t i=0;i<lod.vertex_count;++i)for(std::size_t j=0;j<4;++j){const auto mapped=view.read<std::uint16_t>(static_cast<std::size_t>(b.offset)+i*8+j*2,"joint index");if(mapped>=info.bones_to_weight_indices.size())throw std::runtime_error("Joint index outside minfo bone map");result.vertices[i].joints[first+j]=info.bones_to_weight_indices[mapped];}};
    if(lod.buffer_types&2)read_joints(0);
    if(lod.buffer_types&4)read_joints(4);
    const auto read_weights=[&](std::size_t first){const auto& b=buffer(buffer_index++,static_cast<std::uint64_t>(lod.vertex_count)*8);for(std::uint32_t i=0;i<lod.vertex_count;++i)for(std::size_t j=0;j<4;++j)result.vertices[i].weights[first+j]=view.read<std::uint16_t>(static_cast<std::size_t>(b.offset)+i*8+j*2,"joint weight")/65535.0f;};
    if(lod.buffer_types&8)read_weights(0);
    if(lod.buffer_types&16)read_weights(4);
    result.influence_count=(lod.buffer_types&16)?8:((lod.buffer_types&8)?4:0);
    if(lod.buffer_types&32){const auto& b=buffer(buffer_index++,static_cast<std::uint64_t>(lod.vertex_count)*4);for(std::uint32_t i=0;i<lod.vertex_count;++i){const auto p=static_cast<std::size_t>(b.offset)+i*4;result.vertices[i].color={view.read<std::uint8_t>(p,"color r")/255.0f,view.read<std::uint8_t>(p+1,"color g")/255.0f,view.read<std::uint8_t>(p+2,"color b")/255.0f,view.read<std::uint8_t>(p+3,"color a")/255.0f};}result.has_color=true;}
    if(lod.buffer_types&64){const auto& b=buffer(buffer_index++,static_cast<std::uint64_t>(lod.vertex_count)*4);for(std::uint32_t i=0;i<lod.vertex_count;++i){const auto p=static_cast<std::size_t>(b.offset)+i*4;result.vertices[i].uv1={half_to_float(view.read<std::uint16_t>(p,"uv1 x")),half_to_float(view.read<std::uint16_t>(p+2,"uv1 y"))};}result.has_uv1=true;}
    const auto& ib=buffer(lod.buffers.size()-1,static_cast<std::uint64_t>(lod.index_count)*4);result.indices.resize(lod.index_count);
    for(std::uint32_t i=0;i<lod.index_count;i+=3){const auto p=static_cast<std::size_t>(ib.offset)+i*4;result.indices[i]=view.read<std::uint32_t>(p+8,"triangle c");result.indices[i+1]=view.read<std::uint32_t>(p+4,"triangle b");result.indices[i+2]=view.read<std::uint32_t>(p,"triangle a");if(result.indices[i]>=lod.vertex_count||result.indices[i+1]>=lod.vertex_count||result.indices[i+2]>=lod.vertex_count)throw std::runtime_error("Triangle index outside vertex buffer");}
    result.chunks=lod.chunks;return result;
}
}
