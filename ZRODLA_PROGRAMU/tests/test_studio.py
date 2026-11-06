import copy,math,tempfile,unittest
from pathlib import Path
from core import Project,Part,add_template,world,render_faces,Camera
from assembly import attach,rebuild_dependents,inferred_role
from furniture_library import create_furniture,TEMPLATES
from dimension_model import regenerate_hinges,resize_dimensions,fit_part
from fittings import system32,add_handle,cabinet_connectors
from mechanisms import set_motion,posed_project
from productivity import extract_group,insert_template
from palette import install_palette,PALETTE
from cad_geometry import drilled_shape,step_export

class StudioTests(unittest.TestCase):
    def cabinet(self,index=0):return create_furniture(Project(),index)[0]
    def test_all_templates(self):
        for i in range(len(TEMPLATES)):
            with self.subTest(template=TEMPLATES[i][1]):
                p,g=create_furniture(Project(),i);p.validate();self.assertTrue(p.parts);self.assertTrue(all(q.group==g for q in p.parts));self.assertEqual(len({q.id for q in p.parts}),len(p.parts))
    def test_fronts_get_real_hinge_holes(self):
        p=self.cabinet();front=next(q for q in p.parts if q.role=='door')
        self.assertEqual(sum(o.get('purpose')=='puszka zawiasu' for o in front.operations),2)
        self.assertEqual(sum(o.get('purpose')=='mocowanie puszki zawiasu' for o in front.operations),4)
        self.assertTrue(any(h.get('kind')=='hinge' for h in p.hardware))
    def test_old_front_hinge_repair(self):
        p=Project();add_template(p,'cabinet',800,1200,480,shelves=0,front=True,group='A')
        front=next(q for q in p.parts if q.name=='Front 2');result=regenerate_hinges(p,front.id)
        q=next(q for q in result.parts if q.id==front.id);self.assertEqual(q.motion['mode'],'hinge_right');self.assertEqual(len(q.operations),6)
    def test_short_front_hinges(self):
        p=Project();add_template(p,'cabinet',600,180,300,shelves=0,front=False,group='A')
        p,q=attach(p,Part(u=596,v=176,t=18,material=p.materials[0].id), 'A','door');self.assertEqual(len(q.operations),2)
    def test_hinge_pattern_regenerates(self):
        p=self.cabinet();q=next(q for q in p.parts if q.role=='door');result=resize_dimensions(p,q.id,q.u,650,q.t);q=next(q for q in result.parts if q.role=='door');self.assertEqual(sum(o.get('purpose')=='mocowanie puszki zawiasu' for o in q.operations),4)
    def test_handle_kept_on_front_regeneration(self):
        p=self.cabinet();front=next(q for q in p.parts if q.role=='door');r=resize_dimensions(p,front.id,front.u,650,18);self.assertEqual(sum(h.get('kind')=='handle' and h.get('owner_id')==front.id for h in r.hardware),1)
    def test_no_hinge_duplicates(self):
        p=self.cabinet();q=next(q for q in p.parts if q.role=='door');p=regenerate_hinges(p,q.id,count=3);p=regenerate_hinges(p,q.id,count=3);q=next(x for x in p.parts if x.id==q.id);self.assertEqual(sum(o.get('purpose')=='mocowanie puszki zawiasu' for o in q.operations),6)
    def test_system32_replace(self):
        p=self.cabinet();q=next(q for q in p.parts if q.name=='Bok lewy');r=system32(p,q.id);r=system32(r,q.id);ops=[o for x in r.parts if x.id==q.id for o in x.operations if o.get('recipe_id')=='system32'];self.assertEqual(len(ops),2*(math.floor((q.v-128)/32)+1));self.assertEqual(ops[1]['v']-ops[0]['v'],32)
    def test_handle_drilling(self):
        p=self.cabinet();q=next(q for q in p.parts if q.role=='door');r=add_handle(p,q.id,160,100,300,True);ops=[o for x in r.parts if x.id==q.id for o in x.operations if o.get('recipe_id')=='handle'];self.assertEqual([o['v'] for o in ops],[220,380]);self.assertTrue(all(o['depth']==18 for o in ops))
    def test_confirmat_mating(self):
        p=self.cabinet();r=cabinet_connectors(p,p.parts[0].group);ops=[(q,o) for q in r.parts for o in q.operations if o.get('recipe_id')=='confirmat7'];self.assertEqual(len(ops),16)
        from features import drill_location
        for panel in [q for q in r.parts if inferred_role(q) in ('top','bottom')]:
            edgeops=[o for o in panel.operations if o.get('recipe_id')=='confirmat7'];self.assertEqual(len(edgeops),4)
            for op in edgeops:
                origin,_=drill_location(panel,op);a=world(panel,*origin)
                matching=[(q,o) for q,o in ops if q.id!=panel.id and o.get('owner_id')==panel.id and abs(world(q,*drill_location(q,o)[0])[1]-a[1])<1e-5 and abs(world(q,*drill_location(q,o)[0])[2]-a[2])<1e-5];self.assertEqual(len(matching),2)
        for part in r.parts:drilled_shape(part)
    def test_counterbore_volume(self):
        p=Part(u=100,v=80,t=18);p.operations=[dict(type='drill',face='top',u=40,v=40,diameter=7,depth=18,countersink_diameter=10,countersink_depth=2)];p.validate();shape=drilled_shape(p);base=100*80*18-math.pi*3.5**2*18;self.assertLess(shape.Volume(),base)
    def test_connection_idempotence(self):
        p=self.cabinet();p=cabinet_connectors(p,p.parts[0].group);r=cabinet_connectors(p,p.parts[0].group);self.assertEqual(sum(len(q.operations) for q in p.parts),sum(len(q.operations) for q in r.parts))
    def test_motion_no_mutation(self):
        p=self.cabinet();original=copy.deepcopy(p.data());q=next(q for q in p.parts if q.role=='door');posed=posed_project(p,{q.id:1});self.assertEqual(p.data(),original);self.assertNotEqual(next(x for x in posed.parts if x.id==q.id).rz,q.rz)
    def test_drawer_children_move(self):
        p=self.cabinet(3);q=next(q for q in p.parts if q.role=='drawer');r=posed_project(p,{q.id:1});original={x.id:x for x in p.parts}
        for x in r.parts:
            if x.id==q.id or x.assembly.get('generated_by')==q.id:self.assertAlmostEqual(x.y-original[x.id].y,-q.motion['travel'])
    def test_lift_sliding_cargo(self):
        for mode in ('lift','slide_left','slide_right','cargo','flap_down'):
            p=self.cabinet();q=next(q for q in p.parts if q.role=='door');r=set_motion(p,q.id,mode,90,300);posed=posed_project(r,{q.id:1});a=next(x for x in posed.parts if x.id==q.id);self.assertNotEqual((a.x,a.y,a.z,a.rx,a.rz),(q.x,q.y,q.z,q.rx,q.rz));r.validate()
    def test_lid(self):
        p=self.cabinet(29);q=next(q for q in p.parts if q.name=='Pokrywa');r=posed_project(p,{q.id:1});a=next(x for x in r.parts if x.id==q.id);self.assertGreater(a.z,q.z)
    def test_palette(self):
        p=Project();install_palette(p);n=len(p.materials);install_palette(p);self.assertEqual(len(p.materials),n);self.assertGreaterEqual(len(PALETTE),49);self.assertEqual(len({name for name,color in PALETTE}),len(PALETTE));p.validate()
    def test_fitting_faces(self):
        p=self.cabinet();camera=Camera();camera.fit(p,1000,700);self.assertTrue(any(f.get('fitting') for f in render_faces(p,camera,1000,700)))
    def test_library_copy_hardware(self):
        p=self.cabinet(3);r=insert_template(p,extract_group(p,p.parts[0].group),'B',(1000,0,0));self.assertEqual(len(r.hardware),2*len(p.hardware));r.validate()
    def test_fit_shelf(self):
        p=self.cabinet();s=next(q for q in p.parts if q.role=='shelf');s.u-=10;r=fit_part(p,s.id);q=next(q for q in r.parts if q.id==s.id);self.assertGreater(q.u,s.u)
    def test_roundtrip_motion_and_fittings(self):
        p=self.cabinet(4);r=Project.from_data(p.data());self.assertEqual(r.data(),p.data())
    def test_step_with_confirmats(self):
        p=self.cabinet();r=cabinet_connectors(p,p.parts[0].group)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'assembly.step';step_export(r,path);self.assertGreater(path.stat().st_size,1000)
    def test_whole_resize_handles(self):
        from direct_edit import resize_furniture_face
        p=self.cabinet();front=next(q for q in p.parts if q.role=='door');r=resize_furniture_face(p,front.group,'x1',100);q=next(q for q in r.parts if q.id==front.id)
        handle=next(h for h in r.hardware if h.get('owner_id')==front.id and h.get('kind')=='handle');self.assertEqual(q.u,front.u+100);self.assertEqual(handle['u'],q.u-35);self.assertEqual(handle['v'],q.v/2)
    def test_whole_resize_confirmats(self):
        from direct_edit import resize_furniture_face
        p=self.cabinet();p=cabinet_connectors(p,p.parts[0].group);r=resize_furniture_face(p,p.parts[0].group,'y1',100);r.validate();self.assertEqual(sum(o.get('recipe_id')=='confirmat7' for q in r.parts for o in q.operations),16)
    def test_whole_resize_minimum_anchor(self):
        from direct_edit import resize_furniture_face
        from assembly import frame
        p=self.cabinet();group=p.parts[0].group;old=frame(p,group);r=resize_furniture_face(p,group,'x0',-100);new=frame(r,group);self.assertEqual(new['lower'][0],old['lower'][0]-100);self.assertEqual(new['upper'][0],old['upper'][0])
    def test_handle_thickness_updates(self):
        p=self.cabinet();q=next(q for q in p.parts if q.role=='door');r=resize_dimensions(p,q.id,q.u,q.v,16);self.assertTrue(all(o['depth']==16 for x in r.parts if x.id==q.id for o in x.operations if o.get('recipe_id')=='handle'))
if __name__=='__main__':unittest.main()
