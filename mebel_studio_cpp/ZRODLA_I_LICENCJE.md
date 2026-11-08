# Źródła pomysłów i licencje (Mebel Studio 3D, wersja C++)

To notatka robocza dla twórców, nie porada prawna. Przed publicznym wydaniem programu warto, aby ktoś zweryfikował licencje.

## Zasady przy dodawaniu funkcji
- **Inspirujemy się ogólnymi pomysłami interfejsu** (np. podpowiedzi przy kursorze, wpisywanie wartości z klawiatury, skróty narzędzi),
  ale **nie kopiujemy cudzego kodu, grafik, ikon, tekstów pomocy ani nazw handlowych**. Nazw cudzych produktów nie używamy w interfejsie programu
  ani w nazwach funkcji; w dokumentacji tylko, gdy to konieczne i neutralnie.
- Własne rozwiązania są pisane od zera na podstawie opisu zachowania, a nie cudzego kodu źródłowego.
- Format **DXF** czytamy własnym parserem na podstawie publicznie dostępnego opisu formatu tekstowego (pary „kod grupy – wartość”).
  Nie używamy bibliotek ani kodu Autodesk. Nie wypisujemy nazw produktów Autodesk w interfejsie.
- Kopiując fragment cudzego kodu lub zasobu (nawet z internetu) trzeba najpierw sprawdzić jego licencję i dopisać go do listy poniżej.

## Biblioteki używane przez program
| Biblioteka | Licencja (sprawdź dokładną wersję przy wydaniu) | Uwagi |
|---|---|---|
| nlohmann/json 3.12 | MIT | tekst licencji dołączony do nagłówka |
| OpenCASCADE Technology | LGPL 2.1 z wyjątkiem OCCT | biblioteki dołączamy jako osobne DLL (łączenie dynamiczne); trzeba dołączyć tekst licencji i umożliwić podmianę DLL |
| Tcl/Tk 8.6 | licencja typu BSD (Tcl/Tk License) | dołączane skrypty Tcl/Tk razem z programem |
| VTK (opcjonalnie, rendering GPU) | BSD 3-Clause | tylko gdy zbudowano z VTK |
| Pakiet środowiska Python (katalog `licencje`) | różne (BSD/MIT/NCSA) | dotyczy dołączonej wersji 0.14 |

## Własne implementacje (bez cudzego kodu)
- Tryb grafiki Klasyczny/Realistyczny (`render_style`, `graphics_mode_ui`), kanty i wzorki szkicownika (`sketch_design`) — napisane od zera dla tego programu;
  z makiety użytkownika wzięto wyłącznie ogólny układ interfejsu (panel „Silnik i jakość 3D”, zaokrąglenia, fronty). Nie używamy żadnych zewnętrznych grafik ani tekstów.
- Pracownia 3D (`modeler_geometry`, `modeler_ui`) i system frezów (`cutters`) — napisane od zera. Z programów do modelowania bryłowego (np. SketchUp) przejęto
  wyłącznie ogólne, niechronione pomysły użytkowe: rysowanie prostokąta na płaszczyźnie, wyciąganie ściany, przyciąganie do punktów i osi, wpisywanie wymiarów
  z klawiatury. Nie używamy cudzego kodu, bibliotek modelowania, ikon, nazw poleceń ani plików; rzut ekranu → promień, wybór ściany, przyciąganie
  i zamiana bryły na płytę to własna matematyka na kamerze programu. Katalog frezów (prosty, kulowy, V, zaokrąglający, fazujący) to powszechnie znane rodzaje narzędzi CNC.
- Tryb 2D CAD Pracowni (`cad2d`, `modeler_cad`, rozszerzenia `modeler_geometry`) — napisany od zera. Łuki zapisane jako wierzchołki z „wybrzuszeniem” (bulge) to powszechnie
  znany, matematyczny sposób opisu łuku w łamanej (wzór tan(kąt/4)); nie używamy żadnego cudzego kodu ani bibliotek CAD. Z programów CAD przejęto wyłącznie ogólne idee
  użytkowe: linia/łuk 3-punktowy/łuk styczny, zaokrąglenie i ścięcie narożnika, okrąg, szczelina, przyciąganie do końców/środków/kwadrantów, wpisywanie długości i kąta
  z klawiatury, siatka, zmiana płaszczyzny rysunku. Nazwy poleceń i interfejs są własne.
- Usłojenie drewna (`wood_grain`): procedura w GLSL liczona od „osi pnia” (słoje jako odległość od osi, pochylenie osi daje łuki typowe dla desek) z szumem wartościowym
  i funkcją skrótu typu `fract(sin(...))` — powszechnie znane, niechronione techniki grafiki proceduralnej; kod napisany od zera, bez tekstur i cudzych zasobów.
  Ziarno każdej płyty pochodzi z hasza jej identyfikatora (FNV-1a + miksowanie 64-bitowe — algorytmy publiczne).
- Tryb „Szkic ołówkiem” (`pencil_filter`, `render_style`) — filtr obrazu napisany od zera: obrys z granic elementów i skoków głębi, drżenie linii z szumu wartościowego,
  kreskowanie z jasności. Z programów rysunkowych przejęto wyłącznie ogólną ideę wyglądu „od ręki”; nie używamy cudzych tekstur papieru ani pędzli.
- Narzędzie Ołówek (`cad2d::fit_stroke`): uproszczenie pociągnięcia z podziałem w miejscu największej odchyłki (idea klasyczna, jak w algorytmie Douglasa–Peuckera),
  dopasowanie okręgu metodą najmniejszych kwadratów (Kåsa — metoda publiczna) i rozpoznanie okręgu/prostokąta — własna implementacja, bez bibliotek zewnętrznych.

## Czego jeszcze brakuje
- Zbiorczego pliku z pełnymi tekstami licencji dla wersji C++ (OpenCASCADE, Tcl/Tk, VTK) w paczce wydawniczej.
- Przeglądu czcionek i ikon użytych w interfejsie pod kątem licencji.
