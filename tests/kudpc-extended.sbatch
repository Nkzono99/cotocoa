#!/bin/bash
#SBATCH -p eb
#SBATCH -t 0:15:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-extended
#SBATCH -o build/cotocoa-extended.%j.out
#SBATCH -e build/cotocoa-extended.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
module list
.venv/bin/python tests/run_matrix.py
.venv/bin/python tests/run_typed_matrix.py
