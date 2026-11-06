import copy
import math
import tempfile
import unittest
from pathlib import Path
from core import Project,Part,world
from furniture_configurator import default_config,normalize_config,configure,read_config,configuration_status,infer_config
from furniture_layout import rotate_group,move_group,group_layout,group_bounds
from assembly import frame_for_part


class FurnitureConfiguratorTests(unittest.TestCase):
    def make(self,index=0,**changes):
        project=Project();config=default_config(index,project);config.update(changes)
        return project,config

    def test_defaults_build_exact_total_height_and_purchased_legs(self):
        p,c=self.make();before=p.data();q,g=configure(p,c)
        self.assertEqual(p.data(),before);self.assertEqual(read_config(q,g)['height'],820)
        left=next(part for part in q.parts if part.role=='left');self.assertEqual((left.z,left.v),(100,720))
        legs=next(h for h in q.hardware if h.get('kind')=='leg');self.assertEqual(legs['qty'],4)
        self.assertEqual(legs['height'],100);self.assertTrue(all(pos['z']==0 for pos in legs['positions']))
        self.assertFalse(any(part.role=='leg' for part in q.parts));self.assertTrue(configuration_status(q,g)['editable'])

    def test_worktop_and_plinth_use_real_panels_with_total_height(self):
        p,c=self.make(height=1000,worktop=True,worktop_thickness=30,plinth=True)
        q,g=configure(p,c);top=next(part for part in q.parts if part.role=='worktop');plinth=next(part for part in q.parts if part.name=='Cokół')
        self.assertEqual(top.z+top.t,1000);self.assertEqual((plinth.z,plinth.v),(0,100))
        self.assertEqual(plinth.role,'accessory_panel')

    def test_three_stages_and_columns_create_usable_local_bays(self):
        p,c=self.make(width=1200,height=1800)
        c['sections'][0].update(enabled=True,height=300,columns=2,shelves=0,front='drawers',drawers=2)
        c['sections'][1].update(columns=3,shelves=2,doors=2)
        c['sections'][2].update(enabled=True,height=300,columns=2,shelves=0)
        q,g=configure(p,c)
        self.assertEqual(len([part for part in q.parts if part.role=='door']),8)
        self.assertEqual(len([part for part in q.parts if part.role=='drawer']),4)
        self.assertEqual(len([part for part in q.parts if part.role=='shelf']),6)
        for part in [part for part in q.parts if part.role in ('door','drawer','shelf')]:
            f=frame_for_part(q,part);self.assertLess(f['lower'][0],f['upper'][0]);self.assertTrue(part.assembly['config']['bay'])
            if part.role=='door':
                cup=[op for op in part.operations if op.get('purpose')=='puszka zawiasu'];self.assertEqual(len(cup),2)
                support=f['left'] if part.assembly['config']['hinge_side']=='left' else f['right']
                self.assertTrue(any(op.get('owner_id')==part.id for op in support.operations))
        q.validate()

    def test_IDs_layout_materials_and_price_metadata_survive_rebuild(self):
        p,c=self.make(width=900);q,g=configure(p,c);q=rotate_group(move_group(q,g,1500,-600,40),g,90)
        ids={part.configurator_source['key']:part.id for part in q.parts};layout=group_layout(q,g)
        left=next(part for part in q.parts if part.role=='left');left.material=q.materials[2].id;left.name='Mój lewy bok';left.grain=False;left.edges[0]=True
        hinge=next(h for h in q.hardware if h.get('kind')=='hinge');hinge['price']=17;hinge['supplier']='Mój dostawca'
        c['width']=1000;r,_=configure(q,c,g)
        self.assertEqual(group_layout(r,g),layout);self.assertEqual(ids,{part.configurator_source['key']:part.id for part in r.parts})
        left=next(part for part in r.parts if part.id==left.id);self.assertEqual(left.material,q.materials[2].id);self.assertEqual(left.name,'Mój lewy bok');self.assertFalse(left.grain);self.assertTrue(left.edges[0])
        self.assertEqual(next(h['price'] for h in r.hardware if h.get('kind')=='hinge'),17)

    def test_custom_parts_and_manual_hardware_and_machining_are_retained(self):
        p,c=self.make();q,g=configure(p,c);left=next(part for part in q.parts if part.role=='left')
        left.holes=[[220,220,4,5]]
        left.operations.append(dict(type='drill',face='top',u=250,v=250,diameter=4,depth=5,purpose='Mój otwór'))
        extra=Part(name='Ręczny dodatek',group=g,x=1100,material=q.materials[0].id,u=100,v=100,t=18)
        q.parts.append(extra);q.hardware.append(dict(owner_id=left.id,name='Własne okucie',qty=1,price=12,manual=True))
        c['width']=700;r,_=configure(q,c,g)
        self.assertEqual(next(part for part in r.parts if part.id==extra.id),extra)
        left=next(part for part in r.parts if part.id==left.id);self.assertEqual(left.holes,[[220,220,4,5]])
        self.assertEqual(sum(op.get('purpose')=='Mój otwór' for op in left.operations),1)
        self.assertTrue(any(h['name']=='Własne okucie' for h in r.hardware));r.validate()

    def test_manual_geometry_edit_refuses_without_modifying_input(self):
        p,c=self.make();q,g=configure(p,c);q.parts[0].u-=5;before=q.data()
        self.assertFalse(configuration_status(q,g)['editable'])
        with self.assertRaisesRegex(ValueError,'ręcznie'):configure(q,c,g)
        self.assertEqual(before,q.data())

    def test_removing_a_part_with_manual_features_is_refused(self):
        p,c=self.make();c['sections'][1]['shelves']=2;q,g=configure(p,c)
        shelf=next(part for part in q.parts if part.configurator_source.get('key','').endswith('.shelf.2'));shelf.holes=[[150,100,4,5]]
        before=q.data();c['sections'][1]['shelves']=1
        with self.assertRaisesRegex(ValueError,'ręczną'):configure(q,c,g)
        self.assertEqual(before,q.data())

    def test_manual_append_collision_is_reported_atomically(self):
        p,c=self.make();q,g=configure(p,c)
        q.parts.append(Part(name='Własna półka',group=g,x=18,y=0,z=110,material=q.materials[0].id,u=560,v=560,t=18))
        before=q.data();c['sections'][1]['shelves']=2
        with self.assertRaises(ValueError):configure(q,c,g)
        self.assertEqual(q.data(),before)

    def test_read_save_load_and_new_project_name_are_stable(self):
        p,c=self.make();q,g=configure(p,c);r,new_group=configure(q,c)
        self.assertNotEqual(g,new_group);self.assertEqual(len(r.parts),len(q.parts)*2)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'kreator.mebel';r.save(path);loaded=Project.load(path)
            self.assertEqual(loaded.data(),r.data());self.assertTrue(configuration_status(loaded,g)['editable'])

    def test_legacy_conversion_is_explicit_and_preserves_all_IDs(self):
        from furniture_library import create_furniture
        for index in (0,3,16):
            with self.subTest(index=index):
                p,g=create_furniture(Project(),index);before=p.data();self.assertIsNone(read_config(p,g))
                cfg=infer_config(p,g);q,_=configure(p,cfg,g)
                self.assertEqual(p.data(),before);self.assertEqual({part.id for part in p.parts},{part.id for part in q.parts})
                self.assertTrue(configuration_status(q,g)['editable'])

    def test_modified_legacy_cabinet_is_never_silently_adopted(self):
        from furniture_library import create_furniture
        p,g=create_furniture(Project(),0);p.parts[0].u-=1;before=p.data()
        with self.assertRaises(ValueError):infer_config(p,g)
        self.assertEqual(p.data(),before)
        p,g=create_furniture(Project(),0);p.parts[0].operations.append(dict(type='drill',face='top',u=200,v=200,diameter=4,depth=5))
        with self.assertRaises(ValueError):infer_config(p,g)

    def test_runner_profile_applies_to_each_bay_and_motion_rotates_correctly(self):
        from mechanisms import posed_project
        p,c=self.make(3,width=1200,runner_profile='blum_movento_760h',runner_length=450)
        c['sections'][1]['columns']=2;q,g=configure(p,c)
        drawers=[part for part in q.parts if part.role=='drawer'];self.assertEqual(len(drawers),6)
        runners=[h for h in q.hardware if h.get('kind')=='runner'];self.assertEqual(len(runners),6)
        self.assertTrue(all(h['dimensions']['external_clearance_per_side']==5 for h in runners))
        q=rotate_group(q,g,90);front=next(part for part in q.parts if part.role=='drawer');posed=posed_project(q,{front.id:1})
        placed=next(part for part in posed.parts if part.id==front.id)
        a,b=world(front,0,0,0),world(placed,0,0,0);self.assertAlmostEqual(b[0]-a[0],450);self.assertAlmostEqual(b[1]-a[1],0)

    def test_refit_and_repair_hinges_do_not_escape_the_bay(self):
        from dimension_model import fit_part,regenerate_hinges
        p,c=self.make(width=1200);c['sections'][1].update(columns=3,doors=2)
        q,g=configure(p,c);front=next(part for part in q.parts if part.configurator_source.get('key')=='main.bay.1.door.1')
        original=frame_for_part(q,front);q=regenerate_hinges(q,front.id,count=3);q=fit_part(q,front.id)
        repaired=next(part for part in q.parts if part.id==front.id);f=frame_for_part(q,repaired)
        self.assertEqual(f['lower'],original['lower']);self.assertEqual(f['upper'],original['upper'])
        self.assertAlmostEqual(repaired.x+repaired.u,f['upper'][0]-2);self.assertEqual(len([op for op in repaired.operations if op.get('purpose')=='puszka zawiasu']),3)

    def test_tip_on_uses_its_bay_opposite_support(self):
        p,c=self.make(width=1200,handle_mode='tip_short',hinge_spring='unsprung')
        c['sections'][1].update(columns=3,doors=1);q,g=configure(p,c)
        front=next(part for part in q.parts if part.configurator_source.get('key')=='main.bay.1.door.0')
        expected=frame_for_part(q,front)['right'].id
        opening=next(h for h in q.hardware if h.get('owner_id')==front.id and h.get('kind')=='opening')
        self.assertEqual(opening['anchors'],[expected]);q.validate()

    def test_finger_style_does_not_also_add_a_metal_handle(self):
        p,c=self.make(front_style='finger');q,g=configure(p,c)
        self.assertFalse(any(h.get('kind')=='handle' for h in q.hardware))
        front=next(part for part in q.parts if part.role=='door');self.assertTrue(any(cut.get('config_auto') for cut in front.machining))
        c['height']+=50;r,_=configure(q,c,g);self.assertFalse(any(h.get('kind')=='handle' for h in r.hardware))

    def test_invalid_size_and_sections_are_atomic(self):
        p,c=self.make();before=p.data()
        for changes in ({'height':100,'leg_height':200},{'width':float('nan')},{'leg_count':5}):
            invalid=copy.deepcopy(c);invalid.update(changes)
            with self.assertRaises(ValueError):configure(p,invalid)
        invalid=copy.deepcopy(c);invalid['sections'][0].update(enabled=True,height=700)
        with self.assertRaises(ValueError):configure(p,invalid)
        self.assertEqual(before,p.data())

    def test_LED_rebuild_is_idempotent_and_a_purchase_cost_change_applies(self):
        p,c=self.make(led=True,leg_price=4,led_price_per_m=12)
        q,g=configure(p,c);r,_=configure(q,c,g)
        self.assertEqual(len(q.hardware),len(r.hardware));self.assertEqual(sum(len(part.machining) for part in q.parts),sum(len(part.machining) for part in r.parts))
        c['leg_price']=9;r,_=configure(r,c,g)
        self.assertEqual(next(h['price'] for h in r.hardware if h.get('kind')=='leg'),9)

    def test_group_rename_keeps_IDs_layout_manual_parts_and_references(self):
        p,c=self.make();q,g=configure(p,c);q=rotate_group(move_group(q,g,1200,-400,80),g,90)
        ids={part.id for part in q.parts};layout=group_layout(q,g)
        left=next(part for part in q.parts if part.role=='left')
        extra=Part(name='Ręczny dodatek',group=g,x=1500,material=q.materials[0].id,u=100,v=100,t=18,layout=copy.deepcopy(layout))
        extra.assembly={'group':g};q.parts.append(extra)
        q.hardware.append(dict(owner_id=extra.id,group=g,name='Ręczne okucie',qty=1,price=5))
        c['name']='Szafka po zmianie';before=q.data();r,new_group=configure(q,c,g)
        self.assertEqual(before,q.data());self.assertEqual(new_group,c['name'])
        self.assertEqual({part.id for part in r.parts},ids|{extra.id});self.assertEqual(group_layout(r,new_group),layout)
        self.assertTrue(all(part.group==new_group for part in r.parts))
        self.assertTrue(all(part.assembly.get('group',new_group)==new_group for part in r.parts))
        self.assertEqual(next(h['group'] for h in r.hardware if h.get('owner_id')==extra.id),new_group)
        self.assertIsNone(read_config(r,g));self.assertEqual(read_config(r,new_group)['name'],new_group)
        self.assertTrue(configuration_status(r,new_group)['editable'])
        other=copy.deepcopy(c);other['name']='Zajęta nazwa';r,occupied=configure(r,other)
        invalid=read_config(r,new_group);invalid['name']=occupied;before=r.data()
        with self.assertRaisesRegex(ValueError,'już istnieje'):configure(r,invalid,new_group)
        self.assertEqual(r.data(),before)

    def test_whole_furniture_drag_updates_config_total_height_legs_and_LED(self):
        from direct_edit import resize_furniture
        p,c=self.make(width=900,worktop=True,worktop_thickness=30,led=True)
        q,g=configure(p,c);q=rotate_group(move_group(q,g,900,500,25),g,90)
        layout=group_layout(q,g);ids={part.configurator_source['key']:part.id for part in q.parts};before=q.data()
        r=resize_furniture(q,g,1000,780,600);updated=read_config(r,g)
        self.assertEqual(q.data(),before);self.assertEqual((updated['width'],updated['height'],updated['depth']),(1000,910,600))
        self.assertEqual(ids,{part.configurator_source['key']:part.id for part in r.parts});self.assertEqual(group_layout(r,g),layout)
        self.assertTrue(configuration_status(r,g)['editable'])
        self.assertEqual(next(h['height'] for h in r.hardware if h.get('kind')=='leg'),100)
        self.assertEqual(sum(h.get('qty',0) for h in r.hardware if h.get('kind')=='led_strip'),.9)
        updated['sections'][1]['doors']=2;s,_=configure(r,updated,g)
        self.assertEqual(len([part for part in s.parts if part.role=='door']),2)
        self.assertTrue(configuration_status(s,g)['editable'])

    def test_minimum_face_drag_changes_floor_origin_and_preserves_opposite_face(self):
        from direct_edit import resize_furniture_face
        from assembly import frame
        p,c=self.make(width=900,worktop=True,worktop_thickness=30,led=True,origin=[200,300,50])
        q,g=configure(p,c);old=frame(q,g);q=rotate_group(move_group(q,g,900,400,20),g,90)
        r=resize_furniture_face(q,g,'x0',40);cfg=read_config(r,g);f=frame(r,g)
        self.assertEqual(cfg['origin'],[240,300,50]);self.assertEqual(cfg['width'],860)
        self.assertEqual(f['upper'][0],old['upper'][0]);self.assertTrue(configuration_status(r,g)['editable'])
        s=resize_furniture_face(r,g,'z0',30);cfg=read_config(s,g);f=frame(s,g)
        self.assertEqual(cfg['origin'],[240,300,80]);self.assertEqual(cfg['height'],c['height']-30)
        self.assertEqual(f['upper'][2],old['upper'][2]);self.assertTrue(configuration_status(s,g)['editable'])
        self.assertTrue(all(pos['z']==80 for h in s.hardware if h.get('kind')=='leg' for pos in h['positions']))
        self.assertEqual(group_layout(s,g),group_layout(q,g))

    def test_normalization_rejects_unknown_hinges_and_preserves_integer_template_index(self):
        p,c=self.make();c['template_index']=0.0
        self.assertEqual(normalize_config(c,p)['template_index'],0)
        for key,value in (('hinge_pattern','missing'),('hinge_spring','wrong')):
            invalid=copy.deepcopy(c);invalid[key]=value
            with self.assertRaisesRegex(ValueError,'zawias'):normalize_config(invalid,p)

    def test_magnetic_tip_on_requires_actual_unsprung_hinge_declaration(self):
        p,c=self.make(handle_mode='tip_short');before=p.data()
        for state in ('unknown','sprung'):
            c['hinge_spring']=state
            with self.assertRaisesRegex(ValueError,'bez sprężyny'):configure(p,c)
            self.assertEqual(p.data(),before)
        c.update(hinge_spring='unsprung',hinge_pattern='hinge_sensys52')
        with self.assertRaisesRegex(ValueError,'sprężyn'):configure(p,c)


if __name__=='__main__':unittest.main()
