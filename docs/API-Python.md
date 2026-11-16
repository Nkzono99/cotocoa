# Pythonで使う

[ドキュメント一覧](README.md) · [共通仕様](HIGH-LEVEL-API.md)

Pythonは送信側・受信側のどちらにも使えます。
配列にはNumPyを使い、通信の開始と終了は `Context` で管理します。

## 送受信の例

次は[入門例](HIGH-LEVEL-USAGE.md)と同じ設定で動くプログラムです。
引数に `producer` または `consumer` を渡して起動します。
全体の起動は `cotocoa run` が行います。

```python
import sys
import numpy as np
from cotocoa import Context

name = sys.argv[1]
with Context(name) as ctx:
    if name == "producer":
        rank = ctx.comm.Get_rank()
        for step in range(1, 11):
            field = np.full((3, 5, 7), step + rank, dtype=np.float64)
            ctx.send("potential", field, step=step, offset=(rank * 3, 0, 0))
    else:
        for message in ctx.receive("potential"):
            with message:
                print(message.step, message.source_rank, message.array.mean())
```

`with Context(...)` の終了時にContextを閉じます。
受信側の `with message:` は、ブロックが正常に終わったときに処理完了を通知します。
ブロック内で例外が発生すると連携ジョブを中断します。

## Context

`Context(name, *, config="cotocoa.yaml")` の `name` は設定のプログラム名です。
別の設定ファイルを使うときは `config` を指定します。
`cotocoa run` が設定を読み込み、各プロセスへ通信に必要な情報を渡します。

| メソッド・属性 | 用途 |
| --- | --- |
| `open()` / `close()` | 明示的な開始・終了。通常はwith文を使う |
| `comm` | 自分のプログラム、またはワーカーグループのmpi4py communicator |
| `send(name, array, *, step, time=None, offset=None)` | Streamへ配列をコピーして送る |
| `borrow(name, array, *, step=0, time=None, offset=None)` | Streamへ元配列を借用して送り、Ticketを返す |
| `submit(name, array, *, step=0, time=None, offset=None, borrow=False)` | Taskを依頼し、Ticketを返す |
| `receive(name=None)` | 入力を受信するイテレータ。StreamとTaskの混在にも対応 |
| `tasks(operation=None)` | Taskを受信するイテレータ |
| `flush()` | 自rankの全送信が完了するまで待つ |

`name`・`operation` はポート名です。
入力ポートが複数ある場合は、名前を省略して受信し、`message.port` で処理を分けます。
受信ループは終端まで回してください。

## 送る配列

`array` はnative byte orderの `np.int32`・`np.float32`・`np.float64` のndarrayで、
次元数は1〜3です。型とshapeは接続の設定に一致させます。
Pythonのリストや `int64` 配列の自動変換は行いません。

必要な型へ変換して送る例です。Contextを開いたproducer内で使います。

```python
field = np.asarray(values, dtype=np.float64)
ctx.send("potential", field, step=10, time=0.5)
```

`values` は利用者の配列です。スライスや逆順の配列も渡せます。
snapshotのsendが戻った後は、元の配列を変更できます。
borrowでは完了まで変更できません。詳しくは[配列の保持](DELIVERY-AND-SHARING.md)を参照してください。

## 受信したMessage

`message.array` はNumPyが所有する読み取り専用の配列です。
参照を保存すれば、Messageの完了後やContextの終了後も使えます。
値を変更する場合は `message.array.copy()` でコピーしてください。

`step`、`time`、`source_rank`、`offset` などは属性で取得します。
時刻が付いていない場合、`message.time` は `None` です。
ほかの属性は[メタデータ一覧](HIGH-LEVEL-API.md#配列とメタデータ)を参照してください。

with文を使わない場合は、処理後に `message.complete()` を1回呼びます。
完了する前に次のメッセージを受信することはできません。

## TicketとAssembler

`submit` と `borrow` が返すTicketは、受信側の処理完了を確認するためのものです。

| メソッド | 動作 |
| --- | --- |
| `test()` | 完了済みなら `True`、未完了なら `False` |
| `wait()` | 完了するまで待つ |
| `release()` | ハンドルを解放する。処理の取消しにはならない |

Taskの例は[複数接続とTask](TASKS-AND-PORTS.md)、
`Assembler` と完成した `Frame` の使い方は[領域の組み立て](ASSEMBLY.md)にあります。

## エラー

引数の型や値が不正な場合は `ValueError` など、ライブラリが検出したエラーは
`RuntimeError` として通知されます。MPI全体の整合を保てないエラーではジョブを中断します。
失敗したContextを開き直して再利用することはできません。

[起動とトラブルシューティング](DIAGNOSTICS.md)も参照してください。
