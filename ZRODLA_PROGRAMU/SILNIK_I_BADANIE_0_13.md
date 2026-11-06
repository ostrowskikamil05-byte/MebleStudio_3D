# Silnik, grafika i zgodność — 0.13

Nowy widok GPU jest oddzielony od sprawdzonego CAD oraz danych produkcyjnych. Program pozostaje jedną aplikacją offline. Przełączenie CPU/GPU, jakości, skali i paneli nie zmienia projektu, wierceń ani kosztorysu.

## Wybrana droga

VTK 9.6.2 / OpenGL jest już dostępny w używanym środowisku i pozwala wykorzystać kartę graficzną bez zmiany formatu projektu ani przepisywania całego interfejsu. Renderer zachowuje geometrię między ruchami kamery, odczytuje głębokość powierzchni oraz identyfikator części i przekazuje je istniejącym narzędziom. Mniejsze próbkowanie podczas ruchu wraca do pełnej jakości po zwolnieniu myszy. Dalszy zakres poprawy jakości i konkretne czasy podaje raport wydajności w paczce.

Dokumentacja VTK opisuje ten silnik jako bibliotekę do grafiki 3D i przetwarzania geometrii: [VTK — About](https://docs.vtk.org/en/latest/about.html). Własności materiałów, oświetlenie i przezroczystość mają osobne mechanizmy: [VTK Property](https://vtk.org/doc/nightly/release/9.5/html/classvtkProperty.html), [przykład poprawnego renderowania przezroczystości](https://examples.vtk.org/site/PythonicAPI/Visualization/CorrectlyRenderTranslucentGeometry/).

CAD nadal korzysta z OpenCascade przez CadQuery/OCP. Algorytmy modelowania są wykonywane w bibliotece C++; zmiana języka całego programu nie była potrzebna do przeniesienia rysowania na GPU. [OpenCascade — Modeling Algorithms](https://occt3d.com/dev/doc/overview/html/occt_user_guides__modeling_algos.html).

## Rozpatrzone alternatywy

Qt Quick 3D ma nowoczesne materiały, oświetlenie i obsługę własnych geometrii, lecz migracja interfejsu, zakładek, zdarzeń myszy i nakładek wymiarowych rozszerzyłaby ryzyko regresji tej aktualizacji. To możliwa droga dalszego rozwoju. Nie jest częścią tej paczki. [Qt Quick 3D — Python API](https://doc.qt.io/qtforpython-6.8/PySide6/QtQuick3D/index.html), [własna geometria](https://doc.qt.io/qtforpython-6.8/PySide6/QtQuick3D/QQuick3DGeometry.html).

Zakładki z odpinaniem i regulowane panele pozostają w dotychczasowym interfejsie Tk. Dokumentacja Qt pokazuje podobne rozwiązania dokowania i zachowania stanu okna: [QMainWindow](https://doc.qt.io/qt-6/qmainwindow.html), [QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html). Nie instalujemy drugiego programu CAD ani komercyjnego AutoCAD.

## Zgodność i sposób sprawdzenia

Oryginalny katalog 0.12, wydruki, format .mebel i geometria produkcyjna są zachowane. Nowe ustawienia interfejsu są lokalne, walidowane i zapisywane atomowo do osobnego JSON. Paczka zawiera raporty testów funkcji, rzeczywistych kontrolek Tk, renderera, zgodności źródeł w EXE i uruchomienia EXE po przeniesieniu do innego folderu bez Python/pip w PATH.
