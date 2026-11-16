# 起動と実行状態の確認

[ドキュメント目次](README.md) ｜ 関連：[設定ファイル](CONFIGURATION.md)・[困ったとき](TROUBLESHOOTING.md)

連携するプログラムは、`cotocoa` コマンドでまとめて起動します。
このページでは、コマンドの使い方と、実行中・実行後の状態の調べ方を説明します。

## コマンドの一覧

| コマンド | すること | MPIプログラムを起動するか |
| --- | --- | --- |
| [`cotocoa check 設定ファイル`](#設定を確認するcheck) | 設定の誤りを調べる | しない |
| [`cotocoa plan 設定ファイル`](#必要な資源を見積もるplan) | プロセス数やメモリの見積もりを表示する | しない |
| [`cotocoa run 設定ファイル --launcher ...`](#実行するrun) | 全プログラムとCouplerを起動する | する |
| [`cotocoa probe 設定ファイル --launcher ...`](#mpi環境を確かめるprobe) | 利用者のプログラムの代わりに確認用プログラムを起動し、MPI環境を確かめる | する |
| [`cotocoa status [記録ディレクトリ]`](#実行状態を調べる) | 実行中・実行後の状態を表示する | しない |

リポジトリの中で[インストール](INSTALL.md)した場合、コマンドは `.venv/bin/cotocoa` にあります。
このページでは `cotocoa` と書きます。`python -m cotocoa` としても同じです。

`check`・`plan`・`status` は、ログインノードでも実行できます。
`run` と `probe` はMPIプログラムを起動するので、計算ノード（ジョブの中）で実行してください。

## 設定を確認する（check）

```sh
cotocoa check cotocoa.yaml
```

```text
valid: simulation -> analysis; 4 ranks including Coupler
```

設定ファイルの書式、接続の組み合わせ、`command` の実行ファイルがあるかを調べます。
問題がなければ、送信側と受信側のプログラム名と、必要なMPIプロセス数（Couplerを含む）を表示します。
問題があれば、理由を表示して終了コード2で終わります。

設定を書き換えたら、ジョブを投入する前に `check` を実行することをおすすめします。

## 必要な資源を見積もる（plan）

```sh
cotocoa plan cotocoa.yaml
```

設定を解釈した結果を、JSONで表示します。主な項目は次のとおりです。

| 項目 | 内容 |
| --- | --- |
| `ranks` | 必要なMPIプロセス数（Couplerを含む） |
| `producers`・`consumers` | 送信側・受信側のプログラム名 |
| `programs` | 各プログラムの `command`（絶対パスに直したもの）、`ranks`、`group_size` |
| `connections` | 各接続の設定。省略した項目には既定値が入っている |
| `producer_buffer_bytes` | 送信バッファの合計バイト数（全送信プロセスの合計） |
| `consumer_buffer_bytes` | 受信した配列を保持するためのバイト数（全受信プロセスの合計） |

`producer_buffer_bytes` は、`buffer_slots` を増やしたときのメモリ使用量の確認に使えます。
ここに含まれるのはCoToCoAが確保する分だけで、利用者のプログラム自身の配列は含みません。

## 実行する（run）

```sh
cotocoa run cotocoa.yaml --launcher mpiexec    # mpiexec を使う
cotocoa run cotocoa.yaml --launcher slurm      # Slurm の srun を使う
```

`--launcher` は必須です。`run` は、次の順でMPIプロセスを並べて1つのジョブとして起動します。

1. 送信側のプログラム（複数あれば名前順）
2. Coupler（1プロセス）
3. 受信側のプログラム（複数あれば名前順）

各プログラムは、設定ファイルのあるディレクトリを作業ディレクトリとして起動されます。
環境変数は、`cotocoa run` を実行した環境のものが引き継がれます。
プログラムの標準出力と標準エラー出力は、`cotocoa run` の出力にそのまま出ます。

`run` の終了コードは、すべてのプログラムが正常に終われば0です。

### mpiexecで起動する

```sh
cotocoa run cotocoa.yaml --launcher mpiexec
```

内部では、`mpiexec -n 2 ./simulation : -n 1 cotocoa-coupler ... : -n 1 ./analysis` のように、
複数のプログラムを `:` で区切って1つのジョブにする形式で起動します。
この形式に対応したMPIが必要です（一般的なMPIは対応しています）。

### Slurmで起動する

必要なプロセス数を確保した**ジョブの中で**、`cotocoa run` を**1回だけ**実行します。

```sh
#!/bin/bash
#SBATCH --ntasks=4
#SBATCH --time=00:10:00

cd "$SLURM_SUBMIT_DIR"
cotocoa run cotocoa.yaml --launcher slurm
```

- 確保するタスク数は、`cotocoa check` が表示する「ranks including Coupler」の数以上にします。
- `cotocoa run` は、内部で `srun` を呼んで全プロセスを起動します。
  `srun cotocoa run ...` のように、`cotocoa run` 自体を `srun` で起動しないでください。
- `cotocoa run` は資源の確保やジョブの投入は行いません。ジョブの外で実行するとエラーになります。
- 設定ファイル、実行ファイル、作業ディレクトリは、計算ノードから見える場所に置いてください。
- 全プロセスは、1回の `srun` で同じ条件で起動されます。
  OpenMPを併用する場合、1プロセスあたりのコア数はジョブの資源指定で全プロセス共通に決め、
  スレッド数は環境変数 `OMP_NUM_THREADS` で指定します。プログラムごとにコア数を変えることはできません。

資源の指定方法は施設によって異なります。
KUDPCでは `#SBATCH --rsc p=4:t=1:c=1` のように指定します。
[examples/snapshot/kudpc.sbatch](../examples/snapshot/kudpc.sbatch) に例があります。

### runのオプション

| オプション | 意味 |
| --- | --- |
| `--launcher mpiexec` または `--launcher slurm` | 起動方法（必須） |
| `--probe-first` | 本実行の前に[probe](#mpi環境を確かめるprobe)を行い、成功した場合だけ本実行に進む |
| `--run-dir ディレクトリ` | [実行の記録](#実行状態を調べる)の保存先。まだ存在しないディレクトリを指定する |
| `--coupler パス` | `cotocoa-coupler` の場所。自動で見つからない場合に指定する |
| `--probe パス` | `cotocoa-probe` の場所。自動で見つからない場合に指定する |

`cotocoa-coupler` と `cotocoa-probe` は、環境変数 `PATH` と、リポジトリの `build/` から探します。

## MPI環境を確かめる（probe）

```sh
cotocoa probe cotocoa.yaml --launcher slurm
```

```text
PASS probe: 4 ranks, schema/MPI ABI, communicators, windows and collective shutdown
```

`probe` は、利用者のプログラムの代わりに、付属の確認用プログラムを同じプロセス配置で起動します。
MPIの初期化、プロセス数、CoToCoAが使う通信の準備と終了が、その環境でできることを確かめます。

初めて使う計算機や、MPIの環境を変えた後に実行すると、
「自分のプログラムの問題か、環境の問題か」を切り分けられます。
利用者のプログラムやmpi4pyは起動しないので、それらの問題は `probe` では分かりません。

## 実行状態を調べる

`run` と `probe` は、実行のたびに記録用のディレクトリを作り、起動時にその場所を表示します。

```text
cotocoa: run record: /path/to/.cotocoa/runs/20261116T001416Z-1a2b3c4d
```

既定の場所は、設定ファイルと同じディレクトリの `.cotocoa/runs/` の下です。
このパスを `status` に渡すと、状態を表示します。実行中でも、実行後でも使えます。

```sh
cotocoa status /path/to/.cotocoa/runs/20261116T001416Z-1a2b3c4d
```

```text
finished: /path/to/.cotocoa/runs/20261116T001416Z-1a2b3c4d
rank=0 producer closed step=10 sent=10 received=0 completed=0 dropped=0
rank=1 producer closed step=10 sent=10 received=0 completed=0 dropped=0
rank=2 __coupler__ closed step=10 sent=0 received=20 completed=0 dropped=0
rank=3 consumer closed step=10 sent=0 received=20 completed=20 dropped=0
```

`.cotocoa/runs` を渡すと、その中で最も新しい実行を表示します。
引数を省略すると、現在のディレクトリの `.cotocoa/runs` を見ます。
`--json` を付けると、同じ内容をJSONで出力します。

### 1行目：実行全体の状態

| 表示 | 意味 |
| --- | --- |
| `probing`・`running` | probeの実行中・本実行の実行中 |
| `finished` | 正常に終了した |
| `failed` | エラーで終了した |
| `timeout` | [時間制限](#時間制限を設ける)を超えたので中断した |
| `interrupted` | Ctrl+Cなどで中断された |
| `controller-lost` | 実行中のはずだが、`cotocoa run` のプロセスが見つからない。ジョブが強制終了された可能性がある |

### 2行目以降：各プロセスの状態

1行が1つのMPIプロセスです。`rank` はジョブ全体での番号で、`__coupler__` はCouplerです。

| 項目 | 意味 |
| --- | --- |
| プログラム名の次の語 | そのプロセスが最後に記録した段階（下の表） |
| `step` | 最後に扱ったメッセージの `step` |
| `sent` | 送信した件数 |
| `received` | 受信した件数（Couplerでは中継した件数） |
| `completed` | 完了通知した件数 |
| `dropped` | [`delivery: latest`](DELIVERY.md) で捨てた件数（Couplerの行に出る） |

| 段階 | そのプロセスがしていること |
| --- | --- |
| `open` | Contextを開いた |
| `send`・`sent` | 送信中・送信を終えて利用者の計算に戻った |
| `ticket-completion` | 受信側の完了通知を待っている（送信バッファの空き待ち、`wait`、`flush`、`close`） |
| `receive`・`worker-poll` | 次のメッセージが届くのを待っている（Streamの受信側でも `worker-poll` と表示される） |
| `processing` | メッセージを受け取り、利用者の処理に入った |
| `completed` | 完了通知を終えて利用者の処理に戻った |
| `closed` | Contextを閉じた |
| `failed` | エラーで中断した。続けて理由が表示される |
| `dispatched`・`coupler-poll` | （Coupler）通知を中継した・次の通知を待っている |

状態は、段階が変わるたびに記録されます。待っている間は、約1秒ごとに経過時間が更新されます。
利用者の計算が長く続いている間は、最後の段階のまま変わりません。
たとえば `sent` や `processing` が続いているプロセスは、利用者の計算をしている最中です。

`ranks without a status record` と表示されたプロセスは、一度も状態を記録していません。
Contextを開く前に終了したか、まだ開いていないことを表します。

止まったまま進まないときの状態の読み方は、[困ったとき](TROUBLESHOOTING.md#止まったまま進まない)にあります。

## 時間制限を設ける

設定ファイルに次のように書くと、`run` 全体（`--probe-first` の時間を含む）を600秒に制限できます。

```yaml
runtime:
  timeout_seconds: 600
```

時間を過ぎると、`run` は制限時間に達した時点の各プロセスの状態を表示してから、全プロセスを終了させます
（終了を要求し、5秒たっても終わらなければ強制終了します）。終了コードは2です。

```text
cotocoa: run exceeded timeout_seconds=600.0; rank states when the limit expired:
cotocoa:   rank=0 simulation ticket-completion step=3 sent=2 received=0 completed=0 dropped=0
cotocoa:   rank=1 simulation ticket-completion step=3 sent=2 received=0 completed=0 dropped=0
cotocoa:   rank=2 __coupler__ coupler-poll step=2 sent=0 received=4 completed=0 dropped=0
cotocoa:   rank=3 analysis processing step=1 sent=0 received=1 completed=0 dropped=0
cotocoa: run exceeded timeout_seconds=600.0; inspect cotocoa status /path/to/.cotocoa/runs/20261116T001416Z-1a2b3c4d
```

この表示から、どのプロセスが何を待っていたかが分かります
（読み方は[困ったとき](TROUBLESHOOTING.md#止まったまま進まない)）。
中断された後も、`cotocoa status` で同じ状態を確認できます。1行目の表示は `timeout` になります。

同じ値は、ライブラリの中の個々の待ち（完了通知の待ち、次のメッセージの待ちなど）の上限にもなります。
[`cotocoa run` を使わずに起動した](#cotocoa-runを使わずに起動する)場合などに、1回の待ちがこの時間を超えると、
どのプロセスが何を待っていたかを表示して、ジョブを中断します。

```text
cotocoa: rank=0 program=producer: timeout waiting for ticket-completion; connection=0 step=99 limit=600 seconds
```

本番の長い計算では、計算全体にかかる時間より十分長い値にするか、省略（制限なし）してください。
ジョブ全体の時間制限は、Slurmなどジョブ管理システムの側でも必ず設定してください。

## cotocoa runを使わずに起動する

既存のジョブスクリプトの起動方法を変えられない場合は、`cotocoa run` を使わずに、自分でMPIプロセスを起動できます。

まず、設定ファイルを **manifest**（ライブラリが読む形式のファイル）に変換します。

```sh
cotocoa plan cotocoa.yaml --output cotocoa.manifest
```

次に、manifestの絶対パスを環境変数 `COTOCOA_MANIFEST` に設定し、全プロセスを1つのMPIジョブとして起動します。

```sh
export COTOCOA_MANIFEST="$PWD/cotocoa.manifest"
mpiexec -n 2 ./simulation : -n 1 cotocoa-coupler "$COTOCOA_MANIFEST" : -n 1 ./analysis
```

守ることは次のとおりです。

- プロセスを並べる順序は、**送信側のプログラム（名前順）→ Coupler（1プロセス）→ 受信側のプログラム（名前順）** です。
- 各プログラムのプロセス数は、設定ファイルの `ranks` と一致させます。
- 環境変数 `COTOCOA_MANIFEST` は、全プロセスに同じ値が渡るようにします。
- Couplerには、manifestのパスを引数として渡します。
- 設定ファイルを書き換えたら、manifestを作り直します。

順序やプロセス数が合っていないと、起動時に `startup mismatch` のエラーになります。

この方法では、`cotocoa run` による実行全体の時間制限と、`cotocoa status` のための記録は行われません。
