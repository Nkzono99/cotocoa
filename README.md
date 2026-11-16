# CoToCoA

CoToCoA（Code To Code Adapter）は、別々のプログラムとして書かれた計算コードを
1つのMPIジョブの中でつなぎ、配列を直接受け渡すためのライブラリです。

たとえば、Fortranのシミュレーションが各ステップで計算した電位を、
ファイルへ書き出さずにPythonの解析プログラムへ渡せます。
つなぐプログラムは、Fortran・C・Pythonのどの組み合わせでも構いません。

## どう動くか

```text
              one MPI job, started by `cotocoa run`

 +--------------------+     +-----------+     +--------------------+
 | simulation         |     | Coupler   |     | analysis           |
 | Fortran, 2 ranks   | --> | 1 rank    | --> | Python, 1 rank     |
 | call ctx%send(...) |     | (bundled) |     | ctx.receive(...)   |
 +--------------------+     +-----------+     +--------------------+
```

- 配列を送るプログラムは `send` を、受け取るプログラムは `receive` を呼びます。
  相手の言語やプロセス数を、コードの中で意識する必要はありません。
- どのプログラムをつなぐか、それぞれ何プロセスで動かすかは、YAMLの設定ファイルに書きます。
- 送信側と受信側の間を取り持つCoupler（送信の通知を中継するプロセス）は付属しています。
  `cotocoa run 設定ファイル` が、すべてのプログラムとCouplerをまとめて起動します。

## コードの例

付属の例 [examples/snapshot](examples/snapshot) からの抜粋です。
動かせる全体は[チュートリアル](docs/TUTORIAL.md)で説明しています。

送る側（Fortran）：

```fortran
call ctx%open('producer')
do step = 1, 10
  field = real(step + rank, c_double)          ! ここが本来の計算
  call ctx%send('potential', field, step=int(step, c_int64_t), &
                offset=[int(rank*3, c_int64_t), 0_c_int64_t, 0_c_int64_t])
end do
call ctx%close()
```

受け取る側（Python）：

```python
with Context("consumer") as ctx:
    for message in ctx.receive("potential"):
        with message:
            print(message.step, message.source_rank, message.array.mean())
```

2つをつなぐ設定（YAML）：

```yaml
programs:
  producer:
    command: ["../../build/snapshot_fortran", "producer"]
    ranks: 2
  consumer:
    command: ["../../.venv/bin/python", "python.py", "consumer"]
    ranks: 1
connections:
  - name: potential
    from: producer.potential
    to: consumer.potential
    dtype: float64
    shape: [3, 5, 7]
    global_shape: [6, 5, 7]
    axes: [x, y, z]
```

## はじめる

1. [インストール](docs/INSTALL.md) — ライブラリをビルドし、`cotocoa` コマンドを使えるようにする
2. [チュートリアル](docs/TUTORIAL.md) — 付属の例を動かし、設定とコードを読む
3. [しくみと用語](docs/CONCEPTS.md) — 送受信の流れと、守るべき約束の理由を理解する

そのあとは、使う言語のガイド（[Fortran](docs/GUIDE-FORTRAN.md)・[C](docs/GUIDE-C.md)・[Python](docs/GUIDE-PYTHON.md)）へ進んでください。
全ページの一覧は[ドキュメント目次](docs/README.md)にあります。

## できること

| やりたいこと | 読むページ |
| --- | --- |
| 計算の各ステップの配列を、別のプログラムへ渡す | [チュートリアル](docs/TUTORIAL.md) |
| 複数の配列を送る・同じ配列を複数の相手へ送る | [複数の配列・複数の相手とつなぐ](docs/CONNECTIONS.md) |
| 独立した計算を、空いているワーカーへ割り振る | [計算をワーカーへ依頼する](docs/TASKS.md) |
| 受信側が遅いとき、古い値を飛ばして新しい値を届ける | [受信が追いつかないとき](docs/DELIVERY.md) |
| 大きな配列を、送信時にコピーせずに送る | [コピーせずに送る](docs/BORROW.md) |
| 領域分割された配列を、受信側で1つにまとめる | [分割された配列をまとめる](docs/ASSEMBLY.md) |
| Slurmで起動する・止まった原因を調べる | [起動と実行状態の確認](docs/RUNNING.md)・[困ったとき](docs/TROUBLESHOOTING.md) |

送れるのは `int32`・`float32`・`float64` の1〜3次元配列で、型と形状は接続ごとに固定です。
データの流れは送信側から受信側への一方向で、同じプログラムが送信と受信を兼ねることはできません。
対応していないことの一覧は[しくみと用語](docs/CONCEPTS.md#できないこと)にあります。

## 従来のCoToCoA APIを使っている方へ

Requester・Coupler・Workerを自分で書く従来のAPI（`ctca.h`、Fortranの `ctca` モジュール、
Pythonの `ctca4py`）も、そのまま使えます。
[従来APIのガイド](docs/legacy/README.md)を参照してください。

このドキュメントで単に「CoToCoA」と書くときは、上で紹介した新しいAPI
（`cotocoa.h`、Fortranの `cotocoa` モジュール、Pythonの `cotocoa` パッケージ）を指します。
従来APIと区別する必要があるときは「高水準API」と呼びます。

## このリポジトリについて

[tnanri/cotocoa](https://github.com/tnanri/cotocoa) を基にした
[Nkzono99/cotocoa](https://github.com/Nkzono99/cotocoa) です。
高水準APIはこのリポジトリの `main` ブランチで提供しています。
ライセンスは [LICENSE](LICENSE) を参照してください。

CoToCoAの開発者：Takeshi Nanri、Yuto Katoh、Keiichiro Fukazawa、Yohei Miyake、
Kazuya Nakazawa、Jingde Zhow、Youhei Sunada、Haichao Zhao。
