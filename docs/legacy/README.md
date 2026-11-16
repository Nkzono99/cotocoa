# 従来APIのガイド

[ドキュメント目次](../README.md) ｜ [従来APIの関数一覧](API.md)

このディレクトリは、Requester・Coupler・Workerを自分で書く**従来のAPI**
（`CTCAR_*`・`CTCAC_*`・`CTCAW_*`）の文書です。既存のCoToCoAのプログラムは、そのまま使い続けられます。

これから新しく連携を作る場合は、設定ファイルとポート名でつなぐ高水準APIのほうが簡単です。
[チュートリアル](../TUTORIAL.md)から始めてください。

| | 高水準API | 従来API（このディレクトリ） |
| --- | --- | --- |
| ヘッダー・モジュール | `cotocoa.h`、Fortran `cotocoa`、Python `cotocoa` | `ctca.h`、Fortran `ctca`、Python `ctca4py` |
| Coupler | 付属のものを使う | 利用者が書く |
| 構成の指定 | YAMLの設定ファイル | 各プログラムの初期化の引数 |
| 起動 | `cotocoa run` | `mpiexec` などを利用者が直接呼ぶ |
| Workerから結果を返す | できない | Requesterが公開した共有領域へ書き込める |

高水準APIに付属するCoupler（`cotocoa-coupler`）は、従来APIの依頼を扱えません。
従来APIのRequesterとWorkerは、従来APIで書いたCouplerと組み合わせてください。

## 3つの役割

従来APIでは、次の3種類のプログラムを利用者が用意します。

| 役割 | すること | 関数 |
| --- | --- | --- |
| Requester | 計算の依頼を送る。必要に応じて、共有領域を公開する | [CTCAR_*](API-requester.md) |
| Coupler | 依頼を受け取り、どのWorkerへ渡すかを決めて配送する | [CTCAC_*](API-coupler.md) |
| Worker | 依頼を受け取って処理し、完了を通知する | [CTCAW_*](API-worker.md) |

Workerの `progid` は、Couplerが宛先を選ぶための番号です。
`procspercomm` は、1つのWorkerグループを構成するプロセス数です。
同じWorkerプログラムを複数のグループに分けて起動し、依頼を分担させることができます。

## 既存のMPIプログラムへ組み込む

Cでは `ctca.h` をインクルードし、Fortranでは `use ctca` とします。

1. MPIの初期化を、役割に応じた `CTCAR_init`・`CTCAC_init`・`CTCAW_init` に置き換える。
2. 計算に使っている `MPI_COMM_WORLD` を、`CTCA_subcomm` に置き換える。
3. 依頼の送信、受信、配送、完了通知を追加する。
4. 最後に、役割に応じた `*_finalize` を呼ぶ。

Requesterは、`sendreq` 系の関数で整数のパラメータを送ります。
整数・単精度・倍精度の配列を添付することもできます。
`_hdl` の付く関数はハンドルを返し、`CTCAR_wait`・`CTCAR_test` で依頼の完了を確認できます。

Couplerは、`pollreq` で依頼を受け取り、終了でないことを確認してから、`enqreq` でWorkerへ渡します。
Workerは、`pollreq`、終了の確認、処理、`CTCAW_complete` を繰り返します。
どちらも、終了が通知されるまでループを続けます。

共有領域を使う場合は、各役割で対応する `regarea` を呼び、得られた領域の番号を `readarea`・`writearea` に渡します。
引数の詳細は、各関数のリファレンスを確認してください。

## ビルドする

[インストール](../INSTALL.md)のCMakeビルドは、従来APIも含んでいます。
リンクするCMakeターゲットは、高水準APIと同じです。

| 言語 | インクルード・モジュール | CMakeターゲット |
| --- | --- | --- |
| C | `ctca.h` | `cotocoa::cotocoa` |
| Fortran | `use ctca` | `cotocoa::cotocoa_fortran` |
| Python（Workerのみ） | `ctca4py.CTCAWorker` | [別のパッケージを入れる](PYTHON-WORKER.md) |

fpmを使うFortranプロジェクトでは、依存関係として次のように指定できます。
ビルドにはMPIのコンパイララッパーを使ってください。

```toml
[dependencies]
ctca = { git = "https://github.com/Nkzono99/cotocoa.git", branch = "main" }
```

## 起動する

Requester・Coupler・Workerを、1つのMPIジョブとして起動します。
次は、Requester 2プロセス、Coupler 1プロセス、Worker 4プロセスの例です。

```sh
mpiexec -n 2 ./requester : -n 1 ./coupler : -n 4 ./worker
```

Workerが `procspercomm=2` で初期化すれば、2プロセスのグループが2つできます。
スーパーコンピュータでは、資源を確保した上で、その施設の方法で複数のプログラムを1つのジョブとして起動してください。

高水準API用の `cotocoa run` と設定ファイルは、従来APIの起動には使いません。

## ソースの例

| 内容 | Requester | Coupler | Worker |
| --- | --- | --- | --- |
| Cで配列を添付する | [sample1_requester.c](../../test/sample1_requester.c) | [sample1_coupler.c](../../test/sample1_coupler.c) | [sample1_worker1.c](../../test/sample1_worker1.c) |
| Fortranで配列を添付する | [sample2_requester.f90](../../test/sample2_requester.f90) | [sample2_coupler.f90](../../test/sample2_coupler.f90) | [sample2_worker1.f90](../../test/sample2_worker1.f90) |
| CからPythonのWorkerへ依頼する | [sample4_requester.c](../../test/sample4_requester.c) | [sample4_coupler.c](../../test/sample4_coupler.c) | [sample4_worker.py](../../test/sample4_worker.py) |

サンプルごとに、必要なWorkerの番号やグループの数が異なります。
起動する前に、Requesterの宛先の選び方と、Workerの初期化の引数を確認してください。

## このディレクトリの文書

| 文書 | 内容 |
| --- | --- |
| [API.md](API.md) | 従来APIの関数一覧への入口 |
| [API-requester.md](API-requester.md)・[API-coupler.md](API-coupler.md)・[API-worker.md](API-worker.md) | 役割ごとの関数一覧 |
| [api/](api/) | 関数ごとの引数と戻り値の説明（英語） |
| [PYTHON-WORKER.md](PYTHON-WORKER.md) | PythonでWorkerを書く `ctca4py` の使い方 |
