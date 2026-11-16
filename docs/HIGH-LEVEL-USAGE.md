# 最初の連携を動かす

[インストール](INSTALL.md) → 最初の連携 → [連携のしくみ](OVERVIEW.md)

付属のサンプルで、2つの送信プロセスから1つの受信プロセスへ電位配列を送ります。
初期設定はFortran同士です。プログラムを差し替えるだけで、CやPythonとの組み合わせも試せます。

**準備：** [インストール](INSTALL.md)を終え、リポジトリ直下に `build/` と `.venv/` がある状態にしてください。
以下のコマンドはすべてリポジトリ直下から実行します。

## 1. 設定を見る

[examples/snapshot/cotocoa.yaml](../examples/snapshot/cotocoa.yaml)の中心となる設定は次のとおりです。

```yaml
schema: 1
programs:
  producer:
    command: ["../../build/snapshot_fortran", "producer"]
    ranks: 2
  consumer:
    command: ["../../build/snapshot_fortran", "consumer"]
    ranks: 1
connections:
  - name: potential
    from: producer.potential
    to: consumer.potential
    dtype: float64
    shape: [3, 5, 7]
    global_shape: [6, 5, 7]
    axes: [x, y, z]
    units: V
    time_unit: s
```

`producer.potential` は「producerというプログラムのpotentialという送信口」を表します。
`shape` は1プロセスが送る配列、`global_shape` は全領域の大きさです。
この例では、各producerがx方向に半分ずつ担当します。

設定には利用者のプログラムだけを書きます。CoToCoAが配送用のCouplerを1プロセス追加するため、
必要なMPIプロセス数は **2 + 1 + 1 = 4** です。

## 2. 設定を確認する

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
.venv/bin/cotocoa plan examples/snapshot/cotocoa.yaml
```

`check` は設定と実行ファイルの有無を調べます。
`plan` はプロセス配置やバッファの見積もりを表示します。
どちらもMPIジョブを起動しません。

## 3. 実行する

MPIプログラムを実行できる環境で、次を実行します。

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher mpiexec
```

Slurmを使う場合は、**4タスク以上の資源を確保したジョブの中で**次を1回実行します。

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher slurm
```

`cotocoa run` 自体を `srun -n 4` で複製しないでください。CLIが必要なMPIプロセスを起動します。
KUDPC向けには[ジョブスクリプト](../examples/snapshot/kudpc.sbatch)もあります。
利用可能なキューを `sbatch -p キュー名 examples/snapshot/kudpc.sbatch` で指定して投入します。

## 4. 結果を見る

consumerには `step`、送信元の `source`、配列の平均 `mean` が表示されます。

- `step` は1〜10。
- `source` は0または1。
- 平均値は `step + source`。

各producerが10件ずつ送るので、合計20件を受信します。
同じ送信元のデータは順番に届きますが、2つの送信元の出力が混ざる順序は実行ごとに変わります。
Cの例は平均の代わりに先頭値を表示します。このサンプルでは同じ値です。

届く配列は1件ごとに `[3, 5, 7]` です。
`[6, 5, 7]` の全体配列が必要な場合は、[Assembler](ASSEMBLY.md)を使います。

## 5. 言語を差し替える

同じ設定ファイルの `command` を、次の表から選んで置き換えます。
ほかの接続設定はそのまま使えます。

| 言語 | producerのcommand | consumerのcommand |
| --- | --- | --- |
| Fortran | `["../../build/snapshot_fortran", "producer"]` | `["../../build/snapshot_fortran", "consumer"]` |
| C | `["../../build/snapshot_c", "producer"]` | `["../../build/snapshot_c", "consumer"]` |
| Python | `["../../.venv/bin/python", "python.py", "producer"]` | `["../../.venv/bin/python", "python.py", "consumer"]` |

たとえばproducerをFortran、consumerをPythonにすれば、Fortran→Pythonの連携になります。
Fortran→Fortran、C→Fortran、Python→Cなども同じ手順です。

パスは**設定ファイルのあるディレクトリ**を基準に解決します。
この例では `examples/snapshot/` が基準です。設定を別の場所へコピーする場合はパスも合わせてください。

## 自分のコードへ進む

サンプルの全ソースは[Fortran](../examples/snapshot/fortran.f90)・[C](../examples/snapshot/c.c)・[Python](../examples/snapshot/python.py)にあります。
まず配列を作る処理と受信後の処理を、自分の計算に置き換えると始めやすくなります。

言語ごとの書き方は[Fortranガイド](API-Fortran.md)・[Cガイド](API-C.md)・[Pythonガイド](API-Python.md)、
配列サイズやプロセス数の変更は[設定ファイル](CONFIGURATION.md)を参照してください。
