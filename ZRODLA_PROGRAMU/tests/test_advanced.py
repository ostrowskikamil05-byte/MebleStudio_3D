import unittest,math,tempfile
from pathlib import Path
from core import Project,Part
from features import hole_array,shift_features
from productivity import part_array,mirror_part,extract_group,insert_template,equal_shelves
from cad_geometry import local_shape,drilled_shape,step_export,stl_export
from production_docs import drawings_export,labels_export

def cut(shape='rect',kind='pocket',**kw):
    return dict(type=kind,shape=shape,u=100,v=60,length=40,width=20,depth=5,face='top',angle=0,**kw)
class AdvancedTests(unittest.TestCase):
    def part(self):return Part(u=200,v=120,t=20)
    def test_chamfer_each_edge(self):
        from cad_geometry import edge_specs
        for edge,*_ in edge_specs(self.part().points(),20):
            p=self.part();p.chamfers=[dict(edge=edge,distance=2)];p.validate();self.assertLess(local_shape(p).Volume(),480000)
    def test_pocket_volume(self):
        p=self.part();p.machining=[cut()];p.validate();self.assertAlmostEqual(local_shape(p).Volume(),480000-4000,places=4)
    def test_round_cutout_volume(self):
        p=self.part();p.machining=[cut('circle','cutout')];p.validate();self.assertAlmostEqual(local_shape(p).Volume(),480000-math.pi*100*20,places=3)
    def test_slot_volume(self):
        p=self.part();p.machining=[cut('slot')];p.validate();self.assertAlmostEqual(local_shape(p).Volume(),480000-(20*20+math.pi*100)*5,places=3)
    def test_six_drill_faces(self):
        for face in ('top','bottom','u0','u1','v0','v1'):
            p=self.part();p.operations=hole_array(p,30,10,4,5,face=face);p.validate();self.assertAlmostEqual(drilled_shape(p).Volume(),480000-math.pi*4*5,places=3)
    def test_bad_cut(self):
        p=self.part();p.machining=[cut()];p.machining[0]['depth']=20
        with self.assertRaises(ValueError):p.validate()
    def test_profile_conflict(self):
        p=self.part();p.chamfers=[dict(edge='top:0',distance=2)];p.fillets=[dict(edge='top:0',radius=2)]
        with self.assertRaises(ValueError):p.validate()
    def test_array_atomic(self):
        project=Project();p=self.part();p.material=project.materials[0].id;project.parts=[p]
        result=part_array(project,p.id,3,0,0,50);self.assertEqual(len(result.parts),4);self.assertEqual(len(project.parts),1);self.assertEqual(len({q.id for q in result.parts}),4)
    def test_mirror_features(self):
        p=self.part();p.chamfers=[dict(edge='top:1',distance=2)];p.machining=[cut()];p.operations=hole_array(p,30,10,4,5,face='u0');q=mirror_part(p,'u');self.assertEqual(q.operations[0]['face'],'u1');self.assertAlmostEqual(drilled_shape(q).Volume(),drilled_shape(p).Volume(),places=3)
    def test_template_roundtrip(self):
        from core import add_template
        p=Project();add_template(p,'cabinet',800,1200,480,shelves=0,front=False,group='A');q=insert_template(p,extract_group(p,'A'),'B',(1000,0,0));self.assertEqual(len(q.parts),2*len(p.parts));self.assertEqual(len({x.id for x in q.parts}),len(q.parts));q.validate()
    def test_equal_shelves(self):
        from core import add_template
        p=Project();add_template(p,'cabinet',800,1200,480,shelves=0,front=False,group='A');q=equal_shelves(p,'A',3,18,p.materials[0].id);self.assertEqual(len(q.parts),len(p.parts)+3);q.validate()
    def test_documents_and_exports(self):
        p=Project();q=self.part();q.material=p.materials[0].id;q.machining=[cut()];p.parts=[q]
        with tempfile.TemporaryDirectory() as d:
            for fn,name in ((drawings_export,'parts.html'),(labels_export,'labels.html'),(step_export,'model.step'),(stl_export,'model.stl')):
                path=Path(d)/name;fn(p,path);self.assertGreater(path.stat().st_size,100)
    def test_shift_side_coordinates(self):
        p=self.part();p.operations=hole_array(p,30,10,4,5,face='u0');shift_features(p,2,3,1);self.assertEqual((p.operations[0]['u'],p.operations[0]['v']),(27,9))
if __name__=='__main__':unittest.main()
