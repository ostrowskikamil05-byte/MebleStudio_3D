#include "core_api.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
namespace mebel::model {
namespace {
const Json& catalog(){static const Json value=Json::parse(R"MSCAT({"profiles":{"blum_movento_760h":{"label":"Blum MOVENTO 760H BLUMOTION · 40 kg","manufacturer":"Blum","family":"MOVENTO 760H","mount":"undermount","lengths":[250,270,300,320,350,380,400,420,450,480,500,520,550,600],"sources":["https://publications.blum.com/2022/catalogue/de/419/","https://ea.blum.com/en/building-a-movento-drawer/"],"source":"https://publications.blum.com/2022/catalogue/de/419/","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Profil dotyczy nakładanego frontu, standardowego montażu bocznego i sprzęgieł.","Stabilizacja boczna wymaga dodatkowych 12 mm głębokości; nie jest objęta tym profilem.","Szuflada wewnętrzna wymaga dodania grubości frontu do długości i miejsca w korpusie.","Wymagane osobne sprzęgła, tylne przygotowanie i otwory haka; nie są tutaj generowane."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":15,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"full","load_kg":40,"nominal_runner_thickness":null},"hettich_quadro_v6_5d_eb21_push":{"label":"Hettich Quadro V6 5D M EB21 · Push to open · 30 kg","manufacturer":"Hettich","family":"Quadro V6 5D M EB21","mount":"undermount","lengths":[250,300,320,350,380,400,420,450,480,500,520,550,580,600],"sources":["https://shop.hettich.com/by_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-V6-5D/Full-extension-runner-Quadro-V6-5D-M-%2830-kg%29-with-Push-to-open%2C-500-mm%2C-right/p/9332688","https://www.hettich.com/en-ca/products/runner-systems/quadro","https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=27"],"source":"https://shop.hettich.com/by_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-V6-5D/Full-extension-runner-Quadro-V6-5D-M-%2830-kg%29-with-Push-to-open%2C-500-mm%2C-right/p/9332688","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Wyłącznie nowa metryczna generacja EB21; nie przenosić na starsze EB20 ani wersje calowe USA.","Do montażu wymagany osobno zamawiany komplet sprzęgieł 2D; wycięcia przednie 75 mm, tylne co najmniej 50 mm.","Push to open wymaga miejsca do aktywacji: 4 mm. Dla szerokich frontów dobrać synchronizację.","Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania prowadnicy."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":13,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":75,"rear_recess":50,"cnc_patterns_available":false,"extension":"full","load_kg":30,"nominal_runner_thickness":null,"activation_gap_min":4,"lowest_screw_height_min":38},"hettich_quadro_25_you_eb21":{"label":"Hettich Quadro 25 YOU EB21 · Silent System · 25 kg","manufacturer":"Hettich","family":"Quadro 25 YOU EB21","mount":"undermount","lengths":[270,300,320,350,380,400,420,450,480,500,520,550],"sources":["https://shop.hettich.com/me_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-25-YOU/Partial-extension-runner-Quadro-25-YOU-S-%2825-kg%29-with-Silent-System%2C-500-mm%2C-left-and-right/p/9333185","https://shop.hettich.com/cr_EN/-/Quadro-25-YOU-with-Silent-System%2C-partial-extension-runner-for-wooden-drawer-%28slide-on-installation%2C-drawer-side-profile-thickness-%E2%89%A4-16-mm%29/bp/variant17776458291608","https://www.hettich.com/en-ca/products/runner-systems/quadro"],"source":"https://shop.hettich.com/me_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-25-YOU/Partial-extension-runner-Quadro-25-YOU-S-%2825-kg%29-with-Silent-System%2C-500-mm%2C-left-and-right/p/9333185","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Nowa platforma YOU EB21, montaż nasuwany ze sprzęgłami YOU; nie starsze Quadro EB20.","Wycięcia przednie 50 mm i tylne co najmniej 50 mm; sprzęgła zamawiane osobno.","Wysuw częściowy: długość prowadnicy nie jest automatycznie długością drogi wysuwu.","Nie wygenerowano wycięć pod sprzęgła, haka ani otworów mocowania."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":13,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[0,0],"front_recess":50,"rear_recess":50,"cnc_patterns_available":false,"extension":"partial","load_kg":25,"nominal_runner_thickness":null,"lowest_screw_height_min":34},"hettich_ka5632":{"label":"Hettich KA 5632 · boczna 12,7 mm · 45 kg","manufacturer":"Hettich","family":"KA 5632","mount":"side","lengths":[250,300,350,400,450,500,550,600,650,700],"sources":["https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm%2C-blue-passivated%2C-galvanised/p/73281"],"source":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm%2C-blue-passivated%2C-galvanised/p/73281","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Luz 12,7 mm na zewnątrz skrzynki po każdej stronie, a nie odsunięcie od jej wnętrza.","Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 10 artykułów serii.","Długość boku skrzynki przyjmujemy roboczo równą NL; karta artykułu podaje długość prowadnicy, nie normę długości drewnianej skrzynki. Nie wygenerowano otworów mocowania."],"depth_extra":null,"depth_by_length":{"250":254,"300":304,"350":354,"400":404,"450":454,"500":504,"550":554,"600":604,"650":654,"700":704},"depth_status":"verified","length_offset":0,"length_status":"workshop","max_side_thickness":null,"bottom_recess_min":0,"bottom_recess_max":null,"inner_width_offset":null,"external_clearance":12.7,"inner_width_tolerance":[0,0],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"full","load_kg":45,"nominal_runner_thickness":12.7,"runner_height":46,"article_by_length":{"250":"73276","300":"73277","350":"73278","400":"73279","450":"73280","500":"73281","550":"73282","600":"73283","650":"73284","700":"41355"},"source_by_length":{"250":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-250-mm%2C-blue-passivated%2C-galvanised/p/73276","300":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-300-mm%2C-blue-passivated%2C-galvanised/p/73277","350":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-350-mm%2C-blue-passivated%2C-galvanised/p/73278","400":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-400-mm%2C-blue-passivated%2C-galvanised/p/73279","450":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-450-mm%2C-blue-passivated%2C-galvanised/p/73280","500":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm%2C-blue-passivated%2C-galvanised/p/73281","550":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-550-mm%2C-blue-passivated%2C-galvanised/p/73282","600":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-600-mm%2C-blue-passivated%2C-galvanised/p/73283","650":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-650-mm%2C-blue-passivated%2C-galvanised/p/73284","700":"https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-700-mm%2C-blue-passivated%2C-galvanised/p/41355"}},"hettich_ka4532_push":{"label":"Hettich KA 4532 · Push to open · boczna 12,7 mm · 35 kg","manufacturer":"Hettich","family":"KA 4532 Push to open","mount":"side","lengths":[300,350,400,450,500,550,600],"sources":["https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm/p/9302510","https://shop.hettich.com/au_EN/-/KA-4532-full-extension-runner-with-Push-to-open%2C-load-class-35-kg%2C%2C-12-7-mm-installed-width/bp/variant9420787652550"],"source":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm/p/9302510","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Profil bieżącej serii 9302506–9302512; nie mieszamy otworów ze starszym KA4532 AM.","Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 7 artykułów serii.","Producent podaje drogę wysuwu 293 mm dla wersji nominalnej 300 mm.","Długość skrzynki = NL jest ustawieniem warsztatowym, a nie zweryfikowanym wymiarem drewnianej skrzynki producenta.","Nie wygenerowano otworów mocowania ani automatycznego dystansu aktywatora."],"depth_extra":null,"depth_by_length":{"300":304,"350":354,"400":404,"450":454,"500":504,"550":554,"600":604},"depth_status":"verified","length_offset":0,"length_status":"workshop","max_side_thickness":null,"bottom_recess_min":0,"bottom_recess_max":null,"inner_width_offset":null,"external_clearance":12.7,"inner_width_tolerance":[0,0],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"full","load_kg":35,"nominal_runner_thickness":12.7,"runner_height":46,"article_by_length":{"300":"9302506","350":"9302507","400":"9302508","450":"9302509","500":"9302510","550":"9302511","600":"9302512"},"travel_by_length":{"300":293},"source_by_length":{"300":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-300-mm/p/9302506","350":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-350-mm/p/9302507","400":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-400-mm/p/9302508","450":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-450-mm/p/9302509","500":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm/p/9302510","550":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-550-mm/p/9302511","600":"https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-600-mm/p/9302512"}},"grass_dynapro_softclose_eb21":{"label":"GRASS Dynapro Soft-close EB21 · 40 kg","manufacturer":"GRASS","family":"Dynapro Soft-close EB21","mount":"undermount","lengths":[250,270,300,320,350,380,400,420,450,480,500,520,550,600],"sources":["https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/13/","https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/12/"],"source":"https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/13/","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Nakładany front i montaż boczny; wersja z frontem wpuszczanym wymaga NL+21 zamiast NL+3.","Wymagane sprzęgła przednie i dopasowane adaptery 2D/3D/4D; minimalne wnętrze skrzynki 138 mm.","Tipmatic, Sensomatic, montaż do dna i wersje 70 kg mają osobne wymagania; nie są objęte profilem.","Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":null,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"full","load_kg":40,"nominal_runner_thickness":null,"min_drawer_inner_width":138,"length_tolerance":[-0.5,0.5]},"salice_futura_a615_clip_cdn":{"label":"Salice FUTURA A615 · clip A710.610 · katalog CDN","manufacturer":"Salice","family":"FUTURA A615 Clip (CDN)","mount":"undermount","lengths":[250,300,350,400,450,500,550,600],"sources":["https://www.salice.com/downloads/4071/4122/Salice-Futura-Self-closing-A615-concealed-runner-CDN.pdf","https://www.salice.com/media/Catalog/runners-and-drawers/futura/Salice_FUTURA_cdn.pdf"],"source":"https://www.salice.com/downloads/4071/4122/Salice-Futura-Self-closing-A615-concealed-runner-CDN.pdf","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Wariant CDN A615, wysuw częściowy ze sprzęgłem A710.610. Nie stosować wymiarów do wersji pin-fix ani Futura 65500.","Skrzynka trójboczna: długość NL−10. Skrzynka czteroboczna: NL−10 + grubość wewnętrznego czoła (subfrontu).","Katalog określa X−42 jako szerokość WEWNĘTRZNĄ skrzynki, z tolerancją +1/0.","Przy skrzynce czterobocznej dodatkowo sprawdzić miejsce na wewnętrzne czoło i jego położenie względem prowadnicy.","Nie wygenerowano wycięć, otworów tylnego haka ani sprzęgła."],"depth_extra":0,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":null,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[0,1],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"partial","load_kg":34,"nominal_runner_thickness":null,"four_sided_add_front_thickness":true,"article_by_length":{"250":"A615/25","300":"A615/30","350":"A615/35","400":"A615/40","450":"A615/45","500":"A615/50","550":"A615/55","600":"A615/60"}},"hafele_43244_full_extension":{"label":"Häfele 432.44 · kulkowa pełny wysuw · katalog UK 2018","manufacturer":"Häfele","family":"432.44 (UK 2018)","mount":"side","lengths":[250,300,350,400,450,500,550,600,650,700],"sources":["https://files.hafele.co.uk/CatalogFiles/www/18T10-262.PDF"],"source":"https://files.hafele.co.uk/CatalogFiles/www/18T10-262.PDF","status":"wymiary z archiwalnego katalogu UK 2018; dostępność do sprawdzenia","checked_on":"2026-10-03","notes":["Grubość nominalna prowadnicy 12,7 mm. Producent zaleca miejsce większe o 0,2–0,5 mm: 12,9–13,2 mm na stronę.","Obliczenia przyjmują dolny limit 12,9 mm. Nie zastępuje go 12,7 mm nominalnej grubości.","Archiwalny katalog 2018: dostępność i obecne dane artykułu wymagają sprawdzenia przed zakupem.","Minimalna głębokość montażowa nie jest podana w użytym źródle; sprawdzamy tylko obrys długości prowadnicy.","Długość skrzynki = NL jest ustawieniem warsztatowym. Nie wygenerowano otworów mocowania."],"depth_extra":null,"depth_by_length":{},"depth_status":"unverified","length_offset":0,"length_status":"workshop","max_side_thickness":null,"bottom_recess_min":0,"bottom_recess_max":null,"inner_width_offset":null,"external_clearance":12.9,"inner_width_tolerance":[0,0],"front_recess":null,"rear_recess":null,"cnc_patterns_available":false,"extension":"full","load_kg":null,"nominal_runner_thickness":12.7,"external_clearance_range":[12.9,13.2],"runner_height":44.4,"article_by_length":{"250":"432.44.926","300":"432.44.931","350":"432.44.936","400":"432.44.941","450":"432.44.946","500":"432.44.951","550":"432.44.956","600":"432.44.961","650":"432.44.966","700":"432.44.971"},"load_by_length":{"250":41,"300":42,"350":43,"400":44,"450":45,"500":45,"550":45,"600":45,"650":44,"700":43}},"hettich_actro5d_silent_40":{"label":"Hettich Actro 5D Silent System EB21 · 40 kg · 2026","manufacturer":"Hettich","family":"Actro 5D Silent System EB21 40 kg","mount":"undermount","lengths":[270,300,350,400,450,500,550,600],"sources":["https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8"],"source":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).","Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.","Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.","Push to open Silent jest osobnym zestawem, nie częścią tego profilu.","Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":15,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":75,"rear_recess":50,"cnc_patterns_available":false,"extension":"full","load_kg":40,"nominal_runner_thickness":null,"article_by_length":{"270":"9318269","300":"9318270","350":"9318282","400":"9318284","450":"9318286","500":"9318288","550":"9318290","600":"9318291"},"source_by_length":{"270":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","300":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","350":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","400":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","450":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","500":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","550":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","600":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8"},"activation_gap_min":null},"hettich_actro5d_silent_70":{"label":"Hettich Actro 5D Silent System EB21 · 70 kg · 2026","manufacturer":"Hettich","family":"Actro 5D Silent System EB21 70 kg","mount":"undermount","lengths":[500,550,600,650,700,750],"sources":["https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8"],"source":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).","Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.","Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.","Push to open Silent jest osobnym zestawem, nie częścią tego profilu.","Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":15,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":75,"rear_recess":50,"cnc_patterns_available":false,"extension":"full","load_kg":70,"nominal_runner_thickness":null,"article_by_length":{"500":"9318311","550":"9318313","600":"9318315","650":"9318316","700":"9318317","750":"9318318"},"source_by_length":{"500":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","550":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","600":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","650":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","700":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8","750":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=8"},"activation_gap_min":null},"hettich_actro5d_push_40":{"label":"Hettich Actro 5D Push to open EB21 · 40 kg · 2026","manufacturer":"Hettich","family":"Actro 5D Push to open EB21 40 kg","mount":"undermount","lengths":[270,300,350,400,450,500,550,600],"sources":["https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10"],"source":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).","Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.","Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.","Push to open wymaga min. 3 mm szczeliny.","Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":15,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":75,"rear_recess":50,"cnc_patterns_available":false,"extension":"full","load_kg":40,"nominal_runner_thickness":null,"article_by_length":{"270":"9320613","300":"9320614","350":"9320616","400":"9320618","450":"9320620","500":"9320622","550":"9320624","600":"9320625"},"source_by_length":{"270":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","300":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","350":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","400":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","450":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","500":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","550":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","600":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10"},"activation_gap_min":3},"hettich_actro5d_push_70":{"label":"Hettich Actro 5D Push to open EB21 · 70 kg · 2026","manufacturer":"Hettich","family":"Actro 5D Push to open EB21 70 kg","mount":"undermount","lengths":[500,550,600,650,700,750],"sources":["https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10"],"source":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","status":"wymiary planistyczne producenta; model uproszczony","checked_on":"2026-10-03","notes":["Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).","Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.","Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.","Push to open wymaga min. 3 mm szczeliny.","Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."],"depth_extra":3,"depth_by_length":{},"depth_status":"verified","length_offset":-10,"length_status":"verified","max_side_thickness":16,"bottom_recess_min":12,"bottom_recess_max":15,"inner_width_offset":42,"external_clearance":null,"inner_width_tolerance":[-1.5,0],"front_recess":75,"rear_recess":50,"cnc_patterns_available":false,"extension":"full","load_kg":70,"nominal_runner_thickness":null,"article_by_length":{"500":"9320627","550":"9320629","600":"9320631","650":"9320632","700":"9320633","750":"9320634"},"source_by_length":{"500":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","550":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","600":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","650":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","700":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10","750":"https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf#page=10"},"activation_gap_min":3}},"recipe_defaults":{"gap":2,"level":300,"offset":0,"hinge_side":"left","hinge_count":2,"hinge_end":100,"cup_diameter":35,"cup_depth":12.5,"cup_edge":5,"plate_line":37,"plate_spacing":32,"screw_diameter":3,"screw_depth":10,"drawer_clearance":13,"drawer_depth":400,"drawer_height":120,"drawer_t":16,"drawer_bottom":6,"drawer_raise":0,"drawer_front_gap":20,"slide_line":25,"slide_first":37,"slide_last":250,"shelf_end":40,"shelf_diameter":5,"shelf_depth":10,"hinge_price":0,"slide_price":0,"shelf_price":0,"auto_hardware":true},"corner_defaults":{"name":"Korpus narożny","furniture_type":"cabinet","width":900,"depth":900,"height":720,"arm_depth":560,"arm_depth_y":560,"thickness":18,"shape":"L","side":"left","radius":100,"segments":12,"shelves":1,"top_mode":"panel","rail_width":80,"origin":[0,0,0],"diagonal_angle":45,"fronts":"open","front_thickness":18,"gap":2,"hinge_pattern":"hinge45","hinge_count":2,"opening_angle":100,"handle_mode":"handle","handle_spacing":128,"front_style":"smooth","leg_height":0,"leg_count":6,"leg_diameter":40,"leg_setback":60,"leg_color":"#242628","leg_price":0,"material":"","front_material":""},"checked_on":"2026-10-03"})MSCAT");return value;}

// This catalog and these dimensions are the immutable 0.14 planning rules.
// They deliberately preserve unknown manufacturer data and review warnings.
struct Arguments {
    Json values;
    Arguments(const Json& request, std::initializer_list<const char*> names,
              const Json& defaults = Json::object(), size_t positionalLimit = SIZE_MAX) : values(defaults) {
        const auto args=request.value("args",Json::array());
        const auto kwargs=request.value("kwargs",Json::object());
        if (!args.is_array() || !kwargs.is_object()) throw std::invalid_argument("Nieprawidłowe argumenty funkcji.");
        std::vector<std::string> keys;
        for (auto name:names) keys.emplace_back(name);
        if (args.size()>keys.size() || args.size()>positionalLimit) throw std::invalid_argument("Zbyt wiele argumentów pozycyjnych.");
        for (size_t i=0;i<args.size();++i) values[keys[i]]=args[i];
        for (auto it=kwargs.begin();it!=kwargs.end();++it) {
            auto found=std::find(keys.begin(),keys.end(),it.key());
            if (found==keys.end()) throw std::invalid_argument("Nieznany argument: "+it.key());
            if (size_t(found-keys.begin())<args.size()) throw std::invalid_argument("Powtórzony argument: "+it.key());
            values[it.key()]=it.value();
        }
        for (const auto& key:keys) if (!values.contains(key)) throw std::invalid_argument("Brak wymaganego argumentu: "+key);
    }
    const Json& operator[](const char* key) const {return values.at(key);}
};

bool truth(const Json& value) {
    if(value.is_null()) return false;
    if(value.is_boolean()) return value.get<bool>();
    if(value.is_number()) return value.get<double>()!=0;
    return !value.empty();
}
std::string general(double value) {
    std::ostringstream result; result<<std::setprecision(6)<<std::defaultfloat<<value; return result.str();
}
std::string stripped(const std::string& text);
std::string pythonRepr(const std::string& text) {
    const char quote=text.find('\'')!=std::string::npos && text.find('"')==std::string::npos?'"':'\'';
    std::string out(1,quote);
    for(unsigned char c:text) {
        if(c==quote || c=='\\')out.push_back('\\');
        if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c=='\t')out+="\\t";
        else if(c<32 || c==127) {const char* hex="0123456789abcdef";out+="\\x";out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}
        else out.push_back(static_cast<char>(c));
    }
    out.push_back(quote);return out;
}
double number(const Json& value) {
    if(value.is_number()) return value.get<double>();
    if(value.is_boolean()) return value.get<bool>()?1.:0.;
    if(value.is_string()) {
        const auto source=value.get<std::string>();
        auto invalid=[&]() -> void {throw std::invalid_argument("could not convert string to float: "+pythonRepr(source));};
        std::string text; text.reserve(source.size());
        // Python float accepts underscores between digits, but not arbitrary separators.
        for(size_t i=0;i<source.size();++i) {
            if(source[i]=='_') {
                if(i==0 || i+1==source.size() || source[i-1]<'0' || source[i-1]>'9' || source[i+1]<'0' || source[i+1]>'9')
                    invalid();
            } else text.push_back(source[i]);
        }
        text=stripped(text);
        // strtod also accepts C hexadecimal floats and nan(payload), unlike Python.
        std::string lower=text;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        size_t position=0;if(position<lower.size() && (lower[position]=='+' || lower[position]=='-'))++position;
        const auto special=lower.substr(position);
        if(special!="inf" && special!="infinity" && special!="nan") {
            bool digits=false;
            while(position<text.size() && text[position]>='0' && text[position]<='9'){++position;digits=true;}
            if(position<text.size() && text[position]=='.') {
                ++position;while(position<text.size() && text[position]>='0' && text[position]<='9'){++position;digits=true;}
            }
            if(!digits) invalid();
            if(position<text.size() && (text[position]=='e' || text[position]=='E')) {
                ++position;if(position<text.size() && (text[position]=='+' || text[position]=='-'))++position;
                const auto exponentStart=position;while(position<text.size() && text[position]>='0' && text[position]<='9')++position;
                if(position==exponentStart) invalid();
            }
            if(position!=text.size()) invalid();
        }
        char* end=nullptr; const char* start=text.c_str();
        const double result=std::strtod(start,&end);
        if(end==start) invalid();
        while(*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if(*end) invalid();
        return result;
    }
    const std::string type=value.is_null()?"NoneType":value.is_array()?"list":value.is_object()?"dict":"object";
    throw std::invalid_argument("float() argument must be a string or a real number, not '"+type+"'");
}
double finiteValue(const Json& value,double low=-100000,double high=100000) {
    const double result=number(value);
    if(!std::isfinite(result) || !(result>=low && result<=high))
        throw std::invalid_argument("Wartość musi mieścić się w zakresie "+general(low)+"–"+general(high)+".");
    return result;
}
double positive(const Json& value,const std::string& label) {
    double result=0;
    try {
        if(value.is_boolean()) throw std::invalid_argument("boolean");
        result=number(value);
    } catch(const std::exception&) {throw std::invalid_argument(label+": podaj liczbę w mm.");}
    if(!std::isfinite(result) || result<=0 || result>10000)
        throw std::invalid_argument(label+": wymiar musi być większy od 0 i nie większy niż 10000 mm.");
    return result;
}
Json optional(const Json& object,const char* key,const Json& fallback=nullptr) {
    return object.contains(key)?object.at(key):fallback;
}
Json byLength(const Json& profile,const char* key,double length,const Json& fallback=nullptr) {
    if(!profile.contains(key)) return fallback;
    const auto& map=profile.at(key);
    // The bridge omits original string keys, since Python .get(NL) ignores them.
    // Numeric int/float keys become text in JSON; preserve Python numeric equality.
    if(length==std::trunc(length) && std::abs(length)<9e18) {
        const auto text=std::to_string(static_cast<int64_t>(length));
        if(map.contains(text))return map.at(text);
    }
    if(map.is_object())for(auto it=map.begin();it!=map.end();++it) {
        try {if(number(it.key())==length)return it.value();} catch(const std::invalid_argument&) {}
    }
    return fallback;
}
bool member(const Json& value,std::initializer_list<const char*> options) {
    for(auto option:options) if(value==option) return true;
    return false;
}
Json getProfile(const Json& profile) {
    const auto& profiles=catalog().at("profiles");
    if(profile.is_string()) {
        const auto id=profile.get<std::string>();
        if(!profiles.contains(id)) throw std::invalid_argument("Nieznany profil prowadnicy: "+id);
        auto result=profiles.at(id); result["id"]=id; return result;
    }
    if(profile.is_object()) {
        auto result=profile;
        if(!result.contains("id")) {
            result["id"]="custom";
            for(auto it=profiles.begin();it!=profiles.end();++it) if(it.value()==profile) {result["id"]=it.key();break;}
        }
        return result;
    }
    throw std::invalid_argument("Wybierz profil prowadnicy z katalogu.");
}
Json runner(const Arguments& args) {
    const auto p=getProfile(args["profile"]);
    const double width=positive(args["cabinet_inside_width"],"Światło korpusu");
    const double nl=positive(args["nominal_length"],"Długość nominalna prowadnicy");
    const double side=positive(args["side_thickness"],"Grubość boku szuflady");
    const double depth=positive(args["cabinet_depth"],"Użyteczna głębokość korpusu");
    bool found=false; for(const auto& length:p.at("lengths")) if(length.is_number() && length.get<double>()==nl) {found=true;break;}
    if(!found) throw std::invalid_argument(p.at("label").get<std::string>()+": brak zweryfikowanego wariantu "+general(nl)+" mm w katalogu.");
    const auto construction=args["construction"];
    if(!member(construction,{"three_sided","four_sided"})) throw std::invalid_argument("Konstrukcja skrzynki: three_sided albo four_sided.");
    Json errors=Json::array(),warnings=p.at("notes");
    const auto maxSide=optional(p,"max_side_thickness");
    if(!maxSide.is_null() && side>number(maxSide)+1e-8)
        errors.push_back("Bok "+general(side)+" mm przekracza dopuszczalne "+general(number(maxSide))+" mm.");
    double inner,outer,clearance;
    std::array<double,2> innerRange{},outerRange{},clearanceRange{};
    if(p.at("mount")=="undermount") {
        inner=width-number(p.at("inner_width_offset"));outer=inner+2*side;clearance=(width-outer)/2;
        innerRange={inner+number(p.at("inner_width_tolerance").at(0)),inner+number(p.at("inner_width_tolerance").at(1))};
        outerRange={innerRange[0]+2*side,innerRange[1]+2*side};
        clearanceRange={(width-outerRange[1])/2,(width-outerRange[0])/2};
    } else {
        clearance=number(p.at("external_clearance"));outer=width-2*clearance;inner=outer-2*side;
        const auto range=optional(p,"external_clearance_range",Json::array({clearance,clearance}));
        clearanceRange={number(range.at(0)),number(range.at(1))};
        outerRange={width-2*clearanceRange[1],width-2*clearanceRange[0]};
        innerRange={outerRange[0]-2*side,outerRange[1]-2*side};
    }
    if(inner<=0 || outer<=0 || clearance<0) errors.push_back("Brak miejsca na boki skrzynki i prowadnice przy podanych wymiarach.");
    const auto minWidth=optional(p,"min_drawer_inner_width");
    if(truth(minWidth) && inner<number(minWidth)) errors.push_back("Wnętrze skrzynki musi mieć co najmniej "+general(number(minWidth))+" mm.");
    double drawerLength=nl+number(p.at("length_offset"));
    Json frontThickness=nullptr;
    if(construction=="four_sided" && truth(optional(p,"four_sided_add_front_thickness"))) {
        if(args["drawer_front_thickness"].is_null()) throw std::invalid_argument("Salice: podaj grubość wewnętrznego czoła skrzynki czterobocznej (subfrontu).");
        frontThickness=positive(args["drawer_front_thickness"],"Grubość wewnętrznego czoła");
        drawerLength+=number(frontThickness);
    }
    Json minDepth=byLength(p,"depth_by_length",nl);
    if(minDepth.is_null() && !optional(p,"depth_extra").is_null()) minDepth=nl+number(p.at("depth_extra"));
    Json depthStatus=p.at("depth_status");
    if(minDepth.is_null() && !args["confirmed_min_depth"].is_null()) {
        minDepth=positive(args["confirmed_min_depth"],"Potwierdzona głębokość montażowa");
        if(number(minDepth)<nl) throw std::invalid_argument("Potwierdzona głębokość montażowa nie może być mniejsza niż długość prowadnicy.");
        depthStatus="user_confirmed";
        warnings.push_back("Głębokość montażowa wpisana i potwierdzona przez użytkownika; brak tego wymiaru w dołączonym źródle.");
    }
    Json fitsDepth=minDepth.is_null()?Json(nullptr):Json(depth+1e-8>=number(minDepth));
    if(fitsDepth==false) errors.push_back("Za mała głębokość korpusu: "+general(depth)+" mm; wymagane co najmniej "+general(number(minDepth))+" mm.");
    const bool fitsEnvelope=depth+1e-8>=nl, fitsDrawer=depth+1e-8>=drawerLength;
    if(!fitsEnvelope) errors.push_back("Prowadnica "+general(nl)+" mm jest dłuższa niż użyteczna głębokość "+general(depth)+" mm.");
    if(!fitsDrawer) errors.push_back("Cała skrzynka ("+general(drawerLength)+" mm) jest dłuższa niż użyteczna głębokość "+general(depth)+" mm.");
    if(minDepth.is_null()) warnings.push_back("Głębokość montażowa tego wariantu niezweryfikowana. Dopasowanie obrysu nie potwierdza dopasowania montażowego.");
    if(construction=="four_sided" && truth(optional(p,"four_sided_add_front_thickness"))) warnings.push_back("Głębokość PM z tabeli prowadnicy nie obejmuje automatycznie miejsca na wewnętrzne czoło; sprawdź układ skrzynki.");
    return Json{{"profile_id",p.at("id")},{"label",p.at("label")},{"manufacturer",p.at("manufacturer")},
      {"family",p.at("family")},{"mount",p.at("mount")},{"nominal_length",nl},{"cabinet_inside_width",width},{"cabinet_depth",depth},{"side_thickness",side},
      {"drawer_inner_width",inner},{"drawer_outer_width",outer},{"external_clearance_per_side",clearance},
      {"drawer_inner_width_range",innerRange},{"drawer_outer_width_range",outerRange},{"external_clearance_range",clearanceRange},{"drawer_length",drawerLength},
      {"min_cabinet_depth",minDepth},{"fits_depth",fitsDepth},{"fits_runner_envelope",fitsEnvelope},{"fits_drawer_envelope",fitsDrawer},
      {"planning_complete",!minDepth.is_null() && p.at("length_status")=="verified"},{"valid",errors.empty()},{"errors",errors},{"warnings",warnings},
      {"bottom_recess_min",p.at("bottom_recess_min")},{"bottom_recess_max",p.at("bottom_recess_max")},{"front_recess",p.at("front_recess")},
      {"rear_recess",p.at("rear_recess")},{"max_side_thickness",p.at("max_side_thickness")},{"sources",p.at("sources")},
      {"variant_source",byLength(p,"source_by_length",nl,p.at("source"))},{"status",p.at("status")},{"depth_status",depthStatus},
      {"length_status",p.at("length_status")},{"confirmed_min_depth",args["confirmed_min_depth"]},{"checked_on",catalog().at("checked_on")},
      {"cnc_patterns_available",false},{"article",byLength(p,"article_by_length",nl)},{"construction",construction},
      {"drawer_front_thickness",frontThickness},{"extension",p.at("extension")},{"load_kg",byLength(p,"load_by_length",nl,optional(p,"load_kg"))},
      {"documented_travel",byLength(p,"travel_by_length",nl)}};
}
Json available(const Arguments& args) {
    const auto p=getProfile(args["profile"]);
    const auto depth=positive(args["cabinet_depth"],"Użyteczna głębokość korpusu");
    auto result=Json::array();
    for(const auto& length:p.at("lengths")) {
        const double nl=number(length); Json minimum=byLength(p,"depth_by_length",nl);
        if(minimum.is_null() && !optional(p,"depth_extra").is_null()) minimum=nl+number(p.at("depth_extra"));
        if(!minimum.is_null() && number(minimum)<=depth+1e-8) result.push_back(length);
    }
    return result;
}
Json assemblyRecipe(const Json& values) {
    auto config=catalog().at("recipe_defaults");
    if(truth(values)) {
        if(!values.is_object()) throw std::invalid_argument("Podaj parametry montażu.");
        config.update(values);
    }
    // Preserve Python's validation priority when several dimensions are invalid.
    for(const std::string key:{"gap","level","offset","hinge_count","hinge_end","cup_diameter","cup_depth","cup_edge",
        "plate_line","plate_spacing","screw_diameter","screw_depth","drawer_clearance","drawer_depth","drawer_height","drawer_t",
        "drawer_bottom","drawer_raise","drawer_front_gap","slide_line","slide_first","slide_last","shelf_end","shelf_diameter",
        "shelf_depth","hinge_price","slide_price","shelf_price"}) {
        const bool price=key.size()>=6 && key.substr(key.size()-6)=="_price";
        config[key]=finiteValue(config.at(key),0,price?1e6:10000);
    }
    if(!member(config.at("hinge_side"),{"left","right"})) throw std::invalid_argument("Wybierz lewą lub prawą stronę zawiasów.");
    const double count=number(config.at("hinge_count"));
    if(count!=std::trunc(count) || count<2 || count>8) throw std::invalid_argument("Liczba zawiasów: od 2 do 8.");
    config["hinge_count"]=static_cast<int>(count); return config;
}

using Point=std::array<double,2>;
double orient(const Point& a,const Point& b,const Point& c) {return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
bool onSegment(const Point& a,const Point& b,const Point& p) {
    return std::abs(orient(a,b,p))<1e-8 && std::min(a[0],b[0])-1e-8<=p[0] && p[0]<=std::max(a[0],b[0])+1e-8 && std::min(a[1],b[1])-1e-8<=p[1] && p[1]<=std::max(a[1],b[1])+1e-8;
}
Json validatePolygon(std::vector<Point> points) {
    if(points.size()<3 || points.size()>128) throw std::invalid_argument("Kontur wymaga od 3 do 128 punktów.");
    for(auto& p:points) {p[0]=finiteValue(p[0],0,10000);p[1]=finiteValue(p[1],0,10000);}
    double area=0; for(size_t i=0;i<points.size();++i) {auto a=points[i],b=points[(i+1)%points.size()];area+=a[0]*b[1]-b[0]*a[1];}
    if(std::abs(area)/2<1) throw std::invalid_argument("Kontur musi mieć dodatnią powierzchnię.");
    for(size_t i=0;i<points.size();++i) {
        auto a=points[i],b=points[(i+1)%points.size()];
        if(std::hypot(a[0]-b[0],a[1]-b[1])<.01) throw std::invalid_argument("Kolejne punkty konturu nie mogą być identyczne.");
        for(size_t j=i+1;j<points.size();++j) {
            if(j==i+1 || (i==0 && j==points.size()-1)) continue;
            auto c=points[j],d=points[(j+1)%points.size()];
            const auto o0=orient(a,b,c),o1=orient(a,b,d),o2=orient(c,d,a),o3=orient(c,d,b);
            if((o0*o1<0 && o2*o3<0) || onSegment(a,b,c) || onSegment(a,b,d) || onSegment(c,d,a) || onSegment(c,d,b))
                throw std::invalid_argument("Krawędzie konturu nie mogą się przecinać ani stykać.");
        }
    }
    Json result=Json::array();for(const auto& point:points)result.push_back(point);return result;
}
double rounded7(double value) {std::ostringstream out;out<<std::fixed<<std::setprecision(7)<<value;return std::stod(out.str());}
Json cornerOutline(const Arguments& args) {
    const double w=number(args["width"]),d=number(args["depth"]),a=number(args["arm_depth"]);
    const double b=args["arm_depth_y"].is_null()?a:number(args["arm_depth_y"]);
    const bool straight=args["shape"]=="L" || !truth(args["radius"]);
    const double r=straight?0:number(args["radius"]);
    const double pi=std::acos(-1.);
    std::vector<Point> points{{0,0},{w,0},{w,a}};
    if(straight) points.push_back({b,a});
    else if(args["shape"]=="chamfer") {points.push_back({b+r,a});points.push_back({b,a+r*std::tan(number(args["diagonal_angle"])*(pi/180.))});}
    else if(args["shape"]=="rounded") {
        if(!args["segments"].is_number_integer() && !args["segments"].is_boolean()) throw std::invalid_argument("Liczba segmentów musi być całkowita.");
        const double segmentValue=number(args["segments"]);
        if(segmentValue>124) throw std::invalid_argument("Kontur wymaga od 3 do 128 punktów.");
        // A negative Python range is empty; avoid narrowing a huge negative int.
        const auto segments=segmentValue<0?-1:static_cast<int>(segmentValue);
        if(segments==0) throw std::invalid_argument("Liczba segmentów nie może być zerowa.");
        for(int i=0;i<=segments;++i) {const double angle=-pi/2-i*pi/(2*segments);points.push_back({b+r+r*std::cos(angle),a+r+r*std::sin(angle)});}
    } else throw std::invalid_argument("Nieznany obrys narożnika.");
    points.push_back({b,d});points.push_back({0,d});
    if(args["side"]=="right") {std::reverse(points.begin(),points.end());for(auto& p:points)p[0]=w-p[0];}
    for(auto& p:points){p[0]=rounded7(p[0]);p[1]=rounded7(p[1]);}
    return validatePolygon(points);
}

struct Dimension {const char* key;double low,high;};
std::string stripped(const std::string& text) {
    // Python str.strip uses Unicode White_Space plus ASCII information separators.
    struct Character {size_t offset,end;uint32_t value;};
    std::vector<Character> chars;
    for(size_t i=0;i<text.size();) {
        const size_t start=i;const auto byte=static_cast<unsigned char>(text[i++]);
        uint32_t cp=byte;int remaining=0;
        if((byte&0xe0)==0xc0){cp=byte&0x1f;remaining=1;}
        else if((byte&0xf0)==0xe0){cp=byte&0x0f;remaining=2;}
        else if((byte&0xf8)==0xf0){cp=byte&0x07;remaining=3;}
        while(remaining-- && i<text.size()) cp=(cp<<6)|(static_cast<unsigned char>(text[i++])&0x3f);
        chars.push_back({start,i,cp});
    }
    auto whitespace=[](uint32_t cp) {
        return (cp>=9 && cp<=13) || (cp>=0x1c && cp<=0x20) || cp==0x85 || cp==0xa0 || cp==0x1680
            || (cp>=0x2000 && cp<=0x200a) || cp==0x2028 || cp==0x2029 || cp==0x202f || cp==0x205f || cp==0x3000;
    };
    size_t begin=0,end=chars.size();
    while(begin<end && whitespace(chars[begin].value))++begin;
    while(end>begin && whitespace(chars[end-1].value))--end;
    return begin==end?std::string():text.substr(chars[begin].offset,chars[end-1].end-chars[begin].offset);
}
Json normalizedCorner(const Json& input,const Json& project) {
    if(!input.is_object()) throw std::invalid_argument("Podaj parametry narożnika.");
    auto config=catalog().at("corner_defaults");
    config["furniture_type"]=optional(input,"furniture_type","cabinet");
    if(config["furniture_type"]=="wardrobe") {config["name"]="Szafa narożna";config["height"]=2100;config["shelves"]=4;config["hinge_count"]=4;}
    const bool haveProject=truth(project);
    if(haveProject) {
        const auto& materials=project.at("materials");
        if(materials.empty()) throw std::invalid_argument("Projekt nie zawiera materiałów.");
        config["material"]=materials.at(0).at("id");config["front_material"]=materials.at(std::min<size_t>(1,materials.size()-1)).at("id");
    }
    config.update(input);
    if(!input.contains("arm_depth_y")) config["arm_depth_y"]=config.at("arm_depth");
    if(!config.at("name").is_string() || stripped(config.at("name").get<std::string>()).empty()) throw std::invalid_argument("Wpisz nazwę narożnika.");
    config["name"]=stripped(config.at("name").get<std::string>());
    const Dimension dimensions[]={{"width",150,10000},{"depth",150,10000},{"height",100,10000},{"arm_depth",80,3000},{"arm_depth_y",80,3000},
      {"thickness",6,60},{"radius",0,1500},{"rail_width",20,300},{"diagonal_angle",15,75},{"front_thickness",14,40},{"gap",.5,20},
      {"opening_angle",0,170},{"handle_spacing",0,1000},{"leg_height",0,500},{"leg_diameter",15,100},{"leg_setback",10,200},{"leg_price",0,100000}};
    for(const auto& item:dimensions) config[item.key]=finiteValue(config.at(item.key),item.low,item.high);
    const auto& legCount=config.at("leg_count");
    if(!legCount.is_number() || (legCount!=4 && legCount!=6)) throw std::invalid_argument("Wybierz 4 albo 6 nóg narożnika.");
    for(const auto& item:std::array<Dimension,3>{{{"shelves",0,8},{"segments",4,24},{"hinge_count",2,8}}}) {
        const double value=finiteValue(config.at(item.key),item.low,item.high);
        if(value!=std::trunc(value)) throw std::invalid_argument(std::string("Liczba ")+item.key+" musi być całkowita.");
        config[item.key]=static_cast<int>(value);
    }
    if(!member(config.at("shape"),{"L","chamfer","rounded"})) throw std::invalid_argument("Narożnik: L, ścięty albo zaokrąglony.");
    if(!member(config.at("side"),{"left","right"})) throw std::invalid_argument("Wybierz lewy albo prawy narożnik.");
    if(!member(config.at("top_mode"),{"panel","rails"})) throw std::invalid_argument("Góra narożnika: pełna płyta albo listwy.");
    if(!member(config.at("furniture_type"),{"cabinet","wardrobe"})) throw std::invalid_argument("Typ narożnika: szafka albo szafa.");
    if(!member(config.at("fronts"),{"open","orthogonal","diagonal"})) throw std::invalid_argument("Fronty: otwarte, dwa niezależne albo skośny.");
    if(config["fronts"]=="orthogonal" && config["shape"]!="L") throw std::invalid_argument("Dwa niezależne fronty wymagają naroża L. Dla ścięcia wybierz front skośny, dla zaokrąglenia otwarte półki.");
    if(config["fronts"]=="diagonal" && config["shape"]!="chamfer") throw std::invalid_argument("Front skośny wymaga naroża ściętego.");
    if(!member(config.at("handle_mode"),{"handle","none"})) throw std::invalid_argument("Uchwyt narożnika: standardowy albo brak.");
    if(!member(config.at("hinge_pattern"),{"system32","plate_direct","hinge45","hinge52","hinge_sensys52","hinge_sensys_tb55","confirmat7","confirmat5","dowel8","dowel6","dowel10","cam15","screw35","screw4","screw5","m4","m6","m8","shelf5","shelf3"})) throw std::invalid_argument("Wybierz wzorzec zawiasu narożnika.");
    if(!member(config.at("hinge_pattern"),{"hinge45","hinge52","hinge_sensys52"})) throw std::invalid_argument("Generator narożnika obsługuje standardowy wzór zawiasu nakładanego.");
    if(!config["origin"].is_array() || config["origin"].size()!=3) throw std::invalid_argument("Pozycja narożnika wymaga X, Y i Z.");
    for(auto& value:config["origin"]) value=finiteValue(value);
    const double arm=number(config["arm_depth"]),armY=number(config["arm_depth_y"]),t=number(config["thickness"]),w=number(config["width"]),d=number(config["depth"]),r=number(config["radius"]);
    if(arm>=d-2*t-10 || armY>=w-2*t-10) throw std::invalid_argument("Ramiona muszą być płytsze od gabarytów narożnika, z miejscem na boki.");
    if(std::min(arm,armY)<=2*t+20 || number(config["height"])-number(config["leg_height"])<=2*t+30) throw std::invalid_argument("Narożnik jest za mały dla grubości płyt i nóg.");
    const double rise=config["shape"]=="chamfer"?r*std::tan(number(config["diagonal_angle"])*(std::acos(-1.)/180.)):r;
    if(config["shape"]!="L" && (r>w-armY-2 || rise>d-arm-2)) throw std::invalid_argument("Promień / ścięcie nie mieści się pomiędzy końcami ramion.");
    if(config["top_mode"]=="rails" && number(config["rail_width"])>=std::min(arm,armY)-t) throw std::invalid_argument("Listwa jest zbyt szeroka dla ramienia narożnika.");
    if(haveProject) {
        for(auto key:{"material","front_material"}) {
            bool exists=false;for(const auto& material:project.at("materials")) if(material.at("id")==config.at(key)){exists=true;break;}
            if(!exists) throw std::invalid_argument("Wybierz materiał narożnika i frontu.");
        }
    }
    auto style=config.at("front_style");
    if(member(style,{"plain","slab"})) style="smooth";else if(style=="milled")style="frame";
    if(!member(style,{"smooth","frame","fluted","finger"})) throw std::invalid_argument("Styl frontu: smooth, frame, fluted albo finger.");
    const Dimension frontDims[]={{"front_border",5,250},{"front_depth",.1,30},{"front_radius",0,80},{"flute_width",2,40},{"flute_spacing",3,100},
        {"flute_count",0,32},{"finger_length",20,500},{"finger_width",6,80},{"finger_depth",.1,30}};
    const double frontDefaults[]={60,3,6,8,20,0,120,24,6};
    for(size_t i=0;i<std::size(frontDims);++i) {
        const auto& item=frontDims[i];const double value=finiteValue(optional(config,item.key,frontDefaults[i]),item.low,item.high);
        if(std::string(item.key)=="flute_count" && value!=std::trunc(value)) throw std::invalid_argument("Liczba rowków: wpisz liczbę całkowitą.");
    }
    config["front_style"]=style;return config;
}

double coordinate(const Json& point,int axis) {
    if(!point.is_array()) throw std::invalid_argument("Punkt musi być listą współrzędnych.");
    if(axis<0) axis+=static_cast<int>(point.size());
    if(axis<0 || static_cast<size_t>(axis)>=point.size()) throw std::invalid_argument("Nieprawidłowa oś współrzędnych.");
    return number(point.at(static_cast<size_t>(axis)));
}
double overlap(const Json& a,const Json& b,int axis) {
    return std::min(coordinate(a.at(1),axis),coordinate(b.at(1),axis))-std::max(coordinate(a.at(0),axis),coordinate(b.at(0),axis));
}
Json flatFaces(const Json& polygon) {
    if(!polygon.is_array()) throw std::invalid_argument("Kontur musi być listą punktów.");
    std::vector<Point> points;points.reserve(polygon.size());
    for(const auto& point:polygon) points.push_back({coordinate(point,0),coordinate(point,1)});
    double area=0;for(size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];area+=a[0]*b[1]-b[0]*a[1];}
    if(!(area>0)) std::reverse(points.begin(),points.end());
    auto result=Json::array();
    for(size_t i=0;i<points.size();++i) {
        auto a=points[i],b=points[(i+1)%points.size()];const auto dx=b[0]-a[0],dy=b[1]-a[1];
        if(std::abs(dx)<1e-6 && std::abs(dy)>1e-6) result.push_back(Json::array({0,dy>0?"+":"-",a[0],std::min(a[1],b[1]),std::max(a[1],b[1])}));
        else if(std::abs(dy)<1e-6 && std::abs(dx)>1e-6) result.push_back(Json::array({1,dx>0?"-":"+",a[1],std::min(a[0],b[0]),std::max(a[0],b[0])}));
    }
    return result;
}
} // namespace
std::optional<Json> dispatch_construction(const Json& request) {
    const auto module=request.value("module",std::string()),function=request.value("function",std::string());
    if(module=="runners_catalog") {
        if(function=="get_profile") {Arguments args(request,{"profile"});return getProfile(args["profile"]);}
        if(function=="available_lengths") {Arguments args(request,{"profile","cabinet_depth"});return available(args);}
        if(function=="calculate_runner") {
            Arguments args(request,{"profile","cabinet_inside_width","nominal_length","side_thickness","cabinet_depth","construction","drawer_front_thickness","confirmed_min_depth"},
                Json{{"construction","three_sided"},{"drawer_front_thickness",nullptr},{"confirmed_min_depth",nullptr}},5);
            return runner(args);
        }
    } else if(module=="assembly" && function=="recipe") {
        Arguments args(request,{"values"},Json{{"values",nullptr}});return assemblyRecipe(args["values"]);
    } else if(module=="corner_furniture") {
        if(function=="corner_outline") {
            Arguments args(request,{"width","depth","arm_depth","shape","radius","side","segments","arm_depth_y","diagonal_angle"},
                Json{{"shape","L"},{"radius",100},{"side","left"},{"segments",12},{"arm_depth_y",nullptr},{"diagonal_angle",45}});
            return cornerOutline(args);
        }
        if(function=="normalize_corner_config") {Arguments args(request,{"config","project"},Json{{"project",nullptr}});return normalizedCorner(args["config"],args["project"]);}
    } else if(module=="furniture_layout") {
        if(function=="_overlap") {
            Arguments args(request,{"a","b","axis"});
            if(!args["axis"].is_number_integer() && !args["axis"].is_boolean()) throw std::invalid_argument("Oś musi być liczbą całkowitą.");
            return overlap(args["a"],args["b"],static_cast<int>(number(args["axis"])));
        }
        if(function=="_intersects") {
            Arguments args(request,{"a","b","tolerance"},Json{{"tolerance",.01}});
            const double tolerance=number(args["tolerance"]);
            for(int axis=0;axis<3;++axis) if(!(overlap(args["a"],args["b"],axis)>tolerance))return false;
            return true;
        }
        if(function=="_translated") {
            Arguments args(request,{"box","delta"});auto result=Json::array();
            for(const auto& point:args["box"]){auto moved=Json::array();for(int axis=0;axis<3;++axis)moved.push_back(coordinate(point,axis)+coordinate(args["delta"],axis));result.push_back(moved);}
            return result;
        }
        if(function=="_flat_faces") {Arguments args(request,{"poly"});return flatFaces(args["poly"]);}
    }
    return std::nullopt;
}
} // namespace mebel::model
