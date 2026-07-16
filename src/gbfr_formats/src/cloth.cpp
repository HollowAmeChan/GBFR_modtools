#include <gbfr/formats/cloth.hpp>
#include <pugixml.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
gbfr::Vec4 vec4(const char* text) { gbfr::Vec4 value{}; std::istringstream input(text ? text : ""); if(!(input>>value.x>>value.y>>value.z>>value.w)) throw std::runtime_error("Invalid cloth vec4"); return value; }
int integer(const pugi::xml_node& parent,const char* name,int fallback=0) { const auto node=parent.child(name); return node?node.text().as_int(fallback):fallback; }
float number(const pugi::xml_node& parent,const char* name,float fallback=0) { const auto node=parent.child(name); return node?node.text().as_float(fallback):fallback; }
std::string format_vec4(const gbfr::Vec4& v) { std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<v.x<<' '<<v.y<<' '<<v.z<<' '<<v.w;return out.str(); }
std::string format_float(float v) { std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<v;return out.str(); }
pugi::xml_document read_xml(const std::filesystem::path& path) { pugi::xml_document doc; const auto result=doc.load_file(path.c_str(),pugi::parse_default,pugi::encoding_utf8);if(!result)throw std::runtime_error(std::string("Cloth XML parse error: ")+result.description());return doc; }
}

namespace gbfr {
ClhAsset load_clh(const std::filesystem::path& path) {
    auto doc=read_xml(path); const auto root=doc.child("CLOTH_AT"); if(!root)throw std::runtime_error("CLH root CLOTH_AT missing"); ClhAsset result;
    for(const auto node:root.child("ClothCollision_LIST").children("ClothCollision")) { ClothCollision c;c.id=integer(node,"id_");c.p1=integer(node,"p1");c.p2=integer(node,"p2");c.weight=number(node,"weight");c.radius=number(node,"radius");c.offset1=vec4(node.child("offset1").text().as_string());c.offset2=vec4(node.child("offset2").text().as_string());c.capsule=integer(node,"capsule",-1);c.battle_disabled=integer(node,"notUseInBattle")!=0;c.idle_disabled=integer(node,"notUseInIdle")!=0;result.collisions.push_back(c); }
    return result;
}

ClpAsset load_clp(const std::filesystem::path& path) {
    auto doc=read_xml(path);const auto root=doc.child("CLOTH");if(!root)throw std::runtime_error("CLP root CLOTH missing");ClpAsset result;
    for(const auto node:root.child("CLOTH_WK_LIST").children("CLOTH_WK")){ClothNode c;c.bone=integer(node,"no");c.up=integer(node,"noUp",4095);c.down=integer(node,"noDown",4095);c.side=integer(node,"noSide",4095);c.poly=integer(node,"noPoly",4095);c.fix=integer(node,"noFix",4095);c.rotation_limit=number(node,"rotLimit");c.friction=number(node,"friction");c.offset=vec4(node.child("offset").text().as_string());c.weight=number(node,"weight_");c.thickness=number(node,"thick_");c.wind_area=number(node,"windForceArea_");result.nodes.push_back(c);}return result;
}

void save_clh_collision(const std::filesystem::path& path,const ClothCollision& collision) {
    auto doc=read_xml(path);pugi::xml_node target;for(auto node:doc.child("CLOTH_AT").child("ClothCollision_LIST").children("ClothCollision"))if(integer(node,"id_")==collision.id){target=node;break;}if(!target)throw std::runtime_error("CLH collision id not found");
    target.child("radius").text().set(format_float(collision.radius).c_str());target.child("weight").text().set(format_float(collision.weight).c_str());target.child("offset1").text().set(format_vec4(collision.offset1).c_str());target.child("offset2").text().set(format_vec4(collision.offset2).c_str());
    const auto temporary=path.wstring()+L".tmp";if(!doc.save_file(temporary.c_str(),"  ",pugi::format_default,pugi::encoding_utf8))throw std::runtime_error("Cannot save temporary CLH XML");std::error_code ec;std::filesystem::remove(path,ec);ec.clear();std::filesystem::rename(temporary,path,ec);if(ec)throw std::runtime_error("Cannot replace CLH XML: "+ec.message());
}
}
