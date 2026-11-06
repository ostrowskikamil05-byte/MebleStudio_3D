// Atrapa interpretera: rejestruje polecenia, obsługuje zmienne i polecenia Obj, listy Tcl.
#include <tcl.h>
#include <tk.h>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "mock_api.h"

struct Tcl_Interp { std::string result; };
static Tcl_Interp g_interp;
struct Cmd { Tcl_ObjCmdProc* proc; ClientData data; Tcl_CmdDeleteProc* del; };
static std::map<std::string, Cmd> g_cmds;
static std::map<std::string, std::string> g_vars;
std::vector<std::vector<std::string>> g_log;
std::function<std::string(const std::vector<std::string>&, bool&)> g_hook;
std::vector<unsigned char> g_photo;
int g_photo_w = 0, g_photo_h = 0;

extern "C" {
void Tcl_FindExecutable(const char*) {}
Tcl_Interp* Tcl_CreateInterp(void) { return &g_interp; }
int Tcl_Init(Tcl_Interp*) { return TCL_OK; }
int Tk_Init(Tcl_Interp*) { return TCL_OK; }
Tcl_Obj* Tcl_NewStringObj(const char* b, int n) { auto* o = new Tcl_Obj{0, (char*)malloc(n + 1), n}; memcpy(o->bytes, b, n); o->bytes[n] = 0; return o; }
void Tcl_IncrRefCount(Tcl_Obj* o) { ++o->refCount; }
void Tcl_DecrRefCount(Tcl_Obj* o) { if (--o->refCount <= 0) { free(o->bytes); delete o; } }
const char* Tcl_GetString(Tcl_Obj* o) { return o->bytes; }
static Tcl_Obj* g_result_obj = nullptr;
Tcl_Obj* Tcl_GetObjResult(Tcl_Interp* i) { if (g_result_obj) Tcl_DecrRefCount(g_result_obj); g_result_obj = Tcl_NewStringObj(i->result.data(), i->result.size()); Tcl_IncrRefCount(g_result_obj); return g_result_obj; }
void Tcl_SetObjResult(Tcl_Interp* i, Tcl_Obj* o) { i->result = o->bytes; }
int Tcl_EvalObjv(Tcl_Interp* in, int objc, Tcl_Obj* const objv[], int) {
    std::vector<std::string> words; for (int i = 0; i < objc; ++i) words.emplace_back(objv[i]->bytes);
    g_log.push_back(words);
    auto it = g_cmds.find(words[0]);
    if (it != g_cmds.end()) { in->result.clear(); return it->second.proc(it->second.data, in, objc, objv); }
    bool error = false; std::string r = g_hook ? g_hook(words, error) : std::string();
    in->result = r; return error ? TCL_ERROR : TCL_OK;
}
Tcl_Command Tcl_CreateObjCommand(Tcl_Interp*, const char* n, Tcl_ObjCmdProc* p, ClientData d, Tcl_CmdDeleteProc* del) { g_cmds[n] = {p, d, del}; return nullptr; }
int Tcl_DeleteCommand(Tcl_Interp*, const char* n) { auto it = g_cmds.find(n); if (it == g_cmds.end()) return -1; Cmd c = it->second; g_cmds.erase(it); if (c.del) c.del(c.data); return 0; }
const char* Tcl_GetVar2(Tcl_Interp* in, const char* a, const char*, int) { auto it = g_vars.find(a); if (it == g_vars.end()) { in->result = std::string("can't read \"") + a + "\": no such variable"; return nullptr; } return it->second.c_str(); }
const char* Tcl_SetVar2(Tcl_Interp*, const char* a, const char*, const char* v, int) { g_vars[a] = v; return g_vars[a].c_str(); }
void Tcl_Free(char* p) { free(p); }
char* Tcl_Merge(int argc, const char* const* argv) {
    std::string out;
    for (int i = 0; i < argc; ++i) {
        if (i) out += ' ';
        std::string s = argv[i];
        bool quote = s.empty() || s.find_first_of(" \t\n{}[]\"\;$") != std::string::npos;
        out += quote ? "{" + s + "}" : s;
    }
    return strdup(out.c_str());
}
int Tcl_SplitList(Tcl_Interp*, const char* l, int* argc, const char*** argv) {
    std::vector<std::string> items; std::string s = l; size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        std::string cur;
        if (s[i] == '{') { int depth = 0; size_t st = i; for (; i < s.size(); ++i) { if (s[i] == '{') ++depth; else if (s[i] == '}' && --depth == 0) break; } cur = s.substr(st + 1, i - st - 1); ++i; }
        else { while (i < s.size() && !isspace((unsigned char)s[i])) cur += s[i++]; }
        items.push_back(cur);
    }
    size_t total = sizeof(char*) * items.size(); for (auto& x : items) total += x.size() + 1;
    char* block = (char*)malloc(total + 1); const char** vec = (const char**)block; char* p = block + sizeof(char*) * items.size();
    for (size_t k = 0; k < items.size(); ++k) { vec[k] = p; memcpy(p, items[k].c_str(), items[k].size() + 1); p += items[k].size() + 1; }
    *argc = (int)items.size(); *argv = vec; return TCL_OK;
}
void Tk_MainLoop(void) { g_log.push_back({"<mainloop>"}); }
Tk_PhotoHandle Tk_FindPhoto(Tcl_Interp*, const char* n) { return (void*)n; }
int Tk_PhotoPutBlock(Tcl_Interp*, Tk_PhotoHandle, Tk_PhotoImageBlock* b, int, int, int w, int h, int) { g_photo.assign(b->pixelPtr, b->pixelPtr + b->pitch * h); g_photo_w = w; g_photo_h = h; return TCL_OK; }
}
// wywołanie polecenia zarejestrowanego jak z Tcl
std::string mock_invoke(const std::vector<std::string>& words) {
    std::vector<Tcl_Obj*> o; for (auto& w : words) { o.push_back(Tcl_NewStringObj(w.data(), w.size())); Tcl_IncrRefCount(o.back()); }
    int code = Tcl_EvalObjv(&g_interp, (int)o.size(), o.data(), 0);
    for (auto* x : o) Tcl_DecrRefCount(x);
    return (code == TCL_OK ? "" : "ERR:") + g_interp.result;
}
bool mock_has_command(const std::string& n) { return g_cmds.count(n) > 0; }
void mock_set_var(const std::string& n, const std::string& v) { g_vars[n] = v; }
std::string mock_get_var(const std::string& n) { return g_vars[n]; }
