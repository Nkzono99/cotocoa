# 名前付きAPIの共通仕様

[ドキュメント一覧](README.md) · [API一覧](API.md)

このページは、C・Fortran・Pythonに共通する仕様をまとめたリファレンスです。
初めて使う場合は[最初の連携](HIGH-LEVEL-USAGE.md)から始めてください。

## 言語間の対応

| 操作 | Fortran | C | Python |
| --- | --- | --- | --- |
| Context | `ctca_context` | `cotocoa_context` | `Context` |
| Stream送信 | `ctx%send` | `cotocoa_send` | `ctx.send` |
| Stream借用送信 | `ctx%borrow` | `cotocoa_borrow` | `ctx.borrow` |
| Task送信 | `ctx%submit` | `cotocoa_submit` | `ctx.submit` |
| Task借用送信 | `ctx%submit_borrow` | `cotocoa_submit_borrow` | `ctx.submit(borrow=True)` |
| Stream受信 | `ctx%receive` | `cotocoa_receive` | `ctx.receive` |
| Task受信 | `ctx%next_task` | `cotocoa_next_task` | `ctx.tasks` |
| 処理完了 | `message%complete` | `cotocoa_complete` | `message.complete` |
| 完了確認・待機 | `ticket%test/wait` | `cotocoa_ticket_test/wait` | `ticket.test/wait` |
| 全送信の完了待ち | `ctx%flush` | `cotocoa_flush` | `ctx.flush` |
| 領域の組み立て | `ctca_assembler` | `cotocoa_assembler` | `Assembler` |

各言語の引数・コード例は[Fortran](API-Fortran.md)・[C](API-C.md)・[Python](API-Python.md)を参照してください。

## 配列とメタデータ

配列は固定形状の1〜3次元で、`int32`・`float32`・`float64` に対応します。
CとPythonではバイト単位のstride、Fortranでは配列sectionによって非連続配列を指定できます。

shapeとaxesは論理的な軸順です。言語による記憶配置の違いはライブラリが扱います。
受信するCのviewは、付属のstrideに従って読んでください。
offsetはすべての言語で0始まりです。

| メタデータ | 意味 |
| --- | --- |
| `name` / `port` | 接続名 / 受信側のポート名 |
| `source` / `source_rank` / `source_count` | 送信プログラム名 / その中のrank / そのプログラムのrank数 |
| `step` / `sequence` | 利用者が付けるint64のステップ / ライブラリが付ける送信順序番号 |
| `time` | 任意の有限な倍精度時刻。時刻を付けたかどうかも保持する |
| `shape` / `global_shape` / `offset` | 局所形状 / 全体形状 / 局所領域の開始位置 |
| `dtype` / `axes` / `units` / `time_unit` | 配列の型、軸名、値と時刻の単位 |
| `kind` / `mode` / `delivery` | Stream・Task、snapshot・borrow、all・latestの区別 |

各source rankの配列は別々のメッセージです。
全領域の欠落や重複を確認して結合する場合は[Assembler](ASSEMBLY.md)を使います。

## 配送と完了

`all` は正常な1ジョブ内で受理した全メッセージを処理へ渡します。
Streamは接続・送信元rankごとの順序を保ちます。
異なるrank間の全体の順序や、Taskの処理完了順は保証しません。

`latest` は同じ接続・送信元の未配送分を置き換えます。
処理中のメッセージは変更しません。
借用送信のTicketは、全宛先の処理完了、または未配送での置換をもって完了します。

受信と完了通知はconsumerグループ内のcollective操作です。
各rankが同じ順に呼び、未完了のメッセージは1グループにつき1件にします。
受信した配列を後で使う場合の寿命は[配送と配列の保持](DELIVERY-AND-SHARING.md)を参照してください。

## ContextとMPI

Contextは1プロセスにつき1つで、開閉は1回です。
openとcloseには連携ジョブ全体が参加し、通信操作は各プロセスの1つのスレッドから呼びます。

MPI未初期化時はContextが初期化・終了します。初期化済みのMPIを利用した場合は終了しません。
利用者のcollectiveにはContextのcommunicatorを使ってください。
Contextを閉じた後、ほかの役割が `MPI_COMM_WORLD` のcollectiveに参加するとは限りません。

producerのcloseは残りの送信の完了を待ちます。
consumerは受信終端を確認してからcloseします。
Contextを閉じた後も、保持しているTicketの完了状態は確認できます。

## 対応範囲

| 対応すること | 対応しないこと |
| --- | --- |
| C・Fortran・Python間の全組み合わせ | 同一プログラムの送受信兼任、双方向・循環連携 |
| 固定の型・形状、非連続配列 | 可変形状、AMR、構造体、複素数 |
| 明示した軸・単位・領域位置の受け渡し | 自動の単位変換、補間、領域の再分割 |
| Taskの処理完了通知 | 任意の戻り値を返すRPC、取消し、自動再実行 |
| 実行中の診断と時間制限 | MPI障害からの復旧、永続配送、外部への書き込みの一度限りの実行保証 |

従来の共有領域の読み書きを使う場合は、[従来API](USAGE.md)を利用できます。
