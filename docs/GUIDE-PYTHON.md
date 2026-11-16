# Pythonから使う

[ドキュメント目次](README.md) ｜ ほかの言語：[Fortran](GUIDE-FORTRAN.md)・[C](GUIDE-C.md)

Pythonのプログラムから配列を送る方法と、受け取る方法を説明します。
[チュートリアル](TUTORIAL.md)を済ませ、[しくみと用語](CONCEPTS.md)に目を通していることを前提にします。

このページの例は、[Fortran](GUIDE-FORTRAN.md)・[C](GUIDE-C.md)のガイドと同じ構成です。
送信側と受信側を別の言語で書く場合は、それぞれの言語のページの該当部分を組み合わせてください。

## 例：simulationからanalysisへ電位を送る

2プロセスで動く `simulation` が、領域をx方向に2分割した電位（1プロセスあたり16×8）を5ステップ分送り、
1プロセスの `analysis` が受け取って平均値を表示します。

### 設定ファイル（cotocoa.yaml）

```yaml
schema: 1
programs:
  simulation:
    command: ["python", "simulation.py"]
    ranks: 2
  analysis:
    command: ["python", "analysis.py"]
    ranks: 1
connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    units: V
    time_unit: s
```

`command` の `python` には、`cotocoa` パッケージを入れたPythonを指定します。
[インストール](INSTALL.md#cotocoaコマンドとpythonを使えるようにする)で作った仮想環境を有効にしていれば、`python` のままで構いません。
有効にしていない場合は、`/path/to/cotocoa/.venv/bin/python` のようにパスで書いてください。
各項目の意味は[設定ファイル](CONFIGURATION.md)にあります。

### 送信側（simulation.py）

```python
import numpy as np
from cotocoa import Context

with Context("simulation") as ctx:
    rank = ctx.comm.Get_rank()

    for step in range(1, 6):
        # ここで本来の計算を行う。この例では step + 0.1*rank で埋める。
        potential = np.full((16, 8), step + 0.1 * rank)

        ctx.send("potential", potential,
                 step=step, time=0.5 * step, offset=(16 * rank, 0))
```

### 受信側（analysis.py）

```python
from cotocoa import Context

with Context("analysis") as ctx:
    for message in ctx.receive("potential"):
        with message:
            mean = message.array.mean()
            print(f"step={message.step} source={message.source_rank} "
                  f"offset={message.offset[0]} mean={mean:.3f}")
```

### 実行する

2つのスクリプトと `cotocoa.yaml` を同じディレクトリに置いて実行します。
必要なMPIプロセスは 2 + 1（Coupler）+ 1 = 4 です。

```sh
cotocoa run cotocoa.yaml --launcher mpiexec
```

Slurmでは、4プロセス以上を確保したジョブの中で `--launcher slurm` を指定します。
詳しくは[起動と実行状態の確認](RUNNING.md)を参照してください。

```text
step=1 source=0 offset=0 mean=1.000
step=1 source=1 offset=16 mean=1.100
step=2 source=0 offset=0 mean=2.000
step=2 source=1 offset=16 mean=2.100
...
step=5 source=1 offset=16 mean=5.100
```

`source=0` と `source=1` の行が入り混じる順序は、実行ごとに変わります。

スクリプトは、`python simulation.py` のように単独では実行できません。
相手のプログラムやCouplerと一緒に、`cotocoa run` から起動してください。

## Context

```python
from cotocoa import Context

with Context("simulation") as ctx:
    ...
```

`Context` の引数は、設定ファイルの `programs` に書いた自分の名前です。
`with` ブロックに入るとContextが開き、抜けると閉じます。

`with` ブロックの中で例外が発生すると、エラーを表示して**ジョブ全体を中断**します。
1つのプログラムだけが黙って終了すると、相手のプログラムが待ち続けてしまうためです。

`ctx.comm` は、自分のプログラムのプロセスだけを含むmpi4pyのコミュニケータです。
`ctx.comm.Get_rank()` で自分の番号が、`ctx.comm.Get_size()` で自分のプログラムのプロセス数が得られます。
mpi4pyで並列計算をする場合は、`MPI.COMM_WORLD` ではなく `ctx.comm` を使ってください
（[理由](CONCEPTS.md#自分の計算には-ctxcomm-を使う)）。

`Context("simulation", config="...")` の `config` は、通常は省略します。
この引数が使われるのは、[`cotocoa run` を使わずに起動](RUNNING.md#cotocoa-runを使わずに起動する)し、
かつ環境変数 `COTOCOA_MANIFEST` を設定しない場合だけで、そのときはmanifestのパスを渡します。

## 送信する

```text
ctx.send(name, array, *, step, time=None, offset=None)
```

| 引数 | 必須 | 意味 |
| --- | --- | --- |
| `name` | 必須 | 自分の送信ポート名 |
| `array` | 必須 | 送るNumPy配列（1〜3次元）。型と形状は、接続の `dtype`・`shape` と一致させる |
| `step` | 必須 | この配列に付ける整数の番号。受信側へそのまま伝わる |
| `time` | 省略可 | 物理時刻。省略すると「時刻なし」として届く |
| `offset` | 省略可 | 全体領域の中での開始位置（0始まり）。省略時はすべて0 |

`step`・`time`・`offset` は、キーワード引数で指定します。

### 配列の型に注意する

`array` は、`dtype` が `float64`・`float32`・`int32` のいずれかのNumPy配列でなければなりません。
**型は自動では変換されません。** 次のような場合はエラーになります。

| 渡したもの | 理由 |
| --- | --- |
| Pythonのリスト | NumPy配列ではない |
| `np.arange(10)` | 整数の既定の型は `int64` |
| 接続が `float32` なのに `np.zeros(...)` | `np.zeros` の既定の型は `float64` |

型が合わないときは、送る前に変換します。

```python
field = np.asarray(values, dtype=np.float64)
ctx.send("potential", field, step=10)
```

スライス（`field[::2]` など）や転置した配列も、そのまま渡せます。

`send` は配列をコピーしてから戻ります。戻った後は、配列を書き換えて構いません。

送信バッファがいっぱいのとき、`send` は受信側の完了通知を待ちます（[しくみ](CONCEPTS.md#1回の送受信で起きること)）。

送ったものがすべて受信側で処理されるまで待ちたいときは、`ctx.flush()` を呼びます。
`with` ブロックを抜けるときも、同じように待ってから終了します。

## 受信する

```python
for message in ctx.receive("potential"):
    with message:
        ...   # message.array を処理する
```

`ctx.receive` は、メッセージを1件ずつ返すイテレータです。
すべての送信側が終了して、もう届くものがなくなると、`for` ループが終わります。

引数は自分の受信ポート名です。入力ポートが複数ある場合は、引数を省略して `ctx.receive()` とします
（[複数の配列を受け取る](CONNECTIONS.md)）。

### 完了を通知する

`with message:` のブロックを抜けるときに、完了通知が自動で行われます。
ブロックの中で例外が発生した場合は、ジョブ全体を中断します。

`with` を使わない場合は、処理の後で `message.complete()` を1回呼びます。
完了通知をするまで、次のメッセージは受け取れません。また、送信側の送信バッファも空きません。

`for` ループは、`break` で途中で抜けずに最後まで回してください。
途中でやめると、Contextを閉じるときにエラーになります（[終了のしかた](CONCEPTS.md#終了のしかた)）。

### 配列と付随情報

| 属性 | 内容 |
| --- | --- |
| `message.array` | 届いた配列（NumPy配列、読み取り専用） |
| `message.step` | 送信側が付けたステップ番号 |
| `message.time` | 時刻。付いていなければ `None` |
| `message.offset` | 全体領域の中での開始位置（タプル、0始まり） |
| `message.shape`・`message.global_shape` | 届いた配列の形状、全体領域の形状（タプル） |
| `message.source_rank` | 送信側プログラムの何番目のプロセスが送ったか（0始まり） |
| `message.source`・`message.port` | 送信側のプログラム名、受信ポート名 |

すべての属性は[リファレンス](REFERENCE.md#メッセージに付く情報)にあります。

`message.array` は、メッセージごとに新しく作られるNumPy配列です。
変数やリストに保存しておけば、完了通知の後も、Contextを閉じた後も使えます。
受信した値を誤って書き換えないよう、書き込みはできなくしてあります。値を変更したい場合は `message.array.copy()` でコピーを作ってください。

## エラー

| 状況 | 起きること |
| --- | --- |
| 引数の型や値が正しくない（配列の型が違う、など） | `ValueError` |
| ライブラリが検出したエラー（ポート名が違う、形状が違う、など） | `RuntimeError` |
| `with Context(...)`・`with message:` のブロック内で例外が発生した | ジョブ全体を中断する |
| 続行できないエラー（受信した内容の矛盾など） | ジョブ全体を中断する |

一度閉じたContext、エラーになったContextを開き直すことはできません。

## クラスとメソッドの一覧

| メソッド・属性 | 用途 |
| --- | --- |
| `Context(name, *, config="cotocoa.yaml")` | Contextを作る。`with` で開閉する |
| `ctx.open()`・`ctx.close()` | `with` を使わずに開閉する |
| `ctx.comm` | 自分のプログラム（Taskの受信側では自分のグループ）のコミュニケータ |
| `ctx.send(name, array, *, step, time=None, offset=None)` | Streamへ配列を送る |
| `ctx.receive(name=None)` | メッセージを受け取るイテレータ |
| `ctx.flush()` | 送ったものがすべて完了するまで待つ |
| `ctx.submit(name, array, *, step=0, time=None, offset=None, borrow=False)` | [Task](TASKS.md)を依頼し、Ticketを返す |
| `ctx.tasks(operation=None)` | [Task](TASKS.md)を受け取るイテレータ |
| `ctx.borrow(name, array, *, step=0, time=None, offset=None)` | Streamへ[コピーせずに送り](BORROW.md)、Ticketを返す |
| `message.complete()` | 完了を通知する（`with message:` を使えば不要） |
| `ticket.test()`・`ticket.wait()`・`ticket.release()` | 完了したか調べる・完了を待つ・控えを手放す |
| `Assembler`・`Frame` | [分割された配列をまとめる](ASSEMBLY.md) |

`send` だけは `step` が必須です。`submit`・`borrow` では省略でき、省略時は0になります。
`ctx.tasks` の引数 `operation` は、`ctx.receive` の `name` と同じく、自分の受信ポート名です。

## 次に読むもの

- 複数の配列を送る、複数の相手とつなぐ → [複数の配列・複数の相手とつなぐ](CONNECTIONS.md)
- 独立した計算をワーカーへ割り振る → [計算をワーカーへ依頼する](TASKS.md)
- 分割された配列を受信側でまとめる → [分割された配列をまとめる](ASSEMBLY.md)
- 止まった・エラーが出た → [困ったとき](TROUBLESHOOTING.md)
