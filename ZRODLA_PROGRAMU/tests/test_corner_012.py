import copy
import math
import tempfile
import unittest
from pathlib import Path
from core import Project,local_world
from corner_furniture import default_corner_config,create_corner,convert_to_corner,read_corner_config,corner_status,regenerate_corner_hinges
from furniture_configurator import default_config,configure,read_config
from furniture_layout import move_group,rotate_group,group_layout
from productivity import extract_group,insert_template
from mechanisms import posed_project,set_motion
from dimension_model import fit_part,resize_dimensions
from workspace_worktops import install_shared_worktop,read_shared_worktop
from direct_edit import resize_furniture_face


class Corner012Tests(unittest.TestCase):
    def corner(self,**changes):
        project=Project();c=default_corner_config(project);c.update(fronts='orthogonal',**changes);return create_corner(project,c)

    def test_two_independent_fronts_drill_actual_support_and_open_about_fixed_axis(self):
        for side in ('left','right'):
            p,g=self.corner(side=side,arm_depth_y=480,leg_height=80)
            fronts=[q for q in p.parts if q.role=='door'];self.assertEqual(len(fronts),2)
            for front in fronts:
                bay=front.assembly['config']['corner_bay'];support=next(q for q in p.parts if q.id==bay['support_id'])
                cupops=[op for op in front.operations if op.get('purpose')=='puszka zawiasu'];self.assertEqual(len(cupops),2)
                self.assertTrue(all(op['face']==bay['cup_face'] for op in cupops))
                plateops=[op for op in support.operations if op.get('corner_hinge_owner')==front.id];self.assertEqual(len(plateops),4)
                self.assertTrue(all(op['face']==bay['support_face'] for op in plateops))
                pivot=local_world(front,0,0,front.t if bay['cup_face']=='top' else 0)
                posed=posed_project(p,{front.id:1});opened=next(q for q in posed.parts if q.id==front.id)
                self.assertLess(math.dist(pivot,local_world(opened,0,0,front.t if bay['cup_face']=='top' else 0)),1e-6)
                self.assertNotEqual(front.rz,opened.rz)
                self.assertEqual(p.data(),copy.deepcopy(p).data())
            # Closed front rectangles have no overlapping positive solid.
            from cad_geometry import kernel,local_shape
            from OCP.gp import gp_Trsf
            def part_shape(part):
                origin=local_world(part,0,0,0);basis=[tuple(b-a for a,b in zip(origin,local_world(part,*point))) for point in ((1,0,0),(0,1,0),(0,0,1))]
                transform=gp_Trsf();transform.SetValues(*[value for i in range(3) for value in [basis[j][i] for j in range(3)]+[origin[i]]])
                return local_shape(part).transformShape(kernel().Matrix(transform))
            self.assertLess(part_shape(fronts[0]).intersect(part_shape(fronts[1])).Volume(),.001)

    def test_conversion_retains_shared_ids_layout_dimensions_materials_and_undo_input(self):
        p=Project();c=default_config(0,p);c.update(name='Szafa',width=1200,height=2100,leg_height=0);c['sections'][1].update(doors=2,shelves=3)
        p,g=configure(p,c);p=rotate_group(move_group(p,g,1600,400,30),g,90);before=p.data();old={q.configurator_source['key']:q.id for q in p.parts}
        result,name=convert_to_corner(p,g)
        self.assertEqual(p.data(),before);self.assertEqual(name,g);self.assertEqual(group_layout(result,g),group_layout(p,g));self.assertEqual(read_corner_config(result,g)['height'],2100)
        new={q.assembly['corner_source']['key']:q.id for q in result.parts}
        for source,target in [('carcass.left','back_y'),('carcass.right','end_x'),('carcass.bottom','bottom'),('carcass.top','top'),('main.bay.0.door.0','front.x'),('main.bay.0.door.1','front.y')]:self.assertEqual(old[source],new[target])
        cfg=read_corner_config(result,g);cfg.update(side='right',width=1400,arm_depth_y=500);changed,_=create_corner(result,cfg,g)
        self.assertEqual(new,{q.assembly['corner_source']['key']:q.id for q in changed.parts});self.assertTrue(corner_status(changed,g)['editable'])
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'corner.mebel';changed.save(path);loaded=Project.load(path);self.assertEqual(loaded.data(),changed.data())

    def test_conversion_refuses_manual_geometry_operations_hardware_and_drawers_atomically(self):
        for kind in ('geometry','hole','hardware','drawer'):
            p=Project();c=default_config(0,p)
            if kind=='drawer':c['sections'][1].update(front='drawers',drawers=2,shelves=0)
            p,g=configure(p,c)
            if kind=='geometry':p.parts[0].u-=5
            if kind=='hole':p.parts[0].holes=[[100,100,3,4]]
            if kind=='hardware':p.hardware.append(dict(kind='other',owner_id=p.parts[0].id,name='Własne',qty=1,price=0))
            before=p.data()
            with self.assertRaises(ValueError):convert_to_corner(p,g)
            self.assertEqual(p.data(),before)

    def test_diagonal_is_real_geometry_with_explicit_no_auto_hinge(self):
        p=Project();c=default_corner_config(p);c.update(shape='chamfer',fronts='diagonal',radius=170,diagonal_angle=35,shelves=3)
        q,g=create_corner(p,c);front=next(q for q in q.parts if q.role=='door')
        self.assertNotEqual(front.rz,0);self.assertEqual(front.motion['mode'],'none');self.assertFalse(any(h['kind']=='hinge' for h in q.hardware))
        from dimension_model import regenerate_hinges
        with self.assertRaisesRegex(ValueError,'skośny'):regenerate_hinges(q,front.id)
        with self.assertRaises(ValueError):set_motion(q,front.id,'hinge_left')
        self.assertEqual(posed_project(q,{front.id:1}).data(),q.data())

    def test_copies_remap_bays_construction_offsets_and_style_and_remain_editable(self):
        for placed in (False,True):
            p,g=self.corner(front_style='frame')
            if placed:p=rotate_group(p,g,90)
            q=insert_template(p,extract_group(p,g),'Kopia',(1400,50,0));c=read_corner_config(q,'Kopia');c['width']=1100;q,_=create_corner(q,c,'Kopia')
            originals={x.id for x in p.parts};copied=[x for x in q.parts if x.group=='Kopia']
            for front in (x for x in copied if x.role=='door'):
                self.assertNotIn(front.assembly['config']['corner_bay']['support_id'],originals)
                q=regenerate_corner_hinges(q,front.id,'hinge52',3)
            self.assertTrue(corner_status(q,'Kopia')['editable']);q.validate()

    def test_fit_restores_only_selected_geometry_and_resize_includes_legs_once(self):
        p,g=self.corner(leg_height=100);front=next(q for q in p.parts if q.role=='door');front.u-=10
        self.assertFalse(corner_status(p,g)['editable']);q=fit_part(p,front.id);self.assertTrue(corner_status(q,g)['editable'])
        q=resize_furniture_face(q,g,'z1',50);self.assertEqual(read_corner_config(q,g)['height'],770);self.assertEqual(read_corner_config(q,g)['origin'][2],0)
        q,topg=install_shared_worktop(q,[g],overhang=0);top=next(q for q in q.parts if q.group==topg)
        self.assertEqual(top.z,770);self.assertEqual(read_shared_worktop(q,topg)['status'],'ready')

    def test_new_eighteen_presets_are_real_editable_modules_preserving_original_indices(self):
        from furniture_library import TEMPLATES,create_furniture
        self.assertEqual(len(TEMPLATES),52)
        for index in range(34,52):
            p,g=create_furniture(Project(),index);cfg=read_config(p,g);self.assertIsNotNone(cfg);self.assertGreater(len(p.parts),3)
            if index in (35,38,49):self.assertEqual(cfg['top_mode'],'rails');self.assertFalse(cfg['back'])
            if index in (39,46):self.assertEqual(cfg['sections'][1]['columns'],3)


if __name__=='__main__':unittest.main()
