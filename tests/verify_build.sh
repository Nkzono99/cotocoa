#!/bin/bash
# Run inside a bounded compute allocation, from the repository root.
set -euo pipefail
cmake --build build -j 2
ctest --test-dir build --output-on-failure
.venv/bin/python -m pytest -q
.venv/bin/pip install -e . -e ./ctca4py --no-deps
cmake -S . -B build/c-only -DCOTOCOA_ENABLE_FORTRAN=OFF -DBUILD_TESTING=OFF -DCMAKE_C_COMPILER=mpiicc
cmake --build build/c-only -j 2
prefix="$PWD/build/install-$SLURM_JOB_ID"
relocated="$PWD/build/relocated-$SLURM_JOB_ID"
cmake --install build --prefix "$prefix"
mv "$prefix" "$relocated"
cmake -S tests/install-consumer -B "build/install-c-$SLURM_JOB_ID" -DCMAKE_PREFIX_PATH="$relocated" -DCMAKE_C_COMPILER=mpiicc
cmake --build "build/install-c-$SLURM_JOB_ID" -j 2
"build/install-c-$SLURM_JOB_ID/consumer_c"
cmake -S tests/install-consumer -B "build/install-f-$SLURM_JOB_ID" -DENABLE_FORTRAN=ON -DCMAKE_PREFIX_PATH="$relocated" -DCMAKE_C_COMPILER=mpiicc -DCMAKE_Fortran_COMPILER=mpiifort
cmake --build "build/install-f-$SLURM_JOB_ID" -j 2
"build/install-f-$SLURM_JOB_ID/consumer_fortran"
printf '%s\n' "$relocated" > build/verified-install-prefix
