#include "scene.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace mebel;
namespace {
constexpr int Open=100,Fit=101,Front=102,Top=103,View3D=104,Xray=105,Room=106,Backend=200,Owners=201;
struct Application {
    HWND window=nullptr,viewport=nullptr,backend=nullptr,owners=nullptr,info=nullptr,status=nullptr;
    std::vector<HWND> buttons;
    HFONT font=nullptr;Scene scene;Camera camera;std::unique_ptr<Renderer> renderer;
    std::filesystem::path folder,path;int sidebar=270,selected=-1,drag=0,lastX=0,lastY=0;
    bool xray=false,diagnostic=false,moved=false,ready=false,renderFailed=false;std::string api="auto",lastError;
    std::uint32_t width=900,height=520;int contentTop=78;
    std::vector<std::string> errors;
} app;
std::string escape(const std::string& value){std::string out;for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c>=32)out+=c;}return out;}
std::wstring label(const std::string& text){return widen(text);}
void error(const std::string& message){app.lastError=message;app.errors.push_back(message);SetWindowTextW(app.status,widen(message).c_str());if(!app.diagnostic)MessageBoxW(app.window,widen(message).c_str(),L"MebelStudio3D C++",MB_OK|MB_ICONWARNING);}
bool draw(){
    if(!app.renderer||!app.ready||app.renderFailed)return false;
    std::string message;auto frame=app.camera.frame(app.scene,app.width,app.height,app.xray,app.selected);
    if(!app.renderer->draw(frame,message)){app.renderFailed=true;error(message);return false;}
    auto stats=app.renderer->stats();std::ostringstream text;
    text<<app.renderer->name()<<" · "<<stats.adapter<<(stats.software?" · renderowanie programowe":" · GPU")
        <<" · "<<app.scene.vertices.size()/3<<" trójkątów · obsługa CPU "<<std::fixed<<std::setprecision(2)<<stats.submitMs<<" ms";
    SetWindowTextW(app.status,label(text.str()).c_str());return true;
}
void selection(int index){
    app.selected=index;
    if(index<0||static_cast<std::size_t>(index)>=app.scene.owners.size()){SetWindowTextW(app.info,L"Kliknij element w widoku 3D lub wybierz z listy.");SendMessageW(app.owners,LB_SETCURSEL,-1,0);}
    else{
        auto& owner=app.scene.owners[index];std::ostringstream text;
        text<<owner.label<<"\r\n\r\nGabaryt w scenie [mm]:\r\n"<<std::fixed<<std::setprecision(1)
            <<owner.hi.x-owner.lo.x<<" × "<<owner.hi.y-owner.lo.y<<" × "<<owner.hi.z-owner.lo.z
            <<"\r\n\r\n"<<owner.id<<"\r\n\r\nPodgląd jest tylko do odczytu. Dokładne dane produkcyjne pozostają w projekcie .mebel.";
        SetWindowTextW(app.info,label(text.str()).c_str());SendMessageW(app.owners,LB_SETCURSEL,index,0);
    }draw();
}
LRESULT CALLBACK ViewProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(window!=app.viewport)return DefWindowProcW(window,message,w,l);
    switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(window,&ps);EndPaint(window,&ps);draw();return 0;}
    case WM_LBUTTONDOWN:case WM_MBUTTONDOWN:case WM_RBUTTONDOWN:
        SetFocus(window);SetCapture(window);app.drag=message==WM_LBUTTONDOWN?1:message==WM_MBUTTONDOWN?2:3;app.lastX=GET_X_LPARAM(l);app.lastY=GET_Y_LPARAM(l);app.moved=false;return 0;
    case WM_MOUSEMOVE:
        if(app.drag){int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l),dx=x-app.lastX,dy=y-app.lastY;app.lastX=x;app.lastY=y;
            if(dx||dy)app.moved=true;
            if(app.drag==3||(app.drag==2&&(w&MK_SHIFT))){app.camera.panX+=dx;app.camera.panY+=dy;}
            else{app.camera.yaw+=dx*.45f;app.camera.pitch=std::clamp(app.camera.pitch+dy*.4f,-89.f,89.f);}draw();}return 0;
    case WM_LBUTTONUP:case WM_MBUTTONUP:case WM_RBUTTONUP:
        if(app.drag==1&&!app.moved)selection(pickOwner(app.scene,app.camera,app.width,app.height,GET_X_LPARAM(l)+.5f,GET_Y_LPARAM(l)+.5f));
        app.drag=0;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:app.drag=0;return 0;
    case WM_MOUSEWHEEL:
        app.camera.scale=std::clamp(app.camera.scale*std::pow(1.12f,GET_WHEEL_DELTA_WPARAM(w)/120.f),.0001f,100.f);draw();return 0;
    case WM_KEYDOWN:
        if(w=='F'){app.camera.fit(app.scene,app.width,app.height);draw();return 0;}
        if(w=='X'){app.xray=!app.xray;SendMessageW(app.buttons[5],BM_SETCHECK,app.xray,0);draw();return 0;}break;
    }return DefWindowProcW(window,message,w,l);
}
bool useBackend(const std::string& requested,bool showFailure=true){
    RECT rectangle{};GetClientRect(app.window,&rectangle);
    HWND next=CreateWindowExW(0,L"MebelNativeViewport",L"",WS_CHILD|WS_CLIPSIBLINGS,app.sidebar+8,app.contentTop,app.width,app.height,app.window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!next){if(showFailure)error("Nie można utworzyć natywnego widoku.");return false;}
    std::unique_ptr<Renderer> candidate;std::string problem,combined;
    std::vector<std::string> choices=requested=="auto"?std::vector<std::string>{"d3d11","vulkan"}:std::vector<std::string>{requested};
    for(auto& choice:choices){
        candidate=choice=="vulkan"?createVulkanRenderer():choice=="d3d12"?createD3D12Renderer():createD3D11Renderer();
        if(candidate&&candidate->initialize(next,app.width,app.height,problem)&&candidate->upload(app.scene.vertices,problem)&&candidate->draw(app.camera.frame(app.scene,app.width,app.height,app.xray,app.selected),problem))break;
        combined+=choice+": "+problem+"\n";candidate.reset();
    }
    if(!candidate){DestroyWindow(next);if(showFailure)error(combined);return false;}
    HWND previous=app.viewport;app.renderer.reset();app.viewport=next;app.renderer=std::move(candidate);app.api=requested;app.renderFailed=false;
    ShowWindow(next,SW_SHOW);if(previous)DestroyWindow(previous);draw();return true;
}
void layout(){
    RECT rectangle{};GetClientRect(app.window,&rectangle);int w=rectangle.right,h=rectangle.bottom;
    app.sidebar=std::clamp(app.sidebar,200,std::max(200,w-350));app.width=std::max(1,w-app.sidebar-16);
    MoveWindow(app.backend,8,44,260,26,TRUE);int x=284,y=44;
    for(auto button:app.buttons){if(x+108>w-8){x=8;y+=34;}MoveWindow(button,x,y,108,28,TRUE);x+=112;}
    app.contentTop=y+34;app.height=std::max(1,h-app.contentTop-34);
    MoveWindow(app.owners,8,app.contentTop,app.sidebar-8,std::max(60,h-app.contentTop-230),TRUE);
    MoveWindow(app.info,10,std::max(140,h-220),app.sidebar-14,175,TRUE);
    MoveWindow(app.status,8,h-28,std::max(1,w-16),24,TRUE);
    if(app.viewport){MoveWindow(app.viewport,app.sidebar+8,app.contentTop,app.width,app.height,TRUE);std::string problem;
        if(app.renderer&&!app.renderFailed&&!app.renderer->resize(app.width,app.height,problem)){app.renderFailed=true;error(problem);}draw();}
}
bool load(const std::filesystem::path& path){
    Scene next;std::string problem;if(!loadScene(path,next,problem)){error(problem);return false;}
    Scene previous=std::move(app.scene);app.scene=std::move(next);
    if(app.renderer&&!app.renderer->upload(app.scene.vertices,problem)){app.scene=std::move(previous);std::string recovery;app.renderer->upload(app.scene.vertices,recovery);error(problem);return false;}
    app.path=path;app.selected=-1;app.camera.fit(app.scene,app.width,app.height);SendMessageW(app.owners,LB_RESETCONTENT,0,0);
    for(auto& owner:app.scene.owners)SendMessageW(app.owners,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label(owner.label).c_str()));
    selection(-1);return true;
}
void open(){wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=app.window;dialog.lpstrFilter=L"Sceny MebelStudio3D C++\0*.mscene\0\0";dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrInitialDir=app.folder.c_str();dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&dialog))load(file);}
LRESULT CALLBACK MainProc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        app.window=window;app.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HWND title=CreateWindowW(L"STATIC",L"MEBEL STUDIO 3D · C++ · etap migracji silnika",WS_CHILD|WS_VISIBLE,10,8,760,30,window,nullptr,nullptr,nullptr);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),0);
        app.backend=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,8,44,260,180,window,reinterpret_cast<HMENU>(Backend),nullptr,nullptr);
        for(auto text:{L"Automatyczny",L"DirectX 11",L"DirectX 12",L"Vulkan"})SendMessageW(app.backend,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(app.backend,CB_SETCURSEL,0,0);
        for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Open,L"Otwórz scenę"},{Fit,L"Dopasuj"},{Front,L"Przód"},{Top,L"Góra"},{View3D,L"3D"},{Xray,L"X-ray"},{Room,L"Pomieszczenie"}}){
            HWND button=CreateWindowW(L"BUTTON",item.second,WS_CHILD|WS_VISIBLE|(item.first==Xray?BS_AUTOCHECKBOX:BS_PUSHBUTTON),0,44,108,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(item.first)),nullptr,nullptr);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),0);app.buttons.push_back(button);
        }
        app.owners=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,8,78,250,250,window,reinterpret_cast<HMENU>(Owners),nullptr,nullptr);
        app.info=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,8,340,250,190,window,nullptr,nullptr,nullptr);
        app.status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFTNOWORDWRAP,8,540,1000,24,window,nullptr,nullptr,nullptr);
        for(HWND control:{app.backend,app.owners,app.info,app.status})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),0);return 0;
    }
    case WM_SIZE:if(w!=SIZE_MINIMIZED&&app.backend)layout();return 0;
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={800,520};return 0;}
    case WM_COMMAND:
        if(LOWORD(w)==Backend&&HIWORD(w)==CBN_SELCHANGE){int choice=static_cast<int>(SendMessageW(app.backend,CB_GETCURSEL,0,0));std::string old=app.api;
            if(!useBackend(choice==3?"vulkan":choice==2?"d3d12":choice==1?"d3d11":"auto"))SendMessageW(app.backend,CB_SETCURSEL,old=="vulkan"?3:old=="d3d12"?2:old=="d3d11"?1:0,0);return 0;}
        if(LOWORD(w)==Owners&&HIWORD(w)==LBN_SELCHANGE){selection(static_cast<int>(SendMessageW(app.owners,LB_GETCURSEL,0,0)));return 0;}
        switch(LOWORD(w)){
        case Open:open();return 0;
        case Front:app.camera.yaw=0;app.camera.pitch=0;break;
        case Top:app.camera.yaw=0;app.camera.pitch=90;break;
        case View3D:app.camera.yaw=-38;app.camera.pitch=26;break;
        case Fit:app.camera.fit(app.scene,app.width,app.height);draw();return 0;
        case Room:app.camera.fit(app.scene,app.width,app.height,true);draw();return 0;
        case Xray:app.xray=SendMessageW(app.buttons[5],BM_GETCHECK,0,0)==BST_CHECKED;draw();return 0;
        default:return DefWindowProcW(window,message,w,l);
        }app.camera.fit(app.scene,app.width,app.height);draw();return 0;
    case WM_LBUTTONDOWN:
        if(std::abs(GET_X_LPARAM(l)-app.sidebar)<8){app.drag=4;SetCapture(window);}return 0;
    case WM_MOUSEMOVE:
        if(app.drag==4){app.sidebar=GET_X_LPARAM(l);layout();}return 0;
    case WM_LBUTTONUP:if(app.drag==4){app.drag=0;ReleaseCapture();}return 0;
    case WM_CAPTURECHANGED:if(app.drag==4)app.drag=0;return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:app.ready=false;app.renderer.reset();if(app.font)DeleteObject(app.font);PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,message,w,l);
}
void ppm(const std::filesystem::path& path,const std::vector<std::uint8_t>& image,std::uint32_t w,std::uint32_t h){
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<w<<" "<<h<<"\n255\n";for(std::size_t i=0;i<image.size();i+=4)out.write(reinterpret_cast<const char*>(image.data()+i),3);
}
bool diagnostic(const std::filesystem::path& report){
    std::vector<std::string> checks;std::string problem;std::vector<std::uint8_t> image;std::uint32_t width,height;
    bool ok=app.renderer&&app.renderer->readback(image,width,height,problem);
    if(ok){
        std::size_t changed=0;for(std::size_t i=4;i<image.size();i+=4)if(image[i]!=image[0]||image[i+1]!=image[1]||image[i+2]!=image[2])changed++;
        ok=width==app.width&&height==app.height&&changed>500;
        if(ok)checks.push_back("Native GPU image contains the loaded CAD geometry");else problem="Empty or mismatched GPU image";
    }
    if(ok){
        auto original=app.scene.vertices;app.camera.yaw+=25;ok=draw();app.xray=true;ok=ok&&draw();app.xray=false;app.camera.yaw-=25;ok=ok&&draw();
        ok=ok&&std::memcmp(original.data(),app.scene.vertices.data(),original.size()*sizeof(Vertex))==0;
        if(ok)checks.push_back("Orbit and X-ray retain the original CAD mesh");
    }
    std::vector<double> times;
    if(ok){
        for(int i=0;i<20&&ok;i++){app.camera.yaw=-38+i;auto count=app.renderer->stats().frameCounter;ok=draw()&&app.renderer->stats().frameCounter==count+1;if(ok)times.push_back(app.renderer->stats().submitMs);}
        app.camera.yaw=-38;if(ok)ok=draw();if(ok)checks.push_back("Retained geometry renders twenty actual native frames");
        std::sort(times.begin(),times.end());
        if(ok)ok=app.renderer->readback(image,width,height,problem);if(ok)ppm(report.parent_path()/(app.renderer->name()+".ppm"),image,width,height);
    }
    ok=ok&&app.errors.empty()&&!app.renderFailed;
    auto stats=app.renderer?app.renderer->stats():RenderStats{};std::ofstream out(report);
    out<<"{\n  \"ok\": "<<(ok?"true":"false")<<",\n  \"engine\": \""<<escape(app.renderer?app.renderer->name():app.api)<<"\",\n  \"adapter\": \""<<escape(stats.adapter)<<"\",\n  \"software\": "<<(stats.software?"true":"false")
       <<",\n  \"vertices\": "<<app.scene.vertices.size()<<",\n  \"owners\": "<<app.scene.owners.size()<<",\n  \"width\": "<<app.width<<",\n  \"height\": "<<app.height
       <<",\n  \"median_cpu_submission_ms\": "<<(times.empty()?0:times[times.size()/2])<<",\n  \"timing_note\": \"CPU elapsed including driver waits. No GPU timestamps; this is not a measured FPS rate.\",\n  \"error\": \""<<escape(problem.empty()?app.lastError:problem)<<"\",\n  \"checks\": [";
    for(std::size_t i=0;i<checks.size();i++){if(i)out<<",";out<<"\""<<escape(checks[i])<<"\"";}out<<"]\n}\n";return ok;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    SetProcessDPIAware();wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);app.folder=std::filesystem::path(module).parent_path();
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);std::filesystem::path input=app.folder/L"assets/demo_scena_0_12.mscene",report;
    for(int i=1;i<count;i++){
        std::wstring arg=args[i];if(arg==L"--backend"&&i+1<count)app.api=narrow(args[++i]);
        else if(arg==L"--scene"&&i+1<count)input=args[++i];
        else if(arg==L"--self-test"&&i+1<count){report=args[++i];app.diagnostic=true;}
    }LocalFree(args);
    if(app.api!="auto"&&app.api!="d3d11"&&app.api!="d3d12"&&app.api!="vulkan")return 2;
    WNDCLASSW main{};main.lpfnWndProc=MainProc;main.hInstance=instance;main.lpszClassName=L"MebelNativeMain";main.hCursor=LoadCursor(nullptr,IDC_ARROW);main.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&main);
    WNDCLASSW view{};view.lpfnWndProc=ViewProc;view.hInstance=instance;view.lpszClassName=L"MebelNativeViewport";view.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&view);
    MONITORINFO monitor{};monitor.cbSize=sizeof monitor;GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    auto window=CreateWindowW(main.lpszClassName,L"MebelStudio3D C++ · DirectX / Vulkan · podgląd migracyjny",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,monitor.rcWork.left+10,monitor.rcWork.top+10,std::min<LONG>(1280,monitor.rcWork.right-monitor.rcWork.left-20),std::min<LONG>(720,monitor.rcWork.bottom-monitor.rcWork.top-20),nullptr,nullptr,instance,nullptr);
    if(!window)return 3;layout();
    if(!load(input)){if(app.diagnostic){std::ofstream out(report);out<<"{\"ok\":false,\"error\":\""<<escape(app.lastError)<<"\"}";}DestroyWindow(window);return 4;}
    app.ready=true;
    if(!useBackend(app.api)){if(app.diagnostic){std::ofstream out(report);out<<"{\"ok\":false,\"error\":\""<<escape(app.lastError)<<"\"}";}DestroyWindow(window);return 5;}
    SendMessageW(app.backend,CB_SETCURSEL,app.api=="vulkan"?3:app.api=="d3d12"?2:app.api=="d3d11"?1:0,0);
    if(app.diagnostic){bool ok=diagnostic(report);DestroyWindow(window);return ok?0:6;}
    ShowWindow(window,show);UpdateWindow(window);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}
