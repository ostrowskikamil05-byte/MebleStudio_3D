// Native rigid-layout acceptance. No Python, GUI, CAD kernel or adapter fallback.
#include "../core/core_api.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using mebel::model::Json;
namespace {
using Call=char*(*)(const char*);using Free=void(*)(void*);
Call call=nullptr;Free release=nullptr;Json records=Json::array();
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
Json invoke(const char* function,Json args=Json::array(),const char* module="layout",Json kwargs=Json::object()){
    const auto request=Json{{"module",module},{"function",function},{"args",args},{"kwargs",kwargs}}.dump();
    char* pointer=call(request.c_str());require(pointer!=nullptr,"Native response allocation failed");
    std::string text;
    try{text=pointer;}catch(...){release(pointer);throw;}
    release(pointer);return Json::parse(text);
}
Json run(const char* function,Json args=Json::array(),const char* module="layout"){
    const auto result=invoke(function,args,module);
    if(result.at("ok")!=true)throw std::runtime_error(std::string(function)+": "+result.at("error").get<std::string>());
    return result.at("result");
}
void close_value(double actual,double expected,const char* error){require(std::abs(actual-expected)<=1e-7,error);}
void point(const Json& actual,std::initializer_list<double> expected){size_t i=0;for(double value:expected)close_value(actual.at(i++).get<double>(),value,"Coordinate differs from the independent expected geometry");}
Json part(const char* id,const char* group,double u=600,double v=400,double t=800,double x=0,double y=0,double z=0){
    return {{"id",id},{"group",group},{"u",u},{"v",v},{"t",t},{"x",x},{"y",y},{"z",z},
        {"rx",0},{"ry",0},{"rz",0},{"plane","XY"},{"layout",Json::object()},
        {"points",Json::array({Json::array({0,0}),Json::array({u,0}),Json::array({u,v}),Json::array({0,v})})},
        {"shared_worktop",false},{"corner_bottom",false}};
}
Json boxes(double x=1500,double y=0,double z=0){return {{"parts",Json::array({part("a","A"),part("b","B",600,400,800,x,y,z)})},{"accessories",Json::array()}};}
void test(const char* name,const std::function<void()>& operation){
    try{operation();records.push_back({{"name",name},{"ok",true}});}
    catch(const std::exception& error){records.push_back({{"name",name},{"ok",false},{"error",error.what()}});}
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!dll)return 3;
    call=reinterpret_cast<Call>(GetProcAddress(dll,"ms3d_model_call"));release=reinterpret_cast<Free>(GetProcAddress(dll,"ms3d_model_free"));
    if(!call||!release){FreeLibrary(dll);return 4;}
    test("Native layout capability exposes exactly nine complete operations",[]{
        const auto info=run("info",Json::array(),"native");
        require(info.at("layout_api_version")==1&&info.at("layout_operations").size()==9,"Batch layout capability is incomplete");
        require(info.at("workspace_geometry_api_version")==1,"Python winding compatibility capability is unavailable");
        require(info.at("python_required_by_library")==false,"Native layout requires Python");
    });
    test("Groups preserve first occurrence and lazy layout does not require geometry",[]{
        Json scene={{"parts",Json::array({Json{{"id","b"},{"group","B"}},Json{{"id","a"},{"group","A"}},Json{{"id","b2"},{"group","B"}}})}};
        require(run("groups",{scene})==Json::array({"B","A"}),"Group order or uniqueness changed");
        require(run("group_layout",{scene,"B"})==Json{{"x",0},{"y",0},{"z",0},{"yaw",0}},"Missing layout does not keep legacy zero defaults");
        const auto moved=run("move_group",{scene,"B",100,-70,30});
        require(moved==Json{{"x",100},{"y",-70},{"z",30},{"yaw",0}},"Move required unrelated geometry or changed local blanks");
        require(run("groups",{Json{{"parts",Json::array()}}}).empty(),"Empty scene has groups");
        require(run("contacts",{Json{{"parts",Json::array()}}}).empty(),"Empty scene has contacts");
    });
    test("All three construction planes retain their original coordinate order",[]{
        for(const auto& plane:{"XY","XZ","YZ"}){
            auto row=part("p","P",100,200,18,10,20,30);row["plane"]=plane;
            const auto box=run("group_bounds",{Json{{"parts",Json::array({row})}},"P"});
            point(box[0],{10,20,30});
            if(std::string(plane)=="XY")point(box[1],{110,220,48});
            else if(std::string(plane)=="XZ")point(box[1],{110,38,230});
            else point(box[1],{28,120,230});
        }
    });
    test("Local and world bounds include owner tagged accessories and invert room placement only",[]{
        auto row=part("p","P",100,200,30,10,20,5);row["layout"]={{"x",100},{"y",200},{"z",50},{"yaw",90}};
        Json scene={{"parts",Json::array({row})},{"accessories",Json::array({Json{{"owner","p"},{"vertices",Json::array({Json::array({120,190,45})})}}})}};
        const auto local=run("local_group_bounds",{scene,"P"});point(local[0],{-10,-20,-5});point(local[1],{110,220,35});
        const auto world=run("group_bounds",{scene,"P"});point(world[0],{-120,190,45});point(world[1],{120,310,85});
    });
    test("Group layout consistency keeps the strict 1e-7 threshold",[]{
        auto scene=boxes();auto duplicate=scene["parts"][1];duplicate["id"]="b2";duplicate["layout"]={{"x",1e-7}};scene["parts"].push_back(duplicate);
        require(invoke("group_layout",{scene,"B"}).at("ok")==true,"Exactly tolerated group placement was rejected");
        scene["parts"][2]["layout"]["x"]=1.00001e-7;
        require(invoke("group_layout",{scene,"B"}).at("ok")==false,"Inconsistent group placement was accepted");
    });
    test("Rotation preserves the centre and Python yaw modulo for negative turns",[]{
        auto scene=boxes();auto layout=run("rotate_group",{scene,"A",90});
        close_value(layout["x"],500,"Centre rotation X differs");close_value(layout["y"],-100,"Centre rotation Y differs");require(layout["yaw"]==90,"Quarter-turn yaw changed");
        scene["parts"][0]["layout"]=layout;
        auto restored=run("rotate_group",{scene,"A",-90});close_value(restored["x"],0,"Inverse rotation X differs");close_value(restored["y"],0,"Inverse rotation Y differs");require(restored["yaw"]==0,"Inverse rotation yaw differs");
        auto half=run("rotate_group",{boxes(),"A",-540,Json::array({0,0,0})});require(half["yaw"]==-180,"Negative modulo is not Python compatible");
        auto distant=boxes();distant["parts"][0]["layout"]={{"x",100000}};
        require(invoke("rotate_group",{distant,"A",0}).at("ok")==false,"Default rotation centre outside the finite coordinate range was accepted");
    });
    test("Explicit snap covers every room-axis direction without changing local dimensions",[]{
        for(const auto& direction:{"x+","x-","y+","y-","z+","z-"}){
            auto scene=boxes();auto layout=run("snap_group",{scene,"B","A",direction});scene["parts"][1]["layout"]=layout;
            const auto target=run("group_bounds",{scene,"A"}),source=run("group_bounds",{scene,"B"});
            const int axis=direction[0]=='x'?0:direction[0]=='y'?1:2;const bool positive=direction[1]=='+';
            close_value(source[positive?0:1][axis],target[positive?1:0][axis],"Snapped faces do not meet");
            require(run("contacts",{scene,"B"}).size()==1,"A snapped rectangular pair has incorrect contacts");
        }
    });
    test("Nearest magnet returns one complete batch proposal with tangent corrections",[]{
        const auto result=run("nearest_snap",{boxes(616,8,5),"B",25,true});
        require(result["info"]["target_group"]=="A"&&result["info"]["direction"]=="x+","Nearest target or direction changed");
        point(result["info"]["delta"],{-16,-8,-5});
        close_value(result["info"]["distance"],std::sqrt(345.),"Travel distance is incorrect");
        point(Json::array({result["layout"]["x"],result["layout"]["y"],result["layout"]["z"]}),{-16,-8,-5});
        require(run("nearest_snap",{boxes(),"B",25,false})["info"].is_null(),"Distant furniture teleported");
    });
    test("Concave notch is empty space and equal proposals keep the direction tie break",[]{
        auto scene=boxes(40,40,10);auto& target=scene["parts"][0];target=part("a","A",100,100,80);target["points"]=Json::array({Json::array({0,0}),Json::array({100,0}),Json::array({100,30}),Json::array({30,30}),Json::array({30,100}),Json::array({0,100})});target["corner_bottom"]=true;
        scene["parts"][1]=part("b","B",20,20,30,40,40,10);
        const auto result=run("nearest_snap",{scene,"B",25,false});
        require(result["info"]["direction"]=="x+","Equal notch proposals lost the lexical direction tie break");point(result["info"]["delta"],{-10,0,0});
        scene["parts"][1]["layout"]=result["layout"];
        require(run("contacts",{scene,"B"}).size()==1,"Notch inner face contact was lost");
        target["corner_bottom"]=false;target["shared_worktop"]=true;
        require(run("contacts",{scene,"B"}).size()==1,"Shared worktop did not retain its irregular footprint");
    });
    test("Unicode target tie break follows Python group ordering",[]{
        auto scene=boxes(601);scene["parts"][0]["group"]="Żółć";auto target=scene["parts"][0];target["id"]="z";target["group"]="Z";scene["parts"].push_back(target);
        const auto result=run("nearest_snap",{scene,"B",25,false});require(result["info"]["target_group"]=="Z","Equal-distance target tie break differs from Python");
    });
    test("Third cabinet blocks explicit and nearest snaps atomically",[]{
        auto scene=boxes(610);scene["parts"].push_back(part("c","C",600,400,800,600));
        require(invoke("snap_group",{scene,"B","A","x+"}).at("ok")==false,"Explicit placement passed through a third cabinet");
        require(run("nearest_snap",{scene,"B",25,true})["info"].is_null(),"Nearest proposal collided with a third cabinet");
    });
    test("Nonorthogonal nearest source returns before unrelated inconsistent layouts",[]{
        auto scene=boxes();scene["parts"][0]["layout"]={{"yaw",35}};
        auto duplicate=scene["parts"][1];duplicate["id"]="b2";duplicate["layout"]={{"x",1}};scene["parts"].push_back(duplicate);
        const auto result=run("nearest_snap",{scene,"A"});require(result["info"].is_null()&&result["layout"].is_null(),"Nonorthogonal source evaluated unrelated group consistency");
        require(invoke("contacts",{scene}).at("ok")==false,"Contacts ignored an inconsistent group it evaluates");
    });
    test("Contact gap tolerance includes the exact boundary and excludes a larger gap",[]{
        auto scene=boxes(600.125);
        require(run("contacts",{scene,nullptr,.125}).size()==1,"Exact contact gap tolerance was rejected");
        require(run("contacts",{scene,nullptr,.124999}).empty(),"Contact beyond tolerance was accepted");
    });
    test("Polygon overlap retains positive area, notch and degenerate legacy semantics",[]{
        const Json square={{0,0},{10,0},{10,10},{0,10}};
        require(run("polygons_overlap",{square,square},"workspace_geometry")==true,"Identical footprints do not overlap");
        require(run("polygons_overlap",{square,Json{{10,0},{20,0},{20,10},{10,10}}},"workspace_geometry")==false,"Touching edges overlap");
        require(run("polygons_overlap",{square,Json{{10,10},{20,10},{20,20},{10,20}}},"workspace_geometry")==false,"Touching vertices overlap");
        require(run("polygons_overlap",{Json::array(),square},"workspace_geometry")==false,"Empty polygon overlaps");
        require(run("polygons_overlap",{Json{{5,5}},square},"workspace_geometry")==true,"Legacy degenerate interior point behavior changed");
        const Json notch={{0,0},{20,0},{20,5},{5,5},{5,20},{0,20}};
        require(run("polygons_overlap",{notch,Json{{8,8},{12,8},{12,12},{8,12}}},"workspace_geometry")==false,"Empty concave notch was filled");
        const Json cancellation={{0.,0.},{100000.,0.},{0.,100000.},{-1e-13,0.},{0.,-100000.},{-100000.,0.}};
        const Json outside_probe={{49999.,-.0002},{50001.,-.0002},{50001.,-.00005},{49999.,-.00005}};
        require(run("polygons_overlap",{cancellation,outside_probe},"workspace_geometry")==false,
                "Shoelace cancellation changed orientation and incorrectly moved an inward probe outside the polygon");
        require(run("polygons_overlap",{cancellation,outside_probe,true,true},"workspace_geometry")==false,
                "Explicit floating point Python winding was not respected");
        const Json mixed={{0,0},{100000,0},{0,100000},{-1e-13,0},{0,-100000},{-100000,0}};
        require(run("polygons_overlap",{mixed,outside_probe,false,true},"workspace_geometry")==true,
                "Explicit mixed integer/float Python winding was lost when coordinates became doubles");
        const auto keyword_flags=invoke("polygons_overlap",{mixed,outside_probe},"workspace_geometry",Json{{"a_ccw",false},{"b_ccw",true}});
        require(keyword_flags.at("ok")==true&&keyword_flags.at("result")==true,"Keyword winding flags differ from positional flags");
        for(const auto& invalid_flag:Json::array({0,1,nullptr,"true"}))
            require(invoke("polygons_overlap",{mixed,outside_probe,invalid_flag,true},"workspace_geometry").at("ok")==false,
                    "Nonboolean winding flag was accepted");
        require(invoke("polygons_overlap",{mixed,outside_probe,false},"workspace_geometry",Json{{"a_ccw",false}}).at("ok")==false,
                "Duplicated positional and keyword winding argument was accepted");
    });
    test("Invalid public layout arguments report errors and native buffers are released",[]{
        const auto scene=boxes();
        for(const auto& direction:{"wrong","x","X+"})require(invoke("snap_group",{scene,"B","A",direction}).at("ok")==false,"Invalid direction accepted");
        require(invoke("snap_group",{scene,"A","A"}).at("ok")==false,"Furniture snapped to itself");
        require(invoke("nearest_snap",{scene,"B",-1}).at("ok")==false,"Negative tolerance accepted");
        require(invoke("nearest_snap",{scene,"B",1001}).at("ok")==false,"Excess tolerance accepted");
        require(invoke("group_bounds",{scene,"missing"}).at("ok")==false,"Missing group accepted");
        require(invoke("rotate_group",{scene,"A",30,Json::array({0,0})}).at("ok")==false,"Malformed rotation pivot accepted");
        require(run("info",Json::array(),"native")["outstanding_buffers"]==0,"Native layout response buffers leaked");
    });
    unsigned failed=0;for(const auto& record:records)if(record.at("ok")!=true)++failed;
    Json report={{"ok",failed==0},{"count",records.size()},{"failed",failed},{"checks",records},{"gui",false},{"process_uses_python",false},{"gpu",false}};
    const std::filesystem::path output(argv[2]);std::filesystem::create_directories(output.parent_path());std::ofstream stream(output,std::ios::binary);stream<<report.dump(2);stream.close();
    std::cout<<report.dump()<<'\n';FreeLibrary(dll);return failed?1:0;
}
