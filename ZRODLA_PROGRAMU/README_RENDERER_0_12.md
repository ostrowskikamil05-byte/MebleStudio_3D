# Renderer 0.12: widoczność, cache i podgląd ruchu

Naprawiono błąd z logu 0.11 po dodaniu 48. części. Cache kolorów używał
`id(record)`. Zimny render ruchu tworzył Geometry podglądu, a pierwszy pełny
render zastępował ten podgląd innym Geometry. Python mógł ponownie użyć
zwolnionego identyfikatora obiektu dla geometrii o innej liczbie powierzchni.
Pozostawała wtedy stara tablica kolorów i `colors[i]` zgłaszało IndexError.
Kolory są teraz własnością konkretnego Geometry. Liczby indeksów, normalnych,
środków i kolorów są sprawdzane razem. Zimny podgląd nie jest nadpisywany
podczas pierwszego pełnego renderowania.

Readonly reprodukcja na wydanej 0.11, z sześcioma szafkami, frezowanymi
frontami, LED i sześcioma dodatkowymi częściami, odtworzyła IndexError
w pierwszych trzech z trzech kroków. Ten sam scenariusz w 0.12 wykonał
24 kroki bez błędu. Test automatyczny wykonuje dodatkowo 200 zmian 48-częściowego
projektu, jasności, LED, wymiarów, kamery i przełączeń podglądu/pełnego widoku.

## Rzeczywista widoczność

`depth_raster.py` interpoluje głębokość powierzchni dla każdego piksela.
Większa głębokość Camera.project oznacza punkt bliżej kamery. Kolejność
średnich głębokości polygonów nie decyduje już o końcowym obrazie. Półka lub
bok za frontem zostają zakryte również podczas obracania. Rzeczywiste otwory
przelotowe i wklęsłe kontury zachowują otwarcie. Powierzchnie współpłaszczyznowe
mają stabilne rozstrzygnięcie identyfikatora, ograniczające migotanie.

Ten sam bufor służy do malowania, wskazywania elementu oraz widoczności
adnotacji. `pick_visible(canvas,x,y)` zwraca widoczny identyfikator.
`screen_visibility(canvas,x,y,depth=None,owner=None,tolerance=.8)` sprawdza
punkt przed rysowaniem etykiety, otworu lub uchwytu.

Po paint_faces Canvas udostępnia:

- `_depth_buffer`: float32, H×W, tło -inf;
- `_id_buffer`: int32, H×W, tło -1;
- `_depth_ids`: identyfikatory części i `scene:*`;
- `_depth_sample_scale`: rozdzielczość próbkowania podglądu;
- `_render_diagnostics`: informacje cold_geometry/interactive/faces.

Bufory podglądu są powiększane dokładnie tą samą metodą co obraz, dlatego
wskazywanie zgadza się z widocznym pikselem. W X-ray front nie zapisuje
głębokości powierzchni, a jego widoczny obrys zachowuje identyfikator.

## Szybkość i wygląd

Cache indeksowanej geometrii, transformacji, normalnych i zakupowych okuć
pozostaje niezależny od kamery. Przesuwanie AGD nie tworzy ponownie okuć
nieruchomych mebli. Oświetlenie LED jest liczone grupowo przez lokalny NumPy.
Światło mnoży odbicie materiału, dzięki czemu nasycone kolory nie zmieniają
się w pastel. Jasność sceny steruje ekspozycją.

Raster najpierw otrzymuje bliskie powierzchnie. Zakryte prostokąty są odrzucane
przed obliczeniem próbek. Wypukła powierzchnia jest rasteryzowana w jednym
przebiegu. Tylne powierzchnie CAD i okuć są usuwane z prawidłowym kierunkiem
normalnych, z uwzględnieniem zmiany orientacji płaszczyzny XZ.

Podczas ruchu prostokątne formatki ze ślepymi wierceniami i płytkimi rowkami
używają obrysu materiału. Dekoracyjne ramki i ryflowanie wracają po zakończeniu
ruchu. Kontury, wycięcia przelotowe, otwory użytkownika, zaokrąglenia i fazy
pozostają w geometrii podglądu. Automatyczne otwory montażowe do Ø8 mm,
w tym przeloty Ø4,5 mm pod śruby M4 uchwytów, są pomijane wyłącznie w ruchu.
Wcześniej mały przelot M4 wyłączał uproszczenie całego frontu, przywracając
tysiące powierzchni ryflowania. Pełny widok i produkcja zachowują te otwory.
Okucia używają osobnego zorientowanego pudełka dla każdego spójnego
komponentu; dwa odległe zawiasy nie są łączone w jedno wielkie pudełko. Emisyjna
taśma LED zachowuje swój kształt. Ziarno drewna i plamy światła na podłodze
wracają w pełnym widoku.

Duże standardowe formatki mogą mieć w pełnym widoku siatkę próbek światła
LED. Podgląd ruchu ma osobny rekord zwykłej formatki bez tych podziałów,
niezależnie od kolejności pełnego widoku i podglądu. Rozgrzanie geometrii
przez dopasowanie kamery nie przywraca już gęstej siatki LED podczas ruchu.
Obrys w ruchu powstaje jednym przebiegiem z widocznych identyfikatorów
bufora. Daje czytelne granice i zaznaczenie, zachowując identyczną widoczność
i wskazywanie; pełny widok rysuje także krawędzie poszczególnych powierzchni.

W ruchu duże sceny są próbkowane w 35% rozdzielczości i powiększane; małe
sceny w 60%. Po zakończeniu ruchu wraca pełna rozdzielczość. Formatki,
wiercenia, rozkrój i eksport CAD/CNC nigdy nie są zmieniane przez LOD.

## Pomiar i ograniczenia

Powtarzalne narzędzia robocze: `work/reproduce_viewport_cache_012.py`,
`work/benchmark_viewport_012.py` oraz rzeczywisty Tk
`work/benchmark_tk_0_12.py --new`; wyniki pomiarów są zapisane do JSON.
Benchmark rozdziela pierwszą klatkę od mediany kolejnych obrotów, ponieważ
pierwsze przygotowanie nowej bryły CAD może trwać znacznie dłużej.

Końcowy zapis `work/viewport_0_12_tk_after.json` dla sześciu szafek,
42 części i 42 rekordów okuć, przy 1300×720 pikselach, obejmuje malowanie
i rzeczywiste odświeżenie Tk. Po rozgrzaniu CAD, dla sześciu kątów obrotu:

| Tryb | Mediana klatki | Najwolniejsza klatka | Powierzchnie ostatniej klatki |
| --- | ---: | ---: | ---: |
| Pełny | 620,88 ms | 760,48 ms | 7944 |
| Ruch | 115,44 ms | 226,82 ms | 474 |

To około 8,7 klatki/s według mediany ruchu, bez gwarancji stałych 10 FPS.
Przed poprawkami podgląd miał 4422 powierzchnie i medianę około 285 ms.
Wyniki zależą od sprzętu i bieżącego obciążenia. Pierwsze przygotowanie proxy
może być wolniejsze niż kolejne obroty, a pełny z-buffer nadal jest
kosztowniejszy od poprzedniego malowania warstw, które błędnie pokazywało
przenikające powierzchnie. Wcześniejsze pomiary bez Tk pomijały przesłanie
obrazu i nie opisują końcowej szybkości aplikacji.

Jest to lokalny renderer programowy NumPy/Pillow, bez GPU i bez zewnętrznego
silnika. Poświata LED jest przybliżeniem wizualnym. Podgląd ruchu upraszcza
drobne cechy i ma niższą rozdzielczość; pełny widok zachowuje obróbkę bryły.
Pierwsza generacja wielu różnych frezowanych frontów nadal może wymagać
sekund pracy lokalnego jądra CAD.
