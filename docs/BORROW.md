# コピーせずに送る（borrow）

[ドキュメント目次](README.md) ｜ 前提：[しくみと用語](CONCEPTS.md)

通常の `send` は、配列を送信バッファへコピーしてから戻ります。
配列が大きく、このコピーの時間やメモリが問題になる場合は、コピーを省いて送ることができます。
これを **borrow**（借用）と呼びます。

まずは既定の送り方（**snapshot**）で動かし、コピーが実際に負担になっている場合にだけborrowを検討してください。
borrowでは、利用者が守るべき約束が1つ増えます。

## snapshotとborrowの違い

[しくみ](CONCEPTS.md#1回の送受信で起きること)で説明したとおり、受信側は送信側のメモリから配列を直接読み取ります。
違いは、受信側が**どこから**読むかです。

```text
 snapshot (default)                         borrow

 your array --copy--> slot in the           your array   <--- read by the receiver
                      send buffer  <--- read by the receiver
```

| | snapshot（既定） | borrow |
| --- | --- | --- |
| 送信時のコピー | あり | なし |
| 送信側の追加メモリ | 配列の大きさ × `buffer_slots` | なし |
| 送った配列を書き換えてよい時点 | `send` が戻った直後から | **受信側の完了通知が届いた後** |

borrowでは、受信側が利用者の配列そのものを読みに来ます。
受信側が読み終える前に配列を書き換えたり解放したりすると、受信側に壊れた値が届いたり、プログラムが異常終了したりします。
これが、borrowで増える約束です。

**borrowで送った配列は、その送信が完了するまで、書き換えても解放してもいけません。**

なお、borrowで省けるのは送信側のコピーだけです。受信側は、どちらの場合も自分用のコピーを受け取ります。

## 設定する

接続に `mode: borrow` を書きます。

```yaml
connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    mode: borrow
```

`mode: borrow` の接続には、次の節のborrow用の呼び出しで送ります。
通常の `send` で送ろうとするとエラーになります（逆に、`mode: snapshot` の接続へborrowで送ることもできません）。

## 送る

borrowで送ると、**Ticket**が返ります。Ticketは、その送信が完了したかどうかを確認するための控えです。

### Python

```python
ticket = ctx.borrow("potential", field, step=step)

# ここでは field を書き換えない計算だけを行う。

ticket.wait()        # 受信側の完了通知まで待つ
field[:] = 0.0       # 完了後なので書き換えてよい
```

Pythonでは、完了するまでライブラリが配列への参照を保持するので、
変数を `del` しても配列が先に解放されることはありません。
ただし、配列の**内容の書き換え**までは防げません。
スライス（`field[::2]` など）を送った場合は、元の配列も書き換えないでください。

Taskをborrowで依頼するには、`ctx.submit("trace", seeds, step=step, borrow=True)` とします。

### Fortran

送る配列を `target, asynchronous` 属性を付けて宣言します。

```fortran
real(c_double), target, asynchronous :: field(16, 8)
type(ctca_ticket) :: ticket

call ctx%borrow('potential', field, ticket, step=int(step, c_int64_t))

! ここでは field を書き換えない計算だけを行う。

call ticket%wait()
call ticket%release()
field = 0.0_c_double      ! 完了後なので書き換えてよい
```

`work(1:31:2, :)` のように、大きな配列の一部を取り出した部分配列も、コピーせずにそのまま送れます。

`a + b` のような配列式や、型変換の結果など、**呼び出しの間だけ存在する一時的な配列を渡さないでください**。
呼び出しが戻った時点で配列が消えてしまい、受信側が読めなくなります。

Taskをborrowで依頼するには、`ctx%submit_borrow` を使います。引数は `ctx%borrow` と同じです。

### C

```c
cotocoa_ticket *ticket = NULL;
if (cotocoa_borrow(ctx, "potential", &view, &options, &ticket, &error))
    cotocoa_abort(ctx, &error);

/* ここでは配列を書き換えない計算だけを行う。 */

if (cotocoa_ticket_wait(ticket, &error))
    cotocoa_abort(ctx, &error);
cotocoa_ticket_release(ticket);
/* 完了後なので、配列を書き換えても解放してもよい。 */
```

`view` と `options` の作り方は、通常の送信と同じです（[Cから使う](GUIDE-C.md#送信する)）。
Taskをborrowで依頼するには、`cotocoa_submit_borrow` を使います。

## いつ配列を書き換えてよいか

次の**どれか**が成り立った後です。

| 確認方法 | Python | Fortran | C |
| --- | --- | --- | --- |
| そのTicketの完了を待った | `ticket.wait()` | `call ticket%wait()` | `cotocoa_ticket_wait` |
| そのTicketが完了済みと分かった | `ticket.test()` が `True` | `ticket%test()` が真 | `cotocoa_ticket_test` の `done` が1 |
| 送ったものすべての完了を待った | `ctx.flush()` | `call ctx%flush()` | `cotocoa_flush` |
| Contextを閉じた | `with` を抜けた | `call ctx%close()` | `cotocoa_close` |

**`release` は「書き換えてよい」の合図ではありません。**
`release` は控えを手放すだけの操作で、送信の完了とは関係がありません。
`wait` せずに `release` した場合は、`flush` か `close` が戻るまで配列を保持してください。

同じ配列を複数の宛先へ送っている場合（[形B](CONNECTIONS.md#b-同じ配列を複数の相手へ送る)）、
1つのTicketが全宛先をまとめて表します。Ticketが完了するのは、すべての宛先で完了通知が行われたときです。

[`delivery: latest`](DELIVERY.md) の接続では、受信側に届く前に新しい値に置き換えられて捨てられた場合も、Ticketは完了になります。

## 使い方の型

borrowした配列は完了まで書き換えられないので、時間発展の計算では次のような使い方になります。

**その場で完了を待つ。** 送ってすぐ `wait` します。
コピーは省けますが、送信側は毎回、受信側の処理が終わるまで待つことになります。
受信側の処理が軽いときに向いています。

**2つの配列を交互に使う。** 奇数ステップは配列Aへ、偶数ステップは配列Bへ計算結果を書き、書いた配列をborrowで送ります。
配列Aを次に書き換える前に、配列Aを送ったときのTicketを `wait` します。
こうすると、受信側が1ステップ分遅れていても送信側は待たずに進めます。

同時に完了待ちにできる送信の数は、接続の `buffer_slots`（既定は2）までです。
それを超えて送ろうとすると、最も古い送信が完了するまで待ちます。

## 受信側が読み取れるタイミング

通常の送信（snapshot）では、受信側は送信側が何をしていても配列を読み取れます。
borrowでは、送信側と受信側が**同じノード**で動いている場合、受信側が配列を読み取れるのは、
送信側のプロセスがMPIの処理（CoToCoAの呼び出しや、自分の計算のためのMPI通信）をしている間だけです。
送信側がMPIを呼ばずに計算を続けている間は、受信側は読み取りを待ちます。

送信側が `ticket.wait()` や `flush` で完了を待っている間はMPIの処理が行われるので、読み取りは進みます。
「borrowで送り、完了を待たずに長い計算をする」使い方では、その計算の間は受信側が進まない点に注意してください。
別々のノードで動いている場合は、この制限はありません。

これはIntel MPI 2021.12で確認した動作で、MPIの実装によって異なる場合があります。

## 重なる範囲を同時にborrowする場合

まだ完了していないborrowがあるとき、その配列と**一部だけ重なる**範囲をborrowすると、エラーになります。

| 新しくborrowする範囲 | 結果 |
| --- | --- |
| 完了待ちの範囲と重ならない | 送れる |
| 完了待ちの範囲と同じ、またはその内側に収まる | 送れる |
| 完了待ちの範囲から一部がはみ出して重なる | エラー。先の送信の完了を待ってから送る |

同じ配列を毎回そのまま送る、あるいは別々の配列を送る、という使い方であれば、この制限には当たりません。
