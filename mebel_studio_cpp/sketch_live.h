// Synchronized dimension controls and debounced 3D sketch preview.
//
// Różnica względem Pythona: install/sync/apply_sizes/schedule/preview przyjmują
// Sketch*, a atrybuty dopisywane dynamicznie (size_vars, live_canvas, ...) są
// składowymi klasy Sketch.
#pragma once

namespace sketch { class Sketch; }

namespace sketch_live {

void install(sketch::Sketch* sketch);
void sync(sketch::Sketch* s);
void apply_sizes(sketch::Sketch* s);
void schedule(sketch::Sketch* s);
void preview(sketch::Sketch* s, bool force_cpu = false);

}  // namespace sketch_live
