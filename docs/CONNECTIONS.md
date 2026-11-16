# 複数の配列・複数の相手とつなぐ

[ドキュメント目次](README.md) ｜ 前提：[チュートリアル](TUTORIAL.md)・[しくみと用語](CONCEPTS.md)

設定ファイルの `connections` に接続を並べると、複数の配列や複数のプログラムをつなげます。
よく使う3つの形を説明します。

| 形 | 例 |
| --- | --- |
| [A. 複数の配列を、同じ相手へ送る](#a-複数の配列を同じ相手へ送る) | 電位と密度を、同じ解析プログラムへ |
| [B. 同じ配列を、複数の相手へ送る](#b-同じ配列を複数の相手へ送る) | 電位を、保存用と表示用の2つのプログラムへ |
| [C. 複数の相手から受け取る](#c-複数の相手から受け取る) | 2つのシミュレーションの結果を、1つの解析プログラムへ |

これらは組み合わせて使えます。

## A. 複数の配列を、同じ相手へ送る

```text
  simulation                         analysis
    port "potential"  ------------>    port "potential"
    port "density"    ------------>    port "density"
```

配列の種類ごとに接続を1つ書きます。型や形状は接続ごとに違っていて構いません。

```yaml
schema: 1
programs:
  simulation:
    command: ["./simulation"]
    ranks: 1
  analysis:
    command: ["./analysis"]
    ranks: 1
connections:
  - name: potential
    from: simulation.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [16, 8]
    axes: [x, y]
  - name: density
    from: simulation.density
    to: analysis.density
    dtype: float32
    shape: [16, 8]
    global_shape: [16, 8]
    axes: [x, y]
```

### 送信側

ポート名を変えて、`send` を必要な回数呼びます。

```python
ctx.send("potential", potential, step=step)
ctx.send("density", density, step=step)
```

### 受信側

入力ポートが複数ある受信側は、**ポート名を指定せずに**受信し、届いたメッセージの `port` を見て処理を分けます。

Python：

```python
for message in ctx.receive():
    with message:
        if message.port == "potential":
            analyze_potential(message.array, message.step)
        elif message.port == "density":
            analyze_density(message.array, message.step)
```

Fortran：

```fortran
do
  call ctx%receive('*', message, has_message)
  if (.not. has_message) exit
  select case (trim(message%port()))
  case ('potential')
    call message%copy_to(potential)      ! real(c_double), allocatable :: potential(:,:)
  case ('density')
    call message%copy_to(density)        ! real(c_float), allocatable :: density(:,:)
  end select
  call message%complete()
end do
```

C：

```c
cotocoa_message *message = NULL;
int rc = cotocoa_receive(ctx, NULL, &message, &error);   /* NULL：全ポート */
/* ... rc の確認の後 ... */
cotocoa_metadata meta;
cotocoa_message_metadata(message, &meta, &error);
if (strcmp(meta.port, "potential") == 0) {
    /* 電位の処理 */
} else if (strcmp(meta.port, "density") == 0) {
    /* 密度の処理 */
}
```

| 言語 | 全ポートから受信する書き方 |
| --- | --- |
| Python | `ctx.receive()` |
| Fortran | `call ctx%receive('*', message, has_message)` |
| C | `cotocoa_receive(ctx, NULL, &message, &error)` |

**ポート名を指定した受信（`ctx.receive("potential")` など）は、入力ポートが1種類だけの受信側でしか使えません。**
入力ポートが複数あるのにポート名を指定すると、エラーになります。
受信側に届くメッセージは到着順に1列に並んでいて、特定のポートのものだけを先に取り出すことはできないためです。

### 届く順序

同じ接続の、同じ送信プロセスからのメッセージは、送った順に届きます。
それ以外の順序、たとえば「同じステップの電位と密度のどちらが先に届くか」は決まっていません。
2つの配列を組にして処理したい場合は、受信側で `step` をキーにして、両方が揃うまで保持してください。

## B. 同じ配列を、複数の相手へ送る

```text
                               +---->  archive   port "potential"   (every step)
  simulation                   |
    port "potential"  ---------+
                               |
                               +---->  display   port "potential"   (newest only)
```

同じ `from` を持つ接続を、宛先の数だけ書きます。

```yaml
schema: 1
programs:
  simulation:
    command: ["./simulation"]
    ranks: 2
  archive:
    command: ["./archive"]
    ranks: 1
  display:
    command: ["./display"]
    ranks: 1
connections:
  - name: potential_archive
    from: simulation.potential
    to: archive.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    delivery: all
  - name: potential_display
    from: simulation.potential
    to: display.potential
    dtype: float64
    shape: [16, 8]
    global_shape: [32, 8]
    axes: [x, y]
    delivery: latest
```

送信側のコードは、宛先が1つのときと変わりません。
`send` を1回呼ぶと、同じ配列が両方の宛先へ届きます。

```python
ctx.send("potential", potential, step=step, offset=(16 * rank, 0))
```

受信側の `archive` と `display` は、それぞれ普通に `ctx.receive("potential")` で受け取ります。

同じポートから出る接続は、次の項目を**すべて同じ**にしてください。

`dtype`・`shape`・`global_shape`・`axes`・`units`・`time_unit`・`mode`

`delivery`（全件届けるか、新しい値を優先するか）と `buffer_slots` は、接続ごとに変えられます。
上の例では、保存用の `archive` には全ステップを届け、表示用の `display` には
処理が追いつかないときに古い値を飛ばして新しい値を届けます（[受信が追いつかないとき](DELIVERY.md)）。

送信バッファは接続ごとに別々です。
`delivery: all` の宛先の処理が遅れてそのバッファが埋まると、`send` はそこで待ちます。
もう一方の宛先が `latest` でも、`all` の宛先を待つことに変わりはありません。

## C. 複数の相手から受け取る

```text
  fluid       port "density"  ------+
                                    +---->  analysis   port "density"
  particles   port "density"  ------+
```

`to` が同じ接続を、送信側の数だけ書きます。

```yaml
schema: 1
programs:
  fluid:
    command: ["./fluid"]
    ranks: 1
  particles:
    command: ["./particles"]
    ranks: 1
  analysis:
    command: ["./analysis"]
    ranks: 1
connections:
  - name: fluid_density
    from: fluid.density
    to: analysis.density
    dtype: float64
    shape: [16, 8]
    global_shape: [16, 8]
    axes: [x, y]
  - name: particle_density
    from: particles.density
    to: analysis.density
    dtype: float64
    shape: [16, 8]
    global_shape: [16, 8]
    axes: [x, y]
```

この例では受信ポートがどちらも `density` なので、ポート名を指定して受信できます。
どのプログラムから届いたかは、メッセージの `source`（送信側のプログラム名）で分かります。

```python
for message in ctx.receive("density"):
    with message:
        if message.source == "fluid":
            ...
        elif message.source == "particles":
            ...
```

| メッセージの情報 | 内容 | 使いどころ |
| --- | --- | --- |
| `port` | 受信側のポート名 | 配列の種類で処理を分ける |
| `source` | 送信側のプログラム名 | 送信元のプログラムで処理を分ける |
| `source_rank` | 送信側プログラム内のプロセス番号 | 領域分割のどの部分かを知る |
| `name` | 接続の名前（設定ファイルの `name`） | 接続そのもので処理を分ける |

受信ポート名を送信元ごとに変えた場合（`analysis.fluid_density` と `analysis.particle_density` など）は、
形Aと同じく、ポート名を指定せずに受信して `port` で分けます。

受信側の `receive` が終端を返すのは、**すべての**送信側プログラムが終了したときです。
片方の送信側が先に終了しても、もう片方からのメッセージは引き続き届きます。

## 接続を組み合わせるときの決まり

- 1つのプログラムは、送信側か受信側のどちらかです。あるプログラムの入力を受けて、その出力をさらに別のプログラムへ送る、という多段の構成はできません。
- `programs` に書いたプログラムは、すべてどれかの接続に含めてください。
- 同じ `from` と `to` の組を持つ接続は、1つだけ書けます。
- 接続の種類が[Task](TASKS.md)の場合は、1つの送信ポートにつき宛先は1つです。

設定を書いたら、`cotocoa check 設定ファイル` で確認できます。
