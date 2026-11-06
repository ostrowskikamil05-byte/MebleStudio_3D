"""Viewport cache correctness, true openings and meaningful movement quality."""
import copy
import math
import unittest
from unittest.mock import patch

from core import Camera, Part, Project, faces, render_faces, world
from viewport_render import (clear_caches, prepare_scene, local_geometry,
    geometry_key, merge_coplanar, indexed, lit_color, furniture_lights, rgb)
from viewport_scene import paint_background, paint_faces, fit_scene
from test_viewport_scene import Canvas


class ViewportRenderTests(unittest.TestCase):
    def setUp(self):
        clear_caches()
        self.project=Project()
        self.part=Part(u=600,v=420,t=18,material=self.project.materials[0].id)
        self.project.parts=[self.part]
        self.camera=Camera();self.camera.fit(self.project,1000,700)

    def test_orbit_and_background_share_hardware_and_geometry_cache(self):
        clear_caches()
        with patch('fitting_visuals.fitting_faces',return_value=[]) as hardware:
            for i in range(8):
                self.camera.yaw+=7
                paint_background(Canvas(),self.camera,self.project,(1000,700))
                rendered=render_faces(self.project,self.camera,1000,700)
            self.assertEqual(hardware.call_count,1)
        self.assertEqual(len(rendered),6)
        self.assertLessEqual(local_geometry.cache_info().misses,2)

    def test_in_place_edit_and_undo_do_not_reuse_mutated_pose_or_color(self):
        old=copy.deepcopy(self.project)
        original=render_faces(self.project,self.camera,1000,700,explode=.2)
        self.part.x=1200;self.part.layout={'yaw':90,'x':300,'y':50,'z':0}
        moved=render_faces(self.project,self.camera,1000,700,explode=.2)
        self.assertNotEqual(original[0]['points'],moved[0]['points'])
        self.assertEqual(render_faces(old,self.camera,1000,700,explode=.2),original)
        old.materials[0].color='#00cc44'
        self.assertNotEqual(render_faces(old,self.camera,1000,700)[0]['color'],original[0]['color'])
        self.part.visible=False
        self.assertEqual(render_faces(self.project,self.camera,1000,700),[])

    def test_camera_basis_updates_for_direct_yaw_and_pitch_assignment(self):
        for yaw,pitch in ((0,0),(90,45),(-52,-19),(130,90)):
            self.camera.yaw,self.camera.pitch=yaw,pitch
            point=(122,740,-83);x,y,z=(point[i]-self.camera.center[i] for i in range(3))
            a,b=math.radians(yaw),math.radians(pitch)
            d=x*math.sin(a)+y*math.cos(a)
            expected=(x*math.cos(a)-y*math.sin(a),z*math.cos(b)+d*math.sin(b),-d*math.cos(b)+z*math.sin(b))
            for got,want in zip(self.camera.view(point),expected):self.assertAlmostEqual(got,want)

    def test_merge_preserves_holes_instead_of_filling_their_outer_boundary(self):
        # Eight triangles surround an actual square opening in the centre.
        outer=[(0,0,0),(10,0,0),(10,10,0),(0,10,0)]
        inner=[(3,3,0),(7,3,0),(7,7,0),(3,7,0)]
        polygons=[]
        for i in range(4):
            j=(i+1)%4
            polygons.extend(([outer[i],outer[j],inner[j]],[outer[i],inner[j],inner[i]]))
        vertices,indices=indexed(polygons)
        self.assertEqual(len(merge_coplanar(vertices,indices)[1]),8)
        vertices,indices=indexed([[outer[0],outer[1],outer[2]],[outer[0],outer[2],outer[3]]])
        self.assertEqual(len(merge_coplanar(vertices,indices)[1]),1)

    def test_cad_display_keeps_real_opening_and_exact_production_faces(self):
        from cad_geometry import mesh
        self.part.machining=[dict(type='cutout',shape='circle',face='top',
            u=300,v=210,width=80,length=80,depth=18,angle=0)]
        self.part.plane='YZ';self.part.rx=17;self.part.layout={'yaw':31,'x':100,'y':-200,'z':0}
        before=copy.deepcopy(self.project.data())
        exact_before=mesh(self.part)
        production=faces(self.part)
        self.assertEqual(len(production),len(exact_before))
        for face,local in zip(production,exact_before):
            for point,uvt in zip(face,local):
                self.assertLess(math.dist(point,world(self.part,*uvt)),1e-6)
        full=render_faces(self.project,self.camera,1000,700)
        with patch('cad_geometry.cut_drills',side_effect=AssertionError('CAD entered during orbit')):
            for i in range(4):
                self.camera.yaw+=7
                preview=render_faces(self.project,self.camera,1000,700,interactive=True)
        self.assertTrue(full and preview)
        self.assertEqual(mesh(self.part),exact_before)
        self.assertEqual(self.project.data(),before)
        # Look squarely through the cutout. No merged display polygon may
        # cover its centre, which would conceal a real manufacturing opening.
        self.part.plane='XY';self.part.rx=0;self.part.layout={}
        self.camera=Camera(yaw=0,pitch=90,scale=1,center=(300,210,0))
        displayed=render_faces(self.project,self.camera,1000,700)
        def covers(points,x=500,y=350):
            inside=False
            for a,b in zip(points,points[1:]+points[:1]):
                if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
            return inside
        self.assertFalse(any(covers(f['points']) for f in displayed))

    def test_fluted_preview_uses_stock_without_changing_machining_or_kernel_on_drag(self):
        from configurator_accessories import apply_front_to_part
        self.part.role='door'
        apply_front_to_part(self.project,self.part,'fluted',{'flute_count':12})
        self.camera.pitch=-30
        full=render_faces(self.project,self.camera,1000,700)
        with patch('viewport_render.display_solid',side_effect=AssertionError('new CAD during drag')):
            preview=render_faces(self.project,self.camera,1000,700,interactive=True)
        colors={f['color'] for f in preview}
        self.assertGreater(len(preview),0)
        self.assertLessEqual(len(preview),6)
        self.assertGreater(len(colors),1)
        self.assertLess(len(preview),len(full)/3)
        self.assertEqual(len(self.part.machining),12)

    def test_led_reaches_nearby_surface_and_switch_controls_spill_and_diffuser(self):
        self.part.z=500
        self.project.hardware=[dict(kind='led_strip',owner_id=self.part.id,u=300,v=210,
            axis='u',face='bottom',length=500,width=12,depth=3,color='#fff3bf')]
        context=prepare_scene(self.project)
        self.assertTrue(context.lights)
        normal=(0,0,1);near=(300,210,350);far=(3000,3000,350)
        on=lit_color('#808080',normal,near,context.lights,brightness=.35)
        off=lit_color('#808080',normal,near,[],brightness=.35)
        self.assertGreater(sum(rgb(on)),sum(rgb(off)))
        self.assertEqual(lit_color('#808080',normal,far,context.lights,.35),off)
        self.project.scene['led_enabled']=False
        self.assertFalse(prepare_scene(self.project).lights)
        self.assertFalse(furniture_lights(self.project,{self.part.id:self.part}))
        disabled=render_faces(self.project,self.camera,1000,700)
        self.assertFalse(any(f['color']=='#fff3bf' for f in disabled if f.get('fitting')))
        canvas=Canvas();paint_background(canvas,self.camera,self.project,(1000,700))
        self.assertFalse(any(item[2].get('tags')=='viewport.light_pool' for item in canvas.items))

    def test_brightness_changes_background_and_model_but_preserves_hits(self):
        neutral=render_faces(self.project,self.camera,1000,700)
        self.project.scene['brightness']=.35
        dark=render_faces(self.project,self.camera,1000,700)
        self.assertEqual([f['points'] for f in dark],[f['points'] for f in neutral])
        self.assertLess(sum(rgb(dark[0]['color'])),sum(rgb(neutral[0]['color'])))
        canvas=Canvas();paint_background(canvas,self.camera,self.project,(1000,700))
        self.assertEqual(canvas.items[0][2]['fill'],'#242c36')
        self.project.scene['brightness']=1.25
        bright=render_faces(self.project,self.camera,1000,700)
        self.assertGreater(sum(rgb(bright[0]['color'])),sum(rgb(neutral[0]['color'])))

    def test_room_objects_are_fit_rendered_and_selected_independently_of_panels(self):
        from room_objects import add_object
        project=add_object(self.project,'fridge',x=1200,y=600,yaw=45)
        obj=project.scene['objects'][0];selected='scene:'+obj['id']
        self.camera.fit(project,1000,700)
        rendered=render_faces(project,self.camera,1000,700,selected)
        room=[f for f in rendered if f.get('scene_object')]
        self.assertTrue(room and all(f['id']==selected and f['selected'] for f in room))
        self.assertTrue(all(f.get('fitting') for f in room))
        self.assertTrue(all(40<=x<=960 and 40<=y<=660 for f in room for x,y in f['points']))
        self.assertEqual(len(project.parts),1)

    def test_large_lit_wall_has_local_light_patches_while_cnc_stock_stays_one_panel(self):
        wall=Part(u=2600,v=1800,t=18,plane='XZ',x=-700,y=320,z=0,
                  material=self.project.materials[0].id)
        self.part.z=900
        self.project.parts.append(wall)
        self.project.hardware=[dict(kind='led_strip',owner_id=self.part.id,u=300,v=210,
            axis='u',face='bottom',length=500,width=12,depth=3,color='#fff3bf')]
        rendered=render_faces(self.project,self.camera,1000,700)
        patches=[f for f in rendered if f['id']==wall.id and not f.get('fitting')]
        self.assertGreater(len(patches),6)
        self.assertGreater(len({f['color'] for f in patches}),3)
        self.assertEqual((wall.u,wall.v,wall.t),(2600,1800,18))
        self.assertEqual(len(faces(wall)),6)

    def test_large_tk_model_is_one_local_raster_and_vectors_still_pick(self):
        class Sink(Canvas):
            tk=object()
            def create_image(self,*args,**kwargs):return self.create('image',*args,**kwargs)
        sink=Sink();photo=[]
        one=dict(points=[(100,100),(200,100),(200,200)],depth=0,color='#ff0000',
                 id=self.part.id,mesh=True,normal=(0,0,1))
        model=[copy.deepcopy(one) for _ in range(801)];before=copy.deepcopy(model)
        with patch('PIL.ImageTk.PhotoImage',side_effect=lambda image,**kw: photo.append(image.copy()) or 'local-photo'):
            result=paint_faces(sink,model,self.project,size=(1000,700))
        self.assertTrue(result['raster'])
        self.assertEqual(result['items'],1)
        self.assertEqual(len(sink.items),1)
        self.assertEqual(sink.items[0][0],'image')
        self.assertEqual(photo[0].getpixel((150,130)),(255,0,0,255))
        self.assertEqual(photo[0].getpixel((300,300)),(0,0,0,0))
        self.assertEqual(model,before)


if __name__=='__main__':unittest.main()
