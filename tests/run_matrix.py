"""Run once inside a five-rank Slurm batch allocation (never on a login node).

Each case starts its own srun step. The controller is NOT launched via srun.
"""
from __future__ import annotations

import argparse
import itertools
import os
from pathlib import Path
import signal
import subprocess
import sys

import yaml

def run_case(label, command, env, output, expected_failure=False):
    log = output / f"{label}.log"
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        try:
            rc = process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise AssertionError(f"{label}: timeout; see {log}")
    text = log.read_text(errors="replace")
    if expected_failure:
        assert rc != 0 and "cotocoa:" in text and "injected consumer failure" in text, (label, rc, text)
    else:
        assert rc == 0, (label, rc, text)
        assert "PASS" in text, (label, text)
    print(f"PASS {label} (exit {rc})", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="build/test-results")
    args = parser.parse_args()
    if not os.environ.get("SLURM_JOB_ID"):
        parser.error("requires an existing five-rank Slurm allocation")
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    executables = {"c": [str(root / "build/tests/coupling_c")],
                   "fortran": [str(root / "build/tests/coupling_fortran")],
                   "python": [sys.executable, str(root / "tests/coupling.py")]}
    for sender, receiver in itertools.product(executables, repeat=2):
        label = f"{sender}-to-{receiver}"
        config = {"schema": 1, "programs": {
            "producer": {"command": [*executables[sender], "producer"], "ranks": 2},
            "consumer": {"command": [*executables[receiver], "consumer"], "ranks": 2}},
            "connections": [{"name": "potential", "from": "producer.field", "to": "consumer.field",
                             "dtype": "float64", "shape": [3, 5, 7], "global_shape": [6, 5, 7],
                             "axes": ["x", "y", "z"], "units": "V", "time_unit": "s", "buffer_slots": 2}]}
        if sender == "c" and receiver == "fortran":
            config["connections"][0]["name"] = "p"*63
        # Programs receive the source config; the launcher supplies the native manifest.
        path = output / f"{label}.yaml"
        for program in config["programs"].values():
            program["command"].append(str(path))
        path.write_text(yaml.safe_dump(config))
        command = [sys.executable, "-m", "cotocoa", "run", str(path), "--launcher", "slurm"]
        env = dict(os.environ)
        env.pop("COTOCOA_MANIFEST", None)
        env.pop("COTOCOA_TEST_BORROWED", None)
        env.pop("COTOCOA_TEST_STEPS", None)
        env.pop("COTOCOA_TEST_FAIL", None)
        run_case(label, command, env, output)
        if sender == receiver == "c":
            run_case("zero-messages", command, dict(env, COTOCOA_TEST_STEPS="0"), output)
            run_case("borrowed-native-mpi", command, dict(env, COTOCOA_TEST_BORROWED="1"), output)
        if sender == "c" and receiver == "python":
            run_case("consumer-exception", command, dict(env, COTOCOA_TEST_FAIL="1"), output,
                     expected_failure=True)
    # Keep the legacy attached-payload and RMA paths in the regression surface.
    dispatch = output / "legacy-dispatch.sh"
    legacy = root / "build/tests/legacy_c"
    dispatch.write_text(f'''#!/bin/sh
set -eu
if [ "$SLURM_PROCID" -lt 2 ]; then
  exec "{legacy}" producer
elif [ "$SLURM_PROCID" -eq 2 ]; then
  exec "{legacy}" coupler
else
  exec "{legacy}" consumer
fi
''')
    for borrowed in (False, True):
        env = dict(os.environ)
        env.pop("COTOCOA_TEST_BORROWED", None)
        if borrowed:
            env["COTOCOA_TEST_BORROWED"] = "1"
        run_case(f"legacy-{'borrowed' if borrowed else 'owned'}",
                 ["srun", "/bin/sh", str(dispatch)], env, output)
    print("All language pairs and lifecycle regressions passed.", flush=True)


if __name__ == "__main__":
    main()
