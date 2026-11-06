# Rozwiązania CAD/CAM przydatne w MebelStudio3D

Sprawdzone 3 października 2026 na stronach producentów i w dokumentacji projektów. To analiza funkcji i kierunku rozwoju, a nie potwierdzenie pełnej zgodności naszej aplikacji z wymienionymi systemami. Zakres komercyjnych programów może zależeć od wybranych modułów.

| System | Udokumentowane rozwiązanie | Co z tego wynika dla naszego programu |
|---|---|---|
| [Fusion — Press Pull](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/GUID-02F9ADA3-7556-42A9-8AD1-552728D537AB.htm) | Jedno narzędzie reaguje na profil, krawędź lub powierzchnię; uchwyty i wartości liczbowe sterują zmianą geometrii. | Wybór części na scenie, przeciąganie uchwytów i dokładna zmiana w mm. Nie powinno się wymagać przepisywania całej części w formularzu. |
| [FreeCAD — Sketcher](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/Sketcher_Workbench.md) | Więzy długości, poziomu, pionu, równości i styczności; przycinanie, podział, geometria odniesienia i zaokrąglenia. | Następny etap szkicu powinien dodać przyciąganie do istniejących punktów, linię odniesienia i wymiary sterujące. Obecne napisy z wymiarami nie są solverem więzów. |
| [SketchUp — komponenty](https://help.sketchup.com/en/sketchup/components) | Oddzielne definicje i instancje komponentów, komponenty konfigurowalne oraz narzędzia pozycjonowania. | Biblioteka własnych mebli i okuć, wielokrotne wstawianie oraz rozdzielenie edycji egzemplarza od wspólnego szablonu. |
| [SWOOD — złącza i obrzeża](https://swood.eficad.com/blog/whats-new-in-swood-2022/) | Rozpoznawanie powierzchni kontaktu płyt, reguły ilości i rozstawu złączy oraz uwzględnianie obrzeży przy ich rozmieszczaniu. | Reguły powinny uwzględniać rzeczywisty kontakt części, grubość obrzeża i naddatki; same współrzędne nominalne są niewystarczające. |
| [imos iX CAD](https://www.imos3d.com/en/products/design-order/ix-cad-1/) | Budowanie części z rzutów 2D, interaktywne pozycjonowanie w 3D, punkty przyciągania, reguły okuć, powiązana obróbka sąsiadów oraz dokumentacja części. | Najbliższy kierunek funkcjonalny: rysunek → funkcja elementu → sąsiedzi → okucia → aktualizacja danych produkcyjnych. Przydatne są też przekroje, etykiety i QR. |
| [Cabinet Vision](https://hexagon.com/products/cabinet-vision) | Parametryczne zespoły reagujące na wymiary i specyfikację, formatki, kosztorysy oraz moduły optymalizacji, wizualizacji i CNC. | Konfiguracja całego mebla musi zachowywać grubości i luzy, a zmiana wymiaru powinna przeliczać części i raporty. |
| [TopSolid’Wood / WoodCam](https://www.topsolid.com/en/products/topsolidwood) | Biblioteki komponentów z obróbką, pozycjonowanie, dokumentacja, wizualizacje i powiązanie geometrii z przygotowaniem CAM. | W przyszłości okucie powinno mieć własny model, recepturę otworów i reguły montażu. Program CNC powinien być wynikiem technologii i postprocesora, nie tylko eksportu konturu. |
| [Meblarz 3D — pomoc](https://meblarz3d.eu/pomoc/) | Własne kontury detali, grupowanie produktu, edycja parametrów i ustawienia eksportu dla konkretnego postprocesora. | Zachować prostą ścieżkę pracy dla stolarza i powiązanie mebla z listą części. |

## Wdrożone w wersji 0.5

- Pokazywanie i chowanie wymiarów także w widoku rozstrzelonym. Opisy odnoszą się do wymiarów części, nie do odległości rozstrzelenia.
- „Zapisz i rysuj następny” oraz „Nowy element” w szkicowniku. Każdy zapis tworzy osobną część z własnym identyfikatorem.
- Uchwyty do zmiany ścian U/V/grubości w 3D, z zatwierdzeniem po puszczeniu myszy. Zmiany można cofać.
- Dokładne przesunięcie wybranej ściany w mm oraz zmiana wymiarów całego korpusu z zachowaniem grubości części.
- Osobne promienie dla krawędzi bryły: górnych, dolnych i narożnych przez grubość. Prostokątny element ma 12 wybieralnych kantów.
- Opcjonalny silnik [CadQuery / OpenCascade](https://cadquery.readthedocs.io/en/stable/quickstart.html) do rzeczywistych zaokrągleń i eksportu brył STEP z wierceniami.

## Funkcje najbardziej warte dalszej rozbudowy

| Kolejność | Funkcja | Korzyść | Stan |
|---|---|---|---|
| 1 | Przyciąganie: koniec, środek, przecięcie i powierzchnia sąsiada | Łatwe rysowanie bez ręcznego liczenia współrzędnych | Plan; obecnie przyciąganie do siatki i Orto |
| 2 | Więzy i formuły, np. szerokość półki = światło korpusu − luzy | Konstrukcja przelicza się bez ręcznych poprawek | Częściowo reguły montażu; brak pełnego solvera |
| 3 | Reguły kołków, konfirmatów, mimośrodów i połączeń drewna | Kompletniejsza konstrukcja i obróbka | Plan |
| 4 | Katalogi konkretnych okuć z modelami i parametrami | Wiercenia odpowiadają zakupionemu produktowi | Plan; obecnie wzorce przykładowe |
| 5 | Rowki, kieszenie, fazy i otwory krawędziowe | Przygotowanie realnej obróbki płyt i drewna | Plan; nowy silnik stanowi bazę |
| 6 | Przekrój, ukrywanie wybranych części i kontrola ruchu frontów | Łatwe sprawdzanie wnętrza i montażu | Częściowo: widoczność i rozstrzelenie; pozostałe w planie |
| 7 | Etykiety części i QR z identyfikatorem projektu | Mniej pomyłek podczas cięcia, CNC i montażu | Plan |
| 8 | Materiały z teksturami, światło i render dla klienta | Czytelna prezentacja wyrobu | Plan; obecnie podgląd techniczny |
| 9 | Nesting, narzędzia, naddatki, symulacja i postprocesor | Spójny proces CAD → CAM → CNC | Plan; obecnie rozkrój prostokątnych półfabrykatów i eksporty dokumentacji |

Powyższa kolejność jest moją rekomendacją projektową wynikającą z porównania dokumentacji i obecnego stanu aplikacji. Funkcji oznaczonych „Plan” nie należy traktować jako już dostępnych.

## Granice nowej edycji

Zmiana całego mebla obsługuje korpusy w osiach, a nie dowolne zespoły z obróconych części. Grubości, promienie i parametry prowadnic są zachowywane. Ręczne otwory pozostają w lokalnych współrzędnych; po zmianie wymiarów mogą wymagać korekty. Reguły powiązanych drzwi, półek i szuflad są przeliczane.

Zaokrąglenia wymagają modułu CAD. STEP zawiera dokładną geometrię, lecz nie ścieżki narzędzia ani program maszyny. JSON/CSV zawierają promienie jako parametry obróbki. Eksport DXF zaokrąglonej bryły jest blokowany, aby nie wydać ostrego nominalnego obrysu jako gotowej geometrii zaokrąglenia. Formatki i koszt surowca nadal bazują na nominalnym półfabrykacie.
