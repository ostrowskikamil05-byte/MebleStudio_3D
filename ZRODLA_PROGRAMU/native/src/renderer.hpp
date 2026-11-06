#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mebel {
struct Vec3 { float x=0,y=0,z=0; };
// Binary scene snapshots use this exact 48-byte layout, little endian.
struct Vertex { Vec3 position,normal; std::array<float,4> color; std::uint32_t owner=0,flags=0; };
static_assert(sizeof(Vertex)==48);
enum VertexFlags : std::uint32_t { Emissive=1, Wood=2, SceneObject=4, Architecture=8, Fitting=16, Ghost=32, Selected=64 };
struct Owner { std::string id,label; std::uint32_t kind=0,flags=0; Vec3 lo,hi; };
struct Scene { std::vector<Vertex> vertices; std::vector<Owner> owners; Vec3 lo,hi; float brightness=1; };
struct Frame {
    // Column-major matrix, clip space Z in [0,1], CPU uses Windows screen Y.
    std::array<float,16> viewProjection{};
    std::array<float,4> lightDirection{-.35f,-.55f,.76f,1};
    std::array<float,4> options{1,0,-1,0}; // brightness, x-ray, selected owner, unused
};
static_assert(sizeof(Frame)==96);
struct RenderStats { double submitMs=0; std::uint64_t frameCounter=0; std::string adapter; bool software=false; };
class Renderer {
public:
    virtual ~Renderer()=default;
    virtual bool initialize(HWND window,std::uint32_t width,std::uint32_t height,std::string& error)=0;
    virtual bool upload(const std::vector<Vertex>& vertices,std::string& error)=0;
    virtual bool resize(std::uint32_t width,std::uint32_t height,std::string& error)=0;
    virtual bool draw(const Frame& frame,std::string& error)=0;
    virtual bool readback(std::vector<std::uint8_t>& rgba,std::uint32_t& width,std::uint32_t& height,std::string& error)=0;
    virtual std::string name()const=0;
    virtual RenderStats stats()const=0;
};
std::unique_ptr<Renderer> createD3D11Renderer();
std::unique_ptr<Renderer> createD3D12Renderer();
std::unique_ptr<Renderer> createVulkanRenderer();
}
