"""Visibility and picking must agree at pixels, including crossings and holes."""
import copy
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np
from depth_raster import DepthRaster,pick_visible,screen_visibility,triangulate
from core import Camera,Part,Project,render_faces
from viewport_scene import paint_faces


class DepthTests(unittest.TestCase):
    def test_crossing_faces_interpolate_depth_instead_of_sorting_mean(self):
        points=[(1,1),(29,1),(29,29),(1,29)]
        faces=[(points,[1,29,29,1],'#ff0000','slope'),(points,[15]*4,'#0044ff','flat')]
        result=[]
        for order in (faces,faces[::-1]):
            raster=DepthRaster((32,32),['flat','slope'])
            for args in order:raster.polygon(*args)
            canvas=SimpleNamespace();raster.attach(canvas)
            self.assertEqual(pick_visible(canvas,5,15),'flat')
            self.assertEqual(pick_visible(canvas,24,15),'slope')
            self.assertEqual(tuple(raster.rgba[15,24]),(255,0,0,255))
            self.assertFalse(screen_visibility(canvas,24,15,depth=15,owner='flat'))
            self.assertTrue(screen_visibility(canvas,24,15,depth=25,owner='slope'))
            result.append((raster.depth.copy(),raster.owner.copy(),raster.rgba.copy()))
        for left,right in zip(result[0],result[1]):np.testing.assert_array_equal(left,right)

    def test_concave_face_keeps_notch_open(self):
        points=[(1,1),(29,1),(29,29),(20,29),(20,8),(10,8),(10,29),(1,29)]
        raster=DepthRaster((32,32),['panel'])
        raster.polygon(points,[5]*8,'#ff0000','panel')
        self.assertEqual(raster.owner[18,15],-1)
        self.assertEqual(raster.rgba[18,15,3],0)
        self.assertGreaterEqual(raster.owner[18,5],0)
        self.assertGreaterEqual(raster.owner[5,15],0)

    def test_coplanar_identity_is_stable_and_lines_do_not_show_through_front(self):
        points=[(1,1),(29,1),(29,29),(1,29)]
        owners=[]
        for order in (['a','b'],['b','a']):
            raster=DepthRaster((32,32),order)
            for owner in order:raster.polygon(points,[5000,5001,5001,5000],'#0044ff',owner)
            raster.line([(4,15),(26,15)],[4000,4000],'#ff0000',owner='a',width=2)
            self.assertEqual(tuple(raster.rgba[15,15]),(0,68,255,255))
            owners.append(raster.owner)
        np.testing.assert_array_equal(*owners)

    def test_front_with_opening_occludes_box_interior_and_picks_through_real_hole(self):
        # The aperture is represented by four real border panels, not a mask.
        raster=DepthRaster((32,32),['front','shelf'])
        raster.polygon([(1,1),(29,1),(29,29),(1,29)],[5]*4,'#00ff00','shelf')
        for x0,y0,x1,y1 in ((1,1,29,8),(1,22,29,29),(1,8,8,22),(22,8,29,22)):
            raster.polygon([(x0,y0),(x1,y0),(x1,y1),(x0,y1)],[12]*4,'#cc0000','front')
        canvas=SimpleNamespace();raster.attach(canvas)
        self.assertEqual(pick_visible(canvas,15,15),'shelf')
        self.assertEqual(pick_visible(canvas,5,15),'front')
        self.assertEqual(tuple(raster.rgba[15,5]),(204,0,0,255))
        self.assertFalse(screen_visibility(canvas,5,15,5,owner='shelf'))

    def test_empty_frame_and_viewport_edges_are_valid(self):
        raster=DepthRaster((16,12),[]);canvas=SimpleNamespace();raster.attach(canvas)
        self.assertIsNone(pick_visible(canvas,0,0))
        self.assertIsNone(pick_visible(canvas,-1,0))
        self.assertFalse(screen_visibility(canvas,20,20,0))
        self.assertTrue(screen_visibility(canvas,5,5,0))
        self.assertTrue(np.all(np.isneginf(raster.depth)))

    def test_motion_outline_follows_visible_ids_without_changing_depth_or_picking(self):
        raster=DepthRaster((32,24),['front','rear'])
        raster.polygon([(2,2),(30,2),(30,22),(2,22)],[5]*4,'#00ff00','rear')
        raster.polygon([(4,4),(28,4),(28,20),(4,20)],[15]*4,'#ff0000','front')
        depth=raster.depth.copy();owners=raster.owner.copy()
        raster.outline_visible({'front'})
        np.testing.assert_array_equal(raster.depth,depth)
        np.testing.assert_array_equal(raster.owner,owners)
        self.assertEqual(tuple(raster.rgba[10,4]),(66,137,174,255))
        self.assertEqual(tuple(raster.rgba[10,12]),(255,0,0,255))
        self.assertEqual(tuple(raster.rgba[0,0]),(0,0,0,0))
        canvas=SimpleNamespace();raster.attach(canvas)
        self.assertEqual(pick_visible(canvas,4,10),'front')
        self.assertEqual(pick_visible(canvas,2,10),'rear')

    def test_preview_upscaled_pixels_and_picking_remain_exactly_aligned(self):
        raster=DepthRaster((83,57),['a','b'],scale=.6)
        raster.polygon([(0,0),(41,0),(41,57),(0,57)],[3]*4,'#ff0000','a')
        raster.polygon([(41,0),(83,0),(83,57),(41,57)],[3]*4,'#0000ff','b')
        canvas=SimpleNamespace();raster.attach(canvas)
        image=np.asarray(raster.image())
        red=canvas._id_buffer==canvas._depth_ids.index('a')
        blue=canvas._id_buffer==canvas._depth_ids.index('b')
        np.testing.assert_array_equal(image[:,:,0]>0,red)
        np.testing.assert_array_equal(image[:,:,2]>0,blue)
        self.assertEqual(canvas._depth_buffer.shape,(57,83))

    def test_real_part_projection_supplies_vertex_depths_for_small_scenes(self):
        project=Project();part=Part(u=600,v=450,t=18,material=project.materials[0].id)
        project.parts=[part];camera=Camera();camera.fit(project,300,220)
        faces=render_faces(project,camera,300,220)
        self.assertTrue(all(len(f['depths'])==len(f['points']) for f in faces))
        before=copy.deepcopy(faces)
        class Canvas:
            tk=object()
            def create_image(self,*args,**kw):pass
        canvas=Canvas()
        with patch('PIL.ImageTk.PhotoImage',return_value='photo'):
            result=paint_faces(canvas,faces,project,camera=camera,size=(300,220))
        self.assertTrue(result['depth'])
        self.assertEqual(canvas._depth_buffer.shape,(220,300))
        self.assertEqual(pick_visible(canvas,150,110),part.id)
        self.assertEqual(faces,before)


if __name__=='__main__':unittest.main()
