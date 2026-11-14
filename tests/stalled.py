import sys
import time
import numpy as np
from cotocoa import Context

name,config=sys.argv[1:]
with Context(name,config=config) as ctx:
    if name=='producer':
        ctx.send('field',np.zeros(3),step=99)
        ctx.flush()
    else:
        time.sleep(10)
