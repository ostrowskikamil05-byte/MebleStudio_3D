"""C++ owns committed editor documents and history; Python exposes editable drafts.

Every candidate still passes the complete existing Python/OpenCascade validation
before it crosses the transaction boundary. No history snapshots live in Python.
"""
from __future__ import annotations
import json
from pathlib import Path
import threading
import weakref


def _call(function, *args):
    import native_model
    return native_model.call('document', function, *args)


def serialize(project):
    import native_model
    return json.dumps(project.data(), ensure_ascii=False, allow_nan=False,
                      separators=(',', ':'), default=native_model._default)


def _release(handle):
    try:
        _call('destroy', handle)
    except Exception:
        # Interpreter shutdown must not mask the original process outcome.
        pass


class NativeDocument:
    """One native authoritative document with revision-checked atomic history."""
    def __init__(self, project, history_limit=60):
        import native_model
        info = native_model.call('native', 'info')
        if info.get('document_api_version') != 1:
            raise RuntimeError('Biblioteka C++ nie obsługuje dokumentu edytora. Użyj kompletnej nowej paczki programu.')
        self._lock = threading.RLock()
        self.history_limit = history_limit
        self.handle = 0
        project.validate()
        serialized = serialize(project)
        result = _call('create', serialized, history_limit)
        self._accept(result, serialized)
        self._saved = serialized
        self._finalizer = weakref.finalize(self, _release, self.handle)

    def _accept(self, result, serialized=None):
        self.handle = result['handle']
        self.revision = result['revision']
        self._info = result.copy()
        self._info.pop('serialized', None)
        if serialized is not None:
            self._serialized = serialized

    def _require_open(self):
        if not self.handle:
            raise RuntimeError('Dokument C++ został zamknięty.')

    @property
    def dirty(self):
        return self._serialized != self._saved

    def matches(self, project):
        return serialize(project) == self._serialized

    def info(self):
        with self._lock:
            self._require_open()
            result = _call('info', self.handle)
            result['dirty'] = self.dirty
            return result

    def read(self):
        with self._lock:
            self._require_open()
            result = _call('read', self.handle)
            self._accept(result, result['serialized'])
            return self._serialized

    def project(self):
        from core import Project
        return Project.from_data(json.loads(self.read()))

    def commit(self, project, *, record_history=True):
        with self._lock:
            self._require_open()
            if self.matches(project):
                return False
            project.validate()
            serialized = serialize(project)
            result = _call('commit', self.handle, self.revision, serialized, bool(record_history))
            self._accept(result, serialized)
            return result['changed']

    def _navigate(self, direction):
        from core import Project
        with self._lock:
            self._require_open()
            result = _call(direction, self.handle, self.revision)
            serialized = result['serialized']
            self._accept(result, serialized)
            return Project.from_data(json.loads(serialized)) if result['changed'] else None

    def undo(self):
        return self._navigate('undo')

    def redo(self):
        return self._navigate('redo')

    def reset(self, project, *, saved=True):
        with self._lock:
            self._require_open()
            project.validate()
            serialized = serialize(project)
            result = _call('create', serialized, self.history_limit)
            old_handle = self.handle
            self._finalizer.detach()
            self._accept(result, serialized)
            self._finalizer = weakref.finalize(self, _release, self.handle)
            _call('destroy', old_handle)
            if saved:
                self._saved = serialized

    def clear_history(self):
        with self._lock:
            self._require_open()
            if self._info['undo_count'] or self._info['redo_count']:
                self.reset(self.project(), saved=False)

    def save(self, project, path):
        """Write validated native wire data, then accept the saved name/state."""
        with self._lock:
            self._require_open()
            project.validate()
            serialized = serialize(project)
            result = _call('save', self.handle, self.revision, str(Path(path).absolute()), serialized)
            self._accept(result, serialized)
            self._saved = serialized

    def close(self):
        with self._lock:
            if self.handle:
                _call('destroy', self.handle)
                self._finalizer.detach()
                self.handle = 0


class NativeHistoryView:
    """Compatibility view for legacy truth/length/clear history consumers."""
    def __init__(self, document, direction):
        self.document, self.direction = document, direction

    def __len__(self):
        return self.document._info[self.direction + '_count']

    def clear(self):
        self.document.clear_history()

    def __repr__(self):
        return f'<C++ {self.direction}: {len(self)}>'


def create_if_available(project):
    import native_model
    if not native_model.available():
        return None
    return NativeDocument(project)
