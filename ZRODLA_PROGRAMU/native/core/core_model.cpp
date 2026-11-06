#include "core_api.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <locale.h>
#include <limits>
#include <map>
#include <numbers>
#include <sstream>
#include <regex>
#include <string_view>
#include <vector>

namespace mebel::model {
namespace {

using Point2 = std::array<double, 2>;
using Point3 = std::array<double, 3>;
constexpr double radians = std::numbers::pi / 180.0;

// Python 3.13 sum uses compensated floating-point summation. Preserve that
// behaviour in areas, perimeter and costs rather than accumulating naively.
struct Sum {
    double hi=0, lo=0;
    void add(double value) {
        const double next=hi+value;
        lo += std::abs(hi)>=std::abs(value) ? (hi-next)+value : (value-next)+hi;
        hi=next;
    }
    double value() const { return std::isfinite(lo) ? hi+lo : hi; }
};

std::string general(double value) {
    std::ostringstream stream;
    stream << std::setprecision(6) << std::defaultfloat << value;
    return stream.str();
}

bool whitespace(unsigned char c) { return std::isspace(c)!=0; }

struct Rune {uint32_t codepoint;std::string bytes;};
std::vector<Rune> unicode(const std::string& text) {
    std::vector<Rune> result;
    for (size_t index=0;index<text.size();) {
        const size_t begin=index;const auto byte=static_cast<unsigned char>(text[index++]);
        uint32_t codepoint=byte;size_t continuation=0;
        if (byte>=0xf0) {codepoint=byte&7;continuation=3;}
        else if (byte>=0xe0) {codepoint=byte&15;continuation=2;}
        else if (byte>=0xc0) {codepoint=byte&31;continuation=1;}
        for (size_t n=0;n<continuation;++n) {
            if (index>=text.size() || (static_cast<unsigned char>(text[index])&0xc0)!=0x80)
                throw std::invalid_argument("Nieprawidłowy tekst UTF-8.");
            codepoint=(codepoint<<6)|(static_cast<unsigned char>(text[index++])&63);
        }
        result.push_back({codepoint,text.substr(begin,index-begin)});
    }
    return result;
}

std::string normalize_number(const std::string& text) {
    if (std::none_of(text.begin(),text.end(),[](unsigned char c){return c>=128;})) return text;
    // Unicode 15.1 decimal digit starts, generated from the exact developer
    // CPython 3.13.14 unicodedata.decimal table; each range contains digits 0–9.
    static constexpr uint32_t zero[]={0x30,0x660,0x6f0,0x7c0,0x966,0x9e6,0xa66,0xae6,0xb66,0xbe6,0xc66,0xce6,0xd66,0xde6,0xe50,0xed0,0xf20,0x1040,0x1090,0x17e0,0x1810,0x1946,0x19d0,0x1a80,0x1a90,0x1b50,0x1bb0,0x1c40,0x1c50,0xa620,0xa8d0,0xa900,0xa9d0,0xa9f0,0xaa50,0xabf0,0xff10,0x104a0,0x10d30,0x11066,0x110f0,0x11136,0x111d0,0x112f0,0x11450,0x114d0,0x11650,0x116c0,0x11730,0x118e0,0x11950,0x11c50,0x11d50,0x11da0,0x11f50,0x16a60,0x16ac0,0x16b50,0x1d7ce,0x1d7d8,0x1d7e2,0x1d7ec,0x1d7f6,0x1e140,0x1e2f0,0x1e4f0,0x1e950,0x1fbf0};
    std::string result;
    for (const auto& rune:unicode(text)) {
        const uint32_t cp=rune.codepoint;
        if (cp==0x85 || cp==0xa0 || cp==0x1680 || (0x2000<=cp && cp<=0x200a) || cp==0x2028 || cp==0x2029 || cp==0x202f || cp==0x205f || cp==0x3000) {result+=' ';continue;}
        const auto digit=std::find_if(std::begin(zero),std::end(zero),[&](uint32_t start){return start<=cp && cp<start+10;});
        if (digit!=std::end(zero)) result+=char('0'+cp-*digit);else result+=rune.bytes;
    }
    return result;
}

double number(const Json& value) {
    if (value.is_boolean()) return value.get<bool>() ? 1.0 : 0.0;
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) {
        const std::string original=value.get<std::string>(),normalized=normalize_number(original);
        std::string text;
        for (size_t index=0;index<normalized.size();++index) {
            const char c=normalized[index];
            if (c=='_') {
                if (!index || index+1==normalized.size() || !std::isdigit(static_cast<unsigned char>(normalized[index-1])) || !std::isdigit(static_cast<unsigned char>(normalized[index+1])))
                    throw std::invalid_argument("could not convert string to float: '"+original+"'");
            } else text+=c;
        }
        size_t first=0,last=text.size();
        while (first<last && whitespace(static_cast<unsigned char>(text[first]))) ++first;
        while (last>first && whitespace(static_cast<unsigned char>(text[last-1]))) --last;
        text=text.substr(first,last-first);
        static const std::regex syntax(R"([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?|[+-]?(?:inf(?:inity)?|nan))",std::regex::icase);
        if (std::regex_match(text,syntax)) {
            // C locale and ERANGE return semantics match Python float for
            // overflow to infinity and underflow to signed zero.
            static _locale_t c_locale=_create_locale(LC_NUMERIC,"C");
            char* end=nullptr;
            const double result=_strtod_l(text.c_str(),&end,c_locale);
            if (end && *end=='\0') return result;
        }
        throw std::invalid_argument("could not convert string to float: '"+original+"'");
    }
    throw std::invalid_argument("Oczekiwano liczby.");
}

double finite_number(double result, double low=-100000, double high=100000) {
    if (!std::isfinite(result) || !(low<=result && result<=high))
        throw std::invalid_argument("Wartość musi mieścić się w zakresie "+general(low)+"–"+general(high)+".");
    return result;
}

double finite_value(const Json& value, double low=-100000, double high=100000) {
    return finite_number(number(value),low,high);
}

bool truth(const Json& value) {
    if (value.is_null()) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>()!=0;
    return !value.empty();
}

struct Arguments {
    const Json& args;
    const Json& kwargs;
    const Json& get(size_t index, const char* key) const {
        if (index<args.size()) return args[index];
        if (kwargs.contains(key)) return kwargs.at(key);
        throw std::invalid_argument(std::string("Brakuje argumentu: ")+key);
    }
    Json get(size_t index, const char* key, Json fallback) const {
        if (index<args.size()) return args[index];
        if (kwargs.contains(key)) return kwargs.at(key);
        return fallback;
    }
};

// value() returns a complete JSON copy. Keep request/project arrays borrowed;
// the parsed request stays alive for the entire dispatch and computation.
const Json& member(const Json& object,const char* key,const Json& fallback) {
    const auto item=object.find(key);
    return item==object.end() ? fallback : *item;
}

const Json& array_member(const Json& object,const char* key) {
    static const Json empty=Json::array();
    return member(object,key,empty);
}

const Json& object_member(const Json& object,const char* key) {
    static const Json empty=Json::object();
    return member(object,key,empty);
}

double field(const Json& object, const char* key, double fallback) {
    const auto item=object.find(key);
    return item==object.end() ? fallback : number(*item);
}

Point2 point2(const Json& point) {
    if (!point.is_array() || point.size()<2) throw std::invalid_argument("Punkt wymaga dwóch współrzędnych.");
    return {number(point[0]),number(point[1])};
}

Point3 point3(const Json& point) {
    if (!point.is_array() || point.size()!=3) throw std::invalid_argument("Punkt wymaga trzech współrzędnych.");
    return {number(point[0]),number(point[1]),number(point[2])};
}

std::vector<Point2> points(const Json& value) {
    if (!value.is_array()) throw std::invalid_argument("Nieprawidłowa lista punktów.");
    std::vector<Point2> result;
    result.reserve(value.size());
    for (const auto& item:value) result.push_back(point2(item));
    return result;
}

double area(const std::vector<Point2>& polygon) {
    Sum sum;
    for (size_t index=0;index<polygon.size();++index) {
        const auto& a=polygon[index]; const auto& b=polygon[(index+1)%polygon.size()];
        sum.add(a[0]*b[1]-b[0]*a[1]);
    }
    return std::abs(sum.value())/2;
}

double orient(const Point2& a,const Point2& b,const Point2& c) {
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
}

bool on(const Point2& a,const Point2& b,const Point2& c,double epsilon) {
    return std::abs(orient(a,b,c))<epsilon && std::min(a[0],b[0])-epsilon<=c[0] && c[0]<=std::max(a[0],b[0])+epsilon &&
           std::min(a[1],b[1])-epsilon<=c[1] && c[1]<=std::max(a[1],b[1])+epsilon;
}

Json validate_polygon(const Json& value) {
    if (!value.is_array() || value.size()<3 || value.size()>128)
        throw std::invalid_argument("Kontur wymaga od 3 do 128 punktów.");
    std::vector<Point2> polygon;
    for (const auto& item:value) {
        if (!item.is_array() || item.size()!=2) throw std::invalid_argument("Punkt wymaga dwóch współrzędnych.");
        polygon.push_back({finite_value(item[0],0,10000),finite_value(item[1],0,10000)});
    }
    if (area(polygon)<1) throw std::invalid_argument("Kontur musi mieć dodatnią powierzchnię.");
    for (size_t index=0;index<polygon.size();++index) {
        const auto& a=polygon[index];const auto& b=polygon[(index+1)%polygon.size()];
        if (std::hypot(a[0]-b[0],a[1]-b[1])<0.01)
            throw std::invalid_argument("Kolejne punkty konturu nie mogą być identyczne.");
        for (size_t next=index+1;next<polygon.size();++next) {
            if (next==index+1 || (index==0 && next==polygon.size()-1)) continue;
            const auto& c=polygon[next];const auto& d=polygon[(next+1)%polygon.size()];
            const double o1=orient(a,b,c),o2=orient(a,b,d),o3=orient(c,d,a),o4=orient(c,d,b);
            if ((o1*o2<0 && o3*o4<0) || on(a,b,c,1e-8) || on(a,b,d,1e-8) || on(c,d,a,1e-8) || on(c,d,b,1e-8))
                throw std::invalid_argument("Krawędzie konturu nie mogą się przecinać ani stykać.");
        }
    }
    return polygon;
}

bool inside(const Point2& q,const std::vector<Point2>& polygon) {
    bool hit=false;
    for (size_t index=0;index<polygon.size();++index) {
        const auto& a=polygon[index];const auto& b=polygon[(index+1)%polygon.size()];
        if (on(a,b,q,1e-7)) return true;
        if ((a[1]>q[1])!=(b[1]>q[1]) && q[0]<(b[0]-a[0])*(q[1]-a[1])/(b[1]-a[1])+a[0]) hit=!hit;
    }
    return hit;
}

Point3 rotate(Point3 point,double rx,double ry,double rz) {
    const double a=rx*radians,b=ry*radians,c=rz*radians;
    const double y=point[1]*std::cos(a)-point[2]*std::sin(a),z=point[1]*std::sin(a)+point[2]*std::cos(a);
    point[1]=y;point[2]=z;
    const double x=point[0]*std::cos(b)+point[2]*std::sin(b),z2=-point[0]*std::sin(b)+point[2]*std::cos(b);
    point[0]=x;point[2]=z2;
    const double x2=point[0]*std::cos(c)-point[1]*std::sin(c),y2=point[0]*std::sin(c)+point[1]*std::cos(c);
    return {x2,y2,point[2]};
}

Point3 local_world(const Json& part,double u,double v,double w) {
    const std::string plane=part.value("plane",std::string("XY"));
    Point3 local;
    if (plane=="XY") local={u,v,w};
    else if (plane=="XZ") local={u,w,v};
    else if (plane=="YZ") local={w,u,v};
    else throw std::invalid_argument("Nieprawidłowa płaszczyzna elementu.");
    const auto result=rotate(local,field(part,"rx",0),field(part,"ry",0),field(part,"rz",0));
    return {result[0]+field(part,"x",0),result[1]+field(part,"y",0),result[2]+field(part,"z",0)};
}

Point3 layout_point(const Json& part,Point3 point,bool inverse=false) {
    const Json placement=part.value("layout",Json::object());
    if (!truth(placement)) return point;
    if (!placement.is_object()) throw std::invalid_argument("Nieprawidłowe ustawienie mebla w zestawie.");
    const double yaw=field(placement,"yaw",0);
    if (inverse) {
        for (size_t index=0;index<3;++index) point[index]-=field(placement,std::array<const char*,3>{"x","y","z"}[index],0);
        return rotate(point,0,0,-yaw);
    }
    if (yaw!=0) point=rotate(point,0,0,yaw);
    for (size_t index=0;index<3;++index) point[index]+=field(placement,std::array<const char*,3>{"x","y","z"}[index],0);
    return point;
}

Point3 world(const Json& part,double u,double v,double w) {return layout_point(part,local_world(part,u,v,w));}

Json part_points(const Json& part) {
    const auto contour=part.value("contour",Json::array());
    if (truth(contour)) return contour;
    const double u=field(part,"u",600),v=field(part,"v",400);
    return Json::array({Json::array({0,0}),Json::array({u,0}),Json::array({u,v}),Json::array({0,v})});
}

Point2 blank(const Json& part,double edge_t) {
    const double u=field(part,"u",600),v=field(part,"v",400);
    if (truth(part.value("contour",Json::array()))) return {u,v};
    const Json edges=part.value("edges",Json::array({false,false,false,false}));
    if (!edges.is_array() || edges.size()!=4) throw std::invalid_argument("Nieprawidłowe obrzeża.");
    return {u-edge_t*(static_cast<int>(number(edges[0]))+static_cast<int>(number(edges[1]))),
            v-edge_t*(static_cast<int>(number(edges[2]))+static_cast<int>(number(edges[3])))};
}

double profile_measure(const Json& profile,double u,double v,const char* function) {
    return number(dispatch(Json{{"module","shaped_furniture"},{"function",function},{"args",Json::array({profile,u,v})},{"kwargs",Json::object()}}));
}

double profile_measure(const Json& part,const char* function) {
    return profile_measure(part.at("profile"),field(part,"u",600),field(part,"v",400),function);
}

double part_area(const Json& part) {
    if (truth(part.value("profile",Json::object()))) return profile_measure(part,"profile_area")/1e6;
    return area(points(part_points(part)))/1e6;
}

double edge_m(const Json& part) {
    if (truth(part.value("contour",Json::array()))) {
        if (!truth(part.value("contour_edge",Json(false)))) return 0;
        if (truth(part.value("profile",Json::object()))) return profile_measure(part,"profile_perimeter")/1000;
        const auto polygon=points(part_points(part));Sum sum;
        for (size_t index=0;index<polygon.size();++index) {
            const auto& a=polygon[index];const auto& b=polygon[(index+1)%polygon.size()];
            sum.add(std::hypot(a[0]-b[0],a[1]-b[1]));
        }
        return sum.value()/1000;
    }
    const Json edges=part.value("edges",Json::array({false,false,false,false}));
    if (!edges.is_array() || edges.size()!=4) throw std::invalid_argument("Nieprawidłowe obrzeża.");
    return (field(part,"v",400)*(static_cast<int>(number(edges[0]))+static_cast<int>(number(edges[1])))+
            field(part,"u",600)*(static_cast<int>(number(edges[2]))+static_cast<int>(number(edges[3]))))/1000;
}

double material_cost(const Json& material,const Json& part) {
    return part_area(part)*(material.value("kind",std::string("plyta"))=="drewno" ? field(part,"t",18)/1000*field(material,"price_m3",0) : field(material,"price_m2",95));
}

struct Materials {
    std::vector<const Json*> values;
    std::map<Json,size_t> index;
    explicit Materials(const Json& project) {
        const auto& input=array_member(project,"materials");
        if (!input.is_array()) throw std::invalid_argument("Projekt wymaga materiałów.");
        values.reserve(input.size());
        for (const auto& material:input) {
            // emplace retains the first entry, just as Project.mat's next().
            index.emplace(material.at("id"),values.size());
            values.push_back(&material);
        }
    }
    size_t at(const Json& id) const {
        const auto found=index.find(id);
        if (found==index.end()) throw std::invalid_argument("Element wskazuje nieznany materiał.");
        return found->second;
    }
};

std::array<int,2> edge_counts(const Json& part) {
    static const Json none=Json::array({false,false,false,false});
    const auto& edges=member(part,"edges",none);
    if (!edges.is_array() || edges.size()!=4) throw std::invalid_argument("Nieprawidłowe obrzeża.");
    return {static_cast<int>(number(edges[0]))+static_cast<int>(number(edges[1])),
            static_cast<int>(number(edges[2]))+static_cast<int>(number(edges[3]))};
}

struct CostMeasures {double area_m2,edge_m;};
CostMeasures cost_measures(const Json& part) {
    const double u=field(part,"u",600),v=field(part,"v",400);
    const auto& contour=array_member(part,"contour");
    const auto& profile=object_member(part,"profile");
    const bool has_contour=truth(contour),has_profile=truth(profile);
    // Parse custom contour vertices only once for both measurements. A regular
    // rectangle has precisely u*v area and needs no temporary JSON vertices.
    std::vector<Point2> polygon;
    if (has_contour && !has_profile) polygon=points(contour);
    const double area_m2=(has_profile ? profile_measure(profile,u,v,"profile_area") :
                         has_contour ? area(polygon) : std::abs(u*v))/1e6;
    double length=0;
    if (has_contour) {
        static const Json disabled=false;
        if (truth(member(part,"contour_edge",disabled))) {
            if (has_profile) length=profile_measure(profile,u,v,"profile_perimeter");
            else {
                Sum perimeter;
                for (size_t i=0;i<polygon.size();++i) {
                    const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];
                    perimeter.add(std::hypot(a[0]-b[0],a[1]-b[1]));
                }
                length=perimeter.value();
            }
        }
    } else {
        const auto edges=edge_counts(part);
        length=v*edges[0]+u*edges[1];
    }
    return {area_m2,length/1000};
}

Json costs(const Json& project) {
    const Materials materials(project);
    struct CostMaterial {bool parsed=false,wood=false;double price=0;};
    std::vector<CostMaterial> parsed(materials.values.size());
    Sum panels,edges,hardware;
    for (const auto& part:array_member(project,"parts")) {
        const size_t mi=materials.at(part.at("material"));
        auto& material=parsed[mi];
        if (!material.parsed) {
            const auto& raw=*materials.values[mi];
            material.wood=raw.value("kind",std::string("plyta"))=="drewno";
            material.price=field(raw,material.wood ? "price_m3" : "price_m2",material.wood ? 0 : 95);
            material.parsed=true;
        }
        const auto measures=cost_measures(part);
        panels.add(measures.area_m2*(material.wood ? field(part,"t",18)/1000*material.price : material.price));
        edges.add(measures.edge_m);
    }
    const double edge=edges.value()*field(project,"edge_price",3.5);
    for (const auto& item:array_member(project,"hardware")) hardware.add(number(item.at("qty"))*number(item.at("price")));
    // Python has two separate sums here; retain their final addition order.
    Sum room_objects;
    const auto& scene=object_member(project,"scene");
    for (const auto& object:array_member(scene,"objects")) room_objects.add(field(object,"price",0));
    const double hardware_cost=hardware.value()+room_objects.value(),labor=field(project,"labor",0);
    const double base=panels.value()+edge+hardware_cost+labor;
    return Json{{"panels",panels.value()},{"edges",edge},{"hardware",hardware_cost},{"labor",labor},{"base",base},{"total",base*(1+field(project,"markup",20)/100)}};
}

std::array<double,4> camera_basis(const Json& camera) {
    const double a=field(camera,"yaw",-38)*radians,b=field(camera,"pitch",24)*radians;
    return {std::cos(a),std::sin(a),std::cos(b),std::sin(b)};
}

Point3 camera_view(const Json& camera,Point3 point) {
    const auto center=point3(camera.value("center",Json::array({300,250,420})));
    for (size_t index=0;index<3;++index) point[index]-=center[index];
    const auto basis=camera_basis(camera);
    const double u=point[0]*basis[0]-point[1]*basis[1],d=point[0]*basis[1]+point[1]*basis[0];
    return {u,point[2]*basis[2]+d*basis[3],-d*basis[2]+point[2]*basis[3]};
}

Point3 camera_project(const Json& camera,Point3 point,double width,double height) {
    const auto view=camera_view(camera,point);const auto pan=point2(camera.value("pan",Json::array({0,0})));
    const double scale=field(camera,"scale",.4);
    return {width/2+pan[0]+view[0]*scale,height/2+pan[1]-view[1]*scale,view[2]};
}

std::string shade(const std::string& color,double factor) {
    const auto characters=unicode(color);
    std::ostringstream stream;stream << '#';
    for (size_t index:std::array<size_t,3>{1,3,5}) {
        if (index>=characters.size()) throw std::invalid_argument("Nieprawidłowy kolor.");
        std::string text=characters[index].bytes;
        if (index+1<characters.size()) text+=characters[index+1].bytes;
        text=normalize_number(text);size_t used=0;int channel=0;
        try {
            channel=std::stoi(text,&used,16);
            while (used<text.size() && whitespace(static_cast<unsigned char>(text[used]))) ++used;
            if (used!=text.size()) throw std::invalid_argument("hex trailing data");
        } catch (const std::exception&) {throw std::invalid_argument("Nieprawidłowy kolor.");}
        const double product=channel*factor;
        if (std::isnan(product)) throw std::invalid_argument("cannot convert float NaN to integer");
        if (!std::isfinite(product)) throw std::invalid_argument("cannot convert float infinity to integer");
        const double floor=std::floor(product),fraction=product-floor;
        const double rounded=fraction<.5 ? floor : fraction>.5 ? floor+1 : (std::fmod(std::abs(floor),2)==0 ? floor : floor+1);
        const int value=rounded<=0 ? 0 : rounded>=255 ? 255 : static_cast<int>(rounded);
        stream << std::hex << std::setw(2) << std::setfill('0') << value;
    }
    return stream.str();
}

// The complete original area-sorted guillotine heuristic. The bridge performs
// Project.validate (including CAD/room validators) before serialization. Local
// numeric checks additionally protect direct calls from the native Workbench.
Json cut_plan(const Json& project) {
    const auto& input=array_member(project,"parts");
    if (!input.is_array() || input.size()>2000 || array_member(project,"materials").empty())
        throw std::invalid_argument("Projekt wymaga materiałów i może mieć najwyżej 2000 elementów.");
    const double kerf=finite_number(field(project,"kerf",3.2),0,100000),margin=finite_number(field(project,"margin",10),0,100000),edge_t=finite_number(field(project,"edge_t",1),0,100000);
    const Materials materials(project);
    struct PartStock {const Json* id;const Json* material;Point2 cut;double thickness,sort_area;bool grain;};
    std::vector<PartStock> parts;
    parts.reserve(input.size());
    for (const auto& part:input) {
        const double u=finite_number(field(part,"u",600),.1,10000),v=finite_number(field(part,"v",400),.1,10000),t=finite_number(field(part,"t",18),.1,10000);
        Point2 cut={u,v};
        if (!truth(array_member(part,"contour"))) {
            const auto edges=edge_counts(part);
            cut={u-edge_t*edges[0],v-edge_t*edges[1]};
        }
        if (std::min(cut[0],cut[1])<=0) throw std::invalid_argument("Obrzeże jest grubsze od wymiaru elementu.");
        static const Json aligned=true;
        parts.push_back({&part.at("id"),&part.at("material"),cut,t,cut[0]*cut[1],truth(member(part,"grain",aligned))});
    }
    std::stable_sort(parts.begin(),parts.end(),[](const PartStock& a,const PartStock& b){return a.sort_area>b.sort_area;});
    using FreeRectangle=std::array<double,4>;
    struct Piece {const Json* id;double x,y,u,v;bool rotated;};
    struct Sheet {size_t material;const Json* material_id;double thickness,u,v;std::vector<FreeRectangle> free;std::vector<Piece> pieces;};
    struct MaterialSize {bool parsed=false;double u=0,v=0;};
    std::vector<MaterialSize> material_sizes(materials.values.size());
    std::vector<Sheet> sheets;
    Json unplaced=Json::array();
    for (const auto& stock:parts) {
        const size_t material_index=materials.at(*stock.material);
        struct Option {double a,b;bool rotated;};
        const std::array<Option,2> options={Option{stock.cut[0],stock.cut[1],false},Option{stock.cut[1],stock.cut[0],true}};
        const size_t option_count=stock.grain ? 1 : 2;
        struct Choice {bool present=false;double score=0;size_t sheet=0,free=0;Option option{};} chosen;
        for (size_t si=0;si<sheets.size();++si) {
            const auto& sheet=sheets[si];
            if (sheet.material!=material_index || sheet.thickness!=stock.thickness) continue;
            for (size_t ri=0;ri<sheet.free.size();++ri) {
                const auto& free=sheet.free[ri];
                for (size_t oi=0;oi<option_count;++oi) {
                    const auto& option=options[oi];
                    if (option.a+kerf<=free[2]+1e-8 && option.b+kerf<=free[3]+1e-8) {
                        const double score=free[2]*free[3]-(option.a+kerf)*(option.b+kerf);
                        if (!chosen.present || score<chosen.score) chosen={true,score,si,ri,option};
                    }
                }
            }
        }
        if (!chosen.present) {
            auto& dimensions=material_sizes[material_index];
            if (!dimensions.parsed) {
                const auto& material=*materials.values[material_index];
                dimensions.u=finite_number(field(material,"sheet_u",2800),1,10000);
                dimensions.v=finite_number(field(material,"sheet_v",2070),1,10000);
                dimensions.parsed=true;
            }
            const double sheet_u=dimensions.u,sheet_v=dimensions.v;
            const double sw=sheet_u-2*margin,sh=sheet_v-2*margin;
            const auto end=options.begin()+static_cast<std::array<Option,2>::difference_type>(option_count);
            const auto possible=std::find_if(options.begin(),end,[&](const Option& o){return o.a<=sw+1e-8 && o.b<=sh+1e-8;});
            if (possible==end) {unplaced.push_back(*stock.id);continue;}
            sheets.push_back({material_index,stock.material,stock.thickness,sheet_u,sheet_v,{{margin,margin,sw+kerf,sh+kerf}},{}});
            chosen={true,0,sheets.size()-1,0,*possible};
        }
        auto& sheet=sheets[chosen.sheet];const auto free=sheet.free[chosen.free];
        sheet.free.erase(sheet.free.begin()+static_cast<std::vector<FreeRectangle>::difference_type>(chosen.free));
        const double x=free[0],y=free[1],fw=free[2],fh=free[3],a=chosen.option.a,b=chosen.option.b;
        sheet.pieces.push_back({stock.id,x,y,a,b,chosen.option.rotated});
        const double rw=fw-a-kerf,bh=fh-b-kerf;
        if (rw>1e-8) sheet.free.push_back({x+a+kerf,y,rw,fh});
        if (bh>1e-8) sheet.free.push_back({x,y+b+kerf,a+kerf,bh});
    }
    Json result_sheets=Json::array();
    for (const auto& sheet:sheets) {
        Json pieces=Json::array();
        for (const auto& piece:sheet.pieces) pieces.push_back(Json{{"id",*piece.id},{"x",piece.x},{"y",piece.y},{"u",piece.u},{"v",piece.v},{"rotated",piece.rotated}});
        result_sheets.push_back(Json{{"material",*sheet.material_id},{"thickness",sheet.thickness},{"u",sheet.u},{"v",sheet.v},{"free",sheet.free},{"pieces",std::move(pieces)}});
    }
    Json result=Json::array();
    result.push_back(std::move(result_sheets));result.push_back(std::move(unplaced));
    return result;
}

} // namespace

double document_finite(const Json& value,double low,double high) {
    return finite_value(value,low,high);
}

std::optional<Json> dispatch_core(const Json& request) {
    if (request.value("module",std::string())!="core") return std::nullopt;
    const std::string function=request.value("function",std::string());
    const auto& args=array_member(request,"args");const auto& kwargs=object_member(request,"kwargs");
    if (!args.is_array() || !kwargs.is_object()) throw std::invalid_argument("Argumenty funkcji wymagają listy i słownika.");
    const Arguments a{args,kwargs};
    if (function=="finite") return finite_value(a.get(0,"x"),number(a.get(1,"lo",-100000)),number(a.get(2,"hi",100000)));
    if (function=="polygon_area") return area(points(a.get(0,"points")));
    if (function=="validate_polygon") return validate_polygon(a.get(0,"points"));
    if (function=="inside") return inside(point2(a.get(0,"q")),points(a.get(1,"p")));
    if (function=="rotate") return rotate(point3(a.get(0,"point")),number(a.get(1,"rx")),number(a.get(2,"ry")),number(a.get(3,"rz")));
    if (function=="local_world" || function=="world") {
        const auto& part=a.get(0,"p");const double u=number(a.get(1,"u")),v=number(a.get(2,"v")),w=number(a.get(3,"w"));
        return function=="world" ? Json(world(part,u,v,w)) : Json(local_world(part,u,v,w));
    }
    if (function=="layout_point" || function=="inverse_layout_point") return layout_point(a.get(0,"p"),point3(a.get(1,"point")),function=="inverse_layout_point");
    if (function=="shade") return shade(a.get(0,"color").get<std::string>(),number(a.get(1,"factor")));
    if (function=="cut_plan") return cut_plan(a.get(0,"project"));
    if (function=="Part.points" || function=="part_points") return part_points(a.get(0,"self"));
    if (function=="Part.area" || function=="part_area") return part_area(a.get(0,"self"));
    if (function=="Part.edge_m" || function=="part_edge_m") return edge_m(a.get(0,"self"));
    if (function=="Part.blank" || function=="part_blank") return blank(a.get(0,"self"),number(a.get(1,"edge_t")));
    if (function=="Material.cost" || function=="material_cost") return material_cost(a.get(0,"self"),a.get(1,"part"));
    if (function=="Project.costs" || function=="project_costs") return costs(a.get(0,"self"));
    if (function=="Camera._basis" || function=="camera_basis") return camera_basis(a.get(0,"self"));
    if (function=="Camera.view" || function=="camera_view") return camera_view(a.get(0,"self"),point3(a.get(1,"point")));
    if (function=="Camera.project" || function=="camera_project") return camera_project(a.get(0,"self"),point3(a.get(1,"point")),number(a.get(2,"width")),number(a.get(3,"height")));
    if (function=="explosion_shift") {
        const auto& part=a.get(0,"p");const auto& camera=a.get(1,"camera");
        auto center=world(part,field(part,"u",600)/2,field(part,"v",400)/2,field(part,"t",18));
        const auto camera_center=point3(camera.value("center",Json::array({300,250,420})));const double explode=number(a.get(2,"explode"));
        for (size_t i=0;i<3;++i) center[i]=(center[i]-camera_center[i])*explode;
        return center;
    }
    if (function=="part_metrics") {
        const auto& part=a.get(0,"part");const double edge_t=number(a.get(1,"edge_t",1));
        return Json{{"points",part_points(part)},{"area_m2",part_area(part)},{"edge_m",edge_m(part)},{"blank",blank(part,edge_t)},{"thickness_mm",field(part,"t",18)},
                    {"world_origin",world(part,0,0,0)}};
    }
    return std::nullopt;
}

} // namespace mebel::model
