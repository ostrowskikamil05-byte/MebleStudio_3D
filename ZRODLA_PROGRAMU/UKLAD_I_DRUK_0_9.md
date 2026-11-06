# MebelStudio3D 0.9

## Układ kompletu
Wybierz mebel lub jego element. Układaj meble: przeciąganie na płaszczyźnie XY w 3D lub z góry; Shift przesuwa Z (w 3D lub z przodu). Przód jest stroną −Y. PPM przesuwa kamerę, rolka powiększa. Obrót mebla wokół środka w osi Z jest niezależny od kamery.

W Układ… podajesz minimum obwiedni X/Y/Z i obrót absolutny. Wymiary gabarytowe obejmują fronty i plecy zamkniętego mebla. Pola właściwości pojedynczej części pozostają w lokalnych osiach konstrukcji; rozciąganie i okucia działają po obrocie.

Dosuwanie wybiera drugi mebel, sześć stron, wyrównanie początku/środka/końca i odstęp. Magnes przy przeciąganiu ma domyślnie zasięg 25 mm; w formularzu można go zmienić lub wyłączyć. Styk ścian wymaga obrotów o wielokrotność 90°. Dowolne kąty są dostępne przez ręczne położenie. Dosuwanie odrzuca nachodzenie na trzeci mebel. Ręczne przesuwanie pozwala na dowolne położenie, także nachodzenie; nie jest analizą kolizji ruchomych drzwi. Kontakty nie są stałymi więzami i nie generują okuć między korpusami.

Przeciąganie jest jedną operacją cofania. Escape anuluje podgląd; Cofnij/Ponów/Zapisz podczas ruchu anuluje niezakończony gest. Nowy projekt i wczytanie zamykają stare edytory.

## Zakładki i przyciski
Przechwycenie myszy powstaje dopiero przy przeciąganiu zakładki i jest zwalniane po puszczeniu, Escape, zamykaniu i przypinaniu. Windows dodatkowo wykrywa zagubione zdarzenie puszczenia. Projekt 3D zamknięty przez × wraca do programu. Dodatkowy widok zamyka się. Formularze otwierają się jako przewijane zakładki, z przyciskiem głównym w dolnym pasku.

## Dokumentacja do druku
Drukuj… lub Eksport → Komplet + rozstrzelone szafki + rozkrój. Zapisz HTML i otwórz go w przeglądarce. Drukuj / PDF: A4, skala 100%, zalecane wyłączenie nagłówków przeglądarki. Pojedyncze części mają rysunki U/V, zestawienia wierceń, frezowania i obrzeży. Rozkrój zawiera numery zgodne z formatkami; elementy zbyt duże są wymienione osobno.

Wymiary podawane są w mm, w pozycji zamkniętej. Rozstrzelenie przesuwa tylko rysunek; nie zwiększa wymiarów produkcyjnych. Rzuty i widoki rozstrzelone pokazują nominalne bryły; fazy i zaokrąglenia są opisane w części i dokładnym STEP. Nie są to szablony wydrukowane w skali 1:1. Raport obejmuje także części ukryte w podglądzie.

## Weryfikacja
Do paczki dołączone są wyniki testów modelu, GUI i gotowego EXE. Obsługa okien została sprawdzona w rzeczywistych oknach Tk z syntetycznymi zdarzeniami. Fizyczne przeciąganie między monitorami o różnym DPI pozostaje niezweryfikowane.
