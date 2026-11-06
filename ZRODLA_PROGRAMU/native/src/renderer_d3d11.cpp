#include "renderer.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace mebel {
namespace {
// A small COM owner keeps this backend independent of the Visual Studio runtime.
template<class T> class ComPtr {
    T* value_=nullptr;
public:
    ComPtr()=default;
    ~ComPtr(){ reset(); }
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
    ComPtr(ComPtr&& other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    ComPtr& operator=(ComPtr&& other)noexcept{
        if(this!=&other){ reset(); value_=std::exchange(other.value_,nullptr); }
        return *this;
    }
    T* get()const{return value_;}
    T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
    T** put(){ reset(); return &value_; }
    void reset(){ if(value_){ value_->Release(); value_=nullptr; } }
};

std::string utf8(const wchar_t* value){
    if(!value || !*value)return {};
    int length=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(length<=1)return {};
    std::string text(static_cast<std::size_t>(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),length,nullptr,nullptr);
    text.pop_back();
    return text;
}

std::string describe(HRESULT code){
    char hex[24];
    std::snprintf(hex,sizeof(hex),"0x%08lX",static_cast<unsigned long>(code));
    wchar_t* system=nullptr;
    DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|
        FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,static_cast<DWORD>(code),0,
        reinterpret_cast<wchar_t*>(&system),0,nullptr);
    std::string result=hex;
    if(length && system){
        auto text=utf8(system);
        while(!text.empty() && (text.back()=='\r' || text.back()=='\n' || text.back()==' '))text.pop_back();
        if(!text.empty())result+=" ("+text+")";
    }
    if(system)LocalFree(system);
    return result;
}

constexpr const char* shaderSource=R"HLSL(
cbuffer FrameBuffer : register(b0) {
    column_major float4x4 viewProjection;
    float4 lightDirection;
    float4 options;
};
struct Input {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    uint owner : OWNER;
    uint flags : FLAGS;
};
struct Surface {
    float4 position : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 color : COLOR;
    nointerpolation uint owner : OWNER;
    nointerpolation uint flags : FLAGS;
};
Surface vertexMain(Input input) {
    Surface output;
    output.position=mul(viewProjection,float4(input.position,1));
    output.world=input.position;
    output.normal=input.normal;
    output.color=input.color;
    output.owner=input.owner;
    output.flags=input.flags;
    return output;
}
float4 pixelMain(Surface input) : SV_TARGET {
    if((input.flags & 8u)!=0) {
        float3 cameraNear=-normalize(float3(viewProjection[2][0],viewProjection[2][1],viewProjection[2][2]));
        float facing=dot(input.normal,cameraNear);
        if(abs(input.normal.z)>0.5) { if(facing<=0.001)discard; }
        else { if(facing>=-0.001)discard; }
    }
    // Scene objects, room architecture and fittings remain readable in X-ray.
    bool opaqueReference=options.y>1.5 ? (input.flags & 32u)==0 : (input.flags & (4u|8u|16u))!=0;
    if(options.w < -0.5 && !opaqueReference)discard;
    if(options.w > 0.5 && opaqueReference)discard;
    float3 normal=input.normal*rsqrt(max(dot(input.normal,input.normal),0.00001));
    // Snapshot normals describe the CAD surfaces independently of API winding.
    if((input.flags & 8u)!=0 && dot(normal,-normalize(float3(viewProjection[2][0],viewProjection[2][1],viewProjection[2][2])))<0)normal=-normal;
    float3 light=lightDirection.xyz*rsqrt(max(dot(lightDirection.xyz,lightDirection.xyz),0.00001));
    float diffuse=saturate(dot(normal,light));
    float secondary=saturate(dot(normal,normalize(float3(0.65,0.25,0.7))));
    float3 color=saturate(input.color.rgb);
    if((input.flags & 2u)!=0) {
        // Stable grain uses millimetre scene coordinates, so it does not crawl on orbit.
        float wave=sin(input.world.z*0.031+sin(input.world.x*0.009)*1.7+
                       sin(input.world.y*0.011)*1.1);
        float fine=sin(input.world.z*0.31+sin(input.world.x*0.017)*2.0);
        color*=0.96+0.045*wave+0.012*fine;
    }
    bool emissive=(input.flags & 1u)!=0;
    color*=emissive ? 1.35 : (0.40+0.52*diffuse+0.16*secondary);
    color*=clamp(options.x,0.15,3.0);
    bool selected=(input.flags & 64u)!=0 || (options.z>=0 && abs(float(input.owner)-options.z)<0.5);
    if(selected)color=lerp(color,float3(1.0,0.62,0.13),0.28);
    float alpha=1;
    if(options.w > 0.5)alpha=selected ? 0.36 : 0.20;
    return float4(saturate(color),alpha);
}
)HLSL";

class Direct3D11Renderer final:public Renderer {
    HWND window_=nullptr;
    std::uint32_t width_=0,height_=0;
    bool suspended_=false,lost_=false,hasFrame_=false;
    std::vector<Vertex> retainedVertices_;
    UINT vertexCount_=0;
    Frame lastFrame_{};
    RenderStats stats_;
    ComPtr<IDXGIFactory1> factory_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11Texture2D> backBuffer_,depthTexture_,staging_;
    ComPtr<ID3D11RenderTargetView> target_;
    ComPtr<ID3D11DepthStencilView> depth_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertices_,constants_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depthOpaque_,depthXray_;
    ComPtr<ID3D11BlendState> blendOpaque_,blendXray_;

    bool checked(HRESULT result,const char* operation,std::string& error){
        if(SUCCEEDED(result))return true;
        error=std::string("Direct3D11: ")+operation+": "+describe(result);
        if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET ||
           result==DXGI_ERROR_DEVICE_HUNG || result==DXGI_ERROR_DRIVER_INTERNAL_ERROR){
            lost_=true;
            if(device_)error+="; device removal reason: "+describe(device_->GetDeviceRemovedReason());
        }
        return false;
    }

    void releaseTargets(){
        if(context_)context_->OMSetRenderTargets(0,nullptr,nullptr);
        staging_.reset();depth_.reset();depthTexture_.reset();target_.reset();backBuffer_.reset();
    }

    void releaseDevice(){
        if(context_){context_->ClearState();context_->Flush();}
        releaseTargets();
        vertices_.reset();constants_.reset();layout_.reset();
        vertexShader_.reset();pixelShader_.reset();raster_.reset();
        depthOpaque_.reset();depthXray_.reset();blendOpaque_.reset();blendXray_.reset();
        swapChain_.reset();context_.reset();device_.reset();factory_.reset();
        vertexCount_=0;hasFrame_=false;
    }

    HRESULT tryDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type){
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL actual{};
        constexpr UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT result=D3D11CreateDevice(adapter,type,nullptr,flags,levels,4,
            D3D11_SDK_VERSION,device_.put(),&actual,context_.put());
        // Older runtimes reject 11.1 in the requested list, even if 11.0 is supported.
        if(result==E_INVALIDARG)result=D3D11CreateDevice(adapter,type,nullptr,flags,levels+1,3,
            D3D11_SDK_VERSION,device_.put(),&actual,context_.put());
        return result;
    }

    bool createDevice(std::string& error){
        if(!checked(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(factory_.put())),"CreateDXGIFactory1",error))return false;
        ComPtr<IDXGIFactory6> preferred;
        factory_->QueryInterface(__uuidof(IDXGIFactory6),reinterpret_cast<void**>(preferred.put()));
        HRESULT result=E_FAIL;
        for(UINT index=0;;++index){
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT enumeration=preferred ? preferred->EnumAdapterByGpuPreference(index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,__uuidof(IDXGIAdapter1),
                reinterpret_cast<void**>(adapter.put())) : factory_->EnumAdapters1(index,adapter.put());
            if(enumeration==DXGI_ERROR_NOT_FOUND)break;
            if(FAILED(enumeration))break;
            DXGI_ADAPTER_DESC1 descriptor{};
            if(FAILED(adapter->GetDesc1(&descriptor)) || (descriptor.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            result=tryDevice(adapter.get(),D3D_DRIVER_TYPE_UNKNOWN);
            if(SUCCEEDED(result)){
                stats_.adapter=utf8(descriptor.Description);
                stats_.software=false;
                return true;
            }
            context_.reset();device_.reset();
        }
        // WARP is a compatibility path, explicitly identified as software in diagnostics.
        result=tryDevice(nullptr,D3D_DRIVER_TYPE_WARP);
        if(!checked(result,"D3D11CreateDevice (hardware and WARP unavailable)",error))return false;
        stats_.adapter="Microsoft WARP (software renderer)";
        stats_.software=true;
        return true;
    }

    bool createSwapChain(std::string& error){
        // Obtain the factory belonging to the selected device, including the WARP path.
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory1> actualFactory;
        if(!checked(device_->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgiDevice.put())),
            "QueryInterface IDXGIDevice",error) ||
           !checked(dxgiDevice->GetAdapter(adapter.put()),"GetAdapter",error) ||
           !checked(adapter->GetParent(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(actualFactory.put())),
            "GetParent DXGI factory",error))return false;
        factory_=std::move(actualFactory);
        ComPtr<IDXGIFactory2> modernFactory;
        factory_->QueryInterface(__uuidof(IDXGIFactory2),reinterpret_cast<void**>(modernFactory.put()));
        HRESULT result=E_FAIL;
        if(modernFactory){
            DXGI_SWAP_CHAIN_DESC1 descriptor{};
            descriptor.Width=width_;descriptor.Height=height_;
            descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count=1;
            descriptor.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            descriptor.BufferCount=2;
            descriptor.Scaling=DXGI_SCALING_STRETCH;
            descriptor.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
            descriptor.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
            ComPtr<IDXGISwapChain1> modernChain;
            result=modernFactory->CreateSwapChainForHwnd(device_.get(),window_,&descriptor,
                nullptr,nullptr,modernChain.put());
            if(FAILED(result)){
                descriptor.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                result=modernFactory->CreateSwapChainForHwnd(device_.get(),window_,&descriptor,
                    nullptr,nullptr,modernChain.put());
            }
            if(SUCCEEDED(result))result=modernChain->QueryInterface(__uuidof(IDXGISwapChain),
                reinterpret_cast<void**>(swapChain_.put()));
        }
        if(!swapChain_){
            DXGI_SWAP_CHAIN_DESC descriptor{};
            descriptor.BufferDesc.Width=width_;descriptor.BufferDesc.Height=height_;
            descriptor.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count=1;
            descriptor.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            descriptor.BufferCount=1;
            descriptor.OutputWindow=window_;descriptor.Windowed=TRUE;
            descriptor.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
            result=factory_->CreateSwapChain(device_.get(),&descriptor,swapChain_.put());
        }
        if(!checked(result,"CreateSwapChainForHwnd/CreateSwapChain",error))return false;
        factory_->MakeWindowAssociation(window_,DXGI_MWA_NO_ALT_ENTER);
        ComPtr<IDXGIDevice1> latency;
        if(SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice1),reinterpret_cast<void**>(latency.put()))))
            latency->SetMaximumFrameLatency(1);
        return createTargets(error);
    }

    bool createTargets(std::string& error){
        if(!checked(swapChain_->GetBuffer(0,__uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(backBuffer_.put())),"GetBuffer",error) ||
           !checked(device_->CreateRenderTargetView(backBuffer_.get(),nullptr,target_.put()),
            "CreateRenderTargetView",error))return false;
        D3D11_TEXTURE2D_DESC descriptor{};
        descriptor.Width=width_;descriptor.Height=height_;
        descriptor.MipLevels=1;descriptor.ArraySize=1;
        descriptor.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_DEFAULT;
        descriptor.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        return checked(device_->CreateTexture2D(&descriptor,nullptr,depthTexture_.put()),
            "CreateTexture2D depth",error) &&
            checked(device_->CreateDepthStencilView(depthTexture_.get(),nullptr,depth_.put()),
            "CreateDepthStencilView",error);
    }

    bool compile(const char* entry,const char* profile,ComPtr<ID3DBlob>& code,std::string& error){
        ComPtr<ID3DBlob> diagnostics;
        HRESULT result=D3DCompile(shaderSource,std::strlen(shaderSource),"MebelStudio3D_D3D11",
            nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,code.put(),diagnostics.put());
        if(FAILED(result)){
            checked(result,"D3DCompile",error);
            if(diagnostics)error+="; "+std::string(static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize());
            return false;
        }
        return true;
    }

    bool createPipeline(std::string& error){
        ComPtr<ID3DBlob> vs,ps;
        if(!compile("vertexMain","vs_4_0",vs,error) || !compile("pixelMain","ps_4_0",ps,error))return false;
        if(!checked(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,
            vertexShader_.put()),"CreateVertexShader",error) ||
           !checked(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,
            pixelShader_.put()),"CreatePixelShader",error))return false;
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"OWNER",0,DXGI_FORMAT_R32_UINT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"FLAGS",0,DXGI_FORMAT_R32_UINT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        if(!checked(device_->CreateInputLayout(elements,5,vs->GetBufferPointer(),vs->GetBufferSize(),
            layout_.put()),"CreateInputLayout",error))return false;
        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth=sizeof(Frame);
        constantDescription.Usage=D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(!checked(device_->CreateBuffer(&constantDescription,nullptr,constants_.put()),
            "CreateBuffer frame constants",error))return false;
        D3D11_RASTERIZER_DESC rasterDescription{};
        rasterDescription.FillMode=D3D11_FILL_SOLID;
        rasterDescription.CullMode=D3D11_CULL_NONE;
        rasterDescription.DepthClipEnable=TRUE;
        if(!checked(device_->CreateRasterizerState(&rasterDescription,raster_.put()),
            "CreateRasterizerState",error))return false;
        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.DepthEnable=TRUE;
        depthDescription.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        depthDescription.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        if(!checked(device_->CreateDepthStencilState(&depthDescription,depthOpaque_.put()),
            "CreateDepthStencilState opaque",error))return false;
        depthDescription.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        if(!checked(device_->CreateDepthStencilState(&depthDescription,depthXray_.put()),
            "CreateDepthStencilState X-ray",error))return false;
        D3D11_BLEND_DESC blendDescription{};
        auto& blend=blendDescription.RenderTarget[0];
        blend.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(!checked(device_->CreateBlendState(&blendDescription,blendOpaque_.put()),
            "CreateBlendState opaque",error))return false;
        blend.BlendEnable=TRUE;
        blend.SrcBlend=D3D11_BLEND_SRC_ALPHA;
        blend.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOp=D3D11_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D11_BLEND_ONE;
        blend.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        return checked(device_->CreateBlendState(&blendDescription,blendXray_.put()),
            "CreateBlendState X-ray",error);
    }

    bool uploadGpu(const std::vector<Vertex>& vertices,std::string& error){
        if(vertices.size()%3!=0){error="Direct3D11: scene vertex count is not a triangle list";return false;}
        if(vertices.size()>std::numeric_limits<UINT>::max()/sizeof(Vertex)){
            error="Direct3D11: scene exceeds the Direct3D11 vertex buffer limit";return false;
        }
        ComPtr<ID3D11Buffer> buffer;
        if(!vertices.empty()){
            D3D11_BUFFER_DESC descriptor{};
            descriptor.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(Vertex));
            descriptor.Usage=D3D11_USAGE_IMMUTABLE;
            descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem=vertices.data();
            if(!checked(device_->CreateBuffer(&descriptor,&initial,buffer.put()),
                "CreateBuffer retained scene",error))return false;
        }
        vertices_=std::move(buffer);
        vertexCount_=static_cast<UINT>(vertices.size());
        return true;
    }

    bool updateFrame(const Frame& frame,std::string& error){
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(!checked(context_->Map(constants_.get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),
            "Map frame constants",error))return false;
        std::memcpy(mapped.pData,&frame,sizeof(Frame));
        context_->Unmap(constants_.get(),0);
        return true;
    }

    bool render(const Frame& supplied,std::string& error){
        Frame frame=supplied;
        frame.options[3]=0;
        ID3D11RenderTargetView* target=target_.get();
        context_->OMSetRenderTargets(1,&target,depth_.get());
        const float background[]={0.075f,0.095f,0.135f,1};
        context_->ClearRenderTargetView(target_.get(),background);
        context_->ClearDepthStencilView(depth_.get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
        D3D11_VIEWPORT viewport{};
        viewport.Width=static_cast<float>(width_);viewport.Height=static_cast<float>(height_);
        viewport.MaxDepth=1;
        context_->RSSetViewports(1,&viewport);
        context_->RSSetState(raster_.get());
        context_->IASetInputLayout(layout_.get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* vertexBuffer=vertices_.get();
        const UINT stride=sizeof(Vertex),offset=0;
        context_->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
        context_->VSSetShader(vertexShader_.get(),nullptr,0);
        context_->PSSetShader(pixelShader_.get(),nullptr,0);
        ID3D11Buffer* constantBuffer=constants_.get();
        context_->VSSetConstantBuffers(0,1,&constantBuffer);
        context_->PSSetConstantBuffers(0,1,&constantBuffer);
        const float blendFactor[4]={0,0,0,0};
        context_->OMSetDepthStencilState(depthOpaque_.get(),0);
        context_->OMSetBlendState(blendOpaque_.get(),blendFactor,0xffffffffu);
        if(frame.options[1]>0.5f)frame.options[3]=-1;
        if(!updateFrame(frame,error))return false;
        if(vertexCount_)context_->Draw(vertexCount_,0);
        if(frame.options[1]>0.5f && vertexCount_){
            frame.options[3]=1;
            if(!updateFrame(frame,error))return false;
            context_->OMSetDepthStencilState(depthXray_.get(),0);
            context_->OMSetBlendState(blendXray_.get(),blendFactor,0xffffffffu);
            context_->Draw(vertexCount_,0);
        }
        return true;
    }

    bool rebuild(std::string& error){
        releaseDevice();lost_=false;
        return createDevice(error) && createSwapChain(error) && createPipeline(error) &&
            uploadGpu(retainedVertices_,error);
    }

public:
    ~Direct3D11Renderer()override{releaseDevice();}
    bool initialize(HWND window,std::uint32_t width,std::uint32_t height,std::string& error)override{
        error.clear();
        if(!window || !IsWindow(window)){error="Direct3D11: invalid native window handle";return false;}
        window_=window;
        suspended_=(width==0 || height==0);
        width_=std::max(1u,width);height_=std::max(1u,height);
        stats_=RenderStats{};
        retainedVertices_.clear();
        return rebuild(error);
    }

    bool upload(const std::vector<Vertex>& vertices,std::string& error)override{
        error.clear();
        if(!device_){error="Direct3D11: renderer is not initialized";return false;}
        if(!uploadGpu(vertices,error))return false;
        retainedVertices_=vertices;
        hasFrame_=false;
        return true;
    }

    bool resize(std::uint32_t width,std::uint32_t height,std::string& error)override{
        error.clear();
        if(!swapChain_){error="Direct3D11: renderer is not initialized";return false;}
        if(width==0 || height==0){suspended_=true;return true;}
        suspended_=false;
        if(width==width_ && height==height_ && target_)return true;
        releaseTargets();
        context_->ClearState();context_->Flush();
        width_=width;height_=height;hasFrame_=false;
        if(!checked(swapChain_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),
            "ResizeBuffers",error)){
            if(lost_)return rebuild(error);
            return false;
        }
        return createTargets(error);
    }

    bool draw(const Frame& frame,std::string& error)override{
        error.clear();
        if(!device_ || !target_){error="Direct3D11: renderer has no render target";return false;}
        if(suspended_)return true;
        for(float value:frame.viewProjection)if(!std::isfinite(value)){
            error="Direct3D11: frame matrix contains a non-finite value";return false;
        }
        const auto start=std::chrono::steady_clock::now();
        for(int attempt=0;attempt<2;++attempt){
            bool rendered=render(frame,error);
            if(rendered && checked(swapChain_->Present(0,0),"Present",error)){
                lastFrame_=frame;hasFrame_=true;
                stats_.submitMs=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                ++stats_.frameCounter;
                return true;
            }
            if(attempt!=0 || !lost_ || !rebuild(error))return false;
            error.clear();
        }
        return false;
    }

    bool readback(std::vector<std::uint8_t>& rgba,std::uint32_t& width,
        std::uint32_t& height,std::string& error)override{
        error.clear();rgba.clear();width=0;height=0;
        if(suspended_){error="Direct3D11: readback is unavailable while minimized";return false;}
        if(!device_ || !target_ || !hasFrame_){error="Direct3D11: draw a frame before diagnostic readback";return false;}
        // Flip presentation may discard back-buffer contents. Render the retained frame
        // again only for this explicit diagnostic request; normal frames never read back.
        if(!render(lastFrame_,error))return false;
        if(!staging_){
            D3D11_TEXTURE2D_DESC descriptor{};
            backBuffer_->GetDesc(&descriptor);
            descriptor.Usage=D3D11_USAGE_STAGING;descriptor.BindFlags=0;
            descriptor.CPUAccessFlags=D3D11_CPU_ACCESS_READ;descriptor.MiscFlags=0;
            if(!checked(device_->CreateTexture2D(&descriptor,nullptr,staging_.put()),
                "CreateTexture2D diagnostic staging",error))return false;
        }
        context_->CopyResource(staging_.get(),backBuffer_.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(!checked(context_->Map(staging_.get(),0,D3D11_MAP_READ,0,&mapped),
            "Map diagnostic image",error))return false;
        const std::size_t rowSize=static_cast<std::size_t>(width_)*4;
        rgba.resize(rowSize*height_);
        for(std::uint32_t y=0;y<height_;++y)
            std::memcpy(rgba.data()+static_cast<std::size_t>(y)*rowSize,
                static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,rowSize);
        context_->Unmap(staging_.get(),0);
        width=width_;height=height_;
        return true;
    }

    std::string name()const override{return "Direct3D11";}
    RenderStats stats()const override{return stats_;}
};
}
std::unique_ptr<Renderer> createD3D11Renderer(){return std::make_unique<Direct3D11Renderer>();}
}
