#include "../core/core_api.hpp"
#include "../core/model_api.h"
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using mebel::model::Json;
namespace {
using Call=char*(*)(const char*);
using Free=void(*)(void*);
using Transform=int(*)(const MS3DPartPose*,const double*,uint64_t,double*);
Call call=nullptr;Free release=nullptr;Transform transform=nullptr;
Json results=Json::array();bool ok=true;
void check(bool good,const char* label){results.push_back({{"ok",good},{"name",label}});if(!good)ok=false;}
Json raw(const std::string& request){char* buffer=call(request.c_str());if(!buffer)throw std::runtime_error("No response");std::string text(buffer);release(buffer);return Json::parse(text);}
Json invoke(const char* module,const char* function,Json args=Json::array(),Json kwargs=Json::object()) {return raw(Json{{"module",module},{"function",function},{"args",args},{"kwargs",kwargs}}.dump());}
std::string utf8(const std::wstring& text){int count=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),count,nullptr,nullptr);return result;}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!dll)return 3;
    call=reinterpret_cast<Call>(GetProcAddress(dll,"ms3d_model_call"));
    release=reinterpret_cast<Free>(GetProcAddress(dll,"ms3d_model_free"));
    transform=reinterpret_cast<Transform>(GetProcAddress(dll,"ms3d_model_transform"));
    if(!call||!release||!transform)return 4;
    try {
        check(invoke("native","info")["result"]["python_required_by_library"]==false,"Native DLL requires no Python runtime");
        check(!raw("{")["ok"].get<bool>(),"Malformed JSON rejected without crossing the ABI");
        check(!raw("[]")["ok"].get<bool>(),"Invalid request shape rejected");
        check(!invoke("missing","function")["ok"].get<bool>(),"Unmigrated function is an explicit error");
        check(!raw(std::string(65,'[')+"0"+std::string(65,']'))["ok"].get<bool>(),"Excessive nesting rejected");
        char* null=call(nullptr);check(null&&Json::parse(null)["ok"]==false,"Null request rejected");release(null);
        const Json poly={{0,0},{900,0},{900,600},{450,600},{450,900},{0,900}};
        check(std::abs(invoke("core","polygon_area",{poly})["result"].get<double>()-675000)<1e-6,"Concave polygon area in millimetres");
        check(invoke("core","inside",{{200,700},poly})["result"]==true,"Interior of L contour is inside");
        check(invoke("core","inside",{{700,700},poly})["result"]==false,"Empty notch of L contour is outside");
        check(!invoke("core","validate_polygon",{{{0,0},{100,100},{0,100},{100,0}}})["ok"].get<bool>(),"Self-intersecting production contour rejected");
        check(invoke("depth_raster","triangulate",{poly})["result"].size()==4,"Concave view keeps exactly n minus 2 triangles");
        check(invoke("responsive_ui","flow_layout",{200,{{120,30},{120,30}}})["result"][1]==64,"Toolbar wraps while all controls stay reachable");
        for(int plane=0;plane<3;++plane) {
            MS3DPartPose pose{};pose.plane=plane;pose.origin[0]=10;pose.origin[1]=20;pose.origin[2]=30;
            double input[]={100,200,18},out[3]{};
            int sign=transform(&pose,input,1,out);
            check(sign==(plane==1?-1:1),plane==0?"XY winding preserved":plane==1?"XZ swapped-axis winding preserved":"YZ winding preserved");
            if(plane==2)check(out[0]==28&&out[1]==120&&out[2]==230,"YZ construction plane uses the original coordinate order");
        }
        MS3DPartPose pose{};pose.placement[3]=90;pose.placement[0]=500;
        double input[]={100,0,0},out[3]{};transform(&pose,input,1,out);
        check(std::abs(out[0]-500)<1e-7&&std::abs(out[1]-100)<1e-7,"Furniture layout yaw and room translation preserved");
        double invalid[]={std::numeric_limits<double>::quiet_NaN(),0,0},sentinel[]={7,8,9};
        check(transform(&pose,invalid,1,sentinel)==0&&sentinel[0]==7&&sentinel[1]==8&&sentinel[2]==9,"Invalid batch leaves the caller output unchanged");
        check(transform(nullptr,input,1,out)==0,"Null pose rejected");
        check(transform(&pose,input,2000001,out)==0,"Excessive vertex count rejected before accessing input");
        for(int i=0;i<4096;++i)invoke("core","polygon_area",{poly});
        check(invoke("native","info")["result"]["outstanding_buffers"]==0,"4096 calls release every response in its owning DLL");
        wchar_t tmp[MAX_PATH]{};GetTempPathW(MAX_PATH,tmp);
        const auto folder=std::filesystem::path(tmp)/(L"MebelModel_test_Łódź_"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        const auto project=folder/L"Projekt szafki – łazienka.mebel";
        const std::string path=utf8(project.wstring());
        Json snapshot={{"version",1},{"name","Łazienka i kuchnia"},{"parts",Json::array()},{"materials",Json::array({Json{{"id","mat1"},{"name","Dąb"}}})}};
        check(invoke("project_io","save",{path,snapshot})["ok"]==true,"Native atomic save works in Unicode folder");
        check(invoke("project_io","read",{path})["result"]==snapshot,"Native Unicode project roundtrip is lossless");
        Json wrong=snapshot;wrong["version"]=999;
        check(invoke("project_io","save",{path,wrong})["ok"]==false&&invoke("project_io","read",{path})["result"]==snapshot,"Rejected project cannot overwrite the previous file");
        check(invoke("project_io","save",{utf8(folder.wstring()),snapshot})["ok"]==false,"Atomic replacement failure is reported");
        int leftovers=0;for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".tmp")++leftovers;
        check(leftovers==0,"Atomic replacement failure removes its temporary file");
        const std::string serialized=R"({"version":1,"nested":{"z":1.0,"a":2},"name":"Order preserved"})";
        check(invoke("project_io","save",{path,serialized})["ok"]==true,"Serialized project uses native validated atomic save");
        check(invoke("project_io","read",{path,true})["result"]==serialized,"Native serialized roundtrip preserves nested order and numeric spelling");
        check(invoke("project_io","save",{path,std::string("{broken")})["ok"]==false&&invoke("project_io","read",{path,true})["result"]==serialized,"Malformed serialized project cannot replace a valid file");
        std::filesystem::remove(project);std::filesystem::remove(folder);
    }catch(const std::exception& e){results.push_back({{"ok",false},{"error",e.what()}});ok=false;}
    FreeLibrary(dll);
    Json report={{"ok",ok},{"checks",results},{"count",results.size()},{"process_uses_python",false}};
    std::ofstream output(std::filesystem::path(argv[2]),std::ios::binary);output<<report.dump(2);output.close();
    std::cout<<report.dump()<<'\n';return ok?0:1;
}
