import gc
import sys
import time
import weakref
import numpy as np
from cotocoa import Context

name,config=sys.argv[1:]
with Context(name,config=config) as ctx:
    if name=='producer':
        refs=[]
        for step in range(1,51):
            storage=np.zeros(6)
            field=storage[::-2]
            field[:]=step*1000+ctx.comm.Get_rank()*100+np.arange(3)
            refs.append(weakref.ref(field))
            ticket=ctx.borrow('field',field,step=step)
            ticket.release()
            del field,storage
        ctx.flush()
        gc.collect()
        assert all(ref() is None for ref in refs)
    else:
        count=0; last=[0,0]
        for message in ctx.receive('field'):
            with message:
                np.testing.assert_array_equal(message.array,message.step*1000+message.source_rank*100+np.arange(3))
                assert message.mode=='borrow' and message.delivery=='latest'
                assert message.step>last[message.source_rank]
                last[message.source_rank]=message.step
                time.sleep(0.03)
                count+=1
        assert count<100 and last==[50,50],(count,last)
print('PASS latest borrowed lifetime/final values',flush=True)
