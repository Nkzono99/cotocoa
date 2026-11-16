# 計算依頼をワーカーへ分配する例

2つのproducerが、3要素の配列をそれぞれ10件送ります。
2つのワーカーグループが配列の和を計算し、完了を通知します。

## 実行する

[インストール](../../docs/INSTALL.md)を終えた後、リポジトリ直下から実行します。
必要な資源は、producer 2rank + Coupler 1rank + consumer 2rankの**計5rank**です。

```sh
.venv/bin/cotocoa check examples/tasks/tasks.yaml
.venv/bin/cotocoa run examples/tasks/tasks.yaml --launcher mpiexec --probe-first
```

Slurmでは5タスク以上を確保し、ジョブ内で `--launcher slurm` に置き換えてCLIを1回実行します。

consumerは `step=0`〜`step=9` に対応する和を表示します。
各stepの和は `3 * step + 3` で、2つのproducerが同じ入力を送るため、
各stepが2回ずつ処理されます。出力順はワーカーの進行によって変わります。

## 言語とワーカー数を変える

[tasks.yaml](tasks.yaml)のcommandを、次のいずれかへ変更できます。

| 言語 | producer | consumer |
| --- | --- | --- |
| Fortran | `["../../build/task_fortran", "producer"]` | `["../../build/task_fortran", "consumer"]` |
| C | `["../../build/task_c", "producer"]` | `["../../build/task_c", "consumer"]` |
| Python | `["../../.venv/bin/python", "python.py", "producer"]` | `["../../.venv/bin/python", "python.py", "consumer"]` |

consumerを `ranks: 4, group_size: 2` にすると、
2rankで処理するワーカーグループが2つできます。その場合は計7rankを確保してください。

Ticketは計算の完了を通知します。計算結果を返すものではありません。
接続設定とAPIの詳しい使い方は[複数接続とTask](../../docs/TASKS-AND-PORTS.md)を参照してください。
