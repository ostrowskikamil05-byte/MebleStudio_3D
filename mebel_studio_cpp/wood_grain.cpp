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
    p.axis_depth = lerp(14, 140, unit(h, 1));
    p.tilt = lerp(-0.09, 0.09, unit(h, 2));
    p.ring_width = lerp(7, 18, unit(h, 3));
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

Shader shader_code() {
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
        "float fgNoise(vec2 p) {\n"
        "  vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
        "  float a = fgHash(i); float b = fgHash(i + vec2(1.0, 0.0)); float c = fgHash(i + vec2(0.0, 1.0)); float d = fgHash(i + vec2(1.0, 1.0));\n"
        "  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
        "}\n";
    s.fragment_code =
        "//VTK::Light::Impl\n"
        "{\n"
        "  vec3 fgP = fgPos - fgOrg.xyz;\n"
        "  vec3 fgD = normalize(fgDir.xyz);\n"
        "  vec3 fgN = cross(dFdx(fgPos), dFdy(fgPos));\n"
        "  float fgNl = length(fgN);\n"
        "  fgN = fgNl > 1e-8 ? fgN / fgNl : vec3(0.0, 0.0, 1.0);\n"
        "  float fgAlong = dot(fgP, fgD);\n"
        "  float fgAxisA = (fgSeed.x - 0.5) * 520.0;\n"
        "  float fgAxisD = 14.0 + fgSeed.y * 126.0;\n"
        "  float fgTilt = (fgSeed.z - 0.5) * 0.18;\n"
        "  float fgRing = 7.0 + fgSeed.w * 11.0;\n"
        "  vec3 fgS = cross(fgN, fgD);\n"
        "  float fgSl = length(fgS);\n"
        "  float fgAcross; float fgR;\n"
        "  if (fgSl > 0.3) {\n"
        "    fgS /= fgSl;\n"
        "    fgAcross = dot(fgP, fgS);\n"
        "    fgR = length(vec2(fgAcross - fgAxisA, fgAxisD + fgTilt * fgAlong));\n"
        "  } else {\n"
        "    vec3 fgH = abs(fgD.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
        "    vec3 fgC1 = normalize(cross(fgD, fgH)); vec3 fgC2 = cross(fgD, fgC1);\n"
        "    vec2 fgQ = vec2(dot(fgP, fgC1), dot(fgP, fgC2)) - vec2(fgAxisA, fgAxisD) * 0.5;\n"
        "    fgAcross = fgQ.x; fgR = length(fgQ);\n"
        "  }\n"
        "  fgR += (fgNoise(vec2(fgAlong * 0.006, fgAcross * 0.012)) - 0.5) * 9.0 + (fgNoise(vec2(fgAlong * 0.02, fgAcross * 0.05)) - 0.5) * 2.5;\n"
        "  float fgPh = fgR / fgRing;\n"
        "  float fgFr = fract(fgPh);\n"
        "  float fgFade = 1.0 - smoothstep(0.35, 0.9, fwidth(fgPh));\n"
        "  float fgLate = pow(fgFr, 2.5);\n"
        "  float fgFade2 = 1.0 - smoothstep(0.4, 1.2, fwidth(fgAcross * 0.22));\n"
        "  float fgFade3 = 1.0 - smoothstep(0.4, 1.2, fwidth(fgAcross * 1.7));\n"
        "  float fgRingTerm = (fgLate - 0.29) * 0.50 * fgEx.x * fgFade;\n"
        "  float fgStreak = (fgNoise(vec2(fgAlong * 0.004, fgAcross * 0.22)) - 0.5) * 0.12 * fgEx.y * fgFade2;\n"
        "  float fgFine = (fgNoise(vec2(fgAlong * 0.02, fgAcross * 1.7)) - 0.5) * 0.10 * fgEx.y * fgFade3;\n"
        "  float fgK = 1.0 + fgDir.w * 0.09 - fgRingTerm + fgStreak + fgFine;\n"
        "  vec3 fgTint = vec3(1.0 + fgOrg.w * 0.05, 1.0, 1.0 - fgOrg.w * 0.07);\n"
        "  fgTint *= mix(vec3(1.0), vec3(1.02, 0.96, 0.88), fgLate * fgFade * 0.6);\n"
        "  gl_FragData[0].rgb *= fgK * fgTint;\n"
        "}\n";
    return s;
}

int line_count(const std::string& id) { return 4 + static_cast<int>(unit(hash(id), 11) * 5); }  // 4 … 8

double line_mix(const std::string& id) { return lerp(0.08, 0.19, unit(hash(id), 12)); }

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
