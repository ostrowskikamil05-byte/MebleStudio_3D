# Opcjonalny renderer GPU 0.13

`gpu_viewport.py` używa lokalnego VTK/OpenGL. Istniejący Canvas Tk nadal
obsługuje zdarzenia, zakładki, odpinanie okna, wymiary i adnotacje. Renderer
ma zachowany kontekst oraz geometrię i odczytuje obraz z ukrytego framebuffer.
Nie modyfikuje części, brył CAD, operacji, rozkroju ani eksportu CNC.

## Integracja

`paint_gpu(canvas, faces=None, project=project, camera=camera, size=(w,h),
interactive=False, selected_ids=(), selected=None, explode=0, xray=False,
xray_ids=None, quality='balanced')` zwraca `ok`, `engine`, `drawn` i
`diagnostics`. Wywołanie przed `render_faces` unika pracy nad tysiącami
rzutowanych powierzchni CPU. `drawn` zawiera tanie kontury i reprezentatywne
powierzchnie dla rozmieszczania adnotacji. Nie jest dokładną siatką CNC.

`pick_world(canvas,x,y)` zwraca widoczny `id`, `kind` (`part`, `fitting`,
`scene`), `precise_surface`, `world` i `depth`. Montowanie okucia powinno
wymagać `kind=='part'`; okucie właściciela ma osobny identyfikator powierzchni.

Canvas udostępnia dotychczasowe `_depth_buffer`, `_id_buffer`, `_depth_ids`,
`_depth_sample_scale` i `_render_diagnostics`. Głębokość została przeliczona
do konwencji `Camera.view`: większa wartość oznacza bliższy punkt. Testy
sprawdzają pan, orbit, wskazywanie oraz dokładne dopasowanie obrazu i buforów.

`close_gpu(canvas)` zwalnia kontekst i może być wywołany wielokrotnie.
Zdarzenie zniszczenia Canvas również zamyka renderer. `ok=False` wraz z
komunikatem oznacza kontrolowany powrót do istniejącego renderera CPU.

## Zachowanie geometrii i widoczności

Grupy geometrii są zachowane w kilku aktorach VTK, z osobnymi kolorami
i identyfikatorami dla każdej komórki. Kamera nie przebudowuje siatki.
Oddzielne mappers obrazu i ID zachowują obie tablice kolorów w pamięci GPU,
zamiast przesyłać je ponownie podczas każdego przełączenia pass.
Cache przechowuje silne referencje do odpowiednich rekordów, więc identyfikator
zwolnionego obiektu nie może podszyć się pod żywą geometrię.

Odczyt dotyczy prostokąta sceny, a puste piksele poza nim mają znaną głębokość
i identyfikator tła. Ruch ma niższą rozdzielczość (`draft`: 45%, `balanced`:
55%, `high`: 75%). Obraz i ID są powiększane tą samą metodą nearest;
pełny balanced/high wraca do rozdzielczości okna.

X-ray ma półprzezroczyste powierzchnie. Wskazywanie preferuje nieprzezroczyste
wnętrze, a tam, gdzie pozostało tylko tło lub ściana/podłoga za duchem,
zachowuje najbliższy ghost element i jego głębokość. Cały zestaw pozostaje
zaznaczalny. Zakres clipping obejmuje architekturę, podczas gdy Dopasuj
pozostaje ograniczone do wyposażenia i mebli.

## Wygląd i ograniczenia

Balanced używa oświetlenia Phong, matowych i błyszczących wykończeń,
normalnych oraz dwóch stałych źródeł studyjnych. Do 12 próbek LED/lamp
oświetla faktyczną geometrię. Taśma LED ma powierzchnię emisyjną i respektuje
wyłącznik. Drewno i dekor drewniany mają subtelny shader usłojenia, stabilny
w przestrzeni sceny. To tekstura proceduralna, nie skan konkretnej płyty.

High dodaje PBR (roughness/metallic) i SSAO. W zweryfikowanym demo przy
jasności 1.0 wszystkie normalne miały długość około 1 i Lighting pozostał
włączony po ID pass. Zmiana SSAO dotknęła około 1333 piksele: efekt na tej
scenie jest niewielki. Dodatkowe przezroczyste plamy kontaktowe na podłodze
usunięto: w balanced przebijały przez fronty. Normalny widok zachowuje
nieprzezroczyste powierzchnie i właściwy bufor głębokości; osobny test porównuje
piksele szafki z podłogą i bez niej. Nie jest to ray tracing ani fotorealistyczna
symulacja cieni rzucanych i oświetlenia globalnego.

## Pomiary

W gotowej paczce Windows wynik pomiaru znajduje się w
`SPRAWDZENIE_WYDAJNOSCI.json`, a pełny wynik testów w
`SPRAWDZENIE_TESTOW.txt`. Ścieżki `work/` poniżej opisują zaplecze
deweloperskie, poza katalogiem programu.

Maszyna testowa: Intel HD Graphics 5500, OpenGL 4.4, VTK 9.6.2.
`work/benchmark_gpu_0_13.py` mierzy przygotowanie widoku, rzeczywiste
malowanie Canvas i jego odświeżenie, przy 1300×720. Raporty JSON oddzielają
pierwszą klatkę od ośmiu kolejnych kątów obrotu. Początkowa generacja różnych
frezowanych brył nadal może potrwać około 4 sekund, ponieważ wykorzystuje
lokalny kernel CAD.

Końcowe pomiary parowane wykonano sekwencyjnie na tym samym komputerze,
dla sześciu szafek, 42 części i 42 rekordów okuć, przy 1300×720:

| Tryb | CPU 0.12 | GPU 0.13 balanced | Stosunek czasu CPU/GPU |
| --- | ---: | ---: | ---: |
| Pełny widok, mediana 8 kątów | 573,24 ms | 114,43 ms | 5,01× |
| Podczas ruchu, mediana 8 kątów | 110,94 ms | 95,30 ms | 1,16× |

Pierwsza klatka GPU: 4058,57 ms z zimnym generowaniem CAD. Zachowana geometria
nie przebudowywała się podczas ośmiu pełnych obrotów. Pełny widok używał
4 aktorów materiałowych, 16 269 powierzchni; podgląd 948 powierzchni.
W pełnym widoku tło zajęło medianę 10,65 ms, GPU i obraz Canvas 89,27 ms,
a odświeżenie Tk 13,28 ms. Podczas ruchu było to 5,23/77,91/10,26 ms.
GPU render/readback/ID i kopiowanie obrazu pozostają istotną częścią czasu;
10 FPS nie jest gwarantowane dla dowolnej sceny.

Raport parowany: `work/viewport_paired_0_13_final.json`. Surowe źródła:
`work/viewport_cpu_baseline_for_0_13.json` oraz
`work/viewport_gpu_0_13_viewport_011_benchmark_project_balanced.json`.
Starszy wynik CPU 620,88/115,44 ms pozostaje historyczny; tabelę oparto
na nowym pomiarze, a nie na wcześniejszym wyniku. Usunięcie kontaktowych
plam nie zmienia tej fixture, ponieważ nie zawiera ona podłogi pokoju.

Demo z pokojem, 21 częściami i 12 rekordami okuć ma osobne raporty
`work/viewport_gpu_0_13_DEMO_SCENA_0_12_balanced.json` i `_high.json`.
Nie należy porównywać ich wprost z baseline innego projektu. Wynik jest
zależny od sterownika, sprzętu i obciążenia; wybór GPU nie gwarantuje stałych
10 FPS ani szybszego działania na każdym komputerze.

## Weryfikacja i źródła

13 testów GPU/math przeszło bez pominięć na maszynie testowej: zgodność
kamery, rzeczywista occlusion/depth/ID, przezroczysty cały zestaw, otoczenie
ze ścianami, kind okucia, zachowanie aktora podczas orbitu, jakość high,
normalne, tekstura, brak przebijania podłogowych nakładek przez szafkę
oraz idempotentne zamknięcie. Wynik zapisano w
`work/tests_gpu_final_0_13.log`. Render CPU nadal jest
samodzielnym fallbackiem.

Oficjalne API i przykłady:

- [VTK RenderWindow: odczyt RGBA i głębokości](https://vtk.org/doc/nightly/html/classvtkRenderWindow.html)
- [VTK SSAO pass](https://vtk.org/doc/nightly/html/classvtkSSAOPass.html)
- [VTK proceduralne shadery](https://examples.vtk.org/site/Cxx/Shaders/SpatterShader/)
- [VTK PBR](https://examples.vtk.org/site/PythonicAPI/Rendering/PBR_Skybox_Anisotropy/)

Wybór offscreen zachowuje obecne nakładki Canvas. Natywne okno potomne HWND
umieszczałoby OpenGL ponad nakładkami Tk, wymagając innej obsługi adnotacji,
wymiarów, hit testing i ponownego dokowania. Nie stosujemy WNDPROC subclass.
