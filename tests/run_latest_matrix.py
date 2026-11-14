import itertools
import os
from pathlib import Path
import sys
import yaml
from run_matrix import run_case

root=Path(__file__).resolve().parents[1]
os.chdir(root)
output=root/'build/latest-results'
output.mkdir(exist_ok=True)
commands={'c':[str(root/'build/tests/latest_c')],
          'fortran':[str(root/'build/tests/latest_fortran')],
          'python':[sys.executable,str(root/'tests/latest.py')]}
for source,target in itertools.product(commands,repeat=2):
    label=f'latest-{source}-to-{target}'
    path=output/f'{label}.yaml'
    config={'schema':1,'programs':{
        'producer':{'command':[*commands[source],'producer',str(path)],'ranks':2},
        'consumer':{'command':[*commands[target],'consumer',str(path)],'ranks':2}},
        'connections':[{'name':'latest','from':'producer.field','to':'consumer.field','dtype':'float64',
            'shape':[3],'global_shape':[6],'axes':['x'],'delivery':'latest','buffer_slots':2}],
        'runtime':{'timeout_seconds':60}}
    path.write_text(yaml.safe_dump(config))
    env=dict(os.environ)
    env.pop('COTOCOA_MANIFEST',None)
    run_case(label,[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm'],env,output)
print('All nine latest-delivery language pairs passed.',flush=True)
