#include "core_api.hpp"
#include "layout_geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mebel::model {
namespace {
using Point=layout_geometry::Point;
using Polygon=layout_geometry::Polygon;
using Point3=std::array<double,3>;
using Box=std::array<Point3,2>;
constexpr double radians=std::numbers::pi/180.;
struct Sum {
    double hi=0,lo=0;
    void add(double value){const auto next=hi+value;lo+=std::abs(hi)>=std::abs(value)?(hi-next)+value:(value-next)+hi;hi=next;}
    double value()const{return std::isfinite(lo)?hi+lo:hi;}
};
const Json& object_member(const Json& data,const char* key) {
    static const Json empty=Json::object();const auto found=data.find(key);const auto& value=found==data.end()?empty:*found;
    if(!value.is_object())throw std::invalid_argument(std::string(key)+": wymagany obiekt JSON.");
    return value;
}
const Json& array_member(const Json& data,const char* key) {
    static const Json empty=Json::array();const auto found=data.find(key);const auto& value=found==data.end()?empty:*found;
    if(!value.is_array())throw std::invalid_argument(std::string(key)+": wymagana lista JSON.");
    return value;
}
double number(const Json& value){return document_finite(value,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());}
double field(const Json& value,const char* key,double fallback=0){const auto found=value.find(key);return found==value.end()?fallback:number(*found);}
bool truth(const Json& value){if(value.is_null())return false;if(value.is_boolean())return value.get<bool>();if(value.is_number())return value.get<double>()!=0;return !value.empty();}
std::string group_name(const Json& value){if(!value.is_string())throw std::invalid_argument("Wybierz istniejący mebel.");return value.get<std::string>();}
Point3 point3(const Json& value){if(!value.is_array()||value.size()!=3)throw std::invalid_argument("Punkt wymaga współrzędnych X, Y, Z.");return {number(value[0]),number(value[1]),number(value[2])};}
Point3 rotate(Point3 p,double rx,double ry,double rz) {
    const double a=rx*radians,b=ry*radians,c=rz*radians;
    const double y=p[1]*std::cos(a)-p[2]*std::sin(a),z=p[1]*std::sin(a)+p[2]*std::cos(a);p[1]=y;p[2]=z;
    const double x=p[0]*std::cos(b)+p[2]*std::sin(b),z2=-p[0]*std::sin(b)+p[2]*std::cos(b);p[0]=x;p[2]=z2;
    return {p[0]*std::cos(c)-p[1]*std::sin(c),p[0]*std::sin(c)+p[1]*std::cos(c),p[2]};
}
Point3 placement(const Json& part,Point3 point,bool inverse=false) {
    const auto& layout=object_member(part,"layout");if(layout.empty())return point;
    const auto yaw=field(layout,"yaw");
    if(inverse){for(size_t i=0;i<3;++i)point[i]-=field(layout,std::array<const char*,3>{"x","y","z"}[i]);return rotate(point,0,0,-yaw);}
    if(yaw)point=rotate(point,0,0,yaw);
    for(size_t i=0;i<3;++i)point[i]+=field(layout,std::array<const char*,3>{"x","y","z"}[i]);
    return point;
}
Point3 world(const Json& part,double u,double v,double w,bool local=false) {
    const auto plane=part.value("plane",std::string("XY"));Point3 point{};
    if(plane=="XY")point={u,v,w};else if(plane=="XZ")point={u,w,v};else if(plane=="YZ")point={w,u,v};else throw std::invalid_argument("Nieprawidłowa płaszczyzna elementu.");
    point=rotate(point,field(part,"rx"),field(part,"ry"),field(part,"rz"));
    for(size_t i=0;i<3;++i)point[i]+=field(part,std::array<const char*,3>{"x","y","z"}[i]);
    return local?point:placement(part,point);
}
Polygon points(const Json& part) {
    const auto found=part.find("points");
    if(found==part.end()){const auto u=field(part,"u",600),v=field(part,"v",400);return {{0,0},{u,0},{u,v},{0,v}};}
    if(!found->is_array())throw std::invalid_argument("Kontur musi być listą punktów.");
    Polygon result;result.reserve(found->size());
    for(const auto& point:*found){if(!point.is_array()||point.size()!=2)throw std::invalid_argument("Punkt konturu wymaga dwóch współrzędnych.");result.push_back({number(point[0]),number(point[1])});}
    return result;
}
struct Layout {Json json=Json::object();std::array<double,4> values{};};
struct Face {int axis;bool positive;double coordinate,start,end;};
std::vector<Face> flat_faces(Polygon poly) {
    if(!(layout_geometry::signed_area(poly)>0))std::reverse(poly.begin(),poly.end());
    std::vector<Face> result;
    for(size_t i=0;i<poly.size();++i){const auto& a=poly[i];const auto& b=poly[(i+1)%poly.size()];const auto dx=b[0]-a[0],dy=b[1]-a[1];
        if(std::abs(dx)<1e-6&&std::abs(dy)>1e-6)result.push_back({0,dy>0,a[0],std::min(a[1],b[1]),std::max(a[1],b[1])});
        else if(std::abs(dy)<1e-6&&std::abs(dx)>1e-6)result.push_back({1,dx<0,a[1],std::min(a[0],b[0]),std::max(a[0],b[0])});}
    return result;
}
double overlap(const Box& a,const Box& b,int axis){return std::min(a[1][axis],b[1][axis])-std::max(a[0][axis],b[0][axis]);}
bool intersects(const Box& a,const Box& b){for(int i=0;i<3;++i)if(!(overlap(a,b,i)>.01))return false;return true;}
bool collision(const Box& a,const Polygon& pa,const Box& b,const Polygon& pb){return intersects(a,b)&&layout_geometry::polygons_overlap(pa,pb);}
Box translated(Box box,const Point3& delta){for(auto& p:box)for(int i=0;i<3;++i)p[i]+=delta[i];return box;}
Polygon translated(Polygon polygon,const Point3& delta){for(auto& p:polygon)for(int i=0;i<2;++i)p[i]+=delta[i];return polygon;}
std::string direction(int axis,bool positive){return std::string(1,"xyz"[axis])+(positive?"+":"-");}
int axis_of(const std::string& value){return int(std::string("xyz").find(value[0]));}
bool face_contact(const Polygon& source,const Polygon& target,const std::string& dir,double gap=0) {
    if(dir[0]=='z')return layout_geometry::polygons_overlap(source,target);
    Point3 delta{};delta[axis_of(dir)]=gap+(dir[1]=='+'?-.02:.02);
    return layout_geometry::polygons_overlap(translated(source,delta),target);
}

// Raw rows are intentionally evaluated lazily. In particular nearest_snap on a
// nonorthogonal source must return before inspecting unrelated group layouts.
class Scene {
public:
    const Json& parts;
    const Json& accessories;
    std::vector<std::string> names;
    std::unordered_map<std::string,std::vector<size_t>> members;
    std::unordered_map<std::string,size_t> owners;
    std::unordered_map<std::string,Layout> layouts;
    std::unordered_map<std::string,Box> boxes,local_boxes;
    std::unordered_map<std::string,Polygon> footprints;
    std::unordered_map<std::string,std::vector<Face>> faces;
    explicit Scene(const Json& input):parts(array_member(input,"parts")),accessories(array_member(input,"accessories")) {
        if(!input.is_object())throw std::invalid_argument("Scena ustawienia wymaga obiektu JSON.");
        for(size_t i=0;i<parts.size();++i){const auto& part=parts[i];if(!part.is_object())throw std::invalid_argument("Nieprawidłowy element sceny ustawienia.");
            const auto group=group_name(part.at("group"));if(!members.contains(group))names.push_back(group);members[group].push_back(i);
            if(part.contains("id")&&part.at("id").is_string())owners[part.at("id").get<std::string>()]=i;}
    }
    const std::vector<size_t>& selected(const std::string& group)const{
        const auto found=members.find(group);if(found==members.end())throw std::invalid_argument("Wybierz istniejący mebel.");return found->second;
    }
    const Layout& layout(const std::string& group) {
        if(const auto found=layouts.find(group);found!=layouts.end())return found->second;
        const auto& indices=selected(group);const auto& initial=object_member(parts[indices[0]],"layout");Layout result;
        const char* keys[]={"x","y","z","yaw"};
        for(int k=0;k<4;++k){const auto value=initial.find(keys[k]);result.json[keys[k]]=value==initial.end()?Json(0):*value;result.values[k]=number(result.json[keys[k]]);}
        for(auto index:indices){const auto& value=object_member(parts[index],"layout");for(int k=0;k<4;++k)
            if(std::abs(field(value,keys[k])-result.values[k])>1e-7)throw std::invalid_argument("Elementy mebla mają różne ustawienia zestawu. Przypisz je do osobnych mebli lub wyrównaj ich ustawienie.");}
        return layouts.emplace(group,std::move(result)).first->second;
    }
    bool orthogonal(const std::string& group){return std::abs(std::remainder(layout(group).values[3],90))<1e-7;}
    const Box& bounds(const std::string& group,bool local=false) {
        auto& cache=local?local_boxes:boxes;if(const auto found=cache.find(group);found!=cache.end())return found->second;
        const auto& indices=selected(group);Box box{{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}}};bool any=false;
        auto include=[&](const Point3& point){any=true;for(int i=0;i<3;++i){box[0][i]=std::min(box[0][i],point[i]);box[1][i]=std::max(box[1][i],point[i]);}};
        for(auto index:indices){const auto& part=parts[index];const double thickness=field(part,"t",18);for(const auto& point:points(part))for(const auto t:{0.,thickness})include(world(part,point[0],point[1],t,local));}
        for(const auto& item:accessories){if(!item.is_object())throw std::invalid_argument("Nieprawidłowe wierzchołki akcesorium.");const auto owner=item.find("owner");if(owner==item.end()||!owner->is_string())continue;
            const auto found=owners.find(owner->get<std::string>());if(found==owners.end())continue;const auto& part=parts[found->second];
            if(part.at("group")!=group||(part.contains("visible")&&!truth(part.at("visible"))))continue;
            for(const auto& vertex:array_member(item,"vertices")){const auto point=point3(vertex);include(local?placement(part,point,true):point);}}
        if(!any)throw std::invalid_argument("Nie można wyznaczyć gabarytu pustego mebla.");
        return cache.emplace(group,box).first->second;
    }
    const Polygon& footprint(const std::string& group) {
        if(const auto found=footprints.find(group);found!=footprints.end())return found->second;
        const auto& indices=selected(group);std::optional<size_t> irregular;
        for(auto index:indices)if(parts[index].contains("shared_worktop")&&truth(parts[index].at("shared_worktop"))){irregular=index;break;}
        if(!irregular)for(auto index:indices)if(parts[index].contains("corner_bottom")&&truth(parts[index].at("corner_bottom"))){irregular=index;break;}
        Polygon result;
        if(irregular){const auto& part=parts[*irregular];for(const auto& p:points(part)){const auto value=world(part,p[0],p[1],0);result.push_back({value[0],value[1]});}}
        else {const auto& box=bounds(group);result={{box[0][0],box[0][1]},{box[1][0],box[0][1]},{box[1][0],box[1][1]},{box[0][0],box[1][1]}};}
        return footprints.emplace(group,std::move(result)).first->second;
    }
    const std::vector<Face>& flat(const std::string& group){if(const auto found=faces.find(group);found!=faces.end())return found->second;return faces.emplace(group,flat_faces(footprint(group))).first->second;}
    void all_bounds(){for(const auto& name:names)bounds(name);}
    void all_layouts(){for(const auto& name:names)orthogonal(name);}
    void all_footprints(){for(const auto& name:names)footprint(name);}
    void all_faces(){for(const auto& name:names)flat(name);}
};
Json moved_layout(Scene& scene,const std::string& group,const Point3& delta) {
    auto result=scene.layout(group).json;const char* keys[]={"x","y","z"};
    for(int i=0;i<3;++i)result[keys[i]]=document_finite(number(result[keys[i]])+document_finite(delta[i],-100000,100000),-100000,100000);
    return result;
}
void check_collision(Scene& scene,const std::string& group,const Box& box) {
    const auto& source=scene.bounds(group);const Point3 delta={box[0][0]-source[0][0],box[0][1]-source[0][1],0};
    const auto polygon=translated(scene.footprint(group),delta);
    for(const auto& other:scene.names){const auto& other_box=scene.bounds(other);if(other!=group&&collision(box,polygon,other_box,scene.footprint(other)))
        throw std::invalid_argument("Ustawienie nachodzi na mebel „"+other+"”. Zmień stronę, wysokość lub odstęp.");}
}
Json snap(Scene& scene,const std::string& group,const Json& raw_target,const Json& raw_direction,const Json& align,const Json& raw_gap) {
    if(raw_target==group)throw std::invalid_argument("Wybierz drugi mebel do połączenia.");
    if(!raw_direction.is_string())throw std::invalid_argument("Strona połączenia: x+, x-, y+, y-, z+ albo z-.");
    const auto dir=raw_direction.get<std::string>();if(dir!="x+"&&dir!="x-"&&dir!="y+"&&dir!="y-"&&dir!="z+"&&dir!="z-")throw std::invalid_argument("Strona połączenia: x+, x-, y+, y-, z+ albo z-.");
    if(!scene.orthogonal(group)||!scene.orthogonal(group_name(raw_target)))throw std::invalid_argument("Magnes ścian wymaga obrotów mebli o wielokrotność 90°. Dla innego kąta ustaw pozycję ręcznie.");
    const auto target_group=group_name(raw_target);
    const double gap=document_finite(raw_gap,0,10000);const auto& source=scene.bounds(group);const auto& target=scene.bounds(target_group);const auto axis=axis_of(dir);const bool positive=dir[1]=='+';Point3 delta{};
    delta[axis]=target[positive?1:0][axis]+(positive?gap:-gap)-source[positive?0:1][axis];
    for(int i=0;i<3;++i){if(i==axis)continue;Json mode=align;if(align.is_object())mode=align.value(std::string(1,"xyz"[i]),Json("start"));
        if(mode=="center")delta[i]=(target[0][i]+target[1][i])/2-(source[0][i]+source[1][i])/2;
        else if(mode=="start")delta[i]=target[0][i]-source[0][i];else if(mode=="end")delta[i]=target[1][i]-source[1][i];else if(mode!="keep")throw std::invalid_argument("Wyrównanie: start, center, end albo keep.");}
    check_collision(scene,group,translated(source,delta));return moved_layout(scene,group,delta);
}
struct Proposal {std::string direction;double distance;std::optional<std::array<double,5>> face_range;};
Json nearest(Scene& scene,const std::string& group,const Json& raw_tolerance,const Json& align) {
    const double tolerance=document_finite(raw_tolerance,0,1000);const auto& source=scene.bounds(group);
    if(!scene.orthogonal(group))return Json{{"layout",nullptr},{"info",nullptr}};
    scene.all_bounds();scene.all_layouts();scene.all_footprints();scene.all_faces();
    std::optional<double> best_travel;std::string best_target,best_direction;Point3 best_delta{};Json best_info;
    for(const auto& other:scene.names){const auto& target=scene.bounds(other);if(other==group||!scene.orthogonal(other))continue;std::vector<Proposal> proposals;
        for(const auto& face:scene.flat(other))for(const auto& own:scene.flat(group)){if(face.axis!=own.axis||face.positive==own.positive)continue;if(std::min(face.end,own.end)-std::max(face.start,own.start)<-tolerance)continue;
            proposals.push_back({direction(face.axis,face.positive),face.coordinate-own.coordinate,std::array<double,5>{double(1-face.axis),face.start,face.end,own.start,own.end}});}
        for(const bool positive:{true,false})proposals.push_back({direction(2,positive),target[positive?1:0][2]-source[positive?0:1][2],std::nullopt});
        for(const auto& proposal:proposals){const auto axis=axis_of(proposal.direction);if(std::abs(proposal.distance)>tolerance+1e-8)continue;Point3 delta{};delta[axis]=proposal.distance;bool rejected=false;
            for(int i=0;i<3;++i)if(i!=axis&&overlap(source,target,i)<-tolerance)rejected=true;
            if(rejected)continue;
            if(truth(align))for(int i=0;i<3;++i)if(i!=axis){std::array<double,3> possibilities;
                if(proposal.face_range&&int((*proposal.face_range)[0])==i){const auto& range=*proposal.face_range;possibilities={range[1]-range[3],range[2]-range[4],(range[1]+range[2]-range[3]-range[4])/2};}
                else possibilities={target[0][i]-source[0][i],target[1][i]-source[1][i],(target[0][i]+target[1][i])/2-(source[0][i]+source[1][i])/2};
                double correction=possibilities[0];for(size_t j=1;j<possibilities.size();++j)if(std::abs(possibilities[j])<std::abs(correction))correction=possibilities[j];if(std::abs(correction)<=tolerance)delta[i]=correction;}
            const auto box=translated(source,delta);const auto polygon=translated(scene.footprint(group),delta);
            for(int i=0;i<3;++i)if(i!=axis&&overlap(box,target,i)<=.01)rejected=true;
            if(rejected)continue;
            if(!face_contact(polygon,scene.footprint(other),proposal.direction))continue;
            for(const auto& name:scene.names)if(name!=group&&collision(box,polygon,scene.bounds(name),scene.footprint(name))){rejected=true;break;}
            if(rejected)continue;
            Sum squares;for(const auto value:delta)squares.add(value*value);const double travel=std::sqrt(squares.value());
            if(!best_travel||travel<*best_travel||(travel==*best_travel&&(other<best_target||(other==best_target&&proposal.direction<best_direction)))){
                best_travel=travel;best_target=other;best_direction=proposal.direction;best_delta=delta;
                best_info={{"group",group},{"target_group",other},{"direction",proposal.direction},{"delta",delta},{"distance",travel}};}
        }
    }
    if(!best_travel)return Json{{"layout",nullptr},{"info",nullptr}};
    return Json{{"layout",moved_layout(scene,group,best_delta)},{"info",std::move(best_info)}};
}
Json contacts(Scene& scene,const Json& filter,const Json& raw_tolerance) {
    const double tolerance=document_finite(raw_tolerance,0,1000);Json result=Json::array();scene.all_bounds();scene.all_layouts();scene.all_footprints();scene.all_faces();
    for(size_t first_index=0;first_index<scene.names.size();++first_index)for(size_t second_index=first_index+1;second_index<scene.names.size();++second_index){
        const auto& first=scene.names[first_index];const auto& second=scene.names[second_index];if(!filter.is_null()&&filter!=first&&filter!=second)continue;if(!scene.orthogonal(first)||!scene.orthogonal(second))continue;
        const auto& a=scene.bounds(first);const auto& b=scene.bounds(second);std::vector<Proposal> proposals;
        for(const auto& face:scene.flat(second))for(const auto& own:scene.flat(first)){if(face.axis!=own.axis||face.positive==own.positive)continue;if(std::min(face.end,own.end)-std::max(face.start,own.start)<=.01)continue;proposals.push_back({direction(face.axis,face.positive),face.coordinate-own.coordinate,std::nullopt});}
        for(const bool positive:{true,false})proposals.push_back({direction(2,positive),b[positive?1:0][2]-a[positive?0:1][2],std::nullopt});
        std::unordered_set<std::string> seen;
        for(const auto& proposal:proposals){const auto axis=axis_of(proposal.direction);if(seen.contains(proposal.direction)||std::abs(proposal.distance)>tolerance)continue;bool overlaps=true;
            for(int i=0;i<3;++i)if(i!=axis&&!(overlap(a,b,i)>.01))overlaps=false;
            if(!overlaps||!face_contact(scene.footprint(first),scene.footprint(second),proposal.direction,proposal.distance))continue;
            result.push_back(Json{{"group",first},{"target_group",second},{"direction",proposal.direction},{"gap",std::abs(proposal.distance)}});seen.insert(proposal.direction);}
    }
    return result;
}
struct Arguments {
    const Json& args;const Json& kwargs;
    explicit Arguments(const Json& request):args(array_member(request,"args")),kwargs(object_member(request,"kwargs")){}
    void validate(std::initializer_list<const char*> names)const{
        if(args.size()>names.size())throw std::invalid_argument("Za dużo argumentów ustawienia mebla.");
        for(auto item=kwargs.begin();item!=kwargs.end();++item)if(std::none_of(names.begin(),names.end(),[&](const auto key){return item.key()==key;}))throw std::invalid_argument("Nieznany argument ustawienia mebla: "+item.key());
        size_t i=0;for(const auto key:names)if(i++<args.size()&&kwargs.contains(key))throw std::invalid_argument("Argument ustawienia mebla podano dwukrotnie.");
    }
    const Json& at(size_t index,const char* key)const{if(index<args.size())return args[index];if(kwargs.contains(key))return kwargs.at(key);throw std::invalid_argument(std::string("Brakuje argumentu: ")+key);}
    Json value(size_t index,const char* key,Json fallback)const{if(index<args.size())return args[index];if(kwargs.contains(key))return kwargs.at(key);return fallback;}
};
} // namespace
std::optional<Json> dispatch_layout(const Json& request) {
    if(request.value("module",std::string())!="layout")return std::nullopt;
    const auto function=request.value("function",std::string());const Arguments args(request);
    if(function=="groups")args.validate({"scene"});
    else if(function=="group_layout"||function=="group_bounds"||function=="local_group_bounds")args.validate({"scene","group"});
    else if(function=="move_group")args.validate({"scene","group","dx","dy","dz"});
    else if(function=="rotate_group")args.validate({"scene","group","angle","pivot"});
    else if(function=="snap_group")args.validate({"scene","group","target_group","direction","align","gap"});
    else if(function=="nearest_snap")args.validate({"scene","group","tolerance","align"});
    else if(function=="contacts")args.validate({"scene","group","tolerance"});else return std::nullopt;
    Scene scene(args.at(0,"scene"));
    if(function=="groups")return scene.names;
    if(function=="contacts")return contacts(scene,args.value(1,"group",nullptr),args.value(2,"tolerance",.1));
    const auto group=group_name(args.at(1,"group"));
    if(function=="group_layout")return scene.layout(group).json;
    if(function=="group_bounds"||function=="local_group_bounds")return scene.bounds(group,function=="local_group_bounds");
    if(function=="move_group"){
        auto result=scene.layout(group).json;const char* keys[]={"dx","dy","dz"};const char* axes[]={"x","y","z"};
        for(int i=0;i<3;++i){const auto delta=document_finite(args.value(i+2,keys[i],0),-100000,100000);result[axes[i]]=document_finite(number(result[axes[i]])+delta,-100000,100000);}
        return result;
    }
    if(function=="snap_group")return snap(scene,group,args.at(2,"target_group"),args.value(3,"direction","x+"),args.value(4,"align","start"),args.value(5,"gap",0));
    if(function=="nearest_snap")return nearest(scene,group,args.value(2,"tolerance",80),args.value(3,"align",true));
    const auto& layout=scene.layout(group);const double angle=document_finite(args.at(2,"angle"),-100000,100000);const auto& box=scene.bounds(group);const auto raw_pivot=args.value(3,"pivot",nullptr);Point3 pivot{};
    if(raw_pivot.is_null()){for(int i=0;i<3;++i)pivot[i]=document_finite((box[0][i]+box[1][i])/2,-100000,100000);}
    else {if(!raw_pivot.is_array()||raw_pivot.size()!=3)throw std::invalid_argument("Punkt obrotu wymaga współrzędnych X, Y, Z.");for(int i=0;i<3;++i)pivot[i]=document_finite(raw_pivot[i],-100000,100000);}
    Point3 relative{};for(int i=0;i<3;++i)relative[i]=layout.values[i]-pivot[i];const auto shift=rotate(relative,0,0,angle);Json result=layout.json;const char* keys[]={"x","y","z"};
    for(int i=0;i<3;++i)result[keys[i]]=document_finite(pivot[i]+shift[i],-100000,100000);
    double yaw=std::fmod(layout.values[3]+angle+180,360);if(yaw<0)yaw+=360;yaw-=180;result["yaw"]=std::abs(yaw)<1e-9?Json(0):Json(yaw);return result;
}
} // namespace mebel::model
