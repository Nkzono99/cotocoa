# 従来API：Coupler

[ドキュメント一覧](README.md) · [従来APIの利用ガイド](USAGE.md) · [API一覧](API.md)

Requesterの依頼を受け取り、progidで選んだWorkerへ配送します。
各リンク先に引数と戻り値の詳細（英語）があります。

## 開始・終了と終了確認

| 関数 | 用途 |
| --- | --- |
| [CTCAC_init](api/ctcac_init.md) | 既定値で初期化 |
| [CTCAC_init_detail](api/ctcac_init_detail.md) | 容量を指定して初期化 |
| [CTCAC_finalize](api/ctcac_finalize.md) | 終了処理 |
| [CTCAC_isfin](api/ctcac_isfin.md) | Requester全体の終了を確認 |

## 受信と配送

| 関数 | 用途 |
| --- | --- |
| [CTCAC_pollreq](api/ctcac_pollreq.md) | 整数パラメータ付きの依頼を受信 |
| [CTCAC_enqreq](api/ctcac_enqreq.md) | Workerへ依頼を配送 |
| [CTCAC_pollreq_withint](api/ctcac_pollreq_withint.md) | 整数配列付きの依頼を受信 |
| [CTCAC_enqreq_withint](api/ctcac_enqreq_withint.md) | 整数配列を添付して配送 |
| [CTCAC_pollreq_withreal4](api/ctcac_pollreq_withreal4.md) | 単精度配列付きの依頼を受信 |
| [CTCAC_enqreq_withreal4](api/ctcac_enqreq_withreal4.md) | 単精度配列を添付して配送 |
| [CTCAC_pollreq_withreal8](api/ctcac_pollreq_withreal8.md) | 倍精度配列付きの依頼を受信 |
| [CTCAC_enqreq_withreal8](api/ctcac_enqreq_withreal8.md) | 倍精度配列を添付して配送 |

## 共有領域

| 関数 | 用途 |
| --- | --- |
| [CTCAC_regarea_int](api/ctcac_regarea_int.md) | 整数領域の登録に参加 |
| [CTCAC_regarea_real4](api/ctcac_regarea_real4.md) | 単精度領域の登録に参加 |
| [CTCAC_regarea_real8](api/ctcac_regarea_real8.md) | 倍精度領域の登録に参加 |
| [CTCAC_readarea_int](api/ctcac_readarea_int.md) | 整数領域の読み取り |
| [CTCAC_readarea_real4](api/ctcac_readarea_real4.md) | 単精度領域の読み取り |
| [CTCAC_readarea_real8](api/ctcac_readarea_real8.md) | 倍精度領域の読み取り |
| [CTCAC_writearea_int](api/ctcac_writearea_int.md) | 整数領域の書き込み |
| [CTCAC_writearea_real4](api/ctcac_writearea_real4.md) | 単精度領域の書き込み |
| [CTCAC_writearea_real8](api/ctcac_writearea_real8.md) | 倍精度領域の書き込み |

## 時間計測

| 関数 | 用途 |
| --- | --- |
| [CTCAC_prof_start](api/ctcac_prof_start.md) | 計測開始 |
| [CTCAC_prof_stop](api/ctcac_prof_stop.md) | 計測終了 |
| [CTCAC_prof_start_calc](api/ctcac_prof_start_calc.md) | 計算時間の計測区間を開始 |
| [CTCAC_prof_stop_calc](api/ctcac_prof_stop_calc.md) | 計算時間の計測区間を終了 |
