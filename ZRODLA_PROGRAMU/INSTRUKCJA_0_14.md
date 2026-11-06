# MebelStudio3D 0.14 — pełny program z natywnym widokiem C++

W tej wersji dotychczasowy kreator, panele, szkic, CAD, biblioteki, wyposażenie, rozkrój i eksporty pracują w jednym programie z nowym silnikiem 3D. Silnik C++ obsługuje DirectX 11, DirectX 12 i Vulkan. Dotychczasowa logika CAD i interfejs są zachowane w osadzonym środowisku Python/Tk; nie jest to jeszcze przepisanie wszystkich funkcji do C++.

## Uruchomienie

Rozpakuj cały ZIP do osobnego folderu i uruchom `MebelStudio3D.exe`. Zachowaj wszystkie dołączone foldery oraz biblioteki. Nie potrzeba pip, osobnej instalacji Pythona ani CAD. Starszą wersję 0.13 zachowaj osobno. Z przycisku **Otwórz** można wczytać dotychczasowe projekty `.mebel` i dołączone przykłady.

## Wybór silnika

Otwórz **Ustawienia → Okno, skala, panele i silnik 3D**. Pole **Silnik widoku 3D** zawiera:

| Wybór | Działanie |
|---|---|
| Automatyczny dobór | Uruchamia dostępny silnik C++; gdy jest niedostępny, korzysta z dotychczasowego OpenGL lub CPU. |
| DirectX 11 · silnik C++ | Natywny widok DirectX 11. |
| DirectX 12 · silnik C++ | Natywny widok DirectX 12, jeśli obsługuje go karta i sterownik. |
| Vulkan · silnik C++ | Natywny widok Vulkan, jeśli dostępny jest zgodny sterownik. |
| OpenGL · GPU i wysoka jakość | Dotychczasowy silnik GPU z jego trybem wysokiej jakości. |
| CPU | Dotychczasowy renderer programowy. |

OpenGL zachowuje dotychczasowe efekty oświetlenia wysokiej jakości. Silnik natywny ma własny wygląd materiałów i emisję LED; nie odtwarza wszystkich efektów tego trybu OpenGL.

Wybór silnika dotyczy głównego widoku, dodatkowych zakładek wizualizacji oraz podglądu szkicu. Kamery i ustawienia rozstrzelenia dodatkowych wizualizacji pozostają niezależne. Nieudana zmiana natywnego silnika zachowuje działający podgląd i zapisuje informację w logach. Ustawienia wyglądu nie zmieniają geometrii ani danych produkcyjnych mebli.

## Panele i tworzenie mebli

Górna wstążka zachowuje grupy **Mebel, CAD, Układ, Scena, Produkcja, Widok i Ustawienia**. Inspektor boczny zawiera **Mebel, Elementy / CAD, Pomieszczenie i Narożnik**. Przy wąskim panelu nazwy zakładek przechodzą w rozwijaną listę. Separatorami można zmieniać szerokość panelu oraz wysokość wstążki. Skala interfejsu, rozmiar okna i widoczność paneli zapisują się lokalnie.

Kreator mebla zachowuje pola szerokości, wysokości, głębokości i grubości materiału; fronty, szuflady, podziały, pawlacze, zawiasy, prowadnice, nóżki, cokoły, blaty i wzmocnienia. Pozostają presety, kopiowanie ustawień, paleta dekorów i katalog graficzny. W **Elementy / CAD** można ustawić dokładne U/V/T, pozycję i obrót części, materiał, obrzeża oraz widoczność.

## Mysz i bezpośrednia edycja

- Rolka przybliża lub oddala widok.
- Przytrzymane środkowe kółko albo **Alt + lewy przycisk** obraca kamerę.
- **Shift + środkowe kółko** przesuwa kamerę. Przeciągnięcie prawym przyciskiem także przesuwa widok.
- Krótkie kliknięcie prawym przyciskiem otwiera menu wyboru mebla lub wyposażenia.
- W trybie **Przesuwaj meble** działa magnes i ustawianie kompletu. Obrót mebla ma osobne przyciski −90° i +90°.
- W trybie **Rozciągaj mebel**, **Rozciągaj część** i **Kanty** pozostają aktywne uchwyty, koło osi oraz edycja krawędzi.
- **Dopasuj** lub klawisz **F** ustawia cały projekt w widoku.

W pomieszczeniu można osobno zaznaczać, przesuwać i obracać AGD oraz inne wyposażenie. Ta funkcja nie tworzy z urządzenia płyty do rozkroju.

## Szkic, CAD i okucia

Rysowanie szkicu, kolejne elementy, zamykanie konturu, wybór roli oraz zmiana wymiarów 2D/3D pozostają dostępne. Podgląd żywy pokazuje zatwierdzalny kandydat konstrukcji. Otwarty albo niepoprawny kontur usuwa poprzedni podgląd, aby nie pokazywać nieaktualnego elementu.

Zachowane są dokładne zaokrąglenia i fazowania pojedynczych krawędzi, kieszenie, rowki, wycięcia, otwory, koła i elipsy oraz biblioteki mebli obłych i drewnianych. Narożniki lewe i prawe mogą być L, ścięte lub zaokrąglone. Wspólny blat nadal współpracuje z korpusami wzmocnionymi listwami.

Profile zawiasów i prowadnic, dystanse skrzynek, uchwyty i wypychacze zachowują swoje dane oraz kalkulatory. Przyciskiem **Przeciągnij okucie / LED** można montować element na rzeczywistej powierzchni mebla. System utrzymuje długość LED i zależne wiercenia. Suwak **Otwieranie** oraz animacja drzwi/szuflad zmieniają podgląd; produkcja pozostaje wyliczana dla zamkniętego mebla.

## Wymiary, otwory i prezentacja

W **Widok** pozostają **Rozstrzelony**, **Wymiary**, **X-ray**, zakres X-ray oraz tryby widoczności otworów montażowych. Wymiary można ograniczyć do wybranego mebla, części lub całego zestawu. Oznaczenia otworów zachowują kolory, informacje o średnicy i głębokości oraz możliwość pokazywania także zasłoniętych wierceń.

Scena zachowuje ściany, podłogę, jasność, LED, lampy oraz przełączniki oświetlenia. Dodatkowe zakładki wizualizacji mają osobną kamerę i mogą przedstawiać bieżący projekt albo stały podgląd szkicu.

## Zakładki i odłączone okna

Szkic, główny widok 3D, dodatkową wizualizację, katalog, ustawienia oraz logi można przeciągnąć poza pasek zakładek i z powrotem. Przenoszona jest ta sama zawartość dokumentu, dlatego pozostają jego szkic, wpisane wartości i kamera. Zamknięcie odłączonego głównego widoku 3D przywraca go do programu. Zamknięcie dodatkowej wizualizacji zwalnia jej silnik i kończy odświeżanie.

## Produkcja i dokumenty

Zakładki **Formatki, Kosztorys i Rozkrój** zachowują dotychczasowe obliczenia. **Drukuj komplet** tworzy widoki zestawu, rozstrzelone szafki, wymiary, rysunki części i rozkrój; z okna drukowania przeglądarki można zapisać PDF.

Pozostają eksporty CSV, DXF, STEP, STL, dokumentacja JSON, raporty HTML/SVG, etykiety i rysunki obróbek. Rozkrój gilotynowy nadal jest przybliżony; kontury nieregularne korzystają z prostokątnych półfabrykatów. Gotowy program maszyny CNC wymaga postprocesora dla wybranego sterowania; użytkownik nie wskazał jeszcze producenta CNC.

## Logi

**Scena i diagnostyka → Otwórz / pokaż logi** otwiera zakładkę developerską. Dziennik zapisuje błędy obsługi, problemy silnika i informację o podglądzie zastępczym. Raporty sprawdzenia dołączone do paczki opisują faktycznie wykonane testy.
