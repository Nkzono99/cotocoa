"""Validate all language pairs with nine typed ports, tasks and fan-out."""
import itertools
import os
from pathlib import Path
import sys
import yaml
from run_matrix import run_case


def main():
    if not os.environ.get('SLURM_JOB_ID'):
        raise SystemExit('requires a five-rank Slurm allocation')
    root=Path(__file__).resolve().parents[1]
    os.chdir(root)
    output=root/'build/multiplex-results'
    output.mkdir(exist_ok=True)
    commands={'c':[str(root/'build/tests/multiplex_c')],
              'fortran':[str(root/'build/tests/multiplex_fortran')],
              'python':[sys.executable,str(root/'tests/multiplex.py')]}
    cases=[(f'{kind}-{source}-to-{target}',source,target,kind,False,False)
           for kind in ('stream','task') for source,target in itertools.product(commands,repeat=2)]
    cases.extend([('fanout-c-to-fortran','c','fortran','stream',True,False),
                  ('multiple-producers-python-to-c','python','c','stream',False,True)])
    for label,source,target,kind,fanout,multiple in cases:
        path=output/f'{label}.yaml'
        producers={'producer':1,'producer2':1} if multiple else {'producer':2}
        consumers={'consumer':1,'other':1} if fanout else {'consumer':2}
        programs={name:{'command':[*commands[source],name,str(path)],'ranks':ranks}
                  for name,ranks in producers.items()}
        programs.update({name:{'command':[*commands[target],name,str(path)],'ranks':ranks,
                               'group_size':1 if kind=='task' else ranks} for name,ranks in consumers.items()})
        connections=[]
        for sender,receiver,dtype,ndim in itertools.product(producers,consumers,('float64','float32','int32'),(1,2,3)):
            code={'float64':1,'float32':2,'int32':3}[dtype]
            port=f'field_{code}_{ndim}'
            shape=[3,5,7][:ndim]
            connections.append({'name':f'{sender}_{receiver}_{code}_{ndim}',
                'from':f'{sender}.{port}','to':f'{receiver}.{port}','kind':kind,'dtype':dtype,
                'shape':shape,'global_shape':[producers[sender]*3,*shape[1:]],
                'axes':['x','y','z'][:ndim],'buffer_slots':2})
        path.write_text(yaml.safe_dump({'schema':1,'programs':programs,'connections':connections,
                                       'runtime':{'timeout_seconds':70}}))
        env=dict(os.environ)
        env.pop('COTOCOA_MANIFEST',None)
        env.pop('COTOCOA_TEST_TASK',None)
        if kind=='task': env['COTOCOA_TEST_TASK']='1'
        run_case(label,[sys.executable,'-m','cotocoa','run',str(path),'--launcher','slurm'],env,output)
        done=[]; workers=set()
        for line in (output/f'{label}.log').read_text().splitlines():
            parts=line.split()
            if parts and parts[0]=='DONE':
                assert len(parts)==8,(label,line)
                done.append((parts[1],parts[2],*map(int,parts[3:7])))
                workers.add(int(parts[7]))
        expected={(receiver,sender,rank,dtype,ndim,step)
                  for sender,receiver,dtype,ndim,step in itertools.product(producers,consumers,(1,2,3),(1,2,3),range(1,9))
                  for rank in range(producers[sender])}
        assert len(done)==len(expected) and set(done)==expected,(label,len(done),len(expected),expected-set(done))
        if kind=='task':
            assert len(workers)==2,(label,'both worker groups must process tasks',workers)
    print('All 20 multiplex/task/topology cases passed.',flush=True)


if __name__=='__main__':
    main()
