#include <gbfr/render/preview_renderer.hpp>
#include "preview_gpu_types.hpp"

#include <DirectXMath.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace DirectX;
using gbfr::render_detail::BoneConstants;
using gbfr::render_detail::GpuVertex;

namespace {
int bone_name_id(const std::string& name) {
    if(name.size()<2||name.front()!='_')return -1;int value{};const auto result=std::from_chars(name.data()+1,name.data()+name.size(),value,16);return result.ec==std::errc{}&&result.ptr==name.data()+name.size()?value:-1;
}

XMFLOAT3 quaternion_to_euler(const gbfr::Vec4& q) {
    const float x=std::atan2(2.0f*(q.w*q.x+q.y*q.z),1.0f-2.0f*(q.x*q.x+q.y*q.y));
    const float y=std::asin(std::clamp(2.0f*(q.w*q.y-q.z*q.x),-1.0f,1.0f));
    const float z=std::atan2(2.0f*(q.w*q.z+q.x*q.y),1.0f-2.0f*(q.y*q.y+q.z*q.z));
    return {x,y,z};
}

XMVECTOR euler_to_quaternion(const XMFLOAT3& euler) {
    const float sx=std::sin(euler.x*.5f),cx=std::cos(euler.x*.5f),sy=std::sin(euler.y*.5f),cy=std::cos(euler.y*.5f),sz=std::sin(euler.z*.5f),cz=std::cos(euler.z*.5f);
    return XMVectorSet(sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz);
}

XMFLOAT4 normalize_quaternion(XMFLOAT4 value) {
    const float length=std::sqrt(value.x*value.x+value.y*value.y+value.z*value.z+value.w*value.w);
    if(length<1e-8f)return {0,0,0,1};
    const float inverse=1.0f/length;return {value.x*inverse,value.y*inverse,value.z*inverse,value.w*inverse};
}

XMFLOAT4 multiply_quaternions(const XMFLOAT4& left,const XMFLOAT4& right) {
    return {
        left.w*right.x+left.x*right.w+left.y*right.z-left.z*right.y,
        left.w*right.y-left.x*right.z+left.y*right.w+left.z*right.x,
        left.w*right.z+left.x*right.y-left.y*right.x+left.z*right.w,
        left.w*right.w-left.x*right.x-left.y*right.y-left.z*right.z
    };
}

XMFLOAT4 quaternion_power(XMFLOAT4 value,float rate) {
    value=normalize_quaternion(value);
    if(value.w<0.0f)value={-value.x,-value.y,-value.z,-value.w};
    const float half_angle=std::acos(std::clamp(value.w,-1.0f,1.0f));
    const float sine=std::sin(half_angle);
    if(std::abs(sine)<1e-7f)return {0,0,0,1};
    const float scaled=half_angle*rate;
    const float vector_scale=std::sin(scaled)/sine;
    return normalize_quaternion({value.x*vector_scale,value.y*vector_scale,value.z*vector_scale,std::cos(scaled)});
}

XMFLOAT4 to_float4(const gbfr::Vec4& value) { return {value.x,value.y,value.z,value.w}; }

XMFLOAT4 euler_to_float4(const XMFLOAT3& euler) {
    XMFLOAT4 result{};XMStoreFloat4(&result,euler_to_quaternion(euler));return normalize_quaternion(result);
}

const gbfr::SopProperty* floating_property(const gbfr::SopOperation& operation,std::uint32_t hash) {
    const auto* property=operation.find(hash);
    return property&&property->type==gbfr::SopPropertyType::floating&&std::isfinite(property->floating())?property:nullptr;
}

bool evaluate_core_sop(const gbfr::SopOperation& operation,const XMFLOAT4& source,XMFLOAT4& output) {
    if(operation.type_hash!=gbfr::sop_swing_twist_operation&&operation.type_hash!=gbfr::sop_twist_operation)return false;
    const auto* axis_x=floating_property(operation,gbfr::sop_axis_x_property);
    const auto* axis_y=floating_property(operation,gbfr::sop_axis_y_property);
    const auto* axis_z=floating_property(operation,gbfr::sop_axis_z_property);
    const auto* twist_rate=floating_property(operation,gbfr::sop_twist_rate_property);
    if(!axis_x||!axis_y||!axis_z||!twist_rate)return false;
    XMFLOAT3 axis{axis_x->floating(),axis_y->floating(),axis_z->floating()};
    const float axis_length=std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
    if(axis_length<1e-8f)return false;
    axis.x/=axis_length;axis.y/=axis_length;axis.z/=axis_length;

    const auto normalized_source=normalize_quaternion(source);
    const float projection=normalized_source.x*axis.x+normalized_source.y*axis.y+normalized_source.z*axis.z;
    const auto twist=normalize_quaternion({axis.x*projection,axis.y*projection,axis.z*projection,normalized_source.w});
    XMFLOAT4 base{};
    if(operation.type_hash==gbfr::sop_swing_twist_operation){
        const auto* swing_rate=floating_property(operation,gbfr::sop_swing_rate_property);
        if(!swing_rate)return false;
        const auto inverse_twist=XMFLOAT4{-twist.x,-twist.y,-twist.z,twist.w};
        const auto swing=normalize_quaternion(multiply_quaternions(normalized_source,inverse_twist));
        base=multiply_quaternions(quaternion_power(swing,swing_rate->floating()),quaternion_power(twist,twist_rate->floating()));
    } else {
        base=quaternion_power(twist,twist_rate->floating());
    }

    XMFLOAT3 offset{};
    if(const auto* property=floating_property(operation,gbfr::sop_offset_x_property))offset.x=property->floating();
    if(const auto* property=floating_property(operation,gbfr::sop_offset_y_property))offset.y=property->floating();
    if(const auto* property=floating_property(operation,gbfr::sop_offset_z_property))offset.z=property->floating();
    output=normalize_quaternion(multiply_quaternions(base,euler_to_float4(offset)));
    return true;
}

float quaternion_error(const XMFLOAT4& left,const XMFLOAT4& right) {
    const auto a=normalize_quaternion(left),b=normalize_quaternion(right);
    const float minus=std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)+(a.w-b.w)*(a.w-b.w));
    const float plus=std::sqrt((a.x+b.x)*(a.x+b.x)+(a.y+b.y)*(a.y+b.y)+(a.z+b.z)*(a.z+b.z)+(a.w+b.w)*(a.w+b.w));
    return std::min(minus,plus);
}

XMMATRIX local_matrix(const gbfr::Vec3& position,const gbfr::Vec3& scale,FXMVECTOR rotation) {
    return XMMatrixScaling(scale.x,scale.y,scale.z)*XMMatrixRotationQuaternion(rotation)*XMMatrixTranslation(position.x,position.y,position.z);
}
}

namespace gbfr {
bool PreviewRenderer::apply_animation(const AnimationClip* clip,float frame) {
    if(!index_count_||skeleton_.bones.empty()||!vertices_||!bones_)return false;
    const auto bone_count=skeleton_.bones.size();
    std::vector<Vec3> positions(bone_count),scales(bone_count);
    std::vector<XMFLOAT3> rotations(bone_count);
    std::unordered_map<int,std::size_t> bone_indices;bone_indices.reserve(bone_count);
    std::size_t object_root{};
    for(std::size_t i=0;i<bone_count;++i){const auto& bone=skeleton_.bones[i];positions[i]=bone.position;scales[i]=bone.scale;rotations[i]=quaternion_to_euler(bone.rotation);const int id=bone_name_id(bone.name);if(id>=0)bone_indices.emplace(id,i);if(bone.parent==0xffff&&bone.name=="_900")object_root=i;}
    if(clip){
        for(const auto& track:clip->tracks){
            std::size_t index{};
            if(track.bone_id==-1)index=object_root;
            else {const auto found=bone_indices.find(track.bone_id);if(found==bone_indices.end())continue;index=found->second;}
            const float value=track.sample(frame);
            switch(track.property){
            case 0:positions[index].x=value;break;case 1:positions[index].y=value;break;case 2:positions[index].z=value;break;
            case 3:rotations[index].x=value;break;case 4:rotations[index].y=value;break;case 5:rotations[index].z=value;break;
            case 7:scales[index].x=value;break;case 8:scales[index].y=value;break;case 9:scales[index].z=value;break;
            default:break;
            }
        }
    }

    std::vector<XMFLOAT4> local_rotations(bone_count);
    for(std::size_t i=0;i<bone_count;++i)local_rotations[i]=clip?euler_to_float4(rotations[i]):to_float4(skeleton_.bones[i].rotation);
    applied_sop_operation_count_=0;
    if(clip){
        for(const auto& operation:sop_.operations){
            const auto source=bone_indices.find(static_cast<int>(operation.source_bone));
            const auto target=bone_indices.find(static_cast<int>(operation.target_bone));
            if(source==bone_indices.end()||target==bone_indices.end())continue;
            XMFLOAT4 rest_output{};
            if(!evaluate_core_sop(operation,to_float4(skeleton_.bones[source->second].rotation),rest_output)||
               quaternion_error(rest_output,to_float4(skeleton_.bones[target->second].rotation))>1e-4f)continue;
            XMFLOAT4 animated_output{};
            if(!evaluate_core_sop(operation,local_rotations[source->second],animated_output))continue;
            local_rotations[target->second]=animated_output;++applied_sop_operation_count_;
        }
    }

    std::vector<XMMATRIX> rest_world(bone_count),posed_world(bone_count);
    BoneConstants gpu_bones{};
    const auto identity=XMMatrixIdentity();
    for(auto& matrix:gpu_bones.skin)XMStoreFloat4x4(&matrix,XMMatrixTranspose(identity));
    animated_bone_positions_.resize(bone_count);
    pose_hash_=1469598103934665603ull;
    for(std::size_t i=0;i<bone_count;++i){
        const auto& bone=skeleton_.bones[i];
        const auto rest_local=local_matrix(bone.position,bone.scale,XMVectorSet(bone.rotation.x,bone.rotation.y,bone.rotation.z,bone.rotation.w));
        const auto posed_local=clip?local_matrix(positions[i],scales[i],XMLoadFloat4(&local_rotations[i])):rest_local;
        if(bone.parent==0xffff){rest_world[i]=rest_local;posed_world[i]=posed_local;}
        else {if(bone.parent>=i)return false;rest_world[i]=rest_local*rest_world[bone.parent];posed_world[i]=posed_local*posed_world[bone.parent];}
        XMVECTOR determinant{};const auto inverse=XMMatrixInverse(&determinant,rest_world[i]);
        const auto skin=std::abs(XMVectorGetX(determinant))<1e-8f?XMMatrixIdentity():inverse*posed_world[i];
        XMStoreFloat4x4(&gpu_bones.skin[i],XMMatrixTranspose(skin));
        const auto* bytes=reinterpret_cast<const std::uint8_t*>(&gpu_bones.skin[i]);
        for(std::size_t byte=0;byte<sizeof(XMFLOAT4X4);++byte){pose_hash_^=bytes[byte];pose_hash_*=1099511628211ull;}
        XMFLOAT3 world{};XMStoreFloat3(&world,posed_world[i].r[3]);animated_bone_positions_[i]={world.x,world.y,world.z};
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context_->Map(bones_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,&gpu_bones,sizeof(gpu_bones));context_->Unmap(bones_.Get(),0);

    std::vector<GpuVertex> lines;lines.reserve(line_vertex_count_);
    for(std::size_t i=0;i<bone_count;++i){const auto parent=skeleton_.bones[i].parent;if(!visible_bones_[i]||parent==0xffff||parent>=bone_count)continue;const auto& p=animated_bone_positions_[parent];const auto& b=animated_bone_positions_[i];lines.push_back({{p.x,p.y,p.z},{0,1,0},{0,0}});lines.push_back({{b.x,b.y,b.z},{0,1,0},{0,0}});}
    if(lines_&&!lines.empty())context_->UpdateSubresource(lines_.Get(),0,nullptr,lines.data(),0,0);
    std::vector<GpuVertex> points;points.reserve(bone_point_vertex_count_);const float marker=bone_marker_size_;
    for(std::size_t i=0;i<animated_bone_positions_.size();++i){if(!visible_bones_[i])continue;const auto& p=animated_bone_positions_[i];points.push_back({{p.x-marker,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x+marker,p.y,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y-marker,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y+marker,p.z},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z-marker},{0,1,0},{0,0}});points.push_back({{p.x,p.y,p.z+marker},{0,1,0},{0,0}});}
    if(bone_points_&&!points.empty())context_->UpdateSubresource(bone_points_.Get(),0,nullptr,points.data(),0,0);
    return true;
}
}
