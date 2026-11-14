#!/bin/bash
#SBATCH -p eb
#SBATCH -t 0:10:00
#SBATCH --rsc p=5:t=1:c=1
#SBATCH -J cotocoa-matrix
#SBATCH -o build/cotocoa-matrix.%j.out
#SBATCH -e build/cotocoa-matrix.%j.err

set -euo pipefail
module list
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1
# Submit from the repository root. The controller launches MPI cases via srun.
cd "${SLURM_SUBMIT_DIR:?}"
.venv/bin/python tests/run_matrix.py
