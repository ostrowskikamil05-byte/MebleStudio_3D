# Przeciąganie okuć i oświetlenia — 0.12

Rozwiń pasek **Przeciągnij okucie / LED** przy widoku 3D. Wybierz parametry, przytrzymaj kafelek i przeciągnij go na element mebla. Możesz też kliknąć kafelek, a następnie kliknąć miejsce montażu w 3D. **Esc** lub **Anuluj** wyłącza narzędzie. Zielony obrys pokazuje dopasowany dodatek; czerwony komunikat wskazuje, dlaczego montaż jest niedostępny. Dodanie tworzy jeden krok historii, więc można je cofnąć.

Przycisk **Parametry ▾** rozwija katalogi i ustawienia w tym samym pasku. Domyślnie są zwinięte, aby pozostawić więcej miejsca na widok mebla.

Montaż wykonuj przy zamkniętych mechanizmach i wyłączonym widoku rozstrzelonym. Przed zatwierdzeniem propozycja jest tylko podglądem i nie zmienia projektu.

## Zawiasy, prowadnice i uchwyty

- **Zawiasy** dodawaj do drzwi połączonych z korpusem. Wybierz wzór puszki, liczbę i cenę jednego zawiasu z prowadnikiem. Program zastępuje dotychczasowy komplet, tworzy rzeczywiste otwory puszki i mocowań oraz łączy je z odpowiednim bokiem. Zachowuje oznaczenia źródłowe i robocze istniejącego katalogu. Dla dwóch niezależnych prostych frontów narożnika używa właściwej strony frontu i boku. Skośny front wymaga osobnego doboru mechanizmu.
- **Prowadnice** przeciągaj na front zarządzanej szuflady. Wybierz profil i nominalną długość; puste pole oznacza 400 mm. Cena dotyczy pary. Program przelicza skrzynkę i odstęp na stronę według wybranego profilu, uwzględnia dostępną głębokość i pozostawia dodatki skrzynki. Katalog planowania nie generuje nieudokumentowanych otworów sprzęgieł, zaczepów ani wycięć danego producenta. Wariant wymagający potwierdzonej głębokości dobierz w pełnym kalkulatorze prowadnic.
- **Uchwyt** dodawaj na szerokiej powierzchni elementu. Własny model ma domyślny rozstaw 128 mm i rzeczywiste otwory przelotowe M4. Pozycja pochodzi z trafienia kursora; za blisko krawędzi montaż będzie odrzucony. Cena dotyczy jednej sztuki.

## LED na wybranej powierzchni

**Powierzchnia kursora** wybiera widoczną stronę elementu. Możesz jawnie wskazać +T, −T lub jedną z czterech prostych krawędzi. Kierunek i długość dopasowują się do powierzchni: pusta długość oznacza długość dostępną po odjęciu marginesów końcowych. Szerokość i cena taśmy są edytowalne; cena jest podana za metr. Położenie poprzeczne wynika z kursora i jest ograniczone do szerokości powierzchni.

Domyślny montaż jest powierzchniowy, **bez rowka**. **Rowek LED** dodaje rzeczywistą obróbkę frezarską od +T lub −T, z kontrolą konturu, grubości oraz kolizji z wierceniami. Na krawędzi dostępny jest montaż bez rowka. Na nieregularnym konturze szerokiej powierzchni LED jest skracany do miejsca montażu; montaż na jego nieregularnej krawędzi wymaga ręcznego doboru.

Taśma, profil i dyfuzor są pozycjami zakupowymi w metrach, powiązanymi z właścicielem. Ręczny LED zachowuje się podczas przebudowy tego elementu; długość automatyczna przelicza się ponownie. Usunięcie właściciela usuwa związane wyposażenie. Nie powstaje dodatkowa formatka w rozkroju.

Model LED jest własnym modelem planistycznym. Dobierz rzeczywisty profil, moc, napięcie, zasilacz, sposób mocowania i przewody. Program nie wykonuje obliczeń elektrycznych ani fotometrycznych.

## Lampy i żarówki

Kafelek lampy lub żarówki dodaje zakupowy obiekt w trafionym miejscu sceny lub na powierzchni mebla. Po dodaniu jest zaznaczony i gotowy do przesuwania; jego gabaryty, pozycję, kolor i cenę można dopracować w **Pomieszczeniu**. Jest to wyposażenie sceny, bez części produkcyjnych i automatycznych otworów mocowania.

## API

```python
preview_attachment(project, kind, target_id=None, local_hit=None, options=None)
# dict: valid/message/world_faces/length/kind/target_id/local_hit/options
# LED preview: tylko lekka geometria, bez CAD i walidacji całego projektu.

attach(project, kind, target_id=None, local_hit=None, options=None)
# nowa kopia Project; po błędzie źródło pozostaje zachowane

refresh_manual_attachments(candidate, changed_ids=None)
# mutuje wyłącznie przekazaną kopię, przed ostatecznym validate

HardwarePlacementPanel(parent, app)
app.hardware_drag.begin(kind, options=None, event=None)
app.hardware_drag.move(event)
app.hardware_drag.end(event)
app.hardware_drag.target_click(event)
app.hardware_drag.cancel()
app.hardware_drag.draw_ghost()
```

Picker aplikacji zwraca `{'target_id': id, 'local_hit': {'u': U, 'v': V, 'w': W, 'face': face}}`. Światło może użyć `target_id=None` i `local_hit={'world': (X,Y,Z)}`. Współrzędne zdarzenia są przeliczane z `x_root/y_root` do właściwego canvas, również podczas przeciągania poza pasek.

Ręczne rekordy mają `attachment_id`, `manual=True` i `config_auto=False`. LED dodatkowo zapisuje lokalny środek, bazę powierzchni i proporcje miejsca montażu. Podgląd jest ograniczony do 30 odświeżeń na sekundę i pamięta najwyżej 64 propozycje w obrębie tej samej referencji projektu. Dokładne trafienie oraz walidacja końcowa wykonywane są przy upuszczeniu.
