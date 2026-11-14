import os
import sys
import numpy as np
from cotocoa import Context, Assembler

dtype = np.dtype({"1": "float64", "2": "float32", "3": "int32"}[os.environ["COTOCOA_TEST_DTYPE"]])
ndim = int(os.environ["COTOCOA_TEST_NDIM"])
mode = int(os.environ.get("COTOCOA_TEST_LOAN", "0"))
assemble = bool(os.environ.get("COTOCOA_TEST_ASSEMBLE"))
shape = (3, 5, 7)[:ndim]
grid = np.indices(shape)
indices = sum(grid[d]*[100, 10, 1][d] for d in range(ndim))
name, config = sys.argv[1:]
with Context(name, config=config) as ctx:
    if name == "producer":
        rank = ctx.comm.Get_rank()
        storage = np.zeros((6, *shape[1:]), dtype=dtype)
        for step in range(1, 9):
            field = storage[::2] if step%2 else storage[::-2]
            field[:] = -1000000+rank*1000000+step*1000+indices
            options = dict(step=step, offset=(rank*3, *((0,)*(ndim-1))))
            if mode:
                ticket = ctx.submit("field", field, borrow=True, **options) if mode==2 else ctx.borrow("field", field, **options)
                # The context retains the input even when the user drops a ticket.
                ticket.release()
                ctx.flush()
            else:
                ctx.send("field", field, **options)
            storage[:] = 0
            if assemble:
                ctx.comm.Barrier()
    else:
        count = 0
        assembler = Assembler(max_frame_bytes=1024*1024) if assemble else None
        frames = 0
        for message in ctx.receive("field"):
            with message:
                assert message.dtype == dtype
                assert message.shape == shape
                assert message.offset == (message.source_rank*3, *((0,)*(ndim-1)))
                assert message.axes == ("x", "y", "z")[:ndim]
                np.testing.assert_array_equal(message.array, -1000000+message.source_rank*1000000+message.step*1000+indices)
                count += 1
                if assemble:
                    frame = assembler.push(message)
                    if frame is not None:
                        assert frame.shape == (6, *shape[1:]) and frame.source_rank == -1
                        expected = np.concatenate([-1000000+r*1000000+frame.step*1000+indices for r in (0,1)])
                        np.testing.assert_array_equal(frame.array, expected)
                        frames += 1
        assert count == 16
        if assemble:
            assembler.finish()
            assembler.close()
            assert frames == 8
        print(f"PASS typed Python {dtype} ndim={ndim}", flush=True)
