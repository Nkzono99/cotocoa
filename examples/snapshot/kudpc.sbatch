#!/bin/bash
#SBATCH -p eb
#SBATCH -t 0:05:00
#SBATCH --rsc p=4:t=1:c=1
#SBATCH -J cotocoa-snapshot
#SBATCH -o build/cotocoa-snapshot.%j.out
#SBATCH -e build/cotocoa-snapshot.%j.err

# The job uses the modules (compiler and MPI) loaded when it was submitted.
# 4 processes: 2 producer + 1 Coupler + 1 consumer.  Submit from the repository root:
#   sbatch -p <queue> examples/snapshot/kudpc.sbatch
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:?}"
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1
.venv/bin/python -m cotocoa run examples/snapshot/cotocoa.yaml --launcher slurm
