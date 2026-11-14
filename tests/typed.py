import os
import sys
import numpy as np
from cotocoa import Context

dtype = np.dtype({"1": "float64", "2": "float32", "3": "int32"}[os.environ["COTOCOA_TEST_DTYPE"]])
ndim = int(os.environ["COTOCOA_TEST_NDIM"])
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
            ctx.send("field", field, step=step, offset=(rank*3, *((0,)*(ndim-1))))
            storage[:] = 0
    else:
        count = 0
        for message in ctx.receive("field"):
            with message:
                assert message.dtype == dtype
                assert message.shape == shape
                assert message.offset == (message.source_rank*3, *((0,)*(ndim-1)))
                assert message.axes == ("x", "y", "z")[:ndim]
                np.testing.assert_array_equal(message.array, -1000000+message.source_rank*1000000+message.step*1000+indices)
                count += 1
        assert count == 16
        print(f"PASS typed Python {dtype} ndim={ndim}", flush=True)
