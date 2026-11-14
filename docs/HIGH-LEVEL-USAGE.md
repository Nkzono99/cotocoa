# Fortran・C・Python の名前付き配列連携

高水準 API の最初の実装は、3 言語すべての送信・受信に対応する。
`int32/float32/float64` の固定 1〜3 次元配列を、一方向の `snapshot/all/shards` として転送する。
型や領域分割の異なる配列の自動変換、双方向連成、Task API は後続の実装となる。
非連続配列は送信時に packing する。C/Python は正負の byte stride に対応し、
Fortran の generic send/copy_to は3型×3次元の overload を提供する。

## ビルドと導入

C と Fortran の MPI コンパイラ、CMake 3.16 以降、Python 3.10 以降が必要。
各 binding とバックエンドは同じ MPI 実装を使う。
次は Intel MPI のビルド例。Open MPI 等では対応する `mpicc/mpifort` を指定する。
KUDPC のビルド・テストは、利用可能なキューの計算ノード内で実行する。

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpiicc -DCMAKE_Fortran_COMPILER=mpiifort
cmake --build build -j 2
python -m venv .venv
.venv/bin/pip install -e .
```

Python package のインストールと native library のビルドは別の操作。
`pip install` だけで native library がビルドされるとは扱わない。
開発 checkout では `build/libcotocoa.so` を探索する。
インストールしたライブラリを使う場合は `COTOCOA_LIBRARY` に共有ライブラリの
パスを指定するか、システムの共有ライブラリ探索先へ配置する。

C 利用者は `cotocoa.h` と `libcotocoa`、Fortran 利用者は生成された module と
`libcotocoa_fortran`、`libcotocoa` をリンクする。リポジトリ内の CMake target
`cotocoa` と `cotocoa_fortran` が include とリンク情報を持つ。

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

この profile は `srun` が割り当て済みの全 rank を起動する前提。
確保したタスク数と設定の総 rank 数は一致させる。
各 rank に役割を割り当てる shell script と native manifest を共有ディレクトリへ
生成し、ジョブ終了後に削除する。KUDPC の login/transfer node 上の run は拒否する。
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
flush と producer の close は、自 rank の全送信の処理完了を待つ。

受信したメッセージは、処理後に必ず complete する。
Fortran は `message%complete()`、C は `cotocoa_complete()` と
`cotocoa_message_release()`、Python は `with message:` の正常終了を使う。
処理前の解放、未完了のまま次を受信、ストリーム終端前の consumer の close は
診断付きの失敗となる。Python の処理例外もジョブを失敗させる。

受信コピーの寿命は通信上の完了と別に管理する。
Fortran の `copy_to` は allocatable 配列へコピーし、complete が native Message を
解放する。C の view は `cotocoa_message_release` まで有効。
Python は NumPy 所有の読み取り専用配列へコピーするため、保存した配列は
Context の終了後も有効。初版の Python 受信にはこの追加コピーがある。
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

KUDPC の言語間テストは、5 rank の allocation に対して controller を 1 回起動する。
[tests/kudpc-matrix.sbatch](../tests/kudpc-matrix.sbatch) がその例。
利用できるシステム・キューを確認し、必要なら `sbatch -p <queue>` で指定する。
全9通り、2 producer rank、2 consumer rank、100 件/rank、snapshot 保持と C/F 順、
0 件、MPI 借用、受信側の例外、既存 C API の転送と RMA を検証する。
各ケースのログと設定は `build/test-results/` に出力する。

初期実装の検証環境は KUDPC System A、Intel/Intel MPI、Python 3.12。
設定・起動経路のテスト19件と、全9通りの言語間転送を確認した。
別の MPI 実装や launcher の実機検証は、この結果には含まない。

2026-11-14 の検証はリポジトリ直下で、`module switch SysB SysA` 後に実行した。
ビルドと設定テストの最終確認はジョブ `9175524`、終了コード 0。

```sh
tssrun -p gr20001a -t 0:10:00 --rsc p=1:t=2:c=2 \
  bash -lc 'set -e; cd /LARGE0/gr20001/b36291/Github/cotocoa; cmake --build build -j 2; .venv/bin/python -m pytest -q'
sbatch -p gr20001a tests/kudpc-matrix.sbatch
sbatch -p gr20001a examples/snapshot/kudpc.sbatch
```

MPI テストの最終ジョブ `9175525` は、script 指定の `-t 0:10:00`、
`--rsc p=5:t=1:c=1` で全9通りと5つの回帰ケースを通過し、終了コード 0。
Slurm profile は `srun --kill-on-bad-exit=1 /bin/sh <dispatch>` で各ケースを起動する。
受信側例外のケースは意図した非ゼロ終了と診断を確認して合格とする。
最小の Fortran→Fortran 例はジョブ `9175533`、`-t 0:05:00`、
`--rsc p=4:t=1:c=1` で20メッセージを処理し、終了コード 0。

型・次元拡張はビルドジョブ `9175619` で設定テスト28件を通過した。
`sbatch -p gr20001a tests/kudpc-extended.sbatch` のジョブ `9175623` は、
`-t 0:15:00`、`--rsc p=5:t=1:c=1` で既存14ケースと追加27ケースを通過した。
追加ケースは各言語が int32/float32/float64 と1〜3次元の全9組合せを送信し、
受信言語を交替させ、間引き・逆順スライスと負値の一致を検証する。
