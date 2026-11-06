import unittest,tempfile,copy,json
from pathlib import Path
from core import Project,world
from assembly import inferred_role
from furniture_library import create_furniture
from runners_catalog import PROFILES
from runner_installation import apply_runner
from opening_options import install
from direct_edit import resize_furniture
from dimension_model import resize_dimensions,regenerate_hinges
from productivity import insert_template,independent_copy
from cad_geometry import local_shape
from hardware_report import hardware_report

class UpdateTests(unittest.TestCase):
    def test_profiles_make_correct_box_and_no_guessed_drills(self):
        base,group=create_furniture(Project(),3);front=next(p for p in base.parts if p.role=='drawer');before=base.data()
        for profile in PROFILES:
            with self.subTest(profile=profile):
                nl=400 if 400 in PROFILES[profile]['lengths'] else 500
                options={'confirmed_min_depth':nl+4} if PROFILES[profile].get('depth_extra') is None and not PROFILES[profile].get('depth_by_length') else {}
                result=apply_runner(base,front.id,profile,nl,**options)
                h=next(h for h in result.hardware if h.get('runner_profile')==profile);plan=h['dimensions']
                children=[p for p in result.parts if p.assembly.get('generated_by')==front.id];sides=[p for p in children if p.plane=='YZ']
                self.assertEqual(len(children),5);self.assertAlmostEqual(sides[1].x+sides[1].t-sides[0].x,plan['drawer_outer_width'])
                self.assertAlmostEqual(sides[0].u,plan['drawer_length'])
                self.assertFalse(any(op.get('owner_id')==front.id and 'prowadnic' in op.get('purpose','') for p in result.parts for op in p.operations))
                if plan['mount']=='undermount':
                    bottom=next(p for p in children if p.plane=='XY');self.assertAlmostEqual(bottom.z-sides[0].z,12);self.assertAlmostEqual(bottom.u,plan['drawer_inner_width'])
                resized=resize_furniture(result,group,800,720,560)
                new=next(h for h in resized.hardware if h.get('runner_profile')==profile)
                self.assertAlmostEqual(new['dimensions']['drawer_outer_width']-plan['drawer_outer_width'],200)
                self.assertEqual(base.data(),before)
    def test_push_bore_tracks_resize_and_duplicate_ids(self):
        base,group=create_furniture(Project(),0);door=next(p for p in base.parts if p.role=='door');result=install(base,door.id,'tip_bumper')
        holes=[(p,o) for p in result.parts for o in p.operations if o.get('opening_owner')==door.id]
        self.assertEqual(len(holes),1);side,op=holes[0];self.assertEqual((op['face'],op['diameter'],op['depth']),('u0',10,76));self.assertAlmostEqual(op['v'],side.t/2)
        front=next(p for p in result.parts if p.id==door.id);self.assertAlmostEqual(front.y,-front.t-3.2)
        moved=resize_furniture(result,group,800,900,560)
        front=next(p for p in moved.parts if p.id==door.id);newside,newop=next((p,o) for p in moved.parts for o in p.operations if o.get('opening_owner')==door.id)
        self.assertAlmostEqual(newside.z+newop['u'],front.z+front.v/2)
        imported=insert_template(moved,moved,'Kopia',(1000,0,0));original_count=len([o for p in imported.parts for o in p.operations if o.get('opening_owner')==door.id]);self.assertEqual(original_count,1)
        imported.validate()
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'roundtrip.mebel';imported.save(path);loaded=Project.load(path);self.assertEqual(json.dumps(imported.data()),json.dumps(loaded.data()))
    def test_finger_machining_is_real_and_replaced(self):
        base,group=create_furniture(Project(),0);door=next(p for p in base.parts if p.role=='door');result=install(base,door.id,'finger_pocket',120,24,8)
        p=next(p for p in result.parts if p.id==door.id);self.assertEqual(len(p.machining),1)
        nominal=local_shape(door).Volume();self.assertGreater(nominal-local_shape(p).Volume(),20000)
        result=resize_dimensions(result,door.id,door.u,door.v-100,door.t);p=next(p for p in result.parts if p.id==door.id);self.assertAlmostEqual(p.machining[0]['v'],p.v/2)
        result=install(result,door.id,'finger_slot',100,20,8);p=next(p for p in result.parts if p.id==door.id);self.assertEqual(len(p.machining),1);self.assertEqual(p.machining[0]['type'],'cutout')
    def test_sensys_recipe_and_atomic_failed_option(self):
        base,group=create_furniture(Project(),0);door=next(p for p in base.parts if p.role=='door');result=regenerate_hinges(base,door.id,'hinge_sensys52')
        p=next(p for p in result.parts if p.id==door.id);cups=[o for o in p.operations if o['diameter']==35];self.assertTrue(cups);self.assertEqual(cups[0]['depth'],12.8)
        pilots=[o for o in p.operations if o.get('purpose')=='mocowanie puszki zawiasu'];self.assertAlmostEqual(cups[0]['u']-pilots[0]['u'],5.5);self.assertEqual(pilots[0]['diameter'],2.5)
        before=result.data()
        with self.assertRaises(ValueError):install(result,door.id,'finger_pocket',2000,24,8)
        self.assertEqual(result.data(),before)
    def test_report_contains_box_dimensions_and_sources(self):
        p,g=create_furniture(Project(),3);front=next(q for q in p.parts if q.role=='drawer');p=apply_runner(p,front.id,'blum_movento_760h',400)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'hardware.html';hardware_report(p,path);text=path.read_text(encoding='utf-8');self.assertIn('Szerokość zewnętrzna',text);self.assertIn('publications.blum.com',text)
    def test_inset_back_reduces_real_runner_space_atomically(self):
        p,g=create_furniture(Project(),3);front=next(q for q in p.parts if q.role=='drawer')
        back=next(q for q in p.parts if inferred_role(q)=='back');back.y=300
        before=p.data()
        with self.assertRaises(ValueError):apply_runner(p,front.id,'blum_movento_760h',400)
        self.assertEqual(p.data(),before)
        result=apply_runner(p,front.id,'blum_movento_760h',250)
        self.assertLessEqual(next(h['dimensions']['cabinet_depth'] for h in result.hardware if h.get('owner_id')==front.id and h.get('kind')=='runner'),300)
        self.assertTrue(all(q.y+q.u<300 for q in result.parts if q.assembly.get('generated_by')==front.id and q.plane=='YZ'))
    def test_magnetic_push_cannot_reuse_sprung_sensys(self):
        p,g=create_furniture(Project(),0);front=next(q for q in p.parts if q.role=='door')
        p=regenerate_hinges(p,front.id,'hinge_sensys52');before=p.data()
        with self.assertRaises(ValueError):install(p,front.id,'tip_short',hinge_spring='unsprung')
        self.assertEqual(p.data(),before)
        result=install(p,front.id,'tip_bumper',hinge_spring='sprung')
        hinge=next(h for h in result.hardware if h.get('owner_id')==front.id and h.get('kind')=='hinge')
        self.assertEqual(hinge['closing_spring'],'sprung');self.assertEqual(hinge['article'],'9071205')
    def test_later_hinge_change_preserves_opening_compatibility(self):
        p,g=create_furniture(Project(),0);front=next(q for q in p.parts if q.role=='door')
        p=install(p,front.id,'tip_short',hinge_spring='unsprung');before=p.data()
        with self.assertRaises(ValueError):regenerate_hinges(p,front.id,'hinge_sensys52')
        self.assertEqual(p.data(),before)
    def test_styled_import_retains_resize_origin_and_materials(self):
        from shaped_furniture import create_shaped_furniture,styled_bounds
        p,g=create_shaped_furniture(Project(),4,origin=(50,80,0))
        q=insert_template(Project(),p,'Taboret importowany',(1000,-50,30))
        q=resize_furniture(q,'Taboret importowany',500,600,500)
        f=styled_bounds(q,'Taboret importowany')
        self.assertEqual(f['lower'],(1050,30,30));self.assertEqual(f['upper'],(1550,530,630))
        self.assertTrue(all(part.material in {m.id for m in q.materials} for part in q.parts))
        self.assertFalse(independent_copy(q.parts[0]).style_source)
    def test_manual_curve_edits_do_not_reassign_edge_features(self):
        from core import Part
        from shaped_furniture import apply_shape
        from sketch import normalized_part
        from productivity import mirror_part
        p=Project();p.parts=[Part(u=400,v=400,t=28,material=p.materials[0].id)]
        p=apply_shape(p,p.parts[0].id,'circle');part=p.parts[0]
        part.fillets=[dict(edge='top:0',radius=3)];part.validate()
        before=copy.deepcopy(part)
        points=part.points()[:];points[1]=(points[1][0]-1,points[1][1])
        with self.assertRaises(ValueError):normalized_part(part,points)
        with self.assertRaises(ValueError):mirror_part(part,'u')
        self.assertEqual(part,before)

if __name__=='__main__':unittest.main()
