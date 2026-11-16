# 分割された配列をまとめる（Assembler）

[ドキュメント目次](README.md) ｜ 前提：[チュートリアル](TUTORIAL.md)・[しくみと用語](CONCEPTS.md)

送信側が領域分割している場合、受信側には各プロセスの担当分が別々のメッセージとして届きます
（[領域分割された配列の送り方](CONCEPTS.md#領域分割された配列の送り方)）。
**Assembler** は、同じステップのメッセージを集めて、全体領域の1つの配列にまとめる道具です。

```text
 messages received                              assembled

 step 1, source 0, offset [0,0,0]  --+
                                     +-- push -->  Frame: step 1, shape [6,5,7]
 step 1, source 1, offset [3,0,0]  --+

 step 2, source 1, offset [3,0,0]  --+
                                     +-- push -->  Frame: step 2, shape [6,5,7]
 step 2, source 0, offset [0,0,0]  --+
```

受信したメッセージをAssemblerへ1件ずつ渡していくと（`push`）、
あるステップの全プロセス分が揃った時点で、まとめた結果が返ります。この結果を **Frame** と呼びます。

Assemblerは受信側のプログラムの中だけで動く道具で、通信は行いません。
メッセージの受信と完了通知は、これまでどおり利用者のコードで行います。

## Pythonで使う

[チュートリアル](TUTORIAL.md)の例（1プロセスあたり `[3, 5, 7]`、全体 `[6, 5, 7]`）で、
受信側を次のプログラムに置き換えると、全体領域の平均値を表示できます。

```python
from cotocoa import Assembler, Context

with Context("consumer") as ctx:
    with Assembler(max_pending_steps=10, max_frame_bytes=4096) as assembler:
        for message in ctx.receive("potential"):
            with message:
                frame = assembler.push(message)
                if frame is not None:
                    print(frame.step, frame.array.shape, frame.array.mean())
```

```text
1 (6, 5, 7) 1.5
2 (6, 5, 7) 2.5
...
10 (6, 5, 7) 10.5
```

- `assembler.push(message)` は、メッセージの配列を全体配列の該当位置へコピーします。
  そのステップがまだ揃っていなければ `None` を、揃えばFrameを返します。
- `frame.array` が全体領域の配列、`frame.step` がステップ番号です。
  `frame.time` など、メッセージと同じ属性も使えます。
- メッセージの完了通知は、これまでどおり `with message:` が行います。**Frameには完了通知は要りません。**
- `frame.array` は普通のNumPy配列なので、保存しておけば後からも使えます。
- `with Assembler(...)` のブロックを正常に抜けるとき、揃わないまま残ったステップがあればエラーになります。

`Assembler` の2つの引数については、[上限の決め方](#上限の決め方)で説明します。

## Fortranで使う

```fortran
type(ctca_assembler) :: assembler
type(ctca_message) :: message, frame
real(c_double), allocatable :: global_field(:,:,:)
logical :: has_message, ready

call assembler%open(10, 4096_c_int64_t)
do
  call ctx%receive('potential', message, has_message)
  if (.not. has_message) exit

  call assembler%push(message, frame, ready)
  call message%complete()

  if (ready) then
    call frame%copy_to(global_field)
    print *, frame%step(), sum(global_field) / size(global_field)
    call frame%release()
  end if
end do
call assembler%finish()
call assembler%close()
```

| 呼び出し | すること |
| --- | --- |
| `assembler%open(max_pending_steps, max_frame_bytes)` | Assemblerを用意する。第2引数は `integer(c_int64_t)` |
| `assembler%push(message, frame, ready)` | メッセージを取り込む。揃えば `ready` が真になり、`frame` にFrameが入る |
| `frame%copy_to(array)`・`frame%step()` など | Frameから配列や情報を取り出す。メッセージと同じ手続きが使える |
| `frame%release()` | Frameを解放する。次の `push` の前に呼ぶ |
| `assembler%finish()` | 揃わないまま残ったステップがないことを確認する。残っていればエラー |
| `assembler%close()` | Assemblerを解放する |

Frameは `type(ctca_message)` の変数で受け取りますが、**`complete` ではなく `release` を呼びます**。
完了通知（`complete`）が必要なのは、受信したメッセージ（上の `message`）のほうです。
`push` はメッセージの内容をコピーするので、`push` の直後に `message%complete()` を呼んで構いません。

## Cで使う

```c
cotocoa_assembler *assembler = NULL;
if (cotocoa_assembler_create(10, 4096, &assembler, &error))
    cotocoa_abort(ctx, &error);

for (;;) {
    cotocoa_message *message = NULL, *frame = NULL;
    int rc = cotocoa_receive(ctx, "potential", &message, &error);
    if (rc == COTOCOA_END) break;
    if (rc != COTOCOA_OK) cotocoa_abort(ctx, &error);

    if (cotocoa_assembler_push(assembler, message, &frame, &error))
        cotocoa_abort(ctx, &error);
    if (cotocoa_complete(message, &error))
        cotocoa_abort(ctx, &error);
    cotocoa_message_release(message);

    if (frame) {
        cotocoa_array_view view;
        cotocoa_metadata meta;
        cotocoa_message_view(frame, &view, &error);
        cotocoa_message_metadata(frame, &meta, &error);
        /* view（全体領域の配列）と meta.step を使って処理する。 */
        cotocoa_message_release(frame);
    }
}

if (cotocoa_assembler_finish(assembler, &error))
    cotocoa_abort(ctx, &error);
cotocoa_assembler_release(assembler);
```

Frameは `cotocoa_message *` として得られ、配列と情報の読み方は受信メッセージと同じです。
Frameには `cotocoa_complete` を呼ばず、`cotocoa_message_release` だけを呼びます。

## 使える条件

Assemblerは、次の条件を満たす接続で使えます。満たさないメッセージを `push` するとエラーになります。

| 条件 | 理由 |
| --- | --- |
| 接続が `kind: stream`、`delivery: all` である | `latest` ではプロセスごとに別のステップが届くことがあり、揃わない |
| 各送信プロセスが、各ステップについて**ちょうど1件**送る | 同じ `step` のメッセージを、全プロセス分集めて1つのFrameにするため |
| 各送信プロセスの `step` が、送るたびに**増えていく** | 同じステップの重複を検出するため |
| 同じステップの配列が、全体領域を**すき間も重なりもなく**埋める | 欠けや重複のある配列を、黙って返さないため |
| 同じステップの配列の `time` が一致する（すべて時刻なし、またはすべて同じ値） | 異なる時刻の配列を混ぜないため |

1つのAssemblerが扱うのは、1つの接続です。
複数の接続をまとめたい場合は、接続ごとにAssemblerを用意し、メッセージの `port` や `name` を見て渡し分けます。

## 上限の決め方

Assemblerを作るときに、2つの上限を指定します。

| 引数 | 意味 | 範囲 |
| --- | --- | --- |
| `max_pending_steps` | 同時に「集めている途中」にできるステップの数 | 1〜1024 |
| `max_frame_bytes` | 全体配列1つの大きさの上限（バイト） | 1以上 |

Pythonでは省略でき、既定値は `max_pending_steps=2`、`max_frame_bytes=256*1024*1024`（256 MiB）です。

Assemblerが使うメモリは、最大でおよそ `max_pending_steps × 全体配列の大きさ` です。

### max_frame_bytes

全体配列の大きさ（要素数 × 1要素のバイト数）以上の値にします。
チュートリアルの例では 6×5×7×8 = 1680 バイトなので、4096 を指定しています。
設定の間違いで巨大な配列を確保してしまうのを防ぐための上限です。

### max_pending_steps

送信側のプロセスは、それぞれ自分のペースで送信します。
あるプロセスがステップ3まで送った時点で、別のプロセスはまだステップ1を送っていない、ということが起こりえます。
このときAssemblerは、ステップ1・2・3の3つを「集めている途中」として保持します。
この数が `max_pending_steps` を超えると、エラーになります。

必要な値は、**送信プロセス間で、送信済みのステップがどれだけずれるか**で決まります。

| 送信側のプログラム | ずれ | `max_pending_steps` の目安 |
| --- | --- | --- |
| 各ステップでプロセス間の通信（袖領域の交換など）を行う、通常の領域分割シミュレーション | プロセスは足並みを揃えて進むので、ずれは1ステップ程度 | 2（既定値） |
| プロセス同士が通信せず、独立に進む（チュートリアルの例） | 制限がない | 送る全ステップ数。または下の方法で足並みを揃える |

独立に進むプログラムで足並みを揃えるには、送信側で数ステップごとに
「`flush` を呼び、続けて自分のコミュニケータで `MPI_Barrier` を呼ぶ」ようにします。
こうすると、全プロセスがそこまでの送信を終えてから次へ進みます。

## 完了通知のタイミング

送信側は、メッセージが完了通知されると、そのメッセージを「処理済み」と見なして先へ進みます。

上のFortranとCの例では、`push` の直後に完了通知をしています。
この書き方では、Frameを使った処理は完了通知の後に行われます。
そのため、送信側の `flush` や `close` が戻った時点で、受信側の処理が終わっているとは限りません。

Frameの処理が終わるまで送信側を待たせたい場合は、Pythonの例のように、
Frameの処理を済ませてからメッセージを完了通知してください。
