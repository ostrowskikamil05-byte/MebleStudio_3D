// Projekt z opisu zdaniem: polski opis („3 szafki dolne po 60 cm, dwie wiszące 80 cm, front biały, uchwyty 128”)
// zamieniany jest na konfiguracje szybkiego kreatora mebli. Parser jest regułowy (bez internetu i bez modelu językowego),
// zawsze pokazuje, co zrozumiał i czego nie zrozumiał, a tworzenie mebli to osobny, jawny krok.
//
// Obsługiwane m.in.:
//   typ mebla    — szafka dolna / wisząca / pod zlew / pod umywalkę / pod piekarnik, słupek, cargo, kosz na pranie, szafa, komoda,
//                  regał, witryna, RTV, szafka nocna, kontenerek, szafka na buty, wyspa …
//   ilość        — „4 szafki”, „trzy szafki”, „2 x szafka”
//   wymiary      — „80 cm”, „60x72x56”, „szerokość 90, wysokość 2,1 m”, „po 60 cm” (szerokość każdej)
//   wyposażenie  — drzwi, szuflady (na dole / na górze), półki, komory, poziomy cargo, nogi / cokół, blat, plecy, LED
//   okucia       — uchwyt 128 mm / gałka / bez uchwytów / TIP-ON / frezowany, kolor uchwytu
//   wygląd       — materiał korpusu i frontu (biały, grafit, dąb, MDF, sklejka) oraz styl frontu (ramowy, frezowany)
//   kuchnia      — „kuchnia 3 m” bez wyliczenia mebli buduje typowy układ (zlew + szafki dolne + wiszące)
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace text_design {

using json = core::json;

struct Item {
    long long template_index = 0;
    std::string label;          // nazwa wzorca, np. „Szafka dolna”
    std::string source;         // fragment opisu, z którego powstała pozycja
    int count = 1;
    bool wall = false;          // mebel wiszący (rząd górny)
    json config;                // pełna, znormalizowana konfiguracja kreatora (pusta, gdy error niepusty)
    std::string description;    // „600 × 720 × 560 mm · 1 drzwi · 1 półka · front: Biały mat”
    std::string error;          // dlaczego nie da się utworzyć (niewspierany typ, wymiary poza zakresem …)
};

struct Result {
    std::vector<Item> items;
    std::vector<std::string> ignored;  // słowa i liczby, których parser nie umiał przypisać
    bool kitchen_layout = false;       // pozycje wygenerowane z „kuchnia N m”

    int furniture_count() const;       // suma count pozycji bez błędu
    bool ok() const { return furniture_count() > 0; }
};

// Rozbiera opis na pozycje. Nigdy nie zmienia projektu.
Result parse(const std::string& text, const core::Project& project);

// Linie podsumowania dla użytkownika („Rozumiem: …”, „Pominięto: …”).
std::string summary(const Result& result);

// Tworzy meble: rząd podłogowy od prawej krawędzi istniejącego projektu, meble wiszące na wysokości 1400 mm z tyłem w jednej linii.
// Zwraca nowy projekt i grupy dodanych mebli (w kolejności opisu).
std::pair<core::ProjectPtr, std::vector<std::string>> build(const core::Project& project, const Result& result);

// Pomocnicze (testy): zamiana na małe litery bez polskich znaków.
std::string fold(const std::string& text);

}  // namespace text_design
