"""Small local preset library; project data remains in .mebel files."""
import copy
import json
import os
from pathlib import Path


def preset_path():
    return Path(os.environ.get('LOCALAPPDATA',str(Path.home()))) / 'MebelStudio3D' / 'kreator_presety.json'


def load_presets(path=None):
    path=Path(path) if path else preset_path()
    if not path.exists():return {}
    if path.stat().st_size>2_000_000:raise ValueError('Biblioteka presetów jest zbyt duża.')
    data=json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data,dict) or data.get('schema')!=1 or not isinstance(data.get('presets'),dict):
        raise ValueError('Nieprawidłowa biblioteka presetów.')
    if len(data['presets'])>100 or any(not isinstance(name,str) or not name.strip() or len(name)>80 or not isinstance(config,dict) or not isinstance(config.get('_material_names',{}),dict) for name,config in data['presets'].items()):
        raise ValueError('Nieprawidłowy zapis konfiguracji w presetach.')
    return copy.deepcopy(data['presets'])


def save_preset(name,config,path=None):
    name=str(name).strip()
    if not name or len(name)>80:raise ValueError('Nazwa presetu: od 1 do 80 znaków.')
    path=Path(path) if path else preset_path()
    presets=load_presets(path);presets[name]=copy.deepcopy(config)
    if len(presets)>100:raise ValueError('Maksymalnie 100 własnych presetów.')
    payload=json.dumps({'schema':1,'presets':presets},ensure_ascii=False,indent=2)
    if len(payload.encode('utf-8'))>2_000_000:raise ValueError('Biblioteka presetów jest zbyt duża.')
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix('.tmp');temporary.write_text(payload,encoding='utf-8');temporary.replace(path)


def delete_preset(name,path=None):
    path=Path(path) if path else preset_path();presets=load_presets(path)
    if name not in presets:return False
    del presets[name]
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps({'schema':1,'presets':presets},ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(path)
    return True
