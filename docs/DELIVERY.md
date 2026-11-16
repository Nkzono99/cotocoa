# 受信が追いつかないとき

[ドキュメント目次](README.md) ｜ 前提：[しくみと用語](CONCEPTS.md)

受信側の処理が送信側より遅いと、送信側の `send` が待たされ、シミュレーション全体が遅くなります。
このページでは、なぜ待たされるのかと、その対処を説明します。

## 何が起きているか

[しくみ](CONCEPTS.md#1回の送受信で起きること)で説明したとおり、送信側は送った配列を
**送信バッファ**のスロットに置き、受信側が完了通知するまで保持します。
スロットの数は、設定の `buffer_slots`（既定は2）です。

`buffer_slots: 2` で、受信側の処理が遅い場合の動きは次のようになります。

```text
 sender                                    receiver
 ------                                    --------
 send(step 1)  -> slot 0                   receive step 1, processing ...
 send(step 2)  -> slot 1                           (still processing step 1)
 send(step 3)  -> both slots are in use:
                  WAIT until step 1                 complete step 1
                  is completed
               -> slot 0                    receive step 2, processing ...
```

送信側は、受信側より最大で2件分だけ先へ進めます。それを超えると、受信側に合わせて待つことになります。
この動作のおかげで、受信側が遅くても配列が失われることはありません。

スロットは、接続ごと・送信プロセスごとに別々です。

受信側は、送信側の計算と並行して動きます。
受信側の処理が1ステップの計算時間より短ければ、バッファが埋まることはなく、送信側は待たされません。

## 対処を選ぶ

| 状況 | 対処 |
| --- | --- |
| 受信側が遅いのは一時的で、平均すれば追いつく | [`buffer_slots` を増やす](#buffer_slotsを増やす) |
| 受信側は常に遅い。途中の値は飛ばしてよい（可視化、進行状況の表示など） | [`delivery: latest` にする](#新しい値を優先するdelivery-latest) |
| 受信側は常に遅い。全ステップが必要 | 受信側を速くする（プロセス数を増やすなど）か、送る頻度を下げる |

「送る頻度を下げる」は、送信側のコードで「10ステップに1回だけ `send` を呼ぶ」ようにすることです。

## buffer_slotsを増やす

```yaml
connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    buffer_slots: 8
```

`buffer_slots` を増やすと、送信側が先へ進める件数が増えます。
受信側がときどき遅くなる程度であれば、その遅れをバッファが吸収します。

代わりに、送信側のメモリを使います。
送信バッファの大きさは「配列1つの大きさ × `buffer_slots` × 送信プロセス数」です。
`cotocoa plan 設定ファイル` の出力の `producer_buffer_bytes` で、合計のバイト数を確認できます。

受信側が常に遅い場合は、`buffer_slots` を増やしても、バッファが埋まるまでの時間が延びるだけです。

## 新しい値を優先する（delivery: latest）

```yaml
connections:
  - name: potential
    from: simulation.potential
    to: display.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    delivery: latest
```

`delivery: latest` にすると、受信側が**まだ受け取っていない**古い配列は、新しい配列が送られた時点で捨てられます。
送信側は、受信側の処理を待たずに進めるようになります。

```text
 sender                                    receiver
 ------                                    --------
 send(step 1)                               receive step 1, processing ...
 send(step 2)   (queued)
 send(step 3)   step 2 is dropped,
                step 3 is queued
 send(step 4)   step 3 is dropped,
                step 4 is queued            complete step 1
                                            receive step 4, processing ...
```

| | `delivery: all`（既定） | `delivery: latest` |
| --- | --- | --- |
| 届く配列 | 送ったものすべて | 受信側の処理が間に合った分だけ |
| 受信側が遅いとき | 送信側が待つ | 古い配列を捨てる |
| 向いている用途 | 保存、全ステップの解析、Task | 可視化、進行状況の表示 |

`latest` について知っておくこと：

- **処理中の配列は変わりません。** 捨てられるのは、受信側がまだ受け取っていない配列だけです。
  受信側が受け取って処理している最中の配列が、途中で差し替わることはありません。
- **最後に送った配列は届きます。** 送信側が終了するとき、最後に送った配列が処理されるまで待ちます。
- **受信側では番号が飛びます。** 届いた配列の `step` を見れば、どのステップの値かが分かります。
- **送信プロセスごとに独立して捨てられます。** 領域分割している場合、同じステップの配列が全プロセス分揃うとは限りません。
  このため、[Assembler](ASSEMBLY.md)とは一緒に使えません。全体を揃えて処理する必要があるときは `all` を使ってください。
- **`buffer_slots` は2以上が必要です。** 処理中の配列用と、次に届ける配列用です。既定値の2のままで使えます。
- **[Task](TASKS.md)には使えません。**

実行中に捨てられた件数は、`cotocoa status` の表示の `dropped` で確認できます（[実行状態を調べる](RUNNING.md#実行状態を調べる)）。

`delivery` は接続ごとに選べます。
同じ配列を「保存用には `all`、表示用には `latest`」で送る例が、
[複数の配列・複数の相手とつなぐ](CONNECTIONS.md#b-同じ配列を複数の相手へ送る)にあります。

## 送ったものが処理されるまで待つ

送信側で「ここまでに送ったものが、すべて受信側で処理された」ことを確認したい場合は、`flush` を呼びます。

| 言語 | 呼び出し |
| --- | --- |
| Python | `ctx.flush()` |
| Fortran | `call ctx%flush()` |
| C | `cotocoa_flush(ctx, &error)` |

`flush` は、そのプロセスが送ったすべての配列について、完了通知が届く（`latest` では、捨てられる場合も含む）まで待ちます。
Contextを閉じるときにも同じ待ちが行われるので、終了前に `flush` を呼ぶ必要はありません。
