# 例：計算をワーカーへ依頼する

2プロセスの送信側が、3要素の配列をそれぞれ10件、Taskとして依頼します。
受信側の2つのワーカーが、配列の和を計算して表示します。
Taskの考え方と書き方は、[計算をワーカーへ依頼する](../../docs/TASKS.md)で説明しています。

```text
  producer rank 0 --+                     +--> consumer group 0 (1 rank)
                    +--> Coupler (queue) -+
  producer rank 1 --+                     +--> consumer group 1 (1 rank)
```

## 実行する

[インストール](../../docs/INSTALL.md)を終えた後、リポジトリの直下から実行します。
必要なMPIプロセスは、送信側2 + Coupler 1 + 受信側2 = **5** です。

```sh
.venv/bin/cotocoa check examples/tasks/tasks.yaml
.venv/bin/cotocoa run examples/tasks/tasks.yaml --launcher mpiexec
```

Slurmでは、5プロセス以上を確保したジョブの中で、`--launcher slurm` に替えて1回だけ実行します。

```text
step=0 sum=3.0
step=0 sum=3.0
step=1 sum=6.0
...
step=9 sum=30.0
```

各依頼の入力は `[step, step+1, step+2]` で、和は `3*step + 3` です。
2つの送信プロセスが同じ入力を送るので、各 `step` が2回ずつ表示されます。
どちらのワーカーがどの依頼を処理するかは決まっていないので、表示の順序は実行ごとに変わります。

## ファイル

| ファイル | 内容 |
| --- | --- |
| [tasks.yaml](tasks.yaml) | 設定ファイル。送信側はFortran、受信側はPython |
| [fortran.f90](fortran.f90) | Fortranのプログラム（実行ファイルは `build/task_fortran`） |
| [c.c](c.c) | Cのプログラム（実行ファイルは `build/task_c`） |
| [python.py](python.py) | Pythonのプログラム |

どのプログラムも、引数が `producer` なら送信側、`consumer` なら受信側として動きます。
設定ファイルの `command` を次の表から選ぶと、言語の組み合わせを変えられます。

| 言語 | `producer` の `command` | `consumer` の `command` |
| --- | --- | --- |
| Fortran | `["../../build/task_fortran", "producer"]` | `["../../build/task_fortran", "consumer"]` |
| C | `["../../build/task_c", "producer"]` | `["../../build/task_c", "consumer"]` |
| Python | `["../../.venv/bin/python", "python.py", "producer"]` | `["../../.venv/bin/python", "python.py", "consumer"]` |

## ワーカーの構成を変える

設定ファイルの `consumer` は `ranks: 2`、`group_size: 1` なので、1プロセスのワーカーが2つできます。
`ranks: 4`、`group_size: 2` にすると、2プロセスのワーカーが2つになります。
この場合に必要なMPIプロセスは、2 + 1 + 4 = 7 です。
