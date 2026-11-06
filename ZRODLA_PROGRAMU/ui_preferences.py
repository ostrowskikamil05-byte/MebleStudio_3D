"""Validated local interface preferences; never stored in a furniture project."""
import copy
import json
import math
import os
from pathlib import Path
import tempfile

DEFAULTS = dict(schema=1, window_mode='auto', window_width=1366, window_height=768,
                window_x=None, window_y=None, ui_scale=1.0, inspector_width=330,
                ribbon_height=110, show_inspector=True, show_ribbon=True,
                show_toolbars=True, engine='auto', quality='balanced')

# Keep the original values valid so existing local preferences migrate intact.
ENGINES = ('auto', 'cpu', 'gpu', 'd3d11', 'd3d12', 'vulkan')
ENGINE_LABELS = {
    'Automatyczny dobór': 'auto',
    'DirectX 11 · silnik C++': 'd3d11',
    'DirectX 12 · silnik C++': 'd3d12',
    'Vulkan · silnik C++': 'vulkan',
    'OpenGL · GPU i wysoka jakość': 'gpu',
    'CPU': 'cpu',
}


def preference_path():
    return Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'MebelStudio3D' / 'ui_preferences.json'


def validate_preferences(data):
    if not isinstance(data, dict) or data.get('schema', 1) != 1:
        raise ValueError('Nieobsługiwana wersja ustawień interfejsu.')
    result = copy.deepcopy(DEFAULTS)
    for key, choices in [('window_mode', ('auto', 'custom')), ('engine', ENGINES),
                         ('quality', ('draft', 'balanced', 'high'))]:
        if key in data:
            if data[key] not in choices: raise ValueError('Nieprawidłowe ustawienie: ' + key)
            result[key] = data[key]
    for key, low, high in [('window_width', 900, 7680), ('window_height', 600, 4320),
                           ('inspector_width', 240, 1200), ('ribbon_height', 64, 500),
                           ('ui_scale', .75, 2.0)]:
        if key not in data: continue
        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError('Nieprawidłowy zakres ustawienia: ' + key)
        if key != 'ui_scale' and int(value) != value: raise ValueError('Wpisz całkowity rozmiar: ' + key)
        result[key] = float(value) if key == 'ui_scale' else int(value)
    for key in ('show_inspector', 'show_ribbon', 'show_toolbars'):
        if key in data:
            if not isinstance(data[key], bool): raise ValueError('Nieprawidłowe ustawienie: ' + key)
            result[key] = data[key]
    for key in ('window_x', 'window_y'):
        if key in data:
            value = data[key]
            if value is not None and (isinstance(value, bool) or not isinstance(value, int) or abs(value) > 100_000):
                raise ValueError('Nieprawidłowa pozycja okna.')
            result[key] = value
    return result


def load_preferences(path=None):
    path = Path(path) if path is not None else preference_path()
    try:
        if path.stat().st_size > 65_536: return copy.deepcopy(DEFAULTS)
        return validate_preferences(json.loads(path.read_text(encoding='utf-8-sig')))
    except (OSError, ValueError, TypeError):
        return copy.deepcopy(DEFAULTS)


def save_preferences(preferences, path=None):
    data = validate_preferences(preferences)
    path = Path(path) if path is not None else preference_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix=path.name + '.', suffix='.tmp', delete=False) as output:
            temporary = Path(output.name)
            json.dump(data, output, ensure_ascii=False, indent=2, allow_nan=False)
            output.flush(); os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists(): temporary.unlink()
    return data
