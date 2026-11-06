import copy
import math
import tempfile
import unittest
from pathlib import Path

from core import Project, Part, Camera, cut_plan
from room_objects import (CATALOG, validate_scene, make_object, add_object,
    update_object, delete_object, set_scene, object_vertices, object_bounds,
    object_lights, room_world_faces, room_faces, inventory, purchased_total)


class RoomObjectsTests(unittest.TestCase):
    def test_catalogue_has_all_16_types_and_geometry_inside_nominal_envelope(self):
        self.assertEqual(len(CATALOG),16)
        self.assertTrue({'washer','dryer','fridge','oven','hob','hood','dishwasher',
                         'washbasin','sink','faucet','shower','mirror','shelf','hanger','lamp','bulb'}==set(CATALOG))
        for kind in CATALOG:
            with self.subTest(kind=kind):
                p=add_object(Project(),kind,x=-320,y=145,z=0,yaw=31)
                obj=p.scene['objects'][0];lo,hi=object_bounds(obj)
                faces=room_world_faces(p)
                self.assertGreater(len(faces),3)
                for face in faces:
                    self.assertGreaterEqual(len(face['world']),3)
                    self.assertTrue(face['id'].startswith('scene:'))
                    self.assertTrue(face['scene_object'] and face['fitting'])
                    for pt in face['world']:
                        self.assertTrue(all(math.isfinite(v) for v in pt))
                        self.assertTrue(all(lo[i]-1e-6<=pt[i]<=hi[i]+1e-6 for i in range(3)),pt)

    def test_normalization_and_scene_validation_do_not_mutate_input(self):
        obj=make_object('washer',id='demo',yaw=450)
        source={'room':'bathroom','objects':[obj]}
        before=copy.deepcopy(source);result=validate_scene(source)
        self.assertEqual(source,before)
        self.assertEqual(result['brightness'],1)
        self.assertEqual(result['objects'][0]['yaw'],90)
        result['objects'][0]['name']='Inna'
        self.assertEqual(source,before)
        self.assertEqual(validate_scene({})['objects'],[])

    def test_invalid_scene_data_fails_with_value_error(self):
        obj=make_object('washer',id='demo')
        for scene in (None,{'room':{}},{'room':'bright'}, {'brightness':None}, {'brightness':float('nan')},
                      {'brightness':True},{'lights_enabled':'yes'},{'objects':{}},
                      {'objects':[obj,obj]}, {'objects':[{'id':'demo','type':{}}]},
                      {'objects':[{**obj,'width':-1}]}, {'objects':[{**obj,'price':-2}]},
                      {'objects':[{**obj,'enabled':1}]}, {'objects':[{**obj,'height':float('inf')}]},
                      {'objects':[{**obj,'x':None}]}, {'objects':[{**obj,'color':'#oopsxx'}]},
                      {'objects':[{**obj,'id':'a b'}]}, {'objects':[{**obj,'extra':'bad'}]}):
            with self.subTest(scene=scene):
                with self.assertRaises(ValueError):validate_scene(scene)

    def test_copy_edit_delete_and_failure_are_transactional(self):
        p=Project();before=p.data();q=add_object(p,'fridge',name='Własna lodówka',price=1900)
        self.assertEqual(p.data(),before)
        obj=q.scene['objects'][0];r=update_object(q,obj['id'],width=720,x=-500,yaw=90)
        self.assertEqual(r.scene['objects'][0]['id'],obj['id'])
        self.assertEqual(q.scene['objects'][0]['width'],600)
        saved=copy.deepcopy(r.data())
        with self.assertRaises(ValueError):update_object(r,obj['id'],width=0)
        with self.assertRaises(ValueError):update_object(r,'missing',width=300)
        with self.assertRaises(ValueError):update_object(r,obj['id'],id='changed')
        self.assertEqual(r.data(),saved)
        deleted=delete_object(r,obj['id']);self.assertEqual(deleted.scene['objects'],[])
        self.assertEqual(r.data(),saved)

    def test_equipment_is_not_a_panel_and_leaves_cutting_plan_unchanged(self):
        p=Project();p.parts=[Part(name='Płyta',u=400,v=300,t=18,material=p.materials[0].id)]
        before_parts=copy.deepcopy(p.parts);plan=cut_plan(p)
        q=add_object(p,'washer',price=1250);q=add_object(q,'lamp',price=80)
        self.assertEqual(q.parts,before_parts)
        self.assertEqual(cut_plan(q),plan)
        self.assertEqual(q.hardware,p.hardware)
        self.assertEqual(purchased_total(q),1330)
        self.assertEqual(sum(row['qty'] for row in inventory(q)),2)

    def test_room_save_load_includes_objects_and_lighting_and_legacy_default(self):
        p=add_object(Project(),'mirror',width=850)
        p=set_scene(p,room='bathroom',brightness=.7,led_enabled=False)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'scena.mebel';p.save(path);q=Project.load(path)
            self.assertEqual(q.data(),p.data())
            legacy=p.data();legacy.pop('scene');old=Project.from_data(legacy)
            self.assertEqual(validate_scene(old.scene)['objects'],[])

    def test_room_vertices_rotate_about_object_center_and_hide(self):
        p=add_object(Project(),'oven',width=600,depth=400,height=500,x=100,y=-200,z=40,yaw=90)
        verts=object_vertices(p);obj=p.scene['objects'][0]
        lo,hi=object_bounds(obj)
        self.assertAlmostEqual(hi[0]-lo[0],400)
        self.assertAlmostEqual(hi[1]-lo[1],600)
        self.assertEqual((lo[2],hi[2]),(40,540))
        p=update_object(p,obj['id'],enabled=False)
        self.assertEqual(object_vertices(p),[]);self.assertEqual(room_world_faces(p),[])
        self.assertEqual(len(object_vertices(p,include_disabled=True)),8)
        self.assertEqual(len(inventory(p)),1)

    def test_lights_positions_switches_and_visual_emission(self):
        p=add_object(Project(),'bulb',x=100,y=-50,z=1900,width=80,depth=100,height=120,yaw=90)
        lamp_id=p.scene['objects'][0]['id'];p=add_object(p,'lamp')
        lights=object_lights(p);self.assertEqual(len(lights),2)
        light=next(l for l in lights if l['id']==lamp_id)
        self.assertAlmostEqual(light['position'][0],140)
        self.assertAlmostEqual(light['position'][1],0)
        self.assertAlmostEqual(light['position'][2],1921.6)
        self.assertTrue(any(f['emissive'] for f in room_world_faces(p)))
        q=set_scene(p,lights_enabled=False)
        self.assertEqual(object_lights(q),[])
        self.assertTrue(room_world_faces(q))
        self.assertFalse(any(f['emissive'] for f in room_world_faces(q)))
        q=update_object(p,lamp_id,light_enabled=False)
        self.assertEqual(len(object_lights(q)),1)

    def test_projected_faces_keep_picking_and_world_lighting_metadata(self):
        p=add_object(Project(),'washer');ident=p.scene['objects'][0]['id']
        faces=room_faces(p,Camera(),(1000,700),selected=ident)
        self.assertTrue(all(f['selected'] and 'world' in f and 'center' in f and 'normal' in f for f in faces))
        self.assertEqual([f['depth'] for f in faces],sorted(f['depth'] for f in faces))
        self.assertLess(len(room_world_faces(p,interactive=True)),len(room_world_faces(p)))


if __name__=='__main__':unittest.main()
