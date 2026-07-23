#include "texture_gallery.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}

bool is_gallery_asset(const gbfr::WorkspaceAsset& asset) {
    return asset.kind==gbfr::AssetKind::texture||asset.kind==gbfr::AssetKind::ui_image||asset.kind==gbfr::AssetKind::new_texture||asset.kind==gbfr::AssetKind::granite_texture;
}

struct GalleryItem {
    std::size_t asset_index{};
    fs::path path;
    gbfr::AssetKind kind{};
    std::string subtype;
};

}

namespace gbfr::editor {

void TextureGallery::clear() {
    cache_.clear();
}

TextureGallery::CacheEntry* TextureGallery::find_or_load(PreviewRenderer& renderer,const fs::path& path) {
    auto found=cache_.find(path);
    if(found==cache_.end()){
        CacheEntry entry;
        entry.failed=!renderer.load_texture_thumbnail(path,entry.texture,256);
        found=cache_.emplace(path,std::move(entry)).first;
    }
    return &found->second;
}

void TextureGallery::draw(const Workspace& workspace,PreviewRenderer& renderer,std::optional<std::size_t> selected_asset,const SelectCallback& on_select) {
    ImGui::Begin("贴图库");

    const char* kinds[]={"全部贴图","普通 WTB","Granite / 新贴图","UI-image"};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("类型",&kind_filter_,kinds,static_cast<int>(std::size(kinds)));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat("缩略图",&thumbnail_size_,96.0f,240.0f,"%.0f px");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##texture_gallery_search","按文件名、类别或工作区路径过滤",search_.data(),search_.size());

    const auto search=lower_ascii(search_.data());
    std::vector<GalleryItem> items;
    const auto& assets=workspace.assets();
    for(std::size_t asset_index=0;asset_index<assets.size();++asset_index){
        const auto& asset=assets[asset_index];
        if(!is_gallery_asset(asset))continue;
        if(kind_filter_==1&&asset.kind!=AssetKind::texture)continue;
        if(kind_filter_==2&&asset.kind!=AssetKind::new_texture&&asset.kind!=AssetKind::granite_texture)continue;
        if(kind_filter_==3&&asset.kind!=AssetKind::ui_image)continue;
        std::vector<fs::path> paths;
        if(!asset.wtb_slots.empty())for(const auto& slot:asset.wtb_slots)paths.push_back(slot.second);
        else paths.push_back(asset.input);
        for(const auto& path:paths){
            if(path.extension()!=L".dds"||!fs::is_regular_file(path))continue;
            if(!search.empty()){
                const auto haystack=lower_ascii(utf8(path.filename())+" "+utf8(path.lexically_relative(workspace.root()))+" "+asset.subtype+" "+asset_kind_name(asset.kind));
                if(haystack.find(search)==std::string::npos)continue;
            }
            items.push_back({asset_index,path,asset.kind,asset.subtype});
        }
    }
    std::stable_sort(items.begin(),items.end(),[](const GalleryItem& left,const GalleryItem& right){return natural_less_case_insensitive(left.path.filename().wstring(),right.path.filename().wstring());});
    ImGui::TextDisabled("显示 %zu 张；单击选择，双击切换到单图预览",items.size());
    ImGui::Separator();

    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const float cell_width=thumbnail_size_+16.0f;
    const float cell_height=thumbnail_size_+ImGui::GetTextLineHeightWithSpacing()*2.0f+18.0f;
    const int columns=std::max(1,static_cast<int>((ImGui::GetContentRegionAvail().x+spacing)/(cell_width+spacing)));
    const int rows=items.empty()?0:static_cast<int>((items.size()+static_cast<std::size_t>(columns)-1)/static_cast<std::size_t>(columns));
    if(ImGui::BeginChild("texture_gallery_scroll",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar)){
        ImGuiListClipper clipper;clipper.Begin(rows,cell_height+spacing);
        while(clipper.Step()){
            for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
                for(int column=0;column<columns;++column){
                    const auto item_index=static_cast<std::size_t>(row*columns+column);
                    if(item_index>=items.size())break;
                    const auto& item=items[item_index];
                    ImGui::PushID(static_cast<int>(item_index));
                    const bool selected=selected_asset&&*selected_asset==item.asset_index;
                    if(selected)ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_Header));
                    ImGui::BeginChild("tile",ImVec2(cell_width,cell_height),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);
                    auto* cached=find_or_load(renderer,item.path);
                    bool activate=false;
                    const float image_start_y=ImGui::GetCursorPosY();
                    if(cached&&cached->texture.image){
                        const float width=static_cast<float>(cached->texture.width),height=static_cast<float>(cached->texture.height);
                        const float scale=std::min(thumbnail_size_/std::max(1.0f,width),thumbnail_size_/std::max(1.0f,height));
                        const ImVec2 image_size{width*scale,height*scale};
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(thumbnail_size_-image_size.x)*0.5f+4.0f);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY()+(thumbnail_size_-image_size.y)*0.5f);
                        const bool ui=item.kind==AssetKind::ui_image;
                        ImGui::Image(reinterpret_cast<ImTextureID>(cached->texture.image.Get()),image_size,ui?ImVec2(0,0):ImVec2(0,1),ui?ImVec2(1,1):ImVec2(1,0));
                        activate=ImGui::IsItemClicked();
                    }else{
                        ImGui::Dummy(ImVec2(thumbnail_size_,thumbnail_size_));
                        if(ImGui::IsItemHovered())ImGui::SetTooltip("DDS 无法解码");
                    }
                    ImGui::SetCursorPosY(image_start_y+thumbnail_size_);
                    const auto name=utf8(item.path.filename());
                    if(ImGui::Selectable((name+"##select").c_str(),selected,ImGuiSelectableFlags_AllowDoubleClick,ImVec2(0,ImGui::GetTextLineHeightWithSpacing())))activate=true;
                    ImGui::TextDisabled("%s",item.subtype.c_str());
                    if(ImGui::IsWindowHovered())ImGui::SetTooltip("%s",utf8(item.path.lexically_relative(workspace.root())).c_str());
                    const bool open_single=activate&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    ImGui::EndChild();
                    if(selected)ImGui::PopStyleColor();
                    if(activate){on_select(item.asset_index,item.path,item.kind==AssetKind::ui_image);if(open_single)ImGui::SetWindowFocus("Viewport");}
                    ImGui::PopID();
                    if(column+1<columns&&item_index+1<items.size())ImGui::SameLine();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

}
