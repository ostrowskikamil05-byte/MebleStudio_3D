# Parametryczny kreator mebli 0.10

Kreator przebudowuje korpus, półki, podziały, drzwi, szuflady i powiązane okucia po zmianie parametrów. Obsługuje biblioteczne korpusy, regały, komody i wyspy. Stoły, łóżka, zabudowy cargo i pozostałe typy nadal są dostępne w pełnej bibliotece; nie są automatycznie konwertowane do tego korpusu.

## Wymiary i strefy

Wysokość w konfiguracji obejmuje korpus, nogi oraz włączony blat. Korpus zaczyna się nad nogami. Szerokość i głębokość opisują korpus; grubość frontów, plecy nakładane i wystający blat zwiększają rzeczywisty obrys zestawu.

Można włączyć dolną i górną strefę oraz ustawić ich wysokości. Główna strefa zajmuje pozostałą wysokość korpusu. Każda strefa ma 1–4 kolumny, 0–8 półek, front otwarty, 1–2 drzwi na kolumnę albo 1–8 szuflad na kolumnę. Strefa szuflad nie zawiera jednocześnie półek. Przegrody są rzeczywistymi formatkami. Drzwi, półki i szuflady odnoszą się do swojej komory, a zawiasy i prowadnice do odpowiednich boków komory.

Rozciąganie całego skonfigurowanego mebla w 3D korzysta z tej samej przebudowy. Zachowuje ustawienia kreatora, nogi, LED i obrót mebla. Przy przeciąganiu dolnej/minimalnej powierzchni zmienia się lokalny początek konstrukcji; przeciwna powierzchnia pozostaje na swoim miejscu. Wewnętrzne API rozciągania otrzymuje wysokość korpusu i przelicza ją na całkowitą wysokość widoczną w kreatorze.

## Zachowanie ręcznych zmian

Przebudowa zachowuje identyfikatory istniejących części odpowiadających tym samym funkcjom, układ w pomieszczeniu, nazwy części, widoczność, kierunek usłojenia, oklejanie, indywidualny materiał, ręczne otwory i obróbki oraz ręcznie dodane części i okucia. Zmiana wybranego materiału korpusu/frontów aktualizuje tę kategorię; przy samym wymiarowaniu indywidualne materiały pozostają. Ceny, dostawca i notatki okuć pozostają, natomiast zmiana modelu prowadnicy nie przenosi starego numeru artykułu na nowy produkt.

Zmiana nazwy mebla aktualizuje grupę wraz z odniesieniami. Istniejąca nazwa innego mebla powoduje komunikat błędu. Jeżeli nowa konstrukcja koliduje z ręcznym dodatkiem albo usuwa część z ręczną obróbką, przebudowa zostaje odrzucona. Ręczna zmiana wymiarów, położenia, obrysu lub profilu zarządzanej części blokuje kolejną przebudowę, aby nie nadpisać tej geometrii. Operacje wygenerowane automatycznie przez kreator są podczas przebudowy wyliczane ponownie; własną obróbkę należy dodawać jako ręczną operację.

Konwersja starszego mebla jest jawna. Program rozpoznaje niezmieniony wzorzec biblioteki i zachowuje identyfikatory części. Zmieniona konstrukcja, dodatkowe części lub własne operacje uniemożliwiają rozpoznanie zamiast powodować ich usunięcie.

## Okucia i akcesoria

Zawiasy korzystają z wybranego wzoru wierceń, prowadnice z wybranego profilu i długości nominalnej. Magnetyczny TIP-ON wymaga zadeklarowanych zawiasów bez sprężyny; konkretnego zawiasu Sensys ze sprężyną nie można potraktować jako bezsprężynowego. Ogólny wzór puszki nie jest doborem kompletnego zakupowego artykułu zawiasu.

Nogi są okuciami zakupowymi z ilością i pozycjami, a cokół rzeczywistą formatką. LED obejmuje obróbki oraz metraż taśmy/profilu/dyfuzora i opcjonalny wpis zasilacza. Front frezowany nie dodaje równocześnie standardowego metalowego uchwytu. Szczegóły geometrii i API tych dodatków opisuje [AKCESORIA_KREATORA_0_10.md](AKCESORIA_KREATORA_0_10.md).

## API

```python
from furniture_configurator import (
    default_config, normalize_config, read_config,
    configuration_status, infer_config, configure,
)

config = default_config(template_index=0, project=project)
candidate, group = configure(project, config)  # nowy mebel

config = read_config(candidate, group)
config['width'] = 900
updated, group = configure(candidate, config, group)  # przebudowa

status = configuration_status(updated, group)
```

`configure` zwraca nowy projekt i rzeczywistą nazwę grupy; nie zmienia przekazanego projektu. Wyjątek nie pozostawia częściowej konstrukcji. `origin=` jest opcjonalnym lokalnym początkiem podłogi `[x,y,z]`, używanym przy zmianie minimalnej powierzchni; zwykła przebudowa zachowuje dotychczasowy początek. Obrót i przesunięcie całego mebla są zachowane osobno w `Part.layout`.

`read_config` zwraca kopię ustawień albo `None` dla starszego/swobodnego mebla. `configuration_status` zwraca `configured`, `editable` i `reason`. `infer_config` rozpoznaje niezmieniony starszy wzorzec, ale niczego nie zapisuje; konwersję wykonuje dopiero jawne `configure(project, inferred, group)`.

`Part.configurator_source` przechowuje semantyczny klucz części. Lewy bok korpusu przechowuje dodatkowo konfigurację i manifest identyfikatorów/sygnatur geometrii. Dzięki temu przebudowa, zapis/odczyt i kopiowanie całego modułu mogą zachować powiązania komór.

## Weryfikacja

`tests/test_furniture_configurator.py` sprawdza atomową przebudowę, wymiary całkowite, komory, zawiasy, prowadnice i otwieranie po obrocie, stabilne identyfikatory, ręczne dodatki, materiały, ceny, konwersję starszych wzorców, zmianę nazwy, zapis/odczyt i synchronizację rozciągania 3D z konfiguracją.
