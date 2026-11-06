#include "pyutil.h"
#include <cstdint>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace py {

namespace fs = std::filesystem;

void type_error(const std::string& message) { throw json::type_error::create(302, message, nullptr); }

fs::path to_path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string path_str(const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }

std::string strip(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

std::string join(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) { if (i) out += sep; out += items[i]; }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out; size_t start = 0;
    for (size_t pos; (pos = s.find(sep, start)) != std::string::npos; start = pos + 1) out.push_back(s.substr(start, pos - start));
    out.push_back(s.substr(start));
    return out;
}

std::vector<std::string> splitlines(const std::string& s) {
    // Granice wierszy str.splitlines(): \n \r \r\n \v \f \x1c \x1d \x1e \x85 \u2028 \u2029
    std::vector<std::string> out; std::string line; bool pending = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        size_t width = 0;
        if (c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0x1c || c == 0x1d || c == 0x1e) width = 1;
        else if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0x85) width = 2;
        else if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 && ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9)) width = 3;
        if (width) {
            out.push_back(line); line.clear(); pending = false;
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
            i += width - 1;
        } else { line += s[i]; pending = true; }
    }
    if (pending) out.push_back(line);
    return out;
}

std::string decode_utf8_replace(const std::string& data) {
    // bytes.decode('utf-8', errors='replace'): każdy maksymalny niepoprawny
    // fragment zastępuje jeden znak U+FFFD, tak jak w CPythonie.
    static const std::string replacement = "\xEF\xBF\xBD";
    std::string out; out.reserve(data.size());
    size_t i = 0, n = data.size();
    auto at = [&](size_t k) { return (unsigned char)data[k]; };
    while (i < n) {
        unsigned char c = at(i);
        if (c < 0x80) { out += (char)c; ++i; continue; }
        size_t need; unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else { out += replacement; ++i; continue; }
        size_t k = 1;
        for (; k <= need; ++k) {
            if (i + k >= n) break;
            unsigned char d = at(i + k);
            unsigned char l = k == 1 ? lo : 0x80, h = k == 1 ? hi : 0xBF;
            if (d < l || d > h) break;
        }
        if (k > need) { out.append(data, i, need + 1); i += need + 1; }
        else { out += replacement; i += k; }
    }
    return out;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
    return s;
}

std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; }
std::string upper(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); }); return s; }

size_t len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            default: out += c;
        }
    }
    return out;
}

double to_float(const std::string& text) {
    std::string s = strip(text);
    char* end = nullptr;
    double v = s.empty() ? 0 : std::strtod(s.c_str(), &end);
    if (s.empty() || end != s.c_str() + s.size()) throw ValueError("could not convert string to float: '" + text + "'");
    return v;
}

double to_float(const json& value) {
    if (value.is_boolean()) return value.get<bool>() ? 1 : 0;
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) return to_float(value.get<std::string>());
    type_error(std::string("float() argument must be a string or a real number, not '") + value.type_name() + "'");
}

long long to_int(const std::string& text) {
    auto fail = [&] { return ValueError("invalid literal for int() with base 10: '" + text + "'"); };
    std::string s = strip(text);
    size_t i = 0; bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; ++i; }
    if (i >= s.size()) throw fail();
    long long value = 0; bool previous_digit = false;
    for (; i < s.size(); ++i) {
        char ch = s[i];
        if (ch == '_') { if (!previous_digit || i + 1 >= s.size()) throw fail(); previous_digit = false; continue; }
        if (ch < '0' || ch > '9') throw fail();
        value = value * 10 + (ch - '0'); previous_digit = true;
    }
    return neg ? -value : value;
}

long long int16(const std::string& text) {
    auto fail = [&] { return ValueError("invalid literal for int() with base 16: '" + text + "'"); };
    std::string s = strip(text);
    size_t i = 0; bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; ++i; }
    bool can_underscore = false;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { i += 2; can_underscore = true; }
    long long v = 0; bool digit = false, last_underscore = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c == '_') {
            if (!can_underscore) throw fail();
            can_underscore = false; last_underscore = true;
        } else if (std::isxdigit((unsigned char)c)) {
            v = v * 16 + (std::isdigit((unsigned char)c) ? c - '0' : std::tolower((unsigned char)c) - 'a' + 10);
            digit = true; can_underscore = true; last_underscore = false;
        } else throw fail();
    }
    if (!digit || last_underscore) throw fail();
    return neg ? -v : v;
}

std::string repr(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    bool neg = s[0] == '-'; if (neg) s.erase(0, 1);
    auto epos = s.find('e'); int exp = std::stoi(s.substr(epos + 1));
    std::string digits = s.substr(0, epos);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    std::string out;
    if (exp >= -4 && exp < 16) {
        if (exp >= 0) {
            if ((int)digits.size() <= exp + 1) out = digits + std::string(exp + 1 - digits.size(), '0') + ".0";
            else out = digits.substr(0, exp + 1) + "." + digits.substr(exp + 1);
        } else out = "0." + std::string(-exp - 1, '0') + digits;
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += std::format("e{}{:02d}", exp < 0 ? '-' : '+', std::abs(exp));
    }
    return (neg ? "-" : "") + out;
}

double round_half_even(double x) { return std::nearbyint(x); }

double mod(double a, double b) {
    if (b == 0) throw std::domain_error("float modulo");  // ZeroDivisionError
    double r = std::fmod(a, b);
    if (r != 0) { if ((b < 0) != (r < 0)) r += b; }
    else r = std::copysign(0.0, b);
    return r;
}

// CPython 3.12+: sum() liczb float używa ulepszonego algorytmu Kahana–Babuśki (Neumaier).
double sum(const std::vector<double>& values) {
    double f_result = 0.0, c = 0.0;
    for (double x : values) {
        double t = f_result + x;
        if (std::isfinite(t)) {
            if (std::fabs(f_result) >= std::fabs(x)) c += (f_result - t) + x;
            else c += (x - t) + f_result;
        } else {
            c = 0.0;  // inf/nan: wynik jak w zwykłym dodawaniu
        }
        f_result = t;
    }
    if (c != 0.0 && std::isfinite(c)) f_result += c;
    return f_result;
}

namespace {
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}

std::string sha256_hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg = data; uint64_t bits = (uint64_t)data.size() * 8;
    msg.push_back((char)0x80);
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) msg.push_back((char)((bits >> (8 * i)) & 0xff));
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)(uint8_t)msg[chunk + 4 * i] << 24) | ((uint32_t)(uint8_t)msg[chunk + 4 * i + 1] << 16) | ((uint32_t)(uint8_t)msg[chunk + 4 * i + 2] << 8) | (uint32_t)(uint8_t)msg[chunk + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), maj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    static const char* digits = "0123456789abcdef"; std::string out;
    for (uint32_t v : h) for (int i = 28; i >= 0; i -= 4) out.push_back(digits[(v >> i) & 0xf]);
    return out;
}

double round_to(double x, int ndigits) {
    if (!std::isfinite(x)) return x;
    // printf zaokrągla dokładną wartość binarną (remisy do parzystej), tak jak round() w Pythonie
    return std::strtod(std::format("{:.{}f}", x, ndigits).c_str(), nullptr);
}

static std::string repr_string(const std::string& s) {
    bool has_single = s.find('\'') != std::string::npos, has_double = s.find('"') != std::string::npos;
    char quote = has_single && !has_double ? '"' : '\'';
    std::string out(1, quote);
    for (unsigned char c : s) {
        if (c == (unsigned char)quote || c == '\\') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20 || c == 0x7f) out += std::format("\\x{:02x}", (unsigned)c);
        else out += (char)c;
    }
    out += quote;
    return out;
}

// repr(value) dla list, słowników i wartości prostych.
std::string repr(const json& v) {
    if (v.is_string()) return repr_string(v.get<std::string>());
    if (v.is_array()) {
        std::string out = "[";
        bool first = true;
        for (const auto& item : v) { out += (first ? "" : ", ") + repr(item); first = false; }
        return out + "]";
    }
    if (v.is_object()) {
        std::string out = "{";
        bool first = true;
        for (auto it = v.begin(); it != v.end(); ++it) { out += (first ? "" : ", ") + repr_string(it.key()) + ": " + repr(it.value()); first = false; }
        return out + "}";
    }
    return str(v);
}

std::string str(const json& v) {
    if (v.is_null()) return "None";
    if (v.is_boolean()) return v.get<bool>() ? "True" : "False";
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_float()) return repr(v.get<double>());
    if (v.is_string()) return v.get<std::string>();
    return repr(v);
}

bool truthy(const json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get_ref<const std::string&>().empty();
    if (v.is_array() || v.is_object()) return !v.empty();
    return true;
}

json get(const json& d, const std::string& key, const json& fallback) {
    return d.is_object() && d.contains(key) ? d[key] : fallback;
}

json merged(const json& base, const json& extra) {
    json out = base;
    for (auto& [k, v] : extra.items()) out[k] = v;
    return out;
}

namespace {

void dump_string(std::string& out, const std::string& s, bool ensure_ascii) {
    out += '"';
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c == '"') { out += "\\\""; ++i; }
        else if (c == '\\') { out += "\\\\"; ++i; }
        else if (c == '\n') { out += "\\n"; ++i; }
        else if (c == '\r') { out += "\\r"; ++i; }
        else if (c == '\t') { out += "\\t"; ++i; }
        else if (c == '\b') { out += "\\b"; ++i; }
        else if (c == '\f') { out += "\\f"; ++i; }
        else if (c < 0x20 || (ensure_ascii && c == 0x7f)) { out += std::format("\\u{:04x}", c); ++i; }
        else if (c < 0x80 || !ensure_ascii) { out += (char)c; ++i; }
        else {
            unsigned cp = c; size_t n = 1;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
            for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
            if (cp > 0xFFFF) {
                cp -= 0x10000;
                out += std::format("\\u{:04x}\\u{:04x}", 0xD800 | (cp >> 10), 0xDC00 | (cp & 0x3FF));
            } else out += std::format("\\u{:04x}", cp);
            i += n;
        }
    }
    out += '"';
}

void dump_value(std::string& out, const json& j, bool ensure_ascii, int indent, bool sort_keys, int level) {
    auto newline = [&](int lvl) { out += '\n'; out += std::string(lvl * indent, ' '); };
    if (j.is_null()) out += "null";
    else if (j.is_boolean()) out += j.get<bool>() ? "true" : "false";
    else if (j.is_number_unsigned()) out += std::to_string(j.get<unsigned long long>());
    else if (j.is_number_integer()) out += std::to_string(j.get<long long>());
    else if (j.is_number_float()) {
        double v = j.get<double>();
        out += std::isnan(v) ? "NaN" : std::isinf(v) ? (v > 0 ? "Infinity" : "-Infinity") : repr(v);
    } else if (j.is_string()) dump_string(out, j.get<std::string>(), ensure_ascii);
    else if (j.is_array()) {
        if (j.empty()) { out += "[]"; return; }
        out += '[';
        bool first = true;
        for (auto& item : j) {
            if (!first) out += indent >= 0 ? "," : ", ";
            if (indent >= 0) newline(level + 1);
            dump_value(out, item, ensure_ascii, indent, sort_keys, level + 1); first = false;
        }
        if (indent >= 0) newline(level);
        out += ']';
    } else if (j.is_object()) {
        if (j.empty()) { out += "{}"; return; }
        std::vector<std::string> keys;
        for (auto& [key, _] : j.items()) keys.push_back(key);
        if (sort_keys) std::sort(keys.begin(), keys.end());
        out += '{';
        bool first = true;
        for (auto& key : keys) {
            if (!first) out += indent >= 0 ? "," : ", ";
            if (indent >= 0) newline(level + 1);
            dump_string(out, key, ensure_ascii); out += ": ";
            dump_value(out, j[key], ensure_ascii, indent, sort_keys, level + 1); first = false;
        }
        if (indent >= 0) newline(level);
        out += '}';
    }
}

}  // namespace

std::string dumps(const json& j, bool ensure_ascii, int indent, bool sort_keys) {
    std::string out; dump_value(out, j, ensure_ascii, indent, sort_keys, 0); return out;
}

std::string read_text(const std::string& path) {
    std::ifstream f(to_path(path), std::ios::binary);
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
    std::stringstream buffer; buffer << f.rdbuf();
    // tryb uniwersalnych końców linii, jak Path.read_text
    return replace_all(replace_all(buffer.str(), "\r\n", "\n"), "\r", "\n");
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream f(to_path(path));  // tryb tekstowy: '\n' -> os.linesep, jak w Pythonie
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
    f << text;
    f.close();
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
}

fs::path home() {
#ifdef _WIN32
    if (const char* h = std::getenv("USERPROFILE")) return to_path(h);
#else
    if (const char* h = std::getenv("HOME")) return to_path(h);
#endif
    return fs::current_path();
}

std::optional<std::string> getenv(const std::string& name) {
    if (const char* value = std::getenv(name.c_str())) return std::string(value);
    return std::nullopt;
}

fs::path executable_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4]; GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return fs::canonical(fs::path(buf)).parent_path();
#else
    std::error_code ec; auto exe = fs::canonical("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

std::string as_uri(const fs::path& p) {
    auto generic = fs::absolute(p).generic_u8string();
    std::string s(generic.begin(), generic.end()), out;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || std::string("/-_.~").find((char)ch) != std::string::npos) out += (char)ch;
        else if (ch == ':' && out.size() == 1) out += (char)ch;  // litera dysku (Windows)
        else out += std::format("%{:02X}", ch);
    }
    return (out.starts_with("/") ? "file://" : "file:///") + out;
}

void webbrowser_open(const std::string& uri) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    [[maybe_unused]] int rc = std::system(("open '" + uri + "'").c_str());
#else
    [[maybe_unused]] int rc = std::system(("xdg-open '" + uri + "' >/dev/null 2>&1 &").c_str());
#endif
}

double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace py
