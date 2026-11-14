import copy
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
        for step in range(1,21):
            storage=np.zeros(6)
            field=storage[::-2]
            field[:]=step*1000+ctx.comm.Get_rank()*100+np.arange(3)
            refs.append(weakref.ref(field))
            ticket=ctx.borrow('field',field,step=step)
            other=copy.copy(ticket)
            ticket.release(); other.release()
            del field,storage
            gc.collect()
        ctx.flush()
        gc.collect()
        assert all(ref() is None for ref in refs)
    else:
        count=0
        for message in ctx.receive('field'):
            with message:
                np.testing.assert_array_equal(message.array,message.step*1000+message.source_rank*100+np.arange(3))
                assert message.mode=='borrow'
                time.sleep(0.01)
                count+=1
        assert count==40
print('PASS borrowed fanout/early ticket release/input lifetime',flush=True)
