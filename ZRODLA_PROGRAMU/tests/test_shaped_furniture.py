import copy
import math
import tempfile
import unittest
from pathlib import Path
from core import Part,Project,polygon_area,validate_polygon,dxf_export,world
from furniture_library import create_furniture
from dimension_model import resize_dimensions
from shaped_furniture import contour,apply_shape,create_shaped_furniture,TEMPLATES
from shaped_furniture import profile_area,profile_perimeter,refresh_decor,styled_bounds


class ProfileTests(unittest.TestCase):
    def test_profile_parameters_survive_save_and_fixed_radius_resize(self):
        from direct_edit import resize_part
        p=Project();q=Part(u=600,v=300,t=18,material=p.materials[0].id);p.parts=[q]
        p=apply_shape(p,q.id,'rounded',radii=(0,25,70,45),segments=16)
        p=Project.from_data(p.data())
        p=resize_dimensions(p,q.id,800,400,22)
        self.assertEqual(p.parts[0].profile['radii'],[0.,25.,70.,45.])
        self.assertEqual(p.parts[0].profile['segments'],16)
        self.assertIn((775,0),p.parts[0].contour)
        p=resize_part(p,q.id,'u1',100)
        self.assertIn((875,0),p.parts[0].contour)
        self.assertEqual(p.parts[0].profile['radii'],[0.,25.,70.,45.])

    def test_exact_cad_profiles_and_quantities(self):
        from cad_geometry import local_shape
        for kind,u,v in [('ellipse',200,600),('circle',400,400),('capsule',500,250),
                         ('rounded',600,300),('arch',500,800),('half_ellipse',500,800),('taper',80,700)]:
            with self.subTest(kind=kind):
                p=Project();q=Part(u=u,v=v,t=18,material=p.materials[0].id);p.parts=[q]
                p=apply_shape(p,q.id,kind,segments=4);q=p.parts[0]
                shape=local_shape(q)
                self.assertTrue(shape.isValid())
                self.assertAlmostEqual(shape.Volume()/q.t,profile_area(q.profile,u,v),places=6)
                self.assertAlmostEqual(q.area(),shape.Volume()/q.t/1e6,places=10)
                self.assertAlmostEqual(shape.BoundingBox().xlen,u,places=6)
                self.assertAlmostEqual(shape.BoundingBox().ylen,v,places=6)
                if kind=='circle':self.assertIn('CIRCLE',{e.geomType() for e in shape.Edges()})
                if kind in ('ellipse','half_ellipse'):self.assertIn('ELLIPSE',{e.geomType() for e in shape.Edges()})
                # Resolution changes drawing only; production volume is invariant.
                p2=apply_shape(p,q.id,kind,segments=24)
                self.assertAlmostEqual(local_shape(p2.parts[0]).Volume(),shape.Volume(),places=5)

    def test_exact_curve_rims_fillet_and_chamfer(self):
        from cad_geometry import local_shape,edge_specs,edge_path
        for kind,u,v in [('circle',400,400),('ellipse',600,350),('rounded',600,350),
                         ('capsule',600,350),('arch',600,800),('half_ellipse',600,350)]:
            p=Project();q=Part(u=u,v=v,t=28,material=p.materials[0].id);p.parts=[q]
            p=apply_shape(p,q.id,kind);q=p.parts[0]
            specs=edge_specs(q.points(),q.t,q.profile,(q.u,q.v))
            if kind in ('circle','ellipse'):
                self.assertEqual([s[0] for s in specs],['bottom:0','top:0'])
                self.assertGreater(len(edge_path(specs[0])),48)
            if kind in ('rounded','capsule'):
                self.assertFalse(any(s[0].startswith('corner') for s in specs))
            nominal=local_shape(q).Volume()
            for spec in [s for s in specs if s[0].startswith('top') and getattr(s[2],'geom_type',None)!='LINE']:
                for feature,parameter in [('fillets','radius'),('chamfers','distance')]:
                    with self.subTest(kind=kind,edge=spec[0],feature=feature):
                        r=copy.deepcopy(q);setattr(r,feature,[dict(edge=spec[0],**{parameter:2})])
                        r.validate();shape=local_shape(r)
                        self.assertTrue(shape.isValid());self.assertLess(shape.Volume(),nominal)
                        p.parts=[r];loaded=Project.from_data(p.data())
                        self.assertAlmostEqual(local_shape(loaded.parts[0]).Volume(),shape.Volume(),places=5)

    def test_tangent_chain_values_checked_and_duplicate_additions_collapsed(self):
        from cad_geometry import local_shape,edge_specs
        p=Project();q=Part(u=600,v=350,t=28,material=p.materials[0].id);p.parts=[q]
        p=apply_shape(p,q.id,'rounded');q=p.parts[0]
        specs=edge_specs(q.points(),q.t,q.profile,(q.u,q.v));top=[s for s in specs if s[0].startswith('top')]
        one=copy.deepcopy(q);one.fillets=[dict(edge=top[0][0],radius=2)];one.validate()
        all_edges=copy.deepcopy(q);all_edges.fillets=[dict(edge=s[0],radius=2) for s in top];all_edges.validate()
        self.assertAlmostEqual(local_shape(one).Volume(),local_shape(all_edges).Volume(),places=5)
        q.fillets=[dict(edge=top[0][0],radius=2),dict(edge=top[1][0],radius=3)]
        with self.assertRaisesRegex(ValueError,'stycznie'):q.validate()
        q.fillets=[dict(edge=top[0][0],radius=2)];q.chamfers=[dict(edge=top[1][0],distance=2)]
        with self.assertRaisesRegex(ValueError,'stycznie'):q.validate()

    def test_edge_identity_ignores_drawing_resolution_and_rims_can_differ(self):
        from cad_geometry import local_shape,edge_specs
        p=Project();q=Part(u=600,v=350,t=28,material=p.materials[0].id);p.parts=[q]
        a=apply_shape(p,q.id,'rounded',segments=4).parts[0]
        b=apply_shape(p,q.id,'rounded',segments=24).parts[0]
        sa=edge_specs(a.points(),a.t,a.profile,(a.u,a.v));sb=edge_specs(b.points(),b.t,b.profile,(b.u,b.v))
        self.assertEqual([(s[0],s[1]) for s in sa],[(s[0],s[1]) for s in sb])
        self.assertEqual([tuple(s[2]) for s in sa],[tuple(s[2]) for s in sb])
        a.fillets=[dict(edge='top:0',radius=2),dict(edge='bottom:0',radius=4)]
        a.validate();self.assertTrue(local_shape(a).isValid())

    def test_exact_profile_excludes_fake_vertical_seams(self):
        p=Project();q=Part(u=400,v=400,t=28,material=p.materials[0].id);p.parts=[q]
        q=apply_shape(p,q.id,'circle').parts[0]
        q.fillets=[dict(edge='corner:0',radius=2)]
        with self.assertRaisesRegex(ValueError,'nieistniejącą'):q.validate()

    def test_full_contour_edge_cost_and_legacy_defaults(self):
        p=Project();q=Part(u=400,v=400,t=18,material=p.materials[0].id);p.parts=[q]
        p=apply_shape(p,q.id,'circle');q=p.parts[0];q.contour_edge=True
        self.assertAlmostEqual(q.edge_m(),math.pi*.4,places=9)
        self.assertAlmostEqual(p.costs()['edges'],math.pi*.4*p.edge_price,places=8)
        data=p.data()
        for key in ('profile','contour_edge','decor','style_source'):data['parts'][0].pop(key)
        loaded=Project.from_data(data)
        self.assertEqual(loaded.parts[0].profile,{})
        self.assertEqual(loaded.parts[0].edge_m(),0)
        loaded.parts[0].contour_edge=True
        pts=loaded.parts[0].points()
        self.assertAlmostEqual(loaded.parts[0].edge_m(),sum(math.dist(a,b) for a,b in zip(pts,pts[1:]+pts[:1]))/1000)

    def test_fluting_recomputes_and_keeps_manual_cuts(self):
        p=Project();q=Part(u=400,v=600,t=18,material=p.materials[0].id);p.parts=[q]
        q.machining=[dict(type='pocket',shape='circle',face='top',u=200,v=300,length=10,width=10,depth=2,angle=0)]
        q.decor=dict(type='fluting',width=8,pitch=32,depth=2,margin_u=30,margin_v=20,face='bottom')
        p.validate();old_count=len(q.machining)
        p=resize_dimensions(p,q.id,600,800,18);q=p.parts[0]
        self.assertGreater(len(q.machining),old_count)
        flutes=[c for c in q.machining if c.get('generated_decor')]
        self.assertTrue(all(c['width']==760 and c['v']==400 for c in flutes))
        self.assertEqual(len([c for c in q.machining if c['shape']=='circle']),1)
        self.assertEqual(Project.from_data(p.data()).data(),p.data())

    def test_curves_are_normalized_and_finite(self):
        for kind in ('ellipse','circle','rounded','capsule','arch','half_ellipse','taper'):
            for n in (4,12,24):
                with self.subTest(kind=kind,segments=n):
                    pts=contour(kind,400,400,segments=n)
                    validate_polygon(pts)
                    self.assertEqual(min(x for x,y in pts),0)
                    self.assertEqual(min(y for x,y in pts),0)
                    self.assertEqual(max(x for x,y in pts),400)
                    self.assertEqual(max(y for x,y in pts),400)
                    self.assertLessEqual(len(pts),128)

    def test_independent_radii_and_zero_corner(self):
        pts=contour('rounded',600,300,radii=(0,25,70,45))
        self.assertIn((0,0),pts)
        self.assertNotIn((600,0),pts)
        self.assertIn((575,0),pts)
        self.assertIn((600,230),pts)
        self.assertIn((45,300),pts)

    def test_ellipse_tessellation_error(self):
        pts=contour('ellipse',800,400,segments=24)
        exact=math.pi*400*200
        self.assertLess(abs(polygon_area(pts)-exact)/exact,.001)

    def test_capsule_and_arch_tangencies(self):
        validate_polygon(contour('capsule',800,400,segments=24))
        pts=contour('arch',600,1000,arch_rise=300)
        self.assertIn((600,700),pts)
        self.assertIn((0,700),pts)
        self.assertIn((300,1000),pts)
        self.assertEqual(contour('rectangle',100,100),[])

    def test_impossible_profile_inputs(self):
        for args,kwargs in [(('circle',100,120),{}),(('rounded',100,100),{'radii':(60,60,0,0)}),
                            (('ellipse',100,100),{'segments':4.5}),
                            (('arch',100,100),{'arch_rise':101}),
                            (('taper',100,100),{'taper':50})]:
            with self.subTest(args=args,kwargs=kwargs),self.assertRaises(ValueError):contour(*args,**kwargs)

    def test_profile_preserves_linked_hardware(self):
        p,g=create_furniture(Project(),0)
        front=next(q for q in p.parts if q.role=='door')
        original=copy.deepcopy(p.data());count=len(front.operations)
        result=apply_shape(p,front.id,'rounded',radii=(12,12,20,20))
        q=next(q for q in result.parts if q.id==front.id)
        self.assertTrue(q.contour);self.assertEqual(len(q.operations),count)
        self.assertEqual(len(result.hardware),len(p.hardware));self.assertEqual(p.data(),original)
        self.assertTrue(any(h.get('owner_id')==q.id and h.get('kind')=='handle' for h in result.hardware))

    def test_profile_rejects_displaced_hole_atomically(self):
        p=Project();q=Part(u=100,v=100,t=18,material=p.materials[0].id,
            holes=[[10,10,5,10]]);p.parts.append(q);p.validate();original=p.data()
        with self.assertRaises(ValueError):apply_shape(p,q.id,'ellipse')
        self.assertEqual(p.data(),original)

    def test_profile_rejects_existing_edge_features(self):
        p=Project();q=Part(u=100,v=100,t=18,material=p.materials[0].id,
            fillets=[dict(edge='corner:0',radius=5)]);p.parts.append(q)
        with self.assertRaisesRegex(ValueError,'numeracja'):apply_shape(p,q.id,'ellipse')

    def test_profile_dimensions_roundtrip_and_export(self):
        p=Project();q=Part(u=100,v=100,t=18,material=p.materials[0].id);p.parts.append(q)
        p=apply_shape(p,q.id,'ellipse',240,100,22,segments=16)
        p=resize_dimensions(p,q.id,300,150,22);p.validate()
        r=Project.from_data(p.data());self.assertEqual(r.data(),p.data())
        q=r.parts[0];self.assertEqual((q.u,q.v,q.t),(300,150,22))
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'profile.dxf';dxf_export(r,path)
            self.assertIn('LWPOLYLINE',path.read_text())


class ShapedFurnitureTests(unittest.TestCase):
    def test_resize_templates_updates_supports_and_rotated_rails(self):
        from direct_edit import resize_furniture
        for i,item in enumerate(TEMPLATES):
            with self.subTest(template=item[1]):
                p,g=create_shaped_furniture(Project(),i,origin=(100,30,10))
                source=p.parts[0].style_source;old_ids={q.id for q in p.parts}
                w,h,d=source['width']*1.1,source['height']*1.1,source['depth']*1.1
                resized=resize_furniture(p,g,w,h,d)
                resized.validate()
                self.assertEqual({q.id for q in resized.parts},old_ids)
                self.assertEqual(styled_bounds(resized,g)['upper'],(100+w,30+d,10+h))
                self.assertEqual(Project.from_data(resized.data()).data(),resized.data())
                if item[5] in ('rounded_table','stool'):
                    legs=[q for q in resized.parts if q.name.startswith('Noga')]
                    self.assertTrue(all(abs(q.t-(h-source['thickness']))<1e-7 for q in legs))
                if item[5]=='stool':self.assertTrue(any(abs(q.rz)>1 for q in resized.parts))

    def test_resize_preserves_materials_handles_and_manual_operations(self):
        from direct_edit import resize_furniture
        p,g=create_shaped_furniture(Project(),7)
        door=next(q for q in p.parts if q.role=='door');door.material=p.materials[1].id
        count=len(p.hardware);ids={q.id for q in p.parts}
        p=resize_furniture(p,g,1400,900,480)
        self.assertEqual(len(p.hardware),count);self.assertEqual({q.id for q in p.parts},ids)
        q=next(q for q in p.parts if q.id==door.id)
        self.assertEqual(q.material,door.material);self.assertTrue(q.decor)
        self.assertTrue(any(h.get('kind')=='handle' and h.get('owner_id')==q.id for h in p.hardware))
        p=resize_furniture(p,g,1500,950,500)
        self.assertEqual(len(p.hardware),count)

    def test_resize_edited_geometry_rejected_atomically(self):
        from direct_edit import resize_furniture
        p,g=create_shaped_furniture(Project(),0);p.parts[1].x+=1
        before=p.data()
        with self.assertRaisesRegex(ValueError,'ręcznie'):resize_furniture(p,g,1800,800,900)
        self.assertEqual(p.data(),before)

    def test_resize_minimum_face_updates_origin_for_next_resize(self):
        from direct_edit import resize_furniture_face
        p,g=create_shaped_furniture(Project(),0,origin=(100,30,10))
        p=resize_furniture_face(p,g,'x0',-100)
        self.assertEqual(styled_bounds(p,g)['lower'],(0,30,10))
        p=resize_furniture_face(p,g,'z1',50)
        self.assertEqual(styled_bounds(p,g)['upper'][2],820)

    def test_every_template_closed_dimensions_and_ids(self):
        for i,item in enumerate(TEMPLATES):
            with self.subTest(template=item[1]):
                p,g=create_shaped_furniture(Project(),i)
                p.validate();self.assertTrue(p.parts)
                self.assertTrue(all(q.group==g for q in p.parts))
                self.assertEqual(len({q.id for q in p.parts}),len(p.parts))
                self.assertTrue(any(q.contour for q in p.parts))
                self.assertEqual(Project.from_data(p.data()).data(),p.data())

    def test_origin_and_scaling(self):
        a,g=create_shaped_furniture(Project(),0,width=1800,height=800,depth=900,thickness=40)
        b,g=create_shaped_furniture(Project(),0,width=1800,height=800,depth=900,thickness=40,origin=(1200,30,80))
        for p,q in zip(a.parts,b.parts):
            self.assertEqual((q.x-p.x,q.y-p.y,q.z-p.z),(1200,30,80))
        top=a.parts[0];self.assertEqual((top.u,top.v,top.t,top.z),(1800,900,40,760))

    def test_no_input_mutation_and_unique_groups(self):
        p=Project();original=p.data();a,g=create_shaped_furniture(p,4)
        b,h=create_shaped_furniture(a,4)
        self.assertEqual(p.data(),original);self.assertNotEqual(g,h)
        self.assertEqual(len(b.parts),2*len(a.parts))

    def test_ryflowanie_is_real_and_moves_with_front(self):
        from mechanisms import posed_project
        p,g=create_shaped_furniture(Project(),7)
        door=next(q for q in p.parts if q.role=='door');self.assertTrue(door.machining)
        self.assertTrue(all(c['type']=='groove' and c['face']=='bottom' for c in door.machining))
        posed=posed_project(p,{door.id:1});moved=next(q for q in posed.parts if q.id==door.id)
        self.assertNotEqual(moved.rz,door.rz);self.assertEqual(moved.machining,door.machining)

    def test_stool_and_invalid_dimensions(self):
        p,g=create_shaped_furniture(Project(),4)
        self.assertEqual(len([q for q in p.parts if q.name.startswith('Noga')]),3)
        with self.assertRaises(ValueError):create_shaped_furniture(Project(),4,width=400,depth=420)
        with self.assertRaises(ValueError):create_shaped_furniture(Project(),0,height=20,thickness=30)


if __name__=='__main__':unittest.main()
