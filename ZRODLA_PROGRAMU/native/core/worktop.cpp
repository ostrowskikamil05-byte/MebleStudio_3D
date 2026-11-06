#include "worktop.hpp"
#include "core_api.hpp"
#include "layout_geometry.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mebel::model {
namespace {
using Point = layout_geometry::Point;
using Polygon = layout_geometry::Polygon;
using Edge = std::array<Point, 2>;
using worktop::decimal_round;

double numeric(const Json &input) {
    if (!input.is_number() && !input.is_boolean())
        throw std::invalid_argument("Współrzędna blatu musi być liczbą.");
    const double value = input.is_boolean() ? double(input.get<bool>()) : input.get<double>();
    if (!std::isfinite(value))
        throw std::invalid_argument("Współrzędna blatu musi być skończona.");
    return value;
}
bool truth(const Json &input) {
    if (input.is_null())
        return false;
    if (input.is_boolean())
        return input.get<bool>();
    if (input.is_number())
        return input.get<double>() != 0;
    return !input.empty();
}
bool integral(const Json &input) {
    return input.is_number_integer() || input.is_boolean();
}
double cross(const Point &a, const Point &b) {
    return a[0] * b[1] - a[1] * b[0];
}
Point difference(const Point &a, const Point &b) {
    return {a[0] - b[0], a[1] - b[1]};
}
bool orientation(const Json &input) {
    if (!input.is_boolean())
        throw std::invalid_argument("Orientacja obrysu wymaga wartości logicznej.");
    return input.get<bool>();
}

struct Outline {
    Polygon points;
    std::vector<std::array<bool, 2>> integer_coordinates;
    bool ccw = false;
};
Outline outline(const Json &input, const std::optional<bool> &winding = std::nullopt) {
    if (!input.is_array())
        throw std::invalid_argument("Obrys wymaga listy punktów.");
    Outline result;
    result.points.reserve(input.size());
    result.integer_coordinates.reserve(input.size());
    for (const auto &point : input) {
        if (!point.is_array() || point.size() != 2)
            throw std::invalid_argument("Punkt obrysu wymaga dwóch współrzędnych.");
        result.points.push_back({numeric(point[0]), numeric(point[1])});
        result.integer_coordinates.push_back({integral(point[0]), integral(point[1])});
    }
    result.ccw = winding.value_or(layout_geometry::signed_area(result.points) > 0);
    return result;
}
void orient(Outline &input) {
    if (!input.ccw) {
        std::reverse(input.points.begin(), input.points.end());
        std::reverse(input.integer_coordinates.begin(), input.integer_coordinates.end());
    }
    input.ccw = true;
}
double minimum_distance(const Outline &first, const Outline &second) {
    std::optional<double> result;
    for (int pass = 0; pass < 2; ++pass) {
        const auto &poly = pass ? second.points : first.points;
        const auto &other = pass ? first.points : second.points;
        for (const auto &point : poly)
            for (size_t i = 0; i < other.size(); ++i) {
                const double value =
                    layout_geometry::point_segment(point, other[i], other[(i + 1) % other.size()]);
                if (!result || value < *result)
                    result = value;
            }
    }
    if (!result)
        throw std::invalid_argument("min() iterable argument is empty");
    return *result;
}
double distance(const Outline &first, const Outline &second) {
    return layout_geometry::polygons_overlap(first.points, second.points, first.ccw, second.ccw)
               ? 0
               : minimum_distance(first, second);
}
Outline offset(Outline polygon, double amount) {
    if (!amount)
        return polygon;
    orient(polygon);
    struct Line {
        Point point, vector;
    };
    std::vector<Line> lines;
    lines.reserve(polygon.points.size());
    for (size_t i = 0; i < polygon.points.size(); ++i) {
        const auto &a = polygon.points[i];
        const auto &b = polygon.points[(i + 1) % polygon.points.size()];
        const auto dx = b[0] - a[0], dy = b[1] - a[1], length = std::hypot(dx, dy);
        if (!length)
            throw NativeException("ZeroDivisionError", "float division by zero");
        lines.push_back({{a[0] + dy * amount / length, a[1] - dx * amount / length}, {dx, dy}});
    }
    Outline result;
    result.points.reserve(lines.size());
    result.integer_coordinates.resize(lines.size(), {false, false});
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto &before = lines[(i + lines.size() - 1) % lines.size()];
        const auto &current = lines[i];
        const auto den = cross(before.vector, current.vector);
        if (std::abs(den) < 1e-9) {
            result.points.push_back(current.point);
            continue;
        }
        const auto delta = difference(current.point, before.point);
        const auto t = cross(delta, current.vector) / den;
        result.points.push_back(
            {before.point[0] + t * before.vector[0], before.point[1] + t * before.vector[1]});
    }
    result.ccw = layout_geometry::signed_area(result.points) > 0;
    return result;
}
Polygon unite(std::vector<Outline> polygons) {
    for (auto &polygon : polygons)
        orient(polygon);
    std::set<Edge> edges;
    for (size_t index = 0; index < polygons.size(); ++index) {
        const auto &poly = polygons[index].points;
        for (size_t edge_index = 0; edge_index < poly.size(); ++edge_index) {
            const auto &a = poly[edge_index];
            const auto &b = poly[(edge_index + 1) % poly.size()];
            const auto v = difference(b, a);
            const double length = v[0] * v[0] + v[1] * v[1];
            std::vector<double> cuts{0., 1.};
            for (size_t other_index = 0; other_index < polygons.size(); ++other_index) {
                if (other_index == index)
                    continue;
                const auto &other = polygons[other_index].points;
                for (size_t j = 0; j < other.size(); ++j) {
                    const auto &p = other[j];
                    const auto &q = other[(j + 1) % other.size()];
                    const auto w = difference(q, p), pa = difference(p, a);
                    const double den = cross(v, w);
                    if (std::abs(den) > 1e-9) {
                        const double t = cross(pa, w) / den, u = cross(pa, v) / den;
                        if (-1e-7 <= t && t <= 1 + 1e-7 && -1e-7 <= u && u <= 1 + 1e-7)
                            cuts.push_back(std::max(0., std::min(1., t)));
                    } else if (std::abs(cross(pa, v)) < 1e-5) {
                        if (!length) {
                            const auto &kinds = polygons[index].integer_coordinates;
                            const auto &first = kinds[edge_index];
                            const auto &last = kinds[(edge_index + 1) % kinds.size()];
                            throw NativeException("ZeroDivisionError",
                                                  first[0] && first[1] && last[0] && last[1]
                                                      ? "division by zero"
                                                      : "float division by zero");
                        }
                        for (const auto &point : {p, q}) {
                            const auto t =
                                ((point[0] - a[0]) * v[0] + (point[1] - a[1]) * v[1]) / length;
                            if (0 < t && t < 1)
                                cuts.push_back(t);
                        }
                    }
                }
            }
            std::set<double> unique_cuts;
            for (const auto t : cuts)
                unique_cuts.insert(decimal_round(t, 10));
            if (unique_cuts.empty())
                continue;
            auto start = unique_cuts.begin();
            auto end = std::next(start);
            for (; end != unique_cuts.end(); ++start, ++end) {
                if (*end - *start < 1e-8)
                    continue;
                const Point midpoint{a[0] + (*start + *end) * v[0] / 2,
                                     a[1] + (*start + *end) * v[1] / 2};
                bool hidden = false;
                for (size_t other = 0; other < polygons.size(); ++other)
                    if (other != index &&
                        layout_geometry::inside(midpoint, polygons[other].points, true)) {
                        hidden = true;
                        break;
                    }
                if (hidden)
                    continue;
                const Point p{decimal_round(a[0] + *start * v[0], 6),
                              decimal_round(a[1] + *start * v[1], 6)},
                    q{decimal_round(a[0] + *end * v[0], 6), decimal_round(a[1] + *end * v[1], 6)};
                if (p != q)
                    edges.insert({p, q});
            }
        }
    }
    std::set<Edge> exposed;
    for (const auto &edge : edges)
        if (!edges.contains(Edge{edge[1], edge[0]}))
            exposed.insert(edge);
    edges.swap(exposed);
    if (edges.empty())
        throw std::invalid_argument("Nie można wyznaczyć obrysu wspólnego blatu.");
    std::vector<Polygon> loops;
    while (!edges.empty()) {
        const auto first = *edges.begin();
        edges.erase(edges.begin());
        Polygon loop{first[0]};
        auto current = first[1];
        while (current != loop[0]) {
            loop.push_back(current);
            auto found = edges.end();
            size_t count = 0;
            for (auto item = edges.begin(); item != edges.end(); ++item)
                if ((*item)[0] == current) {
                    found = item;
                    ++count;
                }
            if (count != 1)
                throw std::invalid_argument(
                    "Korpusy łączą się tylko punktem lub tworzą niejednoznaczny obrys blatu.");
            current = (*found)[1];
            edges.erase(found);
        }
        bool changed = true;
        while (changed && loop.size() > 3) {
            changed = false;
            for (size_t i = 0; i < loop.size(); ++i) {
                const auto &p = loop[i];
                const auto &a = loop[(i + loop.size() - 1) % loop.size()];
                const auto &b = loop[(i + 1) % loop.size()];
                if (std::abs(cross(difference(p, a), difference(b, p))) < 1e-5) {
                    loop.erase(loop.begin() + i);
                    changed = true;
                    break;
                }
            }
        }
        loops.push_back(std::move(loop));
    }
    if (loops.size() != 1)
        throw std::invalid_argument("Wspólny blat wymaga jednego połączonego obrysu bez "
                                    "zamkniętego otworu. Rozdziel go na kilka blatów.");
    auto result = std::move(loops[0]);
    if (!(layout_geometry::signed_area(result) > 0))
        std::reverse(result.begin(), result.end());
    return result;
}

// A tiny portable signed integer accumulator preserves Python's integer sum
// before its first float. No nonstandard __int128 or additional library is used.
class IntegerSum {
    int sign_ = 0;
    std::vector<uint32_t> digits_;
    static void trim(std::vector<uint32_t> &digits) {
        while (!digits.empty() && !digits.back())
            digits.pop_back();
    }
    static int compare(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
        if (a.size() != b.size())
            return a.size() < b.size() ? -1 : 1;
        for (size_t i = a.size(); i > 0; --i)
            if (a[i - 1] != b[i - 1])
                return a[i - 1] < b[i - 1] ? -1 : 1;
        return 0;
    }
    static void subtract(std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
        uint64_t borrow = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            const uint64_t right = (i < b.size() ? b[i] : 0) + borrow;
            const auto left = uint64_t(a[i]);
            a[i] = uint32_t(left - right);
            borrow = left < right;
        }
        trim(a);
    }
    static bool bit(const std::vector<uint32_t> &a, size_t index) {
        return index / 32 < a.size() && ((a[index / 32] >> (index % 32)) & 1);
    }
    static size_t bits(const std::vector<uint32_t> &a) {
        return a.empty() ? 0 : (a.size() - 1) * 32 + std::bit_width(a.back());
    }

  public:
    void add(const Json &input) {
        uint64_t value = 0;
        int sign = 1;
        if (input.is_boolean())
            value = input.get<bool>();
        else if (input.is_number_unsigned())
            value = input.get<uint64_t>();
        else {
            const auto signed_value = input.get<int64_t>();
            sign = signed_value < 0 ? -1 : 1;
            value = signed_value < 0 ? uint64_t(-(signed_value + 1)) + 1 : uint64_t(signed_value);
        }
        if (!value)
            return;
        std::vector<uint32_t> other{uint32_t(value)};
        if (value >> 32)
            other.push_back(uint32_t(value >> 32));
        if (!sign_) {
            sign_ = sign;
            digits_ = std::move(other);
            return;
        }
        if (sign_ == sign) {
            const auto size = std::max(digits_.size(), other.size());
            digits_.resize(size, 0);
            uint64_t carry = 0;
            for (size_t i = 0; i < size; ++i) {
                const auto total = uint64_t(digits_[i]) + (i < other.size() ? other[i] : 0) + carry;
                digits_[i] = uint32_t(total);
                carry = total >> 32;
            }
            if (carry)
                digits_.push_back(uint32_t(carry));
            return;
        }
        const auto order = compare(digits_, other);
        if (!order) {
            digits_.clear();
            sign_ = 0;
        } else if (order > 0)
            subtract(digits_, other);
        else {
            subtract(other, digits_);
            digits_ = std::move(other);
            sign_ = sign;
        }
    }
    double divided(size_t denominator = 1) const {
        if (!sign_)
            return 0.;
        auto quotient = digits_;
        uint64_t remainder = 0;
        for (size_t i = quotient.size(); i > 0; --i) {
            const uint64_t value = (remainder << 32) | quotient[i - 1];
            quotient[i - 1] = uint32_t(value / denominator);
            remainder = value % denominator;
        }
        trim(quotient);
        const auto length = bits(quotient);
        uint64_t top = 0;
        int shift = 0;
        bool guard = false, sticky = false;
        if (length >= 53) {
            for (size_t i = length; i > length - 53; --i)
                top = (top << 1) | bit(quotient, i - 1);
            shift = int(length - 53);
            if (shift) {
                guard = bit(quotient, size_t(shift - 1));
                sticky = remainder != 0;
                for (int i = 0; i < shift - 1 && !sticky; ++i)
                    sticky = bit(quotient, size_t(i));
            } else {
                remainder *= 2;
                guard = remainder >= denominator;
                if (guard)
                    remainder -= denominator;
                sticky = remainder != 0;
            }
        } else {
            for (size_t i = length; i > 0; --i)
                top = (top << 1) | bit(quotient, i - 1);
            if (!length) {
                while (!top) {
                    remainder *= 2;
                    --shift;
                    if (remainder >= denominator) {
                        top = 1;
                        remainder -= denominator;
                    }
                }
                for (int i = 0; i < 52; ++i) {
                    remainder *= 2;
                    top = (top << 1) | (remainder >= denominator);
                    if (remainder >= denominator)
                        remainder -= denominator;
                    --shift;
                }
            } else
                for (size_t i = 0; i < 53 - length; ++i) {
                    remainder *= 2;
                    top = (top << 1) | (remainder >= denominator);
                    if (remainder >= denominator)
                        remainder -= denominator;
                    --shift;
                }
            remainder *= 2;
            guard = remainder >= denominator;
            if (guard)
                remainder -= denominator;
            sticky = remainder != 0;
        }
        if (guard && (sticky || (top & 1)))
            ++top;
        return std::copysign(std::ldexp(double(top), shift), double(sign_));
    }
};
class HeightSum {
    IntegerSum integers_;
    bool integer_mode_ = true;
    double hi_ = 0, lo_ = 0;

  public:
    void add(const Json &input) {
        if (integer_mode_ && integral(input)) {
            integers_.add(input);
            return;
        }
        const auto value = numeric(input);
        if (integer_mode_) {
            hi_ = integers_.divided() + value;
            integer_mode_ = false;
            return;
        }
        const auto next = hi_ + value;
        lo_ += std::abs(hi_) >= std::abs(value) ? (hi_ - next) + value : (value - next) + hi_;
        hi_ = next;
    }
    double mean(size_t count) const {
        return integer_mode_ ? integers_.divided(count)
                             : (std::isfinite(lo_) ? hi_ + lo_ : hi_) / double(count);
    }
};
Json geometry(const Json &bodies, const Json &raw_overhang) {
    if (!bodies.is_array())
        throw std::invalid_argument("Korpusy wspólnego blatu wymagają listy.");
    if (bodies.empty())
        throw std::invalid_argument("max() iterable argument is empty");
    double low = INFINITY, high = -INFINITY;
    HeightSum height_sum;
    for (const auto &body : bodies) {
        if (!body.is_object())
            throw std::invalid_argument("Korpus blatu wymaga obiektu.");
        const auto &z = body.at("z");
        const auto height = numeric(z);
        low = std::min(low, height);
        high = std::max(high, height);
        height_sum.add(z);
    }
    if (high - low > .1)
        throw std::invalid_argument(
            "Szafki pod wspólnym blatem muszą mieć tę samą wysokość górnej powierzchni.");
    std::vector<Outline> polygons;
    polygons.reserve(bodies.size());
    for (const auto &body : bodies)
        polygons.push_back(
            outline(body.at("polygon"), body.contains("ccw")
                                            ? std::optional<bool>(orientation(body.at("ccw")))
                                            : std::nullopt));
    for (size_t i = 0; i < polygons.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (layout_geometry::polygons_overlap(polygons[i].points, polygons[j].points,
                                                  polygons[i].ccw, polygons[j].ccw))
                throw std::invalid_argument("Korpusy pod wspólnym blatem nachodzą na siebie.");
    std::vector<bool> visited(polygons.size(), false);
    visited[0] = true;
    while (true) {
        auto grown = visited;
        for (size_t i = 0; i < polygons.size(); ++i)
            for (size_t j = 0; j < polygons.size(); ++j)
                if (visited[j] && distance(polygons[i], polygons[j]) <= .5) {
                    grown[i] = true;
                    break;
                }
        if (grown == visited)
            break;
        visited = std::move(grown);
    }
    if (std::count(visited.begin(), visited.end(), true) != static_cast<ptrdiff_t>(polygons.size()))
        throw std::invalid_argument(
            "Dosuń szafki do siebie magnesem. Wspólny blat wymaga sąsiadujących korpusów.");
    const auto overhang = numeric(raw_overhang);
    for (auto &polygon : polygons)
        polygon = offset(std::move(polygon), overhang);
    const auto polygon = unite(std::move(polygons));
    double x = INFINITY, y = INFINITY;
    for (const auto &point : polygon) {
        x = std::min(x, point[0]);
        y = std::min(y, point[1]);
    }
    Polygon local;
    local.reserve(polygon.size());
    for (const auto &point : polygon)
        local.push_back({decimal_round(point[0] - x, 6), decimal_round(point[1] - y, 6)});
    auto validated = dispatch_core(
        Json{{"module", "core"}, {"function", "validate_polygon"}, {"args", Json::array({local})}});
    if (!validated)
        throw std::logic_error("Brak walidacji konturu blatu.");
    if (validated->size() > 128)
        throw std::invalid_argument("Obrys wspólnego blatu ma zbyt wiele punktów. Zmniejsz liczbę "
                                    "modułów lub podziel blat.");
    Json anchors = Json::array();
    for (const auto &body : bodies)
        anchors.push_back(body.at("anchor"));
    return Json{{"contour", std::move(*validated)},
                {"origin", Json::array({x, y, height_sum.mean(bodies.size())})},
                {"anchors", std::move(anchors)}};
}
struct Arguments {
    const Json args, kwargs;
    explicit Arguments(const Json &request)
        : args(request.value("args", Json::array())),
          kwargs(request.value("kwargs", Json::object())) {
    }
    void validate(std::initializer_list<const char *> names) const {
        if (args.size() > names.size())
            throw std::invalid_argument("Za dużo argumentów wspólnego blatu.");
        for (auto item = kwargs.begin(); item != kwargs.end(); ++item)
            if (std::none_of(names.begin(), names.end(),
                             [&](const char *name) { return item.key() == name; }))
                throw std::invalid_argument("Nieznany argument wspólnego blatu: " + item.key());
        size_t i = 0;
        for (const auto *name : names)
            if (i++ < args.size() && kwargs.contains(name))
                throw std::invalid_argument("Argument wspólnego blatu podano dwukrotnie.");
    }
    bool has(size_t index, const char *name) const {
        return index < args.size() || kwargs.contains(name);
    }
    const Json &at(size_t index, const char *name) const {
        if (index < args.size())
            return args[index];
        if (kwargs.contains(name))
            return kwargs.at(name);
        throw std::invalid_argument(std::string("Brakuje argumentu wspólnego blatu: ") + name);
    }
    std::optional<bool> winding(size_t index, const char *name) const {
        return has(index, name) ? std::optional<bool>(orientation(at(index, name))) : std::nullopt;
    }
};
} // namespace
std::optional<Json> dispatch_workspace_worktop(const Json &request) {
    if (request.value("module", std::string()) != "workspace_worktop")
        return std::nullopt;
    const auto function = request.value("function", std::string());
    const Arguments args(request);
    if (function == "distance") {
        args.validate({"a", "b", "a_ccw", "b_ccw"});
        const auto first = outline(args.at(0, "a"), args.winding(2, "a_ccw")),
                   second = outline(args.at(1, "b"), args.winding(3, "b_ccw"));
        if (layout_geometry::polygons_overlap(first.points, second.points, first.ccw, second.ccw))
            return Json(0);
        return Json(minimum_distance(first, second));
    }
    if (function == "offset") {
        args.validate({"poly", "amount", "ccw"});
        const auto &amount = args.at(1, "amount");
        if (!truth(amount))
            return args.at(0, "poly");
        return offset(outline(args.at(0, "poly"), args.winding(2, "ccw")), numeric(amount)).points;
    }
    if (function == "union") {
        args.validate({"polygons", "ccw"});
        const auto &input = args.at(0, "polygons");
        if (!input.is_array())
            throw std::invalid_argument("Obrysy blatu wymagają listy.");
        const auto *flags = args.has(1, "ccw") ? &args.at(1, "ccw") : nullptr;
        if (flags && (!flags->is_array() || flags->size() != input.size()))
            throw std::invalid_argument("Orientacje wymagają jednej wartości logicznej na obrys.");
        std::vector<Outline> polygons;
        polygons.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i)
            polygons.push_back(outline(
                input[i], flags ? std::optional<bool>(orientation((*flags)[i])) : std::nullopt));
        return unite(std::move(polygons));
    }
    if (function == "geometry") {
        args.validate({"bodies", "overhang"});
        return geometry(args.at(0, "bodies"), args.at(1, "overhang"));
    }
    return std::nullopt;
}
} // namespace mebel::model
