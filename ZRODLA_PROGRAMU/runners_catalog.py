"""Sourced drawer-runner planning dimensions, in millimetres.

The width of an undermount runner refers to the INSIDE of the drawer side.
Purchased fittings are not manufactured by this program.  This catalogue does
not contain CNC drilling patterns, rear notches, clip machining or SKU meshes.
"""
from __future__ import annotations

import copy
import math


CHECKED_ON = "2026-10-03"
BLUM_PLAN = "https://publications.blum.com/2022/catalogue/de/419/"
BLUM_ASSEMBLY = "https://ea.blum.com/en/building-a-movento-drawer/"
HETTICH_FAQ = "https://www.hettich.com/en-ca/products/runner-systems/quadro"
QUADRO_5D = "https://shop.hettich.com/by_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-V6-5D/Full-extension-runner-Quadro-V6-5D-M-%2830-kg%29-with-Push-to-open%2C-500-mm%2C-right/p/9332688"
QUADRO_YOU = "https://shop.hettich.com/me_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-25-YOU/Partial-extension-runner-Quadro-25-YOU-S-%2825-kg%29-with-Silent-System%2C-500-mm%2C-left-and-right/p/9333185"
QUADRO_YOU_RANGE = "https://shop.hettich.com/cr_EN/-/Quadro-25-YOU-with-Silent-System%2C-partial-extension-runner-for-wooden-drawer-%28slide-on-installation%2C-drawer-side-profile-thickness-%E2%89%A4-16-mm%29/bp/variant17776458291608"
KA5632 = "https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm%2C-blue-passivated%2C-galvanised/p/73281"
KA4532 = "https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm/p/9302510"
KA4532_RANGE = "https://shop.hettich.com/au_EN/-/KA-4532-full-extension-runner-with-Push-to-open%2C-load-class-35-kg%2C%2C-12-7-mm-installed-width/bp/variant9420787652550"
GRASS_PLAN = "https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/13/"
GRASS_RANGE = "https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/12/"
SALICE_PLAN = "https://www.salice.com/media/Catalog/runners-and-drawers/futura/Salice_FUTURA_cdn.pdf"
SALICE_A615 = "https://www.salice.com/downloads/4071/4122/Salice-Futura-Self-closing-A615-concealed-runner-CDN.pdf"
HAFELE_PLAN = "https://files.hafele.co.uk/CatalogFiles/www/18T10-262.PDF"
HETTICH_2026 = "https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf"


def _profile(label, manufacturer, family, mount, lengths, sources, **values):
    p = dict(label=label, manufacturer=manufacturer, family=family, mount=mount,
             lengths=list(lengths), sources=list(sources), source=sources[0],
             status="wymiary planistyczne producenta; model uproszczony",
             checked_on=CHECKED_ON, notes=[], depth_extra=None,
             depth_by_length={}, depth_status="unverified", length_offset=0,
             length_status="verified", max_side_thickness=None,
             bottom_recess_min=0, bottom_recess_max=None,
             inner_width_offset=None, external_clearance=None,
             inner_width_tolerance=[0, 0], front_recess=None,
             rear_recess=None, cnc_patterns_available=False,
             extension="full", load_kg=None, nominal_runner_thickness=None)
    p.update(values)
    return p


PROFILES = {
    "blum_movento_760h": _profile(
        "Blum MOVENTO 760H BLUMOTION · 40 kg", "Blum", "MOVENTO 760H", "undermount",
        [250, 270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 600],
        [BLUM_PLAN, BLUM_ASSEMBLY],
        depth_extra=3, depth_status="verified", length_offset=-10,
        max_side_thickness=16, bottom_recess_min=12, bottom_recess_max=15,
        inner_width_offset=42, inner_width_tolerance=[-1.5, 0], load_kg=40,
        notes=["Profil dotyczy nakładanego frontu, standardowego montażu bocznego i sprzęgieł.",
               "Stabilizacja boczna wymaga dodatkowych 12 mm głębokości; nie jest objęta tym profilem.",
               "Szuflada wewnętrzna wymaga dodania grubości frontu do długości i miejsca w korpusie.",
               "Wymagane osobne sprzęgła, tylne przygotowanie i otwory haka; nie są tutaj generowane."]
    ),
    "hettich_quadro_v6_5d_eb21_push": _profile(
        "Hettich Quadro V6 5D M EB21 · Push to open · 30 kg", "Hettich", "Quadro V6 5D M EB21", "undermount",
        [250, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 580, 600],
        [QUADRO_5D, HETTICH_FAQ, HETTICH_2026 + "#page=27"],
        depth_extra=3, depth_status="verified", length_offset=-10,
        max_side_thickness=16, bottom_recess_min=12, bottom_recess_max=13,
        inner_width_offset=42, inner_width_tolerance=[-1.5, 0],
        front_recess=75, rear_recess=50, load_kg=30,
        activation_gap_min=4, lowest_screw_height_min=38,
        notes=["Wyłącznie nowa metryczna generacja EB21; nie przenosić na starsze EB20 ani wersje calowe USA.",
               "Do montażu wymagany osobno zamawiany komplet sprzęgieł 2D; wycięcia przednie 75 mm, tylne co najmniej 50 mm.",
               "Push to open wymaga miejsca do aktywacji: 4 mm. Dla szerokich frontów dobrać synchronizację.",
               "Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania prowadnicy."]
    ),
    "hettich_quadro_25_you_eb21": _profile(
        "Hettich Quadro 25 YOU EB21 · Silent System · 25 kg", "Hettich", "Quadro 25 YOU EB21", "undermount",
        [270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550],
        [QUADRO_YOU, QUADRO_YOU_RANGE, HETTICH_FAQ],
        depth_extra=3, depth_status="verified", length_offset=-10,
        max_side_thickness=16, bottom_recess_min=12, bottom_recess_max=13,
        inner_width_offset=42, front_recess=50, rear_recess=50, load_kg=25,
        extension="partial", lowest_screw_height_min=34,
        notes=["Nowa platforma YOU EB21, montaż nasuwany ze sprzęgłami YOU; nie starsze Quadro EB20.",
               "Wycięcia przednie 50 mm i tylne co najmniej 50 mm; sprzęgła zamawiane osobno.",
               "Wysuw częściowy: długość prowadnicy nie jest automatycznie długością drogi wysuwu.",
               "Nie wygenerowano wycięć pod sprzęgła, haka ani otworów mocowania."]
    ),
    "hettich_ka5632": _profile(
        "Hettich KA 5632 · boczna 12,7 mm · 45 kg", "Hettich", "KA 5632", "side",
        range(250, 701, 50), [KA5632],
        depth_by_length={n:n+4 for n in range(250,701,50)}, depth_status="verified",
        external_clearance=12.7, nominal_runner_thickness=12.7,
        length_status="workshop", 
        runner_height=46, load_kg=45,
        article_by_length={250:"73276",300:"73277",350:"73278",400:"73279",450:"73280",
                           500:"73281",550:"73282",600:"73283",650:"73284",700:"41355"},
        notes=["Luz 12,7 mm na zewnątrz skrzynki po każdej stronie, a nie odsunięcie od jej wnętrza.",
               "Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 10 artykułów serii.",
               "Długość boku skrzynki przyjmujemy roboczo równą NL; karta artykułu podaje długość prowadnicy, nie normę długości drewnianej skrzynki. Nie wygenerowano otworów mocowania."]
    ),
    "hettich_ka4532_push": _profile(
        "Hettich KA 4532 · Push to open · boczna 12,7 mm · 35 kg", "Hettich", "KA 4532 Push to open", "side",
        range(300, 601, 50), [KA4532, KA4532_RANGE],
        depth_by_length={n:n+4 for n in range(300,601,50)}, depth_status="verified",
        external_clearance=12.7, nominal_runner_thickness=12.7,
        length_status="workshop",
        runner_height=46, load_kg=35,
        article_by_length={300:"9302506",350:"9302507",400:"9302508",450:"9302509",
                           500:"9302510",550:"9302511",600:"9302512"},
        travel_by_length={300:293},
        notes=["Profil bieżącej serii 9302506–9302512; nie mieszamy otworów ze starszym KA4532 AM.",
               "Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 7 artykułów serii.",
               "Producent podaje drogę wysuwu 293 mm dla wersji nominalnej 300 mm.",
               "Długość skrzynki = NL jest ustawieniem warsztatowym, a nie zweryfikowanym wymiarem drewnianej skrzynki producenta.",
               "Nie wygenerowano otworów mocowania ani automatycznego dystansu aktywatora."]
    ),
    "grass_dynapro_softclose_eb21": _profile(
        "GRASS Dynapro Soft-close EB21 · 40 kg", "GRASS", "Dynapro Soft-close EB21", "undermount",
        [250, 270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 600],
        [GRASS_PLAN, GRASS_RANGE],
        depth_extra=3, depth_status="verified", length_offset=-10,
        max_side_thickness=16, bottom_recess_min=12,
        inner_width_offset=42, inner_width_tolerance=[-1.5,0], load_kg=40,
        min_drawer_inner_width=138, length_tolerance=[-0.5,0.5],
        notes=["Nakładany front i montaż boczny; wersja z frontem wpuszczanym wymaga NL+21 zamiast NL+3.",
               "Wymagane sprzęgła przednie i dopasowane adaptery 2D/3D/4D; minimalne wnętrze skrzynki 138 mm.",
               "Tipmatic, Sensomatic, montaż do dna i wersje 70 kg mają osobne wymagania; nie są objęte profilem.",
               "Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania."]
    ),
    "salice_futura_a615_clip_cdn": _profile(
        "Salice FUTURA A615 · clip A710.610 · katalog CDN", "Salice", "FUTURA A615 Clip (CDN)", "undermount",
        range(250, 601, 50), [SALICE_A615, SALICE_PLAN],
        depth_extra=0, depth_status="verified", length_offset=-10,
        max_side_thickness=16, bottom_recess_min=12,
        inner_width_offset=42, inner_width_tolerance=[0,1], load_kg=34,
        extension="partial", four_sided_add_front_thickness=True,
        article_by_length={n:f"A615/{n//10}" for n in range(250,601,50)},
        notes=["Wariant CDN A615, wysuw częściowy ze sprzęgłem A710.610. Nie stosować wymiarów do wersji pin-fix ani Futura 65500.",
               "Skrzynka trójboczna: długość NL−10. Skrzynka czteroboczna: NL−10 + grubość wewnętrznego czoła (subfrontu).",
               "Katalog określa X−42 jako szerokość WEWNĘTRZNĄ skrzynki, z tolerancją +1/0.",
               "Przy skrzynce czterobocznej dodatkowo sprawdzić miejsce na wewnętrzne czoło i jego położenie względem prowadnicy.",
               "Nie wygenerowano wycięć, otworów tylnego haka ani sprzęgła."]
    ),
    "hafele_43244_full_extension": _profile(
        "Häfele 432.44 · kulkowa pełny wysuw · katalog UK 2018", "Häfele", "432.44 (UK 2018)", "side",
        range(250,701,50), [HAFELE_PLAN],
        external_clearance=12.9, external_clearance_range=[12.9,13.2],
        nominal_runner_thickness=12.7, runner_height=44.4,
        length_status="workshop",
        article_by_length={250:"432.44.926",300:"432.44.931",350:"432.44.936",400:"432.44.941",
                           450:"432.44.946",500:"432.44.951",550:"432.44.956",600:"432.44.961",
                           650:"432.44.966",700:"432.44.971"},
        load_by_length={250:41,300:42,350:43,400:44,450:45,500:45,550:45,600:45,650:44,700:43},
        status="wymiary z archiwalnego katalogu UK 2018; dostępność do sprawdzenia",
        notes=["Grubość nominalna prowadnicy 12,7 mm. Producent zaleca miejsce większe o 0,2–0,5 mm: 12,9–13,2 mm na stronę.",
               "Obliczenia przyjmują dolny limit 12,9 mm. Nie zastępuje go 12,7 mm nominalnej grubości.",
               "Archiwalny katalog 2018: dostępność i obecne dane artykułu wymagają sprawdzenia przed zakupem.",
               "Minimalna głębokość montażowa nie jest podana w użytym źródle; sprawdzamy tylko obrys długości prowadnicy.",
               "Długość skrzynki = NL jest ustawieniem warsztatowym. Nie wygenerowano otworów mocowania."]
    ),
}
RUNNER_PROFILES = PROFILES

# Separate load classes and opening variants: their article numbers are not
# interchangeable.  Source is the 2026 catalogue, pages 8–11, not older Actro.
for _mode, _title, _page, _articles40, _articles70 in (
    ("silent", "Silent System", 8,
     {270:"9318269",300:"9318270",350:"9318282",400:"9318284",450:"9318286",500:"9318288",550:"9318290",600:"9318291"},
     {500:"9318311",550:"9318313",600:"9318315",650:"9318316",700:"9318317",750:"9318318"}),
    ("push", "Push to open", 10,
     {270:"9320613",300:"9320614",350:"9320616",400:"9320618",450:"9320620",500:"9320622",550:"9320624",600:"9320625"},
     {500:"9320627",550:"9320629",600:"9320631",650:"9320632",700:"9320633",750:"9320634"}),
):
    for _load, _articles in ((40,_articles40),(70,_articles70)):
        _id = f"hettich_actro5d_{_mode}_{_load}"
        PROFILES[_id] = _profile(
            f"Hettich Actro 5D {_title} EB21 · {_load} kg · 2026",
            "Hettich", f"Actro 5D {_title} EB21 {_load} kg", "undermount",
            list(_articles), [HETTICH_2026 + f"#page={_page}"],
            depth_extra=3, depth_status="verified", length_offset=-10,
            max_side_thickness=16, bottom_recess_min=12, bottom_recess_max=15,
            inner_width_offset=42, inner_width_tolerance=[-1.5,0],
            front_recess=75, rear_recess=50, load_kg=_load,
            article_by_length=_articles,
            source_by_length={n:HETTICH_2026 + f"#page={_page}" for n in _articles},
            activation_gap_min=3 if _mode=="push" else None,
            notes=["Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).",
                   "Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.",
                   "Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.",
                   "Push to open wymaga min. 3 mm szczeliny." if _mode=="push" else "Push to open Silent jest osobnym zestawem, nie częścią tego profilu.",
                   "Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."])

# Each KA variant was checked in its own current manufacturer product page;
# the archived AM catalogue is not used as evidence for these dimensions.
for _key, _url in (("hettich_ka5632", KA5632), ("hettich_ka4532_push", KA4532)):
    _p = PROFILES[_key]
    _p["source_by_length"] = {
        n: _url.replace("-500-mm", f"-{n}-mm").rsplit("/p/",1)[0] + "/p/" + sku
        for n,sku in _p["article_by_length"].items()
    }


def get_profile(profile):
    """Return an independent serializable profile, accepting its id or mapping."""
    if isinstance(profile, str):
        try:
            result = copy.deepcopy(PROFILES[profile])
        except KeyError:
            raise ValueError(f"Nieznany profil prowadnicy: {profile}") from None
        result["id"] = profile
        return result
    if isinstance(profile, dict):
        result = copy.deepcopy(profile)
        result.setdefault("id", next((k for k,v in PROFILES.items() if v == profile), "custom"))
        return result
    raise ValueError("Wybierz profil prowadnicy z katalogu.")


def _positive(value, label):
    if isinstance(value, bool):
        raise ValueError(f"{label}: podaj liczbę w mm.")
    try:
        value = float(value)
    except (ValueError, TypeError, OverflowError):
        raise ValueError(f"{label}: podaj liczbę w mm.") from None
    if not math.isfinite(value) or value <= 0 or value > 10000:
        raise ValueError(f"{label}: wymiar musi być większy od 0 i nie większy niż 10000 mm.")
    return value


def calculate_runner(profile, cabinet_inside_width, nominal_length, side_thickness,
                     cabinet_depth, *, construction="three_sided", drawer_front_thickness=None,
                     confirmed_min_depth=None):
    """Calculate planning sizes without silently guessing missing product data.

    `cabinet_depth` is usable INSIDE depth, not the total outside cabinet depth.
    `drawer_front_thickness` is the inner box front, not the decorative overlay.
    Return errors for fit/side-thickness constraints; reject invalid inputs or NL.
    Unknown manufacturer depth returns min_cabinet_depth/fits_depth = None and
    `planning_complete=False`; the runner-envelope check is separate.
    """
    p = get_profile(profile)
    width = _positive(cabinet_inside_width, "Światło korpusu")
    nl = _positive(nominal_length, "Długość nominalna prowadnicy")
    side = _positive(side_thickness, "Grubość boku szuflady")
    depth = _positive(cabinet_depth, "Użyteczna głębokość korpusu")
    if nl not in p["lengths"]:
        raise ValueError(f"{p['label']}: brak zweryfikowanego wariantu {nl:g} mm w katalogu.")
    if construction not in ("three_sided", "four_sided"):
        raise ValueError("Konstrukcja skrzynki: three_sided albo four_sided.")
    errors, warnings = [], list(p["notes"])
    if p.get("max_side_thickness") is not None and side > p["max_side_thickness"] + 1e-8:
        errors.append(f"Bok {side:g} mm przekracza dopuszczalne {p['max_side_thickness']:g} mm.")
    if p["mount"] == "undermount":
        inner = width - p["inner_width_offset"]
        outer = inner + 2 * side
        clearance = (width - outer) / 2
        tol_low, tol_high = p["inner_width_tolerance"]
        inner_range = [inner + tol_low, inner + tol_high]
        outer_range = [v + 2 * side for v in inner_range]
        clearance_range = [(width - outer_range[1])/2, (width - outer_range[0])/2]
    else:
        clearance = p["external_clearance"]
        outer = width - 2 * clearance
        inner = outer - 2 * side
        clearance_range = list(p.get("external_clearance_range", [clearance, clearance]))
        outer_range = [width - 2 * clearance_range[1], width - 2 * clearance_range[0]]
        inner_range = [v - 2 * side for v in outer_range]
    if inner <= 0 or outer <= 0 or clearance < 0:
        errors.append("Brak miejsca na boki skrzynki i prowadnice przy podanych wymiarach.")
    if p.get("min_drawer_inner_width") and inner < p["min_drawer_inner_width"]:
        errors.append(f"Wnętrze skrzynki musi mieć co najmniej {p['min_drawer_inner_width']:g} mm.")
    drawer_length = nl + p["length_offset"]
    front_t = None
    if construction == "four_sided" and p.get("four_sided_add_front_thickness"):
        if drawer_front_thickness is None:
            raise ValueError("Salice: podaj grubość wewnętrznego czoła skrzynki czterobocznej (subfrontu).")
        front_t = _positive(drawer_front_thickness, "Grubość wewnętrznego czoła")
        drawer_length += front_t
    min_depth = p.get("depth_by_length", {}).get(nl)
    if min_depth is None and p.get("depth_extra") is not None:
        min_depth = nl + p["depth_extra"]
    depth_status = p["depth_status"]
    if min_depth is None and confirmed_min_depth is not None:
        min_depth = _positive(confirmed_min_depth, "Potwierdzona głębokość montażowa")
        if min_depth < nl:
            raise ValueError("Potwierdzona głębokość montażowa nie może być mniejsza niż długość prowadnicy.")
        depth_status = "user_confirmed"
        warnings.append("Głębokość montażowa wpisana i potwierdzona przez użytkownika; brak tego wymiaru w dołączonym źródle.")
    fits_depth = None if min_depth is None else depth + 1e-8 >= min_depth
    if fits_depth is False:
        errors.append(f"Za mała głębokość korpusu: {depth:g} mm; wymagane co najmniej {min_depth:g} mm.")
    fits_envelope = depth + 1e-8 >= nl
    if not fits_envelope:
        errors.append(f"Prowadnica {nl:g} mm jest dłuższa niż użyteczna głębokość {depth:g} mm.")
    fits_drawer_envelope = depth + 1e-8 >= drawer_length
    if not fits_drawer_envelope:
        errors.append(f"Cała skrzynka ({drawer_length:g} mm) jest dłuższa niż użyteczna głębokość {depth:g} mm.")
    if min_depth is None:
        warnings.append("Głębokość montażowa tego wariantu niezweryfikowana. Dopasowanie obrysu nie potwierdza dopasowania montażowego.")
    if construction == "four_sided" and p.get("four_sided_add_front_thickness"):
        warnings.append("Głębokość PM z tabeli prowadnicy nie obejmuje automatycznie miejsca na wewnętrzne czoło; sprawdź układ skrzynki.")
    article = p.get("article_by_length", {}).get(nl)
    result = dict(profile_id=p["id"], label=p["label"], manufacturer=p["manufacturer"],
                  family=p["family"], mount=p["mount"], nominal_length=nl,
                  cabinet_inside_width=width, cabinet_depth=depth, side_thickness=side,
                  drawer_inner_width=inner, drawer_outer_width=outer,
                  external_clearance_per_side=clearance,
                  drawer_inner_width_range=inner_range, drawer_outer_width_range=outer_range,
                  external_clearance_range=clearance_range, drawer_length=drawer_length,
                  min_cabinet_depth=min_depth, fits_depth=fits_depth,
                  fits_runner_envelope=fits_envelope, fits_drawer_envelope=fits_drawer_envelope,
                  planning_complete=min_depth is not None and p["length_status"] == "verified",
                  valid=not errors, errors=errors, warnings=warnings,
                  bottom_recess_min=p["bottom_recess_min"], bottom_recess_max=p["bottom_recess_max"],
                  front_recess=p["front_recess"], rear_recess=p["rear_recess"],
                  max_side_thickness=p["max_side_thickness"], sources=list(p["sources"]),
                  variant_source=p.get("source_by_length",{}).get(nl,p["source"]),
                  status=p["status"], depth_status=depth_status,
                  length_status=p["length_status"], confirmed_min_depth=confirmed_min_depth,
                  checked_on=CHECKED_ON,
                  cnc_patterns_available=False, article=article, construction=construction,
                  drawer_front_thickness=front_t, extension=p["extension"],
                  load_kg=p.get("load_by_length", {}).get(nl, p.get("load_kg")),
                  documented_travel=p.get("travel_by_length", {}).get(nl))
    return result


def available_lengths(profile, cabinet_depth):
    """Verified-depth fitting variants; unknown-depth variants are not selected."""
    p = get_profile(profile)
    depth = _positive(cabinet_depth, "Użyteczna głębokość korpusu")
    result = []
    for length in p["lengths"]:
        minimum = p.get("depth_by_length", {}).get(length)
        if minimum is None and p.get("depth_extra") is not None:
            minimum = length + p["depth_extra"]
        if minimum is not None and minimum <= depth + 1e-8:
            result.append(length)
    return result


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_construction as _install_cpp
_install_cpp(globals(), 'runners_catalog')
