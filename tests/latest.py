import sys
import time
import numpy as np
from cotocoa import Context

name,config=sys.argv[1:]
with Context(name,config=config) as ctx:
    if name=='producer':
        rank=ctx.comm.Get_rank()
        for step in range(1,51):
            field=np.asarray(step*1000+rank*100+np.arange(3),dtype=np.float64)
            ctx.send('field',field,step=step,offset=(rank*3,))
            field[:]=0
    else:
        last=[0,0]
        count=0
        for m in ctx.receive('field'):
            with m:
                assert m.step==m.sequence and m.step>last[m.source_rank]
                np.testing.assert_array_equal(m.array,m.step*1000+m.source_rank*100+np.arange(3))
                if not count:
                    time.sleep(.2)
                last[m.source_rank]=m.step
                count+=1
        assert last==[50,50] and count<100
        print('PASS latest Python',count,last,flush=True)
