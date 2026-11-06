#include "core_api.hpp"
#include "model_api.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <system_error>

namespace {
constexpr size_t MaxRequest = 16 * 1024 * 1024;
std::atomic<uint64_t> calls{0}, failures{0}, buffers{0}, transforms{0}, vertices{0};
std::atomic<int64_t> elapsedNs{0};
using mebel::model::Json;
Json info() {
    return {{"api_version",1},{"application_version","0.19-migration"},
        {"implementation","C++20"},{"calls",calls.load()},
        {"failures",failures.load()},{"outstanding_buffers",buffers.load()},
        {"batch_transforms",transforms.load()},{"transformed_vertices",vertices.load()},
        {"call_ms",double(elapsedNs.load()) / 1e6},
        {"python_required_by_library",false},{"document_api_version",1},
        {"document_active_models",mebel::model::document_active_count()},
        {"document_operations",Json::array({"create","read","commit","checkpoint","undo","redo","clone","info","destroy","save"})},
        {"document_validation","structure_scalars_references; exact CAD requires editor preflight"},
        {"layout_api_version",1},
        {"workspace_geometry_api_version",1},
        {"layout_operations",Json::array({"groups","group_layout","group_bounds","local_group_bounds","move_group","rotate_group","snap_group","nearest_snap","contacts"})},
        {"workspace_geometry_operations",Json::array({"polygons_overlap"})},
        {"worktop_api_version",1},
        {"worktop_operations",Json::array({"distance","offset","union","geometry"})}};
}
}
namespace mebel::model {
Json dispatch(const Json& request) {
    if (!request.is_object() || !request.contains("module") || !request.contains("function")
        || !request["module"].is_string() || !request["function"].is_string())
        throw std::invalid_argument("Nieprawidłowe żądanie silnika obliczeniowego.");
    if (request.contains("args") && !request["args"].is_array())
        throw std::invalid_argument("Argumenty muszą być listą.");
    if (request.contains("kwargs") && !request["kwargs"].is_object())
        throw std::invalid_argument("Nazwane argumenty muszą być obiektem.");
    if (request["module"] == "native" && request["function"] == "info") return info();
    if (auto value = dispatch_document(request)) return std::move(*value);
    if (auto value = dispatch_layout(request)) return std::move(*value);
    if (auto value = dispatch_workspace_geometry(request)) return std::move(*value);
    if (auto value = dispatch_workspace_worktop(request)) return std::move(*value);
    if (auto value = dispatch_core(request)) return *value;
    if (auto value = dispatch_shapes(request)) return *value;
    if (auto value = dispatch_construction(request)) return *value;
    if (auto value = dispatch_services(request)) return *value;
    throw std::invalid_argument("Funkcja nie została jeszcze przeniesiona do C++: "
        + request["module"].get<std::string>() + "." + request["function"].get<std::string>());
}
}
extern "C" MS3D_MODEL_API uint32_t ms3d_model_version() { return 1; }
extern "C" MS3D_MODEL_API char* ms3d_model_call(const char* request) {
    const auto start = std::chrono::steady_clock::now();
    ++calls;
    Json response;
    try {
        if (!request) throw std::invalid_argument("Puste żądanie silnika.");
        const size_t size = strnlen(request, MaxRequest + 1);
        if (size > MaxRequest) throw std::invalid_argument("Żądanie silnika jest zbyt duże.");
        auto depthGuard = [](int depth, Json::parse_event_t, Json&) {
            if (depth > 64) throw std::invalid_argument("Dane silnika są zbyt zagnieżdżone.");
            return true;
        };
        const Json data = Json::parse(request, request + size, depthGuard);
        response = {{"ok",true},{"result",mebel::model::dispatch(data)}};
    } catch (const mebel::model::NativeException& e) {
        ++failures;
        response = {{"ok",false},{"error",e.what()},{"type",e.type}};
    } catch (const std::system_error& e) {
        ++failures;
        response = {{"ok",false},{"error",e.what()},{"type","OSError"},{"winerror",e.code().value()}};
    } catch (const std::exception& e) {
        ++failures;
        response = {{"ok",false},{"error",e.what()},{"type","ValueError"}};
    } catch (...) {
        ++failures;
        response = {{"ok",false},{"error","Nieznany błąd silnika obliczeniowego."},{"type","RuntimeError"}};
    }
    try {
        std::string text = response.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (text.size() > 64 * 1024 * 1024) return nullptr;
        auto result = static_cast<char*>(std::malloc(text.size() + 1));
        if (!result) return nullptr;
        std::memcpy(result, text.c_str(), text.size() + 1);
        ++buffers;
        elapsedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    } catch (...) { return nullptr; }
}
extern "C" MS3D_MODEL_API void ms3d_model_free(void* response) {
    if (response) { std::free(response); --buffers; }
}
namespace {
using Point = std::array<double,3>;
Point rotatePoint(Point p, const double* angles) {
    const double a=angles[0]*std::numbers::pi/180.,b=angles[1]*std::numbers::pi/180.,c=angles[2]*std::numbers::pi/180.;
    const double y=p[1]*std::cos(a)-p[2]*std::sin(a),z=p[1]*std::sin(a)+p[2]*std::cos(a);
    p[1]=y;p[2]=z;
    const double x=p[0]*std::cos(b)+p[2]*std::sin(b),z2=-p[0]*std::sin(b)+p[2]*std::cos(b);
    p[0]=x;p[2]=z2;
    const double x2=p[0]*std::cos(c)-p[1]*std::sin(c),y2=p[0]*std::sin(c)+p[1]*std::cos(c);
    return {x2,y2,p[2]};
}
Point posedPoint(const MS3DPartPose& pose, Point p) {
    if (pose.plane==1) std::swap(p[1],p[2]);
    else if (pose.plane==2) p={p[2],p[0],p[1]};
    p=rotatePoint(p,pose.angles);
    for(int i=0;i<3;++i)p[i]+=pose.origin[i];
    const double angles[3]={0,0,pose.placement[3]};
    p=rotatePoint(p,angles);
    for(int i=0;i<3;++i)p[i]+=pose.placement[i];
    return p;
}
}
static_assert(sizeof(MS3DPartPose)==88);
extern "C" MS3D_MODEL_API int ms3d_model_transform(const MS3DPartPose* pose,
    const double* input, uint64_t count, double* output) {
    if (!pose || pose->plane>2 || count>2000000 || (count && (!input || !output))) return 0;
    for(double v:pose->origin)if(!std::isfinite(v))return 0;
    for(double v:pose->angles)if(!std::isfinite(v))return 0;
    for(double v:pose->placement)if(!std::isfinite(v))return 0;
    for(uint64_t i=0;i<count*3;++i)if(!std::isfinite(input[i]))return 0;
    const Point origin=posedPoint(*pose,{0,0,0});
    Point a=posedPoint(*pose,{1,0,0}),b=posedPoint(*pose,{0,1,0}),c=posedPoint(*pose,{0,0,1});
    for(int i=0;i<3;++i){a[i]-=origin[i];b[i]-=origin[i];c[i]-=origin[i];}
    for(uint64_t i=0;i<count;++i) {
        const double u=input[i*3],v=input[i*3+1],w=input[i*3+2];
        for(int j=0;j<3;++j)output[i*3+j]=origin[j]+a[j]*u+b[j]*v+c[j]*w;
    }
    ++transforms;vertices+=count;
    const double determinant=a[0]*(b[1]*c[2]-b[2]*c[1])-b[0]*(a[1]*c[2]-a[2]*c[1])+c[0]*(a[1]*b[2]-a[2]*b[1]);
    return determinant>=0 ? 1 : -1;
}
