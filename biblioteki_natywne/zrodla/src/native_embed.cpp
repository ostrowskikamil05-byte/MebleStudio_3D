#include "native_embed.h"
#include "scene.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
using namespace mebel;
struct Annotation {NativeOverlay style{};std::vector<float> points;std::wstring text,font;};
struct View {
    HWND parent=nullptr,window=nullptr,overlay=nullptr;
    std::unique_ptr<Renderer> renderer;Scene scene;Camera camera;
    std::uint32_t width=1,height=1;NativeCamera state{};
    std::vector<std::vector<std::size_t>> triangles;
    std::vector<Annotation> annotations;
    bool ready=false,visible=false,failed=false;
};
std::unordered_set<View*> live;
ULONG_PTR graphicsToken=0;
void message(wchar_t* error,std::uint32_t capacity,const std::string& text){if(error&&capacity){auto value=widen(text);auto n=std::min<std::size_t>(value.size(),capacity-1);std::memcpy(error,value.data(),n*sizeof(wchar_t));error[n]=0;}}
View* view(void* handle){auto p=static_cast<View*>(handle);return live.contains(p)?p:nullptr;}
Frame frame(View& p){auto f=p.camera.frame(p.scene,p.width,p.height,p.state.xray!=0,p.state.selected);f.options[0]=p.state.brightness;f.options[1]=static_cast<float>(p.state.xray);return f;}
bool draw(View& p,std::string& error){if(!p.ready||p.failed)return false;if(!p.renderer->draw(frame(p),error)){p.failed=true;return false;}return true;}
LRESULT CALLBACK procedure(HWND window,UINT type,WPARAM w,LPARAM l){
    auto p=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(type==WM_NCCREATE){p=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
    if(type==WM_NCHITTEST)return HTTRANSPARENT;
    if(type==WM_ERASEBKGND)return 1;
    if(type==WM_PAINT){PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);if(p&&window==p->window){std::string error;draw(*p,error);}return 0;}
    // All input is handled by the existing Canvas bindings on this same thread.
    if(p&&((type>=WM_MOUSEFIRST&&type<=WM_MOUSELAST)||(type>=WM_KEYFIRST&&type<=WM_KEYLAST))){PostMessageW(p->parent,type,w,l);return 0;}
    return DefWindowProcW(window,type,w,l);
}
HMODULE instance(){HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&module);return module;}
void registerWindows(){static bool done=false;if(done)return;WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance();wc.lpszClassName=L"MebelNative014Viewport";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Nie moĹĽna zarejestrowaÄ‡ widoku C++.");done=true;}
Gdiplus::Color color(std::uint32_t rgb){return Gdiplus::Color(255,GetRValue(rgb),GetGValue(rgb),GetBValue(rgb));}
bool overlay(View& p,std::string& error){
    if(!IsWindow(p.overlay)||!p.width||!p.height)return true;
    if(static_cast<std::uint64_t>(p.width)*p.height>32000000){error="NakĹ‚adka przekracza bezpieczny rozmiar obrazu.";return false;}
    HDC screen=GetDC(p.parent),memory=CreateCompatibleDC(screen);BITMAPINFO description{};description.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);description.bmiHeader.biWidth=p.width;description.bmiHeader.biHeight=-static_cast<LONG>(p.height);description.bmiHeader.biPlanes=1;description.bmiHeader.biBitCount=32;description.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&description,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!memory||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(p.parent,screen);error="Nie moĹĽna utworzyÄ‡ nakĹ‚adki wymiarĂłw.";return false;}
    HGDIOBJ old=SelectObject(memory,bitmap);std::memset(pixels,0,static_cast<std::size_t>(p.width)*p.height*4);
    {
        Gdiplus::Bitmap image(p.width,p.height,p.width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));Gdiplus::Graphics g(&image);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        for(auto& a:p.annotations){auto& s=a.style;auto& q=a.points;if(q.size()<2)continue;
            Gdiplus::Pen pen(color(s.outline),std::max(.5f,s.width));if(s.flags&1)pen.SetDashStyle(Gdiplus::DashStyleDash);
            Gdiplus::AdjustableArrowCap arrow(4,5,TRUE);if(s.flags&2)pen.SetCustomStartCap(&arrow);if(s.flags&4)pen.SetCustomEndCap(&arrow);
            Gdiplus::SolidBrush brush(color(s.fill));
            if(s.type==1&&q.size()>=4&&s.outline!=0xffffffff){std::vector<Gdiplus::PointF> points;for(std::size_t i=0;i+1<q.size();i+=2)points.emplace_back(q[i],q[i+1]);g.DrawLines(&pen,points.data(),static_cast<INT>(points.size()));}
            else if((s.type==2||s.type==3)&&q.size()>=4){Gdiplus::RectF r(std::min(q[0],q[2]),std::min(q[1],q[3]),std::abs(q[2]-q[0]),std::abs(q[3]-q[1]));if(s.fill!=0xffffffff){if(s.type==2)g.FillRectangle(&brush,r);else g.FillEllipse(&brush,r);}if(s.outline!=0xffffffff){if(s.type==2)g.DrawRectangle(&pen,r);else g.DrawEllipse(&pen,r);}}
            else if(s.type==4&&q.size()>=6){std::vector<Gdiplus::PointF> points;for(std::size_t i=0;i+1<q.size();i+=2)points.emplace_back(q[i],q[i+1]);if(s.fill!=0xffffffff)g.FillPolygon(&brush,points.data(),static_cast<INT>(points.size()));if(s.outline!=0xffffffff)g.DrawPolygon(&pen,points.data(),static_cast<INT>(points.size()));}
            else if(s.type==5&&s.fill!=0xffffffff&&!a.text.empty()){
                Gdiplus::FontFamily family(a.font.empty()?L"Segoe UI":a.font.c_str());Gdiplus::Font font(&family,static_cast<float>(std::max(1,s.height)),s.bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
                Gdiplus::StringFormat format;format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip|(s.width>0?0:Gdiplus::StringFormatFlagsNoWrap));
                Gdiplus::RectF bounds;
                if(s.width>0)g.MeasureString(a.text.c_str(),-1,&font,Gdiplus::SizeF(s.width,100000),&format,&bounds);
                else g.MeasureString(a.text.c_str(),-1,&font,Gdiplus::PointF(0,0),&format,&bounds);
                int column=std::clamp(s.anchor,0,8)%3,row=std::clamp(s.anchor,0,8)/3;float x=q[0]-(column==0?0:column==1?bounds.Width/2:bounds.Width),y=q[1]-(row==0?0:row==1?bounds.Height/2:bounds.Height);
                if(s.width>0)g.DrawString(a.text.c_str(),-1,&font,Gdiplus::RectF(x,y,s.width,bounds.Height+2),&format,&brush);
                else g.DrawString(a.text.c_str(),-1,&font,Gdiplus::PointF(x,y),&format,&brush);
            }
        }
    }
    SIZE size{static_cast<LONG>(p.width),static_cast<LONG>(p.height)};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    BOOL ok=UpdateLayeredWindow(p.overlay,nullptr,nullptr,&size,memory,&origin,0,&blend,ULW_ALPHA);
    auto code=GetLastError();SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(p.parent,screen);
    if(!ok){error="Nie moĹĽna wyĹ›wietliÄ‡ wymiarĂłw nad widokiem C++ (Windows "+std::to_string(code)+").";return false;}return true;
}
void dispose(View* p){if(!p)return;p->ready=false;p->renderer.reset();if(IsWindow(p->overlay))DestroyWindow(p->overlay);if(IsWindow(p->window))DestroyWindow(p->window);live.erase(p);delete p;}
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}Vec3 mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool box(const Owner& o,Vec3 origin,Vec3 direction){float first=0,last=INFINITY;for(int i=0;i<3;i++){float a=(&origin.x)[i],d=(&direction.x)[i],lo=(&o.lo.x)[i]-.1f,hi=(&o.hi.x)[i]+.1f;if(std::abs(d)<1e-9f){if(a<lo||a>hi)return false;}else{float x=(lo-a)/d,y=(hi-a)/d;if(x>y)std::swap(x,y);first=std::max(first,x);last=std::min(last,y);if(first>last)return false;}}return true;}
}

extern "C" {
void* ms3d_create(std::uintptr_t parent,const char* backend,wchar_t* error,std::uint32_t capacity){
    View* p=nullptr;try{
        if(!IsWindow(reinterpret_cast<HWND>(parent)))throw std::runtime_error("NieprawidĹ‚owe okno obszaru roboczego.");
        if(!graphicsToken){Gdiplus::GdiplusStartupInput startup;if(Gdiplus::GdiplusStartup(&graphicsToken,&startup,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Nie moĹĽna uruchomiÄ‡ nakĹ‚adki graficznej.");}registerWindows();
        p=new View;p->parent=reinterpret_cast<HWND>(parent);RECT r{};GetClientRect(p->parent,&r);p->width=std::max(1L,r.right);p->height=std::max(1L,r.bottom);
        // Tk routes wheel/hover through WindowFromPoint, which skips disabled
        // children. These two surfaces only display pixels; the enabled Canvas
        // owns all input, focus and capture. HTTRANSPARENT alone is insufficient.
        p->window=CreateWindowExW(WS_EX_TRANSPARENT,L"MebelNative014Viewport",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_DISABLED,0,0,p->width,p->height,p->parent,nullptr,instance(),p);
        p->overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"MebelNative014Viewport",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_DISABLED,0,0,p->width,p->height,p->parent,nullptr,instance(),p);
        if(!p->window||!p->overlay)throw std::runtime_error("Nie moĹĽna utworzyÄ‡ widoku lub nakĹ‚adki C++.");
        std::string requested=backend?backend:"auto",problem,combined;auto choices=requested=="auto"?std::vector<std::string>{"d3d11","vulkan"}:std::vector<std::string>{requested};
        for(auto& choice:choices){p->renderer=choice=="d3d11"?createD3D11Renderer():choice=="d3d12"?createD3D12Renderer():choice=="vulkan"?createVulkanRenderer():nullptr;if(p->renderer&&p->renderer->initialize(p->window,p->width,p->height,problem))break;p->renderer.reset();combined+=choice+": "+problem+"; ";}
        if(!p->renderer)throw std::runtime_error(combined);live.insert(p);message(error,capacity,"");return p;
    }catch(const std::exception& e){dispose(p);message(error,capacity,e.what());return nullptr;}
}
int ms3d_upload(void* handle,const std::uint8_t* bytes,std::uint64_t size,wchar_t* error,std::uint32_t capacity){auto p=view(handle);if(!p)return 0;try{Scene next;std::string issue;if(!loadSceneData(bytes,size,next,issue))throw std::runtime_error(issue);if(!p->renderer->upload(next.vertices,issue))throw std::runtime_error(issue);p->scene=std::move(next);p->triangles.assign(p->scene.owners.size(),{});for(std::size_t i=0;i<p->scene.vertices.size();i+=3)p->triangles[p->scene.vertices[i].owner].push_back(i);p->failed=false;p->ready=true;message(error,capacity,"");return 1;}catch(const std::exception& e){message(error,capacity,e.what());return 0;}}
int ms3d_frame(void* handle,const NativeCamera* camera,wchar_t* error,std::uint32_t capacity){auto p=view(handle);if(!p||!camera)return 0;try{for(float value:{camera->yaw,camera->pitch,camera->scale,camera->panX,camera->panY,camera->center[0],camera->center[1],camera->center[2],camera->brightness})if(!std::isfinite(value))throw std::runtime_error("NieprawidĹ‚owe ustawienia kamery.");if(camera->scale<=0||camera->xray>2)throw std::runtime_error("NieprawidĹ‚owa skala lub tryb X-ray.");p->state=*camera;p->camera.yaw=camera->yaw;p->camera.pitch=camera->pitch;p->camera.scale=camera->scale;p->camera.panX=camera->panX;p->camera.panY=camera->panY;p->camera.center={camera->center[0],camera->center[1],camera->center[2]};std::string issue;if(!draw(*p,issue))throw std::runtime_error(issue.empty()?"Widok C++ wymaga ponownego uruchomienia.":issue);message(error,capacity,"");return 1;}catch(const std::exception& e){message(error,capacity,e.what());return 0;}}
int ms3d_resize(void* handle,std::uint32_t width,std::uint32_t height,wchar_t* error,std::uint32_t capacity){auto p=view(handle);if(!p)return 0;std::string issue;if(!width||!height||width>16384||height>16384){message(error,capacity,"NieprawidĹ‚owy rozmiar widoku.");return 0;}if(!p->renderer->resize(width,height,issue)){message(error,capacity,issue);return 0;}p->width=width;p->height=height;MoveWindow(p->window,0,0,width,height,FALSE);MoveWindow(p->overlay,0,0,width,height,FALSE);message(error,capacity,"");return 1;}
int ms3d_overlay(void* handle,const NativeOverlay* items,std::uint32_t count,wchar_t* error,std::uint32_t capacity){auto p=view(handle);if(!p)return 0;try{if(count>20000||(count&&!items))throw std::runtime_error("Zbyt wiele opisĂłw w widoku.");std::vector<Annotation> next;for(std::uint32_t i=0;i<count;i++){auto& s=items[i];if(s.type<1||s.type>5||s.count>65536||s.count%2||(s.count&&!s.coordinates))throw std::runtime_error("NieprawidĹ‚owy opis graficzny.");Annotation a;a.style=s;if(s.count)a.points.assign(s.coordinates,s.coordinates+s.count);if(s.text)a.text=s.text;if(s.font)a.font=s.font;for(float value:a.points)if(!std::isfinite(value))throw std::runtime_error("NieprawidĹ‚owe wspĂłĹ‚rzÄ™dne opisu.");next.push_back(std::move(a));}p->annotations=std::move(next);std::string issue;if(!overlay(*p,issue))throw std::runtime_error(issue);message(error,capacity,"");return 1;}catch(const std::exception& e){message(error,capacity,e.what());return 0;}}
int ms3d_pick(void* handle,float x,float y,std::uint32_t mode,NativeHit* output){auto p=view(handle);if(!p||!p->ready||!output||!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=p->width||y>=p->height)return 0;auto axes=p->camera.basis();float radius=10000;for(int mask=0;mask<8;mask++){Vec3 q{mask&1?p->scene.hi.x:p->scene.lo.x,mask&2?p->scene.hi.y:p->scene.lo.y,mask&4?p->scene.hi.z:p->scene.lo.z};auto d=sub(q,p->camera.center);radius=std::max(radius,std::sqrt(dot(d,d))+2000);}Vec3 origin=add(p->camera.center,add(mul(axes[0],(x-p->width*.5f-p->camera.panX)/p->camera.scale),add(mul(axes[1],-(y-p->height*.5f-p->camera.panY)/p->camera.scale),mul(axes[2],radius))));Vec3 direction=mul(axes[2],-1);float closest=INFINITY;std::int32_t chosen=-1;std::size_t triangle=0;
    for(int pass=0;pass<2&&(chosen<0||p->scene.owners[chosen].kind==3);pass++)for(std::size_t owner=0;owner<p->scene.owners.size();owner++){auto& o=p->scene.owners[owner];if(mode==1&&(o.kind==1||o.kind==3))continue;if(p->state.xray&&bool(o.flags&Ghost)!=(pass==1))continue;if(!p->state.xray&&pass==1)continue;if(!box(o,origin,direction))continue;for(auto index:p->triangles[owner]){auto& a=p->scene.vertices[index];if(a.flags&Architecture){float facing=dot(a.normal,axes[2]);if((std::abs(a.normal.z)>.5f&&facing<=.001f)||(std::abs(a.normal.z)<=.5f&&facing>=-.001f))continue;}auto b=p->scene.vertices[index+1].position,c=p->scene.vertices[index+2].position;auto e1=sub(b,a.position),e2=sub(c,a.position),q=cross(direction,e2);float determinant=dot(e1,q);if(std::abs(determinant)<1e-9f)continue;float inverse=1/determinant;auto t=sub(origin,a.position);float u=dot(t,q)*inverse;if(u<0||u>1)continue;auto r=cross(t,e1);float v=dot(direction,r)*inverse;if(v<0||u+v>1)continue;float distance=dot(e2,r)*inverse;if(distance>0&&distance<closest){closest=distance;chosen=static_cast<std::int32_t>(owner);triangle=index;}}}
    if(chosen<0)return 0;auto point=add(origin,mul(direction,closest));auto& a=p->scene.vertices[triangle];output->owner=chosen;output->kind=p->scene.owners[chosen].kind;output->flags=a.flags;output->world[0]=point.x;output->world[1]=point.y;output->world[2]=point.z;output->depth=dot(sub(point,p->camera.center),axes[2]);return 1;}
int ms3d_show(void* handle,int visible){auto p=view(handle);if(!p)return 0;p->visible=visible!=0;ShowWindow(p->window,visible?SW_SHOWNA:SW_HIDE);ShowWindow(p->overlay,visible?SW_SHOWNA:SW_HIDE);if(visible)SetWindowPos(p->overlay,HWND_TOP,0,0,p->width,p->height,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);return 1;}
int ms3d_info(void* handle,wchar_t* text,std::uint32_t capacity){
    auto p=view(handle);if(!p)return 0;auto stats=p->renderer->stats();
    auto quote=[](const std::string& value){std::string out="\"";for(unsigned char c:value){if(c=='\\'||c=='"')out+='\\';if(c>=32)out+=static_cast<char>(c);else out+=' ';}return out+'"';};
    std::ostringstream out;out<<"{\"engine\":"<<quote(p->renderer->name())<<",\"adapter\":"<<quote(stats.adapter)<<",\"software\":"<<(stats.software?"true":"false")<<",\"cpu_submit_ms\":"<<stats.submitMs<<",\"frame_counter\":"<<stats.frameCounter<<",\"vertices\":"<<p->scene.vertices.size()<<",\"overlays\":"<<p->annotations.size()<<'}';
    message(text,capacity,out.str());return 1;
}
void ms3d_close(void* handle){if(auto p=view(handle))dispose(p);}
}
