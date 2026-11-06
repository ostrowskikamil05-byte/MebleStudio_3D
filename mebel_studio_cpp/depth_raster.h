// Orthographic software z-buffer, shared by painting, picking and annotations.
//
// Larger Camera.project depth is nearer the viewer. Pixel centres are sampled
// at (x+.5,y+.5), and planar depth is interpolated rather than sorting polygons
// by their mean. The local NumPy/Pillow runtime requires no GPU or network.
//
// W C++ tablice numpy są zwykłymi buforami; arytmetyka float32 odtwarza numpy 2
// (NEP 50). Kompilować bez -ffast-math i bez łączenia działań w FMA
// (-ffp-contract=off), aby wyniki były identyczne co do bitu.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace tk { class Canvas; }

namespace depth_raster {

using json = core::json;
using Point2 = std::array<double, 2>;

// Ear clipping for simple concave faces; triangles/convex quads stay cheap.
std::vector<std::array<int, 3>> triangulate(const std::vector<Point2>& points);
std::vector<std::array<int, 3>> triangulate(const json& points);

template <class T> struct Buffer2D {
    int rows = 0, cols = 0;
    std::vector<T> data;
    Buffer2D() = default;
    Buffer2D(int rows, int cols, T value) : rows(rows), cols(cols), data((size_t)rows * cols, value) {}
    T& at(int y, int x) { return data[(size_t)y * cols + x]; }
    const T& at(int y, int x) const { return data[(size_t)y * cols + x]; }
};

// Atrybuty, które Python dopisuje do obiektu Canvas (canvas._depth_buffer itd.).
// Płótna współpracujące z rastrem dziedziczą tę strukturę.
struct CanvasState {
    virtual ~CanvasState() = default;
    std::optional<Buffer2D<float>> _depth_buffer;
    std::optional<Buffer2D<int32_t>> _id_buffer;
    std::vector<std::string> _depth_ids;
    double _depth_sample_scale = 1;
    json _render_diagnostics = json::object();      // canvas._render_diagnostics (gpu_viewport)
    std::optional<std::string> _gpu_error;          // canvas._gpu_error (gpu_viewport)
    std::function<std::optional<std::string>(double, double)> _native_pick;
    std::function<json(double, double)> _native_depth;
    std::function<json(double, double)> _native_world;  // native_viewport: trafienie z punktem 3D
};

// Wynik Image.fromarray(...).resize(..., NEAREST)
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

class DepthRaster {
public:
    std::pair<int, int> output_size;
    int width, height;
    double scale_x, scale_y;
    std::vector<std::string> ids;
    std::map<std::string, int32_t> lookup;
    Buffer2D<float> depth;
    Buffer2D<int32_t> owner;
    std::vector<uint8_t> rgba;
    long long triangles = 0;

    DepthRaster(std::pair<double, double> size, const std::vector<std::string>& ids, double scale = 1);

    void polygon(std::vector<Point2> points, const std::vector<double>& depths, const std::string& color, const std::string& owner);
    void convex(const std::vector<Point2>& points, const std::vector<double>& depths, const std::string& color, const std::string& owner);
    void _write(int x0, int x1, int y0, int y1, const std::vector<uint8_t>& mask, const std::vector<float>& z, const std::string& color, const std::string& owner);
    void triangle(const std::array<Point2, 3>& points, const std::array<double, 3>& depths, const std::string& color, const std::string& owner);
    void line(std::vector<Point2> points, const std::vector<double>& depths, const std::string& color, const std::optional<std::string>& owner = std::nullopt,
              double width = 1, bool ghost = false, double tolerance = .6);
    Image image() const;
    // Cheap motion edges follow the same visible ownership as picking.
    void outline_visible(const std::vector<std::string>& selected_ids = {});
    void attach(tk::Canvas* canvas) const;
};

std::optional<std::string> pick_visible(tk::Canvas* canvas, double x, double y);

bool screen_visibility(tk::Canvas* canvas, double x, double y, std::optional<double> depth = std::nullopt, const std::optional<std::string>& owner = std::nullopt, double tolerance = .8);

}  // namespace depth_raster
