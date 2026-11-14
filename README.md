CoToCoA (Code To Code Adapter)
=====

CoToCoA is a framework to enable coupled computations, such as multi-scale simulations. Instead of using intermediate files, it enables direct communication among multiple programs.
Compared with similar frameworks, CoToCoA has the following advantages:
- One-directional flow
- Minimul modification to existing codes
- Asynchronous execution
- One-sided data transfer

In this framework, the requester submits requests of computations to the coupler. For each request, the coupler determines which worker program to ask for the computation, then enqueues it with the program ID. The worker with that ID retrieves the request from the queue and executes it.

Currently, CoToCoA supports Fortran and C. Also, a Python package for worker programs is available.
As the fundamental communication layer, it relies on the MPI library.

Developers of CoToCoA:
- Takeshi Nanri (Kyushu Univ., Japan)
- Yuto Katoh (Tohoku Univ., Japan)
- Keiichiro Fukazawa (Kyoto Univ., Japan)
- Yohei Miyake (Kobe Univ., Japan)
- Kazuya Nakazawa (Kobe Univ., Japan)
- Jingde Zhow (Kyoto Univ., Japan)
- Youhei Sunada (Kobe Univ., Japan)
- Haichao Zhao (Kyoto Univ., Japan)

## Fork development

The high-level API is on this fork's `main` branch. The existing default
`master` branch is retained. Clone with `git clone --branch main
https://github.com/Nkzono99/cotocoa.git` to use these additions.

This fork's Fortran, C, and Python interfaces and configuration-driven
coupling are described in [the high-level API design](docs/HIGH-LEVEL-API.md)
(Japanese). All three languages can act as producers or consumers, including
same-language connections.
The implementation provides fixed 1D–3D int32/float32/float64 snapshots, a native Coupler,
and `cotocoa check/plan/run`. See [the build and usage guide](docs/HIGH-LEVEL-USAGE.md)
and [runnable examples](examples/snapshot). Multiple ports, fan-out, multiple
producer programs, and typed task submission with worker groups are covered in
[Tasks and ports](docs/TASKS-AND-PORTS.md).
Streams support `all` and `latest` delivery, and both streams and tasks support
borrowed inputs; see [delivery and data lifetime](docs/DELIVERY-AND-SHARING.md).
Use the bounded [Assembler](docs/ASSEMBLY.md) for complete global frames, and
[probe/status and runtime diagnostics](docs/DIAGNOSTICS.md) to inspect jobs.
[Runnable task examples](examples/tasks) cover all three languages.

## Install

### ctca
fpm.toml

```toml
[dependencies]
ctca = { git = "https://github.com/Nkzono99/cotocoa.git" }
```

### High-level Fortran, C, and Python API

Build the native library with CMake and matching MPI C/Fortran compilers, then
install the Python package from this repository with `pip install -e .`.
For a C-only build, set `-DCOTOCOA_ENABLE_FORTRAN=OFF`. Native CMake install
exports `cotocoa::cotocoa` and the optional `cotocoa::cotocoa_fortran` target.
The native build and Python installation are separate steps; see the guide above.

### Legacy ctca4py

The original Worker interface remains under `ctca4py/`; its Python package
metadata is in that subdirectory. It is separate from the new `cotocoa` package.
Install it with `pip install ./ctca4py`; set `CTCA4PY_LIBRARY` to the matching
native library when it is outside the standard library search path.
