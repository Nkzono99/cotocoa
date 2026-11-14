#!/bin/bash
#SBATCH -p gr20001a
#SBATCH -t 0:20:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-final
#SBATCH -o build/cotocoa-final.%j.out
#SBATCH -e build/cotocoa-final.%j.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
.venv/bin/python tests/run_matrix.py
.venv/bin/python tests/run_typed_matrix.py
.venv/bin/python tests/run_multiplex_matrix.py
.venv/bin/python tests/run_latest_matrix.py
