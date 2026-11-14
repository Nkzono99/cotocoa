#!/bin/bash
#SBATCH -p eb
#SBATCH -t 0:10:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-latest
#SBATCH -o build/cotocoa-latest.%j.out
#SBATCH -e build/cotocoa-latest.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
.venv/bin/python tests/run_latest_matrix.py
