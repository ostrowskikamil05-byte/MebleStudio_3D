# MebelStudio3D 0.9 — przekazanie projektu

Gotowy program Windows x64: `MebelStudio3D.exe` z folderem `_internal`. Silnik CadQuery 2.8.0 / OpenCascade 7.9.3 i Python 3.13 są w paczce. Użytkownik rozpakowuje ZIP i uruchamia EXE; nie instaluje pip ani zewnętrznego CAD. Pakowanie: PyInstaller 6.22.3, onedir.

## Nowe moduły i zachowanie

`dock_workspace.py` zarządza dokumentami w zakładkach, ich odłączaniem i powrotem z osobnych okien. Zachowuje obiekty formularzy i szkicu. Dokumenty zamykają się przy zmianie projektu; generacja projektu chroni przed zastosowaniem starego edytora. Niezapisane kontury i dane formularzy są wykrywane przed zamknięciem.

`runners_catalog.py`, `runner_installation.py`, `hardware_ui.py` i `hardware_report.py` zawierają 12 profili prowadnic 5 producentów, kalkulator tolerancji i wymiarów skrzynki, dobór długości oraz raport źródeł. Głębokość uwzględnia cofnięte plecy. Dane niepotwierdzone są opisane; nieznana głębokość wymaga jawnego potwierdzenia użytkownika.

`opening_options.py` obsługuje 8 sposobów otwierania/wykończenia frontu: TIP-ON, Salice Push, uchwyt krawędziowy, kieszeń i wycięcie. Obróbki są rzeczywiste w bryłach CAD. Kompatybilność magnetycznego TIP-ON ze sprężyną zawiasów jest sprawdzana również przy późniejszej wymianie zawiasu. Sensys 9071205 TH52 posiada własny przepis puszki, opis zakresu potwierdzenia i źródło.

`material_presets.py` rozdziela grubości i formaty produktów/regionów od ustawień warsztatowych. `shaped_furniture.py` i `style_ui.py` dodają generatory obrysów, parametryczne ryflowanie i 12 wzorców mebli. `core.py` zapisuje parametry obrysu, dekoracji, okleinowania i wzorca; `cad_geometry.py` tworzy dokładne krzywe STEP. `direct_edit.py` przebudowuje wzorce, zachowując ID, materiały i ręczne obróbki; odmawia nadpisania ręcznie zmienionego układu konstrukcji.

Dołączono 49 projektów w PRZYKLADY, demonstracje kuchni, okuć, obłych mebli, STEP oraz raporty montażu i dokumentację HTML.

## Walidacja wydania

Dokładne wyniki końcowe zawierają `SPRAWDZENIE_TESTOW.txt`, `SPRAWDZENIE_GUI.json` i `SPRAWDZENIE_EXE.json`. Obejmują model, przebudowę mebli, profile prowadnic, zgodność zawiasów, otwory, dokładne bryły i eksport, zapis/odczyt, interfejs oraz uruchomienie spakowanego EXE bez systemowego Pythona i pip w PATH. Testy GUI obejmują odłączanie i powrót szkicu bez zmiany jego konturu/formularza oraz przełączanie wymiarów w wizualizacji. Starsze projekty .mebel można wczytać; starsza aplikacja nie musi obsługiwać nowych pól.

## Rozbudowa i ograniczenia produkcyjne

Źródła i testy są w `ZRODLA_PROGRAMU`. Testy deweloperskie: `python -m unittest discover -s tests -v`. Budowanie: `python -m PyInstaller --noconfirm --onedir --windowed --name MebelStudio3D --collect-all cadquery --collect-all OCP --collect-all vtkmodules launcher.py`. Instrukcję, przykłady i raporty należy umieścić obok EXE.

STEP zawiera produkowane części i rzeczywiste obróbki, z dokładnymi kołami, elipsami i łukami generatorów. Szkic, podgląd i prosty DXF używają segmentów. Okucia zakupowe są wizualizowane schematycznie. Katalog prowadnic nie zawiera wszystkich artykułów ani pełnych wzorów wierceń haków, zaczepów i sprzęgieł; niepotwierdzone otwory nie są dopisywane automatycznie. Wzory ogólne prowadników zawiasów i niektóre piloty są robocze, oznaczone przy przepisie.

Mechanizmy stanowią podgląd ruchu; nie wykonują pełnej kinematyki ani analizy kolizji. Szablony drewniane nie mają obliczeń wytrzymałości ani wszystkich połączeń stolarskich. Eksport CNC obejmuje geometrię i operacje; postprocesor i ścieżki dla konkretnego modelu maszyny pozostają kolejnym etapem. Ceny nowych dekorów/okuć wymagają wprowadzenia przez użytkownika.

## Aktualizacja 0.9

furniture_layout.py: per-Part layout rigid po transformacji konstrukcji; world/local_world/layout_point oddzielają układ od części. Formularze: furniture_layout_ui.py, form_dialog.py. project_print.py tworzy wektorową dokumentację A4. DockWorkspace naprawia zgubiony grab i zachowuje modalne okna. Wycofanie gestów i aktualne identyfikatory chronią formularze po innych edycjach.
