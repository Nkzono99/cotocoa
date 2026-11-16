# CoToCoA ドキュメント

Fortran・C・Pythonのプログラム間で配列を送受信するための利用ガイドです。
初めて使う場合は、次の順に進めてください。

## 初めて使う

1. [インストール](INSTALL.md) — MPIライブラリとCLIの準備
2. [最初の連携を動かす](HIGH-LEVEL-USAGE.md) — Fortran同士の例から始め、CやPythonへ差し替える
3. [連携のしくみ](OVERVIEW.md) — プログラムの役割、領域の分割、正常終了

## 自分のプログラムへ組み込む

| 言語 | ガイド |
| --- | --- |
| Fortran | [型、送受信、MPI communicator、エラー処理](API-Fortran.md) |
| C | [配列view、送受信、メモリの解放、リンク方法](API-C.md) |
| Python | [Context、NumPy配列、受信ループ、Ticket](API-Python.md) |

接続先、プロセス数、配列の形状は[設定ファイル](CONFIGURATION.md)で指定します。

## 必要な機能を使う

| 目的 | 読むページ |
| --- | --- |
| 接続を増やす・処理を複数のワーカーへ分配する | [複数接続とTask](TASKS-AND-PORTS.md) |
| 全件配送と最新値優先を選ぶ・入力配列の寿命を管理する | [配送と配列の保持](DELIVERY-AND-SHARING.md) |
| 各MPI rankから届く領域を全体配列へまとめる | [領域の組み立て](ASSEMBLY.md) |
| Slurmで起動する・エラーや待ち状態を調べる | [起動とトラブルシューティング](DIAGNOSTICS.md) |
| APIの対応や機能の制約を調べる | [共通仕様](HIGH-LEVEL-API.md)・[API一覧](API.md) |

## 従来APIを使う

Requester・Coupler・Workerを自分で構成する場合は、[従来APIの利用ガイド](USAGE.md)へ進んでください。
Pythonの従来Workerは[ctca4pyの使い方](USAGE_from_Python.md)を参照してください。

[CoToCoAのトップへ](../README.md)
