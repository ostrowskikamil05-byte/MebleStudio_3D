// C++ owns committed editor documents and history; editable drafts are copies.
//
// Every candidate still passes the complete existing validation before it
// crosses the transaction boundary. No history snapshots live in the app.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "core.h"

namespace native_document {

using json = core::json;

// Wspólny interfejs stosów historii używanych przez App (w Pythonie: list
// albo NativeHistoryView — truth/len/clear/append/pop/del[:-n]).
class HistoryStack {
public:
    virtual ~HistoryStack() = default;
    virtual void append(const json& snapshot) = 0;
    virtual json pop() = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    bool empty() const { return size() == 0; }
    virtual void keep_last(size_t count) = 0;  // del stack[:-count]
};

// Zwykła lista Pythona.
class ListHistory : public HistoryStack {
public:
    void append(const json& snapshot) override;
    json pop() override;
    void clear() override;
    size_t size() const override;
    void keep_last(size_t count) override;

private:
    std::vector<json> items_;
};

std::string serialize(const core::Project& project);
inline std::string serialize(const core::ProjectPtr& project) { return serialize(*project); }

// One native authoritative document with revision-checked atomic history.
class NativeDocument {
public:
    explicit NativeDocument(const core::ProjectPtr& project, int history_limit = 60);
    ~NativeDocument();
    NativeDocument(const NativeDocument&) = delete;
    NativeDocument& operator=(const NativeDocument&) = delete;

    bool dirty() const;  // @property
    bool matches(const core::ProjectPtr& project) const;
    json info();
    std::string read();
    core::ProjectPtr project();
    bool commit(const core::ProjectPtr& project, bool record_history = true);
    core::ProjectPtr undo();
    core::ProjectPtr redo();
    void reset(const core::ProjectPtr& project, bool saved = true);
    void clear_history();
    // Write validated native wire data, then accept the saved name/state.
    void save(const core::ProjectPtr& project, const std::string& path);
    void close();

    int history_limit;
    long long handle = 0;
    long long revision = 0;
    json _info = json::object();

private:
    void accept(const json& result, const std::string* serialized = nullptr);
    void require_open() const;
    core::ProjectPtr navigate(const std::string& direction);

    mutable std::recursive_mutex lock_;
    std::string serialized_;
    std::string saved_;
};

// Compatibility view for legacy truth/length/clear history consumers.
class NativeHistoryView : public HistoryStack {
public:
    NativeHistoryView(NativeDocument* document, const std::string& direction) : document_(document), direction_(direction) {}
    void append(const json&) override;
    json pop() override;
    void clear() override;
    size_t size() const override;
    void keep_last(size_t) override;
    std::string repr() const;  // <C++ undo: 3>

private:
    NativeDocument* document_;
    std::string direction_;
};

std::unique_ptr<NativeDocument> create_if_available(const core::ProjectPtr& project);

}  // namespace native_document
