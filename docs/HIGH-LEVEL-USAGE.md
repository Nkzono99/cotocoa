# Fortran・C・Python の名前付き配列連携

高水準 API 0.3 は、3言語すべての送信・受信に対応する。
`int32/float32/float64` の固定1〜3次元配列を名前付き port で転送する。
snapshot/borrow、stream の all/latest、Task と複数 worker group を提供する。
複数接続と Task API は [TASKS-AND-PORTS.md](TASKS-AND-PORTS.md) を参照する。
型や領域分割の異なる配列の自動変換、双方向・循環連成は対象に含めない。
非連続配列は送信時に packing する。C/Python は正負の byte stride に対応し、
Fortran の generic send/copy_to は3型×3次元の overload を提供する。
寿命は [配送と保持](DELIVERY-AND-SHARING.md)、全体の配列は [Assembler](ASSEMBLY.md)、
起動確認と状態照会は [診断](DIAGNOSTICS.md) を参照する。

## ビルドと導入

このforkの高水準APIは `main` ブランチにある。既定の `master` とは分けて取得する。

```sh
git clone --branch main https://github.com/Nkzono99/cotocoa.git
cd cotocoa
```

C と Fortran の MPI 3 以降のコンパイラ、CMake 3.16 以降、Python 3.10 以降が必要。
各 binding とバックエンドは同じ MPI 実装を使う。
次は Intel MPI のビルド例。Open MPI 等では対応する `mpicc/mpifort` を指定する。
KUDPC のビルド・テストは、利用可能なキューの計算ノード内で実行する。

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpiicc -DCMAKE_Fortran_COMPILER=mpiifort
cmake --build build -j 2
python -m venv .venv
.venv/bin/pip install -e .
cmake --install build --prefix /your/install/prefix
```

Python package のインストールと native library のビルドは別の操作。
`pip install` だけで native library がビルドされるとは扱わない。
開発 checkout では `build/libcotocoa.so` を探索する。
インストールしたライブラリを使う場合は `COTOCOA_LIBRARY` に共有ライブラリの
パスを指定するか、システムの共有ライブラリ探索先へ配置する。
native installのbinをPATHに加えるか、run/probeの `--coupler/--probe` に実行ファイルを指定する。
ライブラリの導入先はCMakeの表示するlibまたはlib64へ合わせる。

C 利用者は `cotocoa.h` と `libcotocoa`、Fortran 利用者は生成された module と
`libcotocoa_fortran`、`libcotocoa` をリンクする。リポジトリ内の CMake target
`cotocoa` と `cotocoa_fortran` が include とリンク情報を持つ。
インストール後は `find_package(cotocoa 0.3 REQUIRED)` と
`cotocoa::cotocoa`、Fortran は `find_package(cotocoa 0.3 REQUIRED COMPONENTS Fortran)` と
`cotocoa::cotocoa_fortran` を使う。Fortran module はビルド時と互換なコンパイラが必要。
`-DCOTOCOA_ENABLE_FORTRAN=OFF` なら Fortran コンパイラなしで C 層、Coupler、probe をビルドできる。

## 最小例を起動する

[examples/snapshot](../examples/snapshot) に各言語の送受信プログラムがある。
初期設定は Fortran→Fortran、producer 2 + Coupler 1 + consumer 1 の 4 rank。

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
.venv/bin/cotocoa plan examples/snapshot/cotocoa.yaml \
  --output build/snapshot.manifest
```

check と plan は MPI を初期化せず、設定で指定されたプログラムも起動しない。
plan の表示には rank 数、送信枠の保持量、受信バッファ量を含む。
MPI 自身のメモリや利用者が保持するコピーは別途必要となる。

通常の MPI 環境で、計算資源が使用できる場所から次のように起動する。

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher mpiexec
```

既に確保した Slurm allocation では、controller を 1 回だけ実行する。
設定の総 rank 数に対応する資源を確保した batch script 内で次を実行する。
controller を `srun` で rank 数だけ複製しない。
[examples/snapshot/kudpc.sbatch](../examples/snapshot/kudpc.sbatch) はこの例の
4 rank 用の script。リポジトリ直下から利用可能なキューを指定して submit する。

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher slurm
```

この profile は設定の総 rank 数を `srun -n` に指定する。
設定の総 rank 数以上のタスクを allocation に確保する。
各 rank に役割を割り当てる shell script と native manifest を共有ディレクトリへ
生成し、`.cotocoa/runs/<ID>/` に実行記録とともに残す。
`cotocoa status DIR` で状態を照会する。KUDPC の login/transfer node 上の run は拒否する。
資源の予約や batch submit は CLI の外で行う。

## 言語を差し替える

`examples/snapshot/cotocoa.yaml` の command だけを次から選ぶ。
下のパスはこの設定ファイルのディレクトリからの相対パス。

| 言語 | producer の command | consumer の command |
| --- | --- | --- |
| Fortran | `["../../build/snapshot_fortran", "producer"]` | `["../../build/snapshot_fortran", "consumer"]` |
| C | `["../../build/snapshot_c", "producer"]` | `["../../build/snapshot_c", "consumer"]` |
| Python | `["../../.venv/bin/python", "python.py", "producer"]` | `["../../.venv/bin/python", "python.py", "consumer"]` |

Fortran/C のプログラムだけを起動する場合は、生成済み native manifest のパスを
`COTOCOA_MANIFEST` に設定し、既存の MPMD launcher で producer、標準 Coupler、
consumer をこの順序に配置する。Coupler は `build/cotocoa-coupler`。
Python は manifest の事前生成や CLI に使えるが、MPI プロセスとしては不要。
native API の config 引数へ manifest のパスを直接渡す方法にも対応する。

初版では native API 自身は YAML を解析しない。config が YAML を指す場合は、
launcher が `COTOCOA_MANIFEST` で対応する事前生成 manifest を渡す必要がある。
設定を変更した場合は manifest を再生成する。

## 送信と受信の契約

send はライブラリ所有の送信枠へコピーする。戻った後で元配列を上書きできる。
受信側の処理が完了するまで送信枠を保持し、枠が満杯なら次の send が待機する。
flush と producer の close は、自 rank の全送信が処理済み、またはlatestで未配送破棄済みに
なるまで待つ。borrowの入力は対応するticketの完了まで保持・変更禁止とする。

受信したメッセージは、処理後に必ず complete する。
Fortran は `message%complete()`、C は `cotocoa_complete()` と
`cotocoa_message_release()`、Python は `with message:` の正常終了を使う。
処理前の解放、未完了のまま次を受信、ストリーム終端前の consumer の close は
診断付きの失敗となる。Python の処理例外もジョブを失敗させる。

受信コピーの寿命は通信上の完了と別に管理する。
Fortran の `copy_to` は allocatable 配列へコピーし、complete が native Message を
解放する。C の view は `cotocoa_message_release` まで有効。
Python は NumPy 所有の読み取り専用配列へコピーするため、保存した配列は
Context の終了後も有効。Python 受信にはこの追加コピーがある。
Fortran の Context/Message と C の owning handle をコピーして二重に完了・解放しない。

配列の論理 shape と軸順は全言語で共通。C/F のメモリ配置は descriptor に従って
変換し、C 受信側の view は付属の byte stride で参照する。
offset は全言語で 0 始まりの global 座標。各 producer rank の配列は個別に届き、
同じ step の全領域を自動的に gather しない。

計算コード自身の collective には Context の communicator を使う。
MPMD 全体の `MPI_COMM_WORLD` をそのまま計算の communicator として使わない。
MPI を Context が初期化した場合だけ close で終了し、借用した MPI は終了しない。
close 後に他の役割が MPI を終了している場合、`MPI_COMM_WORLD` の collective に
他の役割が参加することは前提にできない。

## 検証

設定と import の検証は次で実行する。計算ノード上でビルドとは別に実行できる。

```sh
.venv/bin/pip install pytest
.venv/bin/python -m pytest -q
```

検証済み環境はKUDPC System A、Intel/Intel MPI、Python 3.12。
最終回帰70 MPIケース、borrow54、Assembler27、Fortranの正負stride27、
診断・互換性14ケースと37単体テストを通過した。
C-only、C/Fortranのrelocatable install、fpm、clean archiveからの実wheelとMPI連携も検証した。
別のMPI実装やlauncherの実機検証は、この結果に含めない。

KUDPCのMPIテストは5 rankのallocation内でcontrollerを1回だけ起動する。
[tests/kudpc-final.sbatch](../tests/kudpc-final.sbatch) をはじめ、機能ごとのscriptを用意している。
実行方法、各テストの範囲、最終ジョブの結果は [検証記録](VALIDATION.md) を参照する。
