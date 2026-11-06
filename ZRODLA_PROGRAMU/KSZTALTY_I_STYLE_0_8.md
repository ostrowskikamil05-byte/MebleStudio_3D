# Kształty, drewno i style — wersja 0.8

Menu **Kształty i style** udostępnia edytor obrysu zaznaczonego elementu oraz
bibliotekę 12 nowych wzorców. Parametry są podawane w milimetrach.

## Edytor obrysu

- Prostokąt oraz prostokąt z czterema osobnymi promieniami narożników.
- Koło, elipsa, kapsuła, półelipsa i profil z łukiem u góry.
- Trapez: zwężenie dolnego końca po obu stronach, np. noga drewniana.
- Wspólne U/V/T ze szkicem i modelem 3D, podgląd obrysu i pola powierzchni.
- Zapamiętywanie rodzaju obrysu, czterech promieni, wysokości łuku, zwężenia
  oraz liczby odcinków. Ponowne otwarcie edytora odtwarza zapisane parametry.
  Zmiana U/V przez formularz lub rozciąganie zachowuje promienie w milimetrach;
  program odrzuca wymiary, w których łuki już się nie mieszczą.
- Obrzeże całego obrysu: długość i koszt obejmują krzywe lub własny wielokąt.
  Dla obrysów krzywych nominalna obwiednia U/V do rozkroju nie jest pomniejszana
  o grubość obrzeża. Naddatki i kompensację obrysu dobiera się w przygotowaniu CAM.
- Parametryczne ryflowanie: szerokość, podziałka, głębokość, marginesy oraz strona.
  Po zmianie U/V program przelicza liczbę i długość rowków. Ręczne kieszenie
  pozostają zachowane. Rowki są rzeczywistymi ubytkami bryły w STEP.
- Zachowanie wierceń i okuć, przeliczanie powiązanych wymiarów oraz kontrola,
  czy zachowane obróbki mieszczą się w nowym konturze. Cała operacja jest cofana
  jednym użyciem **Cofnij**. Jeżeli kontur usunie miejsce pod otwór, zapis zostaje
  odrzucony, a projekt pozostaje bez zmian.

Narożniki R1/R2/R3/R4 oznaczają kolejno lewy dolny, prawy dolny, prawy górny
i lewy górny narożnik szkicu U/V. Suma promieni sąsiednich narożników nie może
przekroczyć długości wspólnej krawędzi. Promień 0 zachowuje ostry narożnik.
To narzędzie zmienia obrys formatki; promień kantów w trzecim wymiarze ustawia
się w menu CAD. Przed zamianą konturu należy usunąć istniejące fazy/zaokrąglenia
kantów, ponieważ zmienia się numeracja krawędzi.

## Kanty dokładnych obrysów

Edytor faz i zaokrągleń pokazuje rzeczywiste krawędzie bryły: proste odcinki,
łuki kołowe i eliptyczne. Kliknięcie w widoku 3D oraz zaznaczenie w tabeli
podświetla przebieg łuku. Koło i elipsa mają cały górny i dolny obwód jako dwie
krawędzie; sztuczny szew powierzchni bocznej nie jest pokazywany jako kant.
Numeracja tych krawędzi nie zależy od liczby odcinków podglądu.

Silnik CAD rozciąga fazę lub zaokrąglenie wzdłuż stycznie połączonego ciągu.
Dlatego zaznaczenie łuku obłego blatu ustawia również połączone z nim stycznie
odcinki i sąsiednie łuki. Edytor pokazuje tę samą wartość na wszystkich członach
ciągu; sprzeczne wartości lub mieszanie fazy z promieniem są odrzucane.
Górny i dolny obwód mogą mieć różne wartości. Oddzielne ostre kanty również
zachowują osobne parametry. Cztery promienie R1…R4 samego obrysu są niezależne.
Zmienny promień wzdłuż jednego gładkiego ciągu wymaga profilu przejścia;
ta wersja udostępnia stały promień lub stałą fazę.

Dokument produkcyjny JSON zawiera `edge_numbering` i legendę krawędzi z typem
krzywej, punktami charakterystycznymi i ciągiem stycznym. Zasada numerowania
zwykłych wielokątów z wersji 0.7 pozostaje zachowana. Ręczna edycja punktów lub
odbicie dokładnego obrysu z aktywnymi fazami/zaokrągleniami wymagają najpierw
usunięcia tych obróbek, aby nie zamienić ich na inne krawędzie.

**STEP zachowuje dokładne krzywe CAD.** Koło jest kołem, elipsa jest elipsą,
narożniki są łukami kołowymi, a górny łuk i półowal są łukami eliptycznymi.
Zdefiniowane promienie i wymiary służą do tworzenia bryły oraz obliczania pola
materiału i długości obrzeża. Liczba odcinków nie zmienia objętości bryły STEP.

**Szkic, prosty podgląd, rysunki HTML i DXF przybliżają krzywe odcinkami.**
Wybór 4–24 odcinków na ćwiartkę zmienia gładkość tych podglądów i pliku DXF.
Edytor ręcznych punktów przekształca obrys w zwykły wielokąt; wówczas dokładna
definicja parametrów przestaje obowiązywać. Eksport JSON zawiera zarówno punkty
podglądu, jak i `outline_definition` oraz opis rozróżnienia geometrii.
Dla okręgu o promieniu R błąd cięciwy wynosi
`R * (1 - cos(pi / (4 * n)))`, gdzie n jest liczbą odcinków ćwiartki.
Przykład: R=200 mm, n=24 daje ok. 0,107 mm różnicy promienia w środku odcinka.
Przed obróbką wybierz dokładność odpowiednią do materiału i narzędzia.

Listy grubości płyty, MDF, sklejki, drewna, HDF i blatów są wygodnymi wartościami
do wyboru, a nie uniwersalną normą. Rzeczywistą grubość i format materiału
wpisuje się według zakupionego surowca lub dokumentacji dostawcy.

## Nowe wzorce

1. Stół obły z czterema okrągłymi nogami i ramą.
2. Stolik eliptyczny z dwoma okrągłymi postumentami i półką.
3. Biurko z obłym blatem oraz nogami o trapezowym profilu.
4. Krzesło drewniane: siedzisko obłe, łukowy obrys oparcia, okrągłe nogi i rama.
5. Taboret okrągły z trzema nogami i łączynami.
6. Konsola z kapsułowym blatem, obłymi podporami i półką.
7. Toaletka z obłym blatem i lustrem o łukowym obrysie.
8. Komoda Japandi z realnymi pionowymi rowkami frezowanymi w frontach.
9. Szafka nocna z nakładanym obłym blatem.
10. Ławka z obłym siedziskiem i łukowym obrysem oparcia.
11. Lustro łukowe na podkładzie.
12. Stolik przyścienny z półeliptycznym blatem.

Biblioteka filtruje style i nazwy, pozwala wybrać wymiary, materiał, nazwę oraz
pozycję mebla. Powstają zwykłe elementy projektu dostępne do dalszej edycji.
Kołowe nogi i postumenty są dokładnymi cylindrami w STEP; długość nogi jest
trzecim wymiarem jej bryły. Łukowe oparcia mają zaokrąglony **obrys płaskiego
elementu**. Nie ma tu ścieżek narzędzia tokarki, symulacji gięcia drewna,
tapicerki ani konstrukcji powierzchni swobodnych. Krzesło zawiera ramę pod
siedziskiem, nogi i tylne słupki stykające się z oparciem; ławka ma tylne słupki.
Wzorce mają charakter projektu wyjściowego: połączenia, nośność, ergonomię i sposób
zamocowania elementów drewnianych należy dopracować dla konkretnego mebla.
Korpusy komód/szafek korzystają z istniejącej logiki zawiasów i uchwytów.
Rowki frontu należą do jego bryły i poruszają się wraz z frontem podczas otwierania.
Model lustra ma osobny materiał dekoracyjny; nie dobiera systemu jego zawieszenia.

## Zmiana wymiarów całego wzorca

Rozciąganie całego mebla i formularz zmiany wymiarów przebudowują wzorzec z jego
parametrów. Dzięki temu długość nóg dostosowuje się do wysokości, a łączyny
taboretu zachowują właściwe kąty. Zachowane są identyfikatory elementów, materiały,
uchwyty, parametry ryflowania i ręczne obróbki, o ile pasują do nowych wymiarów.

Jeżeli użytkownik ręcznie zmienił obrys, pozycję lub układ elementów albo dodał
własne części do grupy, przebudowa całego wzorca zostaje zatrzymana z komunikatem.
Własne zmiany można dalej edytować pojedynczo. To pozwala uniknąć nadpisania
szkicu przy zmianie rozmiaru wzorca. Szablony zapisane i wstawione do nowego
projektu zachowują parametry wzorca; niezależna kopia pojedynczej części ma
status samodzielnego elementu.

## Podstawa usprawnień — źródła oficjalne

- [CadQuery — Edge.makeCircle, Edge.makeEllipse, Edge.makeThreePointArc i Solid.extrudeLinear](https://cadquery.readthedocs.io/en/latest/classreference.html):
  wbudowany silnik tworzy dokładne łuki i wyciąga ich zamknięte obrysy w bryły.

- [Autodesk — Parameters in Fusion](https://help.autodesk.com/view/fusion360/ENU/?contextId=SLD-MODIFY-PARAMETERS):
  nazwane parametry i relacje wymiarowe pozwalają zmieniać projekt bez ponownego
  tworzenia geometrii. W tej aktualizacji wykorzystano wspólne U/V/T i wzorce
  budowane z wymiarów; nie zaimplementowano pełnego solvera wyrażeń Fusion.
- [Autodesk — Woodworking & Furniture Design](https://www.autodesk.com/solutions/woodworking-furniture-design-software):
  połączenie szkicu, modelu, materiałów i dokumentacji warsztatowej uzasadnia
  stosowanie jednego modelu elementu w widoku 3D, szkicu i eksporcie.
- [SketchUp — Dynamic component predefined attributes](https://help.sketchup.com/en/sketchup/dynamic-component-predefined-attributes):
  komponenty mogą mieć edytowane pozycje, wymiary, obroty, widoczność i kopie.
  Wzorce w MebelStudio3D udostępniają odpowiednie parametry lokalne projektu.
- [SketchUp — Interacting with Dynamic Components](https://help.sketchup.com/en/sketchup/interacting-dynamic-components):
  konfigurowalne komponenty mogą zmieniać opcje i pokazywać otwieranie drzwi.
  Istniejące mechanizmy aplikacji zachowano również dla komody z rowkami.

Odczyt źródeł: 03.10.2026. Są to źródła dotyczące sposobu projektowania,
a nie normy konstrukcji krzeseł, wytrzymałości drewna czy montażu okuć.
