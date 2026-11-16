# 計算をワーカーへ依頼する（Task）

[ドキュメント目次](README.md) ｜ 前提：[チュートリアル](TUTORIAL.md)・[しくみと用語](CONCEPTS.md)

互いに独立した計算がたくさんあり、複数のワーカーに並行して処理させたいときは、接続の種類を **Task** にします。
たとえば「シミュレーションの各ステップで、粒子の軌道追跡を別プログラムに任せ、シミュレーション自体は先へ進める」
といった使い方です。

## Streamとの違い

```text
  simulation                 Coupler               tracing  (ranks: 8, group_size: 2)

  submit("trace", a1) --->                    +--> group 0 (2 ranks)
  submit("trace", a2) --->   task queue   ----+--> group 1 (2 ranks)
  submit("trace", a3) --->                    +--> group 2 (2 ranks)
  ...                                         +--> group 3 (2 ranks)

                        each task goes to one idle group
```

| | Stream | Task |
| --- | --- | --- |
| 受信側の構成 | 全プロセスが1つのグループ | `group_size` プロセスごとのグループに分かれる |
| 1件のメッセージが届く先 | そのグループ（＝全プロセス） | **空いているグループ1つ** |
| 並行して処理される数 | 1件ずつ | グループの数まで |
| 送信の呼び出し | `send` | `submit` |
| 送信側が得るもの | なし | **Ticket**（完了を確認するための控え） |

受信側の1つのグループを、以下では **ワーカー** と呼びます。

依頼は入力の配列を渡すだけで、**計算結果を送信側へ返すことはできません**。
結果が必要な場合は、ワーカーがファイルへ書き出すなど、利用者のコードで保存してください。

## 設定する

接続に `kind: task` を書き、受信側プログラムに `group_size` を書きます。

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

`ranks: 8`、`group_size: 2` は、「8プロセスを2プロセスずつに分け、4つのワーカーを作る」という意味です。
`ranks` は `group_size` で割り切れる値にします。
`group_size` を省略すると `ranks` と同じ値になり、ワーカーは1つだけになります。

この構成に必要なMPIプロセスは、2 + 1（Coupler）+ 8 = 11 です。

## 依頼する側

`submit` で入力の配列を渡すと、Ticketが返ります。

Python：

```python
ticket = ctx.submit("trace", seeds, step=step)

# ここで、依頼の結果を待たずに自分の計算を進められる。

ticket.wait()       # ワーカーの処理が終わるまで待つ
```

Fortran：

```fortran
type(ctca_ticket) :: ticket

call ctx%submit('trace', seeds, ticket, step=int(step, c_int64_t))
! ここで自分の計算を進められる。
call ticket%wait()
call ticket%release()
```

C：

```c
cotocoa_ticket *ticket = NULL;
cotocoa_send_options options = cotocoa_send_defaults();
options.step = step;
if (cotocoa_submit(ctx, "trace", &view, &options, &ticket, &error))
    cotocoa_abort(ctx, &error);
/* ここで自分の計算を進められる。 */
if (cotocoa_ticket_wait(ticket, &error))
    cotocoa_abort(ctx, &error);
cotocoa_ticket_release(ticket);
```

`submit` は、`send` と同じく配列をコピーしてから戻ります。戻った後は配列を書き換えて構いません。
`step`・`time`・`offset` も `send` と同じように指定できます（`submit` では `step` を省略でき、省略時は0です）。

### Ticketの使い方

| 操作 | Python | Fortran | C |
| --- | --- | --- | --- |
| 完了したか調べる（待たない） | `ticket.test()` | `ticket%test()` | `cotocoa_ticket_test(ticket, &done, &error)` |
| 完了するまで待つ | `ticket.wait()` | `call ticket%wait()` | `cotocoa_ticket_wait(ticket, &error)` |
| 控えを手放す | `ticket.release()` | `call ticket%release()` | `cotocoa_ticket_release(ticket)` |

Ticketの「完了」は、ワーカーがそのTaskの完了通知（`complete`）を行ったことを意味します。

`release` は、控えのメモリを手放すだけの操作です。**依頼は取り消されません。**
完了を確認する必要がなければ、`wait` せずに `release` して構いません。
その場合も、Contextを閉じるとき（`close`）に、残っている依頼がすべて完了するまで待ちます。
Pythonでは、Ticketの変数が不要になれば自動で手放されるので、`release` を書かなくても構いません。

複数の依頼をまとめて出し、後でまとめて待つこともできます。

```python
tickets = [ctx.submit("trace", make_seeds(i), step=i) for i in range(10)]
for ticket in tickets:
    ticket.wait()
```

一度に出せる依頼の数には上限があります。
完了していない依頼の数が `buffer_slots`（既定は2）に達していると、次の `submit` は最も古い依頼の完了を待ちます。
多くの依頼を先に出しておきたい場合は、接続の `buffer_slots` を増やしてください。

## 処理する側（ワーカー）

ワーカーは、Taskを受け取り、処理し、完了を通知する、というループを回します。Streamの受信側と同じ形です。

Python：

```python
with Context("tracing") as ctx:
    for task in ctx.tasks("trace"):
        with task:
            trace(task.array, ctx.comm)
```

Fortran：

```fortran
do
  call ctx%next_task('trace', task, has_task)
  if (.not. has_task) exit
  call task%copy_to(seeds)
  ! ここで計算する。
  call task%complete()
end do
```

C：

```c
for (;;) {
    cotocoa_message *task = NULL;
    int rc = cotocoa_next_task(ctx, "trace", &task, &error);
    if (rc == COTOCOA_END) break;
    if (rc != COTOCOA_OK) cotocoa_abort(ctx, &error);
    /* cotocoa_message_view で配列を得て計算する。 */
    if (cotocoa_complete(task, &error)) cotocoa_abort(ctx, &error);
    cotocoa_message_release(task);
}
```

受け取ったTaskは、Streamのメッセージと同じ型（Pythonの `Message`、Fortranの `type(ctca_message)`、Cの `cotocoa_message`）です。
配列や `step` の取り出し方も同じです。

ワーカーが複数プロセス（`group_size` が2以上）の場合は、次の点に注意してください。

- ワーカー内の**全プロセス**が、同じ順序で受信と完了通知を行います。
- 入力の配列は、ワーカー内の全プロセスに同じものが渡されます。
- `ctx.comm`（Fortranは `ctx%comm()`、Cは `cotocoa_comm(ctx)`）は、**そのワーカーのプロセスだけ**を含むコミュニケータです。
  ワーカーの中でMPI並列の計算をするときは、これを使います。
- 結果のファイル出力などは、ワーカー内のrank 0だけが行うようにします。

すべての送信側が終了し、残りのTaskがなくなると、受信ループが終わります。

## 動かせる例

[examples/tasks](../examples/tasks/README.md) に、3つの言語で書いた例があります。
2プロセスの送信側がそれぞれ10件の依頼を出し、2つのワーカーが配列の和を計算して表示します。

```sh
.venv/bin/cotocoa run examples/tasks/tasks.yaml --launcher mpiexec
```

## Taskの決まり

- **結果は返らない。** Ticketで分かるのは、処理が完了したことだけです。
- **完了の順序は決まっていない。** 複数のワーカーが並行して処理するので、先に出した依頼が先に終わるとは限りません。
- **取り消しや再実行はできない。** 一度出した依頼は、必ずどれかのワーカーへ渡されます。ワーカーでエラーが起きた場合は、ジョブ全体が中断されます。
- **1つの送信ポートの宛先は1つ。** Taskの接続は、同じ配列を複数の宛先へ送る形（[形B](CONNECTIONS.md#b-同じ配列を複数の相手へ送る)）にはできません。
- **`delivery` は `all` のみ。** 依頼が飛ばされることはありません。
- **ワーカーを複数に分けるプログラムは、Taskだけを受け取る。**
  `group_size` が `ranks` より小さい受信側プログラムには、Streamの接続をつなげません。

ワーカーが1つだけ（`group_size` を省略）の受信側プログラムは、StreamとTaskの両方を受け取れます。
その場合は、ポート名を指定せずに `receive` で受信し、メッセージの `kind`（`stream` か `task` か）で処理を分けます。
`tasks`（`next_task`）は、入力がすべてTaskの受信側で使います。
