import sys
import numpy as np
from cotocoa import Context

name=sys.argv[1]
with Context(name,config='tasks.yaml') as ctx:
    if name=='producer':
        tickets=[ctx.submit('sum',np.arange(3,dtype=np.float64)+step,step=step) for step in range(10)]
        for ticket in tickets:
            ticket.wait()
    else:
        for task in ctx.tasks('sum'):
            with task:
                print(f'step={task.step} sum={task.array.sum()}',flush=True)
print('PASS task example Python',flush=True)
