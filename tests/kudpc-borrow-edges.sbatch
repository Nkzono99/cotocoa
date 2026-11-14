#!/bin/bash
#SBATCH -p gr20001a
#SBATCH -t 0:05:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-borrow-edges
#SBATCH -o build/cotocoa-borrow-edges.%j.out
#SBATCH -e build/cotocoa-borrow-edges.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
.venv/bin/python tests/run_borrow_edges.py
