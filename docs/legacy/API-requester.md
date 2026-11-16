# 従来API：Requester

[ドキュメント目次](../README.md) ｜ [従来APIのガイド](README.md) ｜ [従来APIの関数一覧](API.md)

計算依頼を送る側です。完了待ちや、Workerからアクセスする共有領域の登録も行います。
各リンク先に引数と戻り値の詳細（英語）があります。

## 開始と終了

| 関数 | 用途 |
| --- | --- |
| [CTCAR_init](api/ctcar_init.md) | 既定値で初期化 |
| [CTCAR_init_detail](api/ctcar_init_detail.md) | 容量を指定して初期化 |
| [CTCAR_finalize](api/ctcar_finalize.md) | 依頼の終了を通知して終了 |

## 依頼の送信

| 関数 | 用途 |
| --- | --- |
| [CTCAR_sendreq](api/ctcar_sendreq.md) | 整数パラメータを送る |
| [CTCAR_sendreq_hdl](api/ctcar_sendreq_hdl.md) | 整数パラメータを送り、完了待ちハンドルを取得 |
| [CTCAR_sendreq_withint](api/ctcar_sendreq_withint.md) | 整数配列を添付して送る |
| [CTCAR_sendreq_withint_hdl](api/ctcar_sendreq_withint_hdl.md) | 整数配列を添付し、完了待ちハンドルを取得 |
| [CTCAR_sendreq_withreal4](api/ctcar_sendreq_withreal4.md) | 単精度配列を添付して送る |
| [CTCAR_sendreq_withreal4_hdl](api/ctcar_sendreq_withreal4_hdl.md) | 単精度配列を添付し、完了待ちハンドルを取得 |
| [CTCAR_sendreq_withreal8](api/ctcar_sendreq_withreal8.md) | 倍精度配列を添付して送る |
| [CTCAR_sendreq_withreal8_hdl](api/ctcar_sendreq_withreal8_hdl.md) | 倍精度配列を添付し、完了待ちハンドルを取得 |

## 完了確認

| 関数 | 用途 |
| --- | --- |
| [CTCAR_test](api/ctcar_test.md) | ハンドルに対応する依頼の完了を確認 |
| [CTCAR_wait](api/ctcar_wait.md) | ハンドルに対応する依頼の完了を待つ |

## 共有領域

| 関数 | 用途 |
| --- | --- |
| [CTCAR_regarea_int](api/ctcar_regarea_int.md) | 整数配列を読み書きできる領域として公開 |
| [CTCAR_regarea_real4](api/ctcar_regarea_real4.md) | 単精度配列を読み書きできる領域として公開 |
| [CTCAR_regarea_real8](api/ctcar_regarea_real8.md) | 倍精度配列を読み書きできる領域として公開 |

## 時間計測

| 関数 | 用途 |
| --- | --- |
| [CTCAR_prof_start](api/ctcar_prof_start.md) | 計測開始 |
| [CTCAR_prof_stop](api/ctcar_prof_stop.md) | 計測終了 |
| [CTCAR_prof_start_total](api/ctcar_prof_start_total.md) | 全体時間の計測区間を開始 |
| [CTCAR_prof_stop_total](api/ctcar_prof_stop_total.md) | 全体時間の計測区間を終了 |
| [CTCAR_prof_start_calc](api/ctcar_prof_start_calc.md) | 計算時間の計測区間を開始 |
| [CTCAR_prof_stop_calc](api/ctcar_prof_stop_calc.md) | 計算時間の計測区間を終了 |
