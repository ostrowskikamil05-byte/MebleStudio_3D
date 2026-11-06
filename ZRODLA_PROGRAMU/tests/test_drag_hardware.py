import copy
import math
import unittest
from unittest.mock import patch

from core import Project,Part,world
from configurator_accessories import led_proposal,install_manual_led,refresh_manual_led,led_local_geometry
from drag_hardware import attach,preview_attachment,refresh_manual_attachments
from fitting_visuals import fitting_faces
from furniture_configurator import configure,default_config,read_config,configuration_status
from assembly import delete_with_connections


def panel(plane='XY'):
    p=Project();part=Part(name='Panel',u=180,v=120,t=18,material=p.materials[0].id,plane=plane,
                         x=200,y=-40,z=100,rx=13,ry=7,rz=23,layout=dict(x=20,y=10,z=0,yaw=35))
    p.parts.append(part);return p,part


def cabinet(drawers=False):
    p=Project();c=default_config(3 if drawers else 0,p);c.update(width=600,height=820,depth=560)
    c['sections'][1].update(front='drawers' if drawers else 'doors',drawers=2,doors=2,shelves=0)
    return configure(p,c)


class LEDPlacementTests(unittest.TestCase):
    def test_led_preview_never_attaches_or_validates_entire_project(self):
        p,part=panel();before=p.data()
        with patch('drag_hardware.attach',side_effect=AssertionError('No commit during LED preview')),patch.object(Project,'validate',side_effect=AssertionError('No full project validation')):
            q=preview_attachment(p,'led',part.id,options=dict(face='top',groove=True))
        self.assertTrue(q['valid'],q['message']);self.assertTrue(q['world_faces']);self.assertEqual(p.data(),before)

    def test_all_six_faces_in_all_parent_planes_and_room_rotations(self):
        for plane in ('XY','XZ','YZ'):
            for face in ('top','bottom','u0','u1','v0','v1'):
                with self.subTest(plane=plane,face=face):
                    p,part=panel(plane);before=p.data()
                    q=attach(p,'led',part.id,options=dict(face=face,width=10,margin=20))
                    h=next(h for h in q.hardware if h['kind']=='led_strip')
                    self.assertEqual(p.data(),before);self.assertEqual(len(q.parts),1);self.assertFalse(q.parts[0].machining)
                    self.assertEqual(h['qty'],h['length']/1000);self.assertEqual(h['unit'],'m')
                    self.assertEqual(h['local_face'],face);self.assertFalse(h['config_auto']);self.assertTrue(h['manual'])
                    a,b,n=h['local_tangent'],h['local_cross'],h['local_normal']
                    self.assertAlmostEqual(sum(a[i]*n[i] for i in range(3)),0)
                    local=[point for polygon,color in led_local_geometry(part,h) for point in polygon]
                    for pt in local:
                        delta=[pt[i]-h['local_center'][i] for i in range(3)]
                        self.assertLessEqual(abs(sum(delta[i]*a[i] for i in range(3))),h['length']/2+.0001)
                        self.assertLessEqual(abs(sum(delta[i]*b[i] for i in range(3))),h['width']/2+.0001)
                        self.assertGreaterEqual(sum(delta[i]*n[i] for i in range(3)),-.0001)
                    faces=fitting_faces(q,{'led_strip'})
                    self.assertTrue(faces);self.assertEqual(faces[0][1][0],world(q.parts[0],*local[0]))

    def test_opt_in_groove_is_real_cad_on_both_broad_faces(self):
        from cad_geometry import local_shape
        for face in ('top','bottom'):
            p,part=panel();q=attach(p,'led',part.id,options=dict(face=face,width=12,margin=20,groove=True,depth=3))
            cut=q.parts[0].machining[0];h=next(h for h in q.hardware if h['kind']=='led_strip')
            expected=part.u*part.v*part.t-((h['length']-12)*12+math.pi*36)*3
            self.assertAlmostEqual(local_shape(q.parts[0]).Volume(),expected,places=4)
            self.assertEqual(cut['attachment_id'],h['attachment_id']);self.assertEqual(cut['face'],face)
            self.assertEqual(len(p.parts[0].machining),0)

    def test_invalid_face_dimensions_and_edge_groove_are_atomic(self):
        p,part=panel();before=p.data()
        for opts in (dict(face='u0',groove=True),dict(face='u0',width=50),dict(face='else'),dict(width=float('nan'))):
            self.assertFalse(preview_attachment(p,'led',part.id,options=opts)['valid'])
            with self.assertRaises(ValueError):attach(p,'led',part.id,options=opts)
            self.assertEqual(p.data(),before)

    def test_groove_collision_checks_same_and_opposite_drilling(self):
        p,part=panel();part.operations=[dict(type='drill',face='top',u=90,v=60,diameter=5,depth=10)]
        before=p.data()
        with self.assertRaises(ValueError):attach(p,'led',part.id,options=dict(face='top',groove=True))
        self.assertEqual(p.data(),before)
        q=attach(p,'led',part.id,options=dict(face='bottom',groove=True,depth=10))
        self.assertEqual(q.parts[0].machining[0]['depth'],7)

    def test_unique_records_preserve_qty_prices_and_refit_after_resize(self):
        p,part=panel();q=attach(p,'led',part.id,options=dict(face='top',price_per_m=10,profile_price_per_m=20,diffuser_price_per_m=30,groove=True))
        q=attach(q,'led',part.id,options=dict(face='bottom',price_per_m=5));ids={h['attachment_id'] for h in q.hardware}
        self.assertEqual(len(ids),2);self.assertEqual(len(q.hardware),6)
        q.parts[0].u=300;refresh_manual_led(q);q.validate()
        self.assertEqual({h['attachment_id'] for h in q.hardware},ids)
        self.assertTrue(all(h['length']==240 for h in q.hardware))
        self.assertEqual(sorted(h['price'] for h in q.hardware),[0,0,5,10,20,30])
        self.assertTrue(all('240 × 12' in h['note'] for h in q.hardware))
        self.assertEqual(len(q.parts[0].machining),1)
        self.assertEqual(len(p.hardware),0)

    def test_shaped_part_footprint_is_clipped_inside_actual_contour(self):
        p,part=panel();part.contour=[(0,0),(180,0),(180,60),(90,120),(0,120)]
        cfg=led_proposal(part,dict(u=120,v=60,w=18),dict(face='top',width=10,margin=0))
        self.assertLess(cfg['length'],180)
        q=attach(p,'led',part.id,dict(u=120,v=60,w=18),dict(face='top',width=10,margin=0,groove=True))
        q.validate()

    def test_delete_owner_removes_buy_records_and_no_parts_in_cutting_list(self):
        p,part=panel();q=attach(p,'led',part.id,options=dict(face='top',groove=True))
        delete_with_connections(q,part.id);q.validate()
        self.assertEqual(q.parts,[]);self.assertEqual(q.hardware,[])

    def test_metadata_winding_is_outward_on_each_rotated_surface(self):
        for plane in ('XY','XZ','YZ'):
            for face in ('top','bottom','u0','u1','v0','v1'):
                p,part=panel(plane);q=attach(p,'led',part.id,options=dict(face=face,width=10))
                h=next(h for h in q.hardware if h['kind']=='led_strip')
                for owner,polygon,color,sign in fitting_faces(q,{'led_strip'},with_metadata=True):
                    local_z=h['depth']/2 if color=='#949ba2' else h['depth']+.15
                    center=world(part,*[h['local_center'][i]+local_z*h['local_normal'][i] for i in range(3)])
                    a,b,c=polygon[:3];ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
                    normal=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                    fc=[sum(pt[i] for pt in polygon)/len(polygon) for i in range(3)]
                    self.assertGreater(sum((fc[i]-center[i])*normal[i]*sign for i in range(3)),0)


class FittingPlacementTests(unittest.TestCase):
    def test_corner_hinges_use_actual_face_and_survive_rebuild(self):
        from corner_furniture import default_corner_config,create_corner,read_corner_config,corner_status
        p=Project();c=default_corner_config(p);c['fronts']='orthogonal';p,g=create_corner(p,c)
        doors=[d for d in p.parts if d.role=='door'];self.assertEqual(len(doors),2)
        for door in doors:
            p=attach(p,'hinge',door.id,options=dict(recipe_id='hinge_sensys52',count=3,price=9))
            target=next(d for d in p.parts if d.id==door.id);face=target.assembly['config']['corner_bay']['cup_face']
            cups=[op for op in target.operations if op.get('purpose')=='puszka zawiasu']
            self.assertEqual(len(cups),3);self.assertTrue(all(op['face']==face for op in cups))
            h=next(h for h in p.hardware if h.get('owner_id')==door.id and h.get('kind')=='hinge')
            self.assertEqual((h['mount_face'],h['qty'],h['price']),(face,3,9))
        c=read_corner_config(p,g);c['height']+=100;q,g=create_corner(p,c,g);refresh_manual_attachments(q);q.validate()
        self.assertTrue(corner_status(q,g)['editable'])
        for door in doors:
            rows=[h for h in q.hardware if h.get('owner_id')==door.id and h.get('kind')=='hinge']
            self.assertEqual(len(rows),1);self.assertEqual(rows[0]['qty'],3)
        c=default_corner_config(Project());c.update(shape='chamfer',fronts='diagonal')
        base=Project();c=default_corner_config(base);c.update(shape='chamfer',fronts='diagonal');base,g=create_corner(base,c)
        front=next(d for d in base.parts if d.role=='door')
        self.assertFalse(preview_attachment(base,'hinge',front.id)['valid'])

    def test_hinge_preview_and_apply_replace_existing_pair_with_real_drills(self):
        p,g=cabinet();door=next(p for p in p.parts if p.role=='door');before=p.data()
        proposal=preview_attachment(p,'hinge',door.id,options=dict(recipe_id='hinge_sensys52',count=3,price=12))
        self.assertTrue(proposal['valid'],proposal['message']);self.assertTrue(proposal['world_faces']);self.assertEqual(p.data(),before)
        q=attach(p,'hinge',door.id,options=dict(recipe_id='hinge_sensys52',count=3,price=12))
        h=[h for h in q.hardware if h.get('owner_id')==door.id and h.get('kind')=='hinge'];self.assertEqual(len(h),1)
        self.assertEqual(h[0]['qty'],3);self.assertEqual(h[0]['price'],12)
        target=next(p for p in q.parts if p.id==door.id)
        self.assertEqual(len([o for o in target.operations if o.get('purpose')=='puszka zawiasu']),3)
        self.assertTrue(all(o['diameter']==35 and o['depth']==12.8 for o in target.operations if o.get('purpose')=='puszka zawiasu'))
        self.assertTrue(configuration_status(q,g)['editable'])

    def test_runner_rebuild_retains_creator_ids_and_manual_led_on_drawer_bottom(self):
        p,g=cabinet(True);front=next(p for p in p.parts if p.role=='drawer')
        bottom=next(p for p in p.parts if p.assembly.get('generated_by')==front.id and p.plane=='XY')
        p=attach(p,'led',bottom.id,options=dict(face='bottom',width=4))
        before=p.data();q=attach(p,'runner',front.id,options=dict(profile_id='hettich_ka5632',length=400,price=25))
        self.assertEqual(p.data(),before);self.assertEqual(len(q.parts),len(p.parts))
        self.assertTrue(any(p.id==bottom.id for p in q.parts));self.assertTrue(configuration_status(q,g)['editable'])
        runners=[h for h in q.hardware if h.get('owner_id')==front.id and h.get('kind')=='runner']
        self.assertEqual(len(runners),1);self.assertEqual(runners[0]['price'],25)
        self.assertTrue(any(h.get('owner_id')==bottom.id and h.get('kind')=='led_strip' for h in q.hardware))
        self.assertFalse(any(o.get('purpose')=='prowadnica korpusu' and o.get('owner_id')==front.id for p in q.parts for o in p.operations))

    def test_delete_drawer_front_cascades_manual_led_from_generated_bottom(self):
        p,g=cabinet(True);front=next(p for p in p.parts if p.role=='drawer')
        bottom=next(p for p in p.parts if p.assembly.get('generated_by')==front.id and p.plane=='XY')
        p=attach(p,'led',bottom.id,options=dict(face='bottom',width=4));q=copy.deepcopy(p)
        delete_with_connections(q,front.id);q.validate()
        self.assertFalse(any(h.get('owner_id')==bottom.id for h in q.hardware))
        self.assertTrue(any(h.get('owner_id')==bottom.id for h in p.hardware))

    def test_manual_led_and_hinges_survive_configure_without_duplicates(self):
        p,g=cabinet();door=next(p for p in p.parts if p.role=='door');side=next(p for p in p.parts if p.role=='left')
        p=attach(p,'hinge',door.id,options=dict(count=3,price=8));p=attach(p,'led',side.id,options=dict(face='top',price_per_m=15))
        cfg=read_config(p,g);cfg['width']+=80;q,newg=configure(p,cfg,g);refresh_manual_attachments(q);q.validate()
        h=[h for h in q.hardware if h.get('owner_id')==door.id and h.get('kind')=='hinge']
        self.assertEqual(len(h),1);self.assertEqual((h[0]['qty'],h[0]['price']),(3,8))
        self.assertEqual(len([h for h in q.hardware if h.get('configure_feature')=='manual_led' and h.get('kind')=='led_strip']),1)
        self.assertTrue(configuration_status(q,newg)['editable'])

    def test_invalid_parent_or_unfittable_profile_does_not_change_source(self):
        p,part=panel();before=p.data()
        for kind in ('hinge','runner'):
            self.assertFalse(preview_attachment(p,kind,part.id)['valid'])
            with self.assertRaises(ValueError):attach(p,kind,part.id)
        self.assertEqual(p.data(),before)
        p,g=cabinet(True);front=next(p for p in p.parts if p.role=='drawer');before=p.data()
        proposal=preview_attachment(p,'runner',front.id,options=dict(profile_id='hettich_ka5632',length=700))
        self.assertFalse(proposal['valid']);self.assertEqual(p.data(),before)

    def test_lights_drop_on_free_scene_point_or_rotated_panel_with_buy_price(self):
        p,part=panel();q=attach(p,'bulb',None,dict(world=(100,200,300)),dict(price=12))
        self.assertEqual(len(q.parts),1);self.assertEqual(q.scene['objects'][0]['z'],300)
        self.assertEqual(q.scene['objects'][0]['price'],12)
        q=attach(q,'lamp',part.id,dict(u=40,v=50,w=18),dict(offset=10))
        expected=world(part,40,50,18)
        self.assertAlmostEqual(q.scene['objects'][-1]['x']+175,expected[0])
        self.assertAlmostEqual(q.scene['objects'][-1]['z'],expected[2]+10)
        self.assertTrue(preview_attachment(p,'bulb',None,dict(world=(0,0,0)))['valid'])
        self.assertEqual(p.scene['objects'],[])


if __name__=='__main__':unittest.main()
