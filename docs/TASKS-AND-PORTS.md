# 複数の接続とTask

[ドキュメント一覧](README.md) · 前提：[最初の連携](HIGH-LEVEL-USAGE.md)

複数の配列を受け取る、同じ配列を複数の解析へ渡す、計算をワーカーへ分配する方法を説明します。

## 複数のポートから受け取る

電位と密度のように入力ポートが複数ある場合は、名前を指定せずに受信します。
届いたメッセージの `port` で処理を分けてください。

次は `analysis` というconsumerの例です。
`analyze_potential` と `analyze_density` は利用者の処理に置き換えます。

```python
from cotocoa import Context

with Context("analysis") as ctx:
    for message in ctx.receive():
        with message:
            if message.port == "potential":
                analyze_potential(message.array, message.step)
            elif message.port == "density":
                analyze_density(message.array, message.step)
```

| 言語 | 全入力ポートを受信する指定 |
| --- | --- |
| Python | `ctx.receive()` |
| Fortran | `call ctx%receive('*', message, has_message=available)` |
| C | `cotocoa_receive(ctx, NULL, &message, &error)` |

名前付き受信は、consumerの全入力が同じポート名の場合に使えます。
ほかのポートを持つconsumerで特定の名前だけを待つとエラーになります。
複数の送信プログラムを区別する場合は `source`、接続を区別する場合は `name` を見ます。

1グループのconsumerでStreamとTaskを両方受ける場合も、receive-anyを使い、
メッセージの `kind` で区別します。`tasks`・`next_task` は、入力がすべてTaskの場合に使います。

## 同じ配列を複数の解析へ送る

同じ `from` から、異なる `to` へ接続を定義します。
以下は、シミュレーションの電位を保存処理と表示処理へ送る `connections` の例です。
`programs` には `simulation`・`archive`・`display` をそれぞれ宣言してください。

```yaml
connections:
  - name: potential_archive
    from: simulation.potential
    to: archive.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    delivery: all

  - name: potential_display
    from: simulation.potential
    to: display.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    delivery: latest
```

producerが `potential` へ1回sendすると、両方へ配送されます。
接続ごとにdeliveryとbuffer_slotsを選べますが、型・形状・軸・単位・modeは揃えてください。

保存処理が遅く送信枠が満杯になった場合は、次のsendが待ちます。
表示側をlatestにしても、allを選んだ宛先への配送は省略されません。

## 計算をワーカーへ依頼する

独立した計算を並行して処理したい場合は、接続に `kind: task` を指定します。
consumerの `group_size` で、1つの依頼を処理するMPI rank数を決めます。

次は、2rankで動くワーカーを4グループ作る設定例です。
実行ファイル名は自分のプログラムへ置き換えてください。

```yaml
schema: 1
programs:
  simulation:
    command: ["./simulation"]
    ranks: 2
  tracing:
    command: ["./tracing"]
    ranks: 8
    group_size: 2
connections:
  - name: trace_requests
    from: simulation.trace
    to: tracing.trace
    kind: task
    dtype: float64
    shape: [3, 20]
    global_shape: [3, 20]
    axes: [coordinate, seed]
```

この構成ではproducer 2rank、Coupler 1rank、consumer 8rankの**計11rank**を使います。
各依頼は、空いている1グループへ配送されます。

### 依頼する

Contextを開いたproducer内で、入力配列をsubmitします。
`seeds` は設定に一致する `float64`・shape `[3, 20]` の配列です。

```python
ticket = ctx.submit("trace", seeds, step=10)
# ここで、依頼の結果に依存しない計算を進められる。
ticket.wait()
ticket.release()
```

`submit` はsnapshotなら入力をコピーします。
Ticketの `test()` は完了状態を調べ、`wait()` は処理完了まで待ちます。
送信枠が埋まると、submit自体も待つ場合があります。

### 処理する

ワーカーグループの**全rank**が、同じ順で受信と完了通知を行います。

```python
with Context("tracing") as ctx:
    for task in ctx.tasks("trace"):
        with task:
            trace(task.array, ctx.comm)
```

`trace` は利用者の計算です。MPIで計算を分担する場合は `ctx.comm` を使います。
入力配列はグループ内の全rankへ渡されます。
ファイル出力を1回だけ行う場合は、グループ内のrankを見て担当を決めてください。

### C・Fortranで使う

| 操作 | Fortran | C |
| --- | --- | --- |
| 依頼 | `ctx%submit` | `cotocoa_submit` |
| 受信 | `ctx%next_task` | `cotocoa_next_task` |
| 完了 | `task%complete` | `cotocoa_complete` とmessageの解放 |
| 待機 | `ticket%wait` | `cotocoa_ticket_wait` |

実行できるサンプルを[examples/tasks](../examples/tasks/README.md)に用意しています。
[Fortran](../examples/tasks/fortran.f90)・[C](../examples/tasks/c.c)・[Python](../examples/tasks/python.py)のどれも送信側・受信側として使えます。

## Taskで保証されること

Ticketの完了は、ワーカーが処理完了を通知したことを表します。
計算結果を返す機能ではないため、結果の保存はワーカーの利用者コードで行います。
Ticketをreleaseしても依頼は取り消されず、Contextのflushやcloseは残りの依頼を待ちます。

Taskは `delivery: all` を使い、1つの送信ポートの宛先は1つです。
複数グループへ依頼した処理の完了順は保証しません。
また、複数グループを持つconsumerにStream入力を混在させることはできません。
