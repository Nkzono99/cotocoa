# 従来APIの関数一覧

[ドキュメント目次](../README.md) ｜ [従来APIのガイド](README.md)

Requester・Coupler・Workerを自分で書く従来のAPIの関数一覧です。
全体の流れは[従来APIのガイド](README.md)を参照してください。

設定ファイルとポート名でつなぐ高水準API（`cotocoa.h`、Fortranの `cotocoa`、Pythonの `cotocoa`）の一覧は、
[リファレンス](../REFERENCE.md)にあります。

| 役割 | 関数一覧 |
| --- | --- |
| Requester：計算を依頼する | [CTCAR_*](API-requester.md) |
| Coupler：依頼を配送する | [CTCAC_*](API-coupler.md) |
| Worker：依頼を処理する | [CTCAW_*](API-worker.md) |
| PythonのWorker | [ctca4py](PYTHON-WORKER.md) |

各関数の引数と戻り値の説明（英語）は、[api/](api/) にあります。
