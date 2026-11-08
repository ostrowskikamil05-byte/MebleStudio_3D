// Lightweight Tk scene dressing, independent from production geometry.
//
// All model polygons and their hit-test coordinates remain those returned by
// core.render_faces. The floor, shadows and grain are presentation only; no CAD
// kernel, bitmap assets, network connection or third-party renderer is involved.
//
// Tłumaczenie 1:1 viewport_scene.py. Ściany malowane są przez DepthRaster
// (odpowiednik gałęzi `hasattr(canvas, 'tk')`); pole `vector_items` włącza
// gałąź dla płótna bez Tk (osobne elementy wektorowe), używaną w Pythonie
// przez atrapy płótna w testach.
#pragma once
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core.h"
#include "tk.h"
#include "viewport_render.h"

namespace viewport_scene {

using json = core::json;
using Point3 = core::Point3;
using Point2 = std::array<double, 2>;

inline const std::string SKY_TOP = "#edf1f3";
inline const std::string SKY_BOTTOM = "#faf8f3";
inline const std::string FLOOR = "#ece8e0";
inline const std::string GRID = "#d9d6cf";
inline const std::string SELECTION = "#df903f";

// @lru_cache(maxsize=2048) — funkcja czysta, wynik nie zależy od pamięci podręcznej
std::string mix_color(const std::string& first, const std::string& second, double amount);

// Fast display bounds including purchased legs, handles and mechanisms.
std::vector<Point3> scene_vertices(const core::Project& project, bool include_fittings = true, bool include_room = true,
                                   viewport_render::Context* context = nullptr);
std::optional<std::pair<Point3, Point3>> scene_bounds(const core::Project& project, bool include_fittings = true);

// Fit the visible furniture AND purchased accessories; preserve orientation.
void fit_scene(core::Camera& camera, const core::Project& project, std::pair<double, double> size, double padding = 100,
               bool include_fittings = true);

// A readable 1/2/5 series in millimetres, rather than a screen-space grid.
double grid_spacing(double scale);

struct BackgroundOptions {
    bool grid = true, shadows = true, include_fittings = true, interactive = false;
    viewport_render::Context* context = nullptr;
};

// Draw before model polygons; return floor level and the displayed grid step.
json paint_background(tk::Canvas* canvas, const core::Camera& camera, core::ProjectPtr project, std::pair<double, double> size,
                      const BackgroundOptions& options);
inline json paint_background(tk::Canvas* canvas, const core::Camera& camera, core::ProjectPtr project, std::pair<double, double> size,
                             bool interactive) {
    BackgroundOptions options;
    options.interactive = interactive;
    return paint_background(canvas, camera, std::move(project), size, options);
}

double polygon_area(const std::vector<Point2>& points);

// Clip parallel grain strokes to an arbitrary simple screen polygon.
// `seed` (identyfikator elementu) włącza losowe, różne dla każdej płyty rozmieszczenie linii; pusty daje stary, równy rozkład.
std::vector<std::pair<Point2, Point2>> grain_segments(const std::vector<Point2>& points, const Point2& direction, int count = 5, const std::string& seed = "");

struct PaintFacesOptions {
    std::set<std::string> selected_ids;
    bool wood = true;
    std::optional<core::Camera> camera;
    std::pair<double, double> size{-1, -1};  // None: rozmiar płótna (winfo_width, winfo_height)
    bool xray = false;
    double explode = 0;
    bool interactive = false;
    std::optional<std::set<std::string>> xray_ids;
    // getattr(faces, 'stats', {}): diagnostyka renderera programowego dołączona do listy ścian
    std::optional<json> stats;
    // true: płótno bez Tk — osobne elementy wektorowe zamiast rastra głębi
    bool vector_items = false;
};

// Paint already sorted model faces without changing selection geometry.
json paint_faces(tk::Canvas* canvas, const core::Drawn& faces, core::ProjectPtr project, const PaintFacesOptions& options = {});

// Small orientation triad and readable grid legend; draw after the model.
void paint_overlay(tk::Canvas* canvas, const core::Camera& camera, std::pair<double, double> size, const json& spacing = nullptr,
                   bool axes = true);

}  // namespace viewport_scene
