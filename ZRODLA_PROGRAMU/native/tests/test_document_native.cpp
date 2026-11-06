// Adversarial checks for the editable document, deliberately independent of Python.
#include "../core/core_api.hpp"
#include <windows.h>
#include <atomic>
#include <barrier>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using mebel::model::Json;
namespace {
using Call = char* (*)(const char*);
using Free = void (*)(void*);
Call call = nullptr;
Free release = nullptr;
Json results = Json::array();
std::filesystem::path report_directory;

void require(bool good, const std::string& message) {
    if (!good) throw std::runtime_error(message);
}
Json invoke(const char* function, Json args = Json::array(), const char* module = "document") {
    const auto request = Json{{"module",module},{"function",function},{"args",args},{"kwargs",Json::object()}}.dump();
    char* buffer = call(request.c_str());
    require(buffer != nullptr, "Native response allocation failed");
    std::string response;
    try { response = buffer; } catch (...) { release(buffer); throw; }
    release(buffer);
    return Json::parse(response);
}
Json run(const char* function, Json args = Json::array()) {
    const auto response = invoke(function, args);
    if (!response.at("ok").get<bool>())
        throw std::runtime_error(std::string(function) + ": " + response.at("error").get<std::string>());
    return response.at("result");
}
Json info(uint64_t handle) { return run("info", {handle}); }
Json read(uint64_t handle) { return run("read", {handle}); }
std::string serialized(uint64_t handle) { return read(handle).at("serialized").get<std::string>(); }

// Noncanonical whitespace, nested key order, UTF-8, float spelling and complete
// production metadata must survive because legacy exports use those dictionaries.
const std::string original = R"doc({
 "version":1,"name":"Żółć — dokument testowy",
 "parts":[
  {"id":"A","name":"Półka","group":"Szafka","u":600.0,"v":400,"t":18,"plane":"XY","x":5,"y":6,"z":7,"rx":0,"ry":0,"rz":0,"material":"m","grain":false,"edges":[false,false,false,false],"contour":[],"holes":[[30,40,5,8]],"visible":true,"role":"shelf","assembly":{"anchors":["B"],"plugin":{"z":1.00,"a":2}},"operations":[{"type":"drill","face":"top","u":40,"v":30,"diameter":5,"depth":8,"owner_id":"B","plugin":{"z":1,"a":2}}],"fillets":[],"chamfers":[],"machining":[],"motion":{},"profile":{},"contour_edge":false,"decor":{},"style_source":{"plugin":{"z":1,"a":2}},"configurator_source":{"plugin":{"z":1,"a":2}},"layout":{"x":100,"y":200,"z":0,"yaw":90}},
  {"id":"B","name":"Bok","group":"Szafka","u":600,"v":720,"t":18,"plane":"XZ","material":"m","edges":[false,false,false,false],"assembly":{},"operations":[]}
 ],
 "materials":[{"id":"m","name":"Dąb naturalny","color":"#bd9565","kind":"plyta","price_m2":95,"price_m3":0,"sheet_u":2800,"sheet_v":2070}],
 "hardware":[{"name":"Zawias","qty":2,"price":10,"owner_id":"A","attachment_options":{"descriptor":{"z":1,"a":2}}}],
 "scene":{"objects":[],"plugin":{"z":1.0,"a":2}},
 "kerf":3.2,"margin":10,"edge_t":1,"edge_price":3.5,"labor":0,"markup":20
})doc";

struct Document {
    uint64_t handle = 0;
    explicit Document(const std::string& text = original, int limit = 60) {
        handle = run("create", {text,limit}).at("handle").get<uint64_t>();
    }
    ~Document() { if (handle) { try { run("destroy", {handle}); } catch (...) {} } }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
};
std::string renamed(const std::string& value, const std::string& name) {
    Json data = Json::parse(value); data["name"] = name; return data.dump(2);
}
std::string utf8(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
    return result;
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    require(bool(input),"Cannot read test project");
    return std::string(std::istreambuf_iterator<char>(input),{});
}
void test(const char* label, const std::function<void()>& operation) {
    try { operation(); results.push_back({{"name",label},{"ok",true}}); }
    catch (const std::exception& error) { results.push_back({{"name",label},{"ok",false},{"error",error.what()}}); }
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    HMODULE library = LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) return 3;
    call = reinterpret_cast<Call>(GetProcAddress(library,"ms3d_model_call"));
    release = reinterpret_cast<Free>(GetProcAddress(library,"ms3d_model_free"));
    if (!call || !release) { FreeLibrary(library); return 4; }
    report_directory = std::filesystem::path(argv[2]).parent_path();
    std::filesystem::create_directories(report_directory);

    test("Full UTF8 serialized document retains every byte, nested order and metadata", [] {
        Document document;
        const auto state = read(document.handle);
        require(state.at("serialized") == original, "Full document was reordered, projected or normalized");
        require(state.at("revision") == 1 && state.at("undo_count") == 0 && state.at("redo_count") == 0,
                "Fresh document starts with incorrect history");
        require(state.at("parts_count") == 2 && state.at("materials_count") == 1, "Metadata counts are incorrect");
        require(state.at("byte_length") == original.size(), "Byte length counts Unicode characters instead of UTF8 bytes");
    });
    test("Commit undo redo preserve complete snapshots and monotonic revisions", [] {
        Document document;
        const auto changed = renamed(original,"Zmiana wymiaru i nazwy");
        auto state = run("commit", {document.handle,1,changed});
        require(state.at("changed") == true && state.at("revision") == 2 && state.at("undo_count") == 1, "Commit did not record exactly one state");
        require(serialized(document.handle) == changed, "Commit changed snapshot content");
        state = run("undo", {document.handle,2});
        require(state.at("serialized") == original && state.at("revision") == 3 && state.at("redo_count") == 1, "Undo did not restore the original exact bytes");
        state = run("redo", {document.handle,3});
        require(state.at("serialized") == changed && state.at("revision") == 4 && state.at("undo_count") == 1, "Redo did not restore changed state");
    });
    test("Noop commit preserves redo; real branch clears redo", [] {
        Document document;
        const auto a = renamed(original,"A");
        run("commit", {document.handle,1,a});
        run("undo", {document.handle,2});
        auto state = run("commit", {document.handle,3,original});
        require(state.at("revision") == 3 && state.at("changed") == false && state.at("redo_count") == 1, "No-op commit discarded redo or advanced revision");
        const auto b = renamed(original,"B");
        state = run("commit", {document.handle,3,b});
        require(state.at("revision") == 4 && state.at("redo_count") == 0, "A changed branch did not clear redo");
        state = run("redo", {document.handle,4});
        require(state.at("changed") == false && state.at("revision") == 4 && state.at("serialized") == b, "Empty redo mutated the document");
    });
    test("Checkpoint supports existing pre-edit snapshot calls without an extra undo", [] {
        Document document;
        auto state = run("checkpoint", {document.handle,1,original});
        require(state.at("revision") == 2 && state.at("undo_count") == 1 && state.at("changed") == false, "Checkpoint failed to record equal pre-edit snapshot");
        const auto changed = renamed(original,"Edycja po snapshot");
        state = run("commit", {document.handle,2,changed,false});
        require(state.at("undo_count") == 1 && state.at("revision") == 3, "Publishing an already-checkpointed edit duplicated undo");
        state = run("undo", {document.handle,3});
        require(state.at("serialized") == original && state.at("undo_count") == 0, "Single undo did not restore pre-edit state");
    });
    test("Checkpoint adopts untracked mutable edits and clears old redo", [] {
        Document document;
        run("commit", {document.handle,1,renamed(original,"Prior")});
        run("undo", {document.handle,2});
        const auto untracked = renamed(original,"Legacy direct mutation");
        auto state = run("checkpoint", {document.handle,3,untracked});
        require(state.at("revision") == 4 && state.at("redo_count") == 0 && serialized(document.handle) == untracked, "Checkpoint failed to atomically adopt supplied current state");
        state = run("commit", {document.handle,4,renamed(original,"Next"),false});
        state = run("undo", {document.handle,state.at("revision")});
        require(state.at("serialized") == untracked, "Checkpoint lost untracked current state");
    });
    test("History limit evicts the oldest state and permits disabled history", [] {
        Document document(original,3);
        uint64_t revision = 1;
        for (int i=1;i<=5;++i) revision = run("commit", {document.handle,revision,renamed(original,std::to_string(i))}).at("revision").get<uint64_t>();
        require(info(document.handle).at("undo_count") == 3, "History exceeded configured limit");
        Json state;
        for (int i=0;i<3;++i) { state = run("undo", {document.handle,revision}); revision = state.at("revision").get<uint64_t>(); }
        require(Json::parse(state.at("serialized").get<std::string>()).at("name") == "2", "History evicted an incorrect snapshot");
        const auto no_history = run("undo", {document.handle,revision});
        require(no_history.at("revision") == revision && no_history.at("changed") == false, "Exhausted undo advanced revision");
        Document disabled(original,0);
        auto zero = run("commit", {disabled.handle,1,renamed(original,"No history")});
        require(zero.at("undo_count") == 0, "History limit zero retained snapshots");
        require(invoke("create", {original,61}).at("ok") == false, "History limit above 60 was accepted");
        require(invoke("create", {original,-1}).at("ok") == false, "Negative history limit was accepted");
    });
    test("Stale and invalid commits preserve revision, complete data and both histories", [] {
        Document document;
        run("commit", {document.handle,1,renamed(original,"Previous")});
        run("undo", {document.handle,2});
        const auto before = read(document.handle);
        require(invoke("commit", {document.handle,1,renamed(original,"Stale")}).at("ok") == false, "Stale commit accepted");
        std::vector<std::string> invalid{"{broken","[]"};
        auto data = Json::parse(original); data["version"] = 999; invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"][1]["id"] = "A"; invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"][1]["material"] = "missing"; invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"][1]["t"] = -1; invalid.push_back(data.dump());
        data = Json::parse(original); data["hardware"][0]["owner_id"] = "missing"; invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"][0]["operations"][0]["owner_id"] = "missing"; invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"][0]["assembly"]["anchors"] = Json::array({"missing"}); invalid.push_back(data.dump());
        data = Json::parse(original); data["parts"].erase(1); invalid.push_back(data.dump());
        for (const auto& text:invalid) {
            require(invoke("commit", {document.handle,3,text}).at("ok") == false, "Malformed or broken-reference document accepted");
            const auto after = read(document.handle);
            for (const char* key:{"serialized","revision","undo_count","redo_count","history_bytes"})
                require(after.at(key) == before.at(key), std::string("Rejected commit mutated ")+key);
        }
    });
    test("Malformed checkpoint validates before recording history", [] {
        Document document;
        const auto before = read(document.handle);
        require(invoke("checkpoint", {document.handle,1,"{broken"}).at("ok") == false, "Malformed checkpoint accepted");
        const auto after = read(document.handle);
        require(before.at("serialized") == after.at("serialized") && before.at("revision") == after.at("revision") && after.at("undo_count") == 0,
                "Malformed checkpoint partially changed state or history");
    });
    test("Native document save holds revision check, file replacement and publication atomically", [] {
        Document document;
        const auto folder = report_directory / (L"Dokument_Łódź_" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        const auto path = folder / L"Żółć — projekt szafki.mebel";
        const auto path_text = utf8(path.wstring());
        {
            std::ofstream output(path,std::ios::binary); output << original;
            require(bool(output),"Cannot prepare saved project");
        }
        auto geometry = Json::parse(original); geometry["parts"][0]["u"] = 825;
        const auto current = renamed(geometry.dump(),"Current model");
        const auto saved = renamed(current,"Nowy zapis Unicode");
        run("commit", {document.handle,1,current});
        const auto before = read(document.handle);
        auto unchanged = [&] {
            const auto after = read(document.handle);
            for (const char* key:{"serialized","revision","undo_count","redo_count","history_bytes"})
                require(before.at(key) == after.at(key), std::string("Rejected save changed ")+key);
            require(read_file(path) == original,"Rejected save overwrote the existing file");
        };
        require(invoke("save", {document.handle,1,path_text,saved}).at("ok") == false,"Stale save was accepted");
        unchanged();
        require(invoke("save", {document.handle,2,path_text,"{broken"}).at("ok") == false,"Malformed save was accepted");
        unchanged();
        HANDLE held = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(held != INVALID_HANDLE_VALUE,"Cannot lock original file for failure test");
        Json failed;
        try { failed = invoke("save", {document.handle,2,path_text,saved}); }
        catch (...) { CloseHandle(held); throw; }
        CloseHandle(held);
        require(failed.at("ok") == false,"Save to a replacement-locked destination unexpectedly succeeded");
        unchanged();
        const auto committed = run("save", {document.handle,2,path_text,saved});
        require(committed.at("revision") == 3 && committed.at("undo_count") == before.at("undo_count"),
                "Valid save duplicated history or did not publish the updated document");
        require(serialized(document.handle) == saved && read_file(path) == saved,
                "Native Unicode save did not publish exactly the bytes written to disk");
        for (const auto& item:std::filesystem::directory_iterator(folder))
            require(item.path() == path,"Native save left a temporary file behind");
        require(run("save", {document.handle,3,path_text,saved}).at("revision") == 3,
                "Saving identical document bytes advanced revision");
        const auto undone = run("undo", {document.handle,3});
        require(undone.at("serialized") == original && undone.at("revision") == 4 && undone.at("redo_count") == 1,
                "Save-compatibility setup did not restore the old geometry");
        const auto save_as = renamed(original,"Zapis po cofnięciu");
        const auto after_undo_save = run("save", {document.handle,4,path_text,save_as});
        require(after_undo_save.at("revision") == 5 && after_undo_save.at("undo_count") == 0 && after_undo_save.at("redo_count") == 1,
                "Save after undo discarded or added history instead of preserving the legacy redo branch");
        require(read_file(path) == save_as && serialized(document.handle) == save_as,
                "Save after undo did not publish matching file and document bytes");
        const auto redone = run("redo", {document.handle,5});
        require(redone.at("serialized") == saved && redone.at("revision") == 6 && redone.at("undo_count") == 1,
                "Redo after save did not restore the previous edited geometry");
        require(Json::parse(redone.at("serialized").get<std::string>())["parts"][0]["u"] == 825,
                "Redo restored a title but lost the previous geometry");
        const auto restored_save = run("undo", {document.handle,6});
        require(restored_save.at("serialized") == save_as && read_file(path) == save_as,
                "Redo failed to retain the save-created current snapshot for undo");
        std::filesystem::remove(path); std::filesystem::remove(folder);
    });
    test("Serialized snapshot size and nesting limits reject without allocating a document", [] {
        auto data = Json::parse(original);
        data["name"] = std::string(10000001,'x');
        require(invoke("create", {data.dump()}).at("ok") == false, "Snapshot above ten million bytes was accepted");
        Json nested = 0;
        for (int i=0;i<66;++i) nested = Json{{"child",nested}};
        data = Json::parse(original); data["parts"][0]["assembly"]["extension"] = nested;
        require(invoke("create", {data.dump()}).at("ok") == false, "Overly nested serialized document was accepted");
    });
    test("Concurrent same-revision writers yield exactly one commit", [] {
        Document document;
        constexpr int count = 12;
        std::barrier gate(count);
        std::vector<Json> responses(count);
        std::vector<std::thread> writers;
        for (int i=0;i<count;++i) writers.emplace_back([&,i] {
            gate.arrive_and_wait();
            try { responses[i] = invoke("commit", {document.handle,1,renamed(original,"Writer "+std::to_string(i))}); }
            catch (const std::exception& error) { responses[i] = Json{{"transport_error",error.what()}}; }
        });
        for (auto& writer:writers) writer.join();
        int committed = 0;
        for (const auto& response:responses) {
            require(response.contains("ok"), "Concurrency caused an ABI/transport failure");
            if (response.at("ok").get<bool>()) ++committed;
        }
        require(committed == 1 && info(document.handle).at("revision") == 2 && info(document.handle).at("undo_count") == 1,
                "Optimistic concurrency committed more than one writer");
    });
    test("Clone has isolated content history and lifetime", [] {
        Document document;
        run("commit", {document.handle,1,renamed(original,"Parent")});
        const auto cloned = run("clone", {document.handle});
        const auto handle = cloned.at("handle").get<uint64_t>();
        try {
            require(handle != document.handle && cloned.at("revision") == 1 && cloned.at("undo_count") == 0, "Clone reused identity or copied history");
            require(serialized(handle) == serialized(document.handle), "Clone did not preserve all metadata bytes");
            run("commit", {handle,1,renamed(original,"Clone")});
            require(Json::parse(serialized(document.handle)).at("name") == "Parent", "Clone mutation changed its parent");
            require(run("destroy", {document.handle}) == true, "Parent destroy failed");
            document.handle = 0;
            require(Json::parse(serialized(handle)).at("name") == "Clone", "Clone depends on destroyed parent");
        } catch (...) { run("destroy", {handle}); throw; }
        require(run("destroy", {handle}) == true && run("destroy", {handle}) == false, "Destroy was not idempotent");
        require(invoke("read", {handle}).at("ok") == false, "Destroyed handle remained readable");
        Document newer;
        require(newer.handle > handle, "Destroyed handle was reused");
    });
    test("Concurrent destruction and readers cannot retain an invalid document", [] {
        Document document;
        const auto handle = document.handle;
        std::atomic<int> transport_errors{0};
        std::barrier gate(9);
        std::vector<std::thread> readers;
        for (int i=0;i<8;++i) readers.emplace_back([&] {
            gate.arrive_and_wait();
            for (int j=0;j<40;++j) {
                try {
                    const auto response = invoke("read", {handle});
                    if (!response.contains("ok")) ++transport_errors;
                    else if (response.at("ok").get<bool>() && response.at("result").at("serialized") != original) ++transport_errors;
                } catch (...) { ++transport_errors; }
            }
        });
        gate.arrive_and_wait();
        bool destroyed = false;
        try { destroyed = run("destroy", {handle}) == true; }
        catch (...) { ++transport_errors; }
        if (destroyed) document.handle = 0;
        for (auto& reader:readers) reader.join();
        require(destroyed && transport_errors == 0 && invoke("read", {handle}).at("ok") == false, "Lifetime race corrupted state or ABI");
    });
    test("Document registry limit rejects the 257th model and releases capacity", [] {
        std::vector<uint64_t> handles;
        bool created_all = true, rejected_overflow = false;
        try {
            for (int i=0;i<256;++i) {
                const auto result = invoke("create", {original,0});
                if (!result.at("ok").get<bool>()) { created_all = false; break; }
                handles.push_back(result.at("result").at("handle").get<uint64_t>());
            }
            const auto extra = invoke("create", {original,0});
            rejected_overflow = extra.at("ok") == false;
            if (!rejected_overflow) handles.push_back(extra.at("result").at("handle").get<uint64_t>());
        } catch (...) {
            for (const auto handle:handles) run("destroy", {handle});
            throw;
        }
        for (const auto handle:handles) require(run("destroy", {handle}) == true, "Registry capacity cleanup failed");
        require(created_all && rejected_overflow, "Registry limit is inconsistent with the 256-document contract");
        Document available_again;
        require(serialized(available_again.handle) == original, "Destroy did not restore registry capacity");
    });
    test("Native requests retain strict argument and opaque-handle validation", [] {
        Document document;
        for (const auto& bad:Json::array({0,-1,true,1.5,"1",nullptr}))
            require(invoke("read", {bad}).at("ok") == false, "Invalid document handle accepted");
        require(invoke("read", {document.handle,1}).at("ok") == false, "Excess positional arguments accepted");
        require(invoke("commit", {document.handle,true,original}).at("ok") == false, "Boolean revision accepted");
        require(invoke("commit", {document.handle,1,original,"true"}).at("ok") == false, "Nonboolean record_history accepted");
    });
    test("Native document ownership releases every response buffer", [] {
        const auto response = invoke("info", Json::array(), "native");
        require(response.at("ok") == true && response.at("result").at("outstanding_buffers") == 0, "Response buffers leaked across document operations");
        require(response.at("result").at("document_active_models") == 0, "Closed document handles leaked from the native registry");
    });

    unsigned failed = 0;
    for (const auto& record:results) if (!record.at("ok").get<bool>()) ++failed;
    Json report{{"ok",failed==0},{"checks",results},{"count",results.size()},{"failed",failed},{"process_uses_python",false},{"gui",false},{"concurrent_writers",12}};
    const std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(output,std::ios::binary); stream << report.dump(2); stream.close();
    std::cout << report.dump() << '\n';
    FreeLibrary(library);
    return failed ? 1 : 0;
}
