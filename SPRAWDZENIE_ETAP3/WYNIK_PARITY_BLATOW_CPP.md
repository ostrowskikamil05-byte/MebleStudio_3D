# Niezależna walidacja solvera wspólnego blatu — Etap 3

Końcowa biblioteka `MebelModel.dll`: SHA256 `e7a680e782207edd2158158c9b23cf1151c56364507cb069b1cb523b93dfd372`.

Wynik: **1048 z 1048 kontroli zakończyło się poprawnie**, bez pozostawionych buforów natywnych. Proces adaptera zamknął się kodem 0.

## Niezależność porównania

Referencją są niezmienione źródła `MebelStudio3D_Cpp_Etap2_2026_10_30/ZRODLA_PROGRAMU`, uruchomione w środowisku **Python 3.13.14 dostarczonym z paczką**. Wszystkie powiązania natywne wyłączono przed importem modelu i funkcji geometrii. Oracle korzysta więc z oryginalnych algorytmów Python, a nie z badanego solvera.

Drugi, osobny proces tego samego środowiska uruchamia rzeczywisty adapter Etapu 3, ze wskazaniem konkretnej badanej DLL. Dla operacji `_geometry` oba procesy otrzymują identyczny zapis całego projektu. Sprawdzono wynik, kolejność punktów i identyfikatorów, publiczny kontrakt list/tuple, niezmienność projektu oraz liczbę wywołań solvera. Dla zerowego naddatku adapter zachowuje płytką kopię listy punktów bez wywołania DLL.

## Zakres i liczby

- **495 kontroli backendu C++**: distance 95, offset 90, union 106, geometry 204.
- **422 kontrole rzeczywistego adaptera**: distance 95, offset 90, union 106, geometry 131.
- **131 kontroli niezmienności projektów**.
- **86 różnych projektów `.mebel`** spośród 92 zachowanych plików; wszystkie pliki przykładów oraz główne moduły źródłowe Etapu 2 zachowały SHA256.

Badane przypadki obejmują:

- Stykanie krawędzi i narożników, dodatnie nakładanie, zawieranie i rozłączne obrysy.
- Obrysy L i U, zamknięty otwór, połączenie samym punktem oraz usuwanie punktów współliniowych.
- Próg sąsiedztwa 0,5 mm przed rozszerzeniem obrysów; rozszerzenie nie może ukryć błędu rozłącznych korpusów.
- Próg różnicy wysokości 0,1 mm, także reprezentacje zmiennoprzecinkowe bez poszerzania tolerancji.
- Zaokrąglanie cięć do 10 miejsc i współrzędnych do 6 miejsc; wartości `n + 0.0000005` oraz pełny ciąg offset → union, również dla obrotów 17° i 33°.
- Schodkowe obrysy przy granicy 128 punktów i dokładną kolejność sprawdzania błędów.
- Puste wejścia, zerowe krawędzie, pojedyncze i podwójne obrysy z duplikatem punktu, `ZeroDivisionError` i dokładne komunikaty błędów.
- Rzeczywiste pary i układy L skonfigurowanych szafek, odwróconą kolejność podpór i obrót całego układu.

Wyniki końcowego konturu porównywano z tolerancją bezwzględną **1e-9** i względną **2e-13**, w podanej kolejności punktów. Nie obracano listy wierzchołków ani nie sortowano identyfikatorów, aby ukryć różnice. Sprawdzano również znak zmiennoprzecinkowego zera.

## Artefakty

- `worktop_parity.json` — pełny wynik 1048 kontroli, wykaz korpusu, hashe odniesienia i badanej biblioteki.
- `bundled_oracle_process.json` — potwierdzenie wersji dostarczonego interpretera i poprawnego zakończenia oracle.
- `bundled_worktop_launcher.log` — zwięzły wynik końcowy.
- `run_worktop_parity.py`, `worktop_adapter_worker.py`, `launch_bundled_worktop_parity.py` — odtwarzalny harness.

Testy nie otwierają GUI ani GPU. Nie zmieniają aplikacji, projektów bazowych ani jej ustawień użytkownika. Wyznaczanie podpór przez zachowane `_body`, cykl instalowania i dopasowywania blatu, podpisy, statusy oraz dokładny CAD pozostają osobnymi granicami aplikacji; badany solver przejmuje odległości, offset, sumę obrysów, łączność, wysokości i końcowy kontur.
