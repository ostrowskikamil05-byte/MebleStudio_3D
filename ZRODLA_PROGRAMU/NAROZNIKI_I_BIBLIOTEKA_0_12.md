# Narożniki i nowe typy mebli 0.12

W bocznym panelu **Narożnik** wybierz szafkę i użyj **Zamień wybrany**, albo dodaj osobny narożnik. Wymiary i kierunek aktualizują model po krótkiej przerwie w pisaniu. Dolne przyciski są stale widoczne; parametry można przewijać. Zapis/druk najpierw przelicza poprawny szkic parametrów. Nieprawidłowe wymiary zatrzymują tę czynność i są objaśnione w panelu.

Wysokość oznacza cały mebel wraz z nogami; wysokość korpusu to wysokość całkowita minus wysokość nóg. `arm_depth` jest głębokością ramienia biegnącego wzdłuż X, `arm_depth_y` szerokością drugiego ramienia. Gabaryty X/Y obejmują korpus. Fronty i uchwyty mogą wystawać poza korpus. Domyślne rozmiary to własne edytowalne projekty biblioteki, a nie deklarowane normy producenta.

Narożnik L może mieć dwa niezależne fronty, każdy z osobnym bokiem zawiasowym. Otwieranie, puszki i prowadniki używają rzeczywistej orientacji tego frontu i jego boku. To nie jest zestaw drzwi składanych: mechanizm narożny typu bi-fold wymaga osobnego doboru okucia. Wzorce 35/45, 35/52 i Sensys 9071205 mają informacje źródłowe z katalogu oraz flagę weryfikacji produkcyjnej. Automatyczny prowadnik jest roboczym wzorcem montażowym, który trzeba zweryfikować względem konkretnego okucia.

Narożnik ścięty ma rzeczywistą formatkę frontu skośnego, jeśli wybrano ten wariant. Program nie zgaduje dla niego zawiasu ani wierceń i pozostawia otwieranie wyłączone. Kąt ścięcia 15–75° i długość są edytowalne. Narożnik zaokrąglony ma otwarty obrys z rzeczywistymi półkami/dnem/górą. Krzywa jest jawnie aproksymowana 4–24 segmentami, widocznymi w konturze CNC; nie jest frezowanym łukiem G2/G3.

Konwersja zwykłej szafki/szafy chroni ręcznie zmienioną geometrię, dodatkowe części, ręczne wiercenia/frezowania i okucia. Dla takich mebli trzeba dodać osobny narożnik. Nie konwertuje skrzynek szuflad. Dopasowanie wspólnych elementów zachowuje identyfikatory boków, dna, góry i pierwszych drzwi/półek, nazwę grupy i obrót/przesunięcie całego mebla. Zmiana wymiarów lub lewej/prawej strony istniejącego narożnika zachowuje identyfikatory i ręczne okucia, o ile dana część nadal istnieje. Jawne **Dopasuj** przywraca geometrię wybranego elementu do parametrów narożnika.

W bibliotece zachowano wszystkie dotychczasowe indeksy 0–33 i dodano indeksy 34–51: słupek pod piekarnik ze schowkami, szafkę zlewową na listwach, dwa warianty szafek wiszących, otwarty moduł pod urządzenie, szafę trzykomorową, niską i wysoką szafkę na obuwie, komodę na bieliznę, dwukomorowy otwarty regał, kontenerek biurowy, szafkę na segregatory, szafę warsztatową trzykomorową, szafkę górną garażową, komodę warsztatową, szafkę pralniczą na listwach, wąski regał łazienkowy oraz wiszącą zabudowę łazienkową. Są to realne, parametryczne płyty/korpusy, fronty i szuflady; wnęka pod AGD wymaga sprawdzenia wymiarów oraz wentylacji konkretnego urządzenia.

## API dla integracji

```python
default_corner_config(project=None, furniture_type='cabinet') -> dict
normalize_corner_config(config, project=None) -> dict
read_corner_config(project, group) -> dict | None
corner_status(project, group) -> dict
create_corner(project, config, group=None) -> (candidate, group)
convert_to_corner(project, group, shape='L', side='left',
                  arm_depth=None, width=None, depth=None,
                  fronts='orthogonal', **changes) -> (candidate, group)
regenerate_corner_hinges(project, part_id, pattern='hinge45', count=2,
                        price=0, manual=False, validate=True) -> candidate
```

Funkcje zwracają kopię kandydata i nie modyfikują projektu wejściowego. `create_corner(..., group)` wymaga niezmienionej ręcznie geometrii źródłowej. `fronts` przyjmuje `open`, `orthogonal` (tylko L) lub `diagonal` (tylko ścięty). `side` przyjmuje `left`/`right`; `shape` przyjmuje `L`/`chamfer`/`rounded`. `corner_front_frame` opisuje układ lokalny frontu z jawną bazą transformacji; nie jest prostokątnym globalnym korpusem. `CornerPanel(parent, app).refresh()` odświeża wybór, `flush_draft()` przelicza oczekującą zmianę i zwraca `False` dla błędnego projektu parametrów.
