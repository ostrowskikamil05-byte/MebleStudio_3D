import copy
import ctypes
import json
from pathlib import Path
import random
import tempfile
import unittest
from core import Project,Part,add_template,cut_plan,world
import native_model

@unittest.skipUnless(native_model.available(),'C++ calculation library is required')
class NativeModelTests(unittest.TestCase):
    def test_real_calculations_are_routed(self):
        from shaped_furniture import contour
        from runners_catalog import calculate_runner
        from features import validate_drill
        from core import validate_polygon
        for function in (cut_plan,Project.costs,Project.save,Project.load,contour,calculate_runner,validate_drill,validate_polygon):
            self.assertTrue(getattr(function,'_native_cpp',False),function)

    def test_binary_transform_has_original_coordinates_and_winding(self):
        from viewport_render import transform
        rng=random.Random(15015)
        for plane,sign in [('XY',1),('XZ',-1),('YZ',1)]:
            for unused in range(15):
                part=Part(plane=plane,x=rng.uniform(-500,500),y=120,z=400,
                          rx=rng.uniform(-180,180),ry=rng.uniform(-180,180),rz=rng.uniform(-180,180),
                          layout=dict(x=100,y=-50,z=20,yaw=rng.choice([0,90,180,37,-90])))
                points=tuple((rng.random()*600,rng.random()*400,rng.random()*18) for i in range(70))
                actual,actual_sign=transform(part,points)
                self.assertEqual(actual_sign,sign)
                for source,target in zip(points,actual):
                    for a,b in zip(world(part,*source),target):self.assertAlmostEqual(a,b,places=7)

    def test_invalid_batch_does_not_change_production_data(self):
        from viewport_render import transform
        part=Part();before=copy.deepcopy(part.__dict__)
        with self.assertRaises(ValueError):transform(part,[(float('nan'),0,0)])
        self.assertEqual(part.__dict__,before)

    def test_binary_cache_reuses_immutable_mesh_and_refreshes_mutable_vertices(self):
        points=((0.,0.,0.),(10.,20.,18.));part=Part()
        native_model.transform(part,points);buffer=native_model._vertex_buffers[id(points)][1]
        part.x=100;native_model.transform(part,points)
        self.assertIs(buffer,native_model._vertex_buffers[id(points)][1])
        editable=[[0.,0.,0.],[10.,20.,18.]]
        first=native_model.transform(part,editable)[0]
        editable[0][0]=75
        second=native_model.transform(part,editable)[0]
        self.assertEqual(second[0][0]-first[0][0],75)
        self.assertNotIn(id(editable),native_model._vertex_buffers)

    def test_cut_plan_retains_tuple_contract(self):
        project=Project();add_template(project,'cabinet')
        plan=cut_plan(project)
        self.assertIsInstance(plan,tuple)
        self.assertIsInstance(plan[0],list)
        for sheet in plan[0]:
            for rectangle in sheet['free']:self.assertIsInstance(rectangle,tuple)

    def test_profile_dictionary_keeps_key_types_and_duplicates(self):
        from runners_catalog import PROFILES,get_profile,calculate_runner,available_lengths
        original=copy.deepcopy(PROFILES['hettich_ka5632'])
        profile=copy.deepcopy(original);profile['depth_by_length']={500:506,'500':999}
        actual=get_profile(profile)
        self.assertEqual(actual['depth_by_length'],profile['depth_by_length'])
        self.assertEqual(actual['id'],'custom')
        self.assertEqual(calculate_runner(profile,700,500,16,506)['min_cabinet_depth'],506)
        self.assertIn(500,available_lengths(profile,506))

    def test_manual_array_has_distinct_batch_ids(self):
        from features import hole_array
        part=Part()
        a=hole_array(part,40,40,5,10,2,3)
        b=hole_array(part,40,40,5,10,2,3)
        self.assertEqual(len(a),6)
        self.assertEqual(len({op['batch'] for op in a}),1)
        self.assertNotEqual(a[0]['batch'],b[0]['batch'])

    def test_exact_cad_stock_proxy_keeps_drill_location_contract(self):
        from types import SimpleNamespace
        from features import drill_location
        stock=SimpleNamespace(u=600,v=400,t=18)
        for face in ('top','bottom','u0','u1','v0','v1'):
            operation=dict(type='drill',face=face,u=40,v=30,diameter=5,depth=10)
            self.assertEqual(drill_location(stock,operation),drill_location.__wrapped__(stock,operation))

    def test_responsive_native_pixel_values_can_configure_tk(self):
        from responsive_ui import flow_layout,clamp_inspector
        rectangles,height=flow_layout(110,[(80,30),(95,40)])
        self.assertTrue(all(type(value) is int for rectangle in rectangles for value in rectangle))
        self.assertIs(type(height),int)
        self.assertIs(type(clamp_inspector(330,1000)),int)
        self.assertEqual((rectangles,height),flow_layout.__wrapped__(110,[(80,30),(95,40)]))

    def test_project_persistence_unicode_and_failed_save(self):
        project=Project(name='Kuchnia i Łazienka');add_template(project,'cabinet')
        with tempfile.TemporaryDirectory(prefix='MebelCpp_Łódź_') as folder:
            path=Path(folder)/'Projekt klienta – szafki.mebel'
            project.save(path);before=path.read_bytes()
            self.assertEqual(Project.load(path).data(),project.data())
            self.assertEqual(json.dumps(Project.load(path).data()),json.dumps(project.data()))
            broken=copy.deepcopy(project);broken.parts[0].t=-1
            with self.assertRaises(ValueError):broken.save(path)
            self.assertEqual(path.read_bytes(),before)
            with self.assertRaises(OSError):project.save(Path(folder))
            self.assertFalse(list(Path(folder).glob('*.tmp')))
            with self.assertRaises(FileNotFoundError):Project.load(Path(folder)/'missing.mebel')

    def test_native_error_is_not_silently_retried(self):
        with self.assertRaisesRegex(ValueError,'nie została jeszcze przeniesiona'):
            native_model.call('unmigrated','operation')

    def test_owned_buffers_are_released_on_success_and_error(self):
        for i in range(300):
            native_model.call('core','polygon_area',[(0,0),(600,0),(600,400),(0,400)])
            with self.assertRaises(ValueError):native_model.call('core','validate_polygon',[(0,0),(0,0),(1,0)])
        self.assertEqual(native_model.diagnostics()['outstanding_buffers'],0)

    def test_contours_do_not_mutate_configurator_source(self):
        from shaped_furniture import shape_definition,contour
        profile=shape_definition('rounded',600,400,radii=(10,20,30,40))
        before=copy.deepcopy(profile)
        native_model.call('shaped_furniture','profile_area',profile,600,400)
        self.assertEqual(profile,before)

if __name__=='__main__':unittest.main()
