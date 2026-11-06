// Orthographic software z-buffer, shared by painting, picking and annotations.
//
// Larger Camera.project depth is nearer the viewer. Pixel centres are sampled
// at (x+.5,y+.5), and planar depth is interpolated rather than sorting polygons
// by their mean. The local NumPy/Pillow runtime requires no GPU or network.
#include "depth_raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include "tk.h"

namespace depth_raster {

using py::ValueError;

namespace {

// Liczba Pythona w działaniu z tablicą float32 (numpy 2, NEP 50): rzutowana na float32.
inline float f32(double value) { return (float)value; }

std::array<uint8_t, 4> _rgb(const std::string& color) {
    return {(uint8_t)py::int16(color.substr(1, 2)), (uint8_t)py::int16(color.substr(3, 2)), (uint8_t)py::int16(color.substr(5, 2)), 255};
}

struct Bounds { int x0, x1, y0, y1; };

template <class Points> Bounds bounds(const Points& points, int width, int height) {
    double min_x = points[0][0], max_x = points[0][0], min_y = points[0][1], max_y = points[0][1];
    for (auto& p : points) { min_x = std::min(min_x, p[0]); max_x = std::max(max_x, p[0]); min_y = std::min(min_y, p[1]); max_y = std::max(max_y, p[1]); }
    return {std::max(0, (int)std::ceil(min_x - .5)), std::min(width - 1, (int)std::floor(max_x - .5)),
            std::max(0, (int)std::ceil(min_y - .5)), std::min(height - 1, (int)std::floor(max_y - .5))};
}

CanvasState* state_of(tk::Canvas* canvas) { return dynamic_cast<CanvasState*>(canvas); }

}  // namespace

std::vector<std::array<int, 3>> triangulate(const std::vector<Point2>& points) {
    // Ear clipping for simple concave faces; triangles/convex quads stay cheap.
    int count = (int)points.size();
    if (count < 3) return {};
    if (count == 3) return {{0, 1, 2}};
    double area = 0;
    for (int i = 0; i < count; ++i) { const Point2& a = points[i]; const Point2& b = points[(i + 1) % count]; area += a[0] * b[1] - b[0] * a[1]; }
    if (std::abs(area) < 1e-10) return {};
    int sign = area > 0 ? 1 : -1;
    auto cross = [&](const Point2& a, const Point2& b, const Point2& c) { return ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) * sign; };
    bool convex = true;
    for (int i = 0; i < count; ++i) if (!(cross(points[(i - 1 + count) % count], points[i], points[(i + 1) % count]) >= -1e-8)) convex = false;
    if (convex) {
        std::vector<std::array<int, 3>> fan; for (int i = 1; i < count - 1; ++i) fan.push_back({0, i, i + 1});
        return fan;
    }
    std::vector<int> remaining; for (int i = 0; i < count; ++i) remaining.push_back(i);
    std::vector<std::array<int, 3>> result;
    while (remaining.size() > 3) {
        bool found = false;
        int n = (int)remaining.size();
        for (int k = 0; k < n; ++k) {
            int b = remaining[k]; int a = remaining[(k - 1 + n) % n], c = remaining[(k + 1) % n];
            if (cross(points[a], points[b], points[c]) <= 1e-9) continue;
            bool occupied = false;
            for (int j : remaining) {
                if (j == a || j == b || j == c) continue;
                if (cross(points[a], points[b], points[j]) > 1e-9 &&
                    cross(points[b], points[c], points[j]) > 1e-9 &&
                    cross(points[c], points[a], points[j]) > 1e-9) {
                    occupied = true; break;
                }
            }
            if (occupied) continue;
            result.push_back({a, b, c}); remaining.erase(remaining.begin() + k); found = true; break;
        }
        if (!found) {
            throw ValueError("Kontur powierzchni widoku jest samoprzecinający lub zdegenerowany.");
        }
    }
    result.push_back({remaining[0], remaining[1], remaining[2]});
    return result;
}

std::vector<std::array<int, 3>> triangulate(const json& points) {
    std::vector<Point2> list; for (auto& p : points) list.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return triangulate(list);
}

DepthRaster::DepthRaster(std::pair<double, double> size, const std::vector<std::string>& ids_, double scale) {
    output_size = {std::max(1, (int)size.first), std::max(1, (int)size.second)};
    width = std::max(1, (int)py::round_half_even(output_size.first * scale)); height = std::max(1, (int)py::round_half_even(output_size.second * scale));
    scale_x = (double)width / output_size.first; scale_y = (double)height / output_size.second;
    std::set<std::string> unique(ids_.begin(), ids_.end()); ids.assign(unique.begin(), unique.end());
    for (size_t i = 0; i < ids.size(); ++i) lookup[ids[i]] = (int32_t)i;
    depth = Buffer2D<float>(height, width, -std::numeric_limits<float>::infinity());
    owner = Buffer2D<int32_t>(height, width, -1);
    rgba.assign((size_t)height * width * 4, 0);
    triangles = 0;
}

void DepthRaster::polygon(std::vector<Point2> points, const std::vector<double>& depths, const std::string& color, const std::string& owner_) {
    if (points.size() != depths.size()) throw ValueError("Powierzchnia ma inną liczbę punktów i głębokości.");
    if (scale_x != 1 || scale_y != 1) {
        for (auto& p : points) p = {p[0] * scale_x, p[1] * scale_y};
    }
    if (points.empty()) return;
    Bounds b = bounds(points, width, height);
    if (b.x1 < b.x0 || b.y1 < b.y0) return;
    // Near faces are submitted first. A covered bounding rectangle can
    // reject hidden shelves/hardware before allocating barycentric arrays.
    float nearest = std::numeric_limits<float>::infinity();
    for (int y = b.y0; y <= b.y1; ++y) for (int x = b.x0; x <= b.x1; ++x) nearest = std::min(nearest, depth.at(y, x));
    if (nearest > f32(*std::max_element(depths.begin(), depths.end()) + .0005)) return;
    size_t count = points.size();
    if (count > 3) {
        std::vector<double> cross;
        for (size_t i = 0; i < count; ++i) {
            const Point2& prev = points[(i + count - 1) % count]; const Point2& cur = points[i]; const Point2& next = points[(i + 1) % count];
            cross.push_back((cur[0] - prev[0]) * (next[1] - cur[1]) - (cur[1] - prev[1]) * (next[0] - cur[0]));
        }
        if (std::all_of(cross.begin(), cross.end(), [](double v) { return v >= -1e-7; }) || std::all_of(cross.begin(), cross.end(), [](double v) { return v <= 1e-7; })) {
            convex(points, depths, color, owner_);
            return;
        }
    }
    for (auto& t : triangulate(points)) {
        triangle({points[t[0]], points[t[1]], points[t[2]]}, {depths[t[0]], depths[t[1]], depths[t[2]]}, color, owner_);
    }
}

void DepthRaster::convex(const std::vector<Point2>& points, const std::vector<double>& depths, const std::string& color, const std::string& owner_) {
    Bounds bb = bounds(points, width, height);
    if (bb.x1 < bb.x0 || bb.y1 < bb.y0) return;
    const Point2& first = points[0]; std::optional<std::tuple<size_t, size_t, double>> triangle;
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        const Point2& a = points[i]; const Point2& b = points[i + 1];
        double area = (a[0] - first[0]) * (b[1] - first[1]) - (a[1] - first[1]) * (b[0] - first[0]);
        if (std::abs(area) > 1e-8) { triangle = std::make_tuple(i, i + 1, area); break; }
    }
    if (!triangle) return;
    auto [i, j, area] = *triangle; const Point2& a = points[i]; const Point2& b = points[j]; int sign = area > 0 ? 1 : -1;
    int w = bb.x1 - bb.x0 + 1, h = bb.y1 - bb.y0 + 1;
    std::vector<float> xs(w), ys(h);
    for (int k = 0; k < w; ++k) xs[k] = (float)(bb.x0 + k) + .5f;
    for (int k = 0; k < h; ++k) ys[k] = (float)(bb.y0 + k) + .5f;
    std::vector<uint8_t> mask((size_t)w * h, 1);
    for (size_t e = 0; e < points.size(); ++e) {
        const Point2& p = points[e]; const Point2& q = points[(e + 1) % points.size()];
        float qx = f32(q[0] - p[0]), qy = f32(q[1] - p[1]), px = f32(p[0]), pyv = f32(p[1]), s = f32(sign), limit = f32(-1e-5);
        for (int y = 0; y < h; ++y) {
            float term_y = ys[y] - pyv; float left = qx * term_y;
            for (int x = 0; x < w; ++x) {
                float term_x = xs[x] - px; float right = qy * term_x; float value = left - right; value = value * s;
                mask[(size_t)y * w + x] &= value >= limit;
            }
        }
    }
    double dx = ((b[1] - first[1]) * (depths[i] - depths[0]) - (a[1] - first[1]) * (depths[j] - depths[0])) / area;
    double dy = (-(b[0] - first[0]) * (depths[i] - depths[0]) + (a[0] - first[0]) * (depths[j] - depths[0])) / area;
    std::vector<float> z((size_t)w * h);
    float d0 = f32(depths[0]), fdx = f32(dx), fdy = f32(dy), fx = f32(first[0]), fy = f32(first[1]);
    for (int y = 0; y < h; ++y) {
        float column = ys[y] - fy; column = fdy * column;
        for (int x = 0; x < w; ++x) {
            float row = xs[x] - fx; row = fdx * row; row = d0 + row;
            z[(size_t)y * w + x] = row + column;
        }
    }
    _write(bb.x0, bb.x1, bb.y0, bb.y1, mask, z, color, owner_);
    triangles += 1;
}

void DepthRaster::_write(int x0, int x1, int y0, int y1, const std::vector<uint8_t>& mask, const std::vector<float>& z, const std::string& color, const std::string& owner_) {
    int32_t index = lookup.at(owner_); auto rgb = _rgb(color); int w = x1 - x0 + 1;
    const float tolerance = .0005f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            size_t k = (size_t)(y - y0) * w + (x - x0);
            float& target = depth.at(y, x); int32_t& owners = owner.at(y, x); float zk = z[k];
            float difference = zk - target; bool equal = std::abs(difference) <= tolerance;
            float raised = target + tolerance;
            bool visible = mask[k] && ((zk > raised) || (equal && (owners < 0 || index < owners)));
            if (visible) {
                target = zk; owners = index;
                std::copy(rgb.begin(), rgb.end(), rgba.begin() + ((size_t)y * width + x) * 4);
            }
        }
    }
}

void DepthRaster::triangle(const std::array<Point2, 3>& points, const std::array<double, 3>& depths, const std::string& color, const std::string& owner_) {
    auto [ax, ay] = points[0]; auto [bx, by] = points[1]; auto [cx, cy] = points[2];
    double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::abs(area) < 1e-9) return;
    Bounds bb = bounds(points, width, height);
    if (bb.x1 < bb.x0 || bb.y1 < bb.y0) return;
    // Broadcast rows/columns, avoiding a full meshgrid allocation per face.
    int w = bb.x1 - bb.x0 + 1, h = bb.y1 - bb.y0 + 1;
    std::vector<float> xs(w), ys(h);
    for (int k = 0; k < w; ++k) { float v = (float)(bb.x0 + k) + .5f; xs[k] = v - f32(ax); }
    for (int k = 0; k < h; ++k) { float v = (float)(bb.y0 + k) + .5f; ys[k] = v - f32(ay); }
    float c1 = f32(cy - ay), c2 = f32(cx - ax), c3 = f32(-(by - ay)), c4 = f32(bx - ax), fa = f32(area);
    float d0 = f32(depths[0]), d1 = f32(depths[1] - depths[0]), d2 = f32(depths[2] - depths[0]);
    float low = f32(-1e-6), high = f32(1.000001);
    std::vector<uint8_t> mask((size_t)w * h); std::vector<float> z((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float p1 = c1 * xs[x], p2 = c2 * ys[y]; float b = p1 - p2; b = b / fa;
            float q1 = c3 * xs[x], q2 = c4 * ys[y]; float c = q1 + q2; c = c / fa;
            float sum = b + c;
            size_t k = (size_t)y * w + x;
            mask[k] = b >= low && c >= low && sum <= high;
            float zb = b * d1; float zz = d0 + zb; float zc = c * d2; z[k] = zz + zc;
        }
    }
    // Identical coplanar surfaces use stable owner ordering, so rotations
    // cannot change their identity according to polygon painter order.
    _write(bb.x0, bb.x1, bb.y0, bb.y1, mask, z, color, owner_);
    triangles += 1;
}

void DepthRaster::line(std::vector<Point2> points, const std::vector<double>& depths, const std::string& color, const std::optional<std::string>& owner_, double line_width, bool ghost, double tolerance) {
    if (points.size() != depths.size()) throw ValueError("Obrys ma inną liczbę punktów i głębokości.");
    if (scale_x != 1 || scale_y != 1) {
        for (auto& p : points) p = {p[0] * scale_x, p[1] * scale_y};
        line_width = std::max(1.0, line_width * std::min(scale_x, scale_y));
    }
    int32_t index = -1;
    if (owner_) { auto found = lookup.find(*owner_); if (found != lookup.end()) index = found->second; }
    auto rgb = _rgb(color); float tol = f32(tolerance);
    for (size_t s = 0; s + 1 < points.size() && s + 1 < depths.size(); ++s) {
        const Point2& a = points[s]; const Point2& b = points[s + 1]; double za = depths[s], zb = depths[s + 1];
        double dx = b[0] - a[0], dy = b[1] - a[1];
        if (std::max(std::abs(dx), std::abs(dy)) < .001) continue;
        long long steps = std::min((long long)std::max(width, height) * 4, std::max(1LL, (long long)std::ceil(std::max(std::abs(dx), std::abs(dy)))));
        // np.linspace(0, 1, steps+1, dtype=float32): liczone w float64, potem rzutowane
        std::vector<float> t(steps + 1); double step = 1.0 / steps;
        for (long long i = 0; i <= steps; ++i) t[i] = (float)(i * step);
        t[steps] = 1.0f;
        std::vector<int32_t> xs(steps + 1), ys(steps + 1); std::vector<float> zs(steps + 1);
        float fax = f32(a[0]), fay = f32(a[1]), fdx = f32(dx), fdy = f32(dy), fza = f32(za), fdz = f32(zb - za);
        for (long long i = 0; i <= steps; ++i) {
            float px = fdx * t[i]; px = fax + px; xs[i] = (int32_t)std::floor(px);
            float pyv = fdy * t[i]; pyv = fay + pyv; ys[i] = (int32_t)std::floor(pyv);
            float pz = fdz * t[i]; zs[i] = fza + pz;
        }
        int radius = std::max(0, (int)line_width / 2);
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                // Najpierw cała maska widoczności (jak wektorowo w numpy), potem zapis.
                std::vector<std::array<int, 2>> cells; std::vector<float> values;
                for (long long i = 0; i <= steps; ++i) {
                    int x = xs[i] + ox, y = ys[i] + oy;
                    if (!(x >= 0 && x < width && y >= 0 && y < height)) continue;
                    float limit = depth.at(y, x) - tol;
                    bool visible = zs[i] >= limit;
                    if (owner_ && !ghost) visible = visible && owner.at(y, x) == index;
                    if (visible) { cells.push_back({x, y}); values.push_back(zs[i]); }
                }
                for (size_t k = 0; k < cells.size(); ++k) {
                    auto [x, y] = cells[k];
                    std::copy(rgb.begin(), rgb.end(), rgba.begin() + ((size_t)y * width + x) * 4);
                }
                if (ghost && index >= 0) {
                    for (size_t k = 0; k < cells.size(); ++k) owner.at(cells[k][1], cells[k][0]) = index;
                    for (size_t k = 0; k < cells.size(); ++k) depth.at(cells[k][1], cells[k][0]) = values[k];
                }
            }
        }
    }
}

Image DepthRaster::image() const {
    Image result{width, height, rgba};
    if (std::make_pair(width, height) != output_size) {
        // Pillow: Image.resize(size, NEAREST) -> ImagingScaleAffine
        auto [out_w, out_h] = output_size;
        Image scaled{out_w, out_h, std::vector<uint8_t>((size_t)out_w * out_h * 4, 0)};
        double a0 = (double)width / out_w, a4 = (double)height / out_h;
        auto coord = [](double v) { return v < 0.0 ? -1 : (int)v; };
        std::vector<int> xintab(out_w, -1);
        double xo = 0 + a0 * 0.5;
        for (int x = 0; x < out_w; ++x) { int xin = coord(xo); if (xin >= 0 && xin < width) xintab[x] = xin; xo += a0; }
        double yo = 0 + a4 * 0.5;
        for (int y = 0; y < out_h; ++y) {
            int yi = coord(yo);
            if (yi >= 0 && yi < height)
                for (int x = 0; x < out_w; ++x)
                    if (xintab[x] >= 0) std::copy_n(rgba.begin() + ((size_t)yi * width + xintab[x]) * 4, 4, scaled.rgba.begin() + ((size_t)y * out_w + x) * 4);
            yo += a4;
        }
        result = scaled;
    }
    return result;
}

void DepthRaster::outline_visible(const std::vector<std::string>& selected_ids) {
    // Cheap motion edges follow the same visible ownership as picking.
    int h = height, w = width;
    std::vector<uint8_t> edge((size_t)h * w, 0);
    auto o = [&](int y, int x) { return owner.at(y, x); };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool e = false;
            if (y >= 1 && o(y, x) != o(y - 1, x)) e = true;
            if (y + 1 < h && o(y, x) != o(y + 1, x)) e = true;
            if (x >= 1 && o(y, x) != o(y, x - 1)) e = true;
            if (x + 1 < w && o(y, x) != o(y, x + 1)) e = true;
            edge[(size_t)y * w + x] = e && o(y, x) >= 0;
        }
    }
    const float factor = .78f;
    for (size_t k = 0; k < edge.size(); ++k) {
        if (!edge[k]) continue;
        for (int c = 0; c < 3; ++c) { float value = (float)rgba[k * 4 + c] * factor; rgba[k * 4 + c] = (uint8_t)value; }
    }
    for (auto& selected : selected_ids) {
        auto found = lookup.find(selected);
        if (found == lookup.end()) continue;
        for (size_t k = 0; k < edge.size(); ++k)
            if (edge[k] && owner.data[k] == found->second) { rgba[k * 4] = 66; rgba[k * 4 + 1] = 137; rgba[k * 4 + 2] = 174; }
    }
}

void DepthRaster::attach(tk::Canvas* canvas) const {
    CanvasState* state = state_of(canvas);
    if (state == nullptr) throw std::logic_error("depth_raster.attach: płótno musi dziedziczyć depth_raster::CanvasState");
    if (std::make_pair(width, height) != output_size) {
        auto [out_w, out_h] = output_size;
        std::vector<int> xs(out_w), ys(out_h);
        for (int i = 0; i < out_w; ++i) xs[i] = std::min(width - 1, (int)std::floor((i + .5) * scale_x));
        for (int i = 0; i < out_h; ++i) ys[i] = std::min(height - 1, (int)std::floor((i + .5) * scale_y));
        Buffer2D<float> depth_buffer(out_h, out_w, 0); Buffer2D<int32_t> id_buffer(out_h, out_w, 0);
        for (int y = 0; y < out_h; ++y) for (int x = 0; x < out_w; ++x) { depth_buffer.at(y, x) = depth.at(ys[y], xs[x]); id_buffer.at(y, x) = owner.at(ys[y], xs[x]); }
        state->_depth_buffer = depth_buffer; state->_id_buffer = id_buffer;
    } else {
        state->_depth_buffer = depth;
        state->_id_buffer = owner;
    }
    state->_depth_ids = ids;
    state->_depth_sample_scale = std::min(scale_x, scale_y);
}

std::optional<std::string> pick_visible(tk::Canvas* canvas, double x_, double y_) {
    CanvasState* state = state_of(canvas);
    if (state && state->_native_pick) return state->_native_pick(x_, y_);
    if (!state || !state->_id_buffer) return std::nullopt;
    const auto& ids = *state->_id_buffer;
    int x = (int)std::floor(x_), y = (int)std::floor(y_);
    if (!(0 <= y && y < ids.rows && 0 <= x && x < ids.cols)) return std::nullopt;
    int index = ids.at(y, x);
    return index >= 0 ? std::optional<std::string>(state->_depth_ids.at(index)) : std::nullopt;
}

bool screen_visibility(tk::Canvas* canvas, double x_, double y_, std::optional<double> depth, const std::optional<std::string>& owner, double tolerance) {
    CanvasState* state = state_of(canvas);
    if (state && state->_native_depth) {
        if (!(0 <= x_ && x_ < canvas->winfo_width() && 0 <= y_ && y_ < canvas->winfo_height())) return false;
        json hit = state->_native_depth(x_, y_);
        if (hit.is_null()) return true;
        if (depth && *depth < hit.at("depth").get<double>() - tolerance) return false;
        if (owner && !depth && hit.at("id") != *owner) return false;
        return true;
    }
    if (!state || !state->_depth_buffer) return true;
    const auto& buffer = *state->_depth_buffer;
    int x = (int)std::floor(x_), y = (int)std::floor(y_);
    if (!(0 <= y && y < buffer.rows && 0 <= x && x < buffer.cols)) return false;
    double surface = buffer.at(y, x);
    if (depth && *depth < surface - tolerance) return false;
    if (owner) {
        auto visible = pick_visible(canvas, x, y);
        if (visible && *visible != *owner && !depth) return false;
    }
    return true;
}

// Verified C++ calculation ports; UI and exact CAD validators are retained.
// (W Pythonie: native_model.install_services(globals(), 'depth_raster') — tu bez odpowiednika.)

}  // namespace depth_raster
