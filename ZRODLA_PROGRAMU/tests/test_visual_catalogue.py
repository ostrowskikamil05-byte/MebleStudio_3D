import copy
import math
import unittest
from types import SimpleNamespace
from core import Project
from furniture_configurator import default_config,configure,read_config
from visual_catalogue import KINDS,catalogue_items,filter_items,dimension_text,apply_item,canonical_kind
from catalogue_previews import preview_faces,_faces


class CatalogueDataTests(unittest.TestCase):
    def test_all_52_types_keep_indices_and_all_categories_have_native_cached_geometry(self):
        p=Project();before=p.data();items=catalogue_items('furniture',p)
        self.assertEqual([i['payload']['template_index'] for i in items],list(range(52)))
        self.assertEqual(len({i['id'] for i in items}),52)
        for kind in KINDS:
            cards=catalogue_items(kind,p);self.assertTrue(cards,kind)
            for card in cards:
                faces=preview_faces(card);self.assertTrue(faces,card['id']);self.assertIs(faces,preview_faces(card));self.assertTrue(dimension_text(card))
                self.assertTrue(all(math.isfinite(value) for face,color in faces for vertex in face for value in vertex))
                self.assertTrue(all(len(face)>=3 and color.startswith('#') for face,color in faces))
        self.assertEqual(p.data(),before)

    def test_search_handles_polish_accents_manufacturers_and_category_intersection(self):
        p=Project();furniture=catalogue_items('furniture',p);self.assertTrue(filter_items(furniture,'lazienka'));self.assertTrue(filter_items(furniture,'slupek spiZarniany'))
        self.assertEqual(len(filter_items(furniture,'','Kuchnia')),sum(i['category']=='Kuchnia' for i in furniture))
        self.assertFalse(filter_items(furniture,'szafa','Ogród'));self.assertTrue(filter_items(catalogue_items('runner',p),'hettich'))
        self.assertEqual(canonical_kind('handles'),'handle')

    def test_runner_dimensions_are_sourced_and_undermount_inner_offset_is_not_external_gap(self):
        from runners_catalog import PROFILES
        cards=catalogue_items('runner',Project());self.assertEqual(len(cards),len(PROFILES))
        for card in cards:
            profile=PROFILES[card['payload']['profile_id']];self.assertEqual(card['source'],profile['source']);self.assertIn(card['payload']['length'],profile['lengths'])
            self.assertEqual(card['dimensions']['clearance'],profile.get('external_clearance'))
            self.assertEqual(card['dimensions']['inner_width_offset'],profile.get('inner_width_offset'))
            self.assertIn('bez kompletnych wzorców CNC',card['note'])

    def test_appliance_preview_matches_actual_room_mesh(self):
        import json
        from room_objects import _geometry
        for card in catalogue_items('appliance',Project()):
            key=card['preview']|dict(interactive=True);expected=_geometry(json.dumps(key,sort_keys=True))
            self.assertEqual(preview_faces(card),tuple((face,color) for face,color,emissive,glass in expected))


class CatalogueActionTests(unittest.TestCase):
    def setUp(self):
        self.app=SimpleNamespace(project=Project(),project_generation=0,selected=None,selected_scene=None,snapshots=0,refreshes=0,fit_calls=0)
        def snapshot():self.app.snapshots+=1
        def refresh():self.app.refreshes+=1
        def fit():self.app.fit_calls+=1
        self.app.snapshot=snapshot;self.app.refresh=refresh;self.app.fit=fit;self.app.selected_part=lambda:next((p for p in self.app.project.parts if p.id==self.app.selected),None)
        self.app.flush_parameter_drafts=lambda:True

    def cabinet(self):
        self.app.project,g=configure(self.app.project,default_config(0,self.app.project));self.app.selected=next(p.id for p in self.app.project.parts if p.group==g);return g

    def test_furniture_card_creates_real_unique_module_with_one_snapshot(self):
        self.cabinet();before=self.app.project.data();item=catalogue_items('furniture',self.app.project)[34]
        self.assertTrue(apply_item(self.app,item));self.assertEqual(self.app.snapshots,1);self.assertEqual(self.app.fit_calls,1)
        self.assertEqual(len({p.group for p in self.app.project.parts}),2);new=self.app.selected_part();self.assertEqual(read_config(self.app.project,new.group)['template_index'],34)
        self.assertNotEqual(self.app.project.data(),before)

    def test_hardware_cards_launch_target_tool_with_exact_payload_without_committing(self):
        calls=[];self.app.hardware_drag=SimpleNamespace(begin=lambda kind,options:calls.append((kind,options)))
        before=self.app.project.data()
        for kind in ('hinge','runner','led','handle'):
            item=catalogue_items(kind,self.app.project)[0];self.assertTrue(apply_item(self.app,item));self.assertEqual(calls[-1],(kind,item['payload']))
        self.assertEqual(self.app.project.data(),before);self.assertEqual(self.app.snapshots,0)
        self.app.flush_parameter_drafts=lambda:False;self.assertFalse(apply_item(self.app,catalogue_items('hinge',self.app.project)[0]));self.assertEqual(len(calls),4)

    def test_legs_rebuild_purchase_geometry_and_preserve_body_height(self):
        group=self.cabinet();before=read_config(self.app.project,group);item=next(i for i in catalogue_items('leg',self.app.project) if i['payload']['leg_height']==150 and i['payload']['leg_count']==6)
        self.assertTrue(apply_item(self.app,item));after=read_config(self.app.project,group)
        self.assertEqual(after['height']-after['leg_height'],before['height']-before['leg_height']);self.assertEqual(after['leg_height'],150)
        self.assertEqual(next(h['qty'] for h in self.app.project.hardware if h.get('kind')=='leg'),6);self.assertEqual(self.app.snapshots,1)

    def test_material_palette_adds_once_and_applies_to_selected_part_atomically(self):
        group=self.cabinet();item=next(i for i in catalogue_items('material',self.app.project) if 'color' in i['payload']);before=len(self.app.project.materials)
        self.assertTrue(apply_item(self.app,item));self.assertEqual(len(self.app.project.materials),before+1);material=self.app.selected_part().material
        self.assertEqual(next(m.color for m in self.app.project.materials if m.id==material),item['payload']['color'])
        self.assertTrue(apply_item(self.app,item));self.assertEqual(len(self.app.project.materials),before+1);self.assertEqual(self.app.snapshots,1)
        from direct_edit import resize_furniture
        owner=self.app.selected;cfg=read_config(self.app.project,group);self.app.project=resize_furniture(self.app.project,group,cfg['width']+100,cfg['height']-cfg['leg_height'],cfg['depth'])
        self.assertEqual(next(p.material for p in self.app.project.parts if p.id==owner),material)

    def test_armed_hardware_returns_to_main_view_without_closing_catalogue(self):
        calls=[];self.app.hardware_drag=SimpleNamespace(begin=lambda kind,options:calls.append(kind));self.app.design_page='design';self.app.workspace=SimpleNamespace(select=lambda page:calls.append(page));self.app.canvas=SimpleNamespace(focus_set=lambda:calls.append('focus'))
        self.assertTrue(apply_item(self.app,catalogue_items('hinge',self.app.project)[0]));self.assertEqual(calls,['hinge','design','focus'])

    def test_appliance_is_purchased_scene_object_and_does_not_add_manufactured_panels(self):
        group=self.cabinet();before=len(self.app.project.parts);item=catalogue_items('appliance',self.app.project)[0]
        self.assertTrue(apply_item(self.app,item));self.assertEqual(len(self.app.project.parts),before);self.assertEqual(self.app.project.scene['objects'][-1]['type'],item['payload']['object_type']);self.assertTrue(self.app.selected_scene);self.assertIsNone(self.app.selected)

    def test_unknown_or_unselected_target_fails_without_undo_or_mutation(self):
        item=catalogue_items('leg',self.app.project)[0];before=self.app.project.data()
        with self.assertRaises(ValueError):apply_item(self.app,item)
        self.assertEqual(self.app.project.data(),before);self.assertEqual(self.app.snapshots,0)


if __name__=='__main__':unittest.main()
