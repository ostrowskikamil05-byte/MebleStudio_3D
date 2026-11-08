#include "core_api.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mebel::model {
namespace {
constexpr size_t max_document_bytes=10000000,max_parts=2000,max_documents=256,max_history=60;
constexpr const char* validation_scope="structure_scalars_references; exact CAD requires editor preflight";

void require_object(const Json& input,const char* context) {
    if(!input.is_object()) throw std::invalid_argument(std::string(context)+": wymagany obiekt JSON.");
}
void require_array(const Json& input,const char* context) {
    if(!input.is_array()) throw std::invalid_argument(std::string(context)+": wymagana lista JSON.");
}
const Json& member(const Json& input,const char* key,const Json& fallback) {
    const auto found=input.find(key);return found==input.end()?fallback:*found;
}
const Json& optional_array(const Json& input,const char* key) {
    static const Json empty=Json::array();const auto& result=member(input,key,empty);
    require_array(result,key);return result;
}
const Json& optional_object(const Json& input,const char* key) {
    static const Json empty=Json::object();const auto& result=member(input,key,empty);
    require_object(result,key);return result;
}
std::string identifier(const Json& input) {
    if(!input.is_string()) throw std::invalid_argument("Identyfikator dokumentu musi być tekstem.");
    const auto value=input.get<std::string>();
    if(value.empty()||value.find('\0')!=std::string::npos) throw std::invalid_argument("Nieprawidłowy identyfikator dokumentu.");
    return value;
}
void nonblank(const Json& input,const char* context) {
    if(!input.is_string()) throw std::invalid_argument(std::string(context)+": wymagany tekst.");
    const auto& value=input.get_ref<const std::string&>();
    if(value.empty()||std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isspace(c)!=0;}))
        throw std::invalid_argument(std::string(context)+": wymagany niepusty tekst.");
}
double numeric(const Json& input,const char* key,double fallback,double low,double high) {
    const auto found=input.find(key);
    return found==input.end()?fallback:document_finite(*found,low,high);
}
void optional_bool(const Json& input,const char* key) {
    const auto found=input.find(key);
    if(found!=input.end()&&!found->is_boolean()) throw std::invalid_argument(std::string(key)+": wymagana wartość logiczna.");
}
bool truth(const Json& input) {
    if(input.is_null())return false;
    if(input.is_boolean())return input.get<bool>();
    if(input.is_number())return input.get<double>()!=0;
    return !input.empty();
}
void reference(const Json& input,const char* key,const std::unordered_set<std::string>& part_ids) {
    const auto value=input.find(key);
    if(value!=input.end()&&truth(*value)&&!part_ids.contains(identifier(*value)))
        throw std::invalid_argument(std::string(key)+": powiązanie wskazuje usunięty element.");
}
uint64_t integer(const Json& value,bool allow_zero=false) {
    if(value.is_number_unsigned()) {
        const auto result=value.get<uint64_t>();if(result||allow_zero)return result;
    } else if(value.is_number_integer()) {
        const auto result=value.get<int64_t>();
        if(result>0||(allow_zero&&result==0))return static_cast<uint64_t>(result);
    }
    throw std::invalid_argument("Uchwyt, wersja i limit historii wymagają prawidłowej liczby całkowitej.");
}

struct Snapshot {
    std::string serialized;
    size_t parts_count=0,materials_count=0;
};
using SnapshotPtr=std::shared_ptr<const Snapshot>;

// Validate without reserializing: editor/CAD metadata, object ordering and
// numeric spelling belong to the document and never to a lossy projection.
SnapshotPtr parse_snapshot(const Json& input) {
    if(!input.is_string()) throw std::invalid_argument("Dokument wymaga pełnego zapisu JSON jako tekstu UTF-8.");
    const auto& text=input.get_ref<const std::string&>();
    if(text.size()>max_document_bytes) throw std::invalid_argument("Plik projektu jest zbyt duży.");
    auto depth_guard=[](int depth,Json::parse_event_t,Json&) {
        if(depth>64)throw std::invalid_argument("Projekt jest zbyt zagnieżdżony.");
        return true;
    };
    const Json data=Json::parse(text,depth_guard);
    require_object(data,"Dokument");
    if(!data.contains("version")||!data.at("version").is_number_integer()||data.at("version")!=1)
        throw std::invalid_argument("Nieobsługiwana wersja projektu.");
    const auto& materials=data.at("materials");const auto& parts=data.at("parts");
    require_array(materials,"Materiały");require_array(parts,"Elementy");
    if(materials.empty()||parts.size()>max_parts)
        throw std::invalid_argument("Projekt wymaga materiałów i może mieć najwyżej 2000 elementów.");
    std::unordered_set<std::string> material_ids,part_ids;
    for(const auto& material:materials) {
        require_object(material,"Materiał");
        if(!material_ids.insert(identifier(material.at("id"))).second)
            throw std::invalid_argument("Powtarzające się identyfikatory materiałów.");
        if(material.contains("name"))nonblank(material.at("name"),"Nazwa materiału");
        numeric(material,"price_m2",95,0,1e6);numeric(material,"price_m3",0,0,1e9);
        numeric(material,"sheet_u",2800,1,10000);numeric(material,"sheet_v",2070,1,10000);
    }
    for(const auto& part:parts) {
        require_object(part,"Element");
        if(!part_ids.insert(identifier(part.at("id"))).second)
            throw std::invalid_argument("Powtarzające się identyfikatory elementów.");
    }
    const double edge_t=numeric(data,"edge_t",1,0,100000);
    for(const char* key:{"kerf","margin","edge_price","labor","markup"})numeric(data,key,0,0,100000);
    for(const auto& part:parts) {
        if(!material_ids.contains(identifier(part.at("material"))))
            throw std::invalid_argument("Element wskazuje nieznany materiał.");
        if(part.contains("name"))nonblank(part.at("name"),"Nazwa elementu");
        const double u=numeric(part,"u",600,.1,10000),v=numeric(part,"v",400,.1,10000),t=numeric(part,"t",18,.1,10000);
        for(const char* key:{"x","y","z","rx","ry","rz"})numeric(part,key,0,-100000,100000);
        if(part.contains("plane")&&part.at("plane")!="XY"&&part.at("plane")!="XZ"&&part.at("plane")!="YZ")
            throw std::invalid_argument("Nieprawidłowa płaszczyzna elementu.");
        static const Json default_edges=Json::array({false,false,false,false});
        const auto& edges=member(part,"edges",default_edges);require_array(edges,"Obrzeża");
        if(edges.size()!=4||std::any_of(edges.begin(),edges.end(),[](const Json& edge){return !edge.is_boolean();}))
            throw std::invalid_argument("Nieprawidłowe obrzeża.");
        const auto& contour=optional_array(part,"contour");
        if(!contour.empty()) {
            // Existing C++ polygon validation is independent of the CAD kernel.
            dispatch_core(Json{{"module","core"},{"function","validate_polygon"},{"args",Json::array({contour})}});
            if(std::any_of(edges.begin(),edges.end(),[](const Json& edge){return edge.get<bool>();}))
                throw std::invalid_argument("Dla konturu nieregularnego obrzeża należy wyłączyć.");
        } else if(u-edge_t*(int(edges[0].get<bool>())+int(edges[1].get<bool>()))<=0||
                  v-edge_t*(int(edges[2].get<bool>())+int(edges[3].get<bool>()))<=0)
            throw std::invalid_argument("Obrzeże jest grubsze od wymiaru elementu.");
        for(const auto& hole:optional_array(part,"holes")) {
            require_array(hole,"Otwór");if(hole.size()!=4)throw std::invalid_argument("Otwór wymaga czterech parametrów.");
            const double a=document_finite(hole[0],0,10000),b=document_finite(hole[1],0,10000),
                diameter=document_finite(hole[2],0,10000),depth=document_finite(hole[3],0,10000);
            if(diameter<=0||depth<=0||depth>t||a<diameter/2||a>u-diameter/2||b<diameter/2||b>v-diameter/2)
                throw std::invalid_argument("Otwór wykracza poza element lub jego grubość.");
        }
        for(const char* key:{"profile","decor","style_source","configurator_source","layout","motion"})optional_object(part,key);
        const auto& layout=optional_object(part,"layout");
        for(const char* key:{"x","y","z","yaw"})numeric(layout,key,0,-100000,100000);
        const auto& assembly=optional_object(part,"assembly");
        for(const auto& id:optional_array(assembly,"anchors"))
            if(!part_ids.contains(identifier(id)))throw std::invalid_argument("Powiązanie konstrukcyjne wskazuje usunięty element.");
        for(const auto& operation:optional_array(part,"operations")) {
            require_object(operation,"Wiercenie");reference(operation,"owner_id",part_ids);
        }
        for(const char* key:{"fillets","chamfers","machining"}) {
            const auto& values=optional_array(part,key);
            if(std::string(key)=="machining"&&values.size()>200)throw std::invalid_argument("Maksymalnie 200 operacji frezowania na element.");
            for(const auto& item:values)require_object(item,key);
        }
        optional_bool(part,"contour_edge");
    }
    for(const auto& item:optional_array(data,"hardware")) {
        require_object(item,"Okucie");
        if(item.contains("name"))nonblank(item.at("name"),"Nazwa okucia");
        document_finite(item.at("qty"),0,100000);document_finite(item.at("price"),0,100000);
        reference(item,"owner_id",part_ids);
    }
    const auto& scene=optional_object(data,"scene");
    for(const auto& item:optional_array(scene,"objects"))require_object(item,"Obiekt sceny");
    auto result=std::make_shared<Snapshot>();
    result->serialized=text;result->parts_count=parts.size();result->materials_count=materials.size();return result;
}

struct State {
    uint64_t revision=1;
    SnapshotPtr current;
    std::vector<SnapshotPtr> undo,redo;
    void swap(State& other) noexcept {
        std::swap(revision,other.revision);current.swap(other.current);undo.swap(other.undo);redo.swap(other.redo);
    }
};
struct Document {
    std::mutex mutex;
    bool alive=true;
    size_t history_limit=max_history;
    State state;
};
struct Registry {
    std::mutex mutex;
    uint64_t next_handle=1;
    std::unordered_map<uint64_t,std::shared_ptr<Document>> documents;
};
Registry& registry() {static Registry value;return value;}
std::shared_ptr<Document> find_document(uint64_t handle,bool required=true) {
    auto& store=registry();std::lock_guard lock(store.mutex);const auto found=store.documents.find(handle);
    if(found!=store.documents.end())return found->second;
    if(required)throw std::invalid_argument("Nieznany lub zamknięty uchwyt dokumentu.");
    return nullptr;
}
void alive(const Document& document) {
    if(!document.alive)throw std::invalid_argument("Nieznany lub zamknięty uchwyt dokumentu.");
}
void expected_revision(const Document& document,uint64_t expected) {
    if(expected!=document.state.revision)throw std::invalid_argument("Nieaktualna wersja dokumentu; odczytaj bieżącą wersję przed zmianą.");
}
void advance(State& state) {
    if(state.revision==std::numeric_limits<uint64_t>::max())throw std::invalid_argument("Wyczerpano wersje dokumentu.");
    ++state.revision;
}
void push(std::vector<SnapshotPtr>& stack,const SnapshotPtr& snapshot,size_t limit) {
    if(!limit)return;
    stack.push_back(snapshot);
    if(stack.size()>limit)stack.erase(stack.begin(),stack.begin()+(stack.size()-limit));
}
Json describe(uint64_t handle,const State& state,size_t history_limit,bool with_snapshot=false) {
    std::unordered_set<const Snapshot*> unique;
    size_t history_bytes=0;
    auto count=[&](const SnapshotPtr& value){if(unique.insert(value.get()).second)history_bytes+=value->serialized.size();};
    count(state.current);for(const auto& value:state.undo)count(value);for(const auto& value:state.redo)count(value);
    Json result={{"handle",handle},{"revision",state.revision},{"history_limit",history_limit},
        {"undo_count",state.undo.size()},{"redo_count",state.redo.size()},
        {"can_undo",!state.undo.empty()},{"can_redo",!state.redo.empty()},
        {"parts_count",state.current->parts_count},{"materials_count",state.current->materials_count},
        {"byte_length",state.current->serialized.size()},{"history_bytes",history_bytes},{"validation_scope",validation_scope}};
    if(with_snapshot)result["serialized"]=state.current->serialized;
    return result;
}
Json register_document(SnapshotPtr snapshot,size_t history_limit) {
    auto document=std::make_shared<Document>();document->history_limit=history_limit;document->state.current=std::move(snapshot);
    auto& store=registry();std::lock_guard lock(store.mutex);
    if(store.documents.size()>=max_documents)throw std::invalid_argument("Można otworzyć najwyżej 256 dokumentów.");
    if(store.next_handle==std::numeric_limits<uint64_t>::max())throw std::invalid_argument("Wyczerpano uchwyty dokumentów.");
    const auto handle=store.next_handle;
    auto result=describe(handle,document->state,history_limit);
    store.documents.emplace(handle,std::move(document));++store.next_handle;return result;
}
Json publish(uint64_t handle,Document& document,State next,bool changed,bool history_changed,bool with_snapshot=false) {
    if(changed||history_changed)advance(next);
    // Prepare allocations and the response before the sole noexcept commit.
    Json result=describe(handle,next,document.history_limit,with_snapshot);
    result["changed"]=changed;result["history_changed"]=history_changed;
    document.state.swap(next);return result;
}
Json commit(uint64_t handle,Document& document,const Json& serialized,bool record_history,bool checkpoint) {
    auto snapshot=parse_snapshot(serialized);
    const bool changed=snapshot->serialized!=document.state.current->serialized;
    if(!changed)snapshot=document.state.current;
    State next=document.state;
    bool history_changed=false;
    if(checkpoint) {
        next.current=snapshot;
        history_changed=document.history_limit>0||!next.redo.empty();
        push(next.undo,snapshot,document.history_limit);next.redo.clear();
    } else if(changed) {
        if(record_history)push(next.undo,next.current,document.history_limit);
        history_changed=(record_history&&document.history_limit>0)||!next.redo.empty();
        next.redo.clear();next.current=std::move(snapshot);
    }
    return publish(handle,document,std::move(next),changed,history_changed);
}
Json navigate(uint64_t handle,Document& document,bool undo) {
    State next=document.state;auto& source=undo?next.undo:next.redo;auto& target=undo?next.redo:next.undo;
    if(source.empty())return publish(handle,document,std::move(next),false,false,true);
    const auto snapshot=source.back();source.pop_back();
    const bool changed=snapshot->serialized!=next.current->serialized;
    push(target,next.current,document.history_limit);next.current=snapshot;
    return publish(handle,document,std::move(next),changed,true,true);
}
Json save(uint64_t handle,Document& document,const Json& path,const Json& serialized) {
    auto snapshot=parse_snapshot(serialized);
    const bool changed=snapshot->serialized!=document.state.current->serialized;
    State next=document.state;
    // Saving (including Save As renaming after Undo) is not a new edit branch.
    // The legacy editor retains both history stacks while writing the file.
    const bool history_changed=false;
    if(changed) {
        next.current=std::move(snapshot);advance(next);
    }
    // Prepare every allocation needed by the native state and response before
    // the atomic file replacement. Once the writer succeeds only noexcept
    // swaps remain, so stale/invalid/I/O-failed saves retain both prior states.
    Json result=describe(handle,next,document.history_limit);
    result["changed"]=changed;result["history_changed"]=history_changed;
    const Json request={{"module","project_io"},{"function","save"},
        {"args",Json::array({path,next.current->serialized})}};
    dispatch_services(request);
    document.state.swap(next);return result;
}
struct Arguments {
    const Json& args;const Json& kwargs;
    explicit Arguments(const Json& request):args(optional_array(request,"args")),kwargs(optional_object(request,"kwargs")) {}
    void validate(std::initializer_list<const char*> names) const {
        if(args.size()>names.size())throw std::invalid_argument("Za dużo argumentów dokumentu.");
        for(auto item=kwargs.begin();item!=kwargs.end();++item)
            if(std::none_of(names.begin(),names.end(),[&](const char* key){return item.key()==key;}))
                throw std::invalid_argument("Nieznany argument dokumentu: "+item.key());
        size_t index=0;
        for(const auto* name:names)if(index++<args.size()&&kwargs.contains(name))throw std::invalid_argument("Argument dokumentu podano dwukrotnie.");
    }
    const Json& at(size_t index,const char* key) const {
        if(index<args.size())return args[index];
        if(kwargs.contains(key))return kwargs.at(key);
        throw std::invalid_argument(std::string("Brakuje argumentu dokumentu: ")+key);
    }
    Json value(size_t index,const char* key,Json fallback) const {
        if(index<args.size())return args[index];
        if(kwargs.contains(key))return kwargs.at(key);
        return fallback;
    }
};
} // namespace

uint64_t document_active_count() noexcept {
    try{auto& store=registry();std::lock_guard lock(store.mutex);return store.documents.size();}catch(...){return 0;}
}
std::optional<Json> dispatch_document(const Json& request) {
    if(request.value("module",std::string())!="document")return std::nullopt;
    const auto function=request.value("function",std::string());const Arguments args(request);
    if(function=="create") {
        args.validate({"serialized","history_limit"});const auto limit=integer(args.value(1,"history_limit",max_history),true);
        if(limit>max_history)throw std::invalid_argument("Limit historii musi mieścić się w zakresie 0–60.");
        return register_document(parse_snapshot(args.at(0,"serialized")),static_cast<size_t>(limit));
    }
    if(function!="read"&&function!="info"&&function!="commit"&&function!="checkpoint"&&function!="undo"&&function!="redo"&&function!="clone"&&function!="destroy"&&function!="save")return std::nullopt;
    if(function=="commit")args.validate({"handle","expected_revision","serialized","record_history"});
    else if(function=="save")args.validate({"handle","expected_revision","path","serialized"});
    else if(function=="checkpoint")args.validate({"handle","expected_revision","serialized_current"});
    else if(function=="undo"||function=="redo")args.validate({"handle","expected_revision"});
    else args.validate({"handle"});
    const auto handle=integer(args.at(0,"handle"));auto document=find_document(handle,function!="destroy");
    if(!document)return false;
    std::unique_lock lock(document->mutex);
    if(function=="destroy") {
        if(!document->alive)return false;
        auto& store=registry();std::lock_guard registry_lock(store.mutex);
        store.documents.erase(handle);document->alive=false;document->state=State{};return true;
    }
    alive(*document);
    if(function=="read"||function=="info")return describe(handle,document->state,document->history_limit,function=="read");
    if(function=="clone") {
        const auto snapshot=document->state.current;const auto limit=document->history_limit;
        lock.unlock();return register_document(snapshot,limit);
    }
    expected_revision(*document,integer(args.at(1,"expected_revision")));
    if(function=="save")return save(handle,*document,args.at(2,"path"),args.at(3,"serialized"));
    if(function=="undo"||function=="redo")return navigate(handle,*document,function=="undo");
    if(function=="checkpoint")return commit(handle,*document,args.at(2,"serialized_current"),false,true);
    const auto record=args.value(3,"record_history",true);
    if(!record.is_boolean())throw std::invalid_argument("record_history wymaga wartości logicznej.");
    return commit(handle,*document,args.at(2,"serialized"),record.get<bool>(),false);
}
} // namespace mebel::model
