# 分割された領域を1つの配列にまとめる

[ドキュメント一覧](README.md) · 前提：[連携のしくみ](OVERVIEW.md)

複数のproducer rankが分割領域を送る場合、受信側には局所配列が1件ずつ届きます。
**Assembler**は同じstepの局所配列を集め、全領域が揃った時点で1枚のFrameを返します。

[入門例](HIGH-LEVEL-USAGE.md)では、shape `[3, 5, 7]` の2件を合わせて、
`[6, 5, 7]` の配列を作れます。

## Pythonで組み立てる

入門例のconsumerを次のコードに置き換えると、全領域の平均を表示できます。

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

`push` は受信メッセージをコピーして取り込みます。
そのstepがまだ揃わなければ `None`、揃えばFrameを返します。
Frameの `array` は全体配列、`step` は集めたステップです。

入力Messageの完了は `with message:` が行います。
**Frameにはcompleteを呼びません。**
Frameの配列はNumPyが所有し、AssemblerやContextの終了後も保持できます。
Assemblerのwith文は、正常終了時に未完成のstepがないことも確認します。

この例は全10stepを保持できるようにしています。完成配列1枚は1680バイトなので、
1枚の上限を4096バイトにしています。実際の配列と送信元の進行差に合わせて変更してください。

## Fortranで組み立てる

次はContextを開いたconsumerの受信ループです。
`global_field` の型と次元を接続に合わせます。

```fortran
! 宣言部
type(ctca_assembler) :: assembler
type(ctca_message) :: message, frame
real(c_double), allocatable :: global_field(:,:,:)
logical :: available, ready

! ctxを開いた後
call assembler%open(10, 4096_c_int64_t)
do
  call ctx%receive('potential', message, has_message=available)
  if (.not. available) exit
  call assembler%push(message, frame, ready)
  call message%complete()
  if (ready) then
    call frame%copy_to(global_field)
    print *, frame%step(), sum(global_field)/size(global_field)
    call frame%release()
  end if
end do
call assembler%finish()
call assembler%close()
```

完成したFrameは `release` で解放します。入力Messageは通常どおり `complete` します。
`finish` は未完成のstepを検出し、`close` はローカルの保持領域を解放します。

## Cで組み立てる

次の順に呼びます。各関数の戻り値は通常のC APIと同様に確認してください。

| 関数 | 用途 |
| --- | --- |
| `cotocoa_assembler_create` | 保持数とサイズ上限を指定して作成 |
| `cotocoa_assembler_push` | Messageを取り込む。出力frameがNULL以外なら完成 |
| `cotocoa_message_view` / `cotocoa_message_metadata` | 完成したFrameを読む |
| `cotocoa_message_release` | Frameを解放する |
| `cotocoa_assembler_finish` | 未完成のstepがないことを確認 |
| `cotocoa_assembler_release` | Assemblerを解放する |

pushは入力Messageを完了させません。入力には別途 `cotocoa_complete` とreleaseが必要です。

## 組み立てに必要な条件

Assemblerごとに、**1つのStream接続**を扱います。

- 接続は `kind: stream`・`delivery: all`。
- 各source rankは、組み立てる各stepについて1件ずつ送る。
- 各source rankのstepは、送信ごとに増加させる。
- 同じstepの局所領域が、global_shapeを隙間や重なりなく埋める。
- 同じstepでは時刻の有無と値を揃える。時刻の比較に許容誤差はない。

複数の入力接続を組み立てる場合は、接続ごとにAssemblerを用意します。
latestや、Taskを複数のワーカーグループに分配する接続には使えません。

## 保持するメモリと進行差

`max_pending_steps` は同時に組み立て途中にできるstep数で、1〜1024です。
`max_frame_bytes` は完成配列1枚の最大バイト数です。

必要な配列領域の上限は、おおむね
`max_pending_steps × max_frame_bytes` です。これに送信元の追跡情報や利用者が保持する完成配列が加わります。
Pythonの既定値は2step・1枚256 MiBです。

送信元ごとの進行差が上限を超えるとエラーになります。
上限を適切に増やすか、producer側でflushしてから、そのプログラムのcommunicatorでbarrierするなど、
送信元の進行を揃えてください。Assembler自体は送信元を同期しません。

取り込み完了を通知したMessageについて、producerは次へ進めます。
その通知は「全体配列の解析が終わった」という意味ではありません。
全体の解析完了まで送信を待たせる必要がある場合は、処理順序も含めて設計してください。
