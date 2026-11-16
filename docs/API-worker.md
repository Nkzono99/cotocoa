# 従来API：Worker

[ドキュメント一覧](README.md) · [従来APIの利用ガイド](USAGE.md) · [API一覧](API.md)

依頼を受け取り、処理後に完了を通知します。Python版は[ctca4pyの使い方](USAGE_from_Python.md)を参照してください。
各リンク先に引数と戻り値の詳細（英語）があります。

## 開始・終了と終了確認

| 関数 | 用途 |
| --- | --- |
| [CTCAW_init](api/ctcaw_init.md) | progidとグループのrank数を指定して初期化 |
| [CTCAW_init_detail](api/ctcaw_init_detail.md) | 容量も指定して初期化 |
| [CTCAW_finalize](api/ctcaw_finalize.md) | 終了処理 |
| [CTCAW_isfin](api/ctcaw_isfin.md) | 依頼の受信終端を確認 |

## 受信と処理完了

| 関数 | 用途 |
| --- | --- |
| [CTCAW_pollreq](api/ctcaw_pollreq.md) | 整数パラメータ付きの依頼を受信 |
| [CTCAW_pollreq_withint](api/ctcaw_pollreq_withint.md) | 整数配列付きの依頼を受信 |
| [CTCAW_pollreq_withreal4](api/ctcaw_pollreq_withreal4.md) | 単精度配列付きの依頼を受信 |
| [CTCAW_pollreq_withreal8](api/ctcaw_pollreq_withreal8.md) | 倍精度配列付きの依頼を受信 |
| [CTCAW_complete](api/ctcaw_complete.md) | 依頼の処理完了を通知 |

## 共有領域

| 関数 | 用途 |
| --- | --- |
| [CTCAW_regarea_int](api/ctcaw_regarea_int.md) | 整数領域の登録に参加 |
| [CTCAW_regarea_real4](api/ctcaw_regarea_real4.md) | 単精度領域の登録に参加 |
| [CTCAW_regarea_real8](api/ctcaw_regarea_real8.md) | 倍精度領域の登録に参加 |
| [CTCAW_readarea_int](api/ctcaw_readarea_int.md) | 整数領域の読み取り |
| [CTCAW_readarea_real4](api/ctcaw_readarea_real4.md) | 単精度領域の読み取り |
| [CTCAW_readarea_real8](api/ctcaw_readarea_real8.md) | 倍精度領域の読み取り |
| [CTCAW_writearea_int](api/ctcaw_writearea_int.md) | 整数領域の書き込み |
| [CTCAW_writearea_real4](api/ctcaw_writearea_real4.md) | 単精度領域の書き込み |
| [CTCAW_writearea_real8](api/ctcaw_writearea_real8.md) | 倍精度領域の書き込み |

## 時間計測

| 関数 | 用途 |
| --- | --- |
| [CTCAW_prof_start](api/ctcaw_prof_start.md) | 計測開始 |
| [CTCAW_prof_stop](api/ctcaw_prof_stop.md) | 計測終了 |
| [CTCAW_prof_start_calc](api/ctcaw_prof_start_calc.md) | 計算時間の計測区間を開始 |
| [CTCAW_prof_stop_calc](api/ctcaw_prof_stop_calc.md) | 計算時間の計測区間を終了 |
