#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

extern "C" {
// All calls happen on the existing UI thread. Strings are copied on entry.
struct NativeCamera {float yaw,pitch,scale,panX,panY,center[3],brightness;std::uint32_t xray;std::int32_t selected;};
struct NativeHit {std::int32_t owner;std::uint32_t kind,flags;float world[3],depth;};
// type:1=line,2=rectangle,3=oval,4=polygon,5=text. Colors: COLORREF,0xffffffff=none.
// flags:bit0=dashed,bit1=arrow first,bit2=arrow last; text anchor 0..8 NW,N,NE,W,C,E,SW,S,SE.
struct NativeOverlay {std::uint32_t type,outline,fill,flags;float width;const float* coordinates;std::uint32_t count;const wchar_t* text;const wchar_t* font;std::int32_t height,bold,anchor;};
static_assert(sizeof(void*)==8, "The embedded viewport targets Windows x64.");
static_assert(sizeof(NativeCamera)==44 && sizeof(NativeHit)==28);
static_assert(sizeof(NativeOverlay)==72 && offsetof(NativeOverlay,coordinates)==24 && offsetof(NativeOverlay,text)==40 && offsetof(NativeOverlay,anchor)==64);
__declspec(dllexport) void* ms3d_create(std::uintptr_t parent,const char* backend,wchar_t* error,std::uint32_t capacity);
__declspec(dllexport) int ms3d_upload(void* handle,const std::uint8_t* bytes,std::uint64_t size,wchar_t* error,std::uint32_t capacity);
__declspec(dllexport) int ms3d_frame(void* handle,const NativeCamera* camera,wchar_t* error,std::uint32_t capacity);
__declspec(dllexport) int ms3d_resize(void* handle,std::uint32_t width,std::uint32_t height,wchar_t* error,std::uint32_t capacity);
__declspec(dllexport) int ms3d_overlay(void* handle,const NativeOverlay* items,std::uint32_t count,wchar_t* error,std::uint32_t capacity);
__declspec(dllexport) int ms3d_pick(void* handle,float x,float y,std::uint32_t mode,NativeHit* hit);
__declspec(dllexport) int ms3d_show(void* handle,int visible);
__declspec(dllexport) int ms3d_info(void* handle,wchar_t* text,std::uint32_t capacity);
__declspec(dllexport) void ms3d_close(void* handle);
}
