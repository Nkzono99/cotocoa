from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import time
import uuid

from .config import ConfigError, load_config


def _binary(name, path=None):
    candidates=[path] if path else [shutil.which(name),Path(__file__).resolve().parents[2]/'build'/name]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate,os.X_OK):
            return str(Path(candidate).resolve())
    raise ConfigError(f'{name} is not executable; build/install it or pass its explicit path')


def _commands(plan,coupler,manifest):
    before=[(plan.programs[name]['ranks'],plan.programs[name]['command']) for name in plan.producers]
    after=[(plan.programs[name]['ranks'],plan.programs[name]['command']) for name in plan.consumers]
    return [*before,(1,[coupler,str(manifest)]),*after]


def _launch_command(launcher,commands,directory,ranks,phase):
    if launcher=='mpiexec':
        command=['mpiexec']
        for index,(count,program) in enumerate(commands):
            if index: command.append(':')
            command.extend(['-n',str(count),*program])
        return command
    script=directory/f'{phase}-dispatch.sh'
    dispatch=['#!/bin/sh','set -eu','rank=${SLURM_PROCID:?}']
    end=0
    for index,(count,program) in enumerate(commands):
        end+=count
        branch='if' if index==0 else 'elif'
        dispatch.extend([f'{branch} [ "$rank" -lt {end} ]; then',f'exec {shlex.join(program)}'])
    dispatch.extend(['else','echo "cotocoa: rank outside configured layout" >&2','exit 2','fi'])
    script.write_text('\n'.join(dispatch)+'\n',encoding='utf-8')
    return ['srun','--kill-on-bad-exit=1','-n',str(ranks),'/bin/sh',str(script)]


def _save(path,value):
    temporary=path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    temporary.replace(path)


def _terminate(process):
    if process.poll() is not None: return
    try:
        os.killpg(process.pid,signal.SIGTERM)
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try: os.killpg(process.pid,signal.SIGKILL)
        except ProcessLookupError: pass
        process.wait()
    except ProcessLookupError:
        process.wait()


def _status(directory,as_json):
    directory=Path(directory).resolve()
    if not (directory/'run.json').is_file():
        candidates=list(directory.glob('*/run.json'))
        if not candidates: raise ConfigError(f'no run records under {directory}')
        directory=max(candidates,key=lambda p:p.stat().st_mtime_ns).parent
    record=json.loads((directory/'run.json').read_text())
    record['directory']=str(directory)
    record['rank_status']=[json.loads(p.read_text()) for p in sorted(directory.glob('rank-*.json'))]
    record['missing_ranks']=sorted(set(range(record['ranks']))-{r['rank'] for r in record['rank_status']})
    if record['state'] in ('running','probing') and record['hostname']==socket.gethostname():
        try: os.kill(record['pid'],0)
        except ProcessLookupError: record['state']='controller-lost'
        except PermissionError: pass
    if as_json:
        print(json.dumps(record,indent=2))
    else:
        print(f"{record['state']}: {directory}")
        for rank in record['rank_status']:
            print(f"rank={rank['rank']} {rank['program']} {rank['phase']} step={rank['step']} "
                  f"sent={rank['sent']} received={rank['received']} completed={rank['completed']} dropped={rank['dropped']}")
            if rank['detail']: print(f"  {rank['detail']}")
        if record['missing_ranks']: print(f"ranks without a status record: {record['missing_ranks']}")
    return 0


def _run(plan,args):
    host=socket.gethostname().split('.')[0]
    if host.startswith(('laurel','camphor','cinnamon','gardenia','hpcfs')):
        raise ConfigError('run/probe must execute on compute resources, not a KUDPC login/transfer node')
    if args.launcher=='slurm' and not os.environ.get('SLURM_JOB_ID'):
        raise ConfigError('--launcher slurm requires an existing Slurm allocation')
    probe=_binary('cotocoa-probe',args.probe) if args.action=='probe' or args.probe_first else None
    coupler=_binary('cotocoa-coupler',args.coupler) if args.action=='run' else None
    directory=(args.run_dir or plan.config.parent/'.cotocoa'/'runs'/
               (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8])).resolve()
    directory.mkdir(parents=True,exist_ok=False)
    manifest=directory/'cotocoa.manifest'
    manifest.write_text(plan.manifest(),encoding='utf-8')
    record={'state':'probing' if probe else 'running','kind':args.action,'ranks':plan.ranks,
            'config':str(plan.config),'pid':os.getpid(),'hostname':socket.gethostname(),
            'slurm_job_id':os.environ.get('SLURM_JOB_ID'),'started_at':datetime.now(timezone.utc).isoformat(),
            'programs':plan.programs,'timeout_seconds':plan.runtime['timeout_seconds']}
    path=directory/'run.json'
    _save(path,record)
    print(f'cotocoa: run record: {directory}',flush=True)
    env=dict(os.environ,COTOCOA_MANIFEST=str(manifest),COTOCOA_RUN_DIR=str(directory))
    started=time.monotonic()
    timeout=plan.runtime['timeout_seconds']
    phases=[]
    if probe: phases.append(('probing',[(plan.ranks,[probe,str(manifest)])]))
    if coupler: phases.append(('running',_commands(plan,coupler,manifest)))
    process=None
    try:
        rc=0
        for phase,commands in phases:
            record['state']=phase
            command=_launch_command(args.launcher,commands,directory,plan.ranks,phase)
            record['launcher_command']=command; _save(path,record)
            remaining=max(0.001,timeout-(time.monotonic()-started)) if timeout else None
            process=subprocess.Popen(command,cwd=plan.config.parent,env=env,start_new_session=True)
            rc=process.wait(timeout=remaining)
            if rc: break
        record['state']='finished' if rc==0 else 'failed'
        record['exit_code']=rc
        return rc
    except subprocess.TimeoutExpired:
        record['state']='timeout'
        _terminate(process)
        raise ConfigError(f'run exceeded timeout_seconds={timeout}; inspect cotocoa status {directory}')
    except BaseException as exc:
        record['state']='interrupted' if isinstance(exc,KeyboardInterrupt) else 'failed'
        if process: _terminate(process)
        raise
    finally:
        record['finished_at']=datetime.now(timezone.utc).isoformat()
        record['elapsed_seconds']=time.monotonic()-started
        _save(path,record)


def main(argv=None):
    parser=argparse.ArgumentParser(prog='cotocoa')
    sub=parser.add_subparsers(dest='action',required=True)
    for action in ('check','plan','run','probe'):
        command=sub.add_parser(action)
        command.add_argument('config')
        if action=='plan': command.add_argument('--output',type=Path)
        if action in ('run','probe'):
            command.add_argument('--launcher',required=True,choices=('mpiexec','slurm'))
            command.add_argument('--coupler')
            command.add_argument('--probe')
            command.add_argument('--probe-first',action='store_true')
            command.add_argument('--run-dir',type=Path)
    command=sub.add_parser('status')
    command.add_argument('run_dir',nargs='?',default='.cotocoa/runs')
    command.add_argument('--json',action='store_true')
    args=parser.parse_args(argv)
    try:
        if args.action=='status': return _status(args.run_dir,args.json)
        plan=load_config(args.config)
        if args.action=='check':
            print(f"valid: {','.join(plan.producers)} -> {','.join(plan.consumers)}; {plan.ranks} ranks including Coupler")
        elif args.action=='plan':
            print(json.dumps({'ranks':plan.ranks,'producers':plan.producers,'consumers':plan.consumers,
                'programs':plan.programs,'connections':plan.connections,'coupler_ranks':1,
                'snapshot_bytes':plan.snapshot_bytes,'producer_buffer_bytes':plan.producer_buffer_bytes,
                'consumer_buffer_bytes':plan.consumer_buffer_bytes,'python_extra_copy_bytes_per_rank':plan.snapshot_bytes,
                'queue_capacity':plan.queue_capacity},indent=2))
            if args.output: args.output.write_text(plan.manifest(),encoding='utf-8')
        else: return _run(plan,args)
    except (ConfigError,OSError,ValueError) as exc:
        print(f'cotocoa: {exc}',file=sys.stderr)
        return 2
    return 0
