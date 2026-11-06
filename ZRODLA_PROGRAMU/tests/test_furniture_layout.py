import copy
import json
import math
import tempfile
import unittest
from pathlib import Path

from core import Project,Part,world,local_world,layout_point,inverse_layout_point,add_template
from furniture_layout import (groups,group_bounds,local_group_bounds,group_layout,
                              move_group,rotate_group,snap_group,nearest_snap,contacts)


class FurnitureLayoutTests(unittest.TestCase):
    def boxes(self):
        project=Project()
        project.parts=[Part(name='A',group='A',u=600,v=400,t=800,material=project.materials[0].id),
                       Part(name='B',group='B',u=600,v=400,t=800,x=1500,material=project.materials[0].id)]
        return project

    def assertPoint(self,a,b):
        for x,y in zip(a,b):self.assertAlmostEqual(x,y,places=6)

    def test_move_is_rigid_atomic_and_preserves_production(self):
        p=self.boxes();before=p.data();q=move_group(p,'A',100,-70,30)
        self.assertEqual(before,p.data());self.assertEqual(q.parts[0].x,p.parts[0].x)
        self.assertPoint(world(q.parts[0],230,90,100),(330,20,130))
        self.assertEqual(local_group_bounds(p,'A'),local_group_bounds(q,'A'))
        self.assertEqual(group_bounds(p,'B'),group_bounds(q,'B'))

    def test_rotation_about_centre_preserves_height_and_thickness(self):
        p=self.boxes();q=rotate_group(p,'A',90)
        self.assertPoint(group_bounds(q,'A')[0],(100,-100,0))
        self.assertPoint(group_bounds(q,'A')[1],(500,500,800))
        self.assertEqual(q.parts[0].rz,0);self.assertEqual(q.parts[0].t,800)
        restored=rotate_group(q,'A',-90)
        self.assertPoint(world(restored.parts[0],200,300,20),world(p.parts[0],200,300,20))

    def test_custom_pivot_and_inverse_roundtrip(self):
        p=move_group(self.boxes(),'A',110,-80,200)
        q=rotate_group(p,'A',37,(0,0,0));part=q.parts[0]
        pt=(170,20,35);self.assertPoint(inverse_layout_point(part,layout_point(part,pt)),pt)
        self.assertEqual(group_layout(q,'A')['yaw'],37)

    def test_project_save_load_and_legacy_default(self):
        p=rotate_group(move_group(self.boxes(),'A',550,-70,30),'A',90)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'uklad.mebel';p.save(path);q=Project.load(path)
            self.assertEqual(p.data(),q.data())
        old=self.boxes().data()
        for part in old['parts']:part.pop('layout')
        loaded=Project.from_data(old);self.assertEqual(loaded.parts[0].layout,{})
        self.assertPoint(world(loaded.parts[0],200,40,30),(200,40,30))

    def test_invalid_layout_is_rejected(self):
        for layout in ([0],{'x':float('nan')},{'yaw':float('inf')},{'pivot':[0,0,0]}):
            p=self.boxes();p.parts[0].layout=layout
            with self.assertRaises(ValueError):p.validate()

    def test_all_six_contact_directions(self):
        for direction in ('x+','x-','y+','y-','z+','z-'):
            with self.subTest(direction=direction):
                p=self.boxes();q=snap_group(p,'B','A',direction);a,b=group_bounds(q,'A'),group_bounds(q,'B')
                axis='xyz'.index(direction[0]);positive=direction[1]=='+'
                self.assertAlmostEqual(a[1 if positive else 0][axis],b[0 if positive else 1][axis])
                self.assertEqual(len(contacts(q,'B')),1)
                self.assertEqual(group_bounds(p,'B')[0][0],1500)

    def test_contact_gap_and_alignments(self):
        p=self.boxes();p.parts[1].v=200;p.parts[1].t=500
        for alignment,expected in [('start',(610,0,0)),('center',(610,100,150)),('end',(610,200,300))]:
            q=snap_group(p,'B','A','x+',alignment,10)
            self.assertPoint(group_bounds(q,'B')[0],expected)
            self.assertEqual(contacts(q),[])

    def test_L_and_U_arrangements_keep_dimensions(self):
        p=self.boxes();p=rotate_group(p,'B',90);p=snap_group(p,'B','A','x+')
        p.parts.append(Part(name='C',group='C',u=600,v=400,t=800,x=3000,material=p.materials[0].id))
        p=rotate_group(p,'C',90);p=snap_group(p,'C','A','x-')
        self.assertEqual(len(contacts(p)),2)
        self.assertEqual([part.u for part in p.parts],[600,600,600])
        self.assertEqual([part.layout['yaw'] for part in p.parts[1:]],[90,90])
        self.assertTrue(all(abs(value-round(value))<1e-6 for name in groups(p) for corner in group_bounds(p,name) for value in corner))

    def test_nearest_magnet_and_separation(self):
        p=self.boxes();p=move_group(p,'B',-884,8,5)
        q,info=nearest_snap(p,'B',25)
        self.assertIsNotNone(info);self.assertEqual(info['target_group'],'A');self.assertEqual(info['direction'],'x+')
        self.assertPoint(group_bounds(q,'B')[0],(600,0,0))
        self.assertEqual(len(contacts(q)),1)
        self.assertEqual(contacts(move_group(q,'B',100,0,0)),[])

    def test_near_small_penetration_can_snap_out(self):
        p=move_group(self.boxes(),'B',-910,0,0)
        q,info=nearest_snap(p,'B',25)
        self.assertIsNotNone(info);self.assertAlmostEqual(group_bounds(q,'B')[0][0],600)

    def test_magnet_never_teleports_or_joins_corner_only(self):
        p=self.boxes();q,info=nearest_snap(p,'B',25)
        self.assertIsNone(info);self.assertEqual(p.data(),q.data())
        p=move_group(p,'B',-900,400,0);q,info=nearest_snap(p,'B',25,False)
        self.assertIsNone(info)

    def test_third_cabinet_collision_is_atomic(self):
        p=self.boxes();p.parts.append(Part(group='C',u=600,v=400,t=800,x=600,material=p.materials[0].id));before=p.data()
        with self.assertRaises(ValueError):snap_group(p,'B','A','x+')
        self.assertEqual(p.data(),before)
        p=move_group(p,'B',-890,0,0);q,info=nearest_snap(p,'B',25)
        self.assertIsNone(info)

    def test_custom_angle_moves_but_no_false_wall_snap(self):
        p=rotate_group(self.boxes(),'B',35)
        p=move_group(p,'B',20,30,40)
        q,info=nearest_snap(p,'B',25);self.assertIsNone(info)
        with self.assertRaisesRegex(ValueError,'90'):snap_group(p,'B','A','x+')

    def test_rotated_cabinet_resizing_and_new_shelf_stay_local(self):
        from assembly import attach,suggested_size,frame
        from direct_edit import resize_furniture
        p=Project();add_template(p,'cabinet',600,800,500,shelves=0,front=False,group='A')
        p=rotate_group(move_group(p,'A',1000,900,120),'A',90);layout=group_layout(p,'A')
        p=resize_furniture(p,'A',700,850,550)
        self.assertEqual(group_layout(p,'A'),layout);self.assertEqual(frame(p,'A')['upper'],(700,550,850))
        u,v=suggested_size(p,'A','shelf',18)
        p,shelf=attach(p,Part(u=u,v=v,t=18,material=p.materials[0].id),'A','shelf',{'level':350})
        self.assertEqual(shelf.layout,layout);self.assertEqual(len(p.hardware),1);p.validate()

    def test_rotated_front_hinges_fit_opening_and_motion_are_equivariant(self):
        from furniture_library import create_furniture
        from dimension_model import regenerate_hinges,fit_part
        from mechanisms import posed_project
        from opening_options import install
        from fitting_visuals import fitting_faces
        p,g=create_furniture(Project(),0);door=next(part for part in p.parts if part.role=='door')
        p=install(p,door.id,'tip_bumper')
        rotated=rotate_group(move_group(p,g,1000,-500,80),g,90)
        regenerated=regenerate_hinges(rotated,door.id,count=3)
        regenerated=fit_part(regenerated,door.id);regenerated.validate()
        self.assertEqual(sum(o.get('purpose')=='puszka zawiasu' for part in regenerated.parts if part.id==door.id for o in part.operations),3)
        a,b=posed_project(p,{door.id:.75}),posed_project(rotated,{door.id:.75})
        for original,placed in zip(a.parts,b.parts):
            self.assertPoint(world(placed,original.u*.4,original.v*.3,original.t),layout_point(placed,world(original,original.u*.4,original.v*.3,original.t)))
        fa,fb=fitting_faces(a),fitting_faces(b);self.assertEqual(len(fa),len(fb))
        lookup={part.id:part for part in b.parts}
        for (oa,face_a,color_a),(ob,face_b,color_b) in zip(fa,fb):
            self.assertEqual((oa,color_a),(ob,color_b))
            for local,placed in zip(face_a,face_b):self.assertPoint(placed,layout_point(lookup[ob],local))

    def test_rotated_drawer_children_and_runner_follow_furniture(self):
        from furniture_library import create_furniture
        from runner_installation import apply_runner
        from mechanisms import posed_project
        from fitting_visuals import fitting_faces
        p,g=create_furniture(Project(),3);door=next(part for part in p.parts if part.role=='drawer')
        p=rotate_group(move_group(p,g,1800,-200,40),g,-90)
        p=apply_runner(p,door.id,'blum_movento_760h',450)
        layout=group_layout(p,g);children=[part for part in p.parts if part.assembly.get('generated_by')==door.id]
        self.assertTrue(children);self.assertTrue(all(part.layout==layout for part in children))
        posed=posed_project(p,{door.id:1});original={part.id:part for part in p.parts}
        for child in posed.parts:
            if child.id==door.id or child.assembly.get('generated_by')==door.id:
                start=world(original[child.id],0,0,0);end=world(child,0,0,0)
                self.assertAlmostEqual(end[0]-start[0],-450);self.assertAlmostEqual(end[1]-start[1],0)
        self.assertTrue(fitting_faces(posed));p.validate()

    def test_styled_resize_retains_placement(self):
        from shaped_furniture import create_shaped_furniture
        from direct_edit import resize_furniture
        p,g=create_shaped_furniture(Project(),4)
        p=rotate_group(move_group(p,g,900,200,100),g,35)
        layout=group_layout(p,g);q=resize_furniture(p,g,500,600,500)
        self.assertEqual(layout,group_layout(q,g));self.assertEqual(local_group_bounds(q,g)[1][2],600)

    def test_import_rotated_furniture_offsets_in_room_coordinates(self):
        from productivity import insert_template,extract_group
        from furniture_library import create_furniture
        p,g=create_furniture(Project(),3);p=rotate_group(move_group(p,g,400,700,10),g,90)
        source=extract_group(p,g);q=insert_template(Project(),source,'B',(1000,-100,50))
        self.assertPoint(group_bounds(q,'B')[0],tuple(a+b for a,b in zip(group_bounds(p,g)[0],(1000,-100,50))))
        self.assertEqual(source.hardware[0]['positions'],q.hardware[0]['positions']);q.validate()

    def test_minimum_face_resize_stays_in_local_space(self):
        from direct_edit import resize_part
        p=self.boxes();p.parts=p.parts[:1];p=rotate_group(move_group(p,'A',500,300,20),'A',90)
        q=resize_part(p,p.parts[0].id,'u0',50)
        self.assertEqual(q.parts[0].x,50);self.assertEqual(q.parts[0].u,550)
        self.assertEqual(q.parts[0].layout,p.parts[0].layout)

    def test_step_uses_furniture_placement(self):
        from cad_geometry import step_export,kernel
        p=self.boxes();p.parts=p.parts[:1];p=rotate_group(move_group(p,'A',1000,-400,20),'A',90)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'uklad.step';step_export(p,path)
            bb=kernel().importers.importStep(str(path)).val().BoundingBox()
            lo,hi=group_bounds(p,'A');self.assertPoint((bb.xmin,bb.ymin,bb.zmin),lo);self.assertPoint((bb.xmax,bb.ymax,bb.zmax),hi)

    def test_production_json_retains_room_transform_and_local_drills(self):
        from core import manufacturing_export
        p=self.boxes();p.parts=p.parts[:1]
        p.parts[0].holes=[[200,100,8,20]]
        p=rotate_group(move_group(p,'A',1000,-400,20),'A',90)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'zestaw.json';manufacturing_export(p,path);data=json.loads(path.read_text(encoding='utf-8'))
        exported=data['parts'][0]
        self.assertEqual(exported['assembly']['room_placement'],p.parts[0].layout)
        self.assertPoint(exported['assembly']['world_origin'],world(p.parts[0],0,0,0))
        self.assertEqual(exported['drilling'][0]['u'],200);self.assertEqual(exported['drilling'][0]['v'],100)
        self.assertEqual(exported['finished_size'],[600,400])

    def test_all_hardware_world_geometry_obeys_rigid_layout(self):
        from furniture_library import create_furniture
        from fitting_visuals import fitting_faces
        from mechanisms import posed_project
        for index in (3,4,9,21):
            with self.subTest(template=index):
                p,g=create_furniture(Project(),index);front=next(part for part in p.parts if part.role in ('drawer','door'))
                q=rotate_group(move_group(p,g,600,-320,85),g,90)
                p=posed_project(p,{front.id:.6});q=posed_project(q,{front.id:.6})
                fa,fb=fitting_faces(p),fitting_faces(q);self.assertEqual(len(fa),len(fb));lookup={part.id:part for part in q.parts}
                for (oa,face_a,color_a),(ob,face_b,color_b) in zip(fa,fb):
                    self.assertEqual((oa,color_a),(ob,color_b))
                    for local,placed in zip(face_a,face_b):self.assertPoint(placed,layout_point(lookup[ob],local))


if __name__=='__main__':unittest.main()
