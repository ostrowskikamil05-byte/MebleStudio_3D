#include "wood_grain.h"

#include <algorithm>
#include <cmath>

namespace wood_grain {

namespace {

uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Liczba z przedziału [0,1) numer `k` dla danego ziarna.
double unit(uint64_t seed, int k) { return static_cast<double>(mix64(seed + 0x632be59bd9b4e019ULL * static_cast<uint64_t>(k + 1)) >> 11) / 9007199254740992.0; }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

}  // namespace

uint64_t hash(const std::string& text) {
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    return mix64(h);
}

Params params_for(const std::string& id) {
    const uint64_t h = hash(id);
    Params p;
    p.axis_across = lerp(-260, 260, unit(h, 0));
    p.axis_depth = lerp(3, 49, unit(h, 1));
    p.tilt = lerp(-0.15, 0.15, unit(h, 2));
    p.ring_width = lerp(3.2, 7.8, unit(h, 3));
    p.tone = lerp(-1, 1, unit(h, 4));
    p.warm = lerp(-1, 1, unit(h, 5));
    p.contrast = lerp(0.55, 1.35, unit(h, 6));
    p.fibre = lerp(0.5, 1.4, unit(h, 7));
    return p;
}

std::array<double, 3> grain_direction(const core::Part& part) {
    const bool along_v = part.grain && part.grain_v;
    core::Point3 a = core::world(part, 0, 0, 0), b = core::world(part, along_v ? 0 : 1, along_v ? 1 : 0, 0);
    std::array<double, 3> d{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length < 1e-9) return {1, 0, 0};
    return {d[0] / length, d[1] / length, d[2] / length};
}

VertexData vertex_data(const core::Part* part, const std::string& id) {
    const Params p = params_for(id);
    const uint64_t h = hash(id);
    VertexData v;
    v.seed = {static_cast<float>(unit(h, 0)), static_cast<float>(unit(h, 1)), static_cast<float>(unit(h, 2)), static_cast<float>(unit(h, 3))};
    std::array<double, 3> dir{1, 0, 0};
    core::Point3 origin{0, 0, 0};
    if (part != nullptr) {
        dir = grain_direction(*part);
        origin = core::world(*part, 0, 0, 0);
    }
    v.dir = {static_cast<float>(dir[0]), static_cast<float>(dir[1]), static_cast<float>(dir[2]), static_cast<float>(p.tone)};
    v.origin = {static_cast<float>(origin[0]), static_cast<float>(origin[1]), static_cast<float>(origin[2]), static_cast<float>(p.warm)};
    v.extra = {static_cast<float>(p.contrast), static_cast<float>(p.fibre), static_cast<float>(unit(h, 8)), 0.f};
    return v;
}

namespace {
// Korekcja kolorów po obliczeniu oświetlenia (GLSL w nawiasach klamrowych jako osobny blok).
const char* GRADE_BLOCK =
    "  {\n"
    "    vec3 fgC = clamp(gl_FragData[0].rgb, 0.0, 4.0);\n"
    "    float fgL = dot(fgC, vec3(0.2126, 0.7152, 0.0722));\n"
    "    fgC = max(mix(vec3(fgL), fgC, 1.16), vec3(0.0));\n"
    "    fgC = mix(fgC, fgC * vec3(1.04, 1.0, 0.94), smoothstep(0.35, 0.9, fgL) * 0.6);\n"
    "    vec3 fgHi = max(fgC - 0.82, vec3(0.0));\n"
    "    fgC = min(fgC, vec3(0.82)) + 0.18 * (vec3(1.0) - exp(-fgHi * 5.0));\n"
    "    fgC = mix(fgC, fgC * fgC * (3.0 - 2.0 * fgC), 0.18);\n"
    "    gl_FragData[0].rgb = clamp(fgC, 0.0, 1.0);\n"
    "  }\n";
}  // namespace

Shader grade_shader() {
    Shader s;
    s.fragment_declarations = "//VTK::Light::Dec\n";
    s.fragment_code = std::string("//VTK::Light::Impl\n") + GRADE_BLOCK;
    return s;
}

Shader shader_code(bool grade) {
    Shader s;
    s.vertex_declarations =
        "//VTK::PositionVC::Dec\n"
        "in vec4 grainSeedMC;\nin vec4 grainDirMC;\nin vec4 grainOrgMC;\nin vec4 grainExtraMC;\n"
        "out vec3 fgPos;\nout vec4 fgSeed;\nout vec4 fgDir;\nout vec4 fgOrg;\nout vec4 fgEx;\n";
    s.vertex_code =
        "//VTK::PositionVC::Impl\n"
        "fgPos = vertexMC.xyz; fgSeed = grainSeedMC; fgDir = grainDirMC; fgOrg = grainOrgMC; fgEx = grainExtraMC;\n";
    s.fragment_declarations =
        "//VTK::Light::Dec\n"
        "in vec3 fgPos;\nin vec4 fgSeed;\nin vec4 fgDir;\nin vec4 fgOrg;\nin vec4 fgEx;\n"
        "float fgHash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "float fgHash1(float x) { return fract(sin(x * 127.1 + 311.7) * 43758.5453); }\n"
        "float fgNoise(vec2 p) {\n"
        "  vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
        "  float a = fgHash(i); float b = fgHash(i + vec2(1.0, 0.0)); float c = fgHash(i + vec2(0.0, 1.0)); float d = fgHash(i + vec2(1.0, 1.0));\n"
        "  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
        "}\n";
    // Lokalne współrzędne: fgAlong — wzdłuż włókien, fgAcross — w poprzek, w płaszczyźnie ścianki. Oś „pnia” leży równolegle do włókien,
    // fgD0 pod powierzchnią i lekko pochylona (fgTilt), więc kontury odległości od osi dają łuki „katedry” (parabole wokół miejsca,
    // w którym oś zbliża się do powierzchni). Dalej: szum przesuwa współrzędne (faliste włókna), przyrost roczny ma zmienną szerokość,
    // a każdy słój własną siłę (fgRr). Z daleka fwidth wygasza drobne linie, żeby nie powstawał mora.
    s.fragment_code =
        "//VTK::Light::Impl\n"
        "{\n"
        "  vec3 fgP = fgPos - fgOrg.xyz;\n"
        "  vec3 fgD = normalize(fgDir.xyz);\n"
        "  vec3 fgN = cross(dFdx(fgPos), dFdy(fgPos));\n"
        "  float fgNl = length(fgN);\n"
        "  fgN = fgNl > 1e-8 ? fgN / fgNl : vec3(0.0, 0.0, 1.0);\n"
        "  float fgAlong = dot(fgP, fgD);\n"
        "  float fgA = (fgSeed.x - 0.5) * 520.0;\n"
        "  float fgD0 = 3.0 + fgSeed.y * 46.0;\n"
        "  float fgTilt = (fgSeed.z - 0.5) * 0.30;\n"
        "  float fgRingW = 3.2 + fgSeed.w * 4.6;\n"
        "  float fgV = fgEx.z;\n"
        "  vec3 fgS = cross(fgN, fgD);\n"
        "  float fgSl = length(fgS);\n"
        "  bool fgPlanar = fgSl > 0.3;\n"
        "  float fgAcross = 0.0;\n"
        "  vec2 fgQ = vec2(0.0);\n"
        "  if (fgPlanar) {\n"
        "    fgS /= fgSl;\n"
        "    fgAcross = dot(fgP, fgS);\n"
        "  } else {\n"
        "    vec3 fgH = abs(fgD.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
        "    vec3 fgC1 = normalize(cross(fgD, fgH)); vec3 fgC2 = cross(fgD, fgC1);\n"
        "    fgQ = vec2(dot(fgP, fgC1), dot(fgP, fgC2)) - vec2(fgA, fgD0) * 0.05;\n"
        "    fgAcross = fgQ.x;\n"
        "  }\n"
        "  float fgW1 = fgNoise(vec2(fgAlong * 0.0035 + fgSeed.x * 17.0, fgAcross * 0.009 + fgSeed.y * 13.0)) - 0.5;\n"
        "  float fgW2 = fgNoise(vec2(fgAlong * 0.011 + fgSeed.z * 7.0, fgAcross * 0.030 + fgSeed.w * 5.0)) - 0.5;\n"
        "  float fgR;\n"
        "  if (fgPlanar) fgR = length(vec2(fgAcross + fgW1 * 30.0 + fgW2 * 6.0 - fgA, fgD0 + fgTilt * (fgAlong + fgW1 * 70.0)));\n"
        "  else fgR = length(fgQ + vec2(fgW1, fgW2) * 3.0);\n"
        "  float fgG = fgR / fgRingW;\n"
        "  fgG += (fgNoise(vec2(fgR * 0.040 + fgV * 31.0, fgSeed.x * 9.0)) - 0.5) * 2.4;\n"
        "  fgG += (fgNoise(vec2(fgR * 0.27 + fgSeed.y * 19.0, fgSeed.z * 3.0)) - 0.5) * 0.5;\n"
        "  float fgId = floor(fgG);\n"
        "  float fgF = fgG - fgId;\n"
        "  float fgRr = fgHash1(fgId + fgV * 57.0);\n"
        "  float fgFade = 1.0 - smoothstep(0.35, 1.0, fwidth(fgG));\n"
        "  float fgFade2 = 1.0 - smoothstep(0.1, 0.5, fwidth(fgR * 0.085));\n"
        "  float fgLate = smoothstep(0.55, 0.96, fgF) * (1.0 - smoothstep(0.97, 1.0, fgF));\n"
        "  float fgEarly = smoothstep(0.0, 0.9, fgF);\n"
        "  float fgDark = (0.30 * fgEarly + 0.70 * fgLate) * (0.25 + 0.75 * fgRr);\n"
        "  float fgRingTerm = fgDark * 0.30 * fgEx.x * fgFade;\n"
        "  float fgBand = (fgNoise(vec2(fgR * 0.030 + fgSeed.y * 41.0, fgSeed.w * 7.0)) - 0.5) * 0.16 * fgFade2;\n"
        "  float fgBand2 = (fgNoise(vec2(fgR * 0.085 + fgSeed.x * 43.0, fgV * 7.0)) - 0.5) * 0.08 * fgFade2;\n"
        "  float fgToneRing = (fgRr - 0.5) * 0.07 * fgFade;\n"
        "  float fgFb1 = 1.0 - smoothstep(0.4, 1.2, fwidth(fgAcross * 0.85));\n"
        "  float fgFb2 = 1.0 - smoothstep(0.4, 1.2, fwidth(fgAcross * 2.6));\n"
        "  float fgFibre = ((fgNoise(vec2(fgAlong * 0.007 + fgSeed.z * 11.0, fgAcross * 0.85 + fgSeed.x * 5.0)) - 0.5) * 0.10 * fgFb1\n"
        "                 + (fgNoise(vec2(fgAlong * 0.020 + fgSeed.w * 3.0, fgAcross * 2.6 + fgSeed.y * 9.0)) - 0.5) * 0.07 * fgFb2) * fgEx.y;\n"
        "  float fgFb3 = 1.0 - smoothstep(0.4, 1.2, fwidth(fgAcross * 1.9));\n"
        "  float fgPores = smoothstep(0.80, 0.93, fgNoise(vec2(fgAlong * 0.22 + fgSeed.y * 23.0, fgAcross * 1.9 + fgSeed.w * 29.0))) * fgFb3 * 0.14 * fgEx.y * (1.0 - 0.5 * fgF);\n"
        "  float fgCloud = (fgNoise(vec2(fgAlong * 0.0018 + fgV * 5.0, fgAcross * 0.004 + fgSeed.z * 3.0)) - 0.5) * 0.10;\n"
        "  float fgK = 1.07 + fgDir.w * 0.045 + (fgV - 0.5) * 0.10 + fgBand + fgBand2 + fgToneRing + fgFibre + fgCloud - fgRingTerm - fgPores;\n"
        "  fgK = clamp(fgK, 0.55, 1.35);\n"
        "  float fgLc = fgLate * fgFade;\n"
        "  vec3 fgTint = vec3(1.0 + fgOrg.w * 0.03 + fgLc * 0.03, 1.0 - fgLc * 0.03, 1.0 - fgOrg.w * 0.045 - fgLc * 0.10);\n"
        "  gl_FragData[0].rgb *= fgK * fgTint;\n"
        "}\n";
    if (grade) s.fragment_code += GRADE_BLOCK;
    return s;
}

int line_count(const std::string& id) { return 5 + static_cast<int>(unit(hash(id), 11) * 8); }  // 5 … 12

double line_mix(const std::string& id) { return lerp(0.06, 0.15, unit(hash(id), 12)); }

std::vector<double> line_fractions(const std::string& id, int count) {
    std::vector<double> out;
    if (count <= 0) return out;
    const uint64_t h = hash(id);
    const double cell = 1.0 / (count + 1);
    for (int i = 0; i < count; ++i) {
        // Odchylenie do ±0.3 odstępu — kolejność linii zostaje zachowana, a rozkład nie przypomina linijki.
        double fraction = (i + 1) * cell + (unit(h, 20 + i) - 0.5) * 0.6 * cell;
        out.push_back(std::min(0.96, std::max(0.04, fraction)));
    }
    return out;
}

}  // namespace wood_grain
