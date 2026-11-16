# 配送と配列の保持

[ドキュメント一覧](README.md) · 関連：[設定ファイル](CONFIGURATION.md)

まずは既定の `delivery: all`・`mode: snapshot` で使ってください。
全ステップを受け取り、sendが戻った後は元配列をすぐに更新できます。
途中の値を省きたい場合や、送信時のコピーを減らしたい場合に設定を変えます。

## 全件を送るか、新しい値を優先するか

| delivery | 向いている用途 | 動作 |
| --- | --- | --- |
| `all`（既定） | 保存、全ステップの解析、Task | すべてのメッセージを処理へ渡す |
| `latest` | 可視化、進行状況の表示 | 同じ送信元の未配送分を新しい値へ置き換える |

Streamのallは、接続・送信元rankごとの順序を保ちます。
送信枠が満杯になったら、受信側の完了を待ちます。

latestは**まだ配送していない**データだけを置き換えます。
受信側が処理中の配列は変わりません。受信したsequenceは増加しますが、間の番号が飛ぶことがあります。
最後に送った値は、その後の値で置き換えられない限り、flushやcloseが処理完了を待ちます。

接続内に次を指定するとlatestになります。

```yaml
delivery: latest
buffer_slots: 2
```

latestには2枠以上が必要です。送信元rankごとに置き換えるため、
複数rankの配列が同じstepで揃うとは限りません。
全体配列の組み立てが必要な処理ではallを使ってください。
Taskもallのみ対応します。

## 元の配列をいつ変更できるか

| mode | 送信方法 | 元配列を変更・解放できる時点 |
| --- | --- | --- |
| `snapshot`（既定） | ライブラリの送信枠へコピーする | send・submitが戻った後 |
| `borrow` | 元配列を通信に使うため借用する | 対応するTicketの完了後、またはflush・closeが戻った後 |

どちらも、受信側はローカルな配列としてデータを取得します。
borrowは送信側のコピーを省く機能で、通信全体をコピーなしにする保証ではありません。

## 送信時のコピーを省く

接続に `mode: borrow` を設定し、専用の送信APIを使います。
以下のコードはContextを開いたproducer内の例です。

### Python

```python
# fieldは接続に合う型・形状のNumPy配列。
ticket = ctx.borrow("potential", field, step=10)
ticket.wait()
ticket.release()
field.fill(0)  # 完了後なので変更できる。
```

Pythonは元配列への参照を保持します。ただし、別の参照からの書き換えは防ぎません。
スライスを送る場合も、その元配列を含めて変更しないでください。
Taskでは `ctx.submit(..., borrow=True)` を使います。

### Fortran

元配列を `target, asynchronous` として宣言します。
以下は `shape: [3, 5]` の接続へ、1行おきのsectionを送る例です。

```fortran
! 宣言部
real(c_double), target, asynchronous :: field(6,5)
type(ctca_ticket) :: ticket

! ctxを開き、fieldを計算した後
call ctx%borrow('potential', field(1:6:2,:), ticket, step=10_c_int64_t)
call ticket%wait()
call ticket%release()
```

配列式や型変換式など、呼び出し後に消える一時配列はborrowへ渡さないでください。
Taskには `ctx%submit_borrow` を使います。

### C

`cotocoa_borrow`、Taskでは `cotocoa_submit_borrow` を使います。
配列を確保したまま保持し、Ticketの完了を待ってから変更・解放します。
非連続配列の指定方法は[C API](API-C.md#配列viewを作る)を参照してください。

同じ配列の重なる範囲を同時にborrowする場合、
範囲が部分的にだけ重なる送信は先行する送信の完了を待つことがあります。
独立して進めたい送信には、別の配列を使うと寿命を管理しやすくなります。

## Ticketを解放することと、送信が完了すること

`ticket.release()` はハンドルを手放す操作です。
処理の取消しにも、借用配列を変更してよいという通知にもなりません。

借用配列を再利用するときは、次のいずれかを確認してください。

- 対応するTicketの `wait` が戻った。
- 対応するTicketの `test` が完了を返した。
- Contextの `flush` または `close` が戻った。

Streamを複数宛先へ送った場合、1枚のTicketが全宛先を表します。
latestのTicket完了には、未配送のまま新しい値に置き換えられた場合も含まれます。

## 受信した配列はいつまで使えるか

| 言語 | 保持できる期間 |
| --- | --- |
| C | viewは `cotocoa_message_release` まで有効。後で使う場合はコピーする |
| Fortran | `copy_to` でコピーした配列はMessage・Contextの終了後も利用できる |
| Python | `message.array` はNumPy所有のコピー。参照を保存すればContext終了後も利用できる |

受信配列をコピーして保存した場合でも、元のMessageへの完了通知は必要です。
Pythonの配列は読み取り専用なので、変更する場合は `.copy()` で作業用配列を作ってください。
