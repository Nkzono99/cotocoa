# 例：配列を送受信する

2プロセスの送信側が、電位に見立てた3次元配列を10ステップ分送り、1プロセスの受信側が平均値を表示します。
[チュートリアル](../../docs/TUTORIAL.md)で、この例の設定とコードを1つずつ説明しています。

```text
  producer rank 0  (shape [3,5,7], offset [0,0,0]) --+
                                                     +--> Coupler --> consumer rank 0
  producer rank 1  (shape [3,5,7], offset [3,0,0]) --+
```

## 実行する

[インストール](../../docs/INSTALL.md)を終えた後、リポジトリの直下から実行します。
必要なMPIプロセスは、送信側2 + Coupler 1 + 受信側1 = **4** です。

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher mpiexec
```

Slurmでは、4プロセス以上を確保したジョブの中で、`--launcher slurm` に替えて1回だけ実行します。
KUDPC用のジョブスクリプトは [kudpc.sbatch](kudpc.sbatch) です（`sbatch -p キュー名 examples/snapshot/kudpc.sbatch`）。

受信側は、合計20件（2プロセス × 10ステップ）のメッセージを受け取ります。
各配列の値は `step + source_rank` です。

## ファイル

| ファイル | 内容 |
| --- | --- |
| [cotocoa.yaml](cotocoa.yaml) | 設定ファイル。送信側・受信側ともFortran |
| [fortran-to-python.yaml](fortran-to-python.yaml) | 受信側だけをPythonに替えた設定ファイル |
| [fortran.f90](fortran.f90) | Fortranのプログラム（実行ファイルは `build/snapshot_fortran`） |
| [c.c](c.c) | Cのプログラム（実行ファイルは `build/snapshot_c`） |
| [python.py](python.py) | Pythonのプログラム |
| [kudpc.sbatch](kudpc.sbatch) | KUDPC用のジョブスクリプト |

どのプログラムも、引数が `producer` なら送信側、`consumer` なら受信側として動きます。
設定ファイルの `command` を書き換えると、言語の組み合わせを変えられます
（[チュートリアルの表](../../docs/TUTORIAL.md#6-受信側をpythonに替える)）。

2プロセス分の配列を全体領域 `[6, 5, 7]` の1つの配列にまとめる方法は、
[分割された配列をまとめる](../../docs/ASSEMBLY.md)にあります。
