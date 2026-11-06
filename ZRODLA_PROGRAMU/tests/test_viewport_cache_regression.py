"""The user's 48-part cold preview/full transition must remain stable."""
import copy
import unittest
from unittest.mock import patch
from core import Project,Part,Camera,render_faces
from viewport_render import clear_caches,prepare_scene,Geometry,lit_color,lit_colors,fitting_preview,indexed
from configurator_accessories import apply_front_to_part


class CacheRegressionTests(unittest.TestCase):
    def setUp(self):clear_caches()

    def test_hundreds_of_48_part_edits_led_lod_and_angle_switches(self):
        project=Project()
        for i in range(48):
            project.parts.append(Part(u=620,v=740,t=18,x=(i%8)*650,y=(i//8)*780,
                material=project.materials[0].id,group='Szafka '+str(i//8)))
        front=project.parts[0];front.role='door'
        apply_front_to_part(project,front,'fluted',{'flute_count':12})
        project.hardware=[dict(kind='led_strip',owner_id=project.parts[1].id,
            u=300,v=210,face='bottom',axis='u',length=500,width=12,depth=3,color='#fff3bf')]
        camera=Camera();camera.fit(project,900,600)
        source_machining=copy.deepcopy(front.machining)
        for i in range(200):
            project.parts[-1].x=4600+(i%9)*3
            project.parts[-1].u=420+i%7
            project.parts[12].visible=i%3!=0
            project.scene['brightness']=(.35,1,1.25)[i%3]
            project.scene['led_enabled']=i%4!=0
            camera.yaw=-38+(i%90)*4;camera.pitch=20+i%6
            preview=render_faces(project,camera,900,600,interactive=True)
            if i%2==0:
                full=render_faces(project,camera,900,600)
                self.assertTrue(full)
            context=prepare_scene(project)
            for record in context.records(i%2==1):
                self.assertEqual(len(context.face_colors(record)),len(record.indices))
                self.assertEqual(len(record.normals),len(record.indices))
                self.assertEqual(len(record.centers),len(record.indices))
            self.assertTrue(preview)
        self.assertEqual(front.machining,source_machining)

    def test_released_geometry_cannot_leave_an_integer_identity_color_cache(self):
        project=Project();project.parts=[Part(material=project.materials[0].id)]
        context=prepare_scene(project)
        small=Geometry(((0,0,0),(1,0,0),(0,1,0)),((0,1,2),),owner='a',color='#ff0000')
        self.assertEqual(len(context.face_colors(small)),1)
        del small
        # Different face counts on fresh objects never inherit old colors.
        for count in range(1,150):
            record=Geometry(((0,0,0),(1,0,0),(0,1,0)),((0,1,2),)*count,
                            owner='a',color='#00ff00')
            self.assertEqual(len(context.face_colors(record)),count)
        self.assertFalse(hasattr(context,'colors'))

    def test_room_object_drag_reuses_stationary_fitting_geometry(self):
        from room_objects import add_object
        project=Project();part=Part(material=project.materials[0].id)
        project.parts=[part]
        project=add_object(project,'washer',x=600)
        clear_caches()
        with patch('fitting_visuals.fitting_faces',return_value=[]) as fits:
            for i in range(30):
                project.scene['objects'][0]['x']=600+i
                prepare_scene(project)
        self.assertEqual(fits.call_count,1)

    def test_vector_lighting_matches_scalar_and_preserves_saturated_material(self):
        normals=[(0,0,1),(0,1,0),(1,0,0)]
        centers=[(0,0,0),(100,0,0),(3000,3000,0)]
        lights=[dict(position=(0,0,100),radius=650,intensity=.65,color='#fff3bf')]
        for brightness in (.35,1,1.25):
            scalar=tuple(lit_color('#ff2200',n,c,lights,brightness) for n,c in zip(normals,centers))
            self.assertEqual(lit_colors('#ff2200',normals,centers,lights,brightness),scalar)
            self.assertTrue(all(color[-2:]=='00' for color in scalar))

    def test_hardware_preview_keeps_disconnected_hinges_separate_and_led_exact(self):
        from fitting_visuals import cylinder
        from core import world
        part=Part(plane='XZ',x=40,y=80,z=120,layout={'yaw':31,'x':200,'y':30,'z':0})
        polygons=cylinder(20,40,0,12,18)+cylinder(20,640,0,12,18)
        polygons=[[world(part,*point) for point in face] for face in polygons]
        vertices,indices=indexed(polygons)
        actual=Geometry(vertices,indices,owner=part.id,color='#808080',fitting=True,sign=-1)
        proxy=fitting_preview(actual,part)[0]
        self.assertEqual(len(proxy.indices),12)
        # There is no geometry across the middle 570 mm between two hinges.
        origin=world(part,0,0,0);axis=tuple(a-b for a,b in zip(world(part,0,1,0),origin))
        positions=[sum((p[i]-origin[i])*axis[i] for i in range(3)) for p in proxy.vertices]
        self.assertFalse(any(80<v<600 for v in positions))
        self.assertIs(fitting_preview(actual,part,emissive=True)[0],actual)

    def test_warm_full_led_patches_are_never_reused_by_stock_interactive_lod(self):
        project=Project();part=Part(u=2600,v=3200,t=18,z=350,material=project.materials[0].id)
        project.parts=[part]
        project.hardware=[dict(kind='led_strip',owner_id=part.id,u=300,v=210,
            face='bottom',axis='u',length=500,width=12,depth=3,color='#fff3bf')]
        camera=Camera();camera.fit(project,900,650)
        for order in ((False,True),(True,False,True)):
            clear_caches()
            for interactive in order:
                render_faces(project,camera,900,650,interactive=interactive)
                records=[r for r in prepare_scene(project).records(interactive) if r.owner==part.id and not r.fitting]
                if interactive:
                    self.assertEqual(sum(len(r.indices) for r in records),6)
                else:
                    self.assertGreater(sum(len(r.indices) for r in records),6)

    def test_decorated_front_mounting_bores_do_not_disable_motion_lod(self):
        from viewport_render import preview_front
        project=Project();part=Part(u=896,v=896,t=18,material=project.materials[0].id,role='door')
        project.parts=[part]
        apply_front_to_part(project,part,'fluted',{'flute_count':12})
        part.operations.append(dict(type='drill',u=840,v=400,diameter=4.5,
                                    depth=18,face='bottom',config_auto=True))
        original=copy.deepcopy(part.operations)
        camera=Camera();camera.fit(project,900,650)
        render_faces(project,camera,900,650)
        records=[r for r in prepare_scene(project).records(True) if r.owner==part.id and not r.fitting]
        self.assertEqual(sum(len(r.indices) for r in records),6)
        self.assertEqual(part.operations,original)
        # User through bores and larger openings retain their real silhouette.
        part.operations[-1]['config_auto']=False
        self.assertIsNone(preview_front(part,'#ff0000'))
        part.operations[-1].update(config_auto=True,diameter=35)
        self.assertIsNone(preview_front(part,'#ff0000'))


if __name__=='__main__':unittest.main()
