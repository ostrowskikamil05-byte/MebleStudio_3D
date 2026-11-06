import json
import tempfile
import unittest
from pathlib import Path
from core import Project, Part, manufacturing_export

class ManufacturingTests(unittest.TestCase):
    def test_wood_cost_and_legacy_project(self):
        project = Project()
        wood = next(m for m in project.materials if m.kind == 'drewno')
        wood.price_m3 = 5000
        project.parts.append(Part(u=1000,v=200,t=20,material=wood.id))
        self.assertAlmostEqual(project.costs()['panels'],20)
        data = project.data()
        for material in data['materials']:
            material.pop('kind'); material.pop('price_m3')
        self.assertEqual(Project.from_data(data).materials[0].kind,'plyta')

    def test_export_local_drilling(self):
        project = Project()
        project.parts.append(Part(u=100,v=80,t=18,material=project.materials[0].id,
                                  holes=[[20,30,5,12]],x=500))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'production.json'
            manufacturing_export(project,path)
            data = json.loads(path.read_text(encoding='utf-8'))
        self.assertFalse(data['machine_program'])
        self.assertEqual(data['parts'][0]['drilling'][0],
                         dict(u=20,v=30,diameter=5,depth=12,face='top'))
        self.assertEqual(data['parts'][0]['assembly']['position'],[500,0,0])
