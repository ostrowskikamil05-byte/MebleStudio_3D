import csv
import math
from pathlib import Path
import random
import tempfile
import unittest
from core import *

class ModelTests(unittest.TestCase):
    def part(self,project,**kwargs):
        p=Part(material=project.materials[0].id,**kwargs);project.parts.append(p);return p

    def test_cabinet_dimensions(self):
        project=Project();parts=add_template(project,'cabinet',800,1200,480,18,2,True)
        project.validate()
        self.assertEqual(len(parts),9)
        bottom=next(p for p in parts if p.name=='Dno')
        self.assertEqual((bottom.u,bottom.v,bottom.x),(764,480,18))
        fronts=[p for p in parts if p.name.startswith('Front')]
        self.assertEqual(fronts[0].u,397)
        self.assertEqual(fronts[1].x-(fronts[0].x+fronts[0].u),2)
        self.assertEqual(800-(fronts[1].x+fronts[1].u),2)

    def test_table(self):
        p=Project();add_template(p,'table',1200,750,700)
        self.assertEqual(len(p.parts),5);p.validate()
        self.assertTrue(all(math.isfinite(x) for part in p.parts for face in faces(part) for vertex in face for x in vertex))

    def test_edge_deduction_and_costs(self):
        project=Project();project.edge_t=2;project.edge_price=4;project.labor=100;project.markup=20
        p=self.part(project,u=600,v=400,edges=[True,False,True,True]);project.mat(p.material).price_m2=50
        project.hardware=[dict(name='Zawias',qty=4,price=10)]
        self.assertEqual(p.blank(2),(598,396))
        self.assertAlmostEqual(p.edge_m(),1.6)
        c=project.costs();self.assertAlmostEqual(c['panels'],12)
        self.assertAlmostEqual(c['total'],(12+6.4+40+100)*1.2)

    def test_polygon_and_holes(self):
        project=Project();p=self.part(project,contour=[[0,0],[600,0],[600,200],[300,400],[0,400]],holes=[[100,100,10,12]])
        project.validate();self.assertAlmostEqual(p.area(),.21)
        p.holes=[[580,300,10,12]]
        with self.assertRaises(ValueError):p.validate()
        with self.assertRaises(ValueError):validate_polygon([(0,0),(10,10),(0,10),(10,0)])
        with self.assertRaises(ValueError):validate_polygon([(0,0),(10001,0),(0,10)])

    def test_rotations_and_planes(self):
        self.assertAlmostEqual(rotate((1,0,0),0,0,90)[1],1)
        p=Part(plane='YZ',x=10,y=20,z=30)
        self.assertEqual(world(p,100,200,18),(28,120,230))
        for _ in range(50):
            rng=random.Random(_);v=tuple(rng.random()*100 for i in range(3));r=rotate(v,*[rng.random()*360 for i in range(3)])
            self.assertAlmostEqual(sum(x*x for x in v),sum(x*x for x in r),places=7)

    def assert_layout(self,p):
        sheets,bad=cut_plan(p);placed=[]
        for s in sheets:
            for r in s['pieces']:
                placed.append(r['id']);part=next(a for a in p.parts if a.id==r['id'])
                self.assertEqual(s['material'],part.material);self.assertEqual(s['thickness'],part.t)
                self.assertGreaterEqual(r['x'],p.margin);self.assertGreaterEqual(r['y'],p.margin)
                self.assertLessEqual(r['x']+r['u'],s['u']-p.margin+1e-7)
                self.assertLessEqual(r['y']+r['v'],s['v']-p.margin+1e-7)
                self.assertFalse(part.grain and r['rotated'])
            for i,a in enumerate(s['pieces']):
                for b in s['pieces'][i+1:]:
                    # A full kerf must separate each pair on at least one axis.
                    gap=max(b['x']-a['x']-a['u'],a['x']-b['x']-b['u'],b['y']-a['y']-a['v'],a['y']-b['y']-b['v'])
                    self.assertGreaterEqual(gap+1e-7,p.kerf)
        self.assertEqual(len(placed),len(set(placed)))
        self.assertEqual(set(placed+bad),{a.id for a in p.parts})
        return sheets,bad

    def test_cutting_randomized(self):
        for seed in range(12):
            rng=random.Random(seed);p=Project();p.kerf=rng.choice([0,3.2,5]);p.margin=rng.choice([0,10,25])
            for i in range(45):
                p.parts.append(Part(u=rng.randint(30,3000),v=rng.randint(30,2300),t=rng.choice([18,3]),grain=rng.choice([True,False]),material=p.materials[rng.randint(0,1)].id))
            self.assert_layout(p)

    def test_exact_sheet_and_rotation(self):
        p=Project();m=p.materials[0];m.sheet_u=1000;m.sheet_v=500;p.margin=0
        a=self.part(p,u=1000,v=500);s,bad=self.assert_layout(p);self.assertFalse(bad);self.assertEqual(len(s),1)
        a.u,a.v=500,1000;a.grain=True;self.assertEqual(cut_plan(p)[1],[a.id])
        a.grain=False;s,bad=self.assert_layout(p);self.assertFalse(bad);self.assertTrue(s[0]['pieces'][0]['rotated'])

    def test_save_load_exports(self):
        p=Project(name='<Projekt & klient>');add_template(p,'cabinet');p.parts[0].holes=[[100,100,5,10]]
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);p.save(d/'p.mebel');q=Project.load(d/'p.mebel');self.assertEqual(p.data(),q.data())
            csv_export(q,d/'p.csv');dxf_export(q,d/'p.dxf');report_export(q,d/'p.html')
            with (d/'p.csv').open(encoding='utf-8-sig') as f:rows=list(csv.reader(f,delimiter=';'))
            self.assertEqual(len(rows),len(p.parts)+1)
            text=(d/'p.html').read_text();self.assertIn('&lt;Projekt &amp; klient&gt;',text);self.assertNotIn('<Projekt & klient>',text)
            self.assertIn('CIRCLE',(d/'p.dxf').read_text());self.assertIn('$INSUNITS',(d/'p.dxf').read_text())

    def test_invalid_inputs(self):
        p=Project();part=self.part(p)
        for key,value in [('u',-10),('t',float('nan')),('rx',float('inf'))]:
            q=copy.deepcopy(p);setattr(q.parts[0],key,value)
            with self.assertRaises(ValueError):q.validate()
        part.holes=[[10,10,8,100]]
        with self.assertRaises(ValueError):p.validate()
        q=Project();part=self.part(q,u=1,edges=[True]*4);q.edge_t=2
        with self.assertRaises(ValueError):q.validate()

    def test_camera_fit_and_render(self):
        p=Project();add_template(p,'wardrobe',1000,2200,600)
        camera=Camera();camera.fit(p,900,700)
        for part in p.parts:
            for face in faces(part):
                for v in face:
                    x,y,z=camera.project(v,900,700);self.assertTrue(45<=x<=855);self.assertTrue(45<=y<=655)
        self.assertEqual(len(render_faces(p,camera,900,700)),6*len(p.parts))
        self.assertIn('<svg',svg_view(p,camera))

if __name__=='__main__':unittest.main(verbosity=2)
