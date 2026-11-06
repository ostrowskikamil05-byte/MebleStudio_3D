import copy
import tempfile
import unittest
from pathlib import Path
from core import Project,Part,inside,polygon_area,world
from furniture_configurator import default_config,configure,read_config,configuration_status
from furniture_layout import move_group,rotate_group,nearest_snap,group_bounds,group_layout,snap_group
from workspace_worktops import install_shared_worktop,fit_shared_worktop,refresh_shared_worktops,read_shared_worktop,polygons_overlap


class SharedWorktopTests(unittest.TestCase):
    def add(self,project,name,origin=(0,0,0),width=600,depth=560,**changes):
        config=default_config(0,project);config.update(name=name,origin=list(origin),width=width,depth=depth,top_mode='rails',**changes)
        config['sections'][1].update(front='open',shelves=0)
        return configure(project,config)

    def pair(self,**changes):
        p,a=self.add(Project(),'A',**changes);p,b=self.add(p,'B',(600,0,0),**changes)
        return p,a,b

    def test_rails_are_real_parts_and_keep_bay_references_and_carcase_height(self):
        p=Project();c=default_config(0,p);c['top_mode']='rails';q,g=configure(p,c)
        top=next(part for part in q.parts if part.configurator_source.get('key')=='carcass.top')
        rear=next(part for part in q.parts if part.configurator_source.get('key')=='carcass.rail_back')
        self.assertEqual((top.v,rear.v),(80,80));self.assertEqual((top.y,rear.y),(0,480))
        self.assertEqual(top.z+top.t,820);self.assertEqual(top.u,564)
        self.assertTrue(all(part.assembly['config']['bay']['top']==top.id for part in q.parts if part.role=='door'))
        self.assertTrue(configuration_status(q,g)['editable'])
        c['width']=750;r,_=configure(q,c,g)
        self.assertEqual(next(part.id for part in r.parts if part.configurator_source.get('key')=='carcass.top'),top.id)
        self.assertEqual(next(part.u for part in r.parts if part.configurator_source.get('key')=='carcass.rail_back'),714)

    def test_straight_shared_top_is_one_cuttable_panel_with_overhang(self):
        p,a,b=self.pair();before=p.data();q,g=install_shared_worktop(p,[a,b],overhang=20,thickness=38)
        self.assertEqual(p.data(),before);top=next(part for part in q.parts if part.group==g)
        self.assertEqual((top.x,top.y,top.z,top.u,top.v,top.t),(-20,-20,820,1240,600,38))
        self.assertEqual(len(top.contour),4);self.assertEqual(top.role,'worktop');self.assertEqual(read_shared_worktop(q,g)['status'],'ready')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'blat.mebel';q.save(path);loaded=Project.load(path)
            self.assertEqual(q.data(),loaded.data());self.assertEqual(fit_shared_worktop(loaded,g).data(),loaded.data())

    def test_L_and_U_tops_preserve_empty_opening_in_their_real_contours(self):
        p,a,b=self.pair();p,c=self.add(p,'C',(0,560,0));q,g=install_shared_worktop(p,[a,b,c],overhang=0)
        top=next(part for part in q.parts if part.group==g)
        self.assertEqual(len(top.contour),6);self.assertEqual(polygon_area(top.contour),3*600*560)
        self.assertFalse(inside((900,900),top.contour));self.assertTrue(inside((300,900),top.contour))
        p,a=self.add(Project(),'U dolna',width=1800);p,b=self.add(p,'U lewa',(0,560,0),width=560,depth=1000);p,c=self.add(p,'U prawa',(1240,560,0),width=560,depth=1000)
        q,g=install_shared_worktop(p,[a,b,c],overhang=0);top=next(part for part in q.parts if part.group==g)
        self.assertEqual(len(top.contour),8);self.assertFalse(inside((900,1000),top.contour));self.assertTrue(inside((1500,1000),top.contour))

    def test_support_resize_and_rename_refresh_same_worktop_ID(self):
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b],overhang=0);top=next(part for part in q.parts if part.group==g)
        config=read_config(q,b);config['width']=700;config['name']='B po zmianie';r,new_b=configure(q,config,b)
        updated=next(part for part in r.parts if part.id==top.id)
        self.assertEqual(updated.u,1300);self.assertEqual(read_shared_worktop(r,g)['status'],'ready')
        self.assertEqual(read_config(r,new_b)['width'],700);self.assertTrue(configuration_status(r,new_b)['editable'])

    def test_support_separation_or_deletion_preserves_last_top_and_reports_status(self):
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b],overhang=0);top=next(part for part in q.parts if part.group==g)
        r=move_group(q,b,300,0,0);kept=next(part for part in r.parts if part.id==top.id)
        self.assertEqual((kept.contour,kept.x,kept.y),(top.contour,top.x,top.y));self.assertEqual(read_shared_worktop(r,g)['status'],'needs_fit')
        with self.assertRaisesRegex(ValueError,'sąsiadujących'):fit_shared_worktop(r,g)
        restored=move_group(r,b,-300,0,0);self.assertEqual(read_shared_worktop(restored,g)['status'],'ready')
        deleted=copy.deepcopy(q);removed={part.id for part in deleted.parts if part.group==b};deleted.parts=[part for part in deleted.parts if part.id not in removed];deleted.hardware=[h for h in deleted.hardware if h.get('owner_id') not in removed]
        stale=refresh_shared_worktops(deleted);self.assertEqual(read_shared_worktop(stale,g)['status'],'needs_fit')
        with self.assertRaisesRegex(ValueError,'Usunięto'):fit_shared_worktop(stale,g)

    def test_manual_top_geometry_and_movement_are_never_overwritten(self):
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b]);top=next(part for part in q.parts if part.group==g);top.z+=10
        r=refresh_shared_worktops(q);self.assertEqual(next(part.z for part in r.parts if part.id==top.id),top.z)
        self.assertEqual(read_shared_worktop(r,g)['status'],'needs_fit')
        with self.assertRaisesRegex(ValueError,'ręcznie'):fit_shared_worktop(r,g)
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b]);r=move_group(q,g,100,50,0)
        self.assertEqual(group_layout(r,g)['x'],100);self.assertEqual(read_shared_worktop(r,g)['status'],'needs_fit')

    def test_individual_top_replacement_preserves_body_height_and_refuses_manual_cuts(self):
        p,a,b=self.pair(worktop=True,worktop_thickness=30,height=850);q,g=install_shared_worktop(p,[a,b])
        self.assertEqual(next(part.z for part in q.parts if part.group==g),820)
        self.assertTrue(all(not read_config(q,group)['worktop'] for group in (a,b)))
        self.assertTrue(all(read_config(q,group)['height']==820 for group in (a,b)))
        p,a,b=self.pair(worktop=True,worktop_thickness=30,height=850);top=next(part for part in p.parts if part.role=='worktop');top.holes=[[100,100,4,5]];before=p.data()
        with self.assertRaisesRegex(ValueError,'ręczną'):install_shared_worktop(p,[a,b])
        self.assertEqual(p.data(),before)

    def test_invalid_supports_and_duplicate_link_are_atomic(self):
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b]);before=q.data()
        with self.assertRaisesRegex(ValueError,'już wspólny'):install_shared_worktop(q,[a,b])
        self.assertEqual(q.data(),before)
        for invalid in (move_group(p,b,0,0,30),move_group(p,b,100,0,0),rotate_group(p,b,10)):
            before=invalid.data()
            with self.assertRaises(ValueError):install_shared_worktop(invalid,[a,b])
            self.assertEqual(invalid.data(),before)

    def test_stronger_magnet_uses_80mm_default_and_keeps_collision_guard(self):
        p,a,b=self.pair();p=move_group(p,b,65,8,3);q,info=nearest_snap(p,b)
        self.assertIsNotNone(info);self.assertEqual(group_bounds(q,b)[0],(600,0,0))
        obstacle=Part(name='Przeszkoda',group='Przeszkoda',x=610,y=0,z=100,u=80,v=560,t=720,material=p.materials[0].id);p.parts.append(obstacle)
        with self.assertRaisesRegex(ValueError,'nachodzi'):snap_group(p,b,a,'x+')
        q,info=nearest_snap(p,b)
        if info:self.assertGreaterEqual(group_bounds(q,b)[0][0],690)
        else:self.assertEqual(group_bounds(q,b),group_bounds(p,b))

    def test_positive_polygon_area_excludes_only_touching_edges_and_vertices(self):
        a=[(0,0),(10,0),(10,10),(0,10)]
        self.assertTrue(polygons_overlap(a,a));self.assertTrue(polygons_overlap(a,[(5,5),(15,5),(15,15),(5,15)]))
        self.assertFalse(polygons_overlap(a,[(10,0),(20,0),(20,10),(10,10)]));self.assertFalse(polygons_overlap(a,[(10,10),(20,10),(20,20),(10,20)]))

    def test_install_converts_full_top_to_rails_and_protects_manual_features(self):
        p=Project();c=default_config(0,p);c.update(name='Pełna góra');c['sections'][1].update(front='open',shelves=0)
        q,a=configure(p,c);top=next(part for part in q.parts if part.configurator_source.get('key')=='carcass.top')
        r,g=install_shared_worktop(q,[a]);self.assertEqual(read_config(r,a)['top_mode'],'rails')
        self.assertEqual(next(part.name for part in r.parts if part.id==top.id),'Listwa górna przednia')
        self.assertEqual(next(part.v for part in r.parts if part.id==top.id),80)
        top.holes=[[100,200,4,5]];before=q.data()
        with self.assertRaises(ValueError):install_shared_worktop(q,[a])
        self.assertEqual(q.data(),before)

    def test_a_free_worktop_magnet_lands_on_the_cabinet_top(self):
        p,a=self.add(Project(),'Korpus');p.parts.append(Part(name='Swobodny blat',group='Blat',u=620,v=580,t=38,x=-5,y=-5,z=875,material=p.materials[0].id,role='worktop'))
        q,info=nearest_snap(p,'Blat');self.assertIsNotNone(info);self.assertEqual(info['direction'],'z+')
        self.assertEqual(group_bounds(q,'Blat')[0][2],group_bounds(q,a)[1][2])

    def test_copy_whole_connected_set_remaps_links_but_copy_only_top_detaches(self):
        from productivity import insert_template,extract_group
        p,a,b=self.pair();q,g=install_shared_worktop(p,[a,b]);original_top=next(part for part in q.parts if part.group==g)
        r=insert_template(q,q,'Kopia',(2000,0,0));copy_top=next(part for part in r.parts if part.group=='Kopia / '+g)
        copied_ids={part.id for part in r.parts if part.group.startswith('Kopia / ')};old_ids={part.id for part in q.parts}
        self.assertTrue(set(copy_top.assembly['shared_worktop']['anchors']).issubset(copied_ids));self.assertTrue(set(copy_top.assembly['shared_worktop']['anchors']).isdisjoint(old_ids))
        self.assertEqual(copy_top.x,original_top.x+2000);self.assertEqual(read_shared_worktop(r,copy_top.group)['status'],'ready')
        config=read_config(r,'Kopia / B');config['width']=700;s,_=configure(r,config,'Kopia / B')
        self.assertEqual(next(part.u for part in s.parts if part.id==copy_top.id),original_top.u+100)
        self.assertEqual(next(part.u for part in s.parts if part.id==original_top.id),original_top.u)
        template=extract_group(q,g);self.assertFalse(template.parts[0].assembly.get('shared_worktop'))
        standalone=insert_template(q,template,'Sam blat',(1000,0,0));part=next(part for part in standalone.parts if part.group=='Sam blat')
        self.assertFalse(part.assembly.get('shared_worktop'));self.assertEqual(part.x,original_top.x+1000)


if __name__=='__main__':unittest.main()
