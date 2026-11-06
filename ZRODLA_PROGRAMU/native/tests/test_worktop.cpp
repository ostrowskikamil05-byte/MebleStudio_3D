#include "../core/core_api.hpp"
#include "../core/worktop.hpp"
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <windows.h>

using mebel::model::Json;
namespace {
using Call = char *(*)(const char *);
using Free = void (*)(void *);
Call call = nullptr;
Free release = nullptr;
Json checks = Json::array();
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
Json invoke(const char *function, Json args = Json::array(), Json kwargs = Json::object(),
            const char *module = "workspace_worktop") {
    const auto request =
        Json{{"module", module}, {"function", function}, {"args", args}, {"kwargs", kwargs}}.dump();
    char *raw = call(request.c_str());
    require(raw != nullptr, "No native response");
    std::string text;
    try {
        text = raw;
    } catch (...) {
        release(raw);
        throw;
    }
    release(raw);
    return Json::parse(text);
}
Json run(const char *function, Json args = Json::array(), Json kwargs = Json::object(),
         const char *module = "workspace_worktop") {
    const auto response = invoke(function, args, kwargs, module);
    require(response.at("ok").get<bool>(),
            std::string(function) + ": " + response.value("error", std::string()));
    return response.at("result");
}
void test(const char *name, const std::function<void()> &body) {
    try {
        body();
        checks.push_back({{"name", name}, {"ok", true}});
    } catch (const std::exception &error) {
        checks.push_back({{"name", name}, {"ok", false}, {"error", error.what()}});
    }
}
Json rectangle(double x = 0, double y = 0, double width = 600, double depth = 400) {
    return Json{{x, y}, {x + width, y}, {x + width, y + depth}, {x, y + depth}};
}
bool same_points(const Json &actual, const Json &expected) {
    if (actual.size() != expected.size())
        return false;
    for (size_t i = 0; i < actual.size(); ++i)
        for (size_t axis = 0; axis < 2; ++axis)
            if (std::abs(actual[i][axis].get<double>() - expected[i][axis].get<double>()) > 1e-9)
                return false;
    return true;
}
Json body(Json polygon, Json height, const std::string &anchor) {
    return Json{{"polygon", std::move(polygon)}, {"z", height}, {"anchor", anchor}, {"ccw", true}};
}
void fails(const char *function, Json args, const std::string &text,
           const std::string &type = "ValueError") {
    const auto response = invoke(function, args);
    require(response.at("ok") == false, "Invalid input succeeded");
    require(response.at("type") == type, "Exception type changed");
    require(response.at("error").get<std::string>().find(text) != std::string::npos,
            "Exception priority/message changed: " + response.dump());
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 2;
    HMODULE library = LoadLibraryExW(
        argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library)
        return 3;
    call = std::bit_cast<Call>(GetProcAddress(library, "ms3d_model_call"));
    release = std::bit_cast<Free>(GetProcAddress(library, "ms3d_model_free"));
    if (!call || !release) {
        FreeLibrary(library);
        return 4;
    }
    test("Worktop API exposes all four complete native operations", [] {
        const auto info = run("info", Json::array(), Json::object(), "native");
        require(info.at("worktop_api_version") == 1 && info.at("worktop_operations").size() == 4,
                "Incomplete capability");
        require(info.at("python_required_by_library") == false, "Library requires Python");
    });
    test("Distance distinguishes integer overlap zero from floating contact zero", [] {
        const auto overlap = run("distance", {rectangle(), rectangle(50)});
        require(overlap.is_number_integer() && overlap == 0, "Overlap zero lost integer type");
        const auto touching = run("distance", {rectangle(), rectangle(600)});
        require(touching.is_number_float() && touching == 0., "Contact zero lost float type");
        require(run("distance", {rectangle(), rectangle(602.5)}) == 2.5, "Distance changed");
        fails("distance", {Json::array(), rectangle()}, "min()");
    });
    test("Offset preserves zero winding and expands or contracts exact nominal edges", [] {
        auto cw = rectangle();
        std::reverse(cw.begin(), cw.end());
        require(run("offset", {cw, 0}) == cw, "Zero offset reoriented input");
        require(same_points(run("offset", {cw, 20, false}), rectangle(-20, -20, 640, 440)),
                "Clockwise offset changed geometry/order");
        require(same_points(run("offset", {rectangle(), -10}), rectangle(10, 10, 580, 380)),
                "Inward offset changed");
        fails("offset", {Json{{0, 0}, {0, 0}, {10, 0}, {10, 10}}, 1}, "float division by zero",
              "ZeroDivisionError");
    });
    test("Union traces one lexicographically anchored CCW outline", [] {
        require(run("union", {Json::array({rectangle(), rectangle(600)})}) ==
                    rectangle(0, 0, 1200, 400),
                "Adjacent boundary union changed order");
        const Json notch = {{0, 0}, {900, 0}, {900, 400}, {400, 400}, {400, 900}, {0, 900}};
        require(run("union", {Json::array({notch})}) == notch, "Concave notch was filled");
        fails("union", {Json::array()}, "Nie można wyznaczyć");
        fails("union", {Json::array({rectangle(), rectangle(700)})}, "jednego połączonego");
        fails("union", {Json::array({rectangle(), rectangle(600, 400)})}, "tylko punktem");
    });
    test("Union rejects a hole and preserves negative rounded zeros", [] {
        const auto ring = Json::array({rectangle(0, 0, 100, 20), rectangle(0, 80, 100, 20),
                                       rectangle(0, 20, 20, 60), rectangle(80, 20, 20, 60)});
        fails("union", {ring}, "jednego połączonego");
        const auto rounded =
            run("union", {Json::array({rectangle(-.0000004, -.0000004, 1.0000004, 1.0000004)})});
        require(std::signbit(rounded[0][0].get<double>()) &&
                    std::signbit(rounded[0][1].get<double>()),
                "Signed zero was normalized");
    });
    test("Geometry localizes contour, averages heights and retains ordered anchors", [] {
        const auto bodies =
            Json::array({body(rectangle(), 800, "Żółć B"), body(rectangle(600), 800., "A")});
        const auto before = bodies.dump();
        const auto value = run("geometry", {bodies, 20});
        require(value.at("contour") == rectangle(0, 0, 1240, 440), "Finished contour changed");
        require(value.at("origin") == Json::array({-20., -20., 800.}), "World origin changed");
        require(value.at("anchors") == Json::array({"Żółć B", "A"}), "Anchor order/UTF8 lost");
        require(bodies.dump() == before, "Input mutated");
    });
    test("Geometry preserves height overlap connectivity and union error priority", [] {
        fails("geometry", {Json::array(), 20}, "max()");
        fails("geometry",
              {Json::array({body(rectangle(), 0, "A"), body(rectangle(50), 1, "B")}), "bad"},
              "tę samą wysokość");
        fails("geometry",
              {Json::array({body(rectangle(), 0, "A"), body(rectangle(50), 0, "B"),
                            body(rectangle(3000), 0, "C")}),
               20},
              "nachodzą na siebie");
        fails("geometry",
              {Json::array({body(rectangle(), 0, "A"), body(rectangle(600.5001), 0, "B")}), 20},
              "Dosuń szafki");
        fails("geometry",
              {Json::array({body(rectangle(), 0, "A"), body(rectangle(600.5), 0, "B")}), 0},
              "jednego połączonego");
        const auto joined =
            run("geometry",
                {Json::array({body(rectangle(), 0, "A"), body(rectangle(600.5), .1, "B")}), .25});
        require(joined.at("origin")[2] == .05, "Exact height threshold/mean changed");
    });
    test("Geometry retains the original final contour 128 vertex validator", [] {
        Json polygon = Json::array();
        for (int i = 0; i < 129; ++i) {
            const auto angle = double(i) * std::acos(-1.) * 2 / 129;
            polygon.push_back({100 + 100 * std::cos(angle), 100 + 100 * std::sin(angle)});
        }
        require(run("union", {Json::array({polygon})}).size() == 129,
                "Standalone union acquired an artificial vertex cap");
        fails("geometry", {Json::array({body(polygon, 0, "A")}), 0}, "3 do 128");
    });
    test("Typed zero division and strict orientation arguments remain explicit", [] {
        const Json duplicate = {{0, 0}, {0, 0}, {10, 0}, {10, 10}, {0, 10}};
        fails("union", {Json::array({duplicate, rectangle(30, 0, 10, 10)})}, "division by zero",
              "ZeroDivisionError");
        fails("union", {Json::array({rectangle()}), Json::array({1})}, "wartości logicznej");
        require(invoke("offset", {rectangle(), 1, true}, Json{{"ccw", true}}).at("ok") == false,
                "Duplicate arguments accepted");
    });
    size_t round_count = 0;
    test("Decimal round matches bundled Python 3.13 exactly across binary ties and exponents", [&] {
        std::ifstream input(std::filesystem::path(argv[3]), std::ios::binary);
        Json fixture;
        input >> fixture;
        require(fixture.at("python").get<std::string>().rfind("3.13.", 0) == 0,
                "Round oracle was not generated by bundled Python 3.13");
        for (const auto &item : fixture.at("cases")) {
            const auto raw = std::stoull(item.at("input").get<std::string>(), nullptr, 16);
            const auto expected = std::stoull(item.at("expected").get<std::string>(), nullptr, 16);
            const auto actual = std::bit_cast<uint64_t>(mebel::model::worktop::decimal_round(
                std::bit_cast<double>(uint64_t(raw)), item.at("digits").get<int>()));
            require(actual == expected, "Python round bit mismatch: " + item.dump());
            ++round_count;
        }
        require(round_count > 5000, "Round fixture is incomplete");
    });
    test("Native buffers and resident document ownership remain balanced", [] {
        const auto info = run("info", Json::array(), Json::object(), "native");
        require(info.at("outstanding_buffers") == 0 && info.at("document_active_models") == 0,
                "Native ownership leak");
    });
    bool ok = true;
    for (const auto &check : checks)
        if (!check.at("ok").get<bool>())
            ok = false;
    Json report = {{"ok", ok},
                   {"checks", checks},
                   {"count", checks.size()},
                   {"round_cases", round_count},
                   {"process_uses_python", false}};
    std::ofstream output(std::filesystem::path(argv[2]), std::ios::binary);
    output << report.dump(2);
    output.close();
    std::cout << report.dump() << '\n';
    FreeLibrary(library);
    return ok ? 0 : 1;
}
