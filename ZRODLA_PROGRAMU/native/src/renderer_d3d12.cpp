#include "renderer.hpp"
#include <d3d12.h>
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
template<class T> class ComPtr {
    T* value_=nullptr;
public:
    ComPtr()=default;
    ~ComPtr(){reset();}
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
    ComPtr(ComPtr&& other)noexcept:value_(std::exchange(other.value_,nullptr)){}
    ComPtr& operator=(ComPtr&& other)noexcept{
        if(this!=&other){reset();value_=std::exchange(other.value_,nullptr);}return *this;
    }
    T* get()const{return value_;}
    T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
    T** put(){reset();return &value_;}
    void reset(){if(value_){value_->Release();value_=nullptr;}}
};
std::string utf8(const wchar_t* text){
    if(!text || !*text)return {};
    int length=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if(length<=1)return {};
    std::string result(static_cast<std::size_t>(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),length,nullptr,nullptr);
    result.pop_back();return result;
}
std::string describe(HRESULT result){
    char hex[24];std::snprintf(hex,sizeof(hex),"0x%08lX",static_cast<unsigned long>(result));
    wchar_t* system=nullptr;
    DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|
        FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,static_cast<DWORD>(result),0,
        reinterpret_cast<wchar_t*>(&system),0,nullptr);
    std::string text=hex;
    if(length && system){
        auto message=utf8(system);
        while(!message.empty() && (message.back()=='\r' || message.back()=='\n' || message.back()==' '))message.pop_back();
        if(!message.empty())text+=" ("+message+")";
    }
    if(system)LocalFree(system);return text;
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

D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type){
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type=type;properties.CreationNodeMask=1;properties.VisibleNodeMask=1;
    return properties;
}
D3D12_RESOURCE_DESC bufferDescription(UINT64 bytes){
    D3D12_RESOURCE_DESC descriptor{};
    descriptor.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    descriptor.Width=bytes;descriptor.Height=1;descriptor.DepthOrArraySize=1;descriptor.MipLevels=1;
    descriptor.SampleDesc.Count=1;descriptor.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return descriptor;
}
class Direct3D12Renderer final:public Renderer {
    static constexpr UINT frameCount=2;
    static constexpr UINT frameConstantStride=512;
    HWND window_=nullptr;
    std::uint32_t width_=1,height_=1;
    bool suspended_=false,lost_=false,hasFrame_=false;
    std::vector<Vertex> retainedVertices_;
    UINT vertexCount_=0;
    Frame lastFrame_{};
    RenderStats stats_;
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_,dsvHeap_;
    std::array<ComPtr<ID3D12Resource>,frameCount> targets_;
    ComPtr<ID3D12Resource> depth_,vertices_,constants_,readback_;
    ComPtr<ID3D12RootSignature> signature_;
    ComPtr<ID3D12PipelineState> opaquePipeline_,xrayPipeline_;
    std::array<ComPtr<ID3D12CommandAllocator>,frameCount> allocators_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    std::array<UINT64,frameCount> frameFences_{};
    UINT64 nextFence_=0;
    HANDLE fenceEvent_=nullptr;
    UINT rtvStride_=0;
    std::uint8_t* mappedConstants_=nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    UINT64 readbackBytes_=0;

    bool checked(HRESULT result,const char* operation,std::string& error){
        if(SUCCEEDED(result))return true;
        error=std::string("Direct3D12: ")+operation+": "+describe(result);
        if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET ||
           result==DXGI_ERROR_DEVICE_HUNG || result==DXGI_ERROR_DRIVER_INTERNAL_ERROR){
            lost_=true;
            if(device_)error+="; device removal reason: "+describe(device_->GetDeviceRemovedReason());
        }
        return false;
    }

    bool waitFence(UINT64 value,std::string& error){
        if(value==0)return true;
        UINT64 completed=fence_->GetCompletedValue();
        if(completed==std::numeric_limits<UINT64>::max()){
            HRESULT reason=device_->GetDeviceRemovedReason();
            if(SUCCEEDED(reason))reason=DXGI_ERROR_DEVICE_REMOVED;
            return checked(reason,"GPU fence reports a removed device",error);
        }
        if(completed>=value)return true;
        if(!checked(fence_->SetEventOnCompletion(value,fenceEvent_),"SetEventOnCompletion",error))return false;
        DWORD result=WaitForSingleObject(fenceEvent_,10000);
        if(result==WAIT_OBJECT_0)return true;
        HRESULT reason=device_->GetDeviceRemovedReason();
        if(FAILED(reason))return checked(reason,"GPU fence wait",error);
        error=result==WAIT_TIMEOUT ? "Direct3D12: GPU fence wait timed out after 10 seconds" :
            "Direct3D12: GPU fence wait failed: "+describe(HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }

    bool signal(UINT index,std::string& error){
        const UINT64 value=++nextFence_;
        if(!checked(queue_->Signal(fence_.get(),value),"Signal GPU fence",error))return false;
        frameFences_[index]=value;return true;
    }

    bool waitGpu(std::string& error){
        if(!queue_ || !fence_)return true;
        const UINT64 value=++nextFence_;
        if(!checked(queue_->Signal(fence_.get(),value),"Signal idle fence",error) || !waitFence(value,error))return false;
        frameFences_.fill(0);return true;
    }

    void releaseTargets(){
        readback_.reset();readbackBytes_=0;
        for(auto& target:targets_)target.reset();
        depth_.reset();
    }

    void releaseDevice(){
        if(queue_ && fence_ && fenceEvent_ && !lost_){std::string ignored;waitGpu(ignored);}
        releaseTargets();
        if(constants_ && mappedConstants_)constants_->Unmap(0,nullptr);
        mappedConstants_=nullptr;
        vertices_.reset();constants_.reset();commands_.reset();
        for(auto& allocator:allocators_)allocator.reset();
        opaquePipeline_.reset();xrayPipeline_.reset();signature_.reset();
        rtvHeap_.reset();dsvHeap_.reset();swapChain_.reset();queue_.reset();fence_.reset();
        device_.reset();factory_.reset();
        if(fenceEvent_){CloseHandle(fenceEvent_);fenceEvent_=nullptr;}
        frameFences_.fill(0);nextFence_=0;vertexCount_=0;vertexView_={};hasFrame_=false;
    }

    bool createDevice(std::string& error){
        if(!checked(CreateDXGIFactory1(__uuidof(IDXGIFactory4),reinterpret_cast<void**>(factory_.put())),
            "CreateDXGIFactory4",error))return false;
        ComPtr<IDXGIFactory6> preferred;
        factory_->QueryInterface(__uuidof(IDXGIFactory6),reinterpret_cast<void**>(preferred.put()));
        HRESULT result=E_FAIL;
        // The second pass also handles drivers which cannot enumerate GPU preferences.
        const unsigned passes=preferred ? 2 : 1;
        for(unsigned pass=0;pass<passes;++pass){
            for(UINT index=0;;++index){
                ComPtr<IDXGIAdapter1> adapter;
                HRESULT enumeration=preferred && pass==0 ? preferred->EnumAdapterByGpuPreference(index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,__uuidof(IDXGIAdapter1),
                    reinterpret_cast<void**>(adapter.put())) : factory_->EnumAdapters1(index,adapter.put());
                if(enumeration==DXGI_ERROR_NOT_FOUND || FAILED(enumeration))break;
                DXGI_ADAPTER_DESC1 descriptor{};
                if(FAILED(adapter->GetDesc1(&descriptor)) || (descriptor.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))continue;
                result=D3D12CreateDevice(adapter.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),
                    reinterpret_cast<void**>(device_.put()));
                if(SUCCEEDED(result)){
                    stats_.adapter=utf8(descriptor.Description);stats_.software=false;return true;
                }
            }
        }
        ComPtr<IDXGIAdapter> warp;
        if(!checked(factory_->EnumWarpAdapter(__uuidof(IDXGIAdapter),reinterpret_cast<void**>(warp.put())),
            "EnumWarpAdapter after hardware Direct3D12 initialization failed",error))return false;
        result=D3D12CreateDevice(warp.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),
            reinterpret_cast<void**>(device_.put()));
        if(!checked(result,"D3D12CreateDevice (hardware and WARP unavailable)",error))return false;
        stats_.adapter="Microsoft WARP (software renderer)";stats_.software=true;return true;
    }

    bool createQueueAndSwapChain(std::string& error){
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(!checked(device_->CreateCommandQueue(&queueDescription,__uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(queue_.put())),"CreateCommandQueue",error))return false;
        DXGI_SWAP_CHAIN_DESC1 descriptor{};
        descriptor.Width=width_;descriptor.Height=height_;
        descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;descriptor.SampleDesc.Count=1;
        descriptor.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        descriptor.BufferCount=frameCount;descriptor.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        descriptor.Scaling=DXGI_SCALING_STRETCH;descriptor.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        if(!checked(factory_->CreateSwapChainForHwnd(queue_.get(),window_,&descriptor,nullptr,nullptr,chain.put()),
            "CreateSwapChainForHwnd",error) ||
           !checked(chain->QueryInterface(__uuidof(IDXGISwapChain3),reinterpret_cast<void**>(swapChain_.put())),
            "QueryInterface IDXGISwapChain3",error))return false;
        factory_->MakeWindowAssociation(window_,DXGI_MWA_NO_ALT_ENTER);
        swapChain_->SetMaximumFrameLatency(1);
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heapDescription.NumDescriptors=frameCount;
        if(!checked(device_->CreateDescriptorHeap(&heapDescription,__uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void**>(rtvHeap_.put())),"CreateDescriptorHeap RTV",error))return false;
        heapDescription.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heapDescription.NumDescriptors=1;
        if(!checked(device_->CreateDescriptorHeap(&heapDescription,__uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void**>(dsvHeap_.put())),"CreateDescriptorHeap DSV",error))return false;
        rtvStride_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return createTargets(error);
    }

    bool createTargets(std::string& error){
        auto handle=rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        for(UINT index=0;index<frameCount;++index){
            if(!checked(swapChain_->GetBuffer(index,__uuidof(ID3D12Resource),
                reinterpret_cast<void**>(targets_[index].put())),"GetBuffer",error))return false;
            device_->CreateRenderTargetView(targets_[index].get(),nullptr,handle);handle.ptr+=rtvStride_;
        }
        auto heap=heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC descriptor{};
        descriptor.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        descriptor.Width=width_;descriptor.Height=height_;descriptor.DepthOrArraySize=1;descriptor.MipLevels=1;
        descriptor.Format=DXGI_FORMAT_D32_FLOAT;descriptor.SampleDesc.Count=1;
        descriptor.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=descriptor.Format;clear.DepthStencil.Depth=1;
        if(!checked(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&descriptor,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,__uuidof(ID3D12Resource),
            reinterpret_cast<void**>(depth_.put())),"CreateCommittedResource depth",error))return false;
        device_->CreateDepthStencilView(depth_.get(),nullptr,dsvHeap_->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    bool compile(const char* entry,const char* profile,ComPtr<ID3DBlob>& code,std::string& error){
        ComPtr<ID3DBlob> messages;
        HRESULT result=D3DCompile(shaderSource,std::strlen(shaderSource),"MebelStudio3D_D3D12",
            nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,code.put(),messages.put());
        if(!checked(result,"D3DCompile",error)){
            if(messages)error+="; "+std::string(static_cast<const char*>(messages->GetBufferPointer()),messages->GetBufferSize());
            return false;
        }
        return true;
    }

    bool createPipeline(std::string& error){
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameter.Descriptor.ShaderRegister=0;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC signatureDescription{};
        signatureDescription.NumParameters=1;signatureDescription.pParameters=&parameter;
        signatureDescription.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized,messages;
        if(!checked(D3D12SerializeRootSignature(&signatureDescription,D3D_ROOT_SIGNATURE_VERSION_1,
            serialized.put(),messages.put()),"D3D12SerializeRootSignature",error)){
            if(messages)error+="; "+std::string(static_cast<const char*>(messages->GetBufferPointer()),messages->GetBufferSize());
            return false;
        }
        if(!checked(device_->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),
            __uuidof(ID3D12RootSignature),reinterpret_cast<void**>(signature_.put())),"CreateRootSignature",error))return false;
        ComPtr<ID3DBlob> vs,ps;
        if(!compile("vertexMain","vs_5_0",vs,error) || !compile("pixelMain","ps_5_0",ps,error))return false;
        const D3D12_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"OWNER",0,DXGI_FORMAT_R32_UINT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"FLAGS",0,DXGI_FORMAT_R32_UINT,0,44,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature=signature_.get();
        pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pipeline.InputLayout={elements,5};pipeline.SampleMask=std::numeric_limits<UINT>::max();
        pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable=TRUE;
        pipeline.RasterizerState.ConservativeRaster=D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pipeline.DepthStencilState.DepthEnable=TRUE;
        pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pipeline.DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;
        pipeline.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
        auto& front=pipeline.DepthStencilState.FrontFace;
        front.StencilFailOp=front.StencilDepthFailOp=front.StencilPassOp=D3D12_STENCIL_OP_KEEP;
        front.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;pipeline.DepthStencilState.BackFace=front;
        auto& blend=pipeline.BlendState.RenderTarget[0];
        blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline.DSVFormat=DXGI_FORMAT_D32_FLOAT;pipeline.SampleDesc.Count=1;
        if(!checked(device_->CreateGraphicsPipelineState(&pipeline,__uuidof(ID3D12PipelineState),
            reinterpret_cast<void**>(opaquePipeline_.put())),"CreateGraphicsPipelineState opaque",error))return false;
        pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
        blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
        return checked(device_->CreateGraphicsPipelineState(&pipeline,__uuidof(ID3D12PipelineState),
            reinterpret_cast<void**>(xrayPipeline_.put())),"CreateGraphicsPipelineState X-ray",error);
    }

    bool createCommands(std::string& error){
        for(auto& allocator:allocators_)if(!checked(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),reinterpret_cast<void**>(allocator.put())),"CreateCommandAllocator",error))return false;
        if(!checked(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators_[0].get(),
            opaquePipeline_.get(),__uuidof(ID3D12GraphicsCommandList),reinterpret_cast<void**>(commands_.put())),
            "CreateCommandList",error) || !checked(commands_->Close(),"Close initial command list",error))return false;
        if(!checked(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),
            reinterpret_cast<void**>(fence_.put())),"CreateFence",error))return false;
        fenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!fenceEvent_){error="Direct3D12: CreateEvent failed: "+describe(HRESULT_FROM_WIN32(GetLastError()));return false;}
        auto heap=heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        auto descriptor=bufferDescription(frameCount*frameConstantStride);
        if(!checked(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&descriptor,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),
            reinterpret_cast<void**>(constants_.put())),"CreateCommittedResource frame constants",error))return false;
        D3D12_RANGE noReads{0,0};
        return checked(constants_->Map(0,&noReads,reinterpret_cast<void**>(&mappedConstants_)),
            "Map persistent frame constants",error);
    }

    void transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=resource;
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=from;barrier.Transition.StateAfter=to;
        commands_->ResourceBarrier(1,&barrier);
    }

    bool begin(UINT index,std::string& error){
        return waitFence(frameFences_[index],error) && checked(allocators_[index]->Reset(),
            "Reset completed-frame command allocator",error) &&
            checked(commands_->Reset(allocators_[index].get(),opaquePipeline_.get()),"Reset command list",error);
    }

    bool execute(UINT index,std::string& error){
        if(!checked(commands_->Close(),"Close recorded command list",error))return false;
        ID3D12CommandList* lists[]={commands_.get()};queue_->ExecuteCommandLists(1,lists);
        return signal(index,error);
    }

    bool uploadGpu(const std::vector<Vertex>& vertices,std::string& error){
        if(vertices.size()%3!=0){error="Direct3D12: scene vertex count is not a triangle list";return false;}
        if(vertices.size()>std::numeric_limits<UINT>::max()/sizeof(Vertex)){
            error="Direct3D12: scene exceeds the vertex buffer view size limit";return false;
        }
        if(!waitGpu(error))return false;
        ComPtr<ID3D12Resource> target,upload;
        const UINT bytes=static_cast<UINT>(vertices.size()*sizeof(Vertex));
        if(bytes){
            auto descriptor=bufferDescription(bytes);
            auto defaultHeap=heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            auto uploadHeap=heapProperties(D3D12_HEAP_TYPE_UPLOAD);
            if(!checked(device_->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&descriptor,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),reinterpret_cast<void**>(target.put())),
                "CreateCommittedResource retained default vertices",error) ||
               !checked(device_->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&descriptor,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),reinterpret_cast<void**>(upload.put())),
                "CreateCommittedResource vertex upload",error))return false;
            void* mapped=nullptr;D3D12_RANGE noReads{0,0};
            if(!checked(upload->Map(0,&noReads,&mapped),"Map vertex upload",error))return false;
            std::memcpy(mapped,vertices.data(),bytes);D3D12_RANGE written{0,bytes};upload->Unmap(0,&written);
            const UINT index=swapChain_->GetCurrentBackBufferIndex();
            if(!begin(index,error))return false;
            commands_->CopyBufferRegion(target.get(),0,upload.get(),0,bytes);
            transition(target.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            if(!execute(index,error) || !waitFence(frameFences_[index],error))return false;
        }
        vertices_=std::move(target);vertexCount_=static_cast<UINT>(vertices.size());
        vertexView_={};
        if(vertices_){vertexView_.BufferLocation=vertices_->GetGPUVirtualAddress();vertexView_.SizeInBytes=bytes;
            vertexView_.StrideInBytes=sizeof(Vertex);}
        return true;
    }

    void recordFrame(UINT index,const Frame& supplied){
        transition(targets_[index].get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        D3D12_RECT scissor{0,0,static_cast<LONG>(width_),static_cast<LONG>(height_)};
        commands_->RSSetViewports(1,&viewport);commands_->RSSetScissorRects(1,&scissor);
        auto rtv=rtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=static_cast<SIZE_T>(index)*rtvStride_;
        auto dsv=dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        const float background[]={0.075f,0.095f,0.135f,1};
        commands_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        commands_->ClearRenderTargetView(rtv,background,0,nullptr);
        commands_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        commands_->SetGraphicsRootSignature(signature_.get());
        commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if(vertexCount_)commands_->IASetVertexBuffers(0,1,&vertexView_);
        Frame frame=supplied;frame.options[3]=frame.options[1]>0.5f ? -1.0f : 0.0f;
        const UINT offset=index*frameConstantStride;
        std::memcpy(mappedConstants_+offset,&frame,sizeof(Frame));
        commands_->SetPipelineState(opaquePipeline_.get());
        commands_->SetGraphicsRootConstantBufferView(0,constants_->GetGPUVirtualAddress()+offset);
        if(vertexCount_)commands_->DrawInstanced(vertexCount_,1,0,0);
        if(frame.options[1]>0.5f && vertexCount_){
            frame.options[3]=1;std::memcpy(mappedConstants_+offset+256,&frame,sizeof(Frame));
            commands_->SetPipelineState(xrayPipeline_.get());
            commands_->SetGraphicsRootConstantBufferView(0,constants_->GetGPUVirtualAddress()+offset+256);
            commands_->DrawInstanced(vertexCount_,1,0,0);
        }
    }

    bool rebuild(std::string& error){
        releaseDevice();lost_=false;
        return createDevice(error) && createQueueAndSwapChain(error) && createPipeline(error) &&
            createCommands(error) && uploadGpu(retainedVertices_,error);
    }

public:
    ~Direct3D12Renderer()override{releaseDevice();}
    bool initialize(HWND window,std::uint32_t width,std::uint32_t height,std::string& error)override{
        error.clear();
        if(!window || !IsWindow(window)){error="Direct3D12: invalid native window handle";return false;}
        window_=window;suspended_=(width==0 || height==0);
        width_=std::max(1u,width);height_=std::max(1u,height);
        stats_=RenderStats{};retainedVertices_.clear();return rebuild(error);
    }
    bool upload(const std::vector<Vertex>& vertices,std::string& error)override{
        error.clear();
        if(!device_){error="Direct3D12: renderer is not initialized";return false;}
        if(!uploadGpu(vertices,error))return false;
        retainedVertices_=vertices;hasFrame_=false;return true;
    }
    bool resize(std::uint32_t width,std::uint32_t height,std::string& error)override{
        error.clear();
        if(!swapChain_){error="Direct3D12: renderer is not initialized";return false;}
        if(width==0 || height==0){suspended_=true;return true;}
        suspended_=false;
        if(width==width_ && height==height_ && depth_)return true;
        if(!waitGpu(error))return false;
        releaseTargets();width_=width;height_=height;hasFrame_=false;
        if(!checked(swapChain_->ResizeBuffers(frameCount,width,height,DXGI_FORMAT_UNKNOWN,0),
            "ResizeBuffers",error))return lost_ ? rebuild(error) : false;
        return createTargets(error);
    }
    bool draw(const Frame& frame,std::string& error)override{
        error.clear();
        if(!device_ || !depth_){error="Direct3D12: renderer has no render target";return false;}
        if(suspended_)return true;
        for(float value:frame.viewProjection)if(!std::isfinite(value)){
            error="Direct3D12: frame matrix contains a non-finite value";return false;
        }
        const auto start=std::chrono::steady_clock::now();
        for(int attempt=0;attempt<2;++attempt){
            UINT index=swapChain_->GetCurrentBackBufferIndex();
            bool okay=begin(index,error);
            if(okay){
                recordFrame(index,frame);
                transition(targets_[index].get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
                okay=execute(index,error) && checked(swapChain_->Present(0,0),"Present",error);
            }
            if(okay){
                hasFrame_=true;lastFrame_=frame;
                stats_.submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                ++stats_.frameCounter;return true;
            }
            if(attempt!=0 || !lost_ || !rebuild(error))return false;
            error.clear();
        }
        return false;
    }
    bool readback(std::vector<std::uint8_t>& rgba,std::uint32_t& width,std::uint32_t& height,
        std::string& error)override{
        error.clear();rgba.clear();width=0;height=0;
        if(suspended_){error="Direct3D12: readback is unavailable while minimized";return false;}
        if(!device_ || !depth_ || !hasFrame_){error="Direct3D12: draw a frame before diagnostic readback";return false;}
        const UINT index=swapChain_->GetCurrentBackBufferIndex();
        if(!readback_){
            auto texture=targets_[index]->GetDesc();
            device_->GetCopyableFootprints(&texture,0,1,0,&footprint_,nullptr,nullptr,&readbackBytes_);
            auto heap=heapProperties(D3D12_HEAP_TYPE_READBACK);auto descriptor=bufferDescription(readbackBytes_);
            if(!checked(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&descriptor,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),reinterpret_cast<void**>(readback_.put())),
                "CreateCommittedResource diagnostic readback",error))return false;
        }
        // Re-render only on explicit capture: flip presentation may discard old pixels.
        if(!begin(index,error))return false;
        recordFrame(index,lastFrame_);
        transition(targets_[index].get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION destination{},source{};
        destination.pResource=readback_.get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint=footprint_;
        source.pResource=targets_[index].get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands_->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        transition(targets_[index].get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
        if(!execute(index,error) || !waitFence(frameFences_[index],error))return false;
        const std::size_t rowBytes=static_cast<std::size_t>(width_)*4;
        rgba.resize(rowBytes*height_);
        const void* mapped=nullptr;D3D12_RANGE readRange{0,static_cast<SIZE_T>(readbackBytes_)};
        if(!checked(readback_->Map(0,&readRange,const_cast<void**>(&mapped)),"Map diagnostic readback",error))return false;
        const auto* pixels=static_cast<const std::uint8_t*>(mapped)+footprint_.Offset;
        for(UINT y=0;y<height_;++y)std::memcpy(rgba.data()+static_cast<std::size_t>(y)*rowBytes,
            pixels+static_cast<std::size_t>(y)*footprint_.Footprint.RowPitch,rowBytes);
        D3D12_RANGE noWrites{0,0};readback_->Unmap(0,&noWrites);
        width=width_;height=height_;return true;
    }
    std::string name()const override{return "Direct3D12";}
    RenderStats stats()const override{return stats_;}
};
}
std::unique_ptr<Renderer> createD3D12Renderer(){return std::make_unique<Direct3D12Renderer>();}
}
