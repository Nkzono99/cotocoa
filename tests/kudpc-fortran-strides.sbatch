#!/bin/bash
#SBATCH -p gr20001a
#SBATCH -t 0:10:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-fortran-strides
#SBATCH -o build/cotocoa-fortran-strides.%j.out
#SBATCH -e build/cotocoa-fortran-strides.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
export COTOCOA_TEST_SOURCE=fortran
COTOCOA_TEST_ASSEMBLE=1 .venv/bin/python tests/run_typed_matrix.py
.venv/bin/python tests/run_borrow_matrix.py
