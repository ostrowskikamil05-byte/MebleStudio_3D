import unittest
from core import Project
from furniture_configurator import configure,default_config
from drag_hardware import attach,refresh_manual_attachments
from configurator_accessories import refresh_manual_led
from productivity import extract_group,insert_template,independent_copy


class AttachmentCopyTests(unittest.TestCase):
    def test_group_copy_led_has_independent_purchase_and_cut_ids_after_refresh(self):
        p=Project();p,g=configure(p,default_config(0,p));side=next(p for p in p.parts if p.role=='left')
        p=attach(p,'led',side.id,options=dict(face='top',groove=True,price_per_m=19))
        old=p.data();q=insert_template(p,extract_group(p,g),'Kopia',(700,0,0))
        strips=[h for h in q.hardware if h.get('kind')=='led_strip'];self.assertEqual(len(strips),2)
        self.assertEqual(len({h['attachment_id'] for h in strips}),2)
        refresh_manual_led(q);q.validate()
        strips=[h for h in q.hardware if h.get('kind')=='led_strip'];self.assertEqual(len(strips),2)
        for h in strips:
            owner=next(p for p in q.parts if p.id==h['owner_id'])
            cuts=[cut for cut in owner.machining if cut.get('attachment_id')==h['attachment_id']]
            self.assertEqual(len(cuts),1)
            rows=[item for item in q.hardware if item.get('attachment_id')==h['attachment_id']]
            self.assertEqual(len(rows),3);self.assertTrue(all(row['owner_id']==owner.id for row in rows));self.assertEqual(h['price'],19)
        self.assertEqual(p.data(),old)

    def test_independent_part_copy_keeps_real_groove_but_drops_shared_attachment(self):
        p=Project();p,g=configure(p,default_config(0,p));side=next(p for p in p.parts if p.role=='left')
        p=attach(p,'led',side.id,options=dict(face='top',groove=True));side=next(x for x in p.parts if x.id==side.id)
        copied=independent_copy(side);self.assertEqual(len(copied.machining),1)
        self.assertNotIn('attachment_id',copied.machining[0]);copied.validate()
        p.parts.append(copied);refresh_manual_led(p);self.assertEqual(len(copied.machining),1)

    def test_hinge_descriptor_and_drill_ids_share_new_attachment_map(self):
        p=Project();p,g=configure(p,default_config(0,p));door=next(x for x in p.parts if x.role=='door')
        p=attach(p,'hinge',door.id,options=dict(count=3,price=8));original=next(h for h in p.hardware if h.get('attachment_kind')=='hinge')
        original['attachment_options']['descriptor']={'attachment_id':original['attachment_id']}
        q=insert_template(p,extract_group(p,g),'Kopia',(700,0,0));copied=next(h for h in q.hardware if h.get('attachment_kind')=='hinge' and h['owner_id']!=door.id)
        self.assertNotEqual(original['attachment_id'],copied['attachment_id'])
        self.assertEqual(copied['attachment_options']['descriptor']['attachment_id'],copied['attachment_id'])
        operations=[op for part in q.parts for op in part.operations if op.get('owner_id')==copied['owner_id'] and op.get('purpose')=='puszka zawiasu']
        self.assertEqual(len(operations),3);self.assertTrue(all(op['attachment_id']==copied['attachment_id'] for op in operations))
        refresh_manual_attachments(q);q.validate()
        self.assertEqual(len([h for h in q.hardware if h.get('attachment_kind')=='hinge']),2)


if __name__=='__main__':unittest.main()
