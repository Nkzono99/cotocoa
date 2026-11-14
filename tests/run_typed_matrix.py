"""27 cases: every language exercises all nine dtype/rank combinations."""
import os
from pathlib import Path
import sys
import yaml

from run_matrix import run_case


def main():
    if not os.environ.get('SLURM_JOB_ID'):
        raise SystemExit('requires a five-rank Slurm allocation')
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    output = root/'build/typed-results'
    output.mkdir(exist_ok=True)
    languages = ('c','fortran','python')
    selected=os.environ.get('COTOCOA_TEST_SOURCE')
    if selected is not None and selected not in languages:
        raise SystemExit('invalid COTOCOA_TEST_SOURCE')
    cases=0
    commands = {'c': [str(root/'build/tests/typed_c')],
                'fortran': [str(root/'build/tests/typed_fortran')],
                'python': [sys.executable,str(root/'tests/typed.py')]}
    for source_index, source in enumerate(languages):
        if selected is not None and source!=selected:
            continue
        for dtype_code, dtype in enumerate(('float64','float32','int32'),1):
            for ndim in (1,2,3):
                target = languages[(source_index+dtype_code+ndim)%3]
                label = f'{source}-to-{target}-{dtype}-{ndim}d'
                path = output/f'{label}.yaml'
                shape = [3,5,7][:ndim]
                config = {'schema':1,'programs':{
                    'producer':{'command':[*commands[source],'producer',str(path)],'ranks':2},
                    'consumer':{'command':[*commands[target],'consumer',str(path)],'ranks':2}},
                    'connections':[{'name':'typed','from':'producer.field','to':'consumer.field',
                        'dtype':dtype,'shape':shape,'global_shape':[6,*shape[1:]],
                        'axes':['x','y','z'][:ndim],'buffer_slots':2}]}
                path.write_text(yaml.safe_dump(config))
                env = dict(os.environ, COTOCOA_TEST_DTYPE=str(dtype_code), COTOCOA_TEST_NDIM=str(ndim))
                env.pop('COTOCOA_MANIFEST',None)
                run_case(label,[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm'],env,output)
                cases+=1
    print(f'All {cases} typed language/rank cases passed.',flush=True)


if __name__ == '__main__':
    main()
