# 配列を送受信する例

Fortran・C・Pythonを組み合わせて、3次元の電位配列を送るサンプルです。
初期設定はFortran→Fortranで、producer 2rank + Coupler 1rank + consumer 1rankの**計4rank**を使います。

[インストール](../../docs/INSTALL.md)を終えた後、リポジトリ直下から実行します。

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher mpiexec
```

Slurmでは4タスク以上を確保し、ジョブ内で `--launcher slurm` に置き換えてCLIを1回実行します。

各producerは `[3, 5, 7]` の配列を10回送ります。
consumerには合計20件が届き、各配列の値は `step + source_rank` です。
全体領域 `[6, 5, 7]` の中での位置はoffsetに入っています。

実行結果の見方と言語の差し替えは[最初の連携を動かす](../../docs/HIGH-LEVEL-USAGE.md)、
全体配列への結合は[Assembler](../../docs/ASSEMBLY.md)を参照してください。

| 言語 | ソース |
| --- | --- |
| Fortran | [fortran.f90](fortran.f90) |
| C | [c.c](c.c) |
| Python | [python.py](python.py) |

接続設定は[cotocoa.yaml](cotocoa.yaml)、KUDPC向けの起動例は[kudpc.sbatch](kudpc.sbatch)です。
