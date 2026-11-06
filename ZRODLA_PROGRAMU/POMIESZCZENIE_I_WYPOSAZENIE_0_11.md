# Pomieszczenie i zakupowe wyposażenie — 0.11

Panel **Pomieszczenie** pozostaje obok projektu 3D. Dodaje pralkę, suszarkę, lodówkę, piekarnik, płytę grzewczą, okap, zmywarkę, umywalkę, zlew, kran, kabinę prysznicową, lustro, półkę zakupową, wieszak, lampę i żarówkę. To własne modele wyposażenia, oddzielne od części mebli.

## Dodawanie i ustawianie

Wybierz typ w **Dodaj z katalogu** i kliknij **+ Dodaj wyposażenie**. Nowy obiekt powstaje obok istniejącej sceny. Wybierz go na liście albo w 3D, ustaw nazwę, szerokość, głębokość, wysokość, pozycję XYZ, obrót Z, kolor i cenę. Zatwierdź **Enter** lub **Zastosuj do obiektu**. Przyciski ±90° obracają go wokół środka własnego gabarytu; **Usuń obiekt** usuwa wyposażenie. Zmiany można cofać historią projektu.

Wymiary są w mm, obrót w stopniach, cena w zł. Pozycja opisuje dolny przedni lewy narożnik gabarytu przed obrotem. Cena dotyczy jednej sztuki obiektu. **Widoczny w scenie** pozwala czasowo schować obiekt: nie usuwa go z zakupowego zestawienia ani z kosztu. Do usunięcia pozycji służy osobny przycisk.

Wyposażenie nie tworzy formatek, nie zmienia rozkroju ani automatycznych wierceń korpusu. Zakupowa półka z tego katalogu też pozostaje zakupowym obiektem. Własną półkę do wycięcia z materiału twórz jako część mebla w kreatorze lub szkicu.

## Pomieszczenie i światło

Lista obejmuje neutralne pomieszczenie, kuchnię, łazienkę, salon, sypialnię, garaż / warsztat i ogród. **Jasność** reguluje podgląd w zakresie 0,1–2. Przełącznik **LED mebli** steruje prezentacją oświetlenia zamontowanego w meblach, a **Lampy / żarówki** — światłem zakupowych obiektów.

Włączenie **Światło obiektu** dotyczy lampy albo żarówki. Wyłączenie światła pozostawia obiekt w scenie. Pozycje źródeł światła wynikają z położenia i obrotu obiektu. To wizualne oświetlenie podglądu, bez obliczeń fotometrycznych, doboru mocy, obwodów ani dokumentacji elektrycznej.

Scena i obiekty zapisują się w `.mebel` razem z projektem. Po cofnięciu zmiany lub wczytaniu projektu panel odświeża obiekty, pomieszczenie i przełączniki.

## Własny model a rzeczywisty artykuł

Katalogowe gabaryty są edytowalnymi wartościami demonstracyjnymi. Nie są kartą określonego producenta ani potwierdzonym wymiarem wnęki. Wybierz rzeczywisty artykuł i wprowadź jego gabaryty, wymagane luzy montażowe, wentylację i sposób podłączenia.

Dodanie płyty grzewczej, zlewu lub umywalki nie wycina automatycznie otworu w blacie. Taką obróbkę wykonaj na części produkcyjnej według szablonu konkretnego artykułu. Modele nie łączą hydrauliki, nie projektują instalacji i nie potwierdzają kompatybilności suszarki ustawionej nad pralką. Scena służy zaplanowaniu wyposażenia i wizualizacji.

## Gotowe przykłady

- `DEMO_SCENA_0_11.mebel`: dwa moduły (15 części produkcyjnych), frezowane fronty ramkowe, LED w rowku oraz dziewięć obiektów sceny: umywalka, kran, lodówka, piekarnik, płyta, okap, zmywarka, lampa i żarówka. Ten projekt jest przeglądem funkcji wyposażenia w jednej scenie.
- `DEMO_SCENA_KUCHNIA_0_11.mebel` i `PRZYKLADY/54_Scena_kuchnia_AGD.mebel`: trzy moduły meblowe, lodówka, zlew, kran, zmywarka, piekarnik, płyta, okap, lampa i żarówka.
- `DEMO_SCENA_LAZIENKA_0_11.mebel` i `PRZYKLADY/55_Scena_lazienka_wyposazenie.mebel`: szafka, umywalka, kran, lustro, prysznic, pralka, suszarka, półka zakupowa, wieszak i światła.

Ceny wyposażenia w tych projektach wynoszą 0 do własnego uzupełnienia. Pliki ilustrują rozmieszczenie; nie stanowią sprawdzonego projektu instalacyjnego ani zestawu zakupowego konkretnych producentów.

## API integracji

```python
Project.scene = {
    'room': 'neutral', 'brightness': 1.0,
    'led_enabled': True, 'lights_enabled': True, 'objects': []
}

validate_scene(scene) -> dict                    # kanoniczna kopia, bez mutacji wejścia
make_object(kind, **values) -> dict
add_object(project, kind, **values) -> Project  # kopia
update_object(project, object_id, **changes) -> Project
delete_object(project, object_id) -> Project
set_scene(project, **changes) -> Project
object_vertices(project, include_disabled=False) -> list[tuple]
object_bounds(obj) -> (lower, upper)
object_lights(project) -> list[dict]
inventory(project) -> list[dict]
purchased_total(project) -> float
room_world_faces(project, interactive=False) -> list[dict]
room_faces(project, camera, size, selected=None, *, interactive=False) -> list[dict]

RoomScenePanel(parent, app)
panel.refresh()
panel.select_object(object_id)
panel.rotate_selected(angle)
panel.delete_selected()
```

Obiekt ma `id/type/name/x/y/z/yaw/width/depth/height/color/enabled/price/light_enabled`. Identyfikator jest stały; domyślnie generowany lokalnie. Geometria ma identyfikator wyboru `scene:<id>` oraz flagi `scene_object=True` i `fitting=True`. Dane renderu zawierają `world`, `center`, `normal`, `points`, `depth`, `color`, `emissive` i `glass`.

Panel wymaga `app.project`, `app.snapshot()`, `app.refresh()` i `app.draw()`. `app.selected_scene` zawiera identyfikator obiektu, oddzielny od zaznaczonej części mebla. Odświeżaj panel po zmianie projektu, Undo i Load. Renderer może używać nominalnych narożników obiektów do dopasowania kadru, bez generowania CAD mesh. Lista zakupowa zawiera także czasowo ukryte obiekty.

Walidacja odrzuca nieznane typy i pola, powtórzone identyfikatory, niepoprawne kolory, nieboole'owskie przełączniki, ujemne lub nieskończone wymiary i nadmierną liczbę obiektów. Limit sceny wynosi 200 obiektów. Starszy projekt bez `scene` zachowuje pustą neutralną scenę.

