# 高水準 API 改修の完了条件

作業先は `Nkzono99/cotocoa` の `design/high-level-api`。
本家への push・PR は行わず、各段階を計算ノードで検証してコミットする。

| 段階 | 完了条件 | 状態 |
| --- | --- | --- |
| 基本連携 | 全3言語の送受信、標準 Coupler、check/plan/run、MPI 所有権、snapshot | 検証済み・コミット済み |
| 配列 | int32/float32/float64、1〜3次元、C/F配置、非連続スライス、全言語間の一致 | 28単体テスト・既存14+追加27 MPIケースを通過 |
| 接続と Task | 複数ポート・プログラム、fan-out、型付き submit/next_task/complete、ticket、複数 worker group | 未完了 |
| 時系列と共有配列 | all/latest の明確な配送契約、借用配列の寿命と解放、step ごとの領域再構成 | 未完了 |
| 起動と診断 | 実行前 probe、タイムアウトと失敗診断、状態照会、移植可能な導入経路 | 未完了 |
| 最終検証 | 言語・型・次元・配送の組合せ、異常系、既存 API、実行例、README/docs、clean checkout | 未完了 |

循環する双方向連成、MPI プロセス障害からの復旧、AMR、暗黙の物理単位変換は
当初の設計で対象外の項目。これらを後続機能の実装完了と混同しない。
Task の完了待ちは処理完了を表し、任意の戻り値の RPC と区別する。
