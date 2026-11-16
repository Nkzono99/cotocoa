# 設定ファイル

[ドキュメント目次](README.md) ｜ 関連：[チュートリアル](TUTORIAL.md)・[起動と実行状態の確認](RUNNING.md)

どのプログラムを、何プロセスで動かし、どうつなぐかを、1つのYAMLファイルに書きます。
ファイル名は自由です。このドキュメントでは `cotocoa.yaml` としています。

## 全体の形

```yaml
schema: 1

programs:
  simulation:
    command: ["./simulation"]
    ranks: 2
  analysis:
    command: ["./analysis"]
    ranks: 1

connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    units: V
    time_unit: s

runtime:
  timeout_seconds: 600
```

| 部分 | 必須 | 書く内容 |
| --- | --- | --- |
| `schema` | 必須 | 設定ファイルの書式の版。`1` と書く |
| [`programs`](#programs動かすプログラム) | 必須 | 動かすプログラムと、それぞれのプロセス数 |
| [`connections`](#connectionsプログラム間の接続) | 必須 | プログラム間をつなぐ線と、そこを流れる配列の形式 |
| [`runtime`](#runtime実行時間の上限) | 省略可 | 実行時間の上限 |

どのプログラムが送信側で、どれが受信側かは、`connections` の `from` と `to` から決まります。
別に指定する必要はありません。

書いた設定は、`cotocoa check 設定ファイル` で確認できます。
知らない項目名や、同じ項目の重複があると、エラーとして報告されます。

## programs：動かすプログラム

```yaml
programs:
  simulation:              # プログラム名
    command: ["./simulation", "input.dat"]
    ranks: 2
```

| 項目 | 必須 | 意味 |
| --- | --- | --- |
| プログラム名 | 必須 | プログラムに付ける名前。コードの中でContextを開くときに、この名前を渡す |
| `command` | 必須 | 起動するコマンド。実行ファイルと引数を、1つずつリストの要素として書く |
| `ranks` | 必須 | そのプログラムのMPIプロセス数 |
| `group_size` | 省略可 | [Task](TASKS.md)の受信側を、何プロセスずつのワーカーに分けるか。省略時は `ranks` と同じ（分けない） |

プログラムは2つ以上必要です。

### commandの書き方

- **各プログラムは、設定ファイルのあるディレクトリを作業ディレクトリとして起動されます。**
  プログラムが相対パスで入力ファイルを開く場合は、設定ファイルの場所が基準になります。
- 実行ファイルを `./simulation` や `../build/simulation` のように `/` を含む形で書くと、
  設定ファイルのあるディレクトリから見たパスとして扱われます。絶対パスも書けます。
- `python` のように `/` を含まない名前を書くと、環境変数 `PATH` から探します。
- シェルの機能は使えません。`$HOME`、`~`、`*`、`|`、`>` などは展開されず、そのままの文字として渡されます。
- 引数は1つずつ区切って書きます。`["./sim -n 10"]` ではなく `["./sim", "-n", "10"]` です。
- 環境変数（`OMP_NUM_THREADS` など）は、`cotocoa run` を実行した環境のものが全プログラムに引き継がれます。

入力ファイルを標準入力から読むプログラム（`./sim < input.inp`）や、プログラムごとに環境変数を変えたい場合は、
その処理を書いたシェルスクリプトを用意して、`command` にはそのスクリプトを書きます。

```sh
#!/bin/sh
# run_sim.sh（実行権限を付けておく）
exec ./sim < input.inp
```

```yaml
programs:
  simulation:
    command: ["./run_sim.sh"]
    ranks: 2
```

### 必要なプロセス数

ジョブ全体で必要なMPIプロセス数は、**全プログラムの `ranks` の合計 + 1（Coupler）** です。
上の例では 2 + 1 + 1 = 4 です。`cotocoa check` の出力にも表示されます。

## connections：プログラム間の接続

```yaml
connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
```

### 必須の項目

| 項目 | 意味 | 例 |
| --- | --- | --- |
| `name` | 接続に付ける名前。ほかの接続と重複させない | `potential` |
| `from` | 送信側を `プログラム名.ポート名` で書く | `simulation.potential` |
| `to` | 受信側を `プログラム名.ポート名` で書く | `analysis.potential` |
| `dtype` | 要素の型。`float64`・`float32`・`int32` のいずれか | `float64` |
| `shape` | 1プロセスが1回に送る配列の形状。1〜3個の正の整数 | `[16, 8]` |
| `global_shape` | 全体領域の形状。各軸とも `shape` 以上 | `[32, 8]` |
| `axes` | 各軸の名前。`shape` と同じ個数で、重複させない | `[x, y]` |

**ポート名**は、プログラムのコードで `send` や `receive` に指定する名前です。
`from` と `to` のポート名は、同じでも違っていても構いません。

**`name`** は接続そのものの名前で、受信したメッセージの情報（`name`）やエラーメッセージに現れます。
コードで送受信するときに指定するのは、`name` ではなくポート名です。

**`shape` と `global_shape`** の関係は、[領域分割された配列の送り方](CONCEPTS.md#領域分割された配列の送り方)で説明しています。
領域分割をしない配列では、2つを同じ値にします。
`shape` は送信側の全プロセスで共通で、実行中に変えることはできません。

### 省略できる項目

| 項目 | 既定値 | 選べる値 | 説明 |
| --- | --- | --- | --- |
| `kind` | `stream` | `stream`・`task` | 接続の種類。[Task](TASKS.md) |
| `delivery` | `all` | `all`・`latest` | 全件を届けるか、新しい値を優先するか。[受信が追いつかないとき](DELIVERY.md) |
| `buffer_slots` | `2` | 1〜4094 | 送信バッファのスロット数。[受信が追いつかないとき](DELIVERY.md) |
| `mode` | `snapshot` | `snapshot`・`borrow` | 送信時に配列をコピーするか。[コピーせずに送る](BORROW.md) |
| `units` | `"1"` | 文字列 | 値の単位。例：`V` |
| `time_unit` | `"1"` | 文字列 | `send` で付ける時刻の単位。例：`s` |
| `distribution` | `shards` | `shards` のみ | 各プロセスの配列を別々に届けることを表す。書く必要はない |

`units` と `time_unit` は、受信側へ伝える注記です。CoToCoAは単位の換算を行いません。
無次元を明示するときは、数値の `1` ではなく、引用符を付けた文字列の `"1"` と書きます。

### 接続を複数書くとき

接続は、いくつでも並べて書けます。組み合わせ方と例は、
[複数の配列・複数の相手とつなぐ](CONNECTIONS.md)で説明しています。決まりは次のとおりです。

- 1つのプログラムは、送信側か受信側のどちらか一方にする。
- `programs` に書いたプログラムは、すべてどれかの接続に含める。
- 同じポートから複数の宛先へ送る接続は、`dtype`・`shape`・`global_shape`・`axes`・`units`・`time_unit`・`mode` を揃える。
- Taskの接続は、1つの送信ポートにつき宛先を1つにし、`delivery` を `all` にする。
- `delivery: latest` の接続は、`buffer_slots` を2以上にする。
- `group_size` で複数のワーカーに分けた受信側プログラムには、Taskの接続だけをつなぐ。

## runtime：実行時間の上限

```yaml
runtime:
  timeout_seconds: 600
```

`timeout_seconds` に正の秒数を書くと、`cotocoa run` はその時間を過ぎたジョブを中断します。
プログラムの書き間違いで「止まったまま進まない」状態になったときに、計算資源を使い続けるのを防げます。
省略した場合と `0` の場合は、時間制限なしです。

詳しい動作は[時間制限を設ける](RUNNING.md#時間制限を設ける)で説明しています。
Slurmのジョブ時間制限とは別のものなので、そちらも設定してください。

## 名前と大きさの上限

| 項目 | 範囲 |
| --- | --- |
| プログラム名・ポート名・接続名・軸名 | 63文字以内。使える文字は英数字・`_`・`-`。先頭は英字か `_` |
| 使えない名前 | `__coupler__` |
| `units`・`time_unit` | 空でない文字列。UTF-8で63バイト以内 |
| プログラムの数 | 2〜64 |
| 接続の数 | 1〜256 |
| 1プログラムの `ranks` | 1〜1,000,000 |
| 送信側プログラムの `ranks` の合計 | 524,288以下 |
| `buffer_slots` | 1〜4094。1つの送信側プログラムから出る全接続の合計も4094以下 |
| 配列1つ（`shape`）の要素数 | 268,435,455以下 |
| 設定ファイルの大きさ | 1 MiB以下 |
