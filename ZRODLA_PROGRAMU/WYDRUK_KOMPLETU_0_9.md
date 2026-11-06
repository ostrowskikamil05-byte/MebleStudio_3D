# Wydruk całego zestawu mebli

Dokumentacja jest jednym samodzielnym plikiem HTML. Otwórz go w przeglądarce i wybierz **Drukuj / zapisz PDF**. Format: **A4, pionowo**. Wyłącz nagłówki i stopki przeglądarki; włącz drukowanie tła, aby zachować kolory rysunków. Dokument zawiera własne style i rysunki SVG, więc działa bez internetu. W aktualnych przeglądarkach Chromium druk zawiera numer strony i liczbę wszystkich stron.

Komplet obejmuje:

- cały zestaw z góry i z przodu oraz osobny większy widok 3D, z wymiarami zamkniętego zestawu;
- każdą szafkę lub inną grupę osobno, rozstrzeloną, z numerami oraz wymiarami U × V × T wszystkich części;
- tabelę elementów: materiał, sztuki, wymiary gotowe i do cięcia, grubość oraz obrzeża;
- rysunek U/V każdej części, tabelę wierceń, frezowania i opis obróbki kantów;
- listę okuć i wyposażenia;
- arkusze rozkroju z numerami elementów, położeniem na arkuszu, wymiarami i obrotem;
- listę elementów, które nie mieszczą się w zdefiniowanym półfabrykacie.

Odległości rozstrzelenia służą jedynie czytelności widoku. Nie zmieniają wymiarów części ani danych rozkroju. Każda część w projekcie jest jedną sztuką i otrzymuje stały numer według kolejności części w projekcie. Ukryte elementy również są uwzględnione w dokumentacji produkcyjnej. Dokument przedstawia zapisany, zamknięty montaż; otwarcie drzwi na ekranie nie zmienia wydruku.

Widoki całego zestawu zachowują ustawienie mebli w przestrzeni. Widok rozstrzelony pojedynczej szafki jest ustawiony w jej własnych osiach konstrukcyjnych, więc obrót szafki na planie nie zamienia jej szerokości i głębokości. Gabaryty zestawu są zasięgiem obrysów jego zamkniętych części, a nie sumą szerokości poszczególnych szafek.

Schematy 3D pokazują nominalne bryły konstrukcyjne. Otwory i kieszenie są oznaczone na rysunkach U/V oraz opisane w tabelach; fazy i zaokrąglenia mają opis operacji. Do dokładnej geometrii tych obróbek służy eksport STEP. Nieregularne obrysy są pakowane w rozkroju jako prostokątne półfabrykaty, zgodnie z dotychczasowym przybliżonym algorytmem gilotynowym. Wydruk jest dopasowany do strony, nie do skali 1:1.

# API dla integracji

```python
from project_print import export_project_print

info = export_project_print(project, "Dokumentacja.html")

# Dokumentacja jednej wybranej grupy:
info = export_project_print(project, "Szafka.html", group="Szafka dolna")

# Krótszy komplet: zestaw, szafki i zestawienie, bez stron części i rozkroju:
info = export_project_print(
    project, "Prezentacja.html",
    include_parts=False, include_cut_plan=False, explosion=0.35,
)
```

Sygnatura:

```python
export_project_print(
    project, path, *, group=None,
    include_cut_plan=True, include_parts=True, explosion=0.35,
) -> dict
```

`group` to dokładna nazwa grupy lub `None` dla całego zestawu. `explosion` jest współczynnikiem od 0 do 2; wartość 0 zachowuje zamknięty układ. Ponad 20 części jednej grupy otrzymuje kolejne strony widoku rozstrzelonego z osobnymi partiami opisów, aby wszystkie numery pozostały czytelne.

Wynik:

```python
{
    "path": "C:\\...\\Dokumentacja.html",  # ścieżka bezwzględna
    "groups": ["Szafka dolna", "Słupek"],
    "parts": 22,                           # liczba części
    "sheets": 4,                           # arkusze rozkroju
    "unplaced": [],                        # ID nieumieszczonych części
    "sections": 32,                        # sekcje HTML; nie fizyczne strony PDF
}
```

Przeglądarka może przenieść długą tabelę na dodatkową stronę, więc `sections` nie jest liczbą stron PDF. Funkcja pracuje na głębokiej kopii projektu; nie zmienia konturów, materiałów, widoczności, montażu, pozycji ani historii cofania. Rozkrój korzysta z nominalnej kopii części z tymi samymi U/V/T, obrzeżami, kierunkiem włókien i parametrami półfabrykatu. Eksporter nie ładuje ani nie oblicza siatek CAD dla schematycznych widoków.

Testy `tests/test_project_print.py` sprawdzają wymiary przy zmianie rozstrzelenia, niezmienność źródła, wymiary cięcia i rozdzielenie grubości materiału, ustawienie przestrzenne, wybór jednej grupy, kompletność opisów dużej grupy, escaping HTML, części nieumieszczone w rozkroju oraz brak wywołań siatki i walidacji obróbek CAD.
