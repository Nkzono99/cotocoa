# インストール

[ドキュメント一覧](README.md) → インストール → [最初の連携](HIGH-LEVEL-USAGE.md)

CoToCoAは、MPI通信を行うネイティブライブラリと、設定・起動を扱うPythonパッケージで構成されます。
最初は、ライブラリと付属の例をリポジトリ内でビルドする方法が簡単です。

## 必要なもの

| ソフトウェア | 要件 |
| --- | --- |
| MPI | MPI 3以降。C用のコンパイララッパーが必要 |
| Fortranコンパイラ | Fortranを使う場合に必要。MPI用のラッパーも用意する |
| CMake | 3.16以降 |
| Python | 3.10以降。CLIとPython APIに使用する |

ネイティブライブラリ、各プログラム、mpi4pyは**同じMPI実装**を使ってください。
Fortranのモジュールファイルはコンパイラに依存するため、利用側も互換性のあるコンパイラでビルドします。

スパコンでは、その施設の手順でコンパイラとMPIの環境を読み込み、ビルドと実行に使う資源を確保してください。
KUDPCではコンパイルやMPI実行を計算ノードで行います。

## リポジトリを取得する

```sh
git clone https://github.com/Nkzono99/cotocoa.git
cd cotocoa
```

以降のコマンドは、この `cotocoa` ディレクトリで実行します。

## ライブラリとサンプルをビルドする

MPIのコンパイララッパーが `mpicc`・`mpifort` の環境では、次を実行します。

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpicc \
  -DCMAKE_Fortran_COMPILER=mpifort
cmake --build build -j 2
```

Intel MPIで `mpiicc`・`mpiifort` を使う環境では、上のコンパイラ名を置き換えてください。
ビルドすると `build/` に共有ライブラリ、Coupler、起動確認用プログラムとC・Fortranのサンプルができます。

Fortranを使わない場合は、最初のCMakeコマンドに
`-DCOTOCOA_ENABLE_FORTRAN=OFF` を加えます。
その場合、入門例の実行プログラムはCまたはPythonを選んでください。

## Pythonパッケージを入れる

```sh
python -m venv .venv
.venv/bin/python -m pip install -e .
```

NumPy、mpi4py、PyYAMLも依存パッケージとしてインストールされます。
施設側でmpi4pyの導入方法が指定されている場合は、その手順に合わせてください。

`pip install` はネイティブライブラリをビルドしません。
上のCMakeによるビルドも必要です。この配置ではPython APIが `build/libcotocoa.so` を見つけるため、
追加のパス指定なしで[最初の連携](HIGH-LEVEL-USAGE.md)へ進めます。

## 別のプロジェクトから使う

ライブラリを専用のディレクトリへインストールします。

```sh
cmake --install build --prefix "$HOME/.local/cotocoa"
```

CLIがCouplerを見つけられるように、導入先の `bin` をPATHへ加えます。

```sh
export PATH="$HOME/.local/cotocoa/bin:$PATH"
export COTOCOA_LIBRARY="$HOME/.local/cotocoa/lib/libcotocoa.so"
```

導入先が `lib64` の環境では、`COTOCOA_LIBRARY` も `lib64` に合わせてください。
この変数はPython APIが読み込むライブラリを指定します。
C・Fortranの実行ファイルを標準外の場所に配置する場合は、リンク時のRPATHまたは
環境の共有ライブラリ探索パスも設定してください。

CMakeを使う利用側は、次のターゲットにリンクします。

| 言語 | パッケージの読み込み | リンクするターゲット |
| --- | --- | --- |
| C | `find_package(cotocoa 0.3 REQUIRED)` | `cotocoa::cotocoa` |
| Fortran | `find_package(cotocoa 0.3 REQUIRED COMPONENTS Fortran)` | `cotocoa::cotocoa_fortran` |

利用側のCMake設定時に `-DCMAKE_PREFIX_PATH="$HOME/.local/cotocoa"` を渡すと、
インストールしたパッケージを検索できます。具体例は[C](API-C.md)・[Fortran](API-Fortran.md)のガイドにあります。

従来のPython Workerを使う場合は、別パッケージの[ctca4py](USAGE_from_Python.md)をインストールしてください。
