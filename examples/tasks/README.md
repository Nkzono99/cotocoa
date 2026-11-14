# Task example

Build the root CMake project and install `cotocoa` into `.venv` as described in
the main usage guide. In a five-rank allocation, run this controller once:

```sh
cd examples/tasks
../../.venv/bin/cotocoa run tasks.yaml --launcher slurm --probe-first
```

The two producers submit ten independent three-element arrays each. The two
one-rank worker groups print their sums and acknowledge processing. Tickets
wait for processing completion; they do not carry a return value. Source order
is preserved for streams, while task completion order depends on the workers.

Replace commands with `../../build/task_c producer`,
`../../build/task_fortran consumer`, or `../../.venv/bin/python python.py producer`
to use any of the three languages in either role. All three examples use the
same port schema and native Coupler. Increase worker ranks to four and
`group_size` to two for two collective worker groups in a seven-rank allocation.
