import copy
import unittest
from core import Project,Part,add_template
from assembly import attach,suggested_size
from direct_edit import resize_part,resize_furniture

class DirectEditTests(unittest.TestCase):
    def test_opposite_face_and_manual_hole_position(self):
        project=Project();part=Part(u=600,v=400,t=18,x=100,material=project.materials[0].id,holes=[[200,200,4,5]])
        project.parts=[part]
        changed=resize_part(project,part.id,'u0',50);p=changed.parts[0]
        self.assertEqual((p.x,p.u),(150,550));self.assertEqual(p.holes[0][0],150)
        self.assertEqual((project.parts[0].x,project.parts[0].u),(100,600))
        self.assertEqual(resize_part(project,part.id,'v1',100).parts[0].v,500)
    def test_reject_invalid_atomic_edit(self):
        project=Project();part=Part(material=project.materials[0].id);project.parts=[part]
        before=project.data()
        with self.assertRaises(ValueError):resize_part(project,part.id,'u1',-1000)
        self.assertEqual(before,project.data())
    def test_whole_cabinet_preserves_thickness_and_inner_dimensions(self):
        project=Project();add_template(project,'cabinet',800,1200,480,shelves=2,front=True,group='Mebel')
        result=resize_furniture(project,'Mebel',1000,1400,600)
        left=next(p for p in result.parts if p.name=='Bok lewy');right=next(p for p in result.parts if p.name=='Bok prawy')
        bottom=next(p for p in result.parts if p.name=='Dno');top=next(p for p in result.parts if p.name=='Wieniec')
        self.assertEqual((left.u,left.v,left.t),(600,1400,18))
        self.assertEqual(right.x,982)
        self.assertEqual((bottom.u,bottom.v,top.z),(964,600,1382))
        self.assertEqual(next(p for p in result.parts if p.name=='Plecy nakładane HDF').y,600)
        self.assertTrue(all(p.t==18 or p.t==3 for p in result.parts))
    def test_resize_with_managed_door_and_shelf(self):
        project=Project();add_template(project,'cabinet',800,1200,480,shelves=0,front=False,group='Mebel')
        for role in ('door','shelf'):
            u,v=suggested_size(project,'Mebel',role,18)
            project,_=attach(project,Part(u=u,v=v,material=project.materials[0].id),'Mebel',role)
        left=next(p for p in project.parts if p.name=='Bok lewy')
        project,_=attach(project,left,'Mebel','left',{'auto_hardware':False})
        from assembly import rebuild_dependents
        project=rebuild_dependents(project,left.id)
        result=resize_furniture(project,'Mebel',1000,1400,600)
        door=next(p for p in result.parts if p.role=='door');shelf=next(p for p in result.parts if p.role=='shelf')
        self.assertEqual((door.u,door.v),(996,1396))
        self.assertEqual((shelf.u,shelf.v),(960,596))
        self.assertEqual(sum(h['qty'] for h in result.hardware),6)
        self.assertEqual(next(p for p in result.parts if p.name=='Bok lewy').x,0)
