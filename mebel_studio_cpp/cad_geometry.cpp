// Optional OpenCascade CAD kernel, exact edge fillets and STEP solids.
//
// Python korzystał z cadquery (nakładki na OpenCascade). Tu te same operacje
// są wywołane bezpośrednio w OpenCascade, tak jak robi to cadquery pod spodem:
// Workplane.extrude() kończy się ShapeUpgrade_UnifySameDomain, Face.makeFromWires
// naprawia kontur i orientację, positionAt/tangentAt liczą parametr po długości łuku.
#include "cad_geometry.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <tuple>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_FindSurface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeArcOfEllipse.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Trsf.hxx>

#include "features.h"

namespace cad_geometry {

using core::Part;
using core::Point;
using core::Point3;
using py::ValueError;

namespace {

constexpr double DEG2RAD = std::numbers::pi / 180;

double num(const json& value) { return value.get<double>(); }

double dist(const Point3& a, const Point3& b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
double dist2d(const Point3& a, const Point3& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

gp_Pnt pnt(const Point3& p) { return gp_Pnt(p[0], p[1], p[2]); }
Point3 tuple(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }

// shape.Edges(), .Vertices(), .Faces(), .Solids(): unikalne podkształty w kolejności eksploracji
std::vector<TopoDS_Shape> entities(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map; TopExp::MapShapes(shape, type, map);
    std::vector<TopoDS_Shape> out; for (int i = 1; i <= map.Extent(); ++i) out.push_back(map(i));
    return out;
}

std::vector<TopoDS_Edge> edges_of(const TopoDS_Shape& shape) {
    std::vector<TopoDS_Edge> out; for (auto& e : entities(shape, TopAbs_EDGE)) out.push_back(TopoDS::Edge(e));
    return out;
}

// Edge.geomType()
std::string geom_type(const TopoDS_Edge& edge) {
    switch (BRepAdaptor_Curve(edge).GetType()) {
        case GeomAbs_Line: return "LINE";
        case GeomAbs_Circle: return "CIRCLE";
        case GeomAbs_Ellipse: return "ELLIPSE";
        case GeomAbs_Hyperbola: return "HYPERBOLA";
        case GeomAbs_Parabola: return "PARABOLA";
        case GeomAbs_BezierCurve: return "BEZIER";
        case GeomAbs_BSplineCurve: return "BSPLINE";
        case GeomAbs_OffsetCurve: return "OFFSET";
        default: return "OTHER";
    }
}

// Edge.Length()
double edge_length(const TopoDS_Edge& edge) { BRepAdaptor_Curve curve(edge); return GCPnts_AbscissaPoint::Length(curve); }

// Mixin1D.paramAt(d): parametr po znormalizowanej długości łuku
double param_at(const BRepAdaptor_Curve& curve, double d) {
    double l = GCPnts_AbscissaPoint::Length(curve);
    return GCPnts_AbscissaPoint(curve, l * d, curve.FirstParameter()).Parameter();
}

// Edge.positionAt(d).toTuple()
Point3 position_at(const TopoDS_Edge& edge, double d) {
    BRepAdaptor_Curve curve(edge);
    return tuple(curve.Value(param_at(curve, d)));
}

// Edge.tangentAt(d).toTuple()
Point3 tangent_at(const TopoDS_Edge& edge, double d) {
    BRepAdaptor_Curve curve(edge); gp_Pnt p; gp_Vec v;
    curve.D1(param_at(curve, d), p, v);
    gp_Dir dir(v);
    return {dir.X(), dir.Y(), dir.Z()};
}

// Shape.Volume()
double volume(const TopoDS_Shape& shape) { GProp_GProps props; BRepGProp::VolumeProperties(shape, props); return props.Mass(); }

// Shape.isValid()
bool is_valid(const TopoDS_Shape& shape) { return BRepCheck_Analyzer(shape).IsValid(); }

// Shape.intersect / Shape.cut (Shape._bool_op)
template <class Op> TopoDS_Shape bool_op(const TopoDS_Shape& arg, const TopoDS_Shape& tool) {
    Op op; TopTools_ListOfShape args, tools; args.Append(arg); tools.Append(tool);
    op.SetArguments(args); op.SetTools(tools); op.SetRunParallel(true); op.Build();
    return op.Shape();
}
TopoDS_Shape intersect(const TopoDS_Shape& a, const TopoDS_Shape& b) { return bool_op<BRepAlgoAPI_Common>(a, b); }
TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b) { return bool_op<BRepAlgoAPI_Cut>(a, b); }

TopoDS_Shape transformed(const TopoDS_Shape& shape, const gp_Trsf& trsf) { return BRepBuilderAPI_Transform(shape, trsf, true).Shape(); }

// Shape.translate(vector)
TopoDS_Shape translate(const TopoDS_Shape& shape, const Point3& v) { gp_Trsf t; t.SetTranslation(gp_Vec(v[0], v[1], v[2])); return transformed(shape, t); }

// Shape.rotate(start, end, angleDegrees)
TopoDS_Shape rotate(const TopoDS_Shape& shape, const Point3& start, const Point3& end, double angle) {
    gp_Trsf t; t.SetRotation(gp_Ax1(pnt(start), gp_Dir(end[0] - start[0], end[1] - start[1], end[2] - start[2])), angle * DEG2RAD);
    return transformed(shape, t);
}

// Solid.makeCylinder / makeBox / makeCone
TopoDS_Shape make_cylinder(double radius, double height, const Point3& p, const Point3& d = {0, 0, 1}) {
    return BRepPrimAPI_MakeCylinder(gp_Ax2(pnt(p), gp_Dir(d[0], d[1], d[2])), radius, height, 2 * std::numbers::pi).Shape();
}
TopoDS_Shape make_box(double length, double width, double height, const Point3& p, const Point3& d = {0, 0, 1}) {
    return BRepPrimAPI_MakeBox(gp_Ax2(pnt(p), gp_Dir(d[0], d[1], d[2])), length, width, height).Shape();
}
TopoDS_Shape make_cone(double radius1, double radius2, double height, const Point3& p, const Point3& d) {
    return BRepPrimAPI_MakeCone(gp_Ax2(pnt(p), gp_Dir(d[0], d[1], d[2])), radius1, radius2, height, 2 * std::numbers::pi).Shape();
}

// Edge.makeLine / makeThreePointArc / makeEllipse / makeCircle
TopoDS_Edge make_line(const Point3& a, const Point3& b) { return BRepBuilderAPI_MakeEdge(pnt(a), pnt(b)).Edge(); }
TopoDS_Edge make_three_point_arc(const Point3& a, const Point3& m, const Point3& b) {
    Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfCircle(pnt(a), pnt(m), pnt(b)).Value();
    return BRepBuilderAPI_MakeEdge(arc).Edge();
}
TopoDS_Edge make_ellipse(double x_radius, double y_radius, const Point3& center, double angle1 = 360, double angle2 = 360) {
    gp_Pnt p = pnt(center); gp_Dir dir(0, 0, 1), xdir(1, 0, 0);
    gp_Ax1 ax1(p, dir); gp_Ax2 ax2(p, dir, xdir);
    double correction_angle; gp_Elips ellipse;
    if (y_radius > x_radius) {
        // swap x and y radius and rotate by 90° afterwards to create an ellipse with x_radius < y_radius
        correction_angle = 90.0 * DEG2RAD;
        ellipse = gp_Elips(ax2, y_radius, x_radius).Rotated(ax1, correction_angle);
    } else {
        correction_angle = 0.0;
        ellipse = gp_Elips(ax2, x_radius, y_radius);
    }
    if (angle1 == angle2) return BRepBuilderAPI_MakeEdge(ellipse).Edge();
    Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfEllipse(ellipse, angle1 * DEG2RAD - correction_angle, angle2 * DEG2RAD - correction_angle, true).Value();
    return BRepBuilderAPI_MakeEdge(arc).Edge();
}
TopoDS_Edge make_circle(double radius, const Point3& center) {
    return BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(pnt(center), gp_Dir(0, 0, 1)), radius)).Edge();
}

// Wire.assembleEdges(edges)
TopoDS_Wire assemble_edges(const std::vector<TopoDS_Edge>& edges) {
    BRepBuilderAPI_MakeWire builder; TopTools_ListOfShape list;
    for (auto& e : edges) list.Append(e);
    builder.Add(list); builder.Build();
    if (!builder.IsDone()) {
        const char* names[] = {"BRepBuilderAPI_WireDone", "BRepBuilderAPI_EmptyWire", "BRepBuilderAPI_DisconnectedWire", "BRepBuilderAPI_NonManifoldWire"};
        throw ValueError(std::string("Wires not assembled.  ") + names[builder.Error()]);
    }
    return builder.Wire();
}

// Face.makeFromWires(outerWire)
TopoDS_Face face_from_wire(const TopoDS_Wire& outer) {
    if (!BRepLib_FindSurface(outer, -1, true).Found()) throw ValueError("Cannot build face(s): wires not planar");
    // fix outer wire
    ShapeFix_Shape sf_s(outer); sf_s.Perform();
    TopoDS_Wire wo = TopoDS::Wire(sf_s.Shape());
    BRepBuilderAPI_MakeFace builder(wo, true); builder.Build();
    if (!builder.IsDone()) throw ValueError(std::format("Cannot build face(s): {}", (int)builder.Error()));
    ShapeFix_Face sf_f(builder.Face()); sf_f.FixOrientation(); sf_f.Perform();
    return sf_f.Face();
}

// Solid.extrudeLinear(face, vecNormal)
TopoDS_Shape extrude_linear(const TopoDS_Face& face, double thickness) {
    return BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, thickness), true).Shape();
}

// Workplane(...).extrude(distance).val(): Compound z bryłą, oczyszczony przez clean()
TopoDS_Shape workplane_extrude(const TopoDS_Wire& wire, double distance) {
    TopoDS_Shape solid = extrude_linear(face_from_wire(wire), distance);
    BRep_Builder builder; TopoDS_Compound compound; builder.MakeCompound(compound); builder.Add(compound, solid);
    ShapeUpgrade_UnifySameDomain upgrader(compound, true, true, true);
    upgrader.AllowInternalEdges(false);
    upgrader.Build();
    return upgrader.Shape();
}

// Workplane('XY').polyline(points).close() -> wire
TopoDS_Wire closed_polyline(const std::vector<Point>& points) {
    std::vector<TopoDS_Edge> edges;
    for (size_t i = 1; i < points.size(); ++i) edges.push_back(make_line({points[i - 1][0], points[i - 1][1], 0}, {points[i][0], points[i][1], 0}));
    const Point& first = points.front(); const Point& last = points.back();
    if (std::hypot(last[0] - first[0], last[1] - first[1]) > 1e-6) edges.push_back(make_line({last[0], last[1], 0}, {first[0], first[1], 0}));
    return assemble_edges(edges);
}

// Workplane('XY').slot2D(length, diameter) -> wire
TopoDS_Wire slot2d(double length, double diameter, double angle = 0) {
    double radius = diameter / 2;
    Point3 p1{(-length / 2) + radius, diameter / 2, 0};
    Point3 p2{p1[0] + length - diameter, p1[1], 0};
    Point3 p3{p1[0] + length - diameter, p1[1] - diameter, 0};
    Point3 p4{p1[0], p1[1] - diameter, 0};
    Point3 arc1{p2[0] + radius, p2[1] - radius, 0};
    Point3 arc2{p4[0] - radius, p4[1] + radius, 0};
    TopoDS_Wire slot = assemble_edges({make_line(p1, p2), make_three_point_arc(p2, arc1, p3), make_line(p3, p4), make_three_point_arc(p4, arc2, p1)});
    return TopoDS::Wire(rotate(slot, {0, 0, 0}, {0, 0, 1}, angle));
}

std::vector<Point> points_from_json(const json& value) {
    std::vector<Point> out;
    for (auto& p : value) out.push_back({num(p[0]), num(p[1])});
    return out;
}

// zwykła krotka punktu (bez atrybutów EdgeAnchor)
EdgeAnchor plain(const Point3& point) { EdgeAnchor a; a.point = point; return a; }

json points_to_json(const std::vector<Point>& points) {
    json out = json::array();
    for (auto& p : points) out.push_back(json::array({p[0], p[1]}));
    return out;
}

}  // namespace

void kernel() {
    // W Pythonie: import cadquery, a przy jego braku ValueError('Brak modułu CAD. ...').
    // W C++ OpenCascade jest dołączany do programu podczas budowania, więc jądro CAD
    // jest zawsze dostępne.
}

std::vector<Point3> edge_path(const EdgeSpec& spec) {
    // Curve polyline for selection highlighting, not manufacturing geometry.
    if (spec.geometry.geom_type == "LINE") return {spec.geometry.point, spec.end};
    if (spec.geometry.geom_type) return spec.geometry.samples;
    return {spec.geometry.point, spec.end};
}

std::vector<EdgeSpec> _solid_edge_specs(const TopoDS_Shape& shape, double thickness) {
    // Actual rim edges and sharp vertical corners, excluding smooth seams.
    using Item = std::pair<TopoDS_Edge, std::vector<Point3>>;
    std::vector<TopoDS_Edge> edges = edges_of(shape); std::vector<Item> bottom, top, vertical;
    for (auto& edge : edges) {
        int steps = geom_type(edge) != "LINE" && entities(edge, TopAbs_VERTEX).size() == 1 ? 64 : 16;
        std::vector<Point3> samples; for (int i = 0; i <= steps; ++i) samples.push_back(position_at(edge, (double)i / steps));
        double max_z = 0, max_top = 0;
        for (auto& p : samples) { max_z = std::max(max_z, std::abs(p[2])); max_top = std::max(max_top, std::abs(p[2] - thickness)); }
        if (max_z < 1e-6) bottom.emplace_back(edge, samples);
        else if (max_top < 1e-6) top.emplace_back(edge, samples);
        else if (geom_type(edge) == "LINE" && std::abs(edge_length(edge) - thickness) < 1e-6 && dist2d(samples.front(), samples.back()) < 1e-6)
            vertical.emplace_back(edge, samples);
    }
    Bnd_Box box; BRepBndLib::AddOptimal(shape, box, true, false);
    double xmin, ymin, zmin, xmax, ymax, zmax; box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    double cx = (xmin + xmax) / 2, cy = (ymin + ymax) / 2;
    auto order = [&](const Item& item) {
        auto& [edge, samples] = item; const Point3& p = samples[samples.size() / 2];
        double angle = std::atan2(p[1] - cy, p[0] - cx);
        if (std::abs(std::abs(angle) - std::numbers::pi) < 1e-8) angle = -std::numbers::pi;
        if (std::abs(angle) < 1e-8) angle = 0;
        return std::make_tuple(py::round_to(angle, 10), geom_type(edge), py::round_to(p[0], 7), py::round_to(p[1], 7));
    };
    auto by_order = [&](const Item& a, const Item& b) { return order(a) < order(b); };
    std::stable_sort(bottom.begin(), bottom.end(), by_order); std::stable_sort(top.begin(), top.end(), by_order);
    auto label = [](const std::string& type) {
        if (type == "LINE") return std::string("odcinek prosty");
        if (type == "CIRCLE") return std::string("łuk kołowy");
        if (type == "ELLIPSE") return std::string("łuk eliptyczny");
        return type;
    };
    std::vector<EdgeSpec> result;
    for (auto& [kind, title, collection] : std::vector<std::tuple<std::string, std::string, std::vector<Item>*>>{{"bottom", "Dolna", &bottom}, {"top", "Górna", &top}}) {
        std::vector<size_t> parent(collection->size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = i;
        auto find = [&](size_t i) {
            while (parent[i] != i) i = parent[i];
            return i;
        };
        for (size_t i = 0; i < collection->size(); ++i) {
            const TopoDS_Edge& left = (*collection)[i].first;
            for (size_t j = i + 1; j < collection->size(); ++j) {
                const TopoDS_Edge& right = (*collection)[j].first;
                for (int li : {0, 1}) {
                    for (int ri : {0, 1}) {
                        if (dist(position_at(left, li), position_at(right, ri)) > 1e-6) continue;
                        Point3 lt = tangent_at(left, li), rt = tangent_at(right, ri);
                        if (std::abs(lt[0] * rt[0] + lt[1] * rt[1] + lt[2] * rt[2]) > 1 - 1e-7) parent[find(j)] = find(i);
                    }
                }
            }
        }
        for (size_t i = 0; i < collection->size(); ++i) {
            auto& [edge, samples] = (*collection)[i];
            EdgeAnchor a{samples.front(), geom_type(edge), samples, edge_length(edge), std::vector<std::string>{}};
            for (size_t j = 0; j < collection->size(); ++j) if (find(j) == find(i)) a.chain->push_back(std::format("{}:{}", kind, j));
            result.push_back({std::format("{}:{}", kind, i), std::format("{} krawędź {} · {}", title, i + 1, label(geom_type(edge))), a, samples.back()});
        }
    }
    std::vector<Item> sharp;
    for (auto& [edge, samples] : vertical) {
        const Point3& xy = samples.front(); std::vector<Point3> directions;
        for (auto& [rim, _] : bottom) {
            for (int parameter : {0, 1}) {
                Point3 point = position_at(rim, parameter);
                if (dist2d(xy, point) < 1e-6) {
                    directions.push_back(tangent_at(rim, parameter));
                }
            }
        }
        if (directions.size() >= 2) {
            const Point3& a = directions[0]; const Point3& b = directions[1];
            double dot = std::abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
            if (dot < 1 - 1e-7) sharp.emplace_back(edge, samples);
        }
    }
    std::stable_sort(sharp.begin(), sharp.end(), by_order);
    for (size_t i = 0; i < sharp.size(); ++i) {
        Point3 a = sharp[i].second.front(), b = sharp[i].second.back();
        if (a[2] > b[2]) std::swap(a, b);
        result.push_back({std::format("corner:{}", i), std::format("Ostry narożnik {} przez grubość", i + 1), plain(a), b});
    }
    return result;
}

std::vector<EdgeSpec> _profile_edge_specs(const std::string& key_) {
    static py::LruCache<std::vector<EdgeSpec>> cache(128);
    return cache.get(key_, [](const std::string& key) {
        json data = json::parse(key);
        return _solid_edge_specs(outline_solid(data), num(data["thickness"]));
    });
}

std::vector<EdgeSpec> edge_specs(const std::vector<Point>& points, double thickness, const json& profile) {
    if (py::truthy(profile) && profile.at("kind") != "rectangle" && profile.at("kind") != "taper") {
        double u = points[0][0], v = points[0][1];
        for (auto& p : points) { u = std::max(u, p[0]); v = std::max(v, p[1]); }
        return edge_specs(points, thickness, profile, {u, v});
    }
    std::vector<EdgeSpec> result;
    for (size_t i = 0; i < points.size(); ++i) {
        const Point& p = points[i]; const Point& q = points[(i + 1) % points.size()];
        result.push_back({std::format("bottom:{}", i), std::format("Dolna krawędź {}", i + 1), plain({p[0], p[1], 0}), {q[0], q[1], 0}});
        result.push_back({std::format("top:{}", i), std::format("Górna krawędź {}", i + 1), plain({p[0], p[1], thickness}), {q[0], q[1], thickness}});
        result.push_back({std::format("corner:{}", i), std::format("Narożnik {} przez grubość", i + 1), plain({p[0], p[1], 0}), {p[0], p[1], thickness}});
    }
    return result;
}

std::vector<EdgeSpec> edge_specs(const std::vector<Point>& points, double thickness, const json& profile, std::pair<double, double> size) {
    if (py::truthy(profile) && profile.at("kind") != "rectangle" && profile.at("kind") != "taper") {
        json key = {{"points", points_to_json(points)}, {"profile", profile}, {"size", json::array({size.first, size.second})}, {"thickness", thickness}};
        return _profile_edge_specs(py::dumps(key, true, -1, true));
    }
    return edge_specs(points, thickness, profile);
}

TopoDS_Edge match_edge(const TopoDS_Shape& shape, const EdgeAnchor& a, const Point3& b) {
    if (a.geom_type && *a.geom_type != "LINE") {
        kernel();
        for (auto& edge : edges_of(shape)) {
            if (geom_type(edge) != *a.geom_type || std::abs(edge_length(edge) - a.length) > std::max(1e-5, a.length * 1e-7)) continue;
            size_t step = (a.samples.size() - 1) / 4;
            if (step == 0) throw ValueError("slice step cannot be zero");
            bool all = true;
            for (size_t k = 0; k < a.samples.size(); k += step) {
                TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(pnt(a.samples[k])).Vertex();
                if (!(BRepExtrema_DistShapeShape(edge, vertex).Value() < 1e-5)) { all = false; break; }
            }
            if (all) return edge;
        }
        throw ValueError("Wybrany łuk zmienił się po wcześniejszej obróbce. Zmień zestaw faz lub zaokrągleń.");
    }
    Point3 direction{b[0] - a.point[0], b[1] - a.point[1], b[2] - a.point[2]};
    double length2 = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
    for (auto& edge : edges_of(shape)) {
        if (geom_type(edge) != "LINE") continue;
        auto vertices = entities(edge, TopAbs_VERTEX);
        if (vertices.size() != 2) continue;
        Point3 u = tuple(BRep_Tool::Pnt(TopoDS::Vertex(vertices[0]))), v = tuple(BRep_Tool::Pnt(TopoDS::Vertex(vertices[1])));
        auto on_line = [&](const Point3& p) {
            double parameter = ((p[0] - a.point[0]) * direction[0] + (p[1] - a.point[1]) * direction[1] + (p[2] - a.point[2]) * direction[2]) / length2;
            Point3 nearest{a.point[0] + parameter * direction[0], a.point[1] + parameter * direction[1], a.point[2] + parameter * direction[2]};
            return -.00001 <= parameter && parameter <= 1.00001 && dist(p, nearest) < 1e-5;
        };
        if (on_line(u) && on_line(v)) return edge;
    }
    throw ValueError("Kant nie istnieje po wcześniejszej operacji. Zmień zestaw faz lub zaokrągleń.");
}

std::pair<json, json> grouped_edge_features(const std::map<std::string, EdgeSpec>& specifications, const json& fillets, const json& chamfers) {
    // OCCT propagates a constant treatment through a tangent edge chain.
    std::map<std::vector<std::string>, std::pair<std::string, double>> used; json result_fillets = json::array(), result_chamfers = json::array();
    for (auto& [kind, items, parameter] : std::vector<std::tuple<std::string, const json*, std::string>>{{"fillets", &fillets, "radius"}, {"chamfers", &chamfers, "distance"}}) {
        for (auto& item : *items) {
            const json& edge = item.at("edge");
            auto spec = edge.is_string() ? specifications.find(edge.get<std::string>()) : specifications.end();
            if (spec == specifications.end()) throw ValueError("Obróbka wskazuje nieistniejącą krawędź.");
            std::vector<std::string> chain = spec->second.geometry.chain ? *spec->second.geometry.chain : std::vector<std::string>{edge.get<std::string>()};
            double value = py::to_float(item.at(parameter));
            if (used.contains(chain)) {
                auto [old_kind, old_value] = used[chain];
                if (kind != old_kind || std::abs(value - old_value) > 1e-8) {
                    throw ValueError("Krawędzie połączone stycznie tworzą wspólny ciąg. Ustaw dla całego ciągu ten sam promień lub tę samą fazę; różne wartości wymagają osobnego profilu przejścia.");
                }
                continue;
            }
            used[chain] = {kind, value}; (kind == "fillets" ? result_fillets : result_chamfers).push_back(item);
        }
    }
    return {result_fillets, result_chamfers};
}

TopoDS_Shape outline_solid(const json& data) {
    // Extrude exact parametric outline; polygon points remain a GUI proxy.
    kernel(); json profile = py::get(data, "profile", json::object()); double thickness = num(data.at("thickness"));
    if (!py::truthy(profile) || profile.at("kind") == "rectangle" || profile.at("kind") == "taper") {
        return workplane_extrude(closed_polyline(points_from_json(data.at("points"))), thickness);
    }
    json kind = profile.at("kind"); double u = num(data.at("size")[0]), v = num(data.at("size")[1]); std::vector<TopoDS_Edge> edges;
    auto line = [&](const Point& a, const Point& b) {
        if (std::hypot(a[0] - b[0], a[1] - b[1]) > 1e-8) edges.push_back(make_line({a[0], a[1], 0}, {b[0], b[1], 0}));
    };
    TopoDS_Wire wire;
    if (kind == "ellipse" || kind == "circle") {
        TopoDS_Edge edge = kind == "ellipse" ? make_ellipse(u / 2, v / 2, {u / 2, v / 2, 0}) : make_circle(u / 2, {u / 2, v / 2, 0});
        wire = assemble_edges({edge});
    } else if (kind == "arch" || kind == "half_ellipse") {
        double rise = kind == "half_ellipse" ? v : num(profile.at("arch_rise")); double spring = v - rise;
        line({0, 0}, {u, 0}); line({u, 0}, {u, spring});
        edges.push_back(make_ellipse(u / 2, rise, {u / 2, spring, 0}, 0, 180));
        line({0, spring}, {0, 0}); wire = assemble_edges(edges);
    } else if (kind == "rounded" || kind == "capsule") {
        std::vector<double> radii;
        if (kind == "rounded") for (auto& r : profile.at("radii")) radii.push_back(num(r));
        else radii.assign(4, std::min(u, v) / 2);
        struct Segment { Point a, b; std::optional<TopoDS_Edge> arc; };
        std::vector<Segment> segments;
        for (auto [radius, cx, cy, start, corner] : std::vector<std::tuple<double, double, double, double, Point>>{
                 {radii.at(0), radii.at(0), radii.at(0), std::numbers::pi, {0, 0}},
                 {radii.at(1), u - radii.at(1), radii.at(1), std::numbers::pi * 1.5, {u, 0}},
                 {radii.at(2), u - radii.at(2), v - radii.at(2), 0, {u, v}},
                 {radii.at(3), radii.at(3), v - radii.at(3), std::numbers::pi / 2, {0, v}}}) {
            if (radius == 0) { segments.push_back({corner, corner, std::nullopt}); continue; }
            auto point = [&](double angle) { return Point{cx + radius * std::cos(angle), cy + radius * std::sin(angle)}; };
            Point a = point(start), m = point(start + std::numbers::pi / 4), b = point(start + std::numbers::pi / 2);
            segments.push_back({a, b, make_three_point_arc({a[0], a[1], 0}, {m[0], m[1], 0}, {b[0], b[1], 0})});
        }
        for (size_t i = 0; i < segments.size(); ++i) {
            if (segments[i].arc) edges.push_back(*segments[i].arc);
            line(segments[i].b, segments[(i + 1) % segments.size()].a);
        }
        wire = assemble_edges(edges);
    } else throw ValueError("Nieznany dokładny obrys CAD.");
    TopoDS_Shape solid = extrude_linear(face_from_wire(wire), thickness);
    if (!is_valid(solid)) throw ValueError("Parametry obrysu nie tworzą poprawnej bryły CAD.");
    return solid;
}

namespace {
CacheStats g_stats;

// Wiercenia nie wpływają na bryłę bazową (są wycinane osobno w cut_drills), więc nie wchodzą do klucza pamięci podręcznej:
// dodanie okucia nie przelicza zaokrągleń, faz i frezowania — to najdroższa część budowy bryły.
std::string shape_key_without_drilling(const std::string& key) {
    json data = json::parse(key);
    if (!data.is_object() || !data.contains("drilling")) return key;
    data.erase("drilling");
    return py::dumps(data, true, -1, true);
}

TopoDS_Shape build_shape(const std::string& key) {
    {
        kernel(); json data = json::parse(key); std::vector<Point> points = points_from_json(data["points"]); double thickness = num(data["thickness"]);
        TopoDS_Shape shape = outline_solid(data);
        TopoDS_Shape nominal = shape;
        std::map<std::string, EdgeSpec> specifications;
        json size = py::get(data, "size");
        for (auto& spec : size.is_null() ? edge_specs(points, thickness, py::get(data, "profile"))
                                         : edge_specs(points, thickness, py::get(data, "profile"), {num(size[0]), num(size[1])}))
            specifications[spec.edge] = spec;
        auto [fillets, chamfers] = grouped_edge_features(specifications, data.at("fillets"), py::get(data, "chamfers", json::array()));
        if (!chamfers.empty()) {
            BRepFilletAPI_MakeChamfer builder(shape);
            try {
                for (auto& item : chamfers) {
                    const EdgeSpec& spec = specifications.at(item.at("edge").get<std::string>()); TopoDS_Edge edge = match_edge(shape, spec.geometry, spec.end);
                    builder.Add(num(item.at("distance")), edge);
                }
                builder.Build();
                if (!builder.IsDone()) throw ValueError("Nieprawidłowa faza.");
                shape = builder.Shape();
            } catch (...) { throw ValueError("Nie można wykonać faz. Zmniejsz odległość lub zmień krawędzie."); }
        }
        if (!fillets.empty()) {
            BRepFilletAPI_MakeFillet builder(shape);
            for (auto& item : fillets) {
                auto spec = specifications.find(item.at("edge").get<std::string>());
                if (spec == specifications.end()) throw ValueError("Zaokrąglenie wskazuje nieistniejącą krawędź.");
                TopoDS_Edge selected = match_edge(shape, spec->second.geometry, spec->second.end);
                builder.Add(num(item.at("radius")), selected);
            }
            try {
                builder.Build();
                if (!builder.IsDone()) throw ValueError("Promienie nie pasują do tej geometrii.");
                shape = builder.Shape();
                if (!is_valid(shape) || entities(shape, TopAbs_SOLID).empty()) throw ValueError("Zaokrąglenie tworzy nieprawidłową bryłę.");
            } catch (...) {
                throw ValueError("Nie można wykonać tych zaokrągleń. Zmniejsz promień lub zmień zestaw krawędzi.");
            }
        }
        for (auto& c : py::get(data, "machining", json::array())) {
            double depth = c.at("type") == "cutout" ? thickness : num(c.at("depth")); double u = num(c.at("u")), v = num(c.at("v"));
            double z = c.at("face") == "top" ? thickness - depth : 0;
            TopoDS_Shape tool;
            if (c.at("shape") == "circle") tool = make_cylinder(num(c.at("width")) / 2, depth, {u, v, z});
            else if (c.at("shape") == "slot") tool = translate(workplane_extrude(slot2d(num(c.at("length")), num(c.at("width"))), depth), {u, v, z});
            else tool = make_box(num(c.at("length")), num(c.at("width")), depth, {u - num(c.at("length")) / 2, v - num(c.at("width")) / 2, z});
            if (py::truthy(py::get(c, "angle", 0))) tool = rotate(tool, {u, v, 0}, {u, v, 1}, num(c.at("angle")));
            double tool_volume = volume(tool);
            if (std::abs(volume(intersect(nominal, tool)) - tool_volume) > std::max(.001, tool_volume * 1e-6)) throw ValueError("Obróbka wykracza poza kontur części.");
            if (volume(intersect(shape, tool)) < .001) throw ValueError("Obróbka nie przecina materiału.");
            shape = cut(shape, tool);
        }
        if (!is_valid(shape) || volume(shape) <= 0 || entities(shape, TopAbs_SOLID).size() != 1)
            throw ValueError("Obróbka usuwa cały materiał lub dzieli część na oddzielne bryły.");
        return shape;
    }
}
}  // namespace

CacheStats cache_stats() { return g_stats; }
void reset_cache_stats() { g_stats = CacheStats{}; }

TopoDS_Shape cached_shape(const std::string& key_) {
    static py::LruCache<TopoDS_Shape> cache(128);
    bool computed = false;
    TopoDS_Shape result = cache.get(shape_key_without_drilling(key_), [&computed](const std::string& key) {
        computed = true;
        return build_shape(key);
    });
    ++(computed ? g_stats.shape_misses : g_stats.shape_hits);
    return result;
}

std::string shape_key(const Part& part) {
    auto sorted_by_edge = [](const json& items) {
        std::vector<json> list(items.begin(), items.end());
        std::stable_sort(list.begin(), list.end(), [](const json& a, const json& b) { return a.at("edge") < b.at("edge"); });
        return json(list);
    };
    json drilling = json::array();
    for (auto& hole : part.holes) {
        if (hole.size() != 4) throw ValueError(hole.size() < 4 ? std::format("not enough values to unpack (expected 4, got {})", hole.size()) : "too many values to unpack (expected 4)");
        drilling.push_back({{"u", hole[0]}, {"v", hole[1]}, {"diameter", hole[2]}, {"depth", hole[3]}, {"face", "top"}});
    }
    for (auto& op : part.operations) drilling.push_back(op);
    json data = {{"points", points_to_json(part.points())}, {"size", json::array({part.u, part.v})}, {"profile", part.profile}, {"thickness", part.t},
                 {"fillets", sorted_by_edge(part.fillets)}, {"chamfers", sorted_by_edge(part.chamfers)}, {"machining", part.machining},
                 {"drilling", drilling}};
    return py::dumps(data, true, -1, true);
}

TopoDS_Shape local_shape(const Part& part) { return cached_shape(shape_key(part)); }

std::vector<Triangle> cached_mesh(const std::string& key_) {
    static py::LruCache<std::vector<Triangle>> cache(128);
    bool computed = false;
    auto result = cache.get(key_, [&computed](const std::string& key) {
        computed = true;
        TopoDS_Shape shape = cached_shape(key);
        json data = json::parse(key); shape = cut_drills(shape, data["drilling"], num(data["thickness"]), points_from_json(data["points"]));
        // Shape.tessellate(0.2, 0.15)
        double tolerance = .2, angular_tolerance = .15;
        if (!BRepTools::Triangulation(shape, tolerance)) BRepMesh_IncrementalMesh(shape, tolerance, true, angular_tolerance);
        std::vector<Point3> vertices; std::vector<std::array<int, 3>> triangles; int offset = 0;
        for (auto& f : entities(shape, TopAbs_FACE)) {
            TopoDS_Face face = TopoDS::Face(f);
            TopLoc_Location loc;
            Handle(Poly_Triangulation) poly = BRep_Tool::Triangulation(face, loc);
            if (poly.IsNull()) throw std::runtime_error("'NoneType' object has no attribute 'NbNodes'");
            gp_Trsf trsf = loc.Transformation();
            bool reverse = face.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= poly->NbNodes(); ++i) vertices.push_back(tuple(poly->Node(i).Transformed(trsf)));
            for (int i = 1; i <= poly->NbTriangles(); ++i) {
                int n1, n2, n3; poly->Triangle(i).Get(n1, n2, n3);
                triangles.push_back(reverse ? std::array<int, 3>{n1 + offset - 1, n3 + offset - 1, n2 + offset - 1} : std::array<int, 3>{n1 + offset - 1, n2 + offset - 1, n3 + offset - 1});
            }
            offset += poly->NbNodes();
        }
        std::vector<Triangle> out;
        for (auto& t : triangles) out.push_back({vertices[t[0]], vertices[t[1]], vertices[t[2]]});
        return out;
    });
    ++(computed ? g_stats.mesh_misses : g_stats.mesh_hits);
    return result;
}

std::vector<Triangle> mesh(const Part& part) { return cached_mesh(shape_key(part)); }

TopoDS_Shape cut_drills(TopoDS_Shape shape, const json& drilling, double thickness, const std::vector<Point>& points, bool check_only, bool check_boundary) {
    kernel();
    Part proxy; proxy.t = thickness; proxy.u = points[0][0]; proxy.v = points[0][1];
    for (auto& p : points) { proxy.u = std::max(proxy.u, p[0]); proxy.v = std::max(proxy.v, p[1]); }
    for (auto& op : drilling) {
        auto [position, direction] = features::drill_location(proxy, op);
        TopoDS_Shape cylinder = make_cylinder(num(op.at("diameter")) / 2, num(op.at("depth")), position, direction);
        if (check_boundary) {
            double cylinder_volume = volume(cylinder); double overlap = volume(intersect(shape, cylinder));
            if (std::abs(cylinder_volume - overlap) > std::max(.001, cylinder_volume * 1e-6)) {
                throw ValueError("To wiercenie koliduje z profilem krawędzi lub wykracza poza kontur.");
            }
        }
        if (!check_only) {
            shape = cut(shape, cylinder);
            if (py::truthy(py::get(op, "countersink_diameter"))) {
                TopoDS_Shape cone = make_cone(num(op["countersink_diameter"]) / 2, num(op.at("diameter")) / 2, num(op.at("countersink_depth")), position, direction);
                shape = cut(shape, cone);
            }
        }
    }
    return shape;
}

TopoDS_Shape drilled_shape(const Part& part, bool check_only) {
    json data = json::parse(shape_key(part));
    bool side_drilling = false;
    for (auto& op : part.operations) if (op.at("face") != "top" && op.at("face") != "bottom") side_drilling = true;
    return cut_drills(local_shape(part), data["drilling"], part.t, part.points(), check_only,
        (py::truthy(part.fillets) || py::truthy(part.chamfers) || side_drilling) && !py::truthy(part.machining));
}

void validate_fillets(const Part& part) {
    bool side_drilling = false;
    for (auto& o : part.operations) if (o.at("face") != "top" && o.at("face") != "bottom") side_drilling = true;
    if (!(py::truthy(part.fillets) || py::truthy(part.chamfers) || py::truthy(part.machining) || side_drilling)) return;
    TopoDS_Shape shape = local_shape(part);
    if (volume(shape) <= 0) throw ValueError("Bryła po zaokrągleniu nie ma objętości.");
    drilled_shape(part, true);
}

void step_export(core::Project& project, const std::string& path, const std::string& export_type) {
    kernel(); project.validate(); std::vector<TopoDS_Shape> solids;
    for (auto& part : project.parts) {
        TopoDS_Shape shape = drilled_shape(part);
        Point3 origin = core::world(part, 0, 0, 0);
        std::array<Point3, 3> basis;
        Point3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int j = 0; j < 3; ++j) {
            Point3 p = core::world(part, axes[j][0], axes[j][1], axes[j][2]);
            basis[j] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
        }
        gp_Trsf transform;
        transform.SetValues(basis[0][0], basis[1][0], basis[2][0], origin[0],
                            basis[0][1], basis[1][1], basis[2][1], origin[1],
                            basis[0][2], basis[1][2], basis[2][2], origin[2]);
        solids.push_back(BRepBuilderAPI_Transform(shape, transform).Shape());
    }
    if (solids.empty()) throw ValueError("Projekt nie zawiera elementów do eksportu.");
    BRep_Builder builder; TopoDS_Compound compound; builder.MakeCompound(compound);
    for (auto& s : solids) builder.Add(compound, s);
    if (export_type == "STEP") {
        // Shape.exportStep(path)
        STEPControl_Writer writer;
        Interface_Static::SetIVal("write.surfacecurve.mode", 1);
        Interface_Static::SetIVal("write.precision.mode", 0);
        writer.Transfer(compound, STEPControl_AsIs);
        writer.Write(path.c_str());
    } else if (export_type == "STL") {
        // Shape.exportStl(path, tolerance=0.1, angularTolerance=0.1)
        BRepMesh_IncrementalMesh mesher(compound, 0.1, true, 0.1, true);
        mesher.Perform();
        StlAPI_Writer writer;
        writer.ASCIIMode() = false;
        writer.Write(compound, path.c_str());
    }
}

void stl_export(core::Project& project, const std::string& path) { return step_export(project, path, "STL"); }

}  // namespace cad_geometry
