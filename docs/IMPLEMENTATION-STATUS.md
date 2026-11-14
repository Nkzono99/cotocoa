# 高水準 API 0.3 の実装状況

作業先は `Nkzono99/cotocoa` の `design/high-level-api`。
高水準 API の改修は以下の範囲で完了した。本家への push・PR は行っていない。

| 段階 | 提供する機能 | 検証 |
| --- | --- | --- |
| 基本連携 | 全3言語の送受信、標準 Coupler、MPI 所有権、snapshot | 全9言語間、0件・処理例外・既存API・MPI借用を通過 |
| 配列 | int32/float32/float64、1〜3次元、C/F配置、正負の非連続スライス | 型・次元27ケースと Fortran 追加27ケースを通過 |
| 接続と Task | 複数port/program、fan-out、submit/next_task/complete、ticket、worker groups | 全9言語間×9型・次元をstream/Taskで交換、20ケース通過 |
| 配送と借用 | all/latest、処理中データの保持、借用配列・登録の寿命 | latest全9言語間、borrow54ケース、重複登録・解放・latest借用・fan-outを通過 |
| 全体の配列 | bounded Assembler、step/time/source照合、重なり・穴・重複・欠落の検出 | 全3言語の27ケース、3D tileと異常系のローカル契約テストを通過 |
| 起動と診断 | check/plan/run/probe/status、watchdog、native待機診断、永続記録 | 起動・異常系・実行例・旧Pythonを含む14ケース通過 |
| 導入と互換 | C-only、relocatable CMake install、Python wheel、旧ctca4py、fpm | C/Fortranの移動後リンク、clean archiveからのビルド・wheel・MPI連携、fpm build通過 |

設計と公開契約は [HIGH-LEVEL-API.md](HIGH-LEVEL-API.md) に集約した。
実行方法とジョブ単位の根拠は [VALIDATION.md](VALIDATION.md) に記録する。
確認した環境は KUDPC System A、Intel/Intel MPI、Python 3.12。
別の MPI 実装・launcher の実機検証はこの結果に含めない。

循環・双方向連成、同一processの複数Context/reopen、AMR、暗黙の単位変換、
Taskの任意戻り値や取消し、MPIプロセス障害からの復旧は公開契約の対象外。
Task ticket の wait は処理完了を意味する。
