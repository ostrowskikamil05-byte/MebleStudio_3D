import copy
import json
import tempfile
import unittest
from pathlib import Path
from core import Project,Part,add_template,manufacturing_export,dxf_export,report_export
from assembly import attach,suggested_size,delete_with_connections,rebuild_dependents

class ConstructionTests(unittest.TestCase):
    def cabinet(self):
        project=Project();add_template(project,'cabinet',800,1200,480,shelves=0,front=False,group='Szafka',origin=(100,200,50))
        return project
    def panel(self,project,role,config=None):
        u,v=suggested_size(project,'Szafka',role,18,config)
        return Part(u=u,v=v,t=18,material=project.materials[0].id,name=role)
    def test_door_connections_and_regeneration(self):
        project=self.cabinet();q=self.panel(project,'door')
        before=copy.deepcopy(project.data())
        result,door=attach(project,q,'Szafka','door')
        self.assertEqual(project.data(),before)
        self.assertEqual((door.plane,door.x,door.y,door.z),('XZ',102,180,52))
        self.assertEqual(len(door.operations),2)
        left=next(p for p in result.parts if p.name=='Bok lewy')
        self.assertEqual(len(left.operations),4)
        self.assertTrue(all(op['face']=='top' for op in left.operations))
        self.assertEqual(result.hardware[0]['qty'],2)
        result,door=attach(result,door,'Szafka','door',{'hinge_count':3})
        self.assertEqual(len(door.operations),3)
        self.assertEqual(len(next(p for p in result.parts if p.name=='Bok lewy').operations),6)
        self.assertEqual(len(result.hardware),1)
        result,door=attach(result,door,'Szafka','door',{'hinge_side':'right'})
        right=next(p for p in result.parts if p.name=='Bok prawy')
        self.assertTrue(all(op['face']=='bottom' for op in right.operations))
        self.assertEqual(len(next(p for p in result.parts if p.name=='Bok lewy').operations),0)
        delete_with_connections(result,door.id);result.validate()
        self.assertFalse(result.hardware)
        self.assertTrue(all(not p.operations for p in result.parts))
    def test_shelf_drilling(self):
        project=self.cabinet();q=self.panel(project,'shelf')
        result,shelf=attach(project,q,'Szafka','shelf',{'level':300})
        self.assertEqual((shelf.x,shelf.y,shelf.z),(120,202,350))
        self.assertEqual(result.hardware[0]['qty'],4)
        self.assertEqual(sum(len(p.operations) for p in result.parts),4)
    def test_drawer_box_slides_and_cleanup(self):
        project=self.cabinet();q=self.panel(project,'drawer')
        result,front=attach(project,q,'Szafka','drawer',{'level':30})
        self.assertEqual(len(result.parts),len(project.parts)+6)
        children=[p for p in result.parts if p.assembly.get('generated_by')==front.id]
        self.assertEqual(len(children),5)
        self.assertEqual(sum(len(p.operations) for p in result.parts),8)
        result,front=attach(result,front,'Szafka','drawer',{'level':230})
        self.assertEqual(len(result.parts),len(project.parts)+6)
        self.assertEqual(len(result.hardware),1)
        delete_with_connections(result,front.id);result.validate()
        self.assertEqual(len(result.parts),len(project.parts))
    def test_failure_is_atomic_and_collision(self):
        project=self.cabinet();q=self.panel(project,'door');before=copy.deepcopy(project.data())
        with self.assertRaises(ValueError):attach(project,q,'Szafka','door',{'cup_depth':30})
        self.assertEqual(project.data(),before)
        shelf=self.panel(project,'shelf');project,_=attach(project,shelf,'Szafka','shelf',{'level':100})
        drawer=self.panel(project,'drawer')
        with self.assertRaisesRegex(ValueError,'koliduje'):attach(project,drawer,'Szafka','drawer',{'level':30})
    def test_roundtrip_and_exports_preserve_connections(self):
        project=self.cabinet();project,door=attach(project,self.panel(project,'door'),'Szafka','door')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);project.save(root/'project.mebel');project=Project.load(root/'project.mebel')
            manufacturing_export(project,root/'production.json');dxf_export(project,root/'drawing.dxf');report_export(project,root/'report.html')
            data=json.loads((root/'production.json').read_text(encoding='utf-8'))
            self.assertEqual(len(data['hardware']),1)
            self.assertEqual(len(next(p for p in data['parts'] if p['id']==door.id)['drilling']),2)
            self.assertIn('AUTO_TOP_', (root/'drawing.dxf').read_text())
            self.assertIn('Zawias puszkowy', (root/'report.html').read_text(encoding='utf-8'))
    def test_anchor_delete_drops_stale_connections(self):
        project=self.cabinet();project,door=attach(project,self.panel(project,'door'),'Szafka','door')
        left=next(p for p in project.parts if p.name=='Bok lewy')
        delete_with_connections(project,left.id);project.validate()
        self.assertFalse(project.hardware)
        self.assertEqual(next(p for p in project.parts if p.id==door.id).role,'other')

    def test_legacy_two_doors_and_structural_rebuild(self):
        project=Project();add_template(project,'cabinet',800,1200,480,shelves=0,front=True,group='Szafka')
        for name,side in [('Front 1','left'),('Front 2','right')]:
            part=next(p for p in project.parts if p.name==name)
            project,_=attach(project,part,'Szafka','door',{'hinge_side':side})
        self.assertEqual(len(project.hardware),2)
        left=next(p for p in project.parts if p.name=='Bok lewy')
        replacement=copy.deepcopy(left)
        project,_=attach(project,replacement,'Szafka','left',{'auto_hardware':False})
        project=rebuild_dependents(project,left.id)
        self.assertEqual(len(project.hardware),2)
        self.assertEqual(sum(len(p.operations) for p in project.parts),12)
        project.validate()

    def test_overlap_rejection_and_manual_holes_survive(self):
        project=self.cabinet()
        duplicate=self.panel(project,'left')
        with self.assertRaisesRegex(ValueError,'koliduje'):attach(project,duplicate,'Szafka','left')
        q=self.panel(project,'door');q.holes=[[200,300,4,5]]
        project,door=attach(project,q,'Szafka','door')
        project,door=attach(project,door,'Szafka','door',{'hinge_count':3})
        self.assertEqual(door.holes,[[200,300,4,5]])
