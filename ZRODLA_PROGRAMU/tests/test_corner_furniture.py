import copy
import math
import tempfile
import unittest
from pathlib import Path
from core import Project,Part,inside,polygon_area
from corner_furniture import default_corner_config,create_corner,read_corner_config,corner_status,corner_bounds
from furniture_layout import rotate_group,move_group,group_layout,nearest_snap,snap_group,group_bounds,contacts
from direct_edit import resize_furniture,resize_furniture_face
from workspace_worktops import install_shared_worktop,read_shared_worktop
from furniture_configurator import default_config,configure


class CornerFurnitureTests(unittest.TestCase):
    def create(self,**changes):
        p=Project();c=default_corner_config(p);c.update(changes);q,g=create_corner(p,c);return q,g,c

    def test_left_right_mirror_have_real_equal_area_parts_and_valid_bounds(self):
        for side in ('left','right'):
            q,g,c=self.create(side=side);bottom=next(part for part in q.parts if part.role=='bottom')
            self.assertEqual(polygon_area(bottom.contour),900*900-340*340)
            self.assertEqual(inside((50,850),bottom.contour),side=='left');self.assertEqual(inside((850,850),bottom.contour),side=='right')
            self.assertEqual(len(q.parts),7);self.assertEqual(corner_bounds(q,g),dict(lower=(0,0,0),upper=(900,900,720)))
            self.assertTrue(corner_status(q,g)['editable']);q.validate()

    def test_chamfer_and_rounded_have_distinct_true_CNC_contours(self):
        base,g,c=self.create();area=next(part.area() for part in base.parts if part.role=='bottom')*1e6
        q,g,c=self.create(shape='chamfer',radius=100);bottom=next(part for part in q.parts if part.role=='bottom')
        self.assertEqual(polygon_area(bottom.contour)-area,5000)
        q,g,c=self.create(shape='rounded',radius=100,segments=24);bottom=next(part for part in q.parts if part.role=='bottom')
        self.assertGreater(len(bottom.contour),20);self.assertAlmostEqual(polygon_area(bottom.contour)-area,10000*(1-math.pi/4),delta=6)
        from cad_geometry import local_shape
        self.assertAlmostEqual(local_shape(bottom).Volume(),polygon_area(bottom.contour)*18,places=3)

    def test_resize_and_minimum_face_rebuild_preserve_IDs_layout_and_manual_holes(self):
        q,g,c=self.create();q=rotate_group(move_group(q,g,1200,400,30),g,90);layout=group_layout(q,g)
        IDs={part.assembly['corner_source']['key']:part.id for part in q.parts}
        bottom=next(part for part in q.parts if part.role=='bottom');bottom.holes=[[100,100,4,5]]
        r=resize_furniture(q,g,1100,800,1000)
        self.assertEqual(read_corner_config(r,g)['width'],1100);self.assertEqual(group_layout(r,g),layout)
        self.assertEqual(IDs,{part.assembly['corner_source']['key']:part.id for part in r.parts});self.assertEqual(next(part.holes for part in r.parts if part.id==bottom.id),bottom.holes)
        s=resize_furniture_face(r,g,'x0',30);config=read_corner_config(s,g)
        self.assertEqual(config['origin'],[30,0,0]);self.assertEqual(config['width'],1070);self.assertEqual(corner_bounds(s,g)['upper'][0],1100)
        self.assertTrue(corner_status(s,g)['editable'])
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'narożnik.mebel';s.save(path);self.assertEqual(Project.load(path).data(),s.data())

    def test_manual_geometry_and_removed_part_features_protect_source(self):
        q,g,c=self.create();q.parts[1].u-=5;before=q.data()
        with self.assertRaisesRegex(ValueError,'ręcznie'):create_corner(q,c,g)
        self.assertEqual(q.data(),before)
        q,g,c=self.create();shelf=next(part for part in q.parts if part.role=='shelf');shelf.holes=[[100,100,4,5]];c['shelves']=0;before=q.data()
        with self.assertRaisesRegex(ValueError,'ręczną'):create_corner(q,c,g)
        self.assertEqual(q.data(),before)

    def test_corner_rails_and_shared_top_preserve_the_open_notch(self):
        q,g,c=self.create(top_mode='rails',shelves=0);self.assertFalse(any(part.assembly['corner_source']['key']=='top' for part in q.parts))
        r,top_group=install_shared_worktop(q,[g],overhang=0);top=next(part for part in r.parts if part.group==top_group)
        self.assertFalse(inside((850,850),top.contour));self.assertEqual(top.z,720)
        resized=resize_furniture(r,g,1000,800,1100);self.assertEqual(read_shared_worktop(resized,top_group)['status'],'ready')
        top=next(part for part in resized.parts if part.group==top_group);self.assertEqual((top.u,top.v,top.z),(1000,1100,800))

    def test_corner_empty_notch_does_not_cause_false_collision(self):
        q,g,c=self.create(shelves=0)
        # Place a small cabinet above the end of the X arm, inside the empty
        # quadrant of the bounding box. This is a real adjacent L arrangement.
        cfg=default_config(0,q);cfg.update(name='Mała',width=300,depth=300,height=720,leg_height=0,origin=[580,630,0]);cfg['sections'][1].update(front='open',shelves=0)
        q,b=configure(q,cfg);r,info=nearest_snap(q,b)
        self.assertIsNotNone(info);self.assertEqual(info['direction'],'x+')
        self.assertEqual(group_bounds(r,b)[0][0],560);self.assertGreaterEqual(group_bounds(r,b)[0][1],560)
        self.assertEqual(len(contacts(r,b)),1)

    def test_invalid_corner_parameters_are_atomic(self):
        p=Project();c=default_corner_config(p);before=p.data()
        for changes in ({'arm_depth':900},{'radius':500,'shape':'rounded'},{'shelves':1.5},{'side':'wrong'},{'top_mode':'wrong'}):
            invalid=copy.deepcopy(c);invalid.update(changes)
            with self.assertRaises(ValueError):create_corner(p,invalid)
            self.assertEqual(p.data(),before)

    def test_copy_entire_corner_remains_editable_and_manual_geometry_is_not_recertified(self):
        from productivity import insert_template,extract_group
        q,g,c=self.create(side='right',shape='rounded');q=rotate_group(move_group(q,g,400,200,20),g,90)
        template=extract_group(q,g);r=insert_template(q,template,'Kopia narożnika',(2000,300,0))
        self.assertTrue(corner_status(r,'Kopia narożnika')['editable']);self.assertEqual(group_layout(r,'Kopia narożnika')['x'],group_layout(q,g)['x']+2000)
        copied=read_corner_config(r,'Kopia narożnika');self.assertEqual(copied['name'],'Kopia narożnika')
        s=resize_furniture(r,'Kopia narożnika',1000,750,1000);self.assertTrue(corner_status(s,'Kopia narożnika')['editable'])
        self.assertEqual(read_corner_config(s,g)['width'],900)
        changed=copy.deepcopy(template);changed.parts[1].u-=3;blocked=insert_template(Project(),changed,'Zmieniona kopia',(300,0,0))
        self.assertFalse(corner_status(blocked,'Zmieniona kopia')['editable'])

    def test_shared_top_can_span_a_corner_and_a_straight_cabinet(self):
        q,g,c=self.create(shelves=0);cfg=default_config(0,q);cfg.update(name='Sąsiad',width=600,depth=560,height=720,leg_height=0,origin=[900,0,0]);cfg['sections'][1].update(front='open',shelves=0)
        q,b=configure(q,cfg);r,top_group=install_shared_worktop(q,[g,b],overhang=0);top=next(part for part in r.parts if part.group==top_group)
        self.assertEqual((top.u,top.v,top.z),(1500,900,720));self.assertFalse(inside((800,800),top.contour))
        self.assertEqual(read_corner_config(r,g)['top_mode'],'rails');self.assertEqual(read_corner_config(r,g)['height'],720)


if __name__=='__main__':unittest.main()
