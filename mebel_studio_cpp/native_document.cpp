// C++ owns committed editor documents and history; editable drafts are copies.
#include "native_document.h"

#include "native_model.h"
#include "pyutil.h"

namespace native_document {

using core::Project;
using core::ProjectPtr;

namespace {

json call(const std::string& function, const json& args) { return native_model::call("document", function, args); }

void release(long long handle) {
    try {
        call("destroy", json::array({handle}));
    } catch (...) {
        // Process shutdown must not mask the original outcome.
    }
}

}  // namespace

std::string serialize(const Project& project) { return native_model::dumps_compact(project.data()); }

void ListHistory::append(const json& snapshot) { items_.push_back(snapshot); }

json ListHistory::pop() {
    if (items_.empty()) throw py::IndexError("pop from empty list");
    json last = std::move(items_.back());
    items_.pop_back();
    return last;
}

void ListHistory::clear() { items_.clear(); }

size_t ListHistory::size() const { return items_.size(); }

void ListHistory::keep_last(size_t count) {
    if (items_.size() > count) items_.erase(items_.begin(), items_.end() - (std::ptrdiff_t)count);
}

NativeDocument::NativeDocument(const ProjectPtr& project, int history_limit_) : history_limit(history_limit_) {
    json info = native_model::call("native", "info");
    if (py::get(info, "document_api_version") != 1)
        throw native_model::RuntimeError("Biblioteka C++ nie obsługuje dokumentu edytora. Użyj kompletnej nowej paczki programu.");
    project->validate();
    std::string serialized = serialize(*project);
    json result = call("create", json::array({serialized, history_limit}));
    accept(result, &serialized);
    saved_ = serialized;
}

NativeDocument::~NativeDocument() {
    if (handle) release(handle);
}

void NativeDocument::accept(const json& result, const std::string* serialized) {
    handle = result.at("handle").get<long long>();
    revision = result.at("revision").get<long long>();
    _info = result;
    _info.erase("serialized");
    if (serialized) serialized_ = *serialized;
}

void NativeDocument::require_open() const {
    if (!handle) throw native_model::RuntimeError("Dokument C++ został zamknięty.");
}

bool NativeDocument::dirty() const { return serialized_ != saved_; }

bool NativeDocument::matches(const ProjectPtr& project) const { return serialize(*project) == serialized_; }

json NativeDocument::info() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    json result = call("info", json::array({handle}));
    result["dirty"] = dirty();
    return result;
}

std::string NativeDocument::read() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    json result = call("read", json::array({handle}));
    std::string serialized = result.at("serialized").get<std::string>();
    accept(result, &serialized);
    return serialized_;
}

ProjectPtr NativeDocument::project() { return Project::from_data(json::parse(read())); }

bool NativeDocument::commit(const ProjectPtr& project, bool record_history) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    if (matches(project)) return false;
    project->validate();
    std::string serialized = serialize(*project);
    json result = call("commit", json::array({handle, revision, serialized, record_history}));
    accept(result, &serialized);
    return py::truthy(result.at("changed"));
}

ProjectPtr NativeDocument::navigate(const std::string& direction) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    json result = call(direction, json::array({handle, revision}));
    std::string serialized = result.at("serialized").get<std::string>();
    accept(result, &serialized);
    return py::truthy(result.at("changed")) ? Project::from_data(json::parse(serialized)) : nullptr;
}

ProjectPtr NativeDocument::undo() { return navigate("undo"); }

ProjectPtr NativeDocument::redo() { return navigate("redo"); }

void NativeDocument::reset(const ProjectPtr& project, bool saved) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    project->validate();
    std::string serialized = serialize(*project);
    json result = call("create", json::array({serialized, history_limit}));
    long long old_handle = handle;
    accept(result, &serialized);
    call("destroy", json::array({old_handle}));
    if (saved) saved_ = serialized;
}

void NativeDocument::clear_history() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    if (py::truthy(_info.at("undo_count")) || py::truthy(_info.at("redo_count"))) reset(project(), false);
}

void NativeDocument::save(const ProjectPtr& project, const std::string& path) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    require_open();
    project->validate();
    std::string serialized = serialize(*project);
    json result = call("save", json::array({handle, revision, py::path_str(std::filesystem::absolute(py::to_path(path))), serialized}));
    accept(result, &serialized);
    saved_ = serialized;
}

void NativeDocument::close() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (handle) {
        call("destroy", json::array({handle}));
        handle = 0;
    }
}

void NativeHistoryView::append(const json&) { throw std::logic_error("'NativeHistoryView' object has no attribute 'append'"); }

json NativeHistoryView::pop() { throw std::logic_error("'NativeHistoryView' object has no attribute 'pop'"); }

void NativeHistoryView::clear() { document_->clear_history(); }

size_t NativeHistoryView::size() const { return (size_t)document_->_info.at(direction_ + "_count").get<long long>(); }

void NativeHistoryView::keep_last(size_t) { throw std::logic_error("'NativeHistoryView' object does not support item deletion"); }

std::string NativeHistoryView::repr() const { return "<C++ " + direction_ + ": " + std::to_string(size()) + ">"; }

std::unique_ptr<NativeDocument> create_if_available(const ProjectPtr& project) {
    if (!native_model::available()) return nullptr;
    return std::make_unique<NativeDocument>(project);
}

}  // namespace native_document
