"""Use an explicit test handshake to keep overlapping leases in flight."""
import sys
import numpy as np
from mpi4py import MPI
from cotocoa import Context

name,config=sys.argv[1:]
with Context(name,config=config) as ctx:
    if name=='producer':
        rank=ctx.comm.Get_rank()
        storage=np.arange(4,dtype=np.float64)+rank*100
        first=ctx.borrow('field',storage[:3],step=1)
        second=ctx.borrow('field',storage[:3],step=2)
        # The worker holds the first message until we release this handshake.
        MPI.COMM_WORLD.recv(source=3,tag=700)
        try:
            ctx.borrow('field',storage[1:4],step=3)
        except RuntimeError as exc:
            assert 'overlap' in str(exc)
        else:
            raise AssertionError('partial overlap must be rejected before publication')
        MPI.COMM_WORLD.send(True,dest=3,tag=701)
        first.wait(); second.wait()
        # Both references to the original attachment must be detached now.
        third=ctx.borrow('field',storage[1:4],step=3)
        third.wait()
    else:
        count=0
        for message in ctx.receive('field'):
            with message:
                if message.step==1:
                    if ctx.comm.Get_rank()==0:
                        MPI.COMM_WORLD.send(True,dest=message.source_rank,tag=700)
                        MPI.COMM_WORLD.recv(source=message.source_rank,tag=701)
                    ctx.comm.Barrier()
                expected=np.arange(3)+message.source_rank*100+(message.step==3)
                np.testing.assert_array_equal(message.array,expected)
                count+=1
        assert count==6
print('PASS contained registration reuse/partial overlap/detach and reattach',flush=True)
