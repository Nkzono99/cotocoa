import sys
import numpy as np
from cotocoa import Context

name = sys.argv[1]
with Context(name) as ctx:
    if name == "producer":
        rank = ctx.comm.Get_rank()
        for step in range(1, 11):
            field = np.full((3, 5, 7), step+rank, dtype=np.float64)
            ctx.send("potential", field, step=step, offset=(rank*3, 0, 0))
    else:
        for message in ctx.receive("potential"):
            with message:
                print(message.step, message.source_rank, message.array.mean())
