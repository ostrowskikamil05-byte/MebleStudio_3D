# Etap 2 migracji: układ mebli i magnes w C++

C++ przejmuje dziewięć operacji modułu układania mebli: `groups`, `group_layout`, `group_bounds`, `local_group_bounds`, `move_group`, `rotate_group`, `snap_group`, `nearest_snap` i `contacts`. Biblioteka ogłasza `layout_api_version = 1`. Stan dokumentu, historia i atomowy zapis z poprzedniego etapu pozostają aktywne.

## Granica między edytorem i biblioteką

Adapter przekazuje jedną scenę z punktami paneli, ich pozycjami, obrotami i ustawieniem w pokoju. Geometria nóg i LED jest pobierana raz i przypisywana do właścicieli. C++ oblicza transformacje, gabaryty lokalne i światowe, obrysy narożników i blatów, kandydatów magnesu, kolizje i styki. Wewnętrzne porównania nie wracają do Pythona przez JSON.

Operacje zmieniające układ zwracają ustawienie mebla. Zachowane `_placed` tworzy kopię projektu, odświeża zależne wspólne blaty i wykonuje dokładną walidację CAD przed zatwierdzeniem. `validate=False` pozostaje trybem roboczego podglądu. Odrzucona operacja nie zmienia wejściowego projektu. Przeciąganie nadal zatwierdza jeden krok historii dopiero po puszczeniu myszy.

Natywny kernel `workspace_geometry.polygons_overlap` obsługuje też przecinanie obrysów dla wspólnych blatów. Zachowuje dotychczasowy algorytm i jego tolerancje, w tym brak kolizji przy samym styku krawędzi lub wierzchołka. Wyznaczanie i przebudowa całego wspólnego blatu pozostają w istniejącym module.

Dla publicznego wywołania przecinania obrysów adapter zachowuje także sposób sumowania mieszanych liczb całkowitych i zmiennoprzecinkowych w Pythonie. Orientację przekazuje raz; wszystkie testy przecinania wykonuje C++. Cała pętla magnesu korzysta bezpośrednio z natywnego kernela.

## Zgodność

Zachowano kolejność grup, wybór przy remisie, sześć kierunków dosuwania, wyrównanie osobno dla osi, odstępy, dowolny obrót ręczny oraz ograniczenie magnesu do wielokrotności 90 stopni. Narożniki i wspólne blaty mają właściwy wklęsły obrys zamiast zastępczego pełnego prostokąta. Nogi, LED i widoczność właścicieli wpływają na gabaryty tak jak wcześniej.

Adapter zachowuje sygnatury funkcji, krotki, kolejność pól, argumenty liczbowe akceptowane przez `float()` i wcześniejsze priorytety kontroli błędów. Starsza DLL bez wymaganego API powoduje jawny błąd zgodności zamiast częściowego działania nowej paczki.

## Sprawdzanie i budowanie

`SPRAWDZ.bat --native-library SCIEZKA_DO_DLL --report-dir KATALOG` uruchamia regresję z wybraną biblioteką. Nowe testy obejmują całą granicę sceny, jednorazowe pobranie akcesoriów, brak starej pętli porównań, dokładny CAD, podgląd i wspólne blaty. Osobny zestaw porównuje C++ z niezmienionym kodem etapu 1, z wyłączonymi adapterami natywnymi.

`ZBUDUJ_EXE.bat` kompiluje host i obie DLL, wymusza włączenie testów, wykonuje cztery zestawy CTest i tworzy osobną kompletną paczkę. Brak odnalezionych testów jest błędem budowania. Gotowy program ma prywatne środowisko; instalacja Python 3.13 jest potrzebna jedynie do narzędzi deweloperskich.

To kolejny etap migracji. Interfejs, generatory mebli i okuć, geometria akcesoriów, aktualizacja zależnych blatów i dokładny CAD zachowują dotychczasowe moduły. Źródła i wcześniejsze paczki nie zostały usunięte.
