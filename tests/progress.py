"""A consumer must not depend on the producer entering MPI, and status records stay current."""
import json
import os
from pathlib import Path
import sys
import time
import numpy as np
from cotocoa import Context

name,config=sys.argv[1:]
with Context(name,config=config) as ctx:
    if name=='producer':
        ctx.send('field',np.zeros(3),step=1,time=time.time())
        time.sleep(4)  # Outside MPI: the snapshot must still be readable and completable.
        ctx.send('field',np.ones(3),step=2,time=time.time())
    else:
        from mpi4py import MPI
        record=Path(os.environ['COTOCOA_RUN_DIR'])/f'rank-{MPI.COMM_WORLD.Get_rank():06d}.json'
        count=0
        for message in ctx.receive('field'):
            with message:
                delay=time.time()-message.time
                assert delay<2.0,f'step {message.step} arrived {delay:.2f} s after it was sent'
                # Written on the phase change, although this rank opened less than a second ago.
                status=json.loads(record.read_text())
                assert status['phase']=='processing' and status['received']==count+1,status
                count+=1
        assert count==4
print(f'PASS progress {name}',flush=True)
