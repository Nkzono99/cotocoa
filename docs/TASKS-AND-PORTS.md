# 複数接続と計算依頼

各プログラムの役割は接続の from/to から決める。複数の producer と consumer を
指定でき、各 producer は自身の MPI communicator を持つ。producer と consumer を
兼ねる同一プロセスや循環する接続は設定検証で拒否する。

## 複数ポートの受信

1つの consumer が異なるポートを受信するときは、全ポートを受けるループを使う。
メッセージの接続名、受信ポート、送信プログラム、送信 rank、型、次元で処理を選ぶ。

```python
with Context("analysis", config="cotocoa.yaml") as ctx:
    for message in ctx.receive():
        with message:
            if message.port == "potential":
                analyze_potential(message.array, message.step)
            elif message.port == "density":
                analyze_density(message.array, message.step)
```

C は `cotocoa_receive(ctx, NULL, ...)`、Fortran は
`call ctx%receive("*", message, has_message=available)` に対応する。
Fortran の `message%name()/port()/source()/source_rank()/kind()/dtype()/ndim()` と
C の `cotocoa_message_metadata` から同じ情報を取得する。

名前を指定する receive は、当該 consumer の全入力がそのポートを使う場合に限る。
異なるポートのメッセージを黙って読み飛ばしたり、届いたメッセージを保持したまま
別のポートを待ったりしない。複数のポートがあるときの named receive は明示的な
エラーとし、受信の順番による停止を防ぐ。

## 同じデータを複数宛先へ送る

同じ from に対して to の異なる接続を宣言する。接続名はそれぞれ別にする。
dtype、shape、global_shape、axes、units、time_unit、mode は同じ schema にする。
producer の send は各宛先に独立した送信枠で snapshot を送り、各宛先の完了まで
保持する。遅い宛先の枠が埋まると次の送信を待つ。

```yaml
connections:
  - name: potential_analysis
    from: pic.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8, 4]
    global_shape: [32, 8, 4]
    axes: [x, y, z]
  - name: potential_visualization
    from: pic.potential
    to: visualization.potential
    dtype: float64
    shape: [16, 8, 4]
    global_shape: [32, 8, 4]
    axes: [x, y, z]
```

programs には上の全プログラムを宣言する。送信元の関数呼び出しは1回でよい。
fan-out による送信枠と queue の必要量も plan が合算する。

## Task API

接続に `kind: task` を指定する。省略した接続は `kind: stream`。
Task の入力も Stream と同じ型付き配列・snapshot の契約を使う。
Task の出力ポートは1宛先とし、暗黙の fan-out を行わない。

```yaml
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

この例の tracing は2 rankずつの4 groupとなり、各依頼を空いている1 groupへ配送する。
各 group 内では入力を共有し、受信と完了は group 内の集団操作となる。
複数 group の Task の処理完了順は保証しない。Stream は順序を保つため
consumer 全 rank で1 groupとし、group_size を分割する設定を拒否する。

```python
ticket = ctx.submit("trace", seeds, step=step)
advance_simulation()
if not ticket.test():
    ticket.wait()
ticket.release()

# tracing の全 group member が実行する。
for task in ctx.tasks():
    with task:
        trace(task.array)
```

Fortran は型付きの subroutine として ticket 変数を渡す。

```fortran
type(ctca_ticket) :: ticket
call ctx%submit("trace", seeds, ticket, step=step)
call ticket%wait()
call ticket%release()
```

C は `cotocoa_submit(..., &ticket, ...)`、`cotocoa_ticket_test/wait/release` と
`cotocoa_next_task`、`cotocoa_complete` を使う。
型と次元は各言語で int32/float32/float64、1〜3次元に対応する。

ticket の test/wait は入力を受信しただけでは完了せず、Worker が処理して complete
した時点を表す。ticket の解放はローカルの参照の解放で、処理の取消しではない。
ticket を解放しても Context の flush/close は当該処理を待つ。
Context を閉じた後も残した ticket の完了状態を照会できる。
Fortran の ticket の代入は参照を保持し、finalizer は MPI を呼ばずローカル参照だけを
解放する。新しい submit で同じ ticket 変数を再利用することもできる。

Task の wait は任意の計算結果を返す RPC ではなく、非同期処理の完了通知を扱う。
ファイルや外部資源への処理結果の保存は Worker の利用者コードで行う。
