#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <unordered_set>
#include <sstream>

namespace mebel {
static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
static float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static bool finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
template<class T> static T read(std::istream& in){T value{};if(!in.read(reinterpret_cast<char*>(&value),sizeof value))throw std::runtime_error("Niepełny plik sceny.");return value;}
static std::string text(std::istream& in,std::uint32_t count){
    if(count>65536)throw std::runtime_error("Zbyt długi opis elementu.");
    std::string value(count,'\0');if(!in.read(value.data(),count))throw std::runtime_error("Niepełna tabela elementów.");
    if(value.find('\0')!=std::string::npos)throw std::runtime_error("Opis zawiera niedozwolone znaki.");return value;
}
static bool readScene(std::istream& in,std::uint64_t size,Scene& scene,std::string& error){
    try{
        if(size>512ull*1024*1024)throw std::runtime_error("Scena przekracza 512 MB.");
        char magic[8]{};
        if(!in.read(magic,8)||std::memcmp(magic,"MS3DNAT1",8))throw std::runtime_error("To nie jest plik sceny MebelStudio3D C++.");
        if(read<std::uint32_t>(in)!=1)throw std::runtime_error("Nieobsługiwana wersja sceny.");
        auto count=read<std::uint32_t>(in),owners=read<std::uint32_t>(in);
        if(count%3||count>6000000||owners>100000||(count&&!owners))throw std::runtime_error("Nieprawidłowa liczba powierzchni lub elementów.");
        Scene next;next.lo=read<Vec3>(in);next.hi=read<Vec3>(in);next.brightness=read<float>(in);
        if(!finite(next.lo)||!finite(next.hi)||!std::isfinite(next.brightness)||next.brightness<.1f||next.brightness>2)throw std::runtime_error("Nieprawidłowe wymiary lub jasność sceny.");
        if(next.lo.x>next.hi.x||next.lo.y>next.hi.y||next.lo.z>next.hi.z)throw std::runtime_error("Odwrócone granice sceny.");
        const std::uint32_t classifications[]{0,Fitting,SceneObject,Architecture};
        constexpr std::uint32_t classificationMask=Fitting|SceneObject|Architecture;
        std::unordered_set<std::string> identities;
        for(std::uint32_t i=0;i<owners;i++){
            Owner owner;owner.kind=read<std::uint32_t>(in);owner.flags=read<std::uint32_t>(in);
            auto idSize=read<std::uint32_t>(in),labelSize=read<std::uint32_t>(in);
            owner.lo=read<Vec3>(in);owner.hi=read<Vec3>(in);owner.id=text(in,idSize);owner.label=text(in,labelSize);
            if(owner.kind>3||owner.flags>127||!finite(owner.lo)||!finite(owner.hi)||owner.lo.x>owner.hi.x||owner.lo.y>owner.hi.y||owner.lo.z>owner.hi.z||owner.id.empty())throw std::runtime_error("Nieprawidłowy właściciel powierzchni.");
            if((owner.flags&classificationMask)!=classifications[owner.kind]||!identities.insert(owner.id).second)throw std::runtime_error("Powtórzony identyfikator lub niezgodny rodzaj elementu.");
            next.owners.push_back(std::move(owner));
        }
        const auto bytes=static_cast<std::uint64_t>(count)*sizeof(Vertex);
        const auto left=size-static_cast<std::uint64_t>(in.tellg());
        if(left!=bytes)throw std::runtime_error("Rozmiar bufora sceny jest niezgodny z nagłówkiem.");
        next.vertices.resize(count);if(!in.read(reinterpret_cast<char*>(next.vertices.data()),static_cast<std::streamsize>(bytes)))throw std::runtime_error("Brak wierzchołków sceny.");
        for(auto& v:next.vertices){
            const float normal=dot(v.normal,v.normal);
            if(!finite(v.position)||!finite(v.normal)||normal<.8f||normal>1.2f||v.owner>=owners||v.flags>127)throw std::runtime_error("Nieprawidłowa powierzchnia w scenie.");
            const auto& owner=next.owners[v.owner];
            if((v.flags&classificationMask)!=classifications[owner.kind]||(v.flags&~owner.flags))throw std::runtime_error("Powierzchnia ma niezgodny rodzaj elementu.");
            if(v.position.x<owner.lo.x-.1f||v.position.y<owner.lo.y-.1f||v.position.z<owner.lo.z-.1f||v.position.x>owner.hi.x+.1f||v.position.y>owner.hi.y+.1f||v.position.z>owner.hi.z+.1f)throw std::runtime_error("Wierzchołek wykracza poza granice elementu.");
            for(float c:v.color)if(!std::isfinite(c)||c<0||c>1)throw std::runtime_error("Nieprawidłowy kolor powierzchni.");
            if(v.position.x<next.lo.x-.1f||v.position.y<next.lo.y-.1f||v.position.z<next.lo.z-.1f||v.position.x>next.hi.x+.1f||v.position.y>next.hi.y+.1f||v.position.z>next.hi.z+.1f)throw std::runtime_error("Wierzchołek wykracza poza granice sceny.");
        }
        for(std::size_t i=0;i<next.vertices.size();i+=3)if(next.vertices[i].owner!=next.vertices[i+1].owner||next.vertices[i].owner!=next.vertices[i+2].owner||next.vertices[i].flags!=next.vertices[i+1].flags||next.vertices[i].flags!=next.vertices[i+2].flags)throw std::runtime_error("Trójkąt miesza elementy.");
        scene=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool loadScene(const std::filesystem::path& path,Scene& scene,std::string& error){
    try{auto size=std::filesystem::file_size(path);std::ifstream in(path,std::ios::binary);return readScene(in,size,scene,error);}
    catch(const std::exception& e){error=e.what();return false;}
}
bool loadSceneData(const std::uint8_t* bytes,std::uint64_t size,Scene& scene,std::string& error){
    if(!bytes||size>512ull*1024*1024){error="Nieprawidłowy bufor sceny.";return false;}
    try{std::istringstream in(std::string(reinterpret_cast<const char*>(bytes),static_cast<std::size_t>(size)),std::ios::binary);return readScene(in,size,scene,error);}
    catch(const std::exception& e){error=e.what();return false;}
}
std::array<Vec3,3> Camera::basis()const{
    const float a=yaw*3.14159265358979323846f/180,b=pitch*3.14159265358979323846f/180;
    const float ca=std::cos(a),sa=std::sin(a),cb=std::cos(b),sb=std::sin(b);
    return {Vec3{ca,-sa,0},Vec3{sa*sb,ca*sb,cb},Vec3{-sa*cb,-ca*cb,sb}};
}
void Camera::fit(const Scene& scene,std::uint32_t width,std::uint32_t height,bool room){
    Vec3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};bool found=false;
    for(auto& owner:scene.owners){if(!room&&(owner.flags&Architecture))continue;found=true;
        lo={std::min(lo.x,owner.lo.x),std::min(lo.y,owner.lo.y),std::min(lo.z,owner.lo.z)};
        hi={std::max(hi.x,owner.hi.x),std::max(hi.y,owner.hi.y),std::max(hi.z,owner.hi.z)};
    }
    if(!found){lo=scene.lo;hi=scene.hi;}center=mul(add(lo,hi),.5f);panX=panY=0;
    auto vectors=basis();float xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
    for(int mask=0;mask<8;mask++){
        Vec3 point{mask&1?hi.x:lo.x,mask&2?hi.y:lo.y,mask&4?hi.z:lo.z};point=sub(point,center);
        float x=dot(point,vectors[0]),y=dot(point,vectors[1]);xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);
    }
    scale=.84f*std::min(std::max(1u,width)/std::max(1.f,xmax-xmin),std::max(1u,height)/std::max(1.f,ymax-ymin));
}
Frame Camera::frame(const Scene& scene,std::uint32_t width,std::uint32_t height,bool xray,std::int32_t selected)const{
    Frame result;auto axes=basis();float radius=2000;
    for(int mask=0;mask<8;mask++){Vec3 p{mask&1?scene.hi.x:scene.lo.x,mask&2?scene.hi.y:scene.lo.y,mask&4?scene.hi.z:scene.lo.z};auto d=sub(p,center);radius=std::max(radius,std::sqrt(dot(d,d))+2000);}
    float sx=2*scale/std::max(1u,width),sy=2*scale/std::max(1u,height),sz=-1/(2*radius);
    for(int col=0;col<3;col++){
        const auto component=[col](Vec3 p){return col==0?p.x:col==1?p.y:p.z;};
        result.viewProjection[col*4]=component(axes[0])*sx;result.viewProjection[col*4+1]=component(axes[1])*sy;result.viewProjection[col*4+2]=component(axes[2])*sz;
    }
    result.viewProjection[12]=-dot(axes[0],center)*sx+2*panX/std::max(1u,width);
    result.viewProjection[13]=-dot(axes[1],center)*sy-2*panY/std::max(1u,height);
    result.viewProjection[14]=.5f-dot(axes[2],center)*sz;result.viewProjection[15]=1;
    result.options={scene.brightness,xray?1.f:0.f,static_cast<float>(selected),0};return result;
}
std::int32_t pickOwner(const Scene& scene,const Camera& camera,std::uint32_t width,std::uint32_t height,float x,float y){
    auto axes=camera.basis();float radius=10000;
    for(auto& p:scene.vertices){auto delta=sub(p.position,camera.center);radius=std::max(radius,std::sqrt(dot(delta,delta))+1000);}
    Vec3 start=add(camera.center,add(mul(axes[0],(x-width*.5f-camera.panX)/camera.scale),add(mul(axes[1],-(y-height*.5f-camera.panY)/camera.scale),mul(axes[2],radius))));
    Vec3 direction=mul(axes[2],-1);float closest=INFINITY;std::int32_t selected=-1;
    for(std::size_t i=0;i<scene.vertices.size();i+=3){
        const auto& a=scene.vertices[i];auto edge1=sub(scene.vertices[i+1].position,a.position),edge2=sub(scene.vertices[i+2].position,a.position);
        if(a.flags&Architecture){float facing=dot(a.normal,axes[2]);if(std::abs(a.normal.z)>.5f?facing<=.001f:facing>=-.001f)continue;}
        auto p=cross(direction,edge2);float determinant=dot(edge1,p);if(std::abs(determinant)<1e-7f)continue;
        float inv=1/determinant;auto s=sub(start,a.position);float u=dot(s,p)*inv;if(u<-.00001f||u>1.00001f)continue;
        auto q=cross(s,edge1);float v=dot(direction,q)*inv;if(v<-.00001f||u+v>1.00001f)continue;
        float t=dot(edge2,q)*inv;if(t>=0&&t<closest){closest=t;selected=static_cast<std::int32_t>(a.owner);}
    }return selected;
}
std::wstring widen(const std::string& utf8){if(utf8.empty())return {};int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);if(!size)return L"Niepoprawny tekst UTF-8";std::wstring out(size,0);MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(utf8.size()),out.data(),size);return out;}
std::string narrow(const std::wstring& text){if(text.empty())return {};int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string out(size,0);WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr);return out;}
}
