# インストール

[ドキュメント目次](README.md) ｜ 次：[チュートリアル](TUTORIAL.md)

CoToCoAは2つの部品でできています。両方を用意すると使える状態になります。

| 部品 | 中身 | 用意する方法 |
| --- | --- | --- |
| ネイティブライブラリ | `libcotocoa.so`、Coupler（`cotocoa-coupler`）、Fortranモジュール | CMakeでビルドする |
| Pythonパッケージ | `cotocoa` コマンドと、PythonからCoToCoAを使うためのAPI | `pip` で入れる |

FortranやCだけで連携する場合も、起動に `cotocoa` コマンドを使うため、Pythonパッケージを入れてください。

## 必要なもの

| ソフトウェア | 要件 |
| --- | --- |
| MPI | MPI 3以降。`mpicc` などC用のコンパイララッパーが必要 |
| Fortranコンパイラ | Fortranから使う場合に必要。`mpifort` などMPI用のラッパーも用意する |
| CMake | 3.16以降 |
| Python | 3.10以降 |

ライブラリのビルド、自分のプログラムのビルド、Pythonのmpi4pyには、**すべて同じMPI**を使ってください。
異なるMPIが混ざると、起動時にエラーになるか、通信が正しく行われません。

動作を確認している環境は、京都大学スーパーコンピュータ（KUDPC）System Aの
Intelコンパイラ・Intel MPI 2023.2・Python 3.12です。

スーパーコンピュータでは、コンパイルもMPIプログラムの実行も、その施設の決まりに従って行ってください。
KUDPCではログインノードで実行せず、`tssrun` やバッチジョブで計算ノードを使います。

## 手順1：ソースを取得する

```sh
git clone https://github.com/Nkzono99/cotocoa.git
cd cotocoa
```

以降のコマンドは、この `cotocoa` ディレクトリで実行します。

## 手順2：ライブラリをビルドする

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpicc \
  -DCMAKE_Fortran_COMPILER=mpifort
cmake --build build -j 2
```

コンパイラ名は環境に合わせて置き換えます。
Intel MPIでは `mpiicc`・`mpiifort` です。

ビルドが終わると、`build/` に次のものができます。

| ファイル | 用途 |
| --- | --- |
| `libcotocoa.so` | ライブラリ本体 |
| `cotocoa-coupler` | 配送を中継するCoupler。`cotocoa run` が自動で起動する |
| `cotocoa-probe` | MPI環境の起動確認用プログラム。`cotocoa probe` が使う |
| `snapshot_fortran`・`snapshot_c` など | [チュートリアル](TUTORIAL.md)で動かすサンプル |

Fortranを使わない場合は、最初の `cmake` に `-DCOTOCOA_ENABLE_FORTRAN=OFF` を加えます。
この場合はFortranのサンプルが作られないので、チュートリアルではCまたはPythonのサンプルを使ってください。

## 手順3：Pythonパッケージを入れる

```sh
python -m venv .venv
.venv/bin/python -m pip install -e .
```

依存パッケージのNumPy・mpi4py・PyYAMLも一緒に入ります。

mpi4pyは、手順2で使ったのと同じMPIを使っている必要があります。
`pip` が入れたmpi4pyがどのMPIを使っているかは、次のコマンドで確認できます
（MPIを初期化するので、MPIプログラムを実行してよい場所で実行してください）。

```sh
.venv/bin/python -c "from mpi4py import MPI; print(MPI.Get_library_version())"
```

表示されたMPIが手順2のものと違う場合は、そのMPIのコンパイララッパーを指定して、mpi4pyをソースから入れ直します。

```sh
MPICC=mpicc .venv/bin/python -m pip install --force-reinstall --no-cache-dir --no-binary=mpi4py mpi4py
```

施設がmpi4pyの入れ方を指定している場合は、その手順に従ってください。

`pip install` はネイティブライブラリをビルドしません。手順2も必要です。

## 手順4：確認する

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
```

次のように表示されれば、コマンドとサンプルの準備ができています。

```text
valid: producer -> consumer; 4 ranks including Coupler
```

`check` は設定ファイルを調べるだけで、MPIプログラムは起動しません。ログインノードでも実行できます。
実際に動かす手順は[チュートリアル](TUTORIAL.md)へ進んでください。

## 自分のプロジェクトから使う

ここまでの手順で、リポジトリの中のサンプルは動かせます。
別のディレクトリにある自分のプロジェクトから使うには、次の2つを行います。

### cotocoaコマンドとPythonを使えるようにする

`cotocoa` コマンドと、`cotocoa` パッケージの入ったPythonは、手順3で作った仮想環境（`.venv`）の中にあります。
仮想環境を有効にすると、どのディレクトリからでも使えるようになります。

```sh
source /path/to/cotocoa/.venv/bin/activate
```

有効にしない場合は、`/path/to/cotocoa/.venv/bin/cotocoa` のように絶対パスで呼び出します。
ジョブスクリプトの中でも、同じようにどちらかを行ってください。

Pythonのプログラムだけで連携する場合は、これで準備は終わりです。

### CやFortranのプログラムをビルドできるようにする

CやFortranのプログラムからリンクできるように、ライブラリを決まった場所へインストールします。

```sh
cmake --install build --prefix "$HOME/.local/cotocoa"
```

インストール先には、次のものが置かれます。

| 場所 | 内容 |
| --- | --- |
| `include/` | Cのヘッダー（`cotocoa.h`） |
| `include/cotocoa/` | Fortranのモジュールファイル（`cotocoa.mod`） |
| `lib/`（環境によっては `lib64/`） | `libcotocoa.so`、`libcotocoa_fortran.a` |
| `bin/` | `cotocoa-coupler`、`cotocoa-probe` |

**CMakeを使う場合**は、次のターゲットにリンクします。

| 言語 | `CMakeLists.txt` に書く内容 | リンクするターゲット |
| --- | --- | --- |
| C | `find_package(cotocoa 0.3 REQUIRED)` | `cotocoa::cotocoa` |
| Fortran | `find_package(cotocoa 0.3 REQUIRED COMPONENTS Fortran)` | `cotocoa::cotocoa_fortran` |

自分のプロジェクトの `cmake` 実行時に `-DCMAKE_PREFIX_PATH="$HOME/.local/cotocoa"` を渡すと、
インストールしたCoToCoAが見つかります。
`CMakeLists.txt` の全体は、[Fortran](GUIDE-FORTRAN.md#ビルドする)・[C](GUIDE-C.md#ビルドする)のガイドにあります。

**CMakeを使わない場合**（Makefileなど）は、コンパイルとリンクのオプションを次のように指定します。
`lib` の部分は、インストール先に合わせて `lib64` に読み替えてください。

```sh
PREFIX="$HOME/.local/cotocoa"

# Fortran
mpifort -I"$PREFIX/include/cotocoa" simulation.f90 \
  -L"$PREFIX/lib" -lcotocoa_fortran -lcotocoa -Wl,-rpath,"$PREFIX/lib" -o simulation

# C
mpicc -I"$PREFIX/include" simulation.c \
  -L"$PREFIX/lib" -lcotocoa -Wl,-rpath,"$PREFIX/lib" -o simulation
```

Fortranでは、`-lcotocoa_fortran` を `-lcotocoa` より前に書きます。
`-Wl,-rpath,...` は、実行時に `libcotocoa.so` を見つけるための指定です。

Fortranのモジュールファイル（`.mod`）はコンパイラごとに形式が異なります。
自分のプログラムは、CoToCoAをビルドしたのと同じコンパイラでビルドしてください。

### リポジトリのbuild/を使えない場合

`cotocoa` コマンドとPython APIは、Couplerとライブラリを、リポジトリの `build/` から自動で見つけます。
リポジトリの `build/` を消した場合や、`pip install -e .` ではなく通常の `pip install .` で入れた場合は、
インストール先を環境変数で知らせてください。

```sh
export PATH="$HOME/.local/cotocoa/bin:$PATH"
export COTOCOA_LIBRARY="$HOME/.local/cotocoa/lib/libcotocoa.so"
```

| 環境変数 | 何のために必要か |
| --- | --- |
| `PATH` | `cotocoa` コマンドが `cotocoa-coupler` と `cotocoa-probe` を見つけるため |
| `COTOCOA_LIBRARY` | Python APIが `libcotocoa.so` を読み込むため |

インストール先のディレクトリ名が `lib64` の環境では、`COTOCOA_LIBRARY` のパスも `lib64` に合わせてください。

## KUDPCでの例

KUDPCでは、既定で読み込まれているIntelコンパイラとIntel MPIの環境（`PrgEnvIntel`）をそのまま使えます。
コンパイララッパーは `mpiicc`・`mpiifort` です。
使うシステム（System Aなど）のモジュールに切り替えた状態で、ビルドとジョブの投入を行います。
ビルドは計算ノードで行います。キュー名は自分が使えるものに替えてください。

```sh
tssrun -p キュー名 -t 0:10:00 --rsc p=1:t=2:c=2 bash -c '
  cmake -S . -B build -DCMAKE_C_COMPILER=mpiicc -DCMAKE_Fortran_COMPILER=mpiifort &&
  cmake --build build -j 2'
python3.12 -m venv .venv
.venv/bin/python -m pip install -e .
```

`sbatch` で投入したジョブは、投入したときに読み込んでいたモジュールの環境を引き継ぎます。
ビルドしたときと同じ環境で投入してください。

## 従来APIのPython Workerを使う場合

従来APIのWorkerをPythonで書くための `ctca4py` は、別のパッケージです。
[ctca4pyの使い方](legacy/PYTHON-WORKER.md)を参照してください。
