import importlib.util
import tempfile
import unittest
from pathlib import Path
from core import Part,Project,faces,dxf_export,manufacturing_export
from cad_geometry import edge_specs,local_shape,step_export

@unittest.skipUnless(importlib.util.find_spec('cadquery'),'Optional CAD kernel is not installed')
class CadGeometryTests(unittest.TestCase):
    def test_each_of_twelve_edges_can_be_rounded(self):
        base=Part(u=100,v=80,t=18)
        specs=edge_specs(base.points(),base.t);self.assertEqual(len(specs),12)
        for edge,*_ in specs:
            part=Part(u=100,v=80,t=18,fillets=[dict(edge=edge,radius=3)])
            part.validate()
            self.assertTrue(local_shape(part).isValid())
            self.assertLess(local_shape(part).Volume(),100*80*18)
            self.assertGreater(len(faces(part)),6)
    def test_different_radii_and_roundtrip(self):
        project=Project();part=Part(u=100,v=80,t=18,material=project.materials[0].id,
            fillets=[dict(edge='top:0',radius=3),dict(edge='corner:2',radius=5)])
        project.parts=[part];project.validate()
        result=Project.from_data(project.data())
        self.assertEqual(result.parts[0].fillets,part.fillets)
        self.assertAlmostEqual(local_shape(result.parts[0]).Volume(),local_shape(part).Volume())
    def test_impossible_radius_and_drilling_conflict(self):
        with self.assertRaises(ValueError):Part(u=100,v=80,t=18,fillets=[dict(edge='top:0',radius=50)]).validate()
        with self.assertRaisesRegex(ValueError,'wiercenie'):
            Part(u=100,v=80,t=18,fillets=[dict(edge='corner:0',radius=20)],holes=[[4,4,4,5]]).validate()
    def test_step_export_drilling_and_world_planes(self):
        import cadquery as cq
        for plane in ('XY','XZ','YZ'):
            project=Project();p=Part(u=100,v=80,t=18,x=100,y=200,z=300,plane=plane,
                material=project.materials[0].id,fillets=[dict(edge='top:0',radius=3)],holes=[[50,40,5,10]])
            project.parts=[p]
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'model.step';step_export(project,path)
                imported=cq.importers.importStep(str(path)).val()
                self.assertTrue(imported.isValid());box=imported.BoundingBox()
                self.assertAlmostEqual(box.xmin,100,places=5);self.assertAlmostEqual(box.ymin,200,places=5);self.assertAlmostEqual(box.zmin,300,places=5)
                self.assertAlmostEqual(imported.Volume(),local_shape(p).Volume()-3.141592653589793*2.5**2*10,places=3)
                with self.assertRaises(ValueError):dxf_export(project,Path(directory)/'flat.dxf')

    def test_generated_drawer_keeps_edge_radii_after_regeneration(self):
        from core import add_template
        from assembly import attach,suggested_size
        project=Project();add_template(project,'cabinet',800,1200,480,shelves=0,front=False,group='Mebel')
        u,v=suggested_size(project,'Mebel','drawer',18)
        project,front=attach(project,Part(u=u,v=v,t=18,material=project.materials[0].id),'Mebel','drawer',{'level':30})
        child=next(p for p in project.parts if p.assembly.get('generated_by')==front.id)
        child.fillets=[dict(edge='corner:0',radius=3)];name=child.name
        project.validate();project,front=attach(project,front,'Mebel','drawer',{'level':250})
        self.assertEqual(next(p for p in project.parts if p.name==name).fillets,[dict(edge='corner:0',radius=3)])
