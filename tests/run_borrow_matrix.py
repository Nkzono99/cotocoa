"""Borrowed stream/task inputs, including reversed and noncontiguous views."""
import os
from pathlib import Path
import sys
import yaml
from run_matrix import run_case

root=Path(__file__).resolve().parents[1]
os.chdir(root)
if not os.environ.get('SLURM_JOB_ID'):
    raise SystemExit('requires a five-rank Slurm allocation')
output=root/'build/borrow-results'
output.mkdir(exist_ok=True)
languages=('c','fortran','python')
commands={x:[str(root/f'build/tests/typed_{x}')] for x in languages[:2]}
commands['python']=[sys.executable,str(root/'tests/typed.py')]
selected=os.environ.get('COTOCOA_TEST_SOURCE')
if selected is not None and selected not in languages: raise SystemExit('invalid COTOCOA_TEST_SOURCE')
cases=0
for mode in (1,2):
    for si,source in enumerate(languages):
        if selected is not None and source!=selected: continue
        for code,dtype in enumerate(('float64','float32','int32'),1):
            for ndim in (1,2,3):
                target=languages[(si+code+ndim)%3]
                label=f'{mode}-{source}-{target}-{dtype}-{ndim}'
                path=output/f'{label}.yaml'
                shape=[3,5,7][:ndim]
                connection={'name':'loan','from':'producer.field','to':'consumer.field',
                    'dtype':dtype,'shape':shape,'global_shape':[6,*shape[1:]],
                    'axes':['x','y','z'][:ndim],'mode':'borrow','buffer_slots':2,
                    'kind':'task' if mode==2 else 'stream'}
                config={'schema':1,'runtime':{'timeout_seconds':60},'programs':{
                    'producer':{'command':[*commands[source],'producer',str(path)],'ranks':2},
                    'consumer':{'command':[*commands[target],'consumer',str(path)],'ranks':2}},
                    'connections':[connection]}
                path.write_text(yaml.safe_dump(config))
                env=dict(os.environ,COTOCOA_TEST_DTYPE=str(code),COTOCOA_TEST_NDIM=str(ndim),COTOCOA_TEST_LOAN=str(mode))
                env.pop('COTOCOA_MANIFEST',None)
                run_case(label,[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm'],env,output)
                cases+=1
print(f'All {cases} borrowed stream/task cases passed.',flush=True)
