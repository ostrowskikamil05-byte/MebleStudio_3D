// Fast room, shared worktop and corner controls in the existing workspace.
#pragma once

class App;

namespace form_dialog { class FormContent; using Form = FormContent; }

namespace workspace_controls {

// Okno konstrukcji narożnika (formularz). W programie zastąpione zakładką Narożnik (app.corner_dialog).
form_dialog::Form* corner_dialog(App* app);

// Okno wspólnego blatu; ValueError, gdy brak szafek pod blat.
form_dialog::Form* shared_worktop_dialog(App* app);

void install(App* app);

}  // namespace workspace_controls
