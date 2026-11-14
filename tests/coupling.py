import os
import sys
import time

import numpy as np

from cotocoa import Context


def main():
    name, config = sys.argv[1:]
    from mpi4py import MPI
    steps = int(os.environ.get("COTOCOA_TEST_STEPS", "100"))
    with Context(name, config=config) as ctx:
        rank = ctx.comm.Get_rank()
        if name == "producer":
            i, j, k = np.indices((3, 5, 7))
            # Cover both NumPy memory layouts in every Python-producer case.
            for step in range(1, steps+1):
                field = np.array(rank*1000000 + step*1000 + i*100 + j*10 + k,
                                 dtype=np.float64, order="C" if step % 2 else "F")
                ctx.send("field", field, step=step, time=step*0.01, offset=(rank*3, 0, 0))
                field[:] = 0
            ctx.flush()
        else:
            count, saved = 0, None
            source_ranks = MPI.COMM_WORLD.Get_size()-ctx.comm.Get_size()-1
            i, j, k = np.indices((3, 5, 7))
            for message in ctx.receive("field"):
                with message:
                    if os.environ.get("COTOCOA_TEST_FAIL"):
                        raise RuntimeError("injected consumer failure")
                    expected = message.source_rank*1000000 + message.step*1000 + i*100+j*10+k
                    np.testing.assert_array_equal(message.array, expected)
                    assert message.shape == (3, 5, 7)
                    assert message.global_shape == (source_ranks*3, 5, 7)
                    assert message.offset == (message.source_rank*3, 0, 0)
                    assert message.sequence == message.step
                    assert message.time == message.step*0.01
                    assert message.axes == ("x", "y", "z")
                    assert message.units == "V" and message.time_unit == "s"
                    assert not message.array.flags.writeable
                    if saved is None:
                        saved = message.array
                        time.sleep(0.01)
                    count += 1
            assert count == steps*source_ranks
            print(f"PASS Python rank={rank} messages={count}", flush=True)
    assert not MPI.Is_finalized(), "borrowed MPI was finalized"
    if name == "consumer" and saved is not None:
        assert saved[0, 0, 0] >= 1000  # Retained after Context.close too.


if __name__ == "__main__":
    main()
