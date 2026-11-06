#include "core_api.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <vector>
#include <system_error>

namespace mebel::model {
namespace {
Json arg(const Json& r,size_t i,const char* name,Json fallback=Json()) {
    const auto a=r.value("args",Json::array());
    const auto k=r.value("kwargs",Json::object());
    if(a.size()>i)return a[i];
    if(k.contains(name))return k[name];
    if(fallback.is_null())throw std::invalid_argument(std::string("Brak argumentu: ")+name);
    return fallback;
}
std::wstring wide(const std::string& text) {
    if(text.find('\0')!=std::string::npos)throw std::invalid_argument("Ścieżka zawiera znak NUL.");
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),int(text.size()),nullptr,0);
    if(!n)throw std::invalid_argument("Nieprawidłowa ścieżka UTF-8.");
    std::wstring result(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.c_str(),int(text.size()),result.data(),n);
    return result;
}
[[noreturn]] void ioerror(const char* action,DWORD error=GetLastError()) {
    throw std::system_error(int(error),std::system_category(),action);
}
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    bool valid()const{return h!=INVALID_HANDLE_VALUE;}
    void close(){if(valid()){CloseHandle(h);h=INVALID_HANDLE_VALUE;}}
};
Json parseProject(const std::string& text) {
    auto depthGuard=[](int depth,Json::parse_event_t,Json&){if(depth>64)throw std::invalid_argument("Projekt jest zbyt zagnieżdżony.");return true;};
    return Json::parse(text,depthGuard);
}
Json readProject(const std::wstring& path,bool preserve_order=false) {
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!f.valid())ioerror("Nie można otworzyć projektu");
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(f.h,&length))ioerror("Nie można odczytać rozmiaru projektu");
    if(length.QuadPart>10000000)throw std::invalid_argument("Plik projektu jest zbyt duży.");
    std::string text(size_t(length.QuadPart),'\0');DWORD read=0;
    if(!ReadFile(f.h,text.data(),DWORD(text.size()),&read,nullptr)||read!=text.size())ioerror("Nie można odczytać projektu");
    Json parsed=parseProject(text);
    return preserve_order ? Json(text) : parsed;
}
void saveProject(const std::wstring& path,const Json& data) {
    // A serialized snapshot preserves the editor's nested dictionary order and
    // exact numeric spelling. Parse it for safety, then atomically write those
    // bytes instead of reordering arbitrary production/export metadata.
    std::string text=data.is_string() ? data.get<std::string>() : data.dump(2,' ',false);
    if(text.size()>10000000)throw std::invalid_argument("Plik projektu jest zbyt duży.");
    Json parsed=parseProject(text);
    if(!parsed.is_object()||parsed.value("version",0)!=1)throw std::invalid_argument("Nieobsługiwana wersja projektu.");
    static std::atomic<uint64_t> serial{0};
    std::wstring temporary;
    HANDLE raw=INVALID_HANDLE_VALUE;
    for(int i=0;i<20&&raw==INVALID_HANDLE_VALUE;++i) {
        temporary=path+L".ms3d-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial)+L".tmp";
        raw=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(raw==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_EXISTS)ioerror("Nie można utworzyć pliku projektu");
    }
    if(raw==INVALID_HANDLE_VALUE)ioerror("Nie można utworzyć pliku tymczasowego");
    Handle f(raw);
    try {
        DWORD written=0;
        if(!WriteFile(f.h,text.data(),DWORD(text.size()),&written,nullptr)||written!=text.size())ioerror("Nie można zapisać projektu");
        if(!FlushFileBuffers(f.h))ioerror("Nie można zatwierdzić danych projektu");
        f.close();
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))ioerror("Nie można zastąpić projektu");
    }catch(...){f.close();DeleteFileW(temporary.c_str());throw;}
}
Json triangulate(const Json& points) {
    const size_t count=points.size();Json result=Json::array();
    if(count>16384)throw std::invalid_argument("Kontur widoku ma zbyt wiele punktów.");
    if(count<3)return result;
    if(count==3){result.push_back({0,1,2});return result;}
    double area=0;
    for(size_t i=0;i<count;++i){const auto& a=points[i];const auto& b=points[(i+1)%count];area+=double(a[0])*double(b[1])-double(b[0])*double(a[1]);}
    if(std::abs(area)<1e-10)return result;
    const double sign=area>0 ? 1 : -1;
    auto cross=[&](size_t a,size_t b,size_t c){return ((double(points[b][0])-double(points[a][0]))*(double(points[c][1])-double(points[a][1]))-(double(points[b][1])-double(points[a][1]))*(double(points[c][0])-double(points[a][0])))*sign;};
    bool convex=true;
    for(size_t i=0;i<count;++i)if(cross((i+count-1)%count,i,(i+1)%count)<-1e-8){convex=false;break;}
    if(convex){for(size_t i=1;i<count-1;++i)result.push_back({0,i,i+1});return result;}
    std::vector<size_t> remaining;
    for(size_t i=0;i<count;++i)remaining.push_back(i);
    while(remaining.size()>3) {
        bool found=false;
        for(size_t k=0;k<remaining.size();++k) {
            const auto a=remaining[(k+remaining.size()-1)%remaining.size()],b=remaining[k],c=remaining[(k+1)%remaining.size()];
            if(cross(a,b,c)<=1e-9)continue;
            bool occupied=false;
            for(auto j:remaining){if(j==a||j==b||j==c)continue;if(cross(a,b,j)>1e-9&&cross(b,c,j)>1e-9&&cross(c,a,j)>1e-9){occupied=true;break;}}
            if(occupied)continue;
            result.push_back({a,b,c});remaining.erase(remaining.begin()+k);found=true;break;
        }
        if(!found)throw std::invalid_argument("Kontur powierzchni widoku jest samoprzecinający lub zdegenerowany.");
    }
    result.push_back(remaining);return result;
}
}
std::optional<Json> dispatch_services(const Json& r) {
    const std::string module=r.at("module"),function=r.at("function");
    if(module=="project_io") {
        const auto path=wide(arg(r,0,"path").get<std::string>());
        if(function=="read")return readProject(path,arg(r,1,"preserve_order",false).get<bool>());
        if(function=="save"){saveProject(path,arg(r,1,"data"));return nullptr;}
    }
    if(module=="depth_raster"&&function=="triangulate")return triangulate(arg(r,0,"points"));
    if(module=="responsive_ui"&&function=="flow_layout") {
        const int width=std::max(1,int(arg(r,0,"width").get<double>()));
        const auto sizes=arg(r,1,"sizes");const double gap=arg(r,2,"gap",4);
        double x=0,y=0,row=0;Json result=Json::array();
        for(const auto& size:sizes){const int w=std::min(width,std::max(1,int(size[0].get<double>()))),h=std::max(1,int(size[1].get<double>()));
            if(x&&x+w>width){x=0;y+=row+gap;row=0;}
            result.push_back({x,y,w,h});x+=w+gap;row=std::max(row,double(h));}
        return Json::array({result,result.empty()?1:y+row});
    }
    if(module=="responsive_ui"&&function=="clamp_inspector") {
        const double width=arg(r,0,"width"),total=arg(r,1,"total"),scale=arg(r,2,"scale",1);
        const double minimum=std::min(240.,std::max(1.,total-320.));
        const double maximum=std::max(minimum,total-std::max(320.,double(int(320.*std::min(scale,1.25))))-10.);
        return std::max(minimum,std::min(double(int(width)),maximum));
    }
    return std::nullopt;
}
}
