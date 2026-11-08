#include "core_api.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <tuple>
#include <vector>

namespace mebel::model {
namespace {
constexpr double pi=std::numbers::pi_v<double>;
using Point=std::array<double,2>;
using Points=std::vector<Point>;

struct Call {
    Json values;
    Call(const Json& request,std::initializer_list<const char*> names,const Json& defaults={}) : values(defaults) {
        const Json args=request.value("args",Json::array());
        const Json kwargs=request.value("kwargs",Json::object());
        if(!args.is_array()||!kwargs.is_object())throw std::invalid_argument("Nieprawidłowe argumenty funkcji.");
        if(args.size()>names.size())throw std::invalid_argument("Za wiele argumentów funkcji.");
        std::size_t index=0;
        for(const char* name:names) {if(index<args.size())values[name]=args[index];++index;}
        for(auto it=kwargs.begin();it!=kwargs.end();++it) {
            auto found=std::find_if(names.begin(),names.end(),[&](const char* name){return it.key()==name;});
            if(found==names.end())throw std::invalid_argument("Nieznany argument: "+it.key());
            if(static_cast<std::size_t>(std::distance(names.begin(),found))<args.size())throw std::invalid_argument("Argument podany dwukrotnie: "+it.key());
            values[it.key()]=it.value();
        }
    }
    const Json& operator[](const char* key)const {
        if(!values.contains(key))throw std::invalid_argument(std::string("Brak argumentu: ")+key);
        return values.at(key);
    }
};

Json core_call(const char* function,const Json& args) {
    auto result=dispatch_core(Json{{"module","core"},{"function",function},{"args",args},{"kwargs",Json::object()}});
    if(!result)throw std::logic_error(std::string("Brak natywnej funkcji core: ")+function);
    return *result;
}
double scalar(const Json& value) {
    if(value.is_number())return value.get<double>();
    if(value.is_boolean())return value.get<bool>()?1.:0.;
    if(value.is_string()) {
        try {std::size_t used=0;auto text=value.get<std::string>();double v=std::stod(text,&used);
            if(text.find_first_not_of(" \t\r\n",used)==std::string::npos)return v;
        } catch(const std::exception&){}
    }
    throw std::invalid_argument("Wartość musi być liczbą.");
}
double finite(const Json& value,double low=-100000,double high=100000) {
    return core_call("finite",Json::array({value,low,high})).get<double>();
}
double number(const Json& object,const char* key,double fallback=0) {
    return scalar(object.value(key,Json(fallback)));
}
bool truth(const Json& value) {
    if(value.is_null())return false;
    if(value.is_boolean())return value.get<bool>();
    if(value.is_number())return value.get<double>()!=0;
    return !value.empty();
}
std::string text(const Json& value) {return value.is_string()?value.get<std::string>():std::string();}
Points points(const Json& value) {
    Points output;
    for(const auto& point:value) {
        if(!point.is_array()||point.size()!=2)throw std::invalid_argument("Punkt wymaga współrzędnych U i V.");
        output.push_back({scalar(point[0]),scalar(point[1])});
    }
    return output;
}
Json json_points(const Points& value) {
    Json result=Json::array();for(const auto& p:value)result.push_back(Json::array({p[0],p[1]}));return result;
}
Points part_points(const Json& part) {
    auto value=part.value("contour",Json::array());if(!value.empty())return points(value);
    const double u=number(part,"u",600),v=number(part,"v",400);return {{0,0},{u,0},{u,v},{0,v}};
}
bool inside(const Point& point,const Points& polygon) {
    // Typed inner loop: no JSON request construction for each of 72 samples.
    const double x=point[0],y=point[1];bool hit=false;
    for(size_t i=0;i<polygon.size();++i) {
        const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];
        const double cross=(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);
        if(std::abs(cross)<1e-7&&std::min(a[0],b[0])-1e-7<=x&&x<=std::max(a[0],b[0])+1e-7
            &&std::min(a[1],b[1])-1e-7<=y&&y<=std::max(a[1],b[1])+1e-7)return true;
        if((a[1]>y)!=(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])hit=!hit;
    }
    return hit;
}
Points validate_polygon(const Points& value) {
    return points(core_call("validate_polygon",Json::array({json_points(value)})));
}
double signed_area(const Points& polygon) {
    double result=0;for(std::size_t i=0;i<polygon.size();++i) {
        auto a=polygon[i],b=polygon[(i+1)%polygon.size()];result+=a[0]*b[1]-b[0]*a[1];
    } return result;
}
int resolution(const Json& value) {
    const double n=finite(value,4,24);
    if(static_cast<int>(n)!=n)throw std::invalid_argument("Liczba odcinków ćwiartki musi być całkowita: 4–24.");
    return static_cast<int>(n);
}
double round_decimal(double value,int digits) {
    const double scale=std::pow(10.,digits);return std::nearbyint(value*scale)/scale;
}
Points finish(const Points& value,double u,double v) {
    Points clean;
    for(auto p:value) {
        p[0]=std::min(u,std::max(0.,round_decimal(p[0],9)));
        p[1]=std::min(v,std::max(0.,round_decimal(p[1],9)));
        if(std::abs(p[0]-u)<1e-7)p[0]=u;
        if(std::abs(p[1]-v)<1e-7)p[1]=v;
        if(clean.empty()||std::hypot(clean.back()[0]-p[0],clean.back()[1]-p[1])>1e-7)clean.push_back(p);
    }
    if(clean.size()>1&&std::hypot(clean.front()[0]-clean.back()[0],clean.front()[1]-clean.back()[1])<=1e-7)clean.pop_back();
    clean=validate_polygon(clean);
    const double min_u=(*std::min_element(clean.begin(),clean.end(),[](auto a,auto b){return a[0]<b[0];}))[0];
    const double min_v=(*std::min_element(clean.begin(),clean.end(),[](auto a,auto b){return a[1]<b[1];}))[1];
    if(min_u!=0||min_v!=0)throw std::invalid_argument("Kontur jest zbyt mały dla podanej liczby odcinków.");
    return clean;
}
Json contour_defaults() {return Json{{"radii",Json::array({30,30,30,30})},{"segments",12},{"arch_rise",nullptr},{"taper",10}};}
Points contour(const Call& a) {
    std::string kind=text(a["kind"]);
    const double u=finite(a["u"],.1,10000),v=finite(a["v"],.1,10000);const int n=resolution(a["segments"]);
    if(kind=="rectangle")return {};
    if(kind=="circle") {
        if(std::abs(u-v)>1e-7)throw std::invalid_argument("Koło wymaga jednakowej szerokości i wysokości U/V.");
        kind="ellipse";
    }
    Points output;
    if(kind=="ellipse") {
        for(int i=0;i<4*n;++i)output.push_back({u/2+u/2*std::cos(i*2*pi/(4*n)),v/2+v/2*std::sin(i*2*pi/(4*n))});
    } else if(kind=="rounded"||kind=="capsule") {
        Json radii=a["radii"];
        if(kind=="capsule")radii=Json::array({std::min(u,v)/2,std::min(u,v)/2,std::min(u,v)/2,std::min(u,v)/2});
        if(radii.size()!=4)throw std::invalid_argument("Podaj osobny promień dla czterech narożników.");
        std::array<double,4> r{};for(int i=0;i<4;++i)r[i]=finite(radii[i],0,std::min(u,v));
        for(auto [first,second,length]:std::array<std::tuple<int,int,double>,4>{{{0,1,u},{1,2,v},{2,3,u},{3,0,v}}})
            if(r[first]+r[second]>length+1e-7)throw std::invalid_argument("Promienie sąsiednich narożników przekraczają długość krawędzi.");
        const std::array<double,4> cx{r[0],u-r[1],u-r[2],r[3]},cy{r[0],r[1],v-r[2],v-r[3]},start{pi,pi*1.5,0,pi/2};
        const std::array<Point,4> corners{{{0,0},{u,0},{u,v},{0,v}}};
        for(int corner=0;corner<4;++corner) {
            if(r[corner]==0)output.push_back(corners[corner]);
            else for(int i=0;i<=n;++i)output.push_back({cx[corner]+r[corner]*std::cos(start[corner]+i*pi/(2*n)),cy[corner]+r[corner]*std::sin(start[corner]+i*pi/(2*n))});
        }
    } else if(kind=="arch"||kind=="half_ellipse") {
        const double rise=kind=="half_ellipse"?v:finite(a["arch_rise"].is_null()?Json(std::min(v/3,u/2)):a["arch_rise"],.1,v);
        output={{0,0},{u,0}};if(rise<v)output.push_back({u,v-rise});
        for(int i=0;i<=2*n;++i)output.push_back({u/2+u/2*std::cos(i*pi/(2*n)),v-rise+rise*std::sin(i*pi/(2*n))});
        if(rise<v)output.push_back({0,v-rise});
    } else if(kind=="taper") {
        const double inset=finite(a["taper"],0,u/2-.01);output={{inset,0},{u-inset,0},{u,v},{0,v}};
    } else throw std::invalid_argument("Nieznany kształt konturu.");
    return finish(output,u,v);
}
Json shape_definition(Call a) {
    if(a["radii"].size()!=4)throw std::invalid_argument("Obrys wymaga czterech wartości promieni.");
    Json radii=Json::array();for(const auto& r:a["radii"])radii.push_back(finite(r,0,10000));a.values["radii"]=radii;
    const double u=scalar(a["u"]),v=scalar(a["v"]);
    const double rise=finite(a["arch_rise"].is_null()?Json(std::min(v/3,u/2)):a["arch_rise"],0,10000);
    const double taper=finite(a["taper"],0,10000);a.values["arch_rise"]=rise;a.values["taper"]=taper;
    contour(a);
    return Json{{"kind",a["kind"]},{"radii",radii},{"segments",resolution(a["segments"])},{"arch_rise",rise},{"taper",taper}};
}
double ellipse_perimeter(double a,double b) {
    constexpr int n=512;const double step=pi/(2*n);double odd=0,even=0;
    for(int i=1;i<n;++i) {const double value=std::hypot(a*std::sin(i*step),b*std::cos(i*step));if(i%2)odd+=value;else even+=value;}
    return 4*step/3*(std::hypot(0.,b)+std::hypot(a*std::sin(n*step),b*std::cos(n*step))+4*odd+2*even);
}
double profile_metric(const Json& profile,double u,double v,bool perimeter) {
    const std::string kind=text(profile.at("kind"));
    if(kind=="ellipse"||kind=="circle")return perimeter?ellipse_perimeter(u/2,v/2):pi*u*v/4;
    if(kind=="half_ellipse")return perimeter?u+ellipse_perimeter(u/2,v)/2:pi*u*v/4;
    if(kind=="arch") {const double rise=scalar(profile.at("arch_rise"));return perimeter?u+2*(v-rise)+ellipse_perimeter(u/2,rise)/2:u*(v-rise)+pi*u*rise/4;}
    if(kind=="rounded"||kind=="capsule") {
        Json radii=kind=="rounded"?profile.at("radii"):Json::array({std::min(u,v)/2,std::min(u,v)/2,std::min(u,v)/2,std::min(u,v)/2});
        double sum=0;for(const auto& r:radii)sum+=perimeter?scalar(r):scalar(r)*scalar(r);
        return perimeter?2*(u+v)+(pi/2-2)*sum:u*v-(1-pi/4)*sum;
    }
    if(kind=="taper") {double inset=scalar(profile.at("taper"));return perimeter?2*u-2*inset+2*std::hypot(v,inset):v*(u-inset);}
    return perimeter?2*(u+v):u*v;
}
Call profile_call(const Json& profile,double u,double v) {
    Json kwargs=profile;kwargs.erase("kind");
    return Call(Json{{"args",Json::array({profile.at("kind"),u,v})},{"kwargs",kwargs}},
        {"kind","u","v","radii","segments","arch_rise","taper"},contour_defaults());
}
Json resize_profile(Json part,double u,double v) {
    const Json profile=part.value("profile",Json::object());
    if(!profile.empty()) {auto call=profile_call(profile,u,v);part["profile"]=shape_definition(call);part["contour"]=json_points(contour(call));}
    else if(!part.value("contour",Json::array()).empty()) {
        Points outline=part_points(part);for(auto& p:outline){p[0]=p[0]*u/number(part,"u",600);p[1]=p[1]*v/number(part,"v",400);}part["contour"]=json_points(outline);
    }return part;
}
Json refresh_decor(Json part) {
    const Json cfg=part.value("decor",Json::object());
    if(cfg.value("type",Json())!="fluting")throw std::invalid_argument("Nieznana dekoracja parametryczna.");
    const double u=number(part,"u",600),v=number(part,"v",400),t=number(part,"t",18);
    const double width=finite(cfg.value("width",Json(8)),.5,1000),pitch=finite(cfg.value("pitch",Json(24)),width+.5,1000);
    const double depth=finite(cfg.value("depth",Json(3)),.1,t-.1);
    const double margin_u=finite(cfg.value("margin_u",Json(30)),0,u/2),margin_v=finite(cfg.value("margin_v",Json(20)),0,v/2-.01);
    const std::string face=text(cfg.value("face",Json("bottom")));
    if(face!="top"&&face!="bottom")throw std::invalid_argument("Ryflowanie wymaga górnej lub dolnej powierzchni.");
    const double usable=u-2*margin_u;
    if(usable<width||v-2*margin_v<.1)throw std::invalid_argument("Za mały element dla marginesów ryflowania.");
    const int count=static_cast<int>((usable-width)/pitch)+1;Json cuts=Json::array();
    for(const auto& cut:part.value("machining",Json::array()))if(cut.value("generated_decor",Json())!="fluting")cuts.push_back(cut);
    if(cuts.size()+count>200)throw std::invalid_argument("Ryflowanie przekracza 200 operacji. Zwiększ podziałkę.");
    const double start=(u-(count-1)*pitch)/2;
    for(int k=0;k<count;++k)cuts.push_back(Json{{"type","groove"},{"shape","rect"},{"face",face},{"u",start+k*pitch},{"v",v/2},{"length",width},{"width",v-2*margin_v},{"depth",depth},{"angle",0},{"generated_decor","fluting"},{"purpose","Ryflowanie parametryczne"}});
    part["machining"]=cuts;part["decor"]=Json{{"type","fluting"},{"width",width},{"pitch",pitch},{"depth",depth},{"margin_u",margin_u},{"margin_v",margin_v},{"face",face}};return part;
}
Json remove_decor(Json part) {
    Json cuts=Json::array();for(const auto& cut:part.value("machining",Json::array()))if(cut.value("generated_decor",Json())!="fluting")cuts.push_back(cut);
    part["machining"]=cuts;part["decor"]=Json::object();return part;
}
void validate_drill(const Json& part,const Json& op) {
    const std::string face=text(op.value("face",Json()));
    if(op.value("type",Json())!="drill"||(face!="top"&&face!="bottom"&&face!="u0"&&face!="u1"&&face!="v0"&&face!="v1"))throw std::invalid_argument("Nieprawidłowy typ lub strona wiercenia.");
    const double a=finite(op.at("u"),0,10000),b=finite(op.at("v"),0,10000);double d=finite(op.at("diameter"),0,10000);const double h=finite(op.at("depth"),0,10000);
    if(d<=0||h<=0)throw std::invalid_argument("Średnica i głębokość muszą być dodatnie.");
    if(truth(op.value("countersink_diameter",Json()))) {d=finite(op.at("countersink_diameter"),d,10000);finite(op.value("countersink_depth",Json(0)),.1,h);}
    const double u=number(part,"u",600),v=number(part,"v",400),t=number(part,"t",18);
    const std::array<double,3> limits=(face=="top"||face=="bottom")?std::array<double,3>{u,v,t}:(face[0]=='u'?std::array<double,3>{v,t,u}:std::array<double,3>{u,t,v});
    if(!(d/2<=a&&a<=limits[0]-d/2&&d/2<=b&&b<=limits[1]-d/2)||h>limits[2])throw std::invalid_argument("Wiercenie wykracza poza powierzchnię lub grubość w kierunku wiercenia.");
    if(face=="top"||face=="bottom") {const Points polygon=part_points(part);for(int i=0;i<72;++i)if(!inside({a+d/2*std::cos(i*2*pi/72),b+d/2*std::sin(i*2*pi/72)},polygon))throw std::invalid_argument("Wiercenie wykracza poza kontur.");}
}
Json drill_location(const Json& part,const Json& op) {
    const double a=scalar(op.at("u")),b=scalar(op.at("v"));const std::string face=op.at("face").get<std::string>();
    if(face=="top")return Json::array({Json::array({a,b,number(part,"t",18)}),Json::array({0,0,-1})});
    if(face=="bottom")return Json::array({Json::array({a,b,0}),Json::array({0,0,1})});
    if(face=="u0")return Json::array({Json::array({0,a,b}),Json::array({1,0,0})});
    if(face=="u1")return Json::array({Json::array({number(part,"u",600),a,b}),Json::array({-1,0,0})});
    if(face=="v0")return Json::array({Json::array({a,0,b}),Json::array({0,1,0})});
    if(face=="v1")return Json::array({Json::array({a,number(part,"v",400),b}),Json::array({0,-1,0})});
    throw std::invalid_argument("Nieprawidłowa strona wiercenia.");
}
void validate_cut(const Json& part,const Json& cut) {
    const std::string type=text(cut.value("type",Json())),shape=text(cut.value("shape",Json())),face=text(cut.value("face",Json()));
    if((type!="pocket"&&type!="cutout"&&type!="groove")||(shape!="rect"&&shape!="circle"&&shape!="slot"))throw std::invalid_argument("Obróbka: kieszeń, otwór przelotowy lub rowek; kształt prostokątny, kołowy lub podłużny.");
    if(face!="top"&&face!="bottom")throw std::invalid_argument("Frezowanie obsługuje powierzchnie +T i −T.");
    const double u=finite(cut.at("u"),0,10000),v=finite(cut.at("v"),0,10000);
    double length=finite(cut.at("length"),.1,10000);const double width=finite(cut.at("width"),.1,10000),t=number(part,"t",18);
    const double depth=type=="cutout"?t:finite(cut.at("depth"),.1,t);
    if(type!="cutout"&&depth>=t)throw std::invalid_argument("Kieszeń i rowek muszą mieć głębokość mniejszą od grubości. Dla otworu wybierz wycięcie przelotowe.");
    if(shape=="circle")length=width;
    if(shape=="slot"&&length<width)throw std::invalid_argument("Długość otworu podłużnego musi być co najmniej równa jego szerokości.");
    Points samples;
    if(shape=="circle")for(int i=0;i<72;++i)samples.push_back({u+width/2*std::cos(i*2*pi/72),v+width/2*std::sin(i*2*pi/72)});
    else for(const auto& [du,dv]:std::array<Point,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})samples.push_back({u+du*length/2,v+dv*width/2});
    const double angle=finite(cut.value("angle",Json(0)),-360,360)*pi/180;const Points polygon=part_points(part);
    for(auto& p:samples) {const double x=p[0],y=p[1];p={u+(x-u)*std::cos(angle)-(y-v)*std::sin(angle),v+(x-u)*std::sin(angle)+(y-v)*std::cos(angle)};}
    const double pu=number(part,"u",600),pv=number(part,"v",400);
    for(const auto& p:samples)if(!(p[0]>=-1e-7&&p[0]<=pu+1e-7&&p[1]>=-1e-7&&p[1]<=pv+1e-7))throw std::invalid_argument("Kontur obróbki wykracza poza wymiar formatki.");
    for(const auto& p:samples)if(!inside(p,polygon))throw std::invalid_argument("Obróbka wykracza poza kontur części.");
}
std::string uid() {
    static std::atomic<std::uint64_t> sequence{0};
    static const std::uint64_t seed=(static_cast<std::uint64_t>(std::random_device{}())<<32)^std::random_device{}();
    auto value=(seed+sequence.fetch_add(1)+std::chrono::high_resolution_clock::now().time_since_epoch().count())&0xffffffffffffULL;
    std::ostringstream stream;stream<<std::hex<<std::setfill('0')<<std::setw(12)<<value;return stream.str();
}
Json hole_array(const Call& a) {
    const int nu=static_cast<int>(finite(a["nu"],1,100)),nv=static_cast<int>(finite(a["nv"],1,100));
    if(nu*nv>500)throw std::invalid_argument("Maksymalnie 500 otworów w jednym szyku.");
    const std::string batch=uid();Json result=Json::array();
    for(int j=0;j<nv;++j)for(int i=0;i<nu;++i) {
        Json op{{"type","drill"},{"u",scalar(a["u"])+i*scalar(a["du"])},{"v",scalar(a["v"])+j*scalar(a["dv"])},{"diameter",a["diameter"]},{"depth",a["depth"]},{"face",a["face"]},{"batch",batch},{"purpose","szyk ręczny"}};
        validate_drill(a["part"],op);result.push_back(op);
    }
    return result;
}
Json shift_features(Json part,double du,double dv,double dt) {
    Json operations=part.value("operations",Json::array());for(auto& op:operations) {
        const std::string face=op.at("face").get<std::string>();
        const double a=(face=="top"||face=="bottom")?du:(face.starts_with('u')?dv:du),b=(face=="top"||face=="bottom")?dv:dt;
        op["u"]=scalar(op.at("u"))-a;op["v"]=scalar(op.at("v"))-b;
    }part["operations"]=operations;Json cuts=part.value("machining",Json::array());for(auto& cut:cuts){cut["u"]=scalar(cut.at("u"))-du;cut["v"]=scalar(cut.at("v"))-dv;}part["machining"]=cuts;return part;
}
Json normalized_part(Json part,const Json& input) {
    if(input.size()<3)throw std::invalid_argument("Kontur wymaga przynajmniej trzech punktów.");
    Points outline=points(input);for(auto& p:outline){p[0]=finite(p[0]);p[1]=finite(p[1]);}
    const Points old=part_points(part);
    const double u0=(*std::min_element(outline.begin(),outline.end(),[](auto a,auto b){return a[0]<b[0];}))[0],v0=(*std::min_element(outline.begin(),outline.end(),[](auto a,auto b){return a[1]<b[1];}))[1];
    if(outline!=old) {
        if((!part.value("fillets",Json::array()).empty()||!part.value("chamfers",Json::array()).empty())&&(!part.value("profile",Json::object()).empty()||outline.size()!=old.size()||signed_area(outline)*signed_area(old)<=0))throw std::invalid_argument("Przed edycją punktów obrysu usuń fazy i zaokrąglenia kantów w edytorze CAD; zmieni się numeracja krawędzi.");
        part["profile"]=Json::object();
    }
    for(auto& p:outline){p[0]-=u0;p[1]-=v0;}outline=validate_polygon(outline);part["contour"]=json_points(outline);
    const Json position=core_call("local_world",Json::array({part,u0,v0,0}));part["x"]=position[0];part["y"]=position[1];part["z"]=position[2];
    Json holes=part.value("holes",Json::array());for(auto& h:holes){h[0]=scalar(h[0])-u0;h[1]=scalar(h[1])-v0;}part["holes"]=holes;
    part=shift_features(part,u0,v0,0);
    const double u=(*std::max_element(outline.begin(),outline.end(),[](auto a,auto b){return a[0]<b[0];}))[0],v=(*std::max_element(outline.begin(),outline.end(),[](auto a,auto b){return a[1]<b[1];}))[1];
    const Points canonical{{0,0},{u,0},{u,v},{0,v}};
    const Json edges=part.value("edges",Json::array({false,false,false,false}));const bool edged=std::any_of(edges.begin(),edges.end(),[](const Json& e){return truth(e);});
    if(edged&&outline==canonical){part["u"]=u;part["v"]=v;part["contour"]=Json::array();}
    else {part["edges"]=Json::array({false,false,false,false});if(part.value("profile",Json::object()).empty()){part["u"]=u;part["v"]=v;}}
    // Full CAD/Part validation deliberately remains at the adapter boundary.
    return part;
}
Json transform_points(const Points& outline,const std::string& action) {
    if(outline.empty())throw std::invalid_argument("Najpierw narysuj kontur.");
    double xmin=outline[0][0],xmax=xmin,ymin=outline[0][1],ymax=ymin;
    for(auto p:outline){xmin=std::min(xmin,p[0]);xmax=std::max(xmax,p[0]);ymin=std::min(ymin,p[1]);ymax=std::max(ymax,p[1]);}
    const double cx=(xmin+xmax)/2,cy=(ymin+ymax)/2;Points result;
    for(auto p:outline)result.push_back(action=="mirror_u"?Point{2*cx-p[0],p[1]}:action=="mirror_v"?Point{p[0],2*cy-p[1]}:Point{cx-(p[1]-cy),cy+(p[0]-cx)});
    return json_points(result);
}
Json constrain_points(Points outline,int i,bool closed,const std::string& kind,const Json& length) {
    if(i<0||static_cast<std::size_t>(i)>=outline.size()||outline.size()<2)throw std::invalid_argument("Wybierz punkt początkowy odcinka na liście.");
    const int j=(i+1)%static_cast<int>(outline.size());
    if(!closed&&j==0)throw std::invalid_argument("Ostatni punkt otwartego konturu nie ma kolejnego odcinka.");
    auto a=outline[i],b=outline[j];
    if(kind=="horizontal")outline[j]={b[0],a[1]};
    else if(kind=="vertical")outline[j]={a[0],b[1]};
    else {
        if(length.is_null())return nullptr;
        const double old=std::hypot(a[0]-b[0],a[1]-b[1]);if(old==0)throw std::invalid_argument("Punkty się pokrywają.");
        const double target=scalar(length);outline[j]={a[0]+(b[0]-a[0])*target/old,a[1]+(b[1]-a[1])*target/old};
    }return json_points(outline);
}
}

std::optional<Json> dispatch_shapes(const Json& request) {
    const std::string module=request.value("module",std::string()),function=request.value("function",std::string());
    if(module=="shaped_furniture") {
        if(function=="_resolution") {Call a(request,{"value"});return resolution(a["value"]);}
        if(function=="_finish") {Call a(request,{"points","u","v"});return json_points(finish(points(a["points"]),scalar(a["u"]),scalar(a["v"])));}
        if(function=="contour"||function=="shape_definition") {Call a(request,{"kind","u","v","radii","segments","arch_rise","taper"},contour_defaults());return function=="contour"?json_points(contour(a)):shape_definition(a);}
        if(function=="profile_area"||function=="profile_perimeter") {Call a(request,{"profile","u","v"});return profile_metric(a["profile"],scalar(a["u"]),scalar(a["v"]),function=="profile_perimeter");}
        if(function=="_ellipse_perimeter") {Call a(request,{"a","b"});return ellipse_perimeter(scalar(a["a"]),scalar(a["b"]));}
        if(function=="resize_profile") {Call a(request,{"part","u","v"});return resize_profile(a["part"],scalar(a["u"]),scalar(a["v"]));}
        if(function=="refresh_decor"||function=="remove_decor") {Call a(request,{"part"});return function=="refresh_decor"?refresh_decor(a["part"]):remove_decor(a["part"]);}
        if(function=="styled_bounds") {
            Call a(request,{"project","group"});for(const auto& part:a["project"].at("parts"))if(part.at("group")==a["group"]&&!part.value("style_source",Json::object()).empty()) {
                const Json source=part.at("style_source"),origin=source.at("origin");
                const Json upper=Json::array({scalar(origin[0])+scalar(source.at("width")),scalar(origin[1])+scalar(source.at("depth")),scalar(origin[2])+scalar(source.at("height"))});
                return Json{{"lower",origin},{"upper",upper}};
            }
            return Json(nullptr);
        }
    } else if(module=="features") {
        if(function=="validate_drill"||function=="drill_location") {Call a(request,{"part","op"});if(function=="drill_location")return drill_location(a["part"],a["op"]);validate_drill(a["part"],a["op"]);return Json(nullptr);}
        if(function=="validate_cut") {Call a(request,{"part","cut"});validate_cut(a["part"],a["cut"]);return Json(nullptr);}
        if(function=="hole_array") {Call a(request,{"part","u","v","diameter","depth","nu","nv","du","dv","face"},Json{{"nu",1},{"nv",1},{"du",32},{"dv",32},{"face","top"}});return hole_array(a);}
        if(function=="shift_features") {Call a(request,{"part","du","dv","dt"},Json{{"du",0},{"dv",0},{"dt",0}});return shift_features(a["part"],scalar(a["du"]),scalar(a["dv"]),scalar(a["dt"]));}
    } else if(module=="sketch") {
        if(function=="normalized_part") {Call a(request,{"part","points"});return normalized_part(a["part"],a["points"]);}
        if(function=="transform_points") {Call a(request,{"points","action"});return transform_points(points(a["points"]),a["action"].get<std::string>());}
        if(function=="constrain_points") {Call a(request,{"points","index","closed","kind","length"},Json{{"closed",true},{"length",nullptr}});return constrain_points(points(a["points"]),a["index"].get<int>(),a["closed"].get<bool>(),a["kind"].get<std::string>(),a["length"]);}
    }
    return std::nullopt;
}
}
