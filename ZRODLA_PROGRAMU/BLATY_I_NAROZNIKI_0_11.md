# Wspólne blaty, listwy i narożniki 0.11

## Góra korpusu

Kreator ma `top_mode='panel'` albo `top_mode='rails'`. Listwy są dwiema rzeczywistymi formatkami pomiędzy bokami: przednią i tylną, o edytowalnej szerokości `rail_width` (domyślnie 80 mm). Ich górna powierzchnia pozostaje na dotychczasowym poziomie korpusu. Przednia listwa zachowuje identyfikator dotychczasowej góry i odniesienia komór. Plecy, półki, drzwi oraz okucia nadal odnoszą się do tego samego korpusu.

Przy tworzeniu wspólnego blatu korpusy kreatora zostają przełączone na listwy. Niezmienione korpusy rozpoznane z biblioteki można w tym kroku bezpiecznie skonwertować. Ręcznie wykonana pełna góra nie jest usuwana automatycznie. Jeżeli dotychczasowy panel zawiera otwór, który nie mieści się na listwie, zmiana jest odrzucana wraz z komunikatem.

## Jeden wspólny blat

Blat jest rzeczywistym elementem `Part`, z własną grupą, grubością, materiałem, obrysem, obrzeżem i pozycją. Trafia do rozkroju, eksportu produkcyjnego i bryły CAD. Obrys jest sumą połączonych korpusów wraz z ustawionym nawisem. Układ L albo U zachowuje puste wnętrze; nie staje się pełnym prostokątem obejmującym całą zabudowę.

Korpusy muszą stykać się i mieć tę samą wysokość powierzchni pod blatem. Program dopuszcza niewielką tolerancję geometryczną (0,5 mm styku, 0,1 mm wysokości). Wspólny blat obsługuje ustawienie korpusów o wielokrotność 90°. Połączony obrys z zamkniętym otworem lub moduły stykające się tylko jednym punktem wymagają rozdzielenia na osobne blaty.

Jeżeli szafki miały własne blaty kreatora, ich zastąpienie usuwa te formatki i zachowuje wysokość korpusów. Z ustawionej wysokości szafki odejmowana jest wtedy grubość jej poprzedniego indywidualnego blatu. Wspólny blat ma swoją grubość i własną grupę, więc jego grubość jest widoczna w wymiarach całego zestawu, a korpus pozostaje osobno edytowalnym modułem.

Powiązanie korzysta z identyfikatorów części podporowych. Zmiana wymiarów lub nazwy szafki dopasowuje istniejący blat, zachowując jego identyfikator. Usunięcie szafki, rozdzielenie korpusów lub ręczna zmiana geometrii/pozycji blatu zachowuje ostatnią poprawną formatkę i ustawia status `needs_fit` z powodem. Można nadal przemieszczać szafki. Po ich ponownym prawidłowym dosunięciu blat dopasowuje się ponownie. Jawne „Dopasuj” zgłasza powód, gdy dopasowanie jest niemożliwe.

Kopiowanie całego zestawu kopiuje i remapuje powiązania między nowymi szafkami i ich nowym blatem. Kopiowanie samego blatu tworzy samodzielną formatkę bez odwołań do oryginalnych szafek.

## Narożniki

Fabryka narożników tworzy otwarty korpus z rzeczywistym dnem, półkami, tylnymi i końcowymi bokami oraz pełną górą albo listwami. Dostępne są wersje lewa/prawa i trzy wewnętrzne naroża: L, ścięte lub zaokrąglone. Parametr `arm_depth` określa głębokość ramion. `radius` określa długość ścięcia lub promień wklęsłego wewnętrznego naroża. Lewa/prawa wersja odbija konstrukcję w osi X.

Zaokrąglenie obrysu jest zapisane jako kontrolowana liczba odcinków (`segments`, 4–24 na ćwiartkę). Szkic, CNC i bryła CAD korzystają z tego samego rzeczywistego obrysu wielokąta. Ten wariant nie udaje dokładnego łuku w eksporcie STEP. Wymiary, kierunek narożnika i kształt można przebudować z zachowaniem ID, układu, indywidualnych materiałów oraz ręcznych otworów i obróbek. Ręcznie zmieniona geometria blokuje automatyczną przebudowę.

Narożnik jest punktem wyjścia konstrukcji. Nie dodaje automatycznie skośnych zawiasów ani potwierdzonego artykułu okucia. Fronty, konkretne mocowania i właściwe okucia należy dobrać do projektu. Nie wprowadzono domniemanych norm montażowych ani deklaracji wytrzymałości konstrukcji.

## Magnes

Domyślna tolerancja zbliżenia wynosi 80 mm. Magnes wybiera najbliższą możliwą parę powierzchni i sprawdza kolizje z pozostałymi meblami. Rozpoznaje też proste krawędzie wewnętrznego naroża L. Pusty narożnik nieregularnego korpusu lub blatu nie jest traktowany jako wypełniony prostopadłościan. Swobodny blat można dosunąć do górnej powierzchni korpusu.

## API backendu

```python
from workspace_worktops import (
    install_shared_worktop, fit_shared_worktop,
    refresh_shared_worktops, read_shared_worktop,
)

candidate, top_group = install_shared_worktop(
    project, selected_groups, thickness=38, overhang=20,
    material=material_id, name='Wspólny blat',
)
candidate = fit_shared_worktop(candidate, top_group)
source = read_shared_worktop(candidate, top_group)
# source['status'] == 'ready' albo 'needs_fit'
# source['reason'] zawiera powód braku dopasowania

from corner_furniture import (
    default_corner_config, normalize_corner_config, create_corner,
    read_corner_config, corner_status, corner_bounds,
)

config = default_corner_config(project)
config.update(shape='rounded', side='right', radius=100)
candidate, group = create_corner(project, config)
config = read_corner_config(candidate, group)
config['width'] = 1000
candidate, group = create_corner(candidate, config, group)
```

`refresh_shared_worktops(project, changed_groups=None, validate=True)` zwraca kandydata i nie zmienia wejścia. Brak wspólnych blatów nie powoduje dodatkowego kopiowania projektu. Stan i odwołania są zapisane w `Part.assembly['shared_worktop']`. Narożniki przechowują konfigurację oraz manifest geometrii w `Part.assembly['corner_source']`; `corner_bounds` podaje nominalne gabaryty w lokalnym układzie konstrukcji.

## Testy

`tests/test_workspace_worktops.py` sprawdza prawdziwe listwy, obrysy proste/L/U, nawis, zastąpienie własnych blatów, zapis/odczyt, odświeżanie wymiarów i nazwy, rozłączanie/usuwanie podpór, ręczne zmiany, kopie i kontrolę kolizji magnesu. `tests/test_corner_furniture.py` sprawdza odbicie lewe/prawe, pola obrysów i rzeczywistą objętość CAD, wymiary, trwałe ID, rozciąganie po obrocie, ochronę ręcznej geometrii, kopiowanie oraz wspólny blat narożnika i prostego korpusu. Zachowano regresje wcześniejszego układu i kreatora.
