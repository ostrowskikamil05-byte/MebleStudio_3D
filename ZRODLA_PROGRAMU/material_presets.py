"""Documented product sizes, plus editable workshop thickness choices."""
import copy
from core import Material
from dimension_model import resize_dimensions

EGGER='https://support.egger.com/hc/en-us/articles/360001082138-What-are-the-dimensions-of-EGGER-Eurodekor-Faced-Chipboard'
PERFECTSENSE_PL='https://www.egger.com/pl/meble-i-aranzacja-wnetrz/kolekcja-materialow-dekoracyjnych/asortyment-perfectsense?country=PL'
PRESETS={
 'Eurodekor — płyta wiórowa (oferta UK)':dict(kind='plyta',sheet=(2800,2070),thicknesses=(8,12,15,16,18,19,22,25,28,30,38),stock_thicknesses=(18,),source=EGGER,color='#ceb28f',note='Karta EGGER z 21.02.2023, zakład Hexham / UK: 18 mm magazynowo, pozostałe grubości na zamówienie. Oferta i dekor w Polsce wymagają sprawdzenia.'),
 'PerfectSense Premium Matt PM — MDF (PL)':dict(kind='mdf',sheet=(2800,2070),thicknesses=(10,12,16,18,19,22,25),stock_thicknesses=(18,),source=PERFECTSENSE_PL,color='#ba9871',note='EGGER, oferta PL: MDF lakierowany Premium Matt E1E05 TSCA ST, PM/ST9. 18 mm magazynowo; 10, 12, 16, 19, 22, 25 mm jako warianty produkcyjne. Dostępność zależy od dekoru.'),
 'PerfectSense Premium Gloss PG — MDF (PL)':dict(kind='mdf',sheet=(2800,2070),thicknesses=(10,12,16,18,19,22,25),stock_thicknesses=(18,),source=PERFECTSENSE_PL,color='#d2d1ce',note='EGGER, oferta PL: MDF lakierowany Premium Gloss E1E05 TSCA ST, PG/ST9. 18 mm magazynowo; 10, 12, 16, 19, 22, 25 mm jako warianty produkcyjne. Dostępność zależy od dekoru.'),
 'Sklejka — ustawienie warsztatowe':dict(kind='sklejka',sheet=(2500,1250),thicknesses=(4,6,8,9,12,15,18,21,24,30),source='',color='#d4b58a'),
 'HDF — ustawienie warsztatowe':dict(kind='hdf',sheet=(2800,2070),thicknesses=(3,4,6),source='',color='#b69d80'),
 'Drewno lite — ustawienie warsztatowe':dict(kind='drewno',sheet=(2500,200),thicknesses=(18,20,22,25,28,30,40,45,50,60),source='',color='#b68a58')}

def apply_material_preset(project,part_id,name,thickness):
    spec=PRESETS[name]
    if thickness not in spec['thicknesses']:raise ValueError('Wybierz grubość z listy tego materiału.')
    p=next(p for p in project.parts if p.id==part_id)
    result=resize_dimensions(project,part_id,p.u,p.v,thickness);p=next(p for p in result.parts if p.id==part_id)
    material=next((m for m in result.materials if m.name==name),None)
    if material is None:
        material=Material(name=name,kind=spec['kind'],sheet_u=spec['sheet'][0],sheet_v=spec['sheet'][1],color=spec['color'],price_m2=0,price_m3=0)
        result.materials.append(material)
    p.material=material.id;result.validate();return result
