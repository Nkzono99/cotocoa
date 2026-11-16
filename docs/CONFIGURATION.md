# 設定ファイル

[ドキュメント一覧](README.md) · 関連：[最初の連携](HIGH-LEVEL-USAGE.md)

接続の設定をYAMLファイルにまとめます。
プログラムの役割は `connections` の送信元・受信先から決まるため、役割を別に指定する必要はありません。

## 基本形

次は、1つのシミュレーションから1つの解析へ、2次元配列を送る設定です。
`simulation` と `analysis` は、利用者が用意した実行ファイルに置き換えてください。

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
  - name: potential_to_analysis
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

シミュレーションは `simulation`、解析は `analysis` という名前でContextを開きます。
送受信APIに渡す名前はポート名の `potential` です。
`potential_to_analysis` は接続の識別名で、受信メッセージや診断で使います。

## programs：実行するプログラム

| 項目 | 必須 | 意味・既定値 |
| --- | --- | --- |
| プログラム名 | 必須 | Contextを開くときの名前 |
| `command` | 必須 | 実行ファイルと引数のリスト |
| `ranks` | 必須 | そのプログラムのMPIプロセス数 |
| `group_size` | 任意 | 1グループのrank数。省略時は `ranks` と同じ |

実行時の作業ディレクトリは、設定ファイルのあるディレクトリです。
`command` の先頭に `/` を含むパスを書くと、そのディレクトリを基準に解決します。
`python` などパスを含まない名前はPATHから検索します。

`command` はシェルのコマンド文字列ではありません。
`$HOME`、`~`、ワイルドカード、パイプなどは展開されません。
引数は1つずつリストに書いてください。

`group_size` は主にTaskで使います。
たとえば `ranks: 8, group_size: 2` は、2rankで動くワーカーを4グループ作ります。
`ranks` は `group_size` で割り切れる値にします。
producerとStreamのconsumerは、プログラムの全rankで1グループを構成します。

ジョブ全体には、各プログラムの `ranks` の合計に**Couplerの1rank**を加えた資源が必要です。

## connections：配列の接続

### 必須項目

| 項目 | 意味 |
| --- | --- |
| `name` | 接続ごとに異なる名前 |
| `from` | 送信元を `プログラム名.ポート名` で指定 |
| `to` | 受信先を `プログラム名.ポート名` で指定 |
| `dtype` | `int32`、`float32`、`float64` のいずれか |
| `shape` | 1回に送る局所配列の形状。1〜3個の正整数 |
| `global_shape` | 全領域の形状。各軸で `shape` 以上の値 |
| `axes` | 各軸の名前。shapeと同じ数を指定し、重複させない |

`shape` は送信元の全rankで共通です。実行中に変わりません。
送信時の `offset` と `shape` が `global_shape` に収まるようにします。
分割しない配列では `shape` と `global_shape` を同じ値にします。

### 省略できる項目

| 項目 | 既定値 | 選べる値・意味 |
| --- | --- | --- |
| `kind` | `stream` | `stream`：配列の配送、`task`：計算依頼 |
| `mode` | `snapshot` | `snapshot`：送信時にコピー、`borrow`：元配列を借用 |
| `delivery` | `all` | `all`：全件配送、`latest`：未配送分を新しい値へ置換 |
| `buffer_slots` | `2` | 送信元rankごと・接続ごとの未完了送信枠数 |
| `distribution` | `shards` | 各rankの局所配列を個別に配送。指定できる値は `shards` のみ |
| `units` | `"1"` | 配列の値の単位。例：`V` |
| `time_unit` | `"1"` | 送信時刻の単位。例：`s` |

無次元を明示する場合は、数値の `1` ではなく文字列の `"1"` と書きます。
単位はメタデータとして渡されます。CoToCoAは単位変換や補間を行いません。

`buffer_slots` を増やすと、受信側が処理中でも先へ送れる件数が増えます。
snapshotでは保持する配列も増えるため、`cotocoa plan` のメモリ見積もりを確認してください。
borrowの元配列、Assembler、利用者のコピー、MPI内部のメモリは別に必要です。

## 接続を組み合わせるときの規則

- 同じプログラムを送信側と受信側の両方にはできません。宣言した全プログラムを接続してください。
- Streamは同じ送信ポートから複数の宛先へ送れます。各接続の型・形状・軸・単位・modeを揃えます。
- Taskは1つの送信ポートにつき1宛先で、`delivery: all` を使います。
- `delivery: latest` は `buffer_slots: 2` 以上が必要です。
- 複数のワーカーグループを持つconsumerの入力は、すべてTaskにします。

具体例は[複数接続とTask](TASKS-AND-PORTS.md)、配送と寿命の選び方は
[配送と配列の保持](DELIVERY-AND-SHARING.md)を参照してください。

## runtime：実行時間の上限

`timeout_seconds` は0以上の秒数です。省略時の0は時間制限なしです。
正の値を指定すると、`run` 全体の時間制限になります。
`--probe-first` による起動確認の時間も含みます。

Slurmのジョブ時間制限は別に設定してください。
待機中の診断や時間超過時の動作は[トラブルシューティング](DIAGNOSTICS.md)で説明しています。

## 名前と容量の上限

| 項目 | 範囲 |
| --- | --- |
| `schema` | `1` |
| プログラム数 | 2〜64 |
| 接続数 | 1〜256 |
| プログラム名・ポート名・接続名・軸名 | 63文字以内の英数字、`_`、`-`。先頭は英字か `_` |
| 予約名 | `__coupler__` は使えない |
| `units`・`time_unit` | 空でない文字列、UTF-8で63バイト以内、制御文字不可 |
| 1プログラムの `ranks` | 1〜1,000,000。全producerの合計は524,288以下 |
| `buffer_slots` | 1〜4094。1つのproducerの全出力接続の合計も4094以下 |
| 局所配列の要素数 | 268,435,455以下 |
| 設定ファイルの大きさ | 1 MiB以下 |

不明なキーや重複したキーはエラーになります。
編集したら `cotocoa check 設定ファイル` で確認してください。
