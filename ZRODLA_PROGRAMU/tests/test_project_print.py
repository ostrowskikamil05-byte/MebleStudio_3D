import copy
from html.parser import HTMLParser
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from core import Part, Project
from project_print import export_project_print


class BookParser(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.sections, self.callouts, self.external_assets, self.text = [], [], [], []
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag=='section':
            self.sections.append(attrs)
        if tag=='g' and attrs.get('class')=='part-callout':
            self.callouts.append(attrs)
        if tag in ('img','script','link') and ('src' in attrs or 'href' in attrs):
            self.external_assets.append(attrs)

    def handle_data(self, data):
        self.text.append(data)


class ProjectPrintTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.project = Project(name='Komplet kuchenny')
        mat = self.project.materials[0].id
        self.left = Part(name='Bok',group='Szafka A',u=450,v=720,t=18,plane='YZ',material=mat,edges=[False,False,True,False])
        self.top = Part(name='Blat',group='Szafka A',u=600,v=450,t=18,plane='XY',z=702,material=mat,edges=[True,True,True,True])
        self.other = Part(name='Druga półka',group='Szafka B',u=320,v=280,t=16,plane='XY',x=750,material=mat)
        self.project.parts = [self.left,self.top,self.other]

    def tearDown(self):
        self.temp.cleanup()

    def export(self, name='book.html', **kwargs):
        path = Path(self.temp.name)/name
        info = export_project_print(self.project,path,**kwargs)
        return info,path.read_text(encoding='utf-8')

    def test_complete_book_and_source_immutability(self):
        before = copy.deepcopy(self.project.data())
        self.other.visible = False
        before = copy.deepcopy(self.project.data())
        info,source = self.export()
        book = BookParser(source)
        self.assertEqual(self.project.data(),before)
        self.assertEqual(info['groups'],['Szafka A','Szafka B'])
        self.assertEqual(info['parts'],3)
        self.assertEqual(len([s for s in book.sections if s['data-kind']=='overview']),1)
        self.assertEqual(len([s for s in book.sections if s['data-kind']=='assembly']),2)
        self.assertEqual(len([s for s in book.sections if s['data-kind']=='part']),3)
        self.assertEqual(len([s for s in book.sections if s['data-kind']=='cut-plan']),info['sheets'])
        self.assertEqual(info['sections'],len(book.sections))
        self.assertEqual(book.external_assets,[])
        self.assertIn('@page{size:A4 portrait',source)
        self.assertIn('window.print()',source)
        self.assertIn('Cały zestaw — z góry',source)
        self.assertIn('Cały zestaw — z przodu',source)
        self.assertIn('Cały zestaw — 3D',source)
        self.assertEqual({c['data-part-id'] for c in book.callouts},{p.id for p in self.project.parts})

    def test_explosion_never_changes_physical_dimensions(self):
        first,a = self.export('a.html',explosion=0)
        second,b = self.export('b.html',explosion=1.25)
        aa,bb = BookParser(a),BookParser(b)
        physical = lambda book:[(r['data-part-id'],r['data-u'],r['data-v'],r['data-t']) for r in book.callouts]
        self.assertEqual(physical(aa),physical(bb))
        self.assertIn('600 × 450 × 720 mm (X × Y × Z)',a)
        self.assertIn('600 × 450 × 720 mm (X × Y × Z)',b)
        self.assertIn('450 × 720 × 18 mm',a)
        self.assertIn('450 × 720 × 18 mm',b)
        self.assertIn('data-explosion="1.25"',b)
        self.assertEqual(first['parts'],second['parts'])

    def test_cut_dimensions_and_stock_thickness_are_reported(self):
        info,source = self.export()
        self.assertIn('598 × 448',source)  # Four 1-mm edges on the top.
        self.assertIn('450 × 719',source)  # One V edge on side panel.
        self.assertIn('U0, U1, V0, V1',source)
        self.assertIn('grubość 18 mm',source)
        self.assertIn('grubość 16 mm',source)
        self.assertEqual(info['sheets'],2)
        self.assertEqual(info['unplaced'],[])

    def test_html_escape_and_unsafe_hardware_link(self):
        self.project.name = '<script>alert("bad")</script>'
        self.left.name = '<img src=x onerror="attack()">'
        self.left.group = 'A" data-evil="yes'
        self.project.materials[0].name = 'Dąb & <MDF>'
        self.project.hardware = [dict(name='<b>Zawias</b>',qty=2,price=10,owner_id=self.left.id,source='javascript:attack()')]
        _,source = self.export()
        parsed = BookParser(source)
        self.assertNotIn('<script>alert',source)
        self.assertNotIn('<img src=x',source)
        self.assertIn('&lt;script&gt;',source)
        self.assertIn('&lt;img src=x',source)
        self.assertIn('Dąb &amp; &lt;MDF&gt;',source)
        self.assertNotIn('href="javascript:',source)
        self.assertTrue(any(s.get('data-group')==self.left.group for s in parsed.sections))
        self.assertTrue(all('data-evil' not in s for s in parsed.sections))

    def test_single_group_excludes_other_parts_but_keeps_source_numbers(self):
        self.left.assembly = dict(anchors=[self.other.id])
        self.project.hardware = [dict(name='Zawias B',qty=2,price=1,owner_id=self.other.id)]
        before = copy.deepcopy(self.project.data())
        info,source = self.export(group='Szafka B',include_parts=False,include_cut_plan=False)
        parsed = BookParser(source)
        self.assertEqual(info['groups'],['Szafka B'])
        self.assertEqual(info['parts'],1)
        self.assertEqual(info['sheets'],0)
        self.assertEqual({c['data-part-id'] for c in parsed.callouts},{self.other.id})
        self.assertIn('#3 · 320 × 280 × 16 mm',source)
        self.assertNotIn(self.left.id,source)
        self.assertEqual(self.project.data(),before)
        self.assertFalse(any(s['data-kind']=='part' for s in parsed.sections))

    def test_group_pages_use_construction_axes_overview_keeps_layout(self):
        for p in (self.left,self.top):
            p.layout = dict(x=1000,y=50,z=0,yaw=90)
        self.project.parts = [self.left,self.top]
        _,source = self.export(include_parts=False,include_cut_plan=False)
        self.assertIn('Gabaryty zamkniętego zestawu: 450 × 600 × 720 mm',source)
        self.assertIn('Gabaryty zamkniętego mebla w jego osiach: 600 × 450 × 720 mm',source)
        self.assertEqual(self.left.layout['yaw'],90)

    def test_nominal_drawing_does_not_load_cad_mesh_or_finishing_kernel(self):
        self.top.fillets = [dict(edge='top:0',radius=3)]
        self.top.machining = [dict(type='pocket',shape='rect',face='top',u=300,v=225,length=60,width=20,depth=4,angle=0)]
        with patch('cad_geometry.mesh',side_effect=AssertionError('Mesh must not be loaded')),patch('cad_geometry.validate_fillets',side_effect=AssertionError('CAD validation must not be invoked')):
            _,source = self.export()
        self.assertIn('top:0: 3 mm',source)
        self.assertIn('pocket / rect / top',source)
        self.assertIn('Schemat brył konstrukcyjnych',source)

    def test_large_assembly_chunks_preserve_every_part_callout(self):
        mat = self.project.materials[0].id
        self.project.parts = [Part(name=f'Listwa {i}',group='Łóżko',u=800,v=70,t=18,plane='XY',y=80*i,material=mat) for i in range(23)]
        _,source = self.export(include_parts=False,include_cut_plan=False)
        parsed = BookParser(source)
        self.assertEqual(len([s for s in parsed.sections if s['data-kind']=='assembly']),2)
        self.assertEqual(len(parsed.callouts),23)
        self.assertEqual({c['data-part-id'] for c in parsed.callouts},{p.id for p in self.project.parts})

    def test_unplaced_parts_are_identified_and_empty_group_rejected(self):
        self.project.materials[0].sheet_u = 200
        self.project.materials[0].sheet_v = 200
        info,source = self.export(include_parts=False)
        self.assertEqual(set(info['unplaced']),{p.id for p in self.project.parts})
        self.assertIn('Formatki nieumieszczone w rozkroju',source)
        with self.assertRaises(ValueError):
            self.export(group='Nieistniejący mebel')
        with self.assertRaises(ValueError):
            self.export(explosion=float('nan'))


if __name__=='__main__':
    unittest.main()
