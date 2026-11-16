# API一覧

[ドキュメント一覧](README.md)

## 名前付きAPI

設定ファイルでプログラムをつなぎ、ポート名で配列を送受信するAPIです。

| 調べたいこと | ページ |
| --- | --- |
| 言語間の対応、配列、MPI、対応範囲 | [共通仕様](HIGH-LEVEL-API.md) |
| Fortranの型と手続き | [Fortran API](API-Fortran.md) |
| Cの型と関数 | [C API](API-C.md) |
| Pythonのクラスとメソッド | [Python API](API-Python.md) |
| YAMLのキーと既定値 | [設定ファイル](CONFIGURATION.md) |
| CLIのコマンドと起動方法 | [起動とトラブルシューティング](DIAGNOSTICS.md) |

## 従来のRequester・Coupler・Worker API

既存の低水準APIを使う場合のリファレンスです。
全体の流れは[従来APIの利用ガイド](USAGE.md)を参照してください。

| 役割 | リファレンス |
| --- | --- |
| Requester：計算を依頼する | [CTCAR_*](API-requester.md) |
| Coupler：依頼を配送する | [CTCAC_*](API-coupler.md) |
| Worker：依頼を処理する | [CTCAW_*](API-worker.md) |
| Python Worker | [ctca4py](USAGE_from_Python.md) |
