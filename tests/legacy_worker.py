import os
import numpy as np
from ctca4py.ctcaworker import CTCAWorker
from mpi4py import MPI

dtype=int(os.environ['COTOCOA_LEGACY_DTYPE'])
worker=CTCAWorker()
worker.CTCAW_init(1,2)
area=worker.CTCAW_regarea(dtype)
count=0
while True:
    source,params,data=worker.CTCAW_pollreq(2,4,dtype)
    if worker.CTCAW_isfin(): break
    expected=np.arange(4)+params[0]*1000+params[1]*10
    np.testing.assert_array_equal(data,expected)
    np.testing.assert_array_equal(worker.CTCAW_readarea(area,source,0,4,dtype),expected)
    # Noncontiguous input must be packed, not replaced with zeros or mutated.
    storage=np.repeat(-data,2)
    before=storage.copy()
    if worker.subrank==0:
        worker.CTCAW_writearea(area,source,0,4,storage[::2],dtype)
    np.testing.assert_array_equal(storage,before)
    worker.CTCA_subcomm.Barrier()
    worker.CTCAW_complete()
    count+=1
assert count==16
worker.CTCAW_finalize()
assert not MPI.Is_finalized()
print(f'PASS legacy Python payload/read/write dtype={dtype}',flush=True)
