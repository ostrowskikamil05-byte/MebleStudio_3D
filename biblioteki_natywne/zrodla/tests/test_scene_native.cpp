#include "scene.hpp"
#include "native_embed.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace mebel;
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;

static void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static bool close(float a,float b,float tolerance=.001f){return std::abs(a-b)<=tolerance;}
static float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 minus(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
static bool contains(Vec3 lo,Vec3 hi,Vec3 p,float epsilon=.001f){return p.x>=lo.x-epsilon&&p.x<=hi.x+epsilon&&p.y>=lo.y-epsilon&&p.y<=hi.y+epsilon&&p.z>=lo.z-epsilon&&p.z<=hi.z+epsilon;}
static Bytes readFile(const fs::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read "+path.string());return Bytes(std::istreambuf_iterator<char>(in),{});}
static void writeFile(const fs::path& path,const Bytes& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(out),"Cannot write fixture");}
template<class T> static void append(Bytes& bytes,const T& value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),p,p+sizeof value);}
template<class T> static T at(const Bytes& bytes,std::size_t offset){require(offset+sizeof(T)<=bytes.size(),"Bad test fixture offset");T out{};std::memcpy(&out,bytes.data()+offset,sizeof out);return out;}
template<class T> static void put(Bytes& bytes,std::size_t offset,const T& value){require(offset+sizeof(T)<=bytes.size(),"Bad mutation offset");std::memcpy(bytes.data()+offset,&value,sizeof value);}
static std::size_t vertexOffset(const Bytes& bytes){std::size_t offset=48;auto owners=at<std::uint32_t>(bytes,16);for(std::uint32_t i=0;i<owners;i++){auto a=at<std::uint32_t>(bytes,offset+8),b=at<std::uint32_t>(bytes,offset+12);offset+=40+a+b;}return offset;}
static Bytes encode(const Scene& scene){
    Bytes result{'M','S','3','D','N','A','T','1'};append(result,std::uint32_t(1));append(result,std::uint32_t(scene.vertices.size()));append(result,std::uint32_t(scene.owners.size()));append(result,scene.lo);append(result,scene.hi);append(result,scene.brightness);
    for(const auto& o:scene.owners){append(result,o.kind);append(result,o.flags);append(result,std::uint32_t(o.id.size()));append(result,std::uint32_t(o.label.size()));append(result,o.lo);append(result,o.hi);result.insert(result.end(),o.id.begin(),o.id.end());result.insert(result.end(),o.label.begin(),o.label.end());}
    for(const auto& vertex:scene.vertices)append(result,vertex);return result;
}
static std::uint64_t fingerprint(const Scene& scene){const auto bytes=encode(scene);std::uint64_t value=1469598103934665603ull;for(auto byte:bytes){value^=byte;value*=1099511628211ull;}return value;}
struct Pixel{float x,y,z;};
static Pixel project(const Frame& frame,Vec3 p,float width,float height){
    const auto& m=frame.viewProjection;
    float x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
    float z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14],w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
    require(std::isfinite(w)&&std::abs(w)>1e-8f,"Invalid clip W");return {(x/w+1)*width*.5f,(1-y/w)*height*.5f,z/w};
}
static void addQuadXZ(Scene& scene,std::uint32_t owner,float y,float half,float normalY,std::uint32_t flags=0){
    std::array<Vec3,4> p{Vec3{-half,y,-half},Vec3{half,y,-half},Vec3{half,y,half},Vec3{-half,y,half}};
    if(normalY>0)std::reverse(p.begin(),p.end());
    for(int index:{0,1,2,0,2,3})scene.vertices.push_back(Vertex{p[index],{0,normalY,0},{.6f,.3f,.2f,1},owner,flags});
}
static Scene layers(){
    Scene scene;scene.lo={-50,-20,-50};scene.hi={50,20,50};
    scene.owners={Owner{"part:near","Near panel",0,0,{-50,-20,-50},{50,-20,50}},Owner{"part:far","Far panel",0,0,{-50,20,-50},{50,20,50}}};
    addQuadXZ(scene,1,20,50,-1);addQuadXZ(scene,0,-20,50,-1);return scene;
}
static std::string quoted(const std::string& text){std::string out="\"";for(unsigned char c:text){if(c=='\"'||c=='\\'){out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+='?';else out+=char(c);}return out+'\"';}
struct Result{std::string name,error;};
struct Suite{
    std::vector<Result> results;
    void run(const std::string& name,const std::function<void()>& test){std::string error;try{test();}catch(const std::exception& e){error=e.what();}results.push_back({name,error});std::cout<<(error.empty()?"PASS ":"FAIL ")<<name<<(error.empty()?"":" -- "+error)<<"\n";}
    int finish(const fs::path& output){
        unsigned failures=0;for(const auto& r:results)if(!r.error.empty())++failures;
        std::ofstream out(output);out<<"{\n  \"ok\": "<<(failures?"false":"true")<<",\n  \"tests\": "<<results.size()<<",\n  \"passed\": "<<results.size()-failures<<",\n  \"failed\": "<<failures<<",\n  \"gui_windows\": false,\n  \"checks\": [\n";
        for(std::size_t i=0;i<results.size();++i){const auto& r=results[i];out<<"    {\"name\": "<<quoted(r.name)<<", \"ok\": "<<(r.error.empty()?"true":"false")<<", \"error\": "<<quoted(r.error)<<"}"<<(i+1<results.size()?",":"")<<"\n";}
        out<<"  ]\n}\n";require(bool(out),"Cannot save report");std::cout<<"Tests: "<<results.size()<<", passed: "<<results.size()-failures<<", failed: "<<failures<<"\n";return failures?1:0;
    }
};

int main(int argc,char** argv){
    try{
        require(argc==3,"Usage: test_scene_native.exe <native/assets> <report.json>");
        fs::path assets=fs::absolute(argv[1]),report=fs::absolute(argv[2]);fs::create_directories(report.parent_path());
        const auto temporary=report.parent_path()/("native-scene-corrupt-"+std::to_string(GetCurrentProcessId())+".mscene");
        const auto demoBytes=readFile(assets/"demo_scena_0_12.mscene"),benchmarkBytes=readFile(assets/"benchmark_42_parts.mscene");
        const auto sourceDemo=assets/"DEMO_SCENA_0_12.mebel";
        const auto sourceBytes=readFile(sourceDemo);
        Scene demo,benchmark;std::string error;
        require(loadScene(assets/"demo_scena_0_12.mscene",demo,error),"Demo parse: "+error);
        require(loadScene(assets/"benchmark_42_parts.mscene",benchmark,error),"Benchmark parse: "+error);
        Suite suite;
        suite.run("Native ABI uses Vertex48 and Frame96",[]{require(sizeof(Vertex)==48&&sizeof(Frame)==96&&sizeof(Vec3)==12,"Native layout changed");std::uint32_t n=1;require(*reinterpret_cast<std::uint8_t*>(&n)==1,"Little endian target required");});
        suite.run("Saved demo parses panels fittings AGD and architecture",[&]{
            require(demo.vertices.size()==8784&&demo.owners.size()==34,"Unexpected demo counts");std::array<unsigned,4> kinds{};for(auto& o:demo.owners)++kinds[o.kind];require(kinds==std::array<unsigned,4>{21,6,2,5},"Demo owner categories changed");require(close(demo.brightness,.7f),"Demo lighting lost");
        });
        suite.run("Paired fixture preserves 42 boards and millimetre bounds",[&]{
            require(benchmark.vertices.size()==74880&&benchmark.owners.size()==60,"Unexpected benchmark counts");unsigned panels=0;for(auto& o:benchmark.owners)if(o.kind==0)++panels;require(panels==42,"Board count changed");require(close(benchmark.lo.x,0)&&close(benchmark.lo.y,-54)&&close(benchmark.lo.z,0)&&close(benchmark.hi.x,1850)&&close(benchmark.hi.y,1963)&&close(benchmark.hi.z,1000),"Snapshot units or extents changed");
        });
        suite.run("Owners have distinct picking IDs and encompass their own triangles",[&]{
            for(const auto* scene:{&demo,&benchmark}){std::set<std::string> ids;for(const auto& o:scene->owners)require(ids.insert(o.id).second,"Owner ID collision");for(const auto& v:scene->vertices){const auto& owner=scene->owners[v.owner];require(contains(owner.lo,owner.hi,v.position),"Owner bounds exclude an owned triangle");require((v.flags&~owner.flags)==0,"Vertex flags disagree with owner");require(contains(scene->lo,scene->hi,v.position),"Global bounds exclude a vertex");}}
        });
        suite.run("Real triangles have finite unit normals aligned with winding",[&]{
            for(const auto* scene:{&demo,&benchmark})for(std::size_t i=0;i<scene->vertices.size();i+=3){const auto& a=scene->vertices[i];Vec3 n=cross(minus(scene->vertices[i+1].position,a.position),minus(scene->vertices[i+2].position,a.position));require(finite(a.position)&&finite(a.normal),"Nonfinite geometry");require(close(dot(a.normal,a.normal),1,.00002f),"Nonunit normal");require(dot(n,a.normal)>0,"Normal points against winding");}
        });
        suite.run("UTF8 Polish labels roundtrip through Windows conversion",[]{std::string label="Żółta półka — łazienka";require(narrow(widen(label))==label,"Polish UTF8 label changed");});
        suite.run("Camera basis remains orthonormal through all octants",[]{
            for(float yaw:{-180.f,-91.f,-38.f,0.f,91.f,179.f})for(float pitch:{-90.f,-29.f,0.f,26.f,90.f}){Camera camera;camera.yaw=yaw;camera.pitch=pitch;const auto axes=camera.basis();for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)require(close(dot(axes[i],axes[j]),i==j?1.f:0.f,.000003f),"Camera axes are not orthonormal");}
        });
        for(const auto& view:std::vector<std::pair<std::string,std::pair<float,float>>>{{"front",{0,0}},{"top",{0,90}},{"side",{90,0}},{"isometric",{-38,26}}}){
            suite.run("Fitted "+view.first+" projection contains all furniture and clip depth",[&,view]{
                Camera camera;camera.yaw=view.second.first;camera.pitch=view.second.second;camera.panX=200;camera.panY=-100;camera.fit(benchmark,1300,720);require(camera.panX==0&&camera.panY==0,"Fit did not reset pan");require(camera.yaw==view.second.first&&camera.pitch==view.second.second,"Fit changed view orientation");const auto frame=camera.frame(benchmark,1300,720,false,-1);
                for(const auto& v:benchmark.vertices){auto p=project(frame,v.position,1300,720);require(p.x>=100&&p.x<=1200&&p.y>=50&&p.y<=670,"Fit clips model or loses padding");require(p.z>=0&&p.z<=1,"Vertex outside D3D/Vulkan clip depth");}
            });
        }
        suite.run("Column major matrix agrees with known front top and side axes",[&]{
            Camera camera;camera.scale=2;camera.center={13,17,19};camera.panX=37;camera.panY=-11;
            const Vec3 point{23,37,49};
            for(auto view:std::vector<std::array<float,4>>{{0,0,10,30},{0,90,10,20},{90,0,-20,30}}){camera.yaw=view[0];camera.pitch=view[1];auto p=project(camera.frame(benchmark,800,600,true,3),point,800,600);require(close(p.x,400+37+view[2]*2,.0001f)&&close(p.y,300-11-view[3]*2,.0001f),"Matrix convention differs from CAD camera");}
            const auto frame=camera.frame(benchmark,800,600,true,3);require(frame.options[0]==benchmark.brightness&&frame.options[1]==1&&frame.options[2]==3,"Frame options lost");
        });
        suite.run("Camera fit ignores room by default and allows explicit room fit",[&]{
            Camera furniture,room;furniture.fit(demo,1000,700);room.fit(demo,1000,700,true);require(furniture.scale>room.scale,"Architecture controls furniture fit");const auto frame=furniture.frame(demo,1000,700,false,-1);for(const auto& v:demo.vertices)if(!(v.flags&Architecture)){auto p=project(frame,v.position,1000,700);require(p.x>=0&&p.x<=1000&&p.y>=0&&p.y<=700,"Furniture clipped by fit");}
        });
        suite.run("Nearest front face wins ray picking regardless of triangle order",[]{
            Scene scene=layers();Camera camera;camera.yaw=camera.pitch=0;camera.fit(scene,800,600);require(pickOwner(scene,camera,800,600,400,300)==0,"Far panel selected through near panel");std::rotate(scene.vertices.begin(),scene.vertices.begin()+6,scene.vertices.end());require(pickOwner(scene,camera,800,600,400,300)==0,"Picking depends on triangle order");require(pickOwner(scene,camera,800,600,0,0)==-1,"Background returns a panel");
        });
        suite.run("Projected near depth is smaller and panned ray matches the matrix",[]{
            Scene scene=layers();Camera camera;camera.yaw=camera.pitch=0;camera.scale=2;camera.panX=37;camera.panY=-11;auto frame=camera.frame(scene,800,600,false,-1);auto nearPixel=project(frame,{0,-20,0},800,600),farPixel=project(frame,{0,20,0},800,600);require(nearPixel.z<farPixel.z&&nearPixel.z>=0&&farPixel.z<=1,"Native clip depth reverses near and far");require(pickOwner(scene,camera,800,600,nearPixel.x,nearPixel.y)==0,"Panned picking differs from projected face");
        });
        suite.run("Architecture cutaway hides the near wall from picking",[]{
            Scene scene=layers();scene.lo={-200,-200,-200};scene.hi={200,200,200};scene.owners.push_back(Owner{"architecture:near","Near wall",3,Architecture,{-200,-200,-200},{200,-200,200}});scene.owners.push_back(Owner{"architecture:far","Far wall",3,Architecture,{-200,200,-200},{200,200,200}});addQuadXZ(scene,2,-200,200,-1,Architecture);addQuadXZ(scene,3,200,200,1,Architecture);Camera camera;camera.yaw=camera.pitch=0;camera.fit(scene,800,600);require(pickOwner(scene,camera,800,600,400,300)==0,"Hidden near wall steals furniture picking");scene.vertices.erase(scene.vertices.begin(),scene.vertices.begin()+12);require(pickOwner(scene,camera,800,600,400,300)==3,"Visible rear wall cannot be selected");
        });
        suite.run("Architecture floor is pickable from above but cut away from below",[]{
            Scene scene;scene.lo={-100,-100,0};scene.hi={100,100,0};scene.owners.push_back(Owner{"architecture:floor","Floor",3,Architecture,scene.lo,scene.hi});for(auto p:std::array<Vec3,6>{Vec3{-100,-100,0},Vec3{100,-100,0},Vec3{100,100,0},Vec3{-100,-100,0},Vec3{100,100,0},Vec3{-100,100,0}})scene.vertices.push_back(Vertex{p,{0,0,1},{.5f,.5f,.5f,1},0,Architecture});Camera camera;camera.yaw=0;camera.pitch=90;camera.fit(scene,800,600,true);require(pickOwner(scene,camera,800,600,400,300)==0,"Floor lost from above");camera.pitch=-90;require(pickOwner(scene,camera,800,600,400,300)==-1,"Floor steals underside selection");
        });
        suite.run("Empty exporter-compatible snapshot loads and projects a finite background",[&]{
            Scene empty;writeFile(temporary,encode(empty));Scene prior=benchmark;require(loadScene(temporary,prior,error),"Empty snapshot refused: "+error);require(prior.vertices.empty()&&prior.owners.empty(),"Empty load retains prior model");Camera camera;camera.fit(prior,800,600);auto frame=camera.frame(prior,800,600,false,-1);for(auto value:frame.viewProjection)require(std::isfinite(value),"Empty scene camera is nonfinite");require(pickOwner(prior,camera,800,600,400,300)==-1,"Empty scene picks an owner");
        });
        suite.run("Zero-size viewport camera math remains finite",[&]{Camera camera;camera.fit(benchmark,0,0);auto frame=camera.frame(benchmark,0,0,false,-1);require(std::isfinite(camera.scale)&&camera.scale>0,"Invalid minimized scale");for(auto v:frame.viewProjection)require(std::isfinite(v),"Minimized frame has nonfinite values");});

        suite.run("Embedded C ABI matches ctypes x64 layout",[]{require(sizeof(NativeCamera)==44&&sizeof(NativeHit)==28&&sizeof(NativeOverlay)==72,"C ABI size mismatch");require(offsetof(NativeOverlay,coordinates)==24&&offsetof(NativeOverlay,text)==40&&offsetof(NativeOverlay,anchor)==64,"C ABI pointer alignment mismatch");});
        suite.run("Memory upload matches file snapshot exactly",[&]{Scene memory;require(loadSceneData(demoBytes.data(),demoBytes.size(),memory,error),error);require(fingerprint(memory)==fingerprint(demo),"Memory scene differs from file scene");});
        suite.run("Scoped ghost and multi-selection flags survive memory upload",[&]{Scene source=layers();source.owners[0].flags=Ghost|Selected;for(std::size_t i=6;i<12;i++)source.vertices[i].flags=Ghost|Selected;auto bytes=encode(source);Scene parsed;require(loadSceneData(bytes.data(),bytes.size(),parsed,error),error);require(parsed.owners[0].flags==(Ghost|Selected)&&parsed.owners[1].flags==0,"Scoped flags were lost");require(parsed.vertices[6].flags==(Ghost|Selected)&&parsed.vertices[0].flags==0,"Ghost scope leaked into another part");});
        suite.run("Invalid memory upload retains the prior valid scene",[&]{auto bad=demoBytes;bad.pop_back();Scene prior=benchmark;auto before=fingerprint(prior);require(!loadSceneData(bad.data(),bad.size(),prior,error)&&!error.empty(),"Truncated memory accepted");require(before==fingerprint(prior),"Failed memory upload changed scene");});
        suite.run("Null and oversized memory are rejected without dereference",[&]{Scene prior;require(!loadSceneData(nullptr,48,prior,error),"Null memory accepted");require(!loadSceneData(demoBytes.data(),513ull*1024*1024,prior,error),"Oversized memory accepted");});
        const auto dataOffset=vertexOffset(demoBytes);
        const auto original=fingerprint(benchmark);
        auto rejects=[&](const std::string& name,const std::function<void(Bytes&)>& mutate){suite.run("Parser rejects "+name+" transactionally",[&]{Bytes bad=demoBytes;mutate(bad);writeFile(temporary,bad);Scene prior=benchmark;std::string failure;require(!loadScene(temporary,prior,failure),"Corrupt scene accepted");require(!failure.empty(),"Missing parse diagnostic");require(fingerprint(prior)==original,"Failed parse replaced the prior scene");});};
        rejects("wrong magic",[](Bytes& b){b[0]='X';});
        rejects("unsupported version",[](Bytes& b){put(b,8,std::uint32_t(99));});
        rejects("incomplete header",[](Bytes& b){b.resize(11);});
        rejects("non-triangular vertex count",[](Bytes& b){put(b,12,std::uint32_t(8783));});
        rejects("excessive vertex count",[](Bytes& b){put(b,12,std::uint32_t(6000003));});
        rejects("vertices without owners",[](Bytes& b){put(b,16,std::uint32_t(0));});
        rejects("excessive owner count",[](Bytes& b){put(b,16,std::uint32_t(100001));});
        rejects("NaN scene bounds",[](Bytes& b){put(b,20,std::numeric_limits<float>::quiet_NaN());});
        rejects("reversed scene bounds",[](Bytes& b){put(b,20,99999.f);});
        rejects("NaN brightness",[](Bytes& b){put(b,44,std::numeric_limits<float>::quiet_NaN());});
        rejects("out-of-range brightness",[](Bytes& b){put(b,44,2.1f);});
        rejects("unknown owner kind",[](Bytes& b){put(b,48,std::uint32_t(4));});
        rejects("unknown owner flags",[](Bytes& b){put(b,52,std::uint32_t(128));});
        rejects("overlong owner text",[](Bytes& b){put(b,56,std::uint32_t(65537));});
        rejects("NaN owner bounds",[](Bytes& b){put(b,64,std::numeric_limits<float>::quiet_NaN());});
        rejects("truncated owner table",[](Bytes& b){b.resize(89);});
        rejects("truncated triangle data",[](Bytes& b){b.pop_back();});
        rejects("trailing triangle bytes",[](Bytes& b){b.push_back(0);});
        rejects("NaN vertex position",[&](Bytes& b){put(b,dataOffset,std::numeric_limits<float>::quiet_NaN());});
        rejects("infinite normal",[&](Bytes& b){put(b,dataOffset+12,std::numeric_limits<float>::infinity());});
        rejects("zero normal",[&](Bytes& b){for(unsigned i=0;i<3;i++)put(b,dataOffset+12+i*4,0.f);});
        rejects("NaN vertex color",[&](Bytes& b){put(b,dataOffset+24,std::numeric_limits<float>::quiet_NaN());});
        rejects("out-of-range vertex color",[&](Bytes& b){put(b,dataOffset+24,1.1f);});
        rejects("unknown vertex flags",[&](Bytes& b){put(b,dataOffset+44,std::uint32_t(128));});
        rejects("invalid owner index",[&](Bytes& b){put(b,dataOffset+40,std::uint32_t(demo.owners.size()));});
        rejects("triangle mixing owners",[&](Bytes& b){auto old=at<std::uint32_t>(b,dataOffset+40);put(b,dataOffset+48+40,(old+1)%std::uint32_t(demo.owners.size()));});
        rejects("triangle mixing flags",[&](Bytes& b){auto old=at<std::uint32_t>(b,dataOffset+44);put(b,dataOffset+48+44,old^std::uint32_t(1));});
        rejects("vertex outside global bounds",[&](Bytes& b){put(b,dataOffset,-100000.f);});
        rejects("owner bounds excluding its vertices",[](Bytes& b){for(unsigned i=0;i<6;i++)put(b,64+i*4,0.f);});
        rejects("owner classification inconsistent with flags",[](Bytes& b){put(b,52,std::uint32_t(Architecture));});
        rejects("owner flags losing vertex material metadata",[](Bytes& b){put(b,52,std::uint32_t(0));});
        rejects("architecture triangle losing its cutaway flag",[&](Bytes& b){
            auto found=std::find_if(demo.vertices.begin(),demo.vertices.end(),[](const Vertex& v){return bool(v.flags&Architecture);});require(found!=demo.vertices.end(),"Demo has no architecture fixture");const auto index=std::size_t(found-demo.vertices.begin());
            for(std::size_t i=index;i<index+3;i++){auto flags=at<std::uint32_t>(b,dataOffset+i*48+44);put(b,dataOffset+i*48+44,flags&~std::uint32_t(Architecture));}
        });
        suite.run("Parser rejects duplicate owner IDs transactionally",[&]{Scene duplicate=layers();duplicate.owners[1].id=duplicate.owners[0].id;writeFile(temporary,encode(duplicate));Scene prior=benchmark;require(!loadScene(temporary,prior,error),"Duplicate owner IDs accepted");require(fingerprint(prior)==original,"Duplicate parse replaced prior scene");});
        suite.run("Native tests leave snapshots and original production project byte-identical",[&]{require(readFile(assets/"demo_scena_0_12.mscene")==demoBytes,"Demo snapshot changed");require(readFile(assets/"benchmark_42_parts.mscene")==benchmarkBytes,"Benchmark snapshot changed");require(readFile(sourceDemo)==sourceBytes,"Source .mebel changed");});
        fs::remove(temporary);return suite.finish(report);
    }catch(const std::exception& e){std::cerr<<"Native test setup failed: "<<e.what()<<"\n";return 2;}
}
