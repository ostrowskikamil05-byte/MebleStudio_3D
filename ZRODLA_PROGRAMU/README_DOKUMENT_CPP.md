# Etap migracji: dokument projektu i historia w C++

Ten etap przenosi zatwierdzony dokument edytora, historię Cofnij/Ponów oraz atomowy zapis do `MebelModel.dll`. Nie jest pełnym przepisaniem całej aplikacji: formularze, gesty, generatory i dokładny CAD zachowują sprawdzony kod Python/OpenCascade.

## Co wykonuje C++

Nowy moduł `document` za istniejącym ABI JSON zawiera `create`, `read`, `commit`, `checkpoint`, `undo`, `redo`, `clone`, `info`, `destroy` i `save`. `native.info` ogłasza `document_api_version = 1`; starsza DLL nie jest po cichu używana jako zamiennik tej funkcji.

Dokument przechowuje kompletny tekst JSON `.mebel`, a nie wybrane pola kosztorysu. Zachowuje nazwy, identyfikatory, pozycje, materiały, obróbki, powiązania, konfigurator, mechanizmy, pomieszczenie i metadane. Odczyt i historia zachowują nawet kolejność kluczy oraz zapis liczb. Format pliku projektu pozostaje w wersji 1.

C++ zarządza maksymalnie 60 stanami historii. Zatwierdzanie wymaga oczekiwanej rewizji; nieaktualna lub błędna zmiana nie modyfikuje dokumentu ani historii. Nowa edycja po Cofnij usuwa gałąź Ponów. Zapis pod nową nazwą zachowuje Ponów, zgodnie z poprzednim zachowaniem edytora. Klony mają własną tożsamość i historię, a zamknięcie dokumentu zwalnia uchwyt.

`document.save` sprawdza rewizję i dane oraz przygotowuje nowy stan przed zastąpieniem pliku. Dopiero po powodzeniu atomowego zapisu publikuje stan w pamięci. Nieaktualna rewizja, błędne dane i błąd zapisu zachowują poprzedni plik oraz dokument. Ścieżki z polskimi znakami są obsługiwane.

## Jak współpracuje zachowany edytor

`native_document.py` udostępnia bieżący dokument i cienką projekcję dla istniejących formularzy. Stare wywołania `snapshot()` oznaczają początek edycji; po zatwierdzeniu wykonywana jest pełna dotychczasowa walidacja, a C++ zapisuje jeden krok. Python nie przechowuje list kopii historii.

Przesuwanie lub zmiana wymiarów myszą tworzą roboczy podgląd. Ruch myszy i anulowanie nie zmieniają dokumentu C++; puszczenie myszy zatwierdza jeden krok. Zmiana kamery również nie tworzy wpisu w historii ani nie uruchamia ponownie dokładnego CAD bez zmiany modelu. Nowy/Otwórz resetują dokument i historię. Błędna transakcja przywraca spójną projekcję.

Przed każdym nowym zatwierdzeniem nadal działa pełne `Project.validate()` z dotychczasową geometrią CAD. Natywna walidacja przechowywanego dokumentu sprawdza strukturę, liczby i istniejące odwołania; nie jest deklarowana jako zamiennik dokładnej geometrii. Limit pliku 10 MB i maksymalnie 2000 części odpowiadają istniejącym ograniczeniom projektu. Ustawienia, widoki, dokładny eksport i generatory nie zostały usunięte.

## Odtwarzanie

`ZBUDUJ_EXE.bat` buduje host oraz obie biblioteki, uruchamia trzy zestawy CTest i tworzy kompletną osobną paczkę. `SPRAWDZ.bat --native-library SCIEZKA_DO_DLL --report-dir KATALOG` wykonuje regresję źródeł z wybraną DLL. Testy deweloperskie wymagają Python 3.13; gotowy program używa własnego środowiska.

Nowe testy: `native/tests/test_document_native.cpp` oraz `tests/test_native_document_ui.py`. Panel „Silnik C++ i stan migracji” pokazuje bieżącą rewizję i liczby kroków Cofnij/Ponów. Dokładne wyniki i zakres następnych etapów znajdują się w raporcie migracji dołączonym do gotowej paczki.

## Pozostałe etapy

Nadal wymagają przeniesienia zależne edycje i generatory mebli/okuć, pełna walidacja i dokładna geometria CAD, dokumentacja produkcyjna oraz interfejs. Prywatne środowisko Python pozostaje w paczce, dopóki ich zgodne zamienniki nie przejdą testów funkcji i interfejsu. Niekompletne stare główne pliki `.cpp` są zachowane jako materiały robocze i nie zastępują działających modułów.
