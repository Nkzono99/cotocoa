from __future__ import annotations

import ctypes as C
import operator

from ._native import Error, Metadata, Options, View, library


def _int64(value, name):
    value = operator.index(value)
    if not -(2**63) <= value < 2**63:
        raise ValueError(f"{name} is outside int64 range")
    return value


def _check(rc, error):
    if rc < 0:
        raise RuntimeError(error.message.decode("utf-8", errors="replace"))
    return rc


class Context:
    def __init__(self, name, *, config="cotocoa.yaml"):
        self.name, self.config = name, str(config)
        self._handle = C.c_void_p()
        self._active = None
        self._used = False
        self.comm = None

    def open(self):
        if self._used:
            raise RuntimeError("Context supports one open/close lifetime")
        self._lib = library()
        from mpi4py import MPI
        error = Error()
        _check(self._lib.cotocoa_open(self.name.encode(), self.config.encode(),
                                    C.byref(self._handle), C.byref(error)), error)
        self._used = True
        self.comm = MPI.Comm.f2py(self._lib.cotocoa_comm_f(self._handle))
        return self

    def _require_open(self):
        if not self._handle:
            raise RuntimeError("Context is not open")

    def __enter__(self):
        return self.open()

    def __exit__(self, exc_type, exc, tb):
        if exc_type:
            self._abort(f"program {self.name} failed: {exc_type.__name__}: {exc}")
        self.close()

    def _abort(self, text):
        error = Error(-2, text.encode("utf-8", errors="replace")[:511])
        self._lib.cotocoa_abort(self._handle, C.byref(error))

    def send(self, name, array, *, step, time=None, offset=(0, 0, 0)):
        self._require_open()
        import numpy as np
        if not isinstance(array, np.ndarray) or array.dtype != np.dtype("float64") or array.ndim != 3:
            raise ValueError("send requires a native float64 NumPy array with three dimensions")
        if len(offset) != 3:
            raise ValueError("offset must have three elements")
        options = Options()
        options.step = _int64(step, "step")
        options.has_time = time is not None
        options.time = 0.0 if time is None else time
        options.offset[:] = [_int64(x, "offset") for x in offset]
        view = View(array.ctypes.data, 1, 3,
                    (C.c_int64 * 3)(*array.shape), (C.c_int64 * 3)(*array.strides))
        error = Error()
        _check(self._lib.cotocoa_send(self._handle, name.encode(), C.byref(view),
                                    C.byref(options), C.byref(error)), error)

    def receive(self, name):
        self._require_open()
        while True:
            handle, error = C.c_void_p(), Error()
            rc = _check(self._lib.cotocoa_receive(self._handle, name.encode(),
                                                 C.byref(handle), C.byref(error)), error)
            if rc == 1:
                return
            try:
                message = Message(self, handle)
            except BaseException as exc:
                self._abort(f"cannot construct received message: {exc}")
            self._active = message
            yield message

    def flush(self):
        self._require_open()
        error = Error()
        _check(self._lib.cotocoa_flush(self._handle, C.byref(error)), error)

    def close(self):
        self._require_open()
        error = Error()
        _check(self._lib.cotocoa_close(self._handle, C.byref(error)), error)
        self._handle = C.c_void_p()
        self.comm = None


class Message:
    def __init__(self, context, handle):
        import numpy as np
        self._context, self._handle = context, handle
        meta, view, error = Metadata(), View(), Error()
        _check(context._lib.cotocoa_message_metadata(handle, C.byref(meta), C.byref(error)), error)
        _check(context._lib.cotocoa_message_view(handle, C.byref(view), C.byref(error)), error)
        self.step, self.sequence, self.source_rank = meta.step, meta.sequence, meta.source_rank
        self.time = meta.time if meta.has_time else None
        self.shape, self.global_shape, self.offset = tuple(meta.shape), tuple(meta.global_shape), tuple(meta.offset)
        self.axes = tuple(bytes(axis).split(b"\0", 1)[0].decode() for axis in meta.axes)
        self.units, self.time_unit = meta.units.decode(), meta.time_unit.decode()
        pointer = C.cast(view.data, C.POINTER(C.c_double))
        # A NumPy-owned copy keeps saved arrays valid after Message/Context close.
        raw = np.ctypeslib.as_array(pointer, shape=(int(np.prod(self.shape)),))
        self.array = np.array(raw.reshape(self.shape, order="F"), order="F", copy=True)
        self.array.flags.writeable = False

    def __enter__(self):
        if not self._handle:
            raise RuntimeError("message already completed")
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc_type:
            self._context._abort(f"processing {self.step=} failed: {exc_type.__name__}: {exc}")
        self.complete()

    def complete(self):
        if not self._handle:
            raise RuntimeError("message already completed")
        error = Error()
        _check(self._context._lib.cotocoa_complete(self._handle, C.byref(error)), error)
        self._context._lib.cotocoa_message_release(self._handle)
        self._handle = C.c_void_p()
        self._context._active = None
