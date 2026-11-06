# Zakładki i odłączane widoki

Szkic, podgląd montażu, obróbka oraz edytor kantów otwierają się na górnym pasku zakładek. Można pracować w kilku dokumentach, wracać do projektu i zachowywać niedokończony szkic.

- Aby przenieść dokument do osobnego okna, przeciągnij jego zakładkę poza pasek. Możesz też użyć przycisku **Odłącz** albo dwukrotnie kliknąć zakładkę.
- Aby przywrócić okno do programu, przeciągnij uchwyt **⠿** z nagłówka dokumentu na górny pasek zakładek. W Windows działa również przeciągnięcie **systemowego paska tytułu**: puść przycisk myszy, gdy kursor znajduje się nad paskiem zakładek programu. Możesz też użyć przycisku **Przypnij**.
- Przeciągnięcie uchwytu na konkretną zakładkę określa pozycję dokumentu na pasku.
- Odłączenie i przypięcie zachowują ten sam szkic, punkty, historię, wpisane parametry oraz przybliżenie. Nie tworzą kopii części.
- **×** zamyka dodatkowy dokument. Przy zamykaniu zmienionego szkicu program pyta o odrzucenie zmian. Wykrywa również same zmiany nazwy, grubości, materiału i parametrów montażu, nawet gdy kontur nie był zmieniany. Zmiany zapisuje przycisk zapisu w dokumencie.
- **Nowy**, **Otwórz** i zamknięcie aplikacji sprawdzają niezapisane dokumenty. Anulowanie zachowuje zakładki, odłączone okna i projekt. Po potwierdzeniu otwarte dokumenty starego projektu zostają zamknięte; nie mogą zapisać swoich danych w nowym projekcie. Błąd odczytu pliku również zachowuje obecny projekt i dokumenty.
- Zamknięcie odłączonego głównego widoku **Projekt 3D** przywraca go do zakładek.
- Dodatkowa **Wizualizacja 3D** ma własną kamerę, przełącznik wymiarów i widok rozstrzelony. Pokazuje bieżący projekt wraz z otwarciem mechanizmów. Podgląd montażu szkicu pokazuje stan szkicu z chwili otwarcia podglądu.
- Skróty **Ctrl+S/O/N/Z/Y** działają także w odłączonym głównym widoku. W szkicu **Ctrl+Z** cofa lokalne rysowanie.

Okna niewielkich ustawień i wyboru pliku pozostają zwykłymi oknami dialogowymi.

## Dla programisty

`DockWorkspace(app, app.tabs)` zarządza dokumentami. Główna odłączana ramka musi być klasyczną `tk.Frame`, której rodzicem jest `app`. Rejestracja istniejącego widoku: `workspace.add_existing(design, 'Projekt 3D')`. Nowe dokumenty dziedziczą `DockDocument`; dodanie zakładki i uchwytu następuje automatycznie.

Przenoszenie korzysta z `wm manage` / `wm forget`: hierarchia kontrolek pozostaje ta sama. `DockDocument` zapewnia zgodność z używanymi metodami `title`, `geometry`, `minsize`, `protocol`, a dokumenty pozostają niemodalne. Przy niszczeniu dokumentu jego oczekujące wywołania `after` są anulowane, a zakończone timery nie pozostają w rejestrze.

Windows: `WindowsDockBridge` obserwuje `EVENT_SYSTEM_MOVESIZESTART/END` przez `SetWinEventHook` w trybie OUTOFCONTEXT, wyłącznie dla bieżącego procesu. Koniec przesuwania zapisuje pozycję kursora w kolejce; dopiero zwykły timer Tk wykonuje przypięcie. Callback nie wywołuje Tcl i nie zastępuje procedury okna Tk. Hook jest zwalniany przy zamknięciu aplikacji. Zmiana rozmiaru nie jest traktowana jako przeciągnięcie tytułu; zmiana DPI podczas przesunięcia jest uwzględniona. Pozycja okna jest ustawiana przez Tk `wm geometry`, z absolutną składnią `+-120` dla ujemnych współrzędnych. Obszar roboczy monitora ogranicza początkowy rozmiar. Pozycja kursora jest mapowana przez klienta Notebook (`ScreenToClient`, stosunek rozmiarów Win32/Tk), bez założenia stałego DPI.

Dokumenty przechowują `project_generation`. Metody `save` i `apply` podklas są chronione automatycznie. Dla zapisu w dynamicznej funkcji użyj `document.guard(callback)` lub `document.ensure_current_project(raise_error=True)`. Po zastosowaniu zmian w dokumencie, który pozostaje otwarty, wywołaj `document.mark_clean()`. Stan formularza i modeli jest porównywany z zapisanym stanem początkowym; szkic dodatkowo porównuje punkty i zamknięcie konturu. Zmiany samej kamery dodatkowej wizualizacji nie są traktowane jako niezapisana edycja modelu.

Testy `tests/test_workspace.py` działają w rzeczywistych oknach Tk. Wysyłają **syntetyczne** zdarzenia myszy Tk i zdarzenia WinEvent do prawdziwych okien; sprawdzają kolejkę przypinania, czyszczenie hooków, powtarzanie cykli odłączania, ujemną pozycję okna, ograniczenie do obszaru monitora, skróty oraz ochronę New/Load/Close. Nie stanowią testu fizycznego przeciągania myszą przez użytkownika ani testu sprzętowego na kilku monitorach o różnych DPI.

Źródła interfejsu systemowego: [SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook), [stałe zdarzeń Windows](https://learn.microsoft.com/en-us/windows/win32/winauto/event-constants), [wirtualny ekran](https://learn.microsoft.com/en-us/windows/win32/gdi/the-virtual-screen), [DPI aplikacji Windows](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows).
