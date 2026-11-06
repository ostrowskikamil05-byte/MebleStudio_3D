# Okucia, otwory i mechanizmy — źródła i zakres wydania 0.7

Przegląd dokumentacji producentów: 3 października 2026. Parametry w programie są lokalnymi przepisami, bez połączenia z kontem producenta. Dokumentacja WWW jest potrzebna tylko do przeczytania źródła; tworzenie mebli działa offline.

| Źródło pierwotne | Ustalony fakt | Wdrożenie i granica |
|---|---|---|
| [Hettich: technika obróbki, System 32](https://web.hettich.com/fileadmin/Media_Center/09_Further_products/01_Catalogues/Verarbeitungstechnik_2018_16918_en.pdf) | Średnica 5, krok 32, linia 37 od przedniej krawędzi boku | Generator jednego/dwóch rzędów. Głębokość 10 i marginesy 64 są ustawieniami roboczymi; tylny rząd jest edytowalny. |
| [Hettich Direkt](https://shop.hettich.com/fr_FR/-/Hettich-Direkt%2C-r%C3%A9glage-en-hauteur-direct/bp/variant70030644287) | Prowadnik: 37 × 32 i Ø5 × 7,5 dla wskazanych wariantów | Przepis otworów prowadnika. Nie oznacza zgodności każdego zawiasu z tym wariantem. |
| [Hettich: katalog zawiasów](https://www.hettich.com/fileadmin/Media_Center/Catalogue/Hinges_Cat_HUK_Online__NEW_96dpi%20.pdf) | Wzory puszek Ø35, 45 × 9,5 i 52 × 9, głębokość 12,5 | Dwa wzory geometryczne i automatyczne powiązanie z bokiem. Piloty i standardowy prowadnik pozostają robocze; nie są kompletnym, zweryfikowanym wariantem SKU. |
| [Blum: śruby i piloty](https://publications.blum.com/2024/catalogue/en/160/) | Pilot Ø2,8 pod wybrane wkręty Ø3,5, głębokość do 8 | Osobny przepis wiercenia. Nie jest uniwersalnym doborem pilota dla każdego materiału. |
| [Häfele: łączniki](https://www.hafele.com/us/en/products/furniture-fittings-living-solutions/connectors-shelf-supports/connectors/50/) | W katalogu są konfirmaty dla otworu Ø5 | Konfirmat 7: pilot Ø5 oraz robocze przelot Ø7, pogłębienie Ø10 × 2 i pilot głębokości 35. Połączenie korpusu generuje pary otworów w sąsiadujących częściach. |
| [Häfele: Minifix 15](https://files.hafele.co.uk/catalogfiles/www/14CFC294.pdf) | Istnieje wiele puszek i wymaganych głębokości dla Minifix 15 | Sam przepis Ø15 × 12 jest roboczy. Nie generuje trzpienia, jego otworu ani całego połączenia mimośrodowego. |
| [Häfele: uchwyty](https://www.hafele.com/INTERSHOP/static/WFS/Haefele-COM-Site/-/Haefele-COM/en_US/opentext/assets/com/Hafele_Serbia_catalog.pdf) | Przykładowe uchwyty rozstaw 128 i mocowanie M4 | Ogólne modele gałek/uchwytów 64–320, sześć kolorów, otwory Ø4,5 z luzem roboczym. Nie odtwarzają konkretnego produktu. |
| [Hettich KA 5632](https://shop.hettich.com/pl_EN/-/KA-5632-full-extension-runner%2C-load-class-45-kg%2C-12-7-mm-installed-width/bp/variant9396199120838) | Boczne prowadnice mają szerokość instalacyjną 12,7 | Wybór luzu skrzynki 12,7 na stronę i schemat prowadnic. Wiercenia są z dotychczasowej edytowalnej receptury; otwory dla wybranej długości wymagają danych z rysunku tego wariantu. |
| [Blum AVENTOS HK top](https://www.blum.com/no/en/products/liftsystems/aventos-hk-top/downloads-videos/) | Producent publikuje montaż, regulację siły i ogranicznika kąta | Ogólny model klapy z obrotem oraz schematyczne ramiona. Nie symuluje dokładnej kinematyki AVENTOS i nie dobiera jego siły lub wierceń. |
| [Hettich TopLine L](https://hta.hettich.com/en-de/sliding-door-systems/topline-l) | Osobne instrukcje dla wariantów drzwi przesuwnych | Przesuw lewo/prawo i dwie warstwy frontów w szablonie szafy. Prowadzenie jest koncepcyjne, bez dokładnych otworów TopLine. |
| [Kesseböhmer DISPENSA](https://www.kesseboehmer.com/en/storage-solutions/kitchen/larder-units/dispensa) | Wysuwany słupek z regulowanymi poziomami wyposażenia | Koncepcyjny model cargo z trzema koszami i wysuwem; bez odwzorowania konkretnego systemu, jego nośności i mocowania. |

Pozostałe przepisy: kołki Ø6/8/10, podpórki, piloty Ø3/3,5 oraz przeloty pod M4/M6/M8 mają jawnie oznaczony charakter roboczy. Nie dodano fikcyjnych otworów do nieznanych podnośników lub koszy. Rozstaw 32 nie gwarantuje zgodności wszystkich okuć różnych producentów.

## Zmiany faktycznie wdrożone

- Windows EXE z dołączonym Pythonem i CAD; żadnej instalacji pip na komputerze użytkownika.
- Pola U/V szkicu, widok 3D na żywo, kontrola dopasowania i generowania okuć.
- Rozciąganie części i całego osiowego korpusu, dopasowanie elementu, opcjonalny krok 32.
- Naprawa zawiasów starszych frontów, adaptacja odległości zawiasów przy niskich drzwiach, dwie receptury puszek.
- Otwory konfirmatów i kołków w obu częściach, stożkowe pogłębienie w STEP oraz w tabeli dokumentacji.
- Uchwyty i gałki z otworami, powiązaniem położenia przy krawędzi/na środku oraz uproszczonym modelem.
- Uproszczone prowadnice, kosze cargo, klapy i tory przesuwu w wizualizacji.
- Suwak otwarcia oraz animacja; zapis i eksport bazują na zamkniętej konstrukcji.
- Paleta 42 kolorów/dekorów z trzema zakresami przypisania.
- 34 szablony dla siedmiu kategorii pomieszczeń, wyszukiwanie i dobór materiałów.

Pełna geometria kupowanych okuć, ich obliczenie wytrzymałościowe, dokładna symulacja otwierania, detekcja kolizji w ruchu, render fotorealistyczny oraz pełny CAM nie są funkcjami wydania. Drewno i materiały do mebli ogrodowych należy wybrać zgodnie z własną konstrukcją i wykończeniem.

## Testy

71 testów automatycznych modelu oraz scenariusze GUI. Sprawdzono wszystkie szablony, wiercenia na obu częściach, pogłębienie stożkowe, odbicia, parametry, powiązania, zmianę całego korpusu, uchwyty, ruch drzwi/szuflad/cargo/klap/pokrywy, zapisy i eksporty. Osobno sprawdzono EXE z CAD i Tkinter. Testy wykonano na tym komputerze Windows x64; nie jest to badanie produkcji na CNC ani certyfikacja mebla lub okucia.
