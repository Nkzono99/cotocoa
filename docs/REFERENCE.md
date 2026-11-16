# リファレンス

[ドキュメント目次](README.md)

調べものをするためのページです。使い方の説明は、次のページにあります。

- 考え方と用語 → [しくみと用語](CONCEPTS.md)
- 言語ごとの書き方 → [Fortran](GUIDE-FORTRAN.md)・[C](GUIDE-C.md)・[Python](GUIDE-PYTHON.md)
- 設定ファイルの項目と上限 → [設定ファイル](CONFIGURATION.md)
- コマンド → [起動と実行状態の確認](RUNNING.md)

## 3言語のAPI対応表

| 操作 | Fortran（`use cotocoa`） | C（`cotocoa.h`） | Python（`import cotocoa`） |
| --- | --- | --- | --- |
| Contextの型 | `type(ctca_context)` | `cotocoa_context *` | `Context` |
| 開く | `ctx%open(name)` | `cotocoa_open` | `with Context(name) as ctx:` |
| 閉じる | `ctx%close()` | `cotocoa_close` | `with` を抜ける |
| コミュニケータ | `ctx%comm()` | `cotocoa_comm` | `ctx.comm` |
| Streamへ送る | `ctx%send` | `cotocoa_send` | `ctx.send` |
| メッセージを受け取る | `ctx%receive` | `cotocoa_receive` | `ctx.receive` |
| 全ポートから受け取る | `ctx%receive('*', ...)` | `cotocoa_receive(ctx, NULL, ...)` | `ctx.receive()` |
| 配列を取り出す | `message%copy_to` | `cotocoa_message_view` | `message.array` |
| 付随情報を取り出す | `message%step()` など | `cotocoa_message_metadata` | `message.step` など |
| 完了を通知する | `message%complete()` | `cotocoa_complete` と `cotocoa_message_release` | `with message:`、または `message.complete()` |
| 送ったものの完了を待つ | `ctx%flush()` | `cotocoa_flush` | `ctx.flush()` |
| [Task](TASKS.md)を依頼する | `ctx%submit` | `cotocoa_submit` | `ctx.submit` |
| Taskを受け取る | `ctx%next_task` | `cotocoa_next_task` | `ctx.tasks` |
| [borrow](BORROW.md)でStreamへ送る | `ctx%borrow` | `cotocoa_borrow` | `ctx.borrow` |
| borrowでTaskを依頼する | `ctx%submit_borrow` | `cotocoa_submit_borrow` | `ctx.submit(..., borrow=True)` |
| Ticketの型 | `type(ctca_ticket)` | `cotocoa_ticket *` | `Ticket` |
| Ticket：完了したか調べる | `ticket%test()` | `cotocoa_ticket_test` | `ticket.test()` |
| Ticket：完了を待つ | `ticket%wait()` | `cotocoa_ticket_wait` | `ticket.wait()` |
| Ticket：控えを手放す | `ticket%release()` | `cotocoa_ticket_release` | `ticket.release()` |
| [Assembler](ASSEMBLY.md)を用意する | `assembler%open` | `cotocoa_assembler_create` | `Assembler(...)` |
| Assembler：メッセージを取り込む | `assembler%push` | `cotocoa_assembler_push` | `assembler.push` |
| Assembler：残りがないか確認する | `assembler%finish` | `cotocoa_assembler_finish` | `assembler.finish`（`with` を抜けるときに自動） |
| Assembler：解放する | `assembler%close` | `cotocoa_assembler_release` | `assembler.close`（`with` を抜けるときに自動） |
| エラーで中断する | `status` を省略する | `cotocoa_abort` | 例外を送出する |

Fortranの型名と手続き名は `ctca_` で始まりますが、従来APIの `ctca` モジュールとは別の、`cotocoa` モジュールのものです。

## 送れる配列

| 項目 | 対応 |
| --- | --- |
| 要素の型 | `int32`・`float32`・`float64` |
| 次元数 | 1〜3 |
| 形状 | 接続ごとに固定。送信側の全プロセスで共通 |
| メモリ上の並び | 連続した配列のほか、部分配列・スライス・逆順など、とびとびの並びも送れる |
| 軸の順序 | 全言語で共通（[軸の順序](CONCEPTS.md#軸の順序はどの言語でも同じ)） |

| `dtype` | Fortran | C | Python（NumPy） |
| --- | --- | --- | --- |
| `float64` | `real(c_double)` | `double` | `np.float64` |
| `float32` | `real(c_float)` | `float` | `np.float32` |
| `int32` | `integer(c_int32_t)` | `int32_t` | `np.int32` |

## メッセージに付く情報

受信したメッセージ（および[Assembler](ASSEMBLY.md)が返すFrame）から取り出せる情報です。

| 名前 | 内容 | 決まり方 |
| --- | --- | --- |
| `step` | 利用者が付ける番号（64ビット整数） | 送信時の引数 |
| `time` | 物理時刻（倍精度）。付けないこともできる | 送信時の引数 |
| `offset` | 全体領域の中での開始位置。0始まり | 送信時の引数 |
| `sequence` | その接続・その送信プロセスでの通し番号。1から始まる | 自動 |
| `source_rank` | 送信側プログラムの中でのプロセス番号。0始まり | 自動 |
| `source_count` | 送信側プログラムのプロセス数 | 設定ファイルの `ranks` |
| `source` | 送信側のプログラム名 | 設定ファイル |
| `port` | 受信側のポート名 | 設定ファイルの `to` |
| `name` | 接続の名前 | 設定ファイルの `name` |
| `shape` | 届いた配列の形状 | 設定ファイル |
| `global_shape` | 全体領域の形状 | 設定ファイル |
| `dtype`・`ndim` | 要素の型・次元数 | 設定ファイル |
| `axes` | 各軸の名前 | 設定ファイル |
| `units`・`time_unit` | 値の単位・時刻の単位 | 設定ファイル |
| `kind` | `stream` か `task` か | 設定ファイル |
| `delivery` | `all` か `latest` か | 設定ファイル |
| `mode` | `snapshot` か `borrow` か | 設定ファイル |

取り出し方は言語によって異なります。

| 言語 | 取り出し方 | 補足 |
| --- | --- | --- |
| Python | `message.step` のような属性 | `time` は、付いていなければ `None`。`kind`・`delivery`・`mode` は文字列。`dtype` はNumPyのdtype |
| Fortran | `message%step()` のような関数 | 時刻の有無は `message%has_time()`。`kind`・`delivery`・`mode`・`dtype` は整数で、`COTOCOA_STREAM`・`COTOCOA_TASK`・`COTOCOA_ALL`・`COTOCOA_LATEST`・`COTOCOA_SNAPSHOT`・`COTOCOA_BORROW`・`COTOCOA_FLOAT64`・`COTOCOA_FLOAT32`・`COTOCOA_INT32` の定数と比べる |
| C | `cotocoa_message_metadata` で得る `cotocoa_metadata` 構造体のメンバー | 時刻の有無は `has_time`。配列のメンバー（`shape`・`offset` など）は3要素で、使わない軸は `shape` が1、`offset` が0 |

Frameでは、`shape` が `global_shape` と同じ、`offset` がすべて0になります。
特定の送信プロセスに対応しないので、`source_rank` は−1、`sequence` は0です。

## 保証されること

| 項目 | 内容 |
| --- | --- |
| 届く件数（`delivery: all`） | 正常に終了したジョブでは、送ったメッセージがすべて1回ずつ届く |
| 届く件数（`delivery: latest`） | 受信側がまだ受け取っていない古いメッセージは、新しいメッセージで置き換えられる。最後に送ったメッセージは届く |
| 順序（Stream） | 同じ接続・同じ送信プロセスからのメッセージは、送った順に届く。異なる送信プロセス間、異なる接続間の順序は決まっていない |
| 順序（Task） | どのワーカーが受け取るか、どの順に完了するかは決まっていない |
| 受信中の配列 | 受け取った配列の内容が、後から変わることはない（`latest`・borrowでも） |
| 終端 | すべての送信側プログラムが終了し、送られたメッセージがすべて受信された後に、受信側へ知らされる |

## 配列をいつまで使えるか

### 送った配列

| 送り方 | 書き換え・解放してよい時点 |
| --- | --- |
| `send`・`submit`（snapshot） | 呼び出しが戻った直後から |
| `borrow`・`submit_borrow`（borrow） | 対応するTicketが完了した後。または `flush`・`close` が戻った後（[borrow](BORROW.md#いつ配列を書き換えてよいか)） |

### 受け取った配列

| 言語 | 使える期間 |
| --- | --- |
| Python | `message.array` はメッセージごとに作られるNumPy配列。参照を保存しておけば、完了通知やContextの終了の後も使える。読み取り専用 |
| Fortran | `copy_to` でコピーした配列は利用者のもの。完了通知やContextの終了の後も使える |
| C | `cotocoa_message_view` で得たviewが指す領域は、`cotocoa_message_release` を呼ぶまで有効。後で使う値はコピーしておく |

## 呼び出しの決まり

| 決まり | 理由 |
| --- | --- |
| Contextは、1プロセスにつき1つ。開いて閉じるのは1回だけ | 開く処理と閉じる処理は、ジョブ内の全プロセスが揃って行うため |
| 受信側は、メッセージを完了通知してから次を受信する | 1つのグループが同時に持てる未完了のメッセージは1件 |
| 受信側が複数プロセスのとき、グループ内の全プロセスが同じ順序で受信と完了通知を呼ぶ | 受信と完了通知は、グループ全体で揃って行う処理のため |
| 受信側は、終端まで受信してからContextを閉じる | 未受信のメッセージを待つ送信側が、止まらないようにするため |
| CoToCoAの手続きは、各プロセスの1つのスレッドから呼ぶ | 複数スレッドからの同時呼び出しには対応していない |
| 自分の計算のMPI通信には、Contextのコミュニケータを使う | `MPI_COMM_WORLD` には、相手のプログラムとCouplerが含まれるため |

## 環境変数

| 環境変数 | 用途 |
| --- | --- |
| `COTOCOA_LIBRARY` | Python APIが読み込む `libcotocoa.so` のパス。設定しない場合は、リポジトリの `build/` と標準のライブラリ検索先から探す |
| `COTOCOA_MANIFEST` | ライブラリが読む設定（manifest）のパス。`cotocoa run` が自動で設定する。[自分で起動する場合](RUNNING.md#cotocoa-runを使わずに起動する)は利用者が設定する |
| `COTOCOA_RUN_DIR` | 実行状態の記録先。`cotocoa run` が自動で設定する |

## 対応していないこと

[しくみと用語の「できないこと」](CONCEPTS.md#できないこと)にまとめています。
