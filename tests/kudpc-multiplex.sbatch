#!/bin/bash
#SBATCH -p eb
#SBATCH -t 0:15:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-multiplex
#SBATCH -o build/cotocoa-multiplex.%j.out
#SBATCH -e build/cotocoa-multiplex.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
module list
.venv/bin/python tests/run_multiplex_matrix.py
