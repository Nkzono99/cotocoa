# 従来APIの利用ガイド

[ドキュメント一覧](README.md) · [API一覧](API.md)

このページは `CTCAR_*`・`CTCAC_*`・`CTCAW_*` を使うプログラム向けです。
名前付きポートとYAMLで連携する場合は、[最初の連携](HIGH-LEVEL-USAGE.md)を参照してください。

## 役割を決める

従来APIでは、次の3種類のプログラムを利用者が用意します。

| 役割 | すること | API |
| --- | --- | --- |
| Requester | 計算依頼を送る。必要に応じて共有領域を公開する | [CTCAR_*](API-requester.md) |
| Coupler | 依頼を受け取り、対象のWorkerプログラムへ配送する | [CTCAC_*](API-coupler.md) |
| Worker | 依頼を処理し、完了を通知する | [CTCAW_*](API-worker.md) |

Workerの `progid` は、Couplerが宛先を選ぶためのIDです。
`procspercomm` は1つのWorkerグループを構成するrank数です。
同じWorkerプログラムを複数グループ起動して、依頼を分担できます。

従来APIでは独自のCouplerを使います。
名前付きAPI用に付属する `cotocoa-coupler` とは、依頼の形式と配送処理が異なります。

## 既存のMPIプログラムへ組み込む

Cでは `ctca.h`、Fortranでは `use ctca` を使います。

1. MPIの初期化を、その役割の `CTCAR_init`・`CTCAC_init`・`CTCAW_init` に置き換える。
2. 計算に使う `MPI_COMM_WORLD` を `CTCA_subcomm` に置き換える。
3. 依頼の送信、受信、配送、完了通知を追加する。
4. 最後に役割に対応した `*_finalize` を呼ぶ。

Requesterは `sendreq` 系の関数で整数パラメータを送ります。
必要なら整数・単精度・倍精度の配列を添付できます。
`_hdl` 付きの関数はハンドルを返すので、`CTCAR_wait`・`CTCAR_test` で完了を確認できます。

Couplerは `pollreq` で依頼を受け取り、終了を確認してから `enqreq` でWorkerへ渡します。
Workerは `pollreq`、終了確認、処理、`CTCAW_complete` を繰り返します。
いずれも終了通知を受け取るまでループを続けます。

共有領域を使う場合は、各役割で対応する `regarea` を呼び、
取得したarea IDを `readarea`・`writearea` に渡します。
引数や集団操作の条件は、各関数のリファレンスを確認してください。

## ビルドする

[インストール](INSTALL.md)のCMakeビルドは、従来APIも含んでいます。
利用側は名前付きAPIと同じCMakeターゲットへリンクできます。

| 言語 | インクルード・モジュール | CMakeターゲット |
| --- | --- | --- |
| C | `ctca.h` | `cotocoa::cotocoa` |
| Fortran | `ctca` | `cotocoa::cotocoa_fortran` |
| Python Worker | `ctca4py.CTCAWorker` | [別パッケージをインストール](USAGE_from_Python.md) |

fpmを使うFortranプロジェクトでは、依存関係として次を指定できます。
ビルドにはMPIコンパイラを使ってください。

```toml
[dependencies]
ctca = { git = "https://github.com/Nkzono99/cotocoa.git", branch = "main" }
```

## MPIのMPMD形式で起動する

Requester、Coupler、Workerを1つのMPIジョブとして起動します。
以下は、Requester 2rank、Coupler 1rank、Worker 4rankの構成例です。
実行ファイルは自分でビルドしたものに置き換えます。

```sh
mpiexec -n 2 ./requester : -n 1 ./coupler : -n 4 ./worker
```

Workerが `procspercomm=2` で初期化すれば、2rankのグループが2つできます。
スパコンでは資源を確保した上で、その施設が提供するMPMD起動方法を使ってください。
名前付きAPIの `cotocoa run` 用YAMLは、この従来APIの起動には使いません。

## ソース例

| 内容 | Requester | Coupler | Worker |
| --- | --- | --- | --- |
| Cの配列添付 | [sample1_requester.c](../test/sample1_requester.c) | [sample1_coupler.c](../test/sample1_coupler.c) | [sample1_worker1.c](../test/sample1_worker1.c) |
| Fortranの配列添付 | [sample2_requester.f90](../test/sample2_requester.f90) | [sample2_coupler.f90](../test/sample2_coupler.f90) | [sample2_worker1.f90](../test/sample2_worker1.f90) |
| CからPythonへの依頼 | [sample4_requester.c](../test/sample4_requester.c) | [sample4_coupler.c](../test/sample4_coupler.c) | [sample4_worker.py](../test/sample4_worker.py) |

サンプルごとに必要なWorkerのIDやグループ数が異なります。
起動前に、Requesterの宛先選択とWorkerの初期化引数を確認してください。
