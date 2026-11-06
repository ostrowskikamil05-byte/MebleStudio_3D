# Etap 3 migracji MebelStudio3D do C++ — 30.10.2026

Gotowa paczka: `MebelStudio3D_Cpp_Etap3_2026_10_30`. Uruchom `MebelStudio3D.exe`, zachowując cały katalog.
Poprzednie etapy i źródła odniesienia pozostawiono bez zmian.

## Przeniesione obliczenia

Nowy solver C++20 (`native/core/worktop.cpp`) wykonuje odległość obrysów,
naddatki, sumę wielokątów i pełne wyznaczenie geometrii wspólnego blatu.
Sprawdza wysokości podpór, dodatnie nakładanie korpusów, ich połączenie,
obrys z naddatkiem i poprawność konturu. Zachowuje tolerancje, kolejność punktów
i kotew oraz błędy dotychczasowego wydania. Zaokrąglanie odwzorowuje
`round(value, 6/10)` Pythona bez pośredniego mnożenia przez potęgę dziesięciu.

Adapter `native_worktops.py` wyciąga korpusy raz i przekazuje solverowi jedno
żądanie. Pętle geometrii działają w C++ bez wywołań Pythona dla każdej pary
lub krawędzi. Zachowano oryginalne definicje i sygnatury funkcji oraz typy
list/krotek. Zerowy naddatek zachowuje płytką kopię punktów.

## Sprawdzenie

- **503 testów edytora**: 0 błędów, porażek i pominięć.
- **5/5 zestawów CTest**: sceny, model, dokument, układ i blaty.
- **9954 bitowo zgodnych zaokrągleń** z rzeczywistym
  Pythonem 3.13.14 paczki; osobny zestaw solvera obejmuje 11 grup kontroli.
- **1048 niezależnych porównań** z zachowanym kodem Etapu 2:
  odległości, naddatki, sumy i geometrię porównano z wyłączonymi bindingami
  C++ w oracle. Sprawdzono kolejność konturów/kotew i komunikaty błędów.
- **86 unikatowych projektów / 92 plików**
  odniesienia zachowało sumy SHA256.
- Zachowano **912 wcześniejszych funkcji i metod**
  oraz ich sygnatury; istniejących testów i nagłówka C ABI nie zmieniono.
- Nowy EXE przeszedł własny test programu, tworzenie i dopasowanie blatu,
  zachowanie otworów i metadanych, stan po odsunięciu podpory oraz natywne
  cofanie/ponawianie i zapis/odczyt pełnego projektu.
- Rzeczywiste D3D11, D3D12 i Vulkan na karcie RTX 5070 Ti Laptop:
  bez programowego renderera, błędów callbacków Tk i wycieków dokumentów/buforów.
- Rzeczywiste ustawienia użytkownika zachowano; testy używają prywatnego katalogu.

Weryfikowana DLL SHA256: `e7a680e782207edd2158158c9b23cf1151c56364507cb069b1cb523b93dfd372`.
Kompilacja i testy: Windows x64, GNU C++ 16.2.0, C++20, Ninja;
środowisko paczki Python 3.13.14. Kompilacja MSVC nie była uruchamiana.
Pełna regresja źródeł korzysta z deweloperskiego Python 3.13.16 i bibliotek paczki.

## Pozostały zakres

Etap 3 obejmuje solver blatów. Pobieranie korpusów z konfiguracji, tworzenie
części, kotwy, podpis ręcznej geometrii, status `ready` / `needs_fit` i dokładna
walidacja CAD zachowują dotychczasową warstwę. Testy potwierdziły ochronę
ręcznych wierceń/kieszeni, danych użytkownika i ostatniej poprawnej geometrii.
Generatory, GUI i pozostałe moduły nadal wymagają kolejnych etapów migracji.
Cały program nie jest jeszcze przepisany do C++.

Dowody: `SPRAWDZENIE_ETAP3`; źródła: `ZRODLA_PROGRAMU`.
Manifest SHA256 obejmuje wszystkie pliki paczki poza nim samym.
