import math
import unittest
from core import Part, world
from sketch import normalized_part

class SketchGeometryTests(unittest.TestCase):
    def test_normalization_preserves_world_geometry(self):
        for plane in ('XY','XZ','YZ'):
            part=Part(plane=plane,rx=12,ry=23,rz=34,x=20,y=30,z=40,
                      holes=[[50,50,5,10]])
            points=[(10,20),(110,20),(110,120),(10,120)]
            q=normalized_part(part,points)
            self.assertEqual((q.u,q.v),(100,100))
            self.assertEqual(q.holes,[[40,30,5,10]])
            for original,local in zip(points,q.contour):
                self.assertLess(math.dist(world(part,*original,0),world(q,*local,0)),1e-8)

    def test_reject_crossed_and_degenerate_contours(self):
        for points in ([(0,0),(100,100),(0,100),(100,0)],[(0,0),(10,0),(20,0)]):
            with self.assertRaises(ValueError):normalized_part(Part(),points)

    def test_negative_coordinates_and_volume(self):
        q=normalized_part(Part(t=30),[(-100,-50),(100,-50),(100,50),(-100,50)])
        self.assertEqual((q.x,q.y,q.u,q.v),(-100,-50,200,100))
        self.assertAlmostEqual(q.area(),.02)
