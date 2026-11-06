# Katalog graficzny 0.13

Na pasku **Mebel** są przyciski **Katalog mebli**, **Katalog okuć** i **Katalog materiałów**. Na pasku **Scena** znajduje się **Katalog AGD**. Każdy otwiera tę samą zakładkę katalogu; zakładkę można odpiąć i ponownie zadokować tak jak pozostałe dokumenty.

W katalogu wybierz grupę produktów, wpisz nazwę lub producenta i opcjonalnie kategorię. Wyszukiwanie działa także bez polskich znaków. Karty pokazują wymiary w mm oraz geometrię konstrukcji lub schemat okucia. Kliknięcie obrazu albo **Wybierz** uruchamia wybór. **Źródło** otwiera zapisany odnośnik do dokumentacji danego produktu, jeśli pozycja ma taki odnośnik.

Katalog zawiera wszystkie 52 istniejące typy mebli, osiem kompletów nóg, trzy automatyczne wzorce zawiasów, dwanaście profili prowadnic, dziewięć rozstawów uchwytów/gałek i osiem alternatyw chwytu/otwierania, sześć wariantów taśmy/rowka LED, szesnaście obiektów AGD i wyposażenia pomieszczenia oraz materiały projektu i paletę dekorów. Katalog prezentuje funkcje obsługiwane przez program; nie jest pełnym katalogiem wszystkich producentów.

- **Mebel** tworzy rzeczywisty nowy moduł z dotychczasowej biblioteki i ustawia go obok istniejących mebli. Wymiary pozostają edytowalne w odpowiednim kreatorze.
- **Nogi** przebudowują zaznaczony parametryczny mebel lub narożnik. Wysokość korpusu zostaje zachowana, a wysokość całkowita zmienia się o różnicę wysokości nóg. Nogi pozostają zakupowymi okuciami, nie formatkami płyt.
- **Zawias, prowadnica, uchwyt i LED** włączają narzędzie montażu. Program wraca do głównego widoku 3D; wskaż miejsce na meblu, aby dokonać rzeczywistego montażu. Zakładka katalogu pozostaje otwarta.
- **Chwyt frezowany, krawędziowy lub push-to-open** stosują istniejącą funkcję otwierania do zaznaczonego frontu. Dobór sprężyny zawiasu i ograniczenia wiercenia są sprawdzane przez tę funkcję.
- **AGD** dodaje zakupowy obiekt sceny, oddzielony od produkowanych płyt, obok istniejącego zestawu. Zaznaczony obiekt można dalej przesuwać i wymiarować.
- **Materiał** w pełnym katalogu zmienia materiał **zaznaczonego elementu**. Nie zmienia materiału całego mebla. Zmiana elementu jest zachowana podczas późniejszej przebudowy wymiarów, jeżeli ogólny materiał korpusu/frontów w kreatorze pozostaje bez zmiany. Późniejsza jawna zmiana ogólnego materiału w kreatorze może nadpisać taki wybór. Nowy dekor trafia do listy materiałów tylko raz. Ceny nowych dekorów wymagają wpisania własnej ceny w materiale projektu.

Podglądy mebli są lekkimi schematami konstrukcyjnymi opartymi na parametrach biblioteki, nie renderami CAD każdego produktu. Obiekty AGD używają tej samej geometrii co widok 3D. Okucia są schematami wymiarowymi: nie są certyfikowanymi modelami SKU. Prowadnice podwieszane pokazują zależność szerokości wewnętrznej i grubości boków; nie mylą odsunięcia szerokości wewnętrznej z zewnętrznym luzem na stronę. Pełne wiercenia CNC konkretnej prowadnicy nadal zależą od dostępnego wzorca backendu.

## Integracja

```python
from visual_catalogue import install, show, catalogue_items, apply_item

install(app)
app.visual_catalogue('furniture')
app.visual_catalogue('runner', on_select=callback)
```

Rodzaje: `furniture`, `leg`, `hinge`, `runner`, `handle`, `led`, `appliance`, `material`; angielskie nazwy mnogie są aliasami. Picker z callbackiem pozostaje ograniczony do zadanego rodzaju. Callback `on_select(item)` dostaje niezależną kopię słownika:

```python
{
    'id': 'runner:hettich_ka5632',
    'kind': 'runner',
    'title': '...',
    'category': 'Hettich',
    'tags': [...],
    'dimensions': {...},
    'source': 'https://...',
    'note': '...',
    'payload': {'profile_id': 'hettich_ka5632', 'length': 400, 'price': 0},
    'preview': {...},
}
```

Callback odpowiada za zastosowanie konfiguracji. `False` oznacza odrzucenie; `None` lub inny wynik oznacza poprawny wybór. Bez callbacku `apply_item` korzysta z dotychczasowych fabryk i sterownika montażu. `select_design(app)` wybiera istniejącą główną zakładkę 3D i nie zamyka katalogu. `catalogue_items` i `filter_items` nie modyfikują projektu. Przed wyborem są zatwierdzane poprawne szkice parametrów; nieaktualny dokument lub nieprawidłowe pola blokują akcję. Samo wyszukiwanie/filtry nie tworzą niezapisanej zmiany projektu.
