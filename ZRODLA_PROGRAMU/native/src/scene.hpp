#pragma once
#include "renderer.hpp"
#include <filesystem>
namespace mebel {
bool loadScene(const std::filesystem::path& path,Scene& scene,std::string& error);
bool loadSceneData(const std::uint8_t* bytes,std::uint64_t size,Scene& scene,std::string& error);
struct Camera {
    float yaw=-38,pitch=26,scale=.4f,panX=0,panY=0;
    Vec3 center{};
    void fit(const Scene& scene,std::uint32_t width,std::uint32_t height,bool room=false);
    Frame frame(const Scene& scene,std::uint32_t width,std::uint32_t height,bool xray,std::int32_t selected)const;
    std::array<Vec3,3> basis()const;
};
std::int32_t pickOwner(const Scene& scene,const Camera& camera,std::uint32_t width,std::uint32_t height,float x,float y);
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& text);
}
