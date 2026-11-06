# MebelStudio3D 0.10 — korzystanie z kreatora

Kreator pracuje w stałym panelu **Mebel**, obok projektu 3D. Wybór układu frontów, podziałów, materiałów i wyposażenia zmienia ten sam model, z którego powstają formatki, zestawienie okuć i dokumentacja. Ręczny szkic i szczegółowe operacje CAD pozostają dostępne w panelu **Elementy / CAD** i na pasku **Szkic / CAD**.

## Pierwszy projekt

Rozpakuj cały pakiet Windows i uruchom `MebelStudio3D.exe` albo `URUCHOM.bat`. Zachowaj `_internal` obok programu. Python i CAD są wbudowane; nie trzeba instalować pip ani zewnętrznego CAD. Projekty zapisujesz jako `.mebel`.

Otwórz `DEMO_KREATOR_0_10.mebel`, aby zobaczyć działający zestaw: dwa fronty szuflad, szafkę z ramką MDF i LED, front ryflowany oraz szafę z trzema strefami i dwiema środkowymi komorami. Wszystkie cztery meble mają zapisane ustawienia kreatora. Kliknięcie elementu lub wybór mebla w górnej liście panelu przywołuje jego parametry.

Aby zacząć własny mebel, wybierz typ, ustaw wymiary i kliknij **+ Nowy**. Szybki kreator obsługuje korpusy, regały, komody i wyspy. Pozostałe konstrukcje — m.in. krzesła, stoły, łóżka i meble ogrodowe — dodasz przez **Biblioteka wszystkich mebli** albo **Kształty i style**.

## Zmiany bez otwierania kolejnych formularzy

- Przyciski **Otwarty**, **Drzwi**, **2 szuf.**, **3 szuf.**, **4 szuf.** zmieniają główną strefę. Przy wyborze szuflad program wyłącza półki tej strefy.
- Listy wyboru i przełączniki zastosują nowe ustawienie bezpośrednio.
- Liczbę zatwierdź **Enter** albo wyjdź z pola; przycisk **Zastosuj do mebla** zatwierdza cały formularz.
- **Ctrl+Z** cofa zmianę całego mebla, **Ctrl+Y** ją ponawia, **Ctrl+S** zapisuje projekt.
- Nieprawidłowe ustawienie pojawia się w opisie pod panelem. Dotychczasowy projekt pozostaje zachowany; popraw wartość i zatwierdź ponownie.

Parametry podzielono na rozwijane sekcje. Przyciski paska **Fronty i okucia** przewijają panel od razu do właściwej sekcji: frontów, zawiasów i prowadnic, nóg, materiałów albo LED.

## Wymiary, strefy i komory

**Wysokość całkowita** obejmuje nóżki i opcjonalny blat. Przykład: wysokość 860 mm, nogi 100 mm i blat 28 mm dają korpus o wysokości 732 mm. Głębokość i szerokość w kreatorze opisują korpus; grubość frontu, pleców oraz nawis blatu mogą zwiększyć zewnętrzny gabaryt zestawu.

Mebel ma główną strefę oraz opcjonalną **Strefę dolną** i **Pawlacz / strefę górną**. Wysokości dolnej i górnej strefy odejmują się od dostępnej wysokości; główna otrzymuje pozostałą część. **Komory pionowe** dodają przegrody. W każdej strefie możesz wybrać otwarte wnętrze, drzwi albo szuflady, liczbę frontów i półek na komorę.

Okucia drzwi i szuflad korzystają z boków oraz przegród swojej komory. Zmiana szerokości przelicza skrzynki szuflad i luzy dla wybranego profilu prowadnic. Długość prowadnicy jest osobnym ustawieniem; jeśli nowa głębokość korpusu jest zbyt mała, program odrzuci zmianę zamiast wygenerować wystającą skrzynkę.

Styl frontów można wybrać osobno dla każdej strefy; strefa może też korzystać z globalnego ustawienia mebla.

## Fronty i sposoby chwytania

| Styl w programie | Co powstaje |
|---|---|
| Gładki | Płyta bez automatycznej dekoracji frezowanej |
| Ramka frezowana | Płytka kieszeń w środku pojedynczej płyty; pozostaje obwód i zaokrąglone narożniki kieszeni |
| Ryflowany | Rzeczywiste pionowe rowki, z końcami półkolistymi |
| Chwyt frezowany | Podłużna kieszeń chwytowa we froncie; bez dodatkowego metalowego uchwytu |

Ramka jest frezowanym frontem MDF. Nie tworzy osobnych ramiaków, połączeń stolarskich ani wkładu szklanego. Wzór nie zwiększa liczby formatek. Głębokości są ograniczone grubością materiału; program uwzględnia też istniejące wiercenia od przeciwnej powierzchni. W małych frontach szerokość ramki może zostać ograniczona do dostępnego miejsca.

**Otwieranie / chwyt** umożliwia uchwyt, gałkę, brak uchwytu i dostępne warianty profilu krawędziowego, frezowania lub wypychacza. Wypychacz wymaga zgodnych zawiasów oraz konkretnego artykułu; stan sprężyny określasz w sekcji okuć. Wymiary frezowanego chwytu są własną konfiguracją warsztatową.

## Nóżki, cokół, blat i LED

W sekcji **Nóżki, cokół i blat** ustawisz wysokość, 4 albo 6 nóg, średnicę i odsunięcie od krawędzi. Wysokość 0 wyłącza nogi. Są zakupowymi komponentami: mają konkretne pozycje, ilość i koszt, ale nie trafiają do rozkroju płyt. Cokół i blat są rzeczywistymi częściami produkcyjnymi. Cokół wymaga dodatniej wysokości nóg.

**LED i rowki** dodaje rowek pod półkami, a przy braku półek pod wieńcem. Taśma, profil i opcjonalny dyfuzor są zestawiane według długości w metrach. Powtórzenie ustawienia zastępuje poprzednie automatyczne dodatki. Rowek ma geometrię CAD; światło w podglądzie jest uproszczonym paskiem, bez obliczeń fotometrycznych.

Nogi, rowki i profil LED w tej sekcji są modelami własnymi, a nie pełną kartą zakupową producenta. Dobierz rzeczywisty artykuł, mocowanie, nośność, wymiary profilu oraz zasilanie. Ceny w przykładach służą pokazaniu kosztorysu; część okuć ma cenę 0 do własnego uzupełnienia.

## Podgląd, otwieranie i układ zestawu

**Dopasuj** ustawia kadr na widoczny model wraz z nogami i wyposażeniem. **X-ray** pozwala obejrzeć wnętrza bez zmieniania części produkcyjnych. **Rozstrzelony** i **Wymiary** to niezależne opcje: wymiary możesz pokazać lub schować również w widoku rozstrzelonym.

Na pasku **Szkic / CAD** wybierz **Rozciągaj mebel**, aby zmieniać gabaryty całego mebla w 3D. Konfiguracja kreatora, strefy, skrzynki i części zależne są przeliczane razem. Włączenie **Krok 32 mm** nadaje skok regulacji. Ręczna edycja pojedynczej części w **Elementy / CAD** nadal jest osobną zmianą konstrukcji.

Zaznacz drzwi albo front szuflady i użyj suwaka **Otwieranie**. **Cały mebel** obejmuje fronty zaznaczonego mebla; **Zamknij** przywraca pozycję zamkniętą. Otwieranie służy prezentacji. Eksport produkcyjny i wydruk używają zamkniętej konstrukcji.

W trybie **Wybór / obrót** przeciąganie lewym przyciskiem obraca kamerę, prawym przesuwa kadr, a rolka przybliża. Pasek **Układ** → **Przesuwaj meble** zmienia położenie całej grupy; **Shift** przesuwa w pionie. Dostępne są obroty ±90°, magnes i **Łączenie i pozycja…**. Magnes ścian działa dla obrotów będących wielokrotnością 90°. Dosunięcie nie dodaje automatycznie łączników między meblami.

## Własne szkice i ochrona ręcznej konstrukcji

Szkic tworzy element o wybranej roli: bok, góra, dół, półka, blat, drzwi, szuflada albo część swobodna. Zapisany element używa tego samego projektu co 3D, formatki i eksport. Edytory otwierają się w zakładkach; uchwyt **⠿** odłącza je do nowego okna i pozwala wrócić na pasek.

Przebudowa kreatora zachowuje zgodne ręczne otwory, obróbki i własne dodatki. Ręczna zmiana wymiaru, konturu lub lokalnego położenia wygenerowanej części może wyłączyć kreator dla tego mebla. To ochrona własnej konstrukcji przed nadpisaniem. Użyj zwykłych edytorów albo nowego mebla; **Ctrl+Z** pozwala wrócić do stanu sprzed zmiany. Usunięcie części, do której dodano ręczną obróbkę, również może zostać odrzucone.

Przycisk **Przejmij mebel do kreatora** dotyczy rozpoznawalnych korpusów ze wcześniejszej biblioteki. Nie dowodzi, że dowolny własny szkic można bezpiecznie odtworzyć jako standardową szafkę.

## Presety i kopie

**Kopia** powiela cały mebel. Sekcja **Presety i kopiowanie ustawień** zapisuje konfigurację pod własną nazwą oraz przenosi ustawienia do innego mebla. Zapisane projekty `.mebel` nadal pozostają głównymi plikami dokumentacji. Presety są lokalną biblioteką ustawień użytkownika.

## Wydruk i produkcja

Zakładki **Formatki**, **Kosztorys** i **Rozkrój** odzwierciedlają aktualny projekt. Wymiary gotowe oraz do cięcia są rozdzielone; cięcie uwzględnia zadane obrzeże. Rozkrój jest przybliżony, gilotynowy. Kontury nieregularne zajmują prostokątne półfabrykaty, a oznaczenie włókien wpływa na dopuszczony obrót na arkuszu.

**Produkcja** → **Drukuj komplet…** zapisuje samodzielny HTML z rzutami całego zestawu, rozstrzelonymi szafkami, wymiarami wszystkich części, obróbkami, okuciami i rozkrojem. Otwórz dokument w przeglądarce i wybierz druk albo PDF: format **A4**, skala **100%**. Rysunki są dopasowane do strony, nie w skali 1:1. Rozstrzelenie nie powiększa podanych wymiarów fizycznych. Wydruk uwzględnia również ukryte części.

`DOKUMENTACJA_KREATORA_0_10.html` jest pełnym wydrukiem dołączonego zestawu. Rysunki są nominalnymi schematami: detale kieszeni i zaokrągleń opisują tabele, a dokładną geometrię przedstawia eksport **STEP**. Dostępne są także DXF, STL, formatki CSV i dane produkcyjne JSON.

Katalog okuć zawiera wybrane profile i warianty, z informacją o pochodzeniu wymiarów. Nie obejmuje wszystkich producentów, artykułów, zaczepów ani automatycznych otworów każdego mocowania. STEP, DXF i JSON nie są gotowym programem dowolnej maszyny CNC. Postprocesor, narzędzia, mocowanie i parametry technologiczne zależą od konkretnej maszyny.

## Dołączone projekty

| Plik | Zawartość |
|---|---|
| DEMO_KREATOR_0_10.mebel | Cztery meble, 42 rzeczywiste części, 16 nóg, dwie szuflady, ramka, ryflowanie, LED i szafa z podziałami |
| PRZYKLADY/50_Kreator_2_i_3_szuflady.mebel | Dwie komody, razem pięć szuflad; profile Blum MOVENTO, 40 części |
| PRZYKLADY/51_Kreator_3_strefy_i_komory.mebel | Dolne szuflady, dwie główne komory i pawlacz; 26 części |
| PRZYKLADY/52_Kreator_ramka_nogi_LED.mebel | Frezowany front MDF, sześć nóg, blat i LED; 8 części |
| PRZYKLADY/53_Kreator_fronty_ryflowane.mebel | Dwa fronty MDF po 10 rowków; 8 części |

Przykłady mają własną demonstracyjną paletę dębu, kaszmiru i szałwii. Dla wysokich boków oraz pleców dopuszczono obrót w rozkroju, aby mogły zmieścić się na arkuszu 2800 × 2070 mm. Przed wykorzystaniem własnego dekoru ustaw wymagany kierunek włókien i wymiary rzeczywistego półfabrykatu.

Inspiracją dla stałego panelu i szybkiego wyboru frontów były dostarczone filmy. W drugim filmie widać zmiany układów szuflad, edytor przy modelu, przezroczystość i otwieranie frontów. Pokazane wartości pól `HL` nie są udokumentowaną normą stolarską i nie zostały przepisane do programu jako standard. Materiał nie potwierdza nazwy wtyczki ani gotowego eksportu na dowolne CNC.

Dane techniczne nowych dodatków: `AKCESORIA_KREATORA_0_10.md`. Układ zestawu i druk: `UKLAD_I_DRUK_0_9.md`. Szczegóły zakładek: `ZAKLADKI_0_8.md`.
