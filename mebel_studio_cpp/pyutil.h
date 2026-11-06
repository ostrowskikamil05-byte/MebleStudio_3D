// Odpowiedniki wbudowanych funkcji i biblioteki standardowej Pythona,
// z których korzystają przetłumaczone moduły (float(), repr(), json.dumps,
// int(s, 16), functools.lru_cache, Path.write_text, webbrowser.open...).
#pragma once
#include <cmath>
#include <filesystem>
#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace py {

using json = nlohmann::ordered_json;  // dict w Pythonie zachowuje kolejność kluczy

// Wyjątki Pythona
using ValueError = std::invalid_argument;
using IndexError = std::out_of_range;
using KeyError = std::out_of_range;
using OSError = std::system_error;
using TypeError = json::exception;
struct StopIteration : std::runtime_error {
    StopIteration() : std::runtime_error("StopIteration") {}
};
[[noreturn]] void type_error(const std::string& message);

// Ścieżki (UTF-8 <-> std::filesystem)
std::filesystem::path to_path(const std::string& s);
std::string path_str(const std::filesystem::path& p);

// Tekst
std::string strip(const std::string& s);                                   // str.strip()
std::string join(const std::vector<std::string>& items, const std::string& sep);
std::vector<std::string> split(const std::string& s, char sep);            // str.split(sep)
std::vector<std::string> splitlines(const std::string& s);                 // str.splitlines()
std::string decode_utf8_replace(const std::string& data);                  // bytes.decode('utf-8', errors='replace')
std::string replace_all(std::string s, const std::string& from, const std::string& to);
std::string lower(std::string s);                                           // str.lower() (ASCII)
std::string upper(std::string s);                                           // str.upper() (ASCII)
size_t len(const std::string& s);                                           // len(str): znaki, nie bajty
std::string html_escape(const std::string& s);                              // html.escape(s)

// Liczby
double to_float(const std::string& text);   // float(str)
double to_float(const json& value);         // float(value)
long long int16(const std::string& text);   // int(text, 16)
long long to_int(const std::string& text);  // int(text)
std::string repr(double v);                 // repr(float)
std::string repr(const json& v);            // repr(value): lista/słownik jak w Pythonie
double round_half_even(double x);           // round(x)
double round_to(double x, int ndigits);     // round(x, ndigits)
double mod(double a, double b);             // a % b dla float: wynik ma znak dzielnika
double sum(const std::vector<double>& values);  // sum(floats): od Pythona 3.12 z kompensacją Neumaiera

// Wartości dynamiczne
std::string str(const json& v);                                       // str(value)
bool truthy(const json& v);                                           // bool(value)
json get(const json& d, const std::string& key, const json& fallback = nullptr);  // dict.get(key, default)
json merged(const json& base, const json& extra);                     // dict(base, **extra)
std::string dumps(const json& j, bool ensure_ascii = true, int indent = -1, bool sort_keys = false);  // json.dumps

// seq[index] z indeksami ujemnymi
template <class T> T& index(std::vector<T>& seq, long long i) {
    if (i < 0) i += (long long)seq.size();
    if (i < 0 || i >= (long long)seq.size()) throw IndexError("list index out of range");
    return seq[(size_t)i];
}

// hashlib.sha256(data).hexdigest()
std::string sha256_hex(const std::string& data);

// Pliki i system
std::string read_text(const std::string& path);                     // Path.read_text(encoding='utf-8')
void write_text(const std::string& path, const std::string& text);   // Path.write_text(text, encoding='utf-8')
std::filesystem::path home();                                         // Path.home()
std::optional<std::string> getenv(const std::string& name);           // os.environ.get(name)
std::filesystem::path executable_dir();                               // Path(sys.executable).resolve().parent
std::string as_uri(const std::filesystem::path& p);                   // Path.as_uri()
void webbrowser_open(const std::string& uri);                         // webbrowser.open(uri)
double monotonic();                                                   // time.monotonic()

// functools.lru_cache(maxsize=N) dla funkcji z kluczem tekstowym
template <class V> class LruCache {
public:
    explicit LruCache(size_t maxsize) : maxsize_(maxsize) {}
    const V& get(const std::string& key, const std::function<V(const std::string&)>& compute) {
        auto found = index_.find(key);
        if (found != index_.end()) {
            order_.splice(order_.begin(), order_, found->second);
            return found->second->second;
        }
        V value = compute(key);
        order_.emplace_front(key, std::move(value));
        index_[key] = order_.begin();
        if (order_.size() > maxsize_) { index_.erase(order_.back().first); order_.pop_back(); }
        return order_.front().second;
    }
    void clear() { index_.clear(); order_.clear(); }  // cache_clear()
private:
    size_t maxsize_;
    std::list<std::pair<std::string, V>> order_;
    std::unordered_map<std::string, typename std::list<std::pair<std::string, V>>::iterator> index_;
};

}  // namespace py
