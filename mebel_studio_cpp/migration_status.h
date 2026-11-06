// Read-only diagnostics for the ongoing, explicitly staged C++ migration.
#pragma once
#include <functional>
#include "form_dialog.h"
#include "ttk.h"

class App;

namespace migration_status {

form_dialog::Form* open_status(App* app);

// d.refresh_cpp_status / d.cpp_status_table — atrybuty doklejane do okna w Pythonie.
std::function<void()> refresh_cpp_status(form_dialog::Form* dialog);
ttk::Treeview* cpp_status_table(form_dialog::Form* dialog);

}  // namespace migration_status
