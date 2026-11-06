# stubs/ — jedyna brakująca warstwa: nakładka na Tcl/Tk

Wszystkie 71 modułów z `app/*.py` są przetłumaczone na C++20 (`.h` + `.cpp` w katalogu głównym).
W tym folderze zostały tylko dwa nagłówki, które opisują interfejs cienkiej nakładki
na Tcl/Tk 8.6 używany przez przetłumaczone moduły UI:

* `tk.h`  — `tk::Tk`, `Widget`, `Canvas`, `Text`, `Menu`, `StringVar`…, okna dialogowe
* `ttk.h` — `ttk::Button`, `Treeview`, `Notebook`, `Panedwindow`, `Style`…

Ich implementacja (`tk.cpp`, `ttk.cpp`) powstaje na Tcl/Tk C API i jest następnym etapem.
Do tego czasu `Value` w `tk.h` przyjmuje dowolny typ (tylko kontrola kompilacji).

Kompilacja kontrolna jednego pliku:

    g++ -std=c++20 -Wall -Wextra -ffp-contract=off -iquote .. -iquote . -c ../<plik>.cpp
