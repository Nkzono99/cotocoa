# Legacy Python Worker interface

Install with `python -m pip install ./ctca4py` from the repository root.
Build the matching native library using the root CMake project. Set
`CTCA4PY_LIBRARY=/absolute/path/to/libcotocoa.so` if it is outside the standard
library search path. `COTOCOA_LIBRARY` is accepted too. Existing standalone
`ctca4py.so` builds remain usable when they expose the same legacy ABI.

Use `from ctca4py import CTCAWorker` or the original
`from ctca4py.ctcaworker import CTCAWorker`. This package preserves the low-level
Worker API; the `cotocoa` package provides the new named ports and all three
language roles. Build mpi4py and the native library against the same MPI.
