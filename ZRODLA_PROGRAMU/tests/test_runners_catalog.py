import json
import math
import unittest

from runners_catalog import PROFILES, available_lengths, calculate_runner, get_profile


class RunnerPlanningTests(unittest.TestCase):
    def test_undermount_inside_vs_outside_width(self):
        r = calculate_runner("blum_movento_760h", 564, 500, 16, 510)
        self.assertEqual(522, r["drawer_inner_width"])
        self.assertEqual(554, r["drawer_outer_width"])
        self.assertEqual(5, r["external_clearance_per_side"])
        self.assertEqual(490, r["drawer_length"])
        self.assertEqual(503, r["min_cabinet_depth"])
        self.assertTrue(r["valid"])

    def test_thinner_undermount_side_changes_only_outer_width(self):
        r = calculate_runner("blum_movento_760h", 564, 500, 12, 503)
        self.assertEqual(522, r["drawer_inner_width"])
        self.assertEqual(546, r["drawer_outer_width"])
        self.assertEqual(9, r["external_clearance_per_side"])

    def test_side_runner_clearance_independent_of_side_thickness(self):
        for t in (12, 16, 18, 19):
            r = calculate_runner("hettich_ka5632", 564, 500, t, 504)
            self.assertAlmostEqual(538.6, r["drawer_outer_width"])
            self.assertAlmostEqual(12.7, r["external_clearance_per_side"])
            self.assertAlmostEqual(538.6 - 2*t, r["drawer_inner_width"])

    def test_hafele_real_recommended_clearance_includes_margin(self):
        r = calculate_runner("hafele_43244_full_extension", 564, 500, 18, 550)
        self.assertEqual(12.9, r["external_clearance_per_side"])
        self.assertEqual([12.9, 13.2], r["external_clearance_range"])
        self.assertAlmostEqual(538.2, r["drawer_outer_width"])
        self.assertFalse(r["planning_complete"])
        self.assertIsNone(r["min_cabinet_depth"])
        self.assertEqual("432.44.951", r["article"])

    def test_width_tolerance_transforms_into_clearance(self):
        r = calculate_runner("grass_dynapro_softclose_eb21", 564, 500, 16, 503)
        self.assertEqual([520.5,522], r["drawer_inner_width_range"])
        self.assertEqual([5,5.75], r["external_clearance_range"])

    def test_salice_three_and_four_sided_box(self):
        a = calculate_runner("salice_futura_a615_clip_cdn",564,500,16,520)
        b = calculate_runner("salice_futura_a615_clip_cdn",564,500,16,520,
                             construction="four_sided", drawer_front_thickness=16)
        self.assertEqual(490,a["drawer_length"])
        self.assertEqual(506,b["drawer_length"])
        self.assertEqual([522,523],a["drawer_inner_width_range"])
        self.assertEqual([4.5,5],a["external_clearance_range"])
        with self.assertRaisesRegex(ValueError,"subfront"):
            calculate_runner("salice_futura_a615_clip_cdn",564,500,16,520,construction="four_sided")

    def test_side_thickness_limit_is_not_silently_clamped(self):
        r = calculate_runner("blum_movento_760h",564,500,18,520)
        self.assertFalse(r["valid"])
        self.assertEqual(18,r["side_thickness"])
        self.assertTrue(any("16 mm" in s for s in r["errors"]))

    def test_too_shallow_depth_reports_fit_error(self):
        r = calculate_runner("grass_dynapro_softclose_eb21",564,500,16,502)
        self.assertFalse(r["valid"])
        self.assertFalse(r["fits_depth"])
        self.assertTrue(r["fits_runner_envelope"])

    def test_envelope_is_not_manufacturer_mounting_depth(self):
        r = calculate_runner("hafele_43244_full_extension",564,450,16,450)
        self.assertTrue(r["fits_runner_envelope"])
        self.assertIsNone(r["fits_depth"])
        self.assertFalse(r["planning_complete"])
        self.assertTrue(any("niezweryfikowana" in s for s in r["warnings"]))

    def test_even_envelope_must_fit(self):
        r = calculate_runner("hafele_43244_full_extension",564,500,16,490)
        self.assertFalse(r["valid"])
        self.assertFalse(r["fits_runner_envelope"])

    def test_grass_minimum_inside_width(self):
        r = calculate_runner("grass_dynapro_softclose_eb21",170,300,16,400)
        self.assertEqual(128,r["drawer_inner_width"])
        self.assertFalse(r["valid"])
        self.assertTrue(any("138" in s for s in r["errors"]))

    def test_negative_space_rejected_as_fit_error(self):
        r = calculate_runner("hettich_ka5632",40,500,16,600)
        self.assertFalse(r["valid"])

    def test_invalid_numbers_rejected(self):
        for bad in (math.nan, math.inf,-math.inf,0,-1,True,"bad",None,10001):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                calculate_runner("blum_movento_760h",bad,500,16,600)

    def test_unavailable_length_is_not_rounded_to_nearest(self):
        with self.assertRaisesRegex(ValueError,"brak zweryfikowanego"):
            calculate_runner("blum_movento_760h",564,510,16,600)

    def test_unknown_product_rejected(self):
        with self.assertRaisesRegex(ValueError,"Nieznany"):
            calculate_runner("all_runners",564,500,16,600)

    def test_auto_selection_only_uses_verified_depth(self):
        self.assertEqual([250,300,350,400,450,500],available_lengths("hettich_ka5632",504))
        self.assertEqual([],available_lengths("hafele_43244_full_extension",600))
        self.assertEqual(480,max(available_lengths("blum_movento_760h",500)))

    def test_quadro_you_and_5d_have_different_clips_and_recesses(self):
        a=calculate_runner("hettich_quadro_25_you_eb21",564,500,16,503)
        b=calculate_runner("hettich_quadro_v6_5d_eb21_push",564,500,16,503)
        self.assertEqual(50,a["front_recess"])
        self.assertEqual(75,b["front_recess"])
        self.assertEqual("partial",a["extension"])
        self.assertEqual("full",b["extension"])
        self.assertEqual(5,b["external_clearance_per_side"])

    def test_ka4532_exceptional_travel_is_explicit(self):
        r=calculate_runner("hettich_ka4532_push",564,300,16,600)
        self.assertEqual(293,r["documented_travel"])
        self.assertEqual("9302506",r["article"])
        self.assertEqual(304,r["min_cabinet_depth"])
        self.assertTrue(r["variant_source"].endswith("/p/9302506"))

    def test_hafele_load_is_length_dependent(self):
        a=calculate_runner("hafele_43244_full_extension",564,250,16,600)
        b=calculate_runner("hafele_43244_full_extension",564,500,16,600)
        self.assertEqual(41,a["load_kg"])
        self.assertEqual(45,b["load_kg"])

    def test_all_results_serializable_and_width_conserved(self):
        self.assertEqual(5,len({p["manufacturer"] for p in PROFILES.values()}))
        for profile in PROFILES:
            r=calculate_runner(profile,564,500,16,600)
            json.dumps(r,ensure_ascii=False,allow_nan=False)
            self.assertAlmostEqual(564,r["drawer_outer_width"]+2*r["external_clearance_per_side"])
            self.assertAlmostEqual(r["drawer_outer_width"],r["drawer_inner_width"]+32)
            self.assertTrue(r["sources"])
            self.assertFalse(r["cnc_patterns_available"])

    def test_returned_profile_is_independent(self):
        a=get_profile("blum_movento_760h")
        a["lengths"].clear()
        self.assertTrue(PROFILES["blum_movento_760h"]["lengths"])
        self.assertEqual("blum_movento_760h",get_profile(PROFILES["blum_movento_760h"])["id"])

    def test_quadro_5d_tolerance_not_copied_from_you(self):
        a=calculate_runner('hettich_quadro_v6_5d_eb21_push',564,500,16,503)
        b=calculate_runner('hettich_quadro_25_you_eb21',564,500,16,503)
        self.assertEqual([520.5,522],a['drawer_inner_width_range'])
        self.assertEqual([522,522],b['drawer_inner_width_range'])

    def test_actro_classes_and_opening_variants_have_distinct_articles(self):
        expected={'hettich_actro5d_silent_40':('9318288',40),
                  'hettich_actro5d_silent_70':('9318311',70),
                  'hettich_actro5d_push_40':('9320622',40),
                  'hettich_actro5d_push_70':('9320627',70)}
        for key,(article,load) in expected.items():
            r=calculate_runner(key,564,500,16,503)
            self.assertEqual(article,r['article']);self.assertEqual(load,r['load_kg'])
            self.assertEqual(490,r['drawer_length']);self.assertEqual(15,r['bottom_recess_max'])
            self.assertEqual([520.5,522],r['drawer_inner_width_range'])
        with self.assertRaises(ValueError):calculate_runner('hettich_actro5d_silent_70',564,400,16,503)

    def test_whole_salice_box_must_fit_even_when_runner_does(self):
        r=calculate_runner('salice_futura_a615_clip_cdn',564,500,16,503,
                           construction='four_sided',drawer_front_thickness=16)
        self.assertTrue(r['fits_runner_envelope']);self.assertTrue(r['fits_depth'])
        self.assertFalse(r['fits_drawer_envelope']);self.assertFalse(r['valid'])
        self.assertTrue(any('Cała skrzynka' in error for error in r['errors']))

    def test_unverified_depth_can_be_explicitly_confirmed_without_claiming_source(self):
        r=calculate_runner('hafele_43244_full_extension',564,500,16,510,confirmed_min_depth=507)
        self.assertEqual(507,r['min_cabinet_depth']);self.assertEqual('user_confirmed',r['depth_status'])
        self.assertEqual('workshop',r['length_status']);self.assertFalse(r['planning_complete'])
        self.assertTrue(r['valid'])
        with self.assertRaisesRegex(ValueError,'nie może'):
            calculate_runner('hafele_43244_full_extension',564,500,16,510,confirmed_min_depth=499)

    def test_side_runner_box_length_is_not_false_manufacturer_standard(self):
        for profile in ('hettich_ka5632','hettich_ka4532_push','hafele_43244_full_extension'):
            r=calculate_runner(profile,564,500,16,550)
            self.assertEqual('workshop',r['length_status'])
            self.assertFalse(r['planning_complete'])


class InstallationConstraintsTests(unittest.TestCase):
    def fixture(self,back_y=505):
        from core import Part,Project
        p=Project();group='Próba'
        p.parts=[Part(group=group,plane='YZ',u=560,v=720,t=18,role='left'),
                 Part(group=group,plane='YZ',u=560,v=720,t=18,x=582,role='right'),
                 Part(group=group,plane='XY',u=564,v=560,t=18,x=18,role='bottom'),
                 Part(group=group,plane='XY',u=564,v=560,t=18,x=18,z=702,role='top'),
                 Part(group=group,plane='XZ',u=600,v=720,t=18,y=back_y,role='back')]
        front=Part(group=group,plane='XZ',u=596,v=200,t=18,y=-20,z=30,role='drawer')
        p.parts.append(front);return p,front

    def test_inset_back_reduces_depth_and_offsets_count_once(self):
        from runner_installation import drawer_depth_context
        p,front=self.fixture();r=drawer_depth_context(p,front)
        self.assertEqual(20,r['start_y']);self.assertEqual(503,r['end_y']);self.assertEqual(483,r['usable_depth'])
        self.assertEqual([p.parts[4].id],r['back_ids'])
        for part in p.parts:part.y+=1000
        r=drawer_depth_context(p,front)
        self.assertEqual(483,r['usable_depth']);self.assertEqual(1020,r['start_y']);self.assertEqual(1503,r['end_y'])

    def test_overlay_back_does_not_reduce_carcass_depth(self):
        from runner_installation import drawer_depth_context
        p,front=self.fixture(560);r=drawer_depth_context(p,front)
        self.assertEqual(538,r['usable_depth']);self.assertEqual([],r['back_ids'])

    def test_magnetic_push_requires_unsprung_and_sensys_is_not_unsprung(self):
        from core import Project
        from furniture_library import create_furniture
        from opening_options import install
        from dimension_model import regenerate_hinges
        p,g=create_furniture(Project(),0);front=next(part for part in p.parts if part.role=='door');before=p.data()
        with self.assertRaisesRegex(ValueError,'bez sprężyny'):install(p,front.id,'tip_short')
        self.assertEqual(before,p.data())
        result=install(p,front.id,'tip_short',hinge_spring='unsprung')
        f=next(part for part in result.parts if part.id==front.id)
        self.assertEqual('unsprung',f.assembly['config']['hinge_spring'])
        original=result.data()
        with self.assertRaisesRegex(ValueError,'TIP-ON'):regenerate_hinges(result,front.id,'hinge_sensys52')
        self.assertEqual(original,result.data())

    def test_perfectsense_presets_do_not_mix_chipboard_thicknesses(self):
        from material_presets import PRESETS
        names=[name for name in PRESETS if 'PerfectSense' in name]
        self.assertEqual(2,len(names))
        for name in names:
            spec=PRESETS[name]
            self.assertEqual((10,12,16,18,19,22,25),spec['thicknesses'])
            self.assertIn('country=PL',spec['source']);self.assertEqual('mdf',spec['kind'])


if __name__ == "__main__":
    unittest.main()
