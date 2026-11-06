# MebelStudio3D 0.15 — kolejny etap migracji do C++

Nowa biblioteka `MebelModel.dll` zawiera obliczenia C++20. Dotychczasowy interfejs oraz pełny projekt pozostają dostępne w głównym `MebelStudio3D.exe`. Wersja 0.14 jest zachowaną, osobną wersją odniesienia.

## Co wykonuje C++

Biblioteka zawiera 57 operacji: 22 operacje modelu, 19 operacji profili/szkicu/obróbki, 10 operacji konstrukcyjnych i 6 operacji interfejsu, zapisu lub przekształcania siatek. 35 ma aktywne adaptery w bieżącym programie. Pozostałe są dostępne dla rozwijanego natywnego modelu; same testy dostępności nie oznaczają, że wszystkie wywołania starego interfejsu są już przeniesione.

- Dotychczasowy algorytm rozkroju z rzazem, marginesem, obrotem i kierunkiem włókien, podziałem na materiał i grubość.
- Kosztorys płyt i drewna, obrzeży, okuć, wyposażenia, robocizny i narzutu.
- Obrysy prostokątne, z czterema promieniami, eliptyczne, kołowe, kapsułowe, łukowe, trapezowe i półeliptyczne; powierzchnie i obwody.
- Wiercenia na sześciu powierzchniach, pogłębienia, szyki otworów, walidacja kieszeni/rowków/wycięć i przesuwanie operacji.
- Normalizowanie elementu szkicu, odbicia, obrót i więzy odcinków. Pełna dotychczasowa walidacja części działa przed zatwierdzeniem szkicu.
- Obrysy i konfiguracje narożników, receptura montażu oraz obliczenia 12 istniejących profili prowadnic z wymiarami i ostrzeżeniami katalogowymi.
- Transformacja wierzchołków całej części jednym binarnym wywołaniem, z ograniczoną pamięcią podręczną niezmiennych siatek.
- Zawijanie pasków narzędzi i ograniczenia szerokości inspektora.
- Odczyt oraz atomowy zapis pliku projektu przez Win32, również w ścieżkach z polskimi znakami. Walidacja projektu nadal odbywa się przed zapisem i po odczycie.

Silniki DirectX 11, DirectX 12 i Vulkan z wersji 0.14 pozostają dostępne. Dotychczasowy tryb wysokiej jakości OpenGL również jest zachowany.

## Co pozostaje do przepisania

To nadal aplikacja hybrydowa. Żaden cały moduł Python nie został usunięty ani zadeklarowany jako całkowicie zastąpiony C++. Model danych i zależności konstrukcyjne, generatory całych mebli, montaż/regeneracja okuć, mechanizmy, historia edycji, pomieszczenia, szczegółowy interfejs Tk, dokowanie, dokumenty i eksporty zachowują dotychczasowe moduły poza wyżej wymienionymi adapterami. Dokładne bryły CAD korzystają z zachowanego Open CASCADE/CadQuery.

Małe funkcje pomocnicze układu mebli są już dostępne w DLL, ale dotychczasowa pętla magnesu wykonuje je lokalnie. Przekazywanie pojedynczego porównania przez JSON spowalniałoby tę pętlę. Cała pętla powinna przejść razem z natywnym modelem.

Następne etapy:

1. Rezydentny model `Project`/`Part` w C++, wraz z powiązaniami, walidacją, kopiowaniem, historią i poleceniami edycji. Usunie potrzebę przekazywania dużych struktur przez adapter.
2. Zależne generatory mebli i okuć, pełna pętla dopasowywania, wspólne blaty, mechanizmy i sceny.
3. Bezpośredni natywny moduł dokładnej geometrii CAD, dokumentacji i eksportów produkcyjnych.
4. Pełny natywny interfejs z zachowanymi kontrolkami, zakładkami, dokowaniem, ustawieniami i skrótami. Usunięcie wbudowanego środowiska Python dopiero po weryfikacji równoważności całości.

## Testowanie i budowanie

`MIGRACJA_CPP.json` rozróżnia operacje dostępne i aktywne adaptery. Panel **Ustawienia → Silnik C++ i stan migracji** pokazuje bieżące wywołania natywne. Raporty końcowego wydania są w `SPRAWDZENIE`, a wyniki podsumowuje `SPRAWDZENIE_0_15.json`. Pomiary algorytmów nie są gwarancją liczby klatek całego widoku.

Program działa z dołączonymi bibliotekami bez instalowania Python, pip lub CAD. Rozpakuj cały ZIP do krótkiego folderu, np. `C:\MebelStudio3D_0_15`, i uruchom `MebelStudio3D.exe`.

Budowanie biblioteki obliczeń LLVM-MinGW x64:

```powershell
& .\native\tools\build_model.ps1 -Compiler 'C:\llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe'
```

Kontrakt pamięci i ABI opisują `native/core/model_api.h` oraz `native/core/core_api.hpp`. Każdy bufor odpowiedzi należy zwolnić w jego DLL przez `ms3d_model_free`. Błędy algorytmów są zgłaszane bez cichego ponownego uruchamiania starego obliczenia.

JSON for Modern C++ 3.12.0 ma dołączoną licencję MIT. Nagłówek pochodzi z oficjalnego projektu: https://github.com/nlohmann/json/tree/v3.12.0. Zewnętrzne środowisko aplikacji i licencje zachowano z osobnej, działającej wersji 0.14.
