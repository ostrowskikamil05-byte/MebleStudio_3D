# Fronty, nogi i LED w kreatorze 0.10

Styl frontu zmienia rzeczywistą obróbkę elementu, którą zapisuje projekt i eksport geometrii CAD. `smooth` pozostawia gładki front; `frame` wybiera płytką kieszeń w środku i pozostawia obwód; `fluted` dodaje rozstawione pionowe rowki o końcach półkolistych; `finger` dodaje podłużną kieszeń chwytową przez istniejący mechanizm `opening_options`. Frezowany uchwyt nie staje się dodatkową płytą ani kupowanym uchwytem w kosztorysie: jego wpis parametryczny `opening` ma ilość 0.

To wymiary robocze użytkownika. Moduł nie przypisuje frezom, nogom ani LED niezweryfikowanego producenta. Zmiana stylu usuwa wyłącznie obróbki poprzednio utworzone przez ten moduł; ręczne rowki, kieszenie i wiercenia pozostają. Styl `finger` jest wyraźną zmianą sposobu chwytania i zastępuje wcześniejszy uchwyt / mechanizm otwierania tego frontu.

Głębokość dekoracji jest ograniczona do 45% grubości i pozostawienia co najmniej 6 mm materiału. Gdy kieszeń przecina rzut ślepego wiercenia od przeciwnej powierzchni, ograniczenie pozostawia dodatkowo 1 mm pomiędzy dnem wiercenia a kieszenią. To sprawdzenie geometrii, a nie potwierdzenie wytrzymałości konkretnego materiału. Przy małym froncie obwód ramki zmniejsza się do 1/4 krótszego wymiaru. Rowków jest najwyżej 32; jawna liczba, która nie mieści się przy wybranym rozstawie, powoduje czytelny błąd. Efektywne wymiary są zachowane w `Part.configurator_source['front_style']`.

Nogi są zakupowym `hardware.kind='leg'`. Jeden wpis zawiera ilość 4 albo 6, cenę sztuki, średnicę, wysokość i konkretne pozycje stóp w układzie konstrukcji. Widok rysuje okrągłe korpusy ze stopkami. Nogi nie zwiększają liczby formatek. Wysokość korpusu jest liczona nad nogami; całkowitą wysokość mebla wraz z nogami ustala `furniture_configurator`. Cokół jest prawdziwą płytą tworzona przez ten konfigurator i trafia do rozkroju. Mocowania nóg nie są automatycznie wiercone bez rzeczywistej karty artykułu.

LED pod półką tworzy rowek od powierzchni −T poziomej półki, a przy braku półek na spodzie wieńca. LED na boku tworzy pionowy rowek na powierzchni wewnętrznej: +T lewego boku albo −T prawego. Wpisy zakupowe obejmują taśmę, opcjonalny profil i dyfuzor, po rzeczywistej długości w metrach; własny zasilacz jest pozycją 1 szt. Dobór jego mocy i napięcia należy do konfiguracji artykułu. Rowek jest rzeczywistą operacją frezowania, a wizualizacja profilu i taśmy używa lekkiej geometrii zakupowej. Ponowne zastosowanie tych samych ustawień nie dubluje rowków ani kosztów.

## API modułu `configurator_accessories`

```python
normalize_front_style(style='smooth', cfg=None) -> dict
apply_front_style(project, part_id, style='smooth', config=None,
                  *, validate=True) -> Project
apply_front_to_part(project, part, style='smooth', config=None) -> Part
normalize_accessories(config=None) -> dict
install_accessories(project, group, config=None, bounds=None,
                    *, validate=True) -> Project
accessory_vertices(project, group=None, local=False,
                   *, include_all=False) -> list[tuple[float,float,float]]
```

Publiczne `apply_front_style` i `install_accessories` pracują na głębokiej kopii. Nieudana walidacja nie zmienia źródła. `apply_front_to_part` jest pomocnikiem dla budowania kilku frontów w kopii projektu; mutuje tę kopię, a caller musi sprawdzić cały projekt przed jego zatwierdzeniem. Najpierw utwórz wiercenia okuć i uchwytów, następnie zastosuj wzór frontu, aby ograniczenie głębokości widziało otwory.

`bounds` zawiera współrzędne konstrukcji: `{lower:(x,y,z_dna), upper:(x+w,y+d,z_dna+h), floor:z_podłogi}` albo `{body_x,body_y,body_z,width,depth,body_height,floor}`. `body_height` nie zawiera nóg. Bez `floor` podłoga wynosi `body_z-leg_height`. Jawny inny poziom podłogi jest odrzucany, gdy nie odpowiada wybranej wysokości nóg.

`accessory_vertices` domyślnie obejmuje tylko nowe nogi i LED, dzięki czemu dotychczasowy obrys mebla nie powiększa się o stare uchwyty. `include_all=True` obejmuje wszystkie rodzaje okuć. Domyślnie zwraca współrzędne zestawu po `Part.layout`; `local=True` odwraca końcowe ustawienie mebla i zwraca układ konstrukcji. Uwzględnia widoczność właściciela oraz opcjonalny filtr grupy. Nie generuje kosztownego CAD mesh płyt. `fitting_visuals.fitting_faces(project, kinds=None)` zachowuje dotychczasowe działanie, a `kinds` pozwala filtrować rodzaje geometrii.

| Opcje | Domyślne wartości |
|---|---|
| front_style | smooth / frame / fluted / finger |
| front_border, front_depth, front_radius | 60 / 3 / 6 mm |
| flute_width, flute_spacing, flute_count | 8 / 20 mm / 0 (automatycznie) |
| finger_length, finger_width, finger_depth | 120 / 24 / 6 mm |
| leg_height, leg_count, leg_diameter, leg_setback | 0 mm (wyłączone) / 4 / 40 / 60 mm |
| leg_price, leg_color | 0 zł/szt. / #242628 |
| led, led_mount, led_side | False / under_shelf / left |
| led_width, led_depth, led_margin | 12 / 3 / 30 mm |
| led_profile, led_diffuser | True / True |
| led_price_per_m, led_profile_price_per_m, led_diffuser_price_per_m | 0 zł/m |
| led_source, led_source_price, led_color | pusta nazwa zasilacza / 0 zł/szt. / #fff3bf |
| led_targets | None; opcjonalna lista identyfikatorów płyt |

Automatyczne operacje i okucia mają `config_auto=True` oraz `configure_feature='front_style'`, `'legs'` albo `'led'`. Tylko własne oznaczone wpisy są zastępowane. Zamiana grupy nie usuwa ręcznego wyposażenia innych grup.

Testy `tests/test_configurator_accessories.py` sprawdzają objętość rzeczywistej bryły CAD ramki i ryflowania, zachowanie ręcznych obróbek, ograniczenie głębokości, rozstaw i ilość nóg, koszt i metraż LED, brak powielania, widoczność, obroty/przesunięcia zestawu oraz niemutowalność źródła przy błędzie. Nie są testem fizycznej produkcji ani określonego zakupowego artykułu.
