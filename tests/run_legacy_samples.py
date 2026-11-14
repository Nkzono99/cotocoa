"""Run unchanged original samples, retaining their four-rank worker groups."""
import os
from pathlib import Path
import re
import subprocess
import sys
import numpy as np
from cotocoa.cli import _launch_command,_terminate

if not os.environ.get('SLURM_JOB_ID'):
    raise SystemExit('requires a 21-rank compute allocation')
root=Path(__file__).resolve().parents[1]
os.chdir(root)
output=root/'build'/f'legacy-samples-{os.environ["SLURM_JOB_ID"]}'
output.mkdir()
executables=root/'build/legacy-original'
env=dict(os.environ,COTOCOA_LIBRARY=str(root/'build/libcotocoa.so'),PYTHONUNBUFFERED='1')
for key in ('COTOCOA_MANIFEST','COTOCOA_RUN_DIR','COTOCOA_TEST_BORROWED'):
    env.pop(key,None)

def run(label,commands):
    directory=output/label
    directory.mkdir()
    ranks=sum(count for count,_ in commands)
    command=_launch_command('slurm',commands,directory,ranks,label)
    with (directory/'output.log').open('w') as f:
        process=subprocess.Popen(command,cwd=directory,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            rc=process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            _terminate(process)
            raise AssertionError(f'{label}: timeout; see {directory}')
    text=(directory/'output.log').read_text(errors='replace')
    assert rc==0,(label,rc,text)
    return directory,text

for sample in (1,2):
    commands=[(4,[str(executables/f'sample{sample}_requester')]),
        (1,[str(executables/f'sample{sample}_coupler')]),
        *[(8 if i==1 else 4,[str(executables/f'sample{sample}_worker{i}')]) for i in (1,2,3)]]
    directory,text=run(f'sample{sample}',commands)
    if sample==1:
        rows=re.findall(r'worker([123]) rank (\d+) c (\d+) data ([^\n]+)',text)
        assert len(rows)==40,(len(rows),text)
        seen=set()
        for worker,rank,step,values in rows:
            worker,rank,step=map(int,(worker,rank,step))
            assert worker==step%3+1
            np.testing.assert_array_equal(np.fromstring(values,sep=' '),step*10000+np.arange(5))
            assert (step,rank) not in seen
            seen.add((step,rank))
        assert seen=={(step,rank) for step in range(10) for rank in range(4)}
    else:
        files=list(directory.glob('out*.txt'))
        assert len(files)==40,len(files)
        for step in range(1,11):
            for rank in range(4):
                # List-directed Fortran output may wrap one logical row.
                data=np.loadtxt(directory/f'out{step%3+1}.{step:03}-{rank:03}.txt').reshape(400,6)
                expected=step*10000+np.arange(1,401)[:,None]*10+np.arange(1,7)[None,:]
                np.testing.assert_array_equal(data,expected)
    print(f'PASS original sample{sample}: 21 ranks, 40 worker copies with exact values',flush=True)

commands=[(4,[str(executables/'sample4_requester')]),(1,[str(executables/'sample4_coupler')]),
    (2,[sys.executable,str(root/'test/sample4_worker.py')])]
directory,text=run('sample4',commands)
rows=re.findall(r'python worker world_rank (\d+), subrank (\d+) data ([-\d.]+) ([-\d.]+) ([-\d.]+) from requester rank (\d+)',text)
assert len(rows)==20,(len(rows),text)
for _,rank,first,middle,last,source in rows:
    assert int(rank) in (0,1) and int(source)==0
    first,middle,last=map(float,(first,middle,last))
    assert middle-first==625 and last-first==1249
for role in ('Requester','Coupler','Worker'):
    assert f'Profiling results, {role}' in text
print('PASS original sample4: 7 ranks, Python Worker, exact payload differences and profiling/finalize',flush=True)
print('All original C/Fortran/Python samples passed without source changes.',flush=True)
