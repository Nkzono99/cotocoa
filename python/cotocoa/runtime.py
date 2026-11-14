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
        self._pins=[]

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

    def _prepare(self,array,step,time,offset):
        self._require_open()
        import numpy as np
        types = {np.dtype("float64"): 1, np.dtype("float32"): 2, np.dtype("int32"): 3}
        if not isinstance(array, np.ndarray) or array.dtype not in types or not 1 <= array.ndim <= 3:
            raise ValueError("send requires a native int32/float32/float64 NumPy array with one to three dimensions")
        offset = (0,)*array.ndim if offset is None else offset
        if len(offset) != array.ndim:
            raise ValueError("offset must match the array's dimensions")
        options = Options()
        options.step = _int64(step, "step")
        options.has_time = time is not None
        options.time = 0.0 if time is None else time
        options.offset[:] = [_int64(x, "offset") for x in offset] + [0]*(3-array.ndim)
        view = View(array.ctypes.data, types[array.dtype], array.ndim,
                    (C.c_int64 * 3)(*(array.shape + (1,)*(3-array.ndim))),
                    (C.c_int64 * 3)(*(array.strides + (0,)*(3-array.ndim))))
        return view,options

    def send(self,name,array,*,step,time=None,offset=None):
        self._collect_pins()
        view,options=self._prepare(array,step,time,offset)
        error = Error()
        _check(self._lib.cotocoa_send(self._handle, name.encode(), C.byref(view),
                                    C.byref(options), C.byref(error)), error)

    def submit(self,name,array,*,step=0,time=None,offset=None,borrow=False):
        if borrow:
            return self._borrow(name,array,step,time,offset,True)
        view,options=self._prepare(array,step,time,offset)
        error,handle=Error(),C.c_void_p()
        _check(self._lib.cotocoa_submit(self._handle,name.encode(),C.byref(view),C.byref(options),
                                       C.byref(handle),C.byref(error)),error)
        return Ticket(self._lib,handle,self)

    def borrow(self,name,array,*,step=0,time=None,offset=None):
        return self._borrow(name,array,step,time,offset,False)

    def _borrow(self,name,array,step,time,offset,task):
        self._collect_pins()
        view,options=self._prepare(array,step,time,offset)
        error,handle=Error(),C.c_void_p()
        publish=self._lib.cotocoa_submit_borrow if task else self._lib.cotocoa_borrow
        _check(publish(self._handle,name.encode(),C.byref(view),C.byref(options),C.byref(handle),C.byref(error)),error)
        self._lib.cotocoa_ticket_retain(handle)
        pin=Ticket(self._lib,C.c_void_p(handle.value))
        self._pins.append((pin,array))
        ticket=Ticket(self._lib,handle,self)
        self._collect_pins()
        return ticket

    def _collect_pins(self):
        remaining=[]
        for ticket,array in self._pins:
            if ticket.test():
                ticket.release()
            else:
                remaining.append((ticket,array))
        self._pins=remaining

    def receive(self,name=None):
        return self._receive(name,False)

    def tasks(self,operation=None):
        return self._receive(operation,True)

    def _receive(self, name, tasks):
        self._require_open()
        while True:
            handle, error = C.c_void_p(), Error()
            receive=self._lib.cotocoa_next_task if tasks else self._lib.cotocoa_receive
            rc = _check(receive(self._handle, name.encode() if name is not None else None,
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
        self._collect_pins()

    def close(self):
        self._require_open()
        error = Error()
        _check(self._lib.cotocoa_close(self._handle, C.byref(error)), error)
        self._handle = C.c_void_p()
        self.comm = None
        self._collect_pins()


class Message:
    def __init__(self, context, handle):
        import numpy as np
        self._context, self._handle = context, handle
        meta, view, error = Metadata(), View(), Error()
        _check(context._lib.cotocoa_message_metadata(handle, C.byref(meta), C.byref(error)), error)
        _check(context._lib.cotocoa_message_view(handle, C.byref(view), C.byref(error)), error)
        self.step, self.sequence, self.source_rank = meta.step, meta.sequence, meta.source_rank
        self.time = meta.time if meta.has_time else None
        self.shape = tuple(meta.shape)[:meta.ndim]
        self.global_shape, self.offset = tuple(meta.global_shape)[:meta.ndim], tuple(meta.offset)[:meta.ndim]
        self.axes = tuple(bytes(axis).split(b"\0", 1)[0].decode() for axis in meta.axes[:meta.ndim])
        self.units, self.time_unit = meta.units.decode(), meta.time_unit.decode()
        self.kind='task' if meta.kind else 'stream'
        self.name,self.port,self.source=meta.name.decode(),meta.port.decode(),meta.source.decode()
        self.source_count=meta.source_count
        self.delivery='latest' if meta.delivery else 'all'
        self.mode='borrow' if meta.mode else 'snapshot'
        dtype, ctype = {1: (np.dtype("float64"), C.c_double),
                        2: (np.dtype("float32"), C.c_float),
                        3: (np.dtype("int32"), C.c_int32)}[meta.dtype]
        self.dtype = dtype
        pointer = C.cast(view.data, C.POINTER(ctype))
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


class Ticket:
    """Completion handle; releasing it never cancels the requested work."""
    def __init__(self,lib,handle,owner=None):
        self._lib,self._handle=lib,handle
        self._owner=owner

    def test(self):
        if not self._handle:
            raise RuntimeError('ticket was released')
        done,error=C.c_int(),Error()
        _check(self._lib.cotocoa_ticket_test(self._handle,C.byref(done),C.byref(error)),error)
        if self._owner is not None:
            self._owner._collect_pins()
        return bool(done.value)

    def wait(self):
        if not self._handle:
            raise RuntimeError('ticket was released')
        error=Error()
        _check(self._lib.cotocoa_ticket_wait(self._handle,C.byref(error)),error)
        if self._owner is not None:
            self._owner._collect_pins()

    def release(self):
        if self._handle:
            self._lib.cotocoa_ticket_release(self._handle)
            self._handle=C.c_void_p()
        self._owner=None

    def __copy__(self):
        if not self._handle:
            raise RuntimeError('ticket was released')
        self._lib.cotocoa_ticket_retain(self._handle)
        return Ticket(self._lib,C.c_void_p(self._handle.value),self._owner)

    def __deepcopy__(self,memo):
        return self.__copy__()

    def __del__(self):
        # Native ticket release only frees local memory, with no MPI operation.
        self.release()


class Frame(Message):
    """An assembled, NumPy-owned frame; no communication acknowledgement."""
    def __init__(self, assembler, handle):
        try:
            super().__init__(assembler,handle)
        finally:
            assembler._lib.cotocoa_message_release(handle)
        self._context=None
        self._handle=C.c_void_p()

    def complete(self):
        raise RuntimeError('assembled frames need no complete(); complete each input message')

    def __enter__(self):
        return self

    def __exit__(self,exc_type,exc,tb):
        return False


class Assembler:
    """Bounded local assembly of one all-delivery stream connection."""
    def __init__(self,*,max_pending_steps=2,max_frame_bytes=256*1024*1024):
        self._lib=library()
        self._handle=C.c_void_p()
        steps=operator.index(max_pending_steps)
        limit=operator.index(max_frame_bytes)
        if not 1<=steps<=1024 or not 1<=limit<2**64:
            raise ValueError('invalid pending step/frame byte limits')
        error=Error()
        _check(self._lib.cotocoa_assembler_create(steps,limit,C.byref(self._handle),C.byref(error)),error)

    def push(self,message):
        if not self._handle or not message._handle:
            raise RuntimeError('assembler and input message must be open')
        handle,error=C.c_void_p(),Error()
        _check(self._lib.cotocoa_assembler_push(self._handle,message._handle,C.byref(handle),C.byref(error)),error)
        return Frame(self,handle) if handle else None

    def finish(self):
        if not self._handle:
            raise RuntimeError('assembler is closed')
        error=Error()
        _check(self._lib.cotocoa_assembler_finish(self._handle,C.byref(error)),error)

    def close(self):
        if self._handle:
            self._lib.cotocoa_assembler_release(self._handle)
            self._handle=C.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self,exc_type,exc,tb):
        try:
            if exc_type is None:
                self.finish()
        finally:
            self.close()

    def __del__(self):
        # Only local memory: safe after MPI shutdown.
        if getattr(self,'_handle',None):
            self.close()
