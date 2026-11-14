"""Discover the legacy ABI without machine-specific installation paths."""
import ctypes
from ctypes.util import find_library
import os
from pathlib import Path


def load_library():
    override=os.environ.get('CTCA4PY_LIBRARY') or os.environ.get('COTOCOA_LIBRARY')
    root=Path(__file__).resolve().parents[2]
    candidates=[override] if override else [str(Path(__file__).with_name('ctca4py.so')),
        str(root.parent/'build/libcotocoa.so'),str(root/'build/libcotocoa.so'),
        find_library('cotocoa'),find_library('ctca4py')]
    errors=[]
    for path in filter(None,candidates):
        try: return ctypes.CDLL(path)
        except OSError as exc: errors.append(str(exc))
    raise RuntimeError('cannot load legacy CoToCoA ABI; set CTCA4PY_LIBRARY: '+'; '.join(errors))
