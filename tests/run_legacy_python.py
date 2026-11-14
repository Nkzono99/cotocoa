import os
from pathlib import Path
import sys
from cotocoa.cli import _launch_command
from run_matrix import run_case

if not os.environ.get('SLURM_JOB_ID'): raise SystemExit('requires compute allocation')
root=Path(__file__).resolve().parents[1]
os.chdir(root)
output=root/'build'/f'legacy-python-{os.environ["SLURM_JOB_ID"]}'
output.mkdir()
env=dict(os.environ)
env.pop('COTOCOA_MANIFEST',None)
env.pop('COTOCOA_RUN_DIR',None)
native=root/'build/tests/legacy_typed'
for dtype in range(3):
    commands=[(2,[str(native),'producer']),(1,[str(native),'coupler']),
        (2,[sys.executable,str(root/'tests/legacy_worker.py')])]
    command=_launch_command('slurm',commands,output,5,f'legacy-{dtype}')
    run_case(f'legacy-python-{dtype}',command,dict(env,COTOCOA_LEGACY_DTYPE=str(dtype)),output)
print('All legacy Python typed payload/read/write cases passed.',flush=True)
