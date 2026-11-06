# Badanie funkcji meblarskich CAD/CAM — wydanie 0.6

Przegląd dokumentacji producentów wykonano 3 października 2026. Celem było wybranie praktycznych funkcji do istniejącej aplikacji. Poniżej rozróżniamy możliwości opisane przez producentów, rzeczywiste wdrożenie i dalszą pracę. Nie jest to deklaracja zgodności z komercyjnymi programami.

| Dokumentacja pierwotna | Przydatne rozwiązanie | Co wdrożono w MebelStudio3D |
|---|---|---|
| [Fusion: fazowanie](https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-REF-CHAMFER.htm) | Fazowanie jako obróbka krawędzi | Osobne fazy jednakowej odległości, 45°, na wybranych kantach. Dwie różne odległości i inne kąty nie są obsługiwane. |
| [SketchUp: odbicia, obrót i szyki](https://help.sketchup.com/en/sketchup/flipping-mirroring-rotating-and-arrays) | Szybkie powielanie geometrii | Liniowy szyk części z krokiem XYZ, lokalne odbicia U/V z otworami i frezowaniem; obrót konturu 90°. |
| [SketchUp: komponenty](https://help.sketchup.com/en/sketchup/components) | Ponowne używanie elementów | Kopie części i zapis całego mebla do własnej biblioteki .mebel. Kopie są niezależne, bez aktualizacji wszystkich instancji. |
| [imos iX CAD](https://www.imos3d.com/en/products/design-order/ix-cad-1/) | Punkty przyciągania, biblioteka, powiązania i dokumentacja produkcyjna | Przyciąganie do punktów i środków odcinków, kopiowanie powiązanego mebla, rysunki części i etykiety. Ogólne receptury okuć są z wcześniejszego wydania. |
| [SWOOD: shopfittings](https://swood.eficad.com/shopfittings-and-furnishings/) | Połączenie projektu i przygotowania produkcji | Eksport rzeczywistych brył STEP, danych operacji i etykiet. Ścieżki narzędzi, nesting CNC i postprocesory pozostają poza wydaniem. |
| [TopSolid Wood](https://www.topsolid.com/en/products/topsolidwood) | Narzędzia branżowe i przepływ CAD/CAM | Kierunek dalszej rozbudowy konstrukcji i danych materiałowych; bez deklaracji równoważności. |
| [CadQuery: API geometrii](https://cadquery.readthedocs.io/en/latest/classreference.html) | Operacje na bryłach, otwory podłużne i odsunięcie konturu | Rzeczywiste odejmowanie kieszeni/rowków/wycięć, sloty, offset zamkniętego wielokąta i STEP/STL. |
| [FreeCAD: Sketcher](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/Sketcher_Workbench.md) | Precyzyjny szkic z więzami | Ustawianie poziomu, pionu i długości odcinka jako edycja jednorazowa. Pełny solver więzów pozostaje do wdrożenia. |

## Funkcje dostępne

- Kontury prostokątne i wielokątne, siatka, Orto, wymiary w trakcie rysowania.
- Kolejne elementy w jednej sesji szkicu, rola konstrukcyjna i dopasowanie do mebla.
- Punkty i środki odcinków jako cele przyciągania; rzuty konturów części korpusu jako pomocniczy szkic.
- Odbicie/obrót konturu, odsuwanie jednego zamkniętego konturu, poziom/pion i długość odcinka.
- Sześć uchwytów zmiany wymiarów części, przeliczenie osiowego korpusu.
- Niezależne promienie i niezależne fazy 45°.
- Prostokątne i kołowe kieszenie, rowki, otwory podłużne i wycięcia przelotowe.
- Obrót frezowania w płaszczyźnie, obróbka +T/−T, edycja i usuwanie ręcznych frezowań.
- Wiercenia z sześciu stron oraz prostokątny szyk otworów z edytowalnym rozstawem.
- Liniowe szyki części i odbicia całych obrobionych części.
- Równo rozmieszczone nowe półki z podpórkami.
- Kopiowanie całych mebli i własna biblioteka, remapowanie identyfikatorów i powiązań.
- Izolacja części, przywrócenie widoczności, osobne przełączniki rozstrzelenia i wymiarów.
- Dokumentacja części, etykiety A4, eksport STEP/STL, JSON i CSV.
- Zachowane szablony, materiały, obrzeża, włókna, kosztorys, rozkrój oraz montaż frontów i szuflad.

## Granice implementacji

Bryła bazowa to płaski wielokąt wyciągnięty przez grubość. Frezowanie odejmuje zadany kształt; nie wyznacza kolejności przejść frezu, promienia narzędzia, posuwu, mocowania ani podcięć. Prostokątna kieszeń ma geometrycznie ostre narożniki. Część musi pozostać jedną bryłą. Parametry obróbek są liczbami, bez formuł zależnych od szerokości mebla. Kontur offsetu musi pozostać pojedynczą pętlą; rozdzielone i złożone wyniki są odrzucane.

Rysunki HTML pokazują nominalny kontur i tabelę operacji; nie są pełnym rysunkiem wykonawczym z przekrojami i tolerancjami. Dokładny profil faz i zaokrągleń znajduje się w STEP. Rozkrój i koszt materiału używają nominalnej formatki. Etykiety zawierają identyfikator tekstowy, bez kodów kreskowych. Biblioteka zapisuje kopie konstrukcji, nie instancje aktualizowane wspólnie.

## Kolejne większe moduły

| Obszar | Funkcje wymagające dalszej implementacji |
|---|---|
| Szkic CAD | Łuki, okręgi i splajny jako kontur, trim/extend, pełny solver więzów, import DXF, trwałe formuły parametrów. |
| Konstrukcja | Uciosy i cięcia ukośne, wpusty/wręgi, czopy, kołki i złącza z katalogów, nietypowe korpusy, podziały frontów i animacje otwierania. |
| Materiały | Profile drewna, laminaty/okleiny obu powierzchni, słoje jako tekstury, magazyn, naddatki technologiczne i wykończenie. |
| Wizualizacja | Pomieszczenia, oświetlenie, fotorealistyczny renderer, GLB i prezentacja klienta. |
| Produkcja | Nesting dowolnych kształtów, remanenty, narzędzia, promienie wewnętrzne i dogbone, ścieżki, symulacja CNC, postprocesory i QR. |
| Integracje | Katalogi rzeczywistych okuć, wybrana maszyna CNC, import STEP/DXF i wymiana z ERP. |

Żaden skończony pakiet nie obejmuje każdej możliwej funkcji CAD/CAM. Wydanie 0.6 wdraża wymieniony zestaw narzędzi; powyższa tabela nie oznacza funkcji już dostępnych.

## Weryfikacja

45 testów automatycznych przeszło: dotychczasowe konstrukcje i geometria oraz nowe fazy na wszystkich 12 kantach, objętości frezowań, wiercenia sześciostronne, odbicia, szyki, biblioteka, półki i eksporty. Osobne scenariusze Tkinter sprawdziły szkic, montaż drzwi/szuflad, rozciąganie, wymiary rozstrzelone, okna fazowania i frezowania, zapis, cofanie i dialog eksportu. To sprawdzenie aplikacji i geometrii, bez walidacji wykonania na maszynie CNC.
