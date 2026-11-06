import math,unittest
from types import SimpleNamespace
from core import Project,Part,Camera,world
from room_objects import add_object,set_scene,default_scene
from room_architecture import world_faces,vertices,validate
from room_scene_drag import snap_object,begin,move,end,cancel
from attachment_picker import inverse_world,pick
from depth_raster import DepthRaster
from viewport_annotations import _box_edges

class ArchitecturePlacementTests(unittest.TestCase):
    def test_scene_roundtrip_and_legacy_default(self):
        p=Project();self.assertEqual(p.scene,default_scene())
        old=p.data();old['scene'].pop('architecture')
        q=Project.from_data(old);self.assertFalse(q.scene['architecture']['enabled'])
        q=set_scene(q,architecture=dict(enabled=True,width=4000,depth=3000,height=2500))
        self.assertEqual(Project.from_data(q.data()).data(),q.data());self.assertEqual(len(vertices(q)),8)
        self.assertEqual(len(q.parts),0)
    def test_invalid_room_is_atomic(self):
        p=Project();original=p.data()
        for data in [dict(width=-10),dict(enabled=1),dict(floor_color='#zzzzzz'),dict(height=True),dict(depth=float('inf'))]:
            with self.subTest(data=data),self.assertRaises(ValueError):set_scene(p,architecture=data)
            self.assertEqual(p.data(),original)
    def test_walls_follow_camera_cutaway(self):
        p=set_scene(Project(),architecture=dict(enabled=True))
        for yaw in (0,90,180,270):
            camera=Camera(yaw=yaw,pitch=24);faces=world_faces(p,camera)
            self.assertEqual(len(faces),2);self.assertTrue(any(f['id'].endswith('floor') for f in faces))
        p=set_scene(p,architecture=dict(enabled=True,cutaway=False,ceiling=True))
        self.assertEqual(len(world_faces(p,Camera())),6)
    def test_inverse_full_rotation_all_planes(self):
        for plane in ('XY','XZ','YZ'):
            p=Part(u=700,v=600,t=18,plane=plane,x=20,y=-180,z=470,rx=23,ry=-19,rz=127,layout=dict(x=230,y=100,z=50,yaw=91))
            q=(131,277,8)
            self.assertTrue(all(abs(a-b)<1e-8 for a,b in zip(inverse_world(p,world(p,*q)),q)))
    def test_magnet_matches_cabinet_edge(self):
        p=Project();p.parts=[Part(u=600,v=560,t=720,material=p.materials[0].id)]
        p=add_object(p,'washer',x=635,y=0)
        o,contact=snap_object(p,p.scene['objects'][0],80)
        self.assertEqual(o['x'],600);self.assertIsNotNone(contact)
    def test_far_object_has_no_false_magnet(self):
        p=Project();p.parts=[Part(u=600,v=560,t=18,material=p.materials[0].id)];p=add_object(p,'washer',x=635,y=4000)
        o,contact=snap_object(p,p.scene['objects'][0],80)
        self.assertIsNone(contact);self.assertEqual(o['x'],635)
    def test_delete_drawer_cleans_child_led(self):
        from furniture_configurator import configure,default_config
        from drag_hardware import attach
        from assembly import delete_with_connections
        p=Project();p,g=configure(p,default_config(3,p));front=next(p for p in p.parts if p.role=='drawer')
        bottom=next(q for q in p.parts if q.assembly.get('generated_by')==front.id and q.plane=='XY')
        p=attach(p,'led',bottom.id,dict(u=bottom.u/2,v=bottom.v/2,face='bottom'),dict(width=12,margin=30))
        delete_with_connections(p,front.id);p.validate()
        self.assertFalse(any(h.get('owner_id')==bottom.id for h in p.hardware))

    def test_camera_hides_end_on_axis(self):
        canvas=SimpleNamespace()
        for yaw,pitch,expected in [(0,0,{0,2}),(0,90,{0,1}),(-90,0,{1,2}),(0,-90,{0,1})]:
            result=_box_edges((0,0,0),(600,560,820),lambda x:x,Camera(yaw,pitch,.4),(900,600),canvas)
            self.assertEqual({r['axis'] for r in result},expected)
    def test_room_drag_shared_parts_one_transaction(self):
        p=add_object(Project(),'washer',x=900)
        class App:
            project=p;scene_drag=None;camera=Camera();layout_magnet=SimpleNamespace(get=lambda:False);snapshots=0
            def draw(self):pass
            def refresh(self):pass
            def snapshot(self):self.snapshots+=1
        app=App();app.status=SimpleNamespace(set=lambda s:None);app.layout_tolerance=SimpleNamespace(get=lambda:80)
        original=p.data();identity=p.scene['objects'][0]['id']
        self.assertTrue(begin(app,SimpleNamespace(x=0,y=0),'scene:'+identity))
        event=SimpleNamespace(x=75,y=20,state=0);move(app,event,force=True)
        self.assertIs(app.project.parts,p.parts);self.assertEqual(p.data(),original)
        end(app,event);self.assertEqual(app.snapshots,1);self.assertNotEqual(app.project.scene['objects'][0]['x'],900)
    def test_cancel_object_drag_restores_base(self):
        p=add_object(Project(),'washer')
        app=SimpleNamespace(project=p,camera=Camera(),status=SimpleNamespace(set=lambda s:None),layout_magnet=SimpleNamespace(get=lambda:False),layout_tolerance=SimpleNamespace(get=lambda:80),draw=lambda:None,refresh=lambda:None)
        begin(app,SimpleNamespace(x=0,y=0),'scene:'+p.scene['objects'][0]['id']);move(app,SimpleNamespace(x=50,y=15,state=0),True)
        self.assertTrue(cancel(app));self.assertIs(app.project,p)

if __name__=='__main__':unittest.main()
