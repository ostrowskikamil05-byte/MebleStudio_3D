# Wspólne blaty w C++ — etap 3

Natywna biblioteka `MebelModel.dll` oblicza geometrię wspólnego blatu nad
połączonymi szafkami. Obsługuje odległość obrysów, naddatek, sumę wielokątów
oraz kompletny solver geometrii: wysokości, kolizje korpusów, połączenie
podpór, obrys z naddatkiem, początek i kontrolę poprawności konturu.

Interfejs JSON `workspace_worktop` ma wersję 1 i operacje `distance`, `offset`,
`union`, `geometry`. Solver dostaje uporządkowane rzuty korpusów, ich górne
wysokości i identyfikatory kotew. Jedno wywołanie wykonuje wszystkie pętle
geometryczne w C++; nie wywołuje Pythona dla każdej pary ani krawędzi.

Adapter zachowuje sygnatury i typy wyników funkcji `_distance`, `_offset`,
`_union`, `_geometry` w `workspace_worktops.py`. Wyciągnięcie rzutu korpusu
z konfiguracji mebla lub narożnika pozostaje w `_body`. Oryginalne definicje
są zachowane jako punkt odniesienia oraz do uruchomienia źródeł bez DLL.
Opcjonalne informacje o orientacji na granicy adaptera zachowują zachowanie
sumowania mieszanych liczb całkowitych i zmiennoprzecinkowych Pythona.

Tworzenie części, kotwy, identyfikatory, podpis ręcznej geometrii i aktualizacja
stanu `ready` / `needs_fit` pozostają w dotychczasowych funkcjach. Nadal chronią
ostatnią poprawną geometrię po rozłączeniu lub usunięciu podpory i chronią
ręczne zmiany blatu przed nadpisaniem. Dokładna walidacja CAD pozostaje
w istniejącej warstwie CadQuery/OpenCascade.

Etap 3 obejmuje solver blatów. Cały edytor nie jest jeszcze przepisany do C++.
Paczka zawiera prywatne środowisko potrzebne zachowanym modułom. Należy
zachować cały katalog programu.

Budowanie: `ZBUDUJ_EXE.bat` lub `ZBUDUJ_EXE.ps1`. Testy: `SPRAWDZ.bat`
oraz CTest w katalogu kompilacji; CTest zawiera osobny zestaw solvera blatów.
