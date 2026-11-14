from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile

from .config import ConfigError, load_config


def _coupler(path=None):
    candidates = [path] if path else [shutil.which("cotocoa-coupler"),
                                      Path(__file__).resolve().parents[2] / "build" / "cotocoa-coupler"]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return str(Path(candidate).resolve())
    raise ConfigError("cotocoa-coupler is not executable; build it or pass --coupler")


def _commands(plan, coupler, manifest):
    before=[(plan.programs[name]['ranks'],plan.programs[name]['command']) for name in plan.producers]
    after=[(plan.programs[name]['ranks'],plan.programs[name]['command']) for name in plan.consumers]
    return [*before,(1,[coupler,str(manifest)]),*after]


def main(argv=None):
    parser = argparse.ArgumentParser(prog="cotocoa")
    sub = parser.add_subparsers(dest="action", required=True)
    for action in ("check", "plan", "run"):
        command = sub.add_parser(action)
        command.add_argument("config")
        if action == "plan":
            command.add_argument("--output", type=Path)
        if action == "run":
            command.add_argument("--launcher", required=True, choices=("mpiexec", "slurm"))
            command.add_argument("--coupler")
    args = parser.parse_args(argv)
    try:
        plan = load_config(args.config)
        if args.action == "check":
            print(f"valid: {','.join(plan.producers)} -> {','.join(plan.consumers)}; {plan.ranks} ranks including Coupler")
        elif args.action == "plan":
            print(json.dumps({"ranks": plan.ranks, "producers": plan.producers,
                              "consumers": plan.consumers, "programs": plan.programs,
                              "connections": plan.connections,
                              "coupler_ranks": 1, "snapshot_bytes": plan.snapshot_bytes,
                              "producer_buffer_bytes": plan.producer_buffer_bytes,
                              "consumer_buffer_bytes": plan.consumer_buffer_bytes,
                              "python_extra_copy_bytes_per_rank": plan.snapshot_bytes,
                              "queue_capacity": plan.queue_capacity},
                             indent=2))
            if args.output:
                args.output.write_text(plan.manifest(), encoding="utf-8")
        else:
            # Run is a controller: invoke once in an allocation, not once/rank.
            host = socket.gethostname().split(".")[0]
            if host.startswith(("laurel", "camphor", "cinnamon", "gardenia", "hpcfs")):
                raise ConfigError("run must execute on compute resources, not a KUDPC login/transfer node")
            if args.launcher == "slurm" and not os.environ.get("SLURM_JOB_ID"):
                raise ConfigError("--launcher slurm requires an existing Slurm allocation")
            coupler = _coupler(args.coupler)
            # Shared working directory: compute nodes cannot rely on login /tmp.
            with tempfile.TemporaryDirectory(prefix=".cotocoa-", dir=plan.config.parent) as directory:
                manifest = Path(directory) / "cotocoa.manifest"
                manifest.write_text(plan.manifest(), encoding="utf-8")
                commands = _commands(plan, coupler, manifest)
                env = dict(os.environ, COTOCOA_MANIFEST=str(manifest))
                if args.launcher == "mpiexec":
                    command = ["mpiexec"]
                    for index, (ranks, program) in enumerate(commands):
                        if index:
                            command.append(":")
                        command.extend(["-n", str(ranks), *program])
                else:
                    script = Path(directory) / "dispatch.sh"
                    dispatch=['#!/bin/sh','set -eu','rank=${SLURM_PROCID:?}']
                    end=0
                    for index,(ranks,program) in enumerate(commands):
                        end+=ranks
                        branch='if' if index==0 else 'elif'
                        dispatch.extend([f'{branch} [ "$rank" -lt {end} ]; then',f'exec {shlex.join(program)}'])
                    dispatch.extend(['else','echo "cotocoa: rank outside configured layout" >&2','exit 2','fi'])
                    script.write_text('\n'.join(dispatch)+'\n',encoding='utf-8')
                    command = ["srun", "--kill-on-bad-exit=1", "/bin/sh", str(script)]
                process=subprocess.Popen(command,cwd=plan.config.parent,env=env,start_new_session=True)
                try:
                    return process.wait(timeout=plan.runtime['timeout_seconds'] or None)
                except subprocess.TimeoutExpired:
                    import signal
                    os.killpg(process.pid,signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid,signal.SIGKILL)
                        process.wait()
                    raise ConfigError(f"run exceeded timeout_seconds={plan.runtime['timeout_seconds']}")
    except (ConfigError, OSError, ValueError) as exc:
        print(f"cotocoa: {exc}", file=sys.stderr)
        return 2
    return 0
