// Vulkan1.0 renderer. All Vulkan entry points are resolved from the official
// system loader, so the executable also runs on machines without Vulkan.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "renderer.hpp"
#include "vulkan_shaders.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mebel {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" failed (VkResult "+std::to_string(result)+")");
}
std::wstring environment(const wchar_t* name) {
    DWORD n=GetEnvironmentVariableW(name,nullptr,0); if(!n) return {};
    std::wstring s(n,L'\0'); GetEnvironmentVariableW(name,s.data(),n); s.resize(n-1); return s;
}
#define INSTANCE_FUNCTIONS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(GetPhysicalDeviceFormatProperties) X(EnumerateDeviceExtensionProperties) \
 X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) \
 X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) \
 X(GetPhysicalDeviceSurfacePresentModesKHR) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
 X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(QueueWaitIdle) \
 X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateImage) X(DestroyImage) \
 X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateImageView) X(DestroyImageView) \
 X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
 X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdBindDescriptorSets) X(CmdSetViewport) X(CmdSetScissor) \
 X(CmdPushConstants) X(CmdDraw) X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdCopyImageToBuffer) \
 X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(QueueSubmit)

class VulkanRenderer final : public Renderer {
    HMODULE loader_=nullptr;
    HWND window_=nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProc_=nullptr;
    PFN_vkCreateInstance createInstance_=nullptr;
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensions_=nullptr;
#define DECLARE(name) PFN_vk##name vk##name=nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    VkInstance instance_=VK_NULL_HANDLE;
    VkSurfaceKHR surface_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memories_{};
    std::uint32_t family_=0,width_=0,height_=0,vertexCount_=0;
    VkQueue queue_=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    VkFormat format_=VK_FORMAT_B8G8R8A8_UNORM,depthFormat_=VK_FORMAT_D32_SFLOAT;
    std::vector<VkImage> swapImages_;
    // Presentation semaphores are associated with acquired images, not frames:
    // reacquiring an image proves the preceding presentation wait has completed.
    std::vector<VkSemaphore> presented_;
    VkSemaphore acquired_=VK_NULL_HANDLE;
    VkFence frameFence_=VK_NULL_HANDLE;
    VkCommandPool commandPool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkBuffer vertices_=VK_NULL_HANDLE,uniform_=VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_=VK_NULL_HANDLE,uniformMemory_=VK_NULL_HANDLE;
    void* uniformMap_=nullptr;
    VkImage color_=VK_NULL_HANDLE,depth_=VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_=VK_NULL_HANDLE,depthMemory_=VK_NULL_HANDLE;
    VkImageView colorView_=VK_NULL_HANDLE,depthView_=VK_NULL_HANDLE;
    VkRenderPass renderPass_=VK_NULL_HANDLE;
    VkFramebuffer framebuffer_=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_=VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_=VK_NULL_HANDLE;
    VkDescriptorSet descriptor_=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_=VK_NULL_HANDLE;
    VkPipeline solidPipeline_=VK_NULL_HANDLE,ghostPipeline_=VK_NULL_HANDLE;
    RenderStats stats_{};
    bool lost_=false,hasFrame_=false,diagnosticLoader_=false;

    std::uint32_t memoryType(std::uint32_t bits,VkMemoryPropertyFlags properties) {
        for(std::uint32_t i=0;i<memories_.memoryTypeCount;i++)
            if((bits&(1u<<i))&&(memories_.memoryTypes[i].propertyFlags&properties)==properties) return i;
        throw std::runtime_error("Vulkan device has no compatible memory type");
    }
    void createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& buffer,VkDeviceMemory& memory) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size=std::max<VkDeviceSize>(size,4); bi.usage=usage; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_,&bi,nullptr,&buffer),"vkCreateBuffer");
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_,buffer,&req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_,&alloc,nullptr,&memory),"vkAllocateMemory(buffer)");
        check(vkBindBufferMemory(device_,buffer,memory,0),"vkBindBufferMemory");
    }
    void createImage(VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,
                     VkImage& image,VkDeviceMemory& memory,VkImageView& view) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format; ci.extent={width_,height_,1};
        ci.mipLevels=1; ci.arrayLayers=1; ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=usage; ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_,&ci,nullptr,&image),"vkCreateImage");
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_,image,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size; ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_,&ai,nullptr,&memory),"vkAllocateMemory(image)");
        check(vkBindImageMemory(device_,image,memory,0),"vkBindImageMemory");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=format;
        vi.subresourceRange={aspect,0,1,0,1};
        check(vkCreateImageView(device_,&vi,nullptr,&view),"vkCreateImageView");
    }
    void waitFrame() {
        VkResult result=vkWaitForFences(device_,1,&frameFence_,VK_TRUE,10'000'000'000ull);
        if(result==VK_ERROR_DEVICE_LOST) lost_=true;
        check(result,"vkWaitForFences");
    }
    void releaseSwapchain() noexcept {
        if(!device_) return;
        if(solidPipeline_&&vkDestroyPipeline) vkDestroyPipeline(device_,solidPipeline_,nullptr);
        if(ghostPipeline_&&vkDestroyPipeline) vkDestroyPipeline(device_,ghostPipeline_,nullptr);
        solidPipeline_=ghostPipeline_=VK_NULL_HANDLE;
        if(framebuffer_&&vkDestroyFramebuffer) vkDestroyFramebuffer(device_,framebuffer_,nullptr); framebuffer_=VK_NULL_HANDLE;
        if(renderPass_&&vkDestroyRenderPass) vkDestroyRenderPass(device_,renderPass_,nullptr); renderPass_=VK_NULL_HANDLE;
        if(colorView_&&vkDestroyImageView) vkDestroyImageView(device_,colorView_,nullptr); colorView_=VK_NULL_HANDLE;
        if(depthView_&&vkDestroyImageView) vkDestroyImageView(device_,depthView_,nullptr); depthView_=VK_NULL_HANDLE;
        if(color_&&vkDestroyImage) vkDestroyImage(device_,color_,nullptr); color_=VK_NULL_HANDLE;
        if(depth_&&vkDestroyImage) vkDestroyImage(device_,depth_,nullptr); depth_=VK_NULL_HANDLE;
        if(colorMemory_&&vkFreeMemory) vkFreeMemory(device_,colorMemory_,nullptr); colorMemory_=VK_NULL_HANDLE;
        if(depthMemory_&&vkFreeMemory) vkFreeMemory(device_,depthMemory_,nullptr); depthMemory_=VK_NULL_HANDLE;
        for(auto s:presented_) if(s&&vkDestroySemaphore) vkDestroySemaphore(device_,s,nullptr);
        presented_.clear(); swapImages_.clear();
        if(swapchain_&&vkDestroySwapchainKHR) vkDestroySwapchainKHR(device_,swapchain_,nullptr);
        swapchain_=VK_NULL_HANDLE; hasFrame_=false;
    }
    void release() noexcept {
        if(device_&&vkDeviceWaitIdle&&!lost_) vkDeviceWaitIdle(device_);
        releaseSwapchain();
        if(device_) {
            if(uniformMap_&&vkUnmapMemory) vkUnmapMemory(device_,uniformMemory_); uniformMap_=nullptr;
            if(vertices_&&vkDestroyBuffer) vkDestroyBuffer(device_,vertices_,nullptr); vertices_=VK_NULL_HANDLE;
            if(vertexMemory_&&vkFreeMemory) vkFreeMemory(device_,vertexMemory_,nullptr); vertexMemory_=VK_NULL_HANDLE;
            if(uniform_&&vkDestroyBuffer) vkDestroyBuffer(device_,uniform_,nullptr); uniform_=VK_NULL_HANDLE;
            if(uniformMemory_&&vkFreeMemory) vkFreeMemory(device_,uniformMemory_,nullptr); uniformMemory_=VK_NULL_HANDLE;
            if(descriptorPool_&&vkDestroyDescriptorPool) vkDestroyDescriptorPool(device_,descriptorPool_,nullptr); descriptorPool_=VK_NULL_HANDLE;
            if(pipelineLayout_&&vkDestroyPipelineLayout) vkDestroyPipelineLayout(device_,pipelineLayout_,nullptr); pipelineLayout_=VK_NULL_HANDLE;
            if(descriptorLayout_&&vkDestroyDescriptorSetLayout) vkDestroyDescriptorSetLayout(device_,descriptorLayout_,nullptr); descriptorLayout_=VK_NULL_HANDLE;
            if(acquired_&&vkDestroySemaphore) vkDestroySemaphore(device_,acquired_,nullptr); acquired_=VK_NULL_HANDLE;
            if(frameFence_&&vkDestroyFence) vkDestroyFence(device_,frameFence_,nullptr); frameFence_=VK_NULL_HANDLE;
            if(commandPool_&&vkDestroyCommandPool) vkDestroyCommandPool(device_,commandPool_,nullptr); commandPool_=VK_NULL_HANDLE;
            if(vkDestroyDevice) vkDestroyDevice(device_,nullptr); device_=VK_NULL_HANDLE;
        }
        if(surface_&&vkDestroySurfaceKHR) vkDestroySurfaceKHR(instance_,surface_,nullptr); surface_=VK_NULL_HANDLE;
        if(instance_&&vkDestroyInstance) vkDestroyInstance(instance_,nullptr); instance_=VK_NULL_HANDLE;
        if(loader_) FreeLibrary(loader_); loader_=nullptr;
    }
    void load() {
        // Diagnostics may explicitly override the loader. Normal startup first
        // uses the system loader, then the official redistributable beside this
        // executable. Neither lookup searches the process working directory.
        std::wstring path;
        if(environment(L"MEBELSTUDIO_VULKAN_DIAGNOSTIC")==L"1") {
            path=environment(L"MEBELSTUDIO_VULKAN_LOADER");
            if(!path.empty()) {
                if(path.size()<3||path[1]!=L':'||path[2]!=L'\\')
                    throw std::runtime_error("Diagnostic Vulkan loader must use an absolute Windows path");
                diagnosticLoader_=true;
            }
        }
        const DWORD searchFlags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32;
        if(!path.empty()) {
            loader_=LoadLibraryExW(path.c_str(),nullptr,searchFlags);
        } else {
            wchar_t system[MAX_PATH]{}; UINT count=GetSystemDirectoryW(system,MAX_PATH);
            if(!count||count>=MAX_PATH) throw std::runtime_error("Cannot locate the Windows system directory");
            path=std::wstring(system)+L"\\vulkan-1.dll";
            loader_=LoadLibraryExW(path.c_str(),nullptr,searchFlags);
            if(!loader_) {
                std::vector<wchar_t> executable(32768,L'\0');
                DWORD length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
                if(!length||length>=executable.size()) throw std::runtime_error("Cannot locate the executable's portable Vulkan runtime");
                path.assign(executable.data(),length);
                auto separator=path.find_last_of(L"\\/");
                if(separator==std::wstring::npos) throw std::runtime_error("Executable directory is unavailable");
                path.resize(separator+1); path+=L"runtime\\vulkan-1.dll";
                loader_=LoadLibraryExW(path.c_str(),nullptr,searchFlags);
            }
        }
        if(!loader_) throw std::runtime_error("Vulkan loader unavailable: install the GPU manufacturer's Vulkan driver or use DirectX11 (Windows error "+std::to_string(GetLastError())+")");
        getInstanceProc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader_,"vkGetInstanceProcAddr"));
        if(!getInstanceProc_) throw std::runtime_error("Vulkan loader does not export vkGetInstanceProcAddr");
        createInstance_=reinterpret_cast<PFN_vkCreateInstance>(getInstanceProc_(VK_NULL_HANDLE,"vkCreateInstance"));
        enumerateInstanceExtensions_=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(getInstanceProc_(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties"));
        if(!createInstance_||!enumerateInstanceExtensions_) throw std::runtime_error("Incomplete Vulkan loader");
        std::uint32_t count=0; check(enumerateInstanceExtensions_(nullptr,&count,nullptr),"EnumerateInstanceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        check(enumerateInstanceExtensions_(nullptr,&count,extensions.data()),"EnumerateInstanceExtensionProperties");
        for(const char* required:{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME}) {
            if(std::none_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,required)==0;}))
                throw std::runtime_error(std::string("Required Vulkan instance extension unavailable: ")+required);
        }
        const char* required[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName="MebelStudio3D Native"; application.applicationVersion=VK_MAKE_VERSION(0,14,0);
        application.pEngineName="MebelStudio3D C++"; application.engineVersion=1; application.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&application;
        ci.enabledExtensionCount=2; ci.ppEnabledExtensionNames=required;
        check(createInstance_(&ci,nullptr,&instance_),"vkCreateInstance");
#define LOAD_INSTANCE(name) \
        vk##name=reinterpret_cast<PFN_vk##name>(getInstanceProc_(instance_,"vk" #name)); \
        if(!vk##name) throw std::runtime_error("Vulkan instance entry point unavailable: vk" #name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        sci.hinstance=GetModuleHandleW(nullptr); sci.hwnd=window_;
        check(vkCreateWin32SurfaceKHR(instance_,&sci,nullptr,&surface_),"vkCreateWin32SurfaceKHR");
    }
    void chooseDevice() {
        std::uint32_t count=0; check(vkEnumeratePhysicalDevices(instance_,&count,nullptr),"vkEnumeratePhysicalDevices");
        if(!count) throw std::runtime_error("No Vulkan-compatible graphics device was reported by the driver");
        std::vector<VkPhysicalDevice> candidates(count); check(vkEnumeratePhysicalDevices(instance_,&count,candidates.data()),"vkEnumeratePhysicalDevices");
        int best=-1;
        for(auto candidate:candidates) {
            VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(candidate,&props);
            std::uint32_t extensionCount=0;
            if(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,nullptr)!=VK_SUCCESS) continue;
            std::vector<VkExtensionProperties> extensions(extensionCount);
            if(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,extensions.data())!=VK_SUCCESS) continue;
            if(std::none_of(extensions.begin(),extensions.end(),[](const auto& e){return std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0;})) continue;
            std::uint32_t queues=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,nullptr);
            std::vector<VkQueueFamilyProperties> properties(queues); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,properties.data());
            for(std::uint32_t i=0;i<queues;i++) {
                VkBool32 present=VK_FALSE;
                if(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface_,&present)!=VK_SUCCESS) continue;
                if(!present||!(properties[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)||!properties[i].queueCount) continue;
                int score=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?3:props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?2:props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU?0:1;
                if(score>best) {best=score; physical_=candidate; family_=i; stats_.adapter=props.deviceName; stats_.software=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU;}
                break;
            }
        }
        if(!physical_) throw std::runtime_error("No Vulkan device supports graphics and presentation for this Windows window");
        if(diagnosticLoader_) stats_.adapter+=" [explicit diagnostic loader]";
        vkGetPhysicalDeviceMemoryProperties(physical_,&memories_);
        bool foundDepth=false;
        for(auto f:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM}) {
            VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(physical_,f,&properties);
            if(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {depthFormat_=f; foundDepth=true; break;}
        }
        if(!foundDepth) throw std::runtime_error("Vulkan device lacks a supported depth-buffer format");
        float priority=1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=family_; qi.queueCount=1; qi.pQueuePriorities=&priority;
        const char* extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.enabledExtensionCount=1; di.ppEnabledExtensionNames=extensions;
        check(vkCreateDevice(physical_,&di,nullptr,&device_),"vkCreateDevice");
#define LOAD_DEVICE(name) \
        vk##name=reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(device_,"vk" #name)); \
        if(!vk##name) throw std::runtime_error("Vulkan device entry point unavailable: vk" #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        vkGetDeviceQueue(device_,family_,0,&queue_);
    }
    void createResources() {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex=family_;
        check(vkCreateCommandPool(device_,&pool,nullptr,&commandPool_),"vkCreateCommandPool");
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool=commandPool_; ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device_,&ai,&command_),"vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_,&si,nullptr,&acquired_),"vkCreateSemaphore(acquire)");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_,&fi,nullptr,&frameFence_),"vkCreateFence");
        createBuffer(sizeof(Frame),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,uniform_,uniformMemory_);
        check(vkMapMemory(device_,uniformMemory_,0,sizeof(Frame),0,&uniformMap_),"vkMapMemory(uniform)");
        VkDescriptorSetLayoutBinding binding{}; binding.binding=0; binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount=1; binding.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount=1; li.pBindings=&binding;
        check(vkCreateDescriptorSetLayout(device_,&li,nullptr,&descriptorLayout_),"vkCreateDescriptorSetLayout");
        VkPushConstantRange push{}; push.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT; push.size=sizeof(int);
        VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pi.setLayoutCount=1; pi.pSetLayouts=&descriptorLayout_; pi.pushConstantRangeCount=1; pi.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device_,&pi,nullptr,&pipelineLayout_),"vkCreatePipelineLayout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets=1; poolInfo.poolSizeCount=1; poolInfo.pPoolSizes=&size;
        check(vkCreateDescriptorPool(device_,&poolInfo,nullptr,&descriptorPool_),"vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool=descriptorPool_; setInfo.descriptorSetCount=1; setInfo.pSetLayouts=&descriptorLayout_;
        check(vkAllocateDescriptorSets(device_,&setInfo,&descriptor_),"vkAllocateDescriptorSets");
        VkDescriptorBufferInfo bi{uniform_,0,sizeof(Frame)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=descriptor_; write.dstBinding=0; write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo=&bi;
        vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    }
    void createPipelines() {
        VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;
        try {
            VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader.codeSize=sizeof(vulkanVertexShader); shader.pCode=vulkanVertexShader;
            check(vkCreateShaderModule(device_,&shader,nullptr,&vertex),"vkCreateShaderModule(vertex)");
            shader.codeSize=sizeof(vulkanFragmentShader); shader.pCode=vulkanFragmentShader;
            check(vkCreateShaderModule(device_,&shader,nullptr,&fragment),"vkCreateShaderModule(fragment)");
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex; stages[0].pName="main";
            stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragment; stages[1].pName="main";
            VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
            VkVertexInputAttributeDescription attributes[5]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32B32A32_SFLOAT,24},{3,0,VK_FORMAT_R32_UINT,40},{4,0,VK_FORMAT_R32_UINT,44}};
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding;
            vi.vertexAttributeDescriptionCount=5; vi.pVertexAttributeDescriptions=attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=viewport.scissorCount=1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth=1;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable=VK_TRUE; depth.depthWriteEnable=VK_TRUE; depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo colors{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; colors.attachmentCount=1; colors.pAttachments=&blend;
            VkDynamicState dynamics[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamics;
            VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            ci.stageCount=2; ci.pStages=stages; ci.pVertexInputState=&vi; ci.pInputAssemblyState=&assembly;
            ci.pViewportState=&viewport; ci.pRasterizationState=&raster; ci.pMultisampleState=&ms;
            ci.pDepthStencilState=&depth; ci.pColorBlendState=&colors; ci.pDynamicState=&dynamic;
            ci.layout=pipelineLayout_; ci.renderPass=renderPass_; ci.subpass=0;
            check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&ci,nullptr,&solidPipeline_),"vkCreateGraphicsPipelines(solid)");
            depth.depthWriteEnable=VK_FALSE;
            blend.blendEnable=VK_TRUE; blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp=VK_BLEND_OP_ADD; blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.alphaBlendOp=VK_BLEND_OP_ADD;
            check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&ci,nullptr,&ghostPipeline_),"vkCreateGraphicsPipelines(x-ray)");
        } catch(...) {
            if(vertex) vkDestroyShaderModule(device_,vertex,nullptr);
            if(fragment) vkDestroyShaderModule(device_,fragment,nullptr);
            throw;
        }
        vkDestroyShaderModule(device_,vertex,nullptr); vkDestroyShaderModule(device_,fragment,nullptr);
    }
    void createSwapchain(std::uint32_t requestedWidth,std::uint32_t requestedHeight) {
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_,surface_,&caps),"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            throw std::runtime_error("Vulkan window surface does not support GPU image presentation copies");
        VkExtent2D extent=caps.currentExtent;
        if(extent.width==std::numeric_limits<std::uint32_t>::max()) {
            extent.width=std::clamp(requestedWidth,caps.minImageExtent.width,caps.maxImageExtent.width);
            extent.height=std::clamp(requestedHeight,caps.minImageExtent.height,caps.maxImageExtent.height);
        }
        if(!extent.width||!extent.height) {width_=height_=0; return;}
        width_=extent.width; height_=extent.height;
        std::uint32_t count=0; check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,nullptr),"GetPhysicalDeviceSurfaceFormatsKHR");
        if(!count) throw std::runtime_error("Vulkan surface offers no pixel formats");
        std::vector<VkSurfaceFormatKHR> formats(count); check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,formats.data()),"GetPhysicalDeviceSurfaceFormatsKHR");
        VkSurfaceFormatKHR selected=formats.front();
        if(formats.size()==1&&selected.format==VK_FORMAT_UNDEFINED) selected.format=VK_FORMAT_B8G8R8A8_UNORM;
        for(auto f:formats) if((f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {selected=f; break;}
        if(selected.format!=VK_FORMAT_B8G8R8A8_UNORM&&selected.format!=VK_FORMAT_R8G8B8A8_UNORM&&selected.format!=VK_FORMAT_B8G8R8A8_SRGB&&selected.format!=VK_FORMAT_R8G8B8A8_SRGB)
            throw std::runtime_error("Vulkan surface offers no supported RGBA8/BGRA8 presentation format");
        format_=selected.format;
        VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(physical_,format_,&properties);
        if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Vulkan presentation format cannot be used as a render target");
        std::uint32_t imageCount=caps.minImageCount+1;
        if(caps.maxImageCount) imageCount=std::min(imageCount,caps.maxImageCount);
        VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for(auto option:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if(caps.supportedCompositeAlpha&option) {alpha=option; break;}
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface=surface_; ci.minImageCount=imageCount; ci.imageFormat=format_; ci.imageColorSpace=selected.colorSpace;
        ci.imageExtent=extent; ci.imageArrayLayers=1; ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; ci.preTransform=caps.currentTransform;
        ci.compositeAlpha=alpha; ci.presentMode=VK_PRESENT_MODE_FIFO_KHR; ci.clipped=VK_TRUE;
        check(vkCreateSwapchainKHR(device_,&ci,nullptr,&swapchain_),"vkCreateSwapchainKHR");
        check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,nullptr),"vkGetSwapchainImagesKHR");
        swapImages_.resize(count); check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,swapImages_.data()),"vkGetSwapchainImagesKHR");
        presented_.resize(count,VK_NULL_HANDLE);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for(auto& semaphore:presented_) check(vkCreateSemaphore(device_,&si,nullptr,&semaphore),"vkCreateSemaphore(present)");
        createImage(format_,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT,color_,colorMemory_,colorView_);
        createImage(depthFormat_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,depth_,depthMemory_,depthView_);
        VkAttachmentDescription attachments[2]{};
        attachments[0].format=format_; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        attachments[1].format=depthFormat_; attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&colorRef; sub.pDepthStencilAttachment=&depthRef;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0; dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount=2; ri.pAttachments=attachments; ri.subpassCount=1; ri.pSubpasses=&sub;
        ri.dependencyCount=2; ri.pDependencies=dependencies;
        check(vkCreateRenderPass(device_,&ri,nullptr,&renderPass_),"vkCreateRenderPass");
        VkImageView views[]={colorView_,depthView_};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass=renderPass_;
        fi.attachmentCount=2; fi.pAttachments=views; fi.width=width_; fi.height=height_; fi.layers=1;
        check(vkCreateFramebuffer(device_,&fi,nullptr,&framebuffer_),"vkCreateFramebuffer");
        createPipelines();
    }
    void beginCommand() {
        check(vkResetCommandBuffer(command_,0),"vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_,&begin),"vkBeginCommandBuffer");
    }
public:
    ~VulkanRenderer() override {release();}
    bool initialize(HWND window,std::uint32_t width,std::uint32_t height,std::string& error) override {
        error.clear(); if(device_) {error="Vulkan renderer already initialized"; return false;}
        try {
            if(!window||!IsWindow(window)) throw std::runtime_error("Vulkan requires a valid native Windows window");
            window_=window; load(); chooseDevice(); createResources();
            createSwapchain(std::max(width,1u),std::max(height,1u)); return true;
        } catch(const std::exception& e) {error=e.what(); release(); return false;}
    }
    bool upload(const std::vector<Vertex>& vertices,std::string& error) override {
        error.clear(); if(!device_||lost_) {error="Vulkan device is unavailable"; return false;}
        VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        try {
            if(vertices.size()>std::numeric_limits<std::uint32_t>::max()||vertices.size()%3)
                throw std::runtime_error("Vulkan upload requires complete triangles and a uint32 vertex count");
            waitFrame();
            if(!vertices.empty()) {
                const VkDeviceSize bytes=vertices.size()*sizeof(Vertex);
                createBuffer(bytes,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,buffer,memory);
                void* target=nullptr; check(vkMapMemory(device_,memory,0,bytes,0,&target),"vkMapMemory(vertices)");
                std::memcpy(target,vertices.data(),static_cast<std::size_t>(bytes)); vkUnmapMemory(device_,memory);
            }
            if(vertices_) vkDestroyBuffer(device_,vertices_,nullptr);
            if(vertexMemory_) vkFreeMemory(device_,vertexMemory_,nullptr);
            vertices_=buffer; vertexMemory_=memory; vertexCount_=static_cast<std::uint32_t>(vertices.size());
            return true;
        } catch(const std::exception& e) {
            if(buffer) vkDestroyBuffer(device_,buffer,nullptr); if(memory) vkFreeMemory(device_,memory,nullptr);
            error=e.what(); return false;
        }
    }
    bool resize(std::uint32_t width,std::uint32_t height,std::string& error) override {
        error.clear(); if(!device_||lost_) {error="Vulkan device is unavailable"; return false;}
        try {
            if(width==width_&&height==height_&&swapchain_) return true;
            VkResult result=vkDeviceWaitIdle(device_); if(result==VK_ERROR_DEVICE_LOST) lost_=true;
            check(result,"vkDeviceWaitIdle(resize)"); releaseSwapchain();
            width_=width; height_=height;
            if(width&&height) createSwapchain(width,height);
            return true;
        } catch(const std::exception& e) {error=e.what(); return false;}
    }
    bool draw(const Frame& frame,std::string& error) override {
        error.clear(); if(!device_||lost_) {error="Vulkan device is unavailable or was lost"; return false;}
        if(!width_||!height_||!swapchain_) return true;
        const auto start=std::chrono::steady_clock::now();
        try {
            waitFrame();
            std::uint32_t index=0;
            VkResult acquire=vkAcquireNextImageKHR(device_,swapchain_,10'000'000'000ull,acquired_,VK_NULL_HANDLE,&index);
            if(acquire==VK_ERROR_OUT_OF_DATE_KHR) {
                RECT rect{}; GetClientRect(window_,&rect);
                // Force recreation even when the cached extent is unchanged.
                width_=0;
                return resize(static_cast<std::uint32_t>(std::max(0L,rect.right)),static_cast<std::uint32_t>(std::max(0L,rect.bottom)),error);
            }
            if(acquire!=VK_SUCCESS&&acquire!=VK_SUBOPTIMAL_KHR) {if(acquire==VK_ERROR_DEVICE_LOST) lost_=true; check(acquire,"vkAcquireNextImageKHR");}
            std::memcpy(uniformMap_,&frame,sizeof(Frame));
            beginCommand();
            VkClearValue clear[2]{}; clear[0].color={{.075f,.095f,.135f,1.f}}; clear[1].depthStencil={1.f,0};
            VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass=renderPass_; begin.framebuffer=framebuffer_;
            begin.renderArea={{0,0},{width_,height_}}; begin.clearValueCount=2; begin.pClearValues=clear;
            vkCmdBeginRenderPass(command_,&begin,VK_SUBPASS_CONTENTS_INLINE);
            VkViewport viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1}; VkRect2D scissor{{0,0},{width_,height_}};
            vkCmdSetViewport(command_,0,1,&viewport); vkCmdSetScissor(command_,0,1,&scissor);
            vkCmdBindDescriptorSets(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout_,0,1,&descriptor_,0,nullptr);
            if(vertexCount_) {
                VkDeviceSize offset=0; vkCmdBindVertexBuffers(command_,0,1,&vertices_,&offset);
                int pass=0; vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,solidPipeline_);
                vkCmdPushConstants(command_,pipelineLayout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pass),&pass);
                vkCmdDraw(command_,vertexCount_,1,0,0);
                if(frame.options[1]>.5f) {
                    pass=1; vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_GRAPHICS,ghostPipeline_);
                    vkCmdPushConstants(command_,pipelineLayout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pass),&pass);
                    vkCmdDraw(command_,vertexCount_,1,0,0);
                }
            }
            vkCmdEndRenderPass(command_);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.image=swapImages_.at(index); barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkImageCopy copy{}; copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.dstSubresource=copy.srcSubresource; copy.extent={width_,height_,1};
            vkCmdCopyImage(command_,color_,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,swapImages_[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=0;
            vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            check(vkEndCommandBuffer(command_),"vkEndCommandBuffer");
            check(vkResetFences(device_,1,&frameFence_),"vkResetFences");
            VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount=1; submit.pWaitSemaphores=&acquired_; submit.pWaitDstStageMask=&waitStage;
            submit.commandBufferCount=1; submit.pCommandBuffers=&command_; submit.signalSemaphoreCount=1; submit.pSignalSemaphores=&presented_[index];
            VkResult result=vkQueueSubmit(queue_,1,&submit,frameFence_);
            if(result!=VK_SUCCESS) {lost_=true; check(result,"vkQueueSubmit");}
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount=1; present.pWaitSemaphores=&presented_[index];
            present.swapchainCount=1; present.pSwapchains=&swapchain_; present.pImageIndices=&index;
            result=vkQueuePresentKHR(queue_,&present);
            if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR&&result!=VK_ERROR_OUT_OF_DATE_KHR) {if(result==VK_ERROR_DEVICE_LOST) lost_=true; check(result,"vkQueuePresentKHR");}
            hasFrame_=true; stats_.frameCounter++;
            stats_.submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR||acquire==VK_SUBOPTIMAL_KHR) {
                RECT rect{}; GetClientRect(window_,&rect); width_=0;
                return resize(static_cast<std::uint32_t>(std::max(0L,rect.right)),static_cast<std::uint32_t>(std::max(0L,rect.bottom)),error);
            }
            return true;
        } catch(const std::exception& e) {error=e.what(); return false;}
    }
    bool readback(std::vector<std::uint8_t>& rgba,std::uint32_t& width,std::uint32_t& height,std::string& error) override {
        error.clear(); rgba.clear(); width=height=0;
        if(!device_||lost_||!hasFrame_) {error="Vulkan readback requires a successfully rendered frame"; return false;}
        VkBuffer staging=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; void* mapped=nullptr;
        try {
            waitFrame(); const VkDeviceSize bytes=VkDeviceSize(width_)*height_*4;
            createBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,staging,memory);
            beginCommand();
            VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={width_,height_,1};
            vkCmdCopyImageToBuffer(command_,color_,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging,1,&copy);
            // Queue submission completion plus coherent host memory makes the
            // copy visible to the host; include an explicit transfer->host barrier.
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer=staging; barrier.offset=0; barrier.size=bytes;
            vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
            check(vkEndCommandBuffer(command_),"vkEndCommandBuffer(readback)");
            check(vkResetFences(device_,1,&frameFence_),"vkResetFences(readback)");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command_;
            VkResult result=vkQueueSubmit(queue_,1,&submit,frameFence_); if(result!=VK_SUCCESS) {lost_=true; check(result,"vkQueueSubmit(readback)");}
            waitFrame(); check(vkMapMemory(device_,memory,0,bytes,0,&mapped),"vkMapMemory(readback)");
            rgba.resize(static_cast<std::size_t>(bytes)); std::memcpy(rgba.data(),mapped,rgba.size());
            vkUnmapMemory(device_,memory); mapped=nullptr;
            if(format_==VK_FORMAT_B8G8R8A8_UNORM||format_==VK_FORMAT_B8G8R8A8_SRGB)
                for(std::size_t i=0;i<rgba.size();i+=4) std::swap(rgba[i],rgba[i+2]);
            width=width_; height=height_;
            vkDestroyBuffer(device_,staging,nullptr); vkFreeMemory(device_,memory,nullptr);
            return true;
        } catch(const std::exception& e) {
            // A timed-out readback may still be using its staging allocation.
            // Complete outstanding work before freeing it on recoverable errors.
            if(device_&&!lost_) vkDeviceWaitIdle(device_);
            if(mapped) vkUnmapMemory(device_,memory);
            if(staging) vkDestroyBuffer(device_,staging,nullptr); if(memory) vkFreeMemory(device_,memory,nullptr);
            rgba.clear(); width=height=0; error=e.what(); return false;
        }
    }
    std::string name() const override {return "Vulkan 1.0";}
    RenderStats stats() const override {return stats_;}
};
}
std::unique_ptr<Renderer> createVulkanRenderer() {return std::make_unique<VulkanRenderer>();}
}
