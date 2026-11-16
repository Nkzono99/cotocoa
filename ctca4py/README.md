# ctca4py

CoToCoAの従来API（Requester・Coupler・Worker）のWorkerを、Pythonで書くためのパッケージです。
CやFortranで書いたRequesterとCouplerから依頼を受け取り、NumPy配列で処理できます。

## インストール

CoToCoAのリポジトリの直下で、ネイティブライブラリをビルドした後に実行します。

```sh
python -m pip install ./ctca4py
export CTCA4PY_LIBRARY="/absolute/path/to/libcotocoa.so"
```

mpi4pyとネイティブライブラリには、同じMPIを使ってください。

## 使い方

```python
from ctca4py import CTCAWorker
```

従来の書き方 `from ctca4py.ctcaworker import CTCAWorker` も使えます。
受信ループの書き方は、[ctca4pyの使い方](../docs/legacy/PYTHON-WORKER.md)を参照してください。

## Pythonから配列を送りたい場合

このパッケージで書けるのはWorker（受け取る側）だけです。
Pythonから配列を送りたい場合や、設定ファイルでプログラムをつなぐ新しいAPIを使う場合は、
別パッケージの `cotocoa` を使います。[Pythonから使う](../docs/GUIDE-PYTHON.md)を参照してください。
