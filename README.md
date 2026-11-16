# CoToCoA

**Fortran・C・Python のプログラムを、MPIでつなぐ。**

CoToCoA（Code To Code Adapter）は、計算プログラム間で配列を受け渡すためのライブラリです。
たとえば、Fortranのシミュレーションが計算した電位を、別のFortranプログラムやPythonの解析へ送れます。
送信側と受信側は、3言語を自由に組み合わせられます。

プログラムには配列を送る処理・受け取る処理を書き、接続先やMPIプロセス数はYAMLで指定します。
配送を担うCouplerは付属しているので、名前付きAPIを使う場合は自作する必要がありません。

## はじめる

1. [インストール](docs/INSTALL.md)：ライブラリとコマンドを準備する
2. [最初の連携を動かす](docs/HIGH-LEVEL-USAGE.md)：付属の例で配列を送受信する
3. [連携のしくみ](docs/OVERVIEW.md)：役割、配列の分割、処理完了を理解する

自分のコードへ組み込むときは、[Fortran](docs/API-Fortran.md)・[C](docs/API-C.md)・[Python](docs/API-Python.md)のガイドを参照してください。
[ドキュメント一覧](docs/README.md)から、目的に合うページを選べます。

## できること

| やりたいこと | 使う機能 |
| --- | --- |
| 計算の各ステップの配列を送る | [Stream](docs/HIGH-LEVEL-USAGE.md) |
| 複数の解析へ同じ配列を送る | [複数の接続](docs/TASKS-AND-PORTS.md) |
| 独立した処理を空いているワーカーへ依頼する | [Task](docs/TASKS-AND-PORTS.md#計算をワーカーへ依頼する) |
| 遅い可視化へ新しい値を優先して送る | [latest配送](docs/DELIVERY-AND-SHARING.md) |
| 送信時の配列コピーを省く | [borrow](docs/DELIVERY-AND-SHARING.md#送信時のコピーを省く) |
| 分割された領域を1つの配列へまとめる | [Assembler](docs/ASSEMBLY.md) |
| 起動の失敗や待ち状態を調べる | [起動とトラブルシューティング](docs/DIAGNOSTICS.md) |

扱える配列は `int32`・`float32`・`float64` の1〜3次元です。
型と形状は接続ごとに固定し、スライスなどの非連続配列も送れます。
名前付きAPIは一方向の連携に対応します。双方向の接続や可変形状の配列が必要な場合は、
[対応範囲](docs/HIGH-LEVEL-API.md)を確認してください。

## 既存のCoToCoAを使っている方へ

`ctca.h`、Fortranの `ctca` モジュール、Pythonの `ctca4py.CTCAWorker` も利用できます。
既存のRequester・Coupler・Workerを使う手順は、[従来APIの利用ガイド](docs/USAGE.md)にまとめています。

このリポジトリは [tnanri/cotocoa](https://github.com/tnanri/cotocoa) を基にした
[Nkzono99/cotocoa](https://github.com/Nkzono99/cotocoa) です。
本書は、このリポジトリの `main` で提供する機能を説明します。
ライセンスは [LICENSE](LICENSE) を参照してください。

CoToCoAの開発者：Takeshi Nanri、Yuto Katoh、Keiichiro Fukazawa、Yohei Miyake、
Kazuya Nakazawa、Jingde Zhow、Youhei Sunada、Haichao Zhao。
