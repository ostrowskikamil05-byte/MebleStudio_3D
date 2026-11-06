# Interfejs i ustawienia 0.13

Wstążka grupuje pracę w kategoriach **Mebel, CAD, Układ, Scena, Produkcja, Widok i Ustawienia**. Fronty, okucia, nóżki, materiały i LED należą do Mebla; rozstrzelenie, wymiary, X-ray i otwory do Widoku. Paski tuż nad 3D zawierają skróty kamery i narzędzie edycji.

Przeciągnij separator między inspektorem i 3D, aby zmienić szerokość panelu. Separator pod wstążką zmienia jej wysokość. Program zachowuje minimalne miejsce na 3D; zawinięte przyciski w zbyt niskiej wstążce pozostają dostępne przez przewijanie. Gdy zakładki bocznego inspektora nie mieszczą się przy wybranej skali, ich miejsce zajmuje lista wszystkich paneli.

Przycisk **⚙** w nagłówku lub kategoria **Ustawienia** otwiera zwykłą zakładkę z rozmiarem okna, skalą, panelami i jakością 3D. Tę zakładkę można odłączyć tak samo jak inne dokumenty. Dostępne są rozmiary 900 × 600, 1280 × 720, 1366 × 768, 1920 × 1080 oraz własne wartości. Rozmiar jest ograniczany do obszaru roboczego bieżącego monitora, z uwzględnieniem miejsca na pasek zadań i ramkę okna. Nie zmienia to rozdzielczości Windows. Skala jest dodatkową skalą interfejsu, stosowaną do bieżącego DPI.

Można schować inspektor, wstążkę i paski skrótów. Stały przycisk ⚙ pozwala je przywrócić. **Zastosuj i zapisz ustawienia** zapisuje wybory dla tego komputera, bez zmiany projektu mebla, jego geometrii, listy formatek ani historii Cofnij. Jakość 3D również nie zmienia wymiarów produkcyjnych. Tryb GPU wymaga dostępnego silnika; w przeciwnym przypadku program zachowuje podgląd CPU.

Preferencje znajdują się w `%LOCALAPPDATA%\MebelStudio3D\ui_preferences.json`. Zapis używa pliku tymczasowego i atomowego zastąpienia. Uszkodzony plik, nieobsługiwana wersja lub wartości poza dozwolonym zakresem powodują bezpieczne użycie ustawień domyślnych.

## API integracji

- `ui_preferences.load_preferences(path=None)` zwraca zweryfikowany słownik; `save_preferences(prefs, path=None)` zapisuje go atomowo. Domyślny schemat to 1.
- `responsive_ui.install(app, preferences=None, path=None)` należy wywołać po zbudowaniu wszystkich paneli i dołączeniu dodatkowych przycisków. Tworzy `app.ui_layout` i `app.interface_settings`.
- `app.ui_layout.apply(prefs, geometry=True)` stosuje konfigurację. `preferences()` zwraca kopię, `persist()` zapisuje bieżące wybory, a `close()` zapisuje je i kasuje opóźnione zadania.
- Dostępne są `show_inspector(bool)`, `show_ribbon(bool)`, `show_toolbars(bool)`, `fit_view()` i `restore_layout()`.
- Root powinien udostępnić `app.apply_viewport_preferences(prefs)` dla pól `engine = auto/cpu/gpu`, `quality = draft/balanced/high`. Moduł interfejsu nie uruchamia renderera ani nie deklaruje dostępności GPU.
- Build udostępnia `app.main_pane`, `app.workspace_pane`, `app.inspector_frame`, `app.view_frame`, `app.header`, `app.view_toolbar`, `app.detail_toolbar`, `app.motion_toolbar` i `app.responsive_toolbars`. `ribbon_pages` zawiera siedem kategorii oraz aliasy starych kluczy `Fronty i okucia` i `Szkic / CAD`.

Weryfikacja modułu obejmuje walidację i atomowy zapis, presety na ograniczonym obszarze roboczym, monitor o ujemnych współrzędnych, prawdziwe kontrolki Tk, sash, przywracanie paneli i zawijanie ostatniej akcji. Nie zastępuje przeglądu głównego okna aplikacji i testu rzeczywistego renderera.
