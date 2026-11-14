# 配送とデータの保持

## all と latest

`delivery: all` は受理した各メッセージを通常稼働するジョブ内で1回ずつ処理へ渡す。
接続・送信元 rank ごとの順序を保ち、送信枠が埋まったら完了を待つ。
全ステップを必要とする処理では、この契約を使う。

`delivery: latest` は、同じ接続・送信元の未配送メッセージを新しいものへ置き換える。
既に Worker へ渡したメッセージは変更・破棄しない。
受信した `sequence` は増加するが、中間の値は飛ぶことがある。
異なる接続・送信元のメッセージ同士を置換しない。
最後の送信をさらに新しい値で置換しなければ、flush/close はその最後の処理も待つ。

```yaml
delivery: latest
buffer_slots: 2
```

latest には少なくとも2枠必要。処理中の値と置換可能な未配送の値を分けて保持する。
全枠が使われている場合、producer は Coupler へ未配送分の破棄を依頼する。
Coupler は未配送の request handle だけを完了へ遷移させ、producer がその枠を再利用する。
Worker の読み出しや処理が終わる前に、その Worker が参照する枠を再利用しない。

fan-out の接続ごとに all/latest を指定できる。all の遅い宛先が送信を待たせることは
通常の送信量制限として残る。Task は計算依頼を破棄しないため、delivery: all に限定する。

どちらの配送も、MPI プロセス障害後の復旧、永続化、外部副作用の exactly-once を
保証するものではない。複数 rank の shard が同時刻であることも配送だけでは保証しない。

## 検証

`sbatch -p gr20001a tests/kudpc-latest.sbatch` のジョブ `9175741` は
`-t 0:10:00`、`--rsc p=5:t=1:c=1` で全9言語間を検証した。
各2 rankの producer が50件ずつ送り、遅い2 rankの consumer について、
中間の破棄、受信順の単調増加、snapshot の保持、両送信元の最後の値を確認した。
ビルドと単体テスト31件は計算ノードのジョブ `9175736` で通過した。

## snapshot と borrow

`mode: snapshot`（既定）の `send/submit` は、戻る前に入力を送信枠へコピーする。
戻ったら元の配列を変更・解放できる。受信側の処理完了は `flush` または Task の
ticket で待つ。

`mode: borrow` は元配列を動的 MPI window に登録し、送信側の配列コピーを省く。
受信側はローカル snapshot を取得する。通信全体がコピーなしになるという保証ではない。
元配列を保持し、ticket の wait/test が完了するか、Context の flush/close が戻るまで
変更しない。ticket の release は取消しや配列の解放通知ではない。
latest の ticket 完了には、未配送で破棄された場合も含まれる。

```python
# 接続設定: mode: borrow
ticket = ctx.borrow("field", field[::2], step=step)
ticket.wait()
# ここから field を変更できる

# Task 接続でも同じ寿命契約
ticket = ctx.submit("analyze", field, step=step, borrow=True)
ticket.wait()
```

Python は入力 ndarray の参照を Context に保持する。ticket を先に捨てても配列は
保持されるが、別の参照からの書き換えを防ぐことはできない。
完了確認は send/borrow、ticket の test/wait、flush/close で進め、背景スレッドを使わない。
大きな配列の参照を早く外す場合は明示的に flush する。

C は `cotocoa_borrow` / `cotocoa_submit_borrow` を使う。呼び出し側が配列の寿命を管理する。
stream の fan-out は全宛先を表す1枚の ticket を返す。

```fortran
real(c_double), target, asynchronous :: field(6,5)
type(ctca_ticket) :: ticket
! 接続の shape は [3,5]
call ctx%borrow('field',field(1:6:2,:),ticket,step=step)
call ticket%wait()
call ticket%release()
```

Fortran の元変数は `target, asynchronous` として宣言する。配列式や変換式など、
呼び出し後に消える一時配列は渡さない。Task 用の名前は `submit_borrow`。
借用配列は全3型・1〜3次元に対応し、C/Python の正負の stride、Fortran の
非連続配列 section をその descriptor で公開する。

同じ登録範囲、またはその中に収まる範囲は登録を共有する。一部だけ重なる別範囲は、
先の ticket の完了を待ってから登録する。これは MPI の attach 重複禁止に従うため。
登録先を detach してから配列の保持を外す。
[MPI の動的 window と attach の規則](https://www.mpi-forum.org/docs/mpi-3.1/mpi31-report/node264.htm) に基づく。

借用 stream/Task の54ケースはジョブ `9178240` で通過した。
