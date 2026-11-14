import os
import sys
import time
import numpy as np
from cotocoa import Context

task=bool(os.environ.get('COTOCOA_TEST_TASK'))
name,config=sys.argv[1:]
tickets=[]
with Context(name,config=config) as ctx:
    if name.startswith('producer'):
        base=2000000 if name=='producer2' else 1000000
        rank=ctx.comm.Get_rank()
        for step in range(1,9):
            for code,dtype in enumerate(('float64','float32','int32'),1):
                for ndim in (1,2,3):
                    grid=np.indices((3,5,7)[:ndim])
                    field=np.asarray(base+rank*100000+step*1000+
                                     sum(grid[d]*[100,10,1][d] for d in range(ndim)),dtype=dtype)
                    kwargs={'step':step,'offset':(rank*3,*((0,)*(ndim-1)))}
                    if task:
                        ticket=ctx.submit(f'field_{code}_{ndim}',field,**kwargs)
                        ticket.test()
                        if not tickets:
                            ticket.release()
                            tickets.append(None)
                        else:
                            tickets.append(ticket)
                    else:
                        ctx.send(f'field_{code}_{ndim}',field,**kwargs)
                    field[:]=0
        ctx.flush()
        for ticket in filter(None,tickets):
            ticket.wait()
    else:
        from mpi4py import MPI
        world=MPI.COMM_WORLD.Get_rank()
        count=0
        for m in (ctx.tasks() if task else ctx.receive()):
            with m:
                assert m.kind==('task' if task else 'stream')
                code={'float64':1,'float32':2,'int32':3}[str(m.dtype)]
                assert m.port==f'field_{code}_{len(m.shape)}'
                assert m.sequence==m.step
                assert m.global_shape[0]==m.source_count*3
                base=2000000 if m.source=='producer2' else 1000000
                grid=np.indices(m.shape)
                expected=base+m.source_rank*100000+m.step*1000+sum(grid[d]*[100,10,1][d] for d in range(m.array.ndim))
                np.testing.assert_array_equal(m.array,expected)
                if not count and task:
                    time.sleep(.02)
                count+=1
            if ctx.comm.Get_rank()==0:
                code={'float64':1,'float32':2,'int32':3}[str(m.dtype)]
                print('DONE',name,m.source,m.source_rank,code,len(m.shape),m.step,world,flush=True)
        print(f'PASS multiplex Python world={world} messages={count}',flush=True)
for ticket in filter(None,tickets):
    assert ticket.test()
    ticket.release()
