"""The shared C ABI. Library discovery is deferred until Context.open."""
import ctypes as C
from ctypes.util import find_library
from functools import lru_cache
import os
from pathlib import Path


class Error(C.Structure):
    _fields_ = [("code", C.c_int), ("message", C.c_char * 512)]


class View(C.Structure):
    _fields_ = [("data", C.c_void_p), ("dtype", C.c_int), ("ndim", C.c_int),
                ("shape", C.c_int64 * 3), ("strides", C.c_int64 * 3)]


class Options(C.Structure):
    _fields_ = [("step", C.c_int64), ("has_time", C.c_int), ("time", C.c_double),
                ("offset", C.c_int64 * 3)]


class Metadata(C.Structure):
    _fields_ = [("step", C.c_int64), ("sequence", C.c_int64),
                ("source_rank", C.c_int), ("has_time", C.c_int), ("time", C.c_double),
                ("shape", C.c_int64 * 3), ("global_shape", C.c_int64 * 3),
                ("offset", C.c_int64 * 3), ("axes", (C.c_char * 64) * 3),
                ("units", C.c_char * 64), ("time_unit", C.c_char * 64),
                ("dtype", C.c_int), ("ndim", C.c_int)]


@lru_cache(maxsize=1)
def library():
    override = os.environ.get("COTOCOA_LIBRARY")
    candidates = [override] if override else [
        str(Path(__file__).resolve().parents[2] / "build" / "libcotocoa.so"),
        find_library("cotocoa"),
    ]
    failures = []
    for path in filter(None, candidates):
        try:
            lib = C.CDLL(path)
            break
        except OSError as exc:
            failures.append(str(exc))
    else:
        raise RuntimeError("cannot load libcotocoa; build with CMake or set COTOCOA_LIBRARY: " + "; ".join(failures))
    ptr, err = C.c_void_p, C.POINTER(Error)
    signatures = {
        "open": ([C.c_char_p, C.c_char_p, C.POINTER(ptr), err], C.c_int),
        "close": ([ptr, err], C.c_int),
        "comm_f": ([ptr], C.c_int),
        "abort": ([ptr, err], None),
        "send": ([ptr, C.c_char_p, C.POINTER(View), C.POINTER(Options), err], C.c_int),
        "flush": ([ptr, err], C.c_int),
        "receive": ([ptr, C.c_char_p, C.POINTER(ptr), err], C.c_int),
        "message_view": ([ptr, C.POINTER(View), err], C.c_int),
        "message_metadata": ([ptr, C.POINTER(Metadata), err], C.c_int),
        "complete": ([ptr, err], C.c_int),
        "message_release": ([ptr], None),
    }
    for name, (args, result) in signatures.items():
        func = getattr(lib, "cotocoa_" + name)
        func.argtypes, func.restype = args, result
    return lib
