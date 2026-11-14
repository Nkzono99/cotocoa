#!/bin/bash
#SBATCH -p gr20001a
#SBATCH -t 0:10:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-clean
#SBATCH -o build/cotocoa-clean.%j.out
#SBATCH -e build/cotocoa-clean.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
repo="$PWD"
destination="$repo/build/clean-check-$SLURM_JOB_ID"
mkdir -p "$destination/source" "$destination/wheels" "$destination/site"
git archive HEAD | tar -x -C "$destination/source"
source_tree="$destination/source"
cmake -S "$source_tree" -B "$source_tree/build" -DCMAKE_C_COMPILER=mpiicc -DCMAKE_Fortran_COMPILER=mpiifort
cmake --build "$source_tree/build" -j 2
ctest --test-dir "$source_tree/build" --output-on-failure
export PYTHONPATH="$source_tree/python"
"$repo/.venv/bin/python" -m pytest -q "$source_tree/tests"
"$repo/.venv/bin/pip" wheel "$source_tree" "$source_tree/ctca4py" --no-deps -w "$destination/wheels"
"$repo/.venv/bin/pip" install --no-deps --target "$destination/site" "$destination"/wheels/*.whl
export PYTHONPATH="$destination/site"
export COTOCOA_LIBRARY="$source_tree/build/libcotocoa.so"
cd "$destination"
srun -n 1 "$repo/.venv/bin/python" -c 'from pathlib import Path; import cotocoa,ctca4py; from ctca4py import CTCAWorker; from ctca4py.ctcaworker import CTCAWorker as W; assert W is CTCAWorker; assert Path(cotocoa.__file__).parent.parent.name=="site"; assert Path(ctca4py.__file__).parent.parent.name=="site"; W(); print("PASS installed wheels outside source tree")'
cat > "$destination/coupling.yaml" <<EOF
schema: 1
runtime:
  timeout_seconds: 60
programs:
  producer:
    command: ["$source_tree/build/tests/typed_fortran", producer, "$destination/coupling.yaml"]
    ranks: 2
  consumer:
    command: ["$repo/.venv/bin/python", "$source_tree/tests/typed.py", consumer, "$destination/coupling.yaml"]
    ranks: 2
connections:
  - name: field
    from: producer.field
    to: consumer.field
    dtype: float64
    shape: [3,5,7]
    global_shape: [6,5,7]
    axes: [x,y,z]
    mode: borrow
    buffer_slots: 2
EOF
export COTOCOA_TEST_DTYPE=1 COTOCOA_TEST_NDIM=3 COTOCOA_TEST_LOAN=1 COTOCOA_TEST_ASSEMBLE=1
"$repo/.venv/bin/python" -m cotocoa run "$destination/coupling.yaml" --launcher slurm --probe-first \
    --probe "$source_tree/build/cotocoa-probe" --coupler "$source_tree/build/cotocoa-coupler" \
    --run-dir "$destination/run"
"$repo/.venv/bin/python" -m cotocoa status "$destination/run" --json > "$destination/status.json"
echo 'PASS clean archive, native build, unit contracts, real wheels and borrowed Fortran/Python assembly'
