"""Near cabinet fronts must cover rear boards in graphical cards."""
import unittest
from unittest.mock import patch
from catalogue_previews import draw_preview


class CardOcclusionTests(unittest.TestCase):
    def test_near_surface_is_painted_after_overlapping_rear_surface(self):
        colors=[]
        class Canvas:
            def delete(self,*args):pass
            def create_polygon(self,*args,**kwargs):colors.append(kwargs['fill'])
        rear=((0,0,0),(100,0,0),(0,100,0))
        near=((0,0,20),(100,0,20),(0,100,20))
        with patch('catalogue_previews.preview_faces',return_value=((near,'#ff3333'),(rear,'#3333ff'))):
            draw_preview(Canvas(),dict(kind='furniture',id='test'),200,125)
        self.assertEqual(colors,['#3333ff','#ff3333'])


if __name__=='__main__':unittest.main()
