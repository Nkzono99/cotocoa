# Cから使う

[ドキュメント目次](README.md) ｜ ほかの言語：[Fortran](GUIDE-FORTRAN.md)・[Python](GUIDE-PYTHON.md)

Cのプログラムから配列を送る方法と、受け取る方法を説明します。
[チュートリアル](TUTORIAL.md)を済ませ、[しくみと用語](CONCEPTS.md)に目を通していることを前提にします。

このページの例は、[Fortran](GUIDE-FORTRAN.md)・[Python](GUIDE-PYTHON.md)のガイドと同じ構成です。
送信側と受信側を別の言語で書く場合は、それぞれの言語のページの該当部分を組み合わせてください。

## 例：simulationからanalysisへ電位を送る

2プロセスで動く `simulation` が、領域をx方向に2分割した電位（1プロセスあたり16×8）を5ステップ分送り、
1プロセスの `analysis` が受け取って平均値を表示します。

### 設定ファイル（cotocoa.yaml）

```yaml
schema: 1
programs:
  simulation:
    command: ["./build/simulation"]
    ranks: 2
  analysis:
    command: ["./build/analysis"]
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
```

各項目の意味は[設定ファイル](CONFIGURATION.md)にあります。

### 送信側（simulation.c）

```c
#include "cotocoa.h"
#include <stddef.h>

int main(void)
{
    cotocoa_context *ctx = NULL;
    cotocoa_error error = {0};
    if (cotocoa_open("simulation", "cotocoa.yaml", &ctx, &error))
        cotocoa_abort(ctx, &error);

    int rank;
    MPI_Comm_rank(cotocoa_comm(ctx), &rank);

    double potential[16][8];
    const int64_t shape[3] = {16, 8, 1};   /* 使わない軸は1にする */
    cotocoa_array_view view =
        cotocoa_view_f64(&potential[0][0], 2, shape, COTOCOA_ORDER_C);

    for (int step = 1; step <= 5; ++step) {
        /* ここで本来の計算を行う。この例では step + 0.1*rank で埋める。 */
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 8; ++j)
                potential[i][j] = step + 0.1 * rank;

        cotocoa_send_options options = cotocoa_send_defaults();
        options.step = step;
        options.has_time = 1;
        options.time = 0.5 * step;
        options.offset[0] = 16 * rank;
        if (cotocoa_send(ctx, "potential", &view, &options, &error))
            cotocoa_abort(ctx, &error);
    }

    if (cotocoa_close(ctx, &error))
        cotocoa_abort(NULL, &error);
    return 0;
}
```

### 受信側（analysis.c）

```c
#include "cotocoa.h"
#include <stdio.h>

int main(void)
{
    cotocoa_context *ctx = NULL;
    cotocoa_error error = {0};
    if (cotocoa_open("analysis", "cotocoa.yaml", &ctx, &error))
        cotocoa_abort(ctx, &error);

    for (;;) {
        cotocoa_message *message = NULL;
        int rc = cotocoa_receive(ctx, "potential", &message, &error);
        if (rc == COTOCOA_END) break;
        if (rc != COTOCOA_OK) cotocoa_abort(ctx, &error);

        cotocoa_metadata meta;
        cotocoa_array_view view;
        if (cotocoa_message_metadata(message, &meta, &error) ||
            cotocoa_message_view(message, &view, &error))
            cotocoa_abort(ctx, &error);

        double sum = 0.0;
        for (int64_t i = 0; i < view.shape[0]; ++i)
            for (int64_t j = 0; j < view.shape[1]; ++j) {
                const char *element = (const char *)view.data
                                    + i * view.strides[0] + j * view.strides[1];
                sum += *(const double *)element;
            }
        printf("step=%lld source=%d offset=%lld mean=%.3f\n",
               (long long)meta.step, meta.source_rank, (long long)meta.offset[0],
               sum / (double)(view.shape[0] * view.shape[1]));

        if (cotocoa_complete(message, &error))
            cotocoa_abort(ctx, &error);
        cotocoa_message_release(message);
    }

    if (cotocoa_close(ctx, &error))
        cotocoa_abort(NULL, &error);
    return 0;
}
```

### 実行する

次のようにファイルを置き、[ビルド](#ビルドする)してから実行します。

```text
my_coupling/
  CMakeLists.txt
  simulation.c
  analysis.c
  cotocoa.yaml
  build/            <- ビルドすると、ここに simulation と analysis ができる
```

必要なMPIプロセスは 2 + 1（Coupler）+ 1 = 4 です。`my_coupling/` で次を実行します。
`cotocoa` コマンドの場所は、[インストール](INSTALL.md#cotocoaコマンドとpythonを使えるようにする)を参照してください。

```sh
cotocoa run cotocoa.yaml --launcher mpiexec
```

Slurmでは、4プロセス以上を確保したジョブの中で `--launcher slurm` を指定します。
詳しくは[起動と実行状態の確認](RUNNING.md)を参照してください。

```text
step=1 source=0 offset=0 mean=1.000
step=1 source=1 offset=16 mean=1.100
step=2 source=0 offset=0 mean=2.000
step=2 source=1 offset=16 mean=2.100
...
step=5 source=1 offset=16 mean=5.100
```

`source=0` と `source=1` の行が入り混じる順序は、実行ごとに変わります。

## 基本の書き方

`cotocoa.h` をインクルードします。このヘッダーは `mpi.h` を読み込みます。

ほとんどの関数は、戻り値で成否を返し、最後の引数 `cotocoa_error *` にエラーの説明を書き込みます。
戻り値は成功なら0（`COTOCOA_OK`）なので、例のように
`if (関数(...)) cotocoa_abort(ctx, &error);` と書けば、エラーのときにメッセージを表示してジョブ全体を中断できます。

Contextは `cotocoa_open` で開き、`cotocoa_close` で閉じます。
`cotocoa_open` の第1引数は、設定ファイルの `programs` に書いた自分の名前です。
第2引数は、通常は使われません。`"cotocoa.yaml"` を渡しておけば構いません。
この引数が使われるのは、[`cotocoa run` を使わずに起動](RUNNING.md#cotocoa-runを使わずに起動する)し、
かつ環境変数 `COTOCOA_MANIFEST` を設定しない場合だけで、そのときはmanifestのパスを渡します。

## 送信する

Cでは、配列を **view**（`cotocoa_array_view`）という構造体にまとめて渡します。
viewは、配列の先頭アドレス、型、次元数、形状、メモリ上の並び方を記録したものです。

### viewを作る

```c
double potential[16][8];
const int64_t shape[3] = {16, 8, 1};
cotocoa_array_view view =
    cotocoa_view_f64(&potential[0][0], 2, shape, COTOCOA_ORDER_C);
```

| 設定ファイルの `dtype` | Cの型 | viewを作る関数 |
| --- | --- | --- |
| `float64` | `double` | `cotocoa_view_f64` |
| `float32` | `float` | `cotocoa_view_f32` |
| `int32` | `int32_t` | `cotocoa_view_i32` |

引数は `(先頭アドレス, 次元数, 形状, 並び方)` です。

- 形状は、接続の `shape` と同じ順で書きます。上の例では第1軸が16、第2軸が8です。
- 並び方には、通常のCの配列（最後の添字が最も速く変わる）なら `COTOCOA_ORDER_C` を指定します。
  Fortranと同じ並び（最初の添字が最も速く変わる）の配列なら `COTOCOA_ORDER_F` を指定します。

`malloc` で確保した1次元の領域を `a[i*8 + j]` のように使っている場合も、
`shape` を `{16, 8, 1}`、並び方を `COTOCOA_ORDER_C` とすれば同じ配列として送れます。

viewは配列の場所を記録しているだけなので、配列の内容を書き換えても作り直す必要はありません。

一部を飛ばした配列や逆順の配列を送りたい場合は、`cotocoa_array_view` の `strides` を直接設定します。
`strides[d]` は、第d軸の添字を1つ進めたときに動くアドレスの**バイト数**です。
逆順にたどる軸では負の値にし、`data` は添字がすべて0の要素を指すようにします。

### 送る

```c
cotocoa_send_options options = cotocoa_send_defaults();
options.step = step;
options.has_time = 1;
options.time = 0.5 * step;
options.offset[0] = 16 * rank;
if (cotocoa_send(ctx, "potential", &view, &options, &error))
    cotocoa_abort(ctx, &error);
```

`cotocoa_send_options` は、必ず `cotocoa_send_defaults()` で初期化してから必要な項目を設定します。

| メンバー | 既定値 | 意味 |
| --- | --- | --- |
| `step` | 0 | この配列に付ける番号。受信側へそのまま伝わる |
| `has_time`・`time` | 0（時刻なし） | 時刻を付けるときは、`has_time = 1` と `time` の両方を設定する |
| `offset[3]` | すべて0 | 全体領域の中での開始位置（0始まり） |

`cotocoa_send` は配列をコピーしてから戻ります。戻った後は、配列を書き換えたり解放したりできます。

送信バッファがいっぱいのとき、`cotocoa_send` は受信側の完了通知を待ちます（[しくみ](CONCEPTS.md#1回の送受信で起きること)）。

送ったものがすべて受信側で処理されるまで待ちたいときは、`cotocoa_flush(ctx, &error)` を呼びます。
`cotocoa_close` も、同じように待ってから終了します。

## 受信する

```c
cotocoa_message *message = NULL;
int rc = cotocoa_receive(ctx, "potential", &message, &error);
if (rc == COTOCOA_END) break;                     /* 終端 */
if (rc != COTOCOA_OK) cotocoa_abort(ctx, &error); /* エラー */
```

`cotocoa_receive` は、メッセージが届くか、終端になるまで待ちます。
戻り値が `COTOCOA_END` なら、すべての送信側が終了していて、もう届くものはありません。

第2引数は自分の受信ポート名です。入力ポートが複数ある場合は `NULL` を渡します
（[複数の配列を受け取る](CONNECTIONS.md)）。

### 配列を読む

```c
cotocoa_array_view view;
cotocoa_message_view(message, &view, &error);
```

受信した配列もviewとして得られます。
要素 (i, j, k) のアドレスは `view.data + i*strides[0] + j*strides[1] + k*strides[2]`（バイト単位）です。

受信した配列は、**Cの多次元配列と同じ並びとは限りません**。
`(const double (*)[8])view.data` のようにキャストして読むと、値の位置がずれます。
例のように、必ず `strides` を使ってアドレスを計算してください。

`view.data` が指す領域は、`cotocoa_message_release` を呼ぶまで有効です。
後で使う値は、自分の配列へコピーしておきます。

### 付随情報を読む

```c
cotocoa_metadata meta;
cotocoa_message_metadata(message, &meta, &error);
```

| メンバー | 内容 |
| --- | --- |
| `step` | 送信側が付けたステップ番号 |
| `has_time`・`time` | 時刻が付いているか、その値 |
| `offset[3]` | 全体領域の中での開始位置（0始まり） |
| `shape[3]`・`global_shape[3]`・`ndim` | 届いた配列の形状、全体領域の形状、次元数 |
| `source_rank` | 送信側プログラムの何番目のプロセスが送ったか（0始まり） |
| `source`・`port` | 送信側のプログラム名、受信ポート名 |

すべての項目は[リファレンス](REFERENCE.md#メッセージに付く情報)にあります。

### 完了を通知し、解放する

```c
if (cotocoa_complete(message, &error))
    cotocoa_abort(ctx, &error);
cotocoa_message_release(message);
```

処理が終わったら、**2つ**の関数をこの順で呼びます。

| 関数 | すること |
| --- | --- |
| `cotocoa_complete` | 処理の完了を送信側へ知らせる。これを呼ぶまで、送信側の送信バッファは空かない |
| `cotocoa_message_release` | メッセージが使っていたメモリを解放する |

次の `cotocoa_receive` は、この2つを呼んだ後に呼んでください。
`cotocoa_complete` を呼ばずに `cotocoa_message_release` を呼ぶと、エラーとしてジョブが中断されます。

## 既存のMPIプログラムへ組み込む

すでにMPIで並列化されているプログラムに組み込むときは、次の4点を変更します。

1. **Contextを開く。**
   `MPI_Init` の直後で `cotocoa_open` を呼びます。
   `MPI_Init` を呼ばずに `cotocoa_open` だけでも構いません。その場合は `cotocoa_open` がMPIを初期化します。
2. **`MPI_COMM_WORLD` を `cotocoa_comm(ctx)` に置き換える。**
   `MPI_COMM_WORLD` には相手のプログラムやCouplerも含まれるため、そのまま使うと通信が止まります
   （[理由](CONCEPTS.md#自分の計算には-ctxcomm-を使う)）。
3. **送受信を追加する。**
   送信側なら時間ステップのループの中に `cotocoa_send` を、受信側なら受信ループを追加します。
4. **Contextを閉じる。**
   最後に `cotocoa_close` を呼びます。
   自分で `MPI_Init` を呼んでいる場合は、その後でこれまでどおり `MPI_Finalize` を呼びます。
   `cotocoa_open` がMPIを初期化した場合は、`cotocoa_close` が `MPI_Finalize` まで行います。

`cotocoa_open` を呼ぶプログラムは、`cotocoa run` から、相手のプログラムやCouplerと一緒に起動する必要があります。
単独で起動すると、`cotocoa_open` がエラーになります。
同じプログラムを連携なしでも実行したい場合は、連携するときだけ `cotocoa_open` を呼び、
使うコミュニケータ（`cotocoa_comm(ctx)` か `MPI_COMM_WORLD`）を変数に入れておきます。
考え方は[Fortranの例](GUIDE-FORTRAN.md#連携しない実行も残す)と同じです。

## 戻り値とエラー

| 戻り値 | 値 | 意味 |
| --- | --- | --- |
| `COTOCOA_OK` | 0 | 成功 |
| `COTOCOA_END` | 1 | 受信の終端（エラーではない） |
| `COTOCOA_INVALID` | −1 | 引数や設定が正しくない |
| `COTOCOA_STATE` | −2 | 呼び出す順序や状態が正しくない |
| `COTOCOA_MEMORY` | −3 | メモリを確保できない |

エラーの説明は `error.message` に入ります。
`cotocoa_abort(ctx, &error)` は、その説明を標準エラー出力へ表示し、ジョブ全体を中断します。

受信した内容に矛盾があるなど、続行できないエラーでは、ライブラリが直接ジョブを中断することがあります。

## 関数一覧

| 関数 | 用途 |
| --- | --- |
| `cotocoa_open`・`cotocoa_close` | Contextを開く・閉じる |
| `cotocoa_comm` | 自分のプログラム（Taskの受信側では自分のグループ）のコミュニケータ |
| `cotocoa_view_f64`・`cotocoa_view_f32`・`cotocoa_view_i32` | 配列のviewを作る |
| `cotocoa_send_defaults` | 送信オプションを既定値で初期化する |
| `cotocoa_send` | Streamへ配列を送る |
| `cotocoa_receive` | メッセージを受け取る |
| `cotocoa_message_view`・`cotocoa_message_metadata` | 受信した配列と付随情報を得る |
| `cotocoa_complete`・`cotocoa_message_release` | 完了を通知する・メッセージを解放する |
| `cotocoa_flush` | 送ったものがすべて完了するまで待つ |
| `cotocoa_submit`・`cotocoa_next_task` | [Task](TASKS.md)を依頼する・受け取る |
| `cotocoa_borrow`・`cotocoa_submit_borrow` | [コピーせずに送る](BORROW.md) |
| `cotocoa_ticket_test`・`cotocoa_ticket_wait`・`cotocoa_ticket_release` | 完了したか調べる・完了を待つ・控えを手放す |
| `cotocoa_assembler_*` | [分割された配列をまとめる](ASSEMBLY.md) |
| `cotocoa_abort` | エラーを表示してジョブ全体を中断する |

宣言は[cotocoa.h](../include/cotocoa.h)にあります。

## ビルドする

[インストール](INSTALL.md#自分のプロジェクトから使う)したCoToCoAを使う `CMakeLists.txt` の例です。

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_coupling LANGUAGES C)
find_package(cotocoa 0.3 REQUIRED)

add_executable(simulation simulation.c)
target_link_libraries(simulation PRIVATE cotocoa::cotocoa)

add_executable(analysis analysis.c)
target_link_libraries(analysis PRIVATE cotocoa::cotocoa)
```

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpicc \
  -DCMAKE_PREFIX_PATH="$HOME/.local/cotocoa"
cmake --build build
```

CMakeを使わない場合（Makefileなど）のオプションは、[インストール](INSTALL.md#cやfortranのプログラムをビルドできるようにする)にあります。

## 次に読むもの

- 複数の配列を送る、複数の相手とつなぐ → [複数の配列・複数の相手とつなぐ](CONNECTIONS.md)
- 独立した計算をワーカーへ割り振る → [計算をワーカーへ依頼する](TASKS.md)
- 分割された配列を受信側でまとめる → [分割された配列をまとめる](ASSEMBLY.md)
- 止まった・エラーが出た → [困ったとき](TROUBLESHOOTING.md)
