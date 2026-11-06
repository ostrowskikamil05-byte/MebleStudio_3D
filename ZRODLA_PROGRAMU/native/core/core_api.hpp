#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <stdexcept>
#include <string>
#include <optional>
#include <cstdint>
namespace mebel::model {
using Json = nlohmann::json;
struct NativeException : std::runtime_error {
    std::string type;
    NativeException(std::string category, const std::string& message)
        : std::runtime_error(message), type(std::move(category)) {}
};
// Requests: {"module":str,"function":str,"args":[...],"kwargs":{...}}.
// Return std::nullopt when a dispatcher does not own the function.
std::optional<Json> dispatch_core(const Json& request);
std::optional<Json> dispatch_shapes(const Json& request);
std::optional<Json> dispatch_construction(const Json& request);
std::optional<Json> dispatch_services(const Json& request);
// Complete editable document ownership/history. Unlike the 0.16 production
// mirror, snapshots retain the original serialized UTF-8 project verbatim.
// Storage validation covers structure/scalars/references; exact CAD validation
// is still required in the editor preflight during this migration stage.
std::optional<Json> dispatch_document(const Json& request);
std::optional<Json> dispatch_layout(const Json& request);
std::optional<Json> dispatch_workspace_geometry(const Json& request);
std::optional<Json> dispatch_workspace_worktop(const Json& request);
uint64_t document_active_count() noexcept;
double document_finite(const Json& value, double low, double high);
Json dispatch(const Json& request);
}
