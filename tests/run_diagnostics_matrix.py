"""Probe/watchdog/native waits/installation/legacy/lifetime integration tests."""
import json
import os
from pathlib import Path
import subprocess
import sys
import yaml
from cotocoa.cli import _launch_command,_terminate,_commands
from cotocoa.config import load_config

root=Path(__file__).resolve().parents[1]
os.chdir(root)
if not os.environ.get('SLURM_JOB_ID'): raise SystemExit('requires compute allocation')
output=root/'build'/f'diagnostics-{os.environ["SLURM_JOB_ID"]}'
output.mkdir()
env=dict(os.environ)
for key in ('COTOCOA_MANIFEST','COTOCOA_RUN_DIR','COTOCOA_TEST_LOAN','COTOCOA_TEST_ASSEMBLE'):
    env.pop(key,None)

def execute(label,command,environment=env,expected=None):
    log=output/f'{label}.log'
    with log.open('w') as f:
        process=subprocess.Popen(command,env=environment,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
        try: rc=process.wait(timeout=45)
        except subprocess.TimeoutExpired:
            _terminate(process)
            raise AssertionError(f'{label} hung; see {log}')
    text=log.read_text(errors='replace')
    if expected:
        assert rc!=0 and expected in text,(label,rc,text)
    else: assert rc==0 and 'PASS' in text,(label,rc,text)
    print(f'PASS {label}',flush=True)
    return text

def config(label,commands=None,mode='snapshot'):
    path=output/f'{label}.yaml'
    if commands is None: commands={name:[sys.executable,str(root/'tests/borrow_lifetime.py'),name,str(path)] for name in ('producer','consumer')}
    value={'schema':1,'programs':{name:{'command':cmd,'ranks':2} for name,cmd in commands.items()},
        'runtime':{'timeout_seconds':30},'connections':[{'name':'field','from':'producer.field','to':'consumer.field',
            'dtype':'float64','shape':[3],'axes':['x'],'mode':mode,'buffer_slots':2}]}
    return path,value

def launch(label,path,value,action='run',extra=(),environment=env):
    path.write_text(yaml.safe_dump(value))
    run_dir=output/f'{label}-run'
    execute(label,[sys.executable,'-m','cotocoa',action,str(path),'--launcher','slurm','--run-dir',str(run_dir),*extra],environment)
    state=json.loads((run_dir/'run.json').read_text())
    assert state['state']=='finished'
    status=subprocess.check_output([sys.executable,'-m','cotocoa','status',str(run_dir),'--json'],env=environment,text=True)
    status=json.loads(status)
    assert not status['missing_ranks'] and all(r['phase']=='closed' for r in status['rank_status'])
    return run_dir

prefix=Path((root/'build/verified-install-prefix').read_text().strip())
installed_library=next(prefix.glob('lib*/libcotocoa.so'))
path,value=config('probe',mode='borrow')
launch('installed-probe',path,value,'probe',extra=('--probe',str(prefix/'bin/cotocoa-probe')))

# Both consumers see each publication; tickets/arrays may be released by the user immediately.
path,value=config('pins',mode='borrow')
value['programs']['consumer']['ranks']=1
value['programs']['other']={'command':[sys.executable,str(root/'tests/borrow_lifetime.py'),'other',str(path)],'ranks':1}
second=dict(value['connections'][0],name='other_field',to='other.field')
value['connections'].append(second)
launch('borrow-pins-fanout',path,value,extra=('--probe-first',),environment=dict(env,COTOCOA_LIBRARY=str(installed_library)))

# Aggregate native tickets include both fan-out destinations, for both native bindings.
for source,target in (('c','fortran'),('fortran','c')):
    label=f'borrow-fanout-{source}'
    path,value=config(label,mode='borrow')
    value['connections'][0]['global_shape']=[6]
    value['programs']['producer']['command']=[str(root/f'build/tests/typed_{source}'),'producer',str(path)]
    value['programs']['consumer']={'command':[str(root/f'build/tests/typed_{target}'),'consumer',str(path)],'ranks':1}
    value['programs']['other']={'command':[sys.executable,str(root/'tests/typed.py'),'other',str(path)],'ranks':1}
    value['connections'].append(dict(value['connections'][0],name='other_field',to='other.field'))
    launch(label,path,value,environment=dict(env,COTOCOA_TEST_DTYPE='1',COTOCOA_TEST_NDIM='1',COTOCOA_TEST_LOAN='1'))

# Every language participates in the runnable Task example in both roles.
task_commands={'c':[str(root/'build/task_c')],'fortran':[str(root/'build/task_fortran')],
               'python':[sys.executable,str(root/'examples/tasks/python.py')]}
for source,target in (('c','fortran'),('fortran','python'),('python','c')):
    label=f'task-example-{source}-{target}'
    path,value=config(label,{name:[*task_commands[lang],name] for name,lang in (('producer',source),('consumer',target))})
    value['programs']['consumer']['group_size']=1
    value['connections']=[dict(value['connections'][0],kind='task',**{'from':'producer.sum','to':'consumer.sum'})]
    launch(label,path,value,extra=('--coupler',str(prefix/'bin/cotocoa-coupler')))

# The controller watchdog also covers ranks that never reach MPI_Init.
path,value=config('watchdog',{name:['/bin/sleep','30'] for name in ('producer','consumer')})
value['runtime']['timeout_seconds']=1.5
path.write_text(yaml.safe_dump(value)); directory=output/'watchdog-run'
execute('watchdog',[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm','--run-dir',str(directory)],expected='exceeded timeout_seconds')
state=json.loads((directory/'run.json').read_text())
assert state['state']=='timeout' and state['elapsed_seconds']<15

# Direct native launch has no Python watchdog: the progress guard must abort.
path,value=config('native-timeout')
for name in value['programs']:
    value['programs'][name]['command']=[sys.executable,str(root/'tests/stalled.py'),name,str(path)]
value['runtime']['timeout_seconds']=1
path.write_text(yaml.safe_dump(value)); plan=load_config(path)
directory=output/'native-timeout-run'; directory.mkdir()
manifest=directory/'cotocoa.manifest'; manifest.write_text(plan.manifest())
command=_launch_command('slurm',_commands(plan,str(root/'build/cotocoa-coupler'),manifest),directory,5,'native')
execute('native-wait-timeout',command,dict(env,COTOCOA_MANIFEST=str(manifest),COTOCOA_RUN_DIR=str(directory)),expected='timeout waiting for')
statuses=[json.loads(p.read_text()) for p in directory.glob('rank-*.json')]
assert any(s['phase']=='failed' and 'timeout waiting' in s['detail'] for s in statuses)

execute('wrong-rank-count',['srun','--kill-on-bad-exit=1','-n','4',str(root/'build/cotocoa-probe'),str(manifest)],expected='startup mismatch')

# The legacy Python worker must receive integer attached payloads and write each native type correctly.
native=root/'build/tests/legacy_typed'
for dtype in range(3):
    commands=[(2,[str(native),'producer']),(1,[str(native),'coupler']),
              (2,[sys.executable,str(root/'tests/legacy_worker.py')])]
    command=_launch_command('slurm',commands,output,5,f'legacy-{dtype}')
    execute(f'legacy-python-{dtype}',command,dict(env,COTOCOA_LEGACY_DTYPE=str(dtype)))

# A new step must still launch after the preceding failure/timeout steps have exited.
path,value=config('final-probe')
launch('probe-after-failures',path,value,'probe')
print('All diagnostic, native installation, lifetime, Task example and legacy checks passed.',flush=True)
