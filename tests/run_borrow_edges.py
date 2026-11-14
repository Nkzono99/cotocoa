import json
import os
from pathlib import Path
import sys
import yaml
from run_matrix import run_case

if not os.environ.get('SLURM_JOB_ID'): raise SystemExit('requires compute allocation')
root=Path(__file__).resolve().parents[1]
os.chdir(root)
output=root/'build'/f'borrow-edges-{os.environ["SLURM_JOB_ID"]}'
output.mkdir()
for case in ('overlap','latest'):
    path=output/f'{case}.yaml'
    value={'schema':1,'runtime':{'timeout_seconds':30},'programs':{
        name:{'command':[sys.executable,str(root/f'tests/borrow_{case}.py'),name,str(path)],'ranks':2}
        for name in ('producer','consumer')},'connections':[{'name':'field','from':'producer.field','to':'consumer.field',
            'shape':[3],'global_shape':[3],'axes':['x'],'dtype':'float64','mode':'borrow','buffer_slots':2,
            'delivery':'latest' if case=='latest' else 'all'}]}
    path.write_text(yaml.safe_dump(value))
    env=dict(os.environ)
    env.pop('COTOCOA_MANIFEST',None)
    directory=output/f'{case}-run'
    run_case(case,[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm','--run-dir',str(directory)],env,output)
    if case=='latest':
        ranks=[json.loads(p.read_text()) for p in directory.glob('rank-*.json')]
        assert sum(r['dropped'] for r in ranks)>0
print('Borrowed overlap and latest edge cases passed.',flush=True)
