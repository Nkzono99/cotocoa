# ctca4py

従来のCoToCoAのWorkerをPythonで書くためのパッケージです。
C・FortranのRequesterとCouplerから依頼を受け取り、NumPy配列で処理できます。

## インストール

CoToCoAのリポジトリ直下で、ネイティブライブラリをビルドした後に実行します。

```sh
python -m pip install ./ctca4py
export CTCA4PY_LIBRARY="/absolute/path/to/libcotocoa.so"
```

mpi4pyとネイティブライブラリは、同じMPI実装を使ってください。
importは `from ctca4py import CTCAWorker`、
または `from ctca4py.ctcaworker import CTCAWorker` です。

[インストールから受信ループまでの利用ガイド](../docs/USAGE_from_Python.md)を参照してください。
Pythonからも送信したい場合は、[名前付きAPIのcotocoaパッケージ](../docs/API-Python.md)を使えます。
