# 従来のPython Worker：ctca4py

[ドキュメント一覧](README.md) · [従来APIの利用ガイド](USAGE.md)

`ctca4py` は、従来のCoToCoAのWorkerをPythonで書くためのパッケージです。
Pythonを送信側にも使いたい場合や、名前付きポートを使う場合は[Python API](API-Python.md)を参照してください。

## インストール

リポジトリ直下で、[ネイティブライブラリをビルド](INSTALL.md)した後に実行します。

```sh
.venv/bin/python -m pip install ./ctca4py
export CTCA4PY_LIBRARY="$PWD/build/libcotocoa.so"
```

`ctca4py` と `cotocoa` は別のPythonパッケージです。
Worker、Requester、Coupler、mpi4pyは同じMPI実装を使ってください。

ライブラリが標準の探索先にない場合は、上のように `CTCA4PY_LIBRARY` を指定します。
`COTOCOA_LIBRARY` も使えます。
同じ従来ABIを持つ既存の `ctca4py.so` も指定できます。

## Workerを書く

次は、整数パラメータ2個と、倍精度の値3個が添付された依頼を受け取る例です。
RequesterとCouplerも、この数と型に合わせてください。

```python
from ctca4py import CTCAWorker

worker = CTCAWorker()
worker.CTCAW_init(progid=0, procspercomm=1)

while True:
    fromrank, parameters, values = worker.CTCAW_pollreq(
        2, 3, CTCAWorker.DAT_REAL8
    )
    if worker.CTCAW_isfin():
        break

    print(fromrank, parameters, values.sum())
    worker.CTCAW_complete()

worker.CTCAW_finalize()
```

`progid` はCouplerが配送に使うWorker IDです。
`procspercomm` は1つのWorkerグループのrank数です。
グループ内の全rankが受信と完了通知を行います。
計算用のMPI communicatorは `worker.CTCA_subcomm` で取得します。

従来のimport表記 `from ctca4py.ctcaworker import CTCAWorker` も利用できます。

## 添付配列の型を選ぶ

| 定数 | NumPyの型 |
| --- | --- |
| `CTCAWorker.DAT_INT` | `int32` |
| `CTCAWorker.DAT_REAL4` | `float32` |
| `CTCAWorker.DAT_REAL8` | `float64` |

`CTCAW_pollreq(intparamsnum, datanum, datatype)` のdatanumは要素数です。
戻り値は `(fromrank, parameters, values)` です。
添付配列がない場合は `CTCAW_pollreq(intparamsnum)` を使い、
`(fromrank, parameters)` の2つを受け取ります。

## 共有領域と計測

| メソッド | 用途 |
| --- | --- |
| `CTCAW_regarea(datatype)` | Requesterが公開する領域の登録に参加し、area IDを受け取る |
| `CTCAW_readarea(areaid, reqrank, offset, size, datatype)` | 領域を読み取る |
| `CTCAW_writearea(areaid, reqrank, offset, size, data, datatype)` | 領域へ書き込む |
| `CTCAW_prof_start()` / `CTCAW_prof_stop()` | プロファイルの開始・終了 |
| `CTCAW_prof_start_calc()` / `CTCAW_prof_stop_calc()` | 計算区間の開始・終了 |

従来APIのarea ID、offset、型の規則は[Worker API](API-worker.md)を参照してください。
CのRequester・Couplerと組み合わせたソース例は[sample4_worker.py](../test/sample4_worker.py)にあります。
