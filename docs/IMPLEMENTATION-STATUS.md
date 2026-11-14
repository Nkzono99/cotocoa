# 高水準 API 改修の完了条件

作業先は `Nkzono99/cotocoa` の `design/high-level-api`。
本家への push・PR は行わず、各段階を計算ノードで検証してコミットする。

| 段階 | 完了条件 | 状態 |
| --- | --- | --- |
| 基本連携 | 全3言語の送受信、標準 Coupler、check/plan/run、MPI 所有権、snapshot | 検証済み・コミット済み |
| 配列 | int32/float32/float64、1〜3次元、C/F配置、非連続スライス、全言語間の一致 | 28単体テスト・既存14+追加27 MPIケースを通過 |
| 接続と Task | 複数ポート・プログラム、fan-out、型付き submit/next_task/complete、ticket、複数 worker group | 31単体・既存14+追加20 MPIケースを通過 |
| 時系列と共有配列 | all/latest の明確な配送契約、借用配列の寿命と解放、step ごとの領域再構成 | 未完了 |
| 起動と診断 | 実行前 probe、タイムアウトと失敗診断、状態照会、移植可能な導入経路 | 未完了 |
| 最終検証 | 言語・型・次元・配送の組合せ、異常系、既存 API、実行例、README/docs、clean checkout | 未完了 |

循環する双方向連成、MPI プロセス障害からの復旧、AMR、暗黙の物理単位変換は
当初の設計で対象外の項目。これらを後続機能の実装完了と混同しない。
Task の完了待ちは処理完了を表し、任意の戻り値の RPC と区別する。

## 次の段階の契約

- latest は接続・送信元ごとに未配送の古いメッセージだけを破棄する。処理中の
  メッセージは保持する。Coupler の pending queue を破棄し、対応する requester
  handle を完了へ遷移させる内部 hook を使う。送信枠が満杯なら制御リクエストで
  未配送分を退避し、空いた枠へ最新の snapshot を入れる。最終送信は flush で待つ。
- 借用配列は送信側コピーを避ける別 API とし、動的 MPI window へ attach する。
  公開するのはアドレスと stride を持つ descriptor。受信 root が derived datatype
  を使って読み、ticket 完了まで attach と配列の寿命を保持する。Python は入力配列の
  参照も Context に保持し、ticket の早期解放で元配列が消えないようにする。
- 領域の再構成は MPI 通信とは独立した bounded な assembler とする。接続、step、
  時刻、shape、offset と全 source rank を照合し、重複、重なり、穴、容量超過を
  説明付きで拒否する。Message の完了と、再構成後の利用者の解析完了を区別する。
- 診断は native probe、操作の timeout/progress、run 全体の watchdog、永続状態記録を
  組み合わせる。blocking MPI 呼び出しに見せかけの timeout を付けたとは扱わない。
- 残る導入経路には relocatable native install、C-only build、旧 ctca4py の固定パスの
  解消と read/write の互換性検証を含める。
