# Cで使う

[ドキュメント一覧](README.md) · [共通仕様](HIGH-LEVEL-API.md)

`cotocoa.h` をインクルードし、`libcotocoa` にリンクします。
C APIは、配列のポインタ・型・形状・strideをまとめたviewを送受信します。

## 送受信の基本

次は `shape: [3]`、`dtype: float64` のStreamを送受信するプログラムです。
`producer` と `consumer` を各1rank、Couplerを1rankで起動する設定と組み合わせます。
入門例の3次元配列を使う全ソースは[examples/snapshot/c.c](../examples/snapshot/c.c)にあります。

```c
#include "cotocoa.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    cotocoa_context *ctx = NULL;
    cotocoa_error error = {0};
    if (argc != 2) return 2;

    if (cotocoa_open(argv[1], "cotocoa.yaml", &ctx, &error))
        cotocoa_abort(ctx, &error);

    if (!strcmp(argv[1], "producer")) {
        double values[3] = {1.0, 2.0, 3.0};
        int64_t shape[3] = {3, 1, 1};
        cotocoa_array_view view =
            cotocoa_view_f64(values, 1, shape, COTOCOA_ORDER_C);
        cotocoa_send_options options = cotocoa_send_defaults();
        options.step = 1;
        if (cotocoa_send(ctx, "values", &view, &options, &error))
            cotocoa_abort(ctx, &error);
    } else {
        for (;;) {
            cotocoa_message *message = NULL;
            int rc = cotocoa_receive(ctx, "values", &message, &error);
            if (rc == COTOCOA_END) break;
            if (rc != COTOCOA_OK) cotocoa_abort(ctx, &error);

            cotocoa_array_view view;
            if (cotocoa_message_view(message, &view, &error))
                cotocoa_abort(ctx, &error);
            for (int64_t i = 0; i < view.shape[0]; ++i) {
                const char *element =
                    (const char *)view.data + i * view.strides[0];
                printf("%g\n", *(const double *)element);
            }

            if (cotocoa_complete(message, &error))
                cotocoa_abort(ctx, &error);
            cotocoa_message_release(message);
        }
    }

    if (cotocoa_close(ctx, &error)) cotocoa_abort(NULL, &error);
    return 0;
}
```

受信側では `complete` が処理完了を通知し、`message_release` がローカルのメモリを解放します。
両方を呼んでから次を受信してください。
`view.data` はmessageを解放するまで有効です。後で使う値は利用者の配列へコピーします。

上のソースを `transfer.c` としてビルドした実行ファイル `transfer` と、
次の `cotocoa.yaml` を同じディレクトリに置きます。

```yaml
schema: 1
programs:
  producer:
    command: ["./transfer", "producer"]
    ranks: 1
  consumer:
    command: ["./transfer", "consumer"]
    ranks: 1
connections:
  - name: values
    from: producer.values
    to: consumer.values
    dtype: float64
    shape: [3]
    global_shape: [3]
    axes: [item]
```

3rankを使える環境で `cotocoa run cotocoa.yaml --launcher mpiexec` を実行すると、
consumerが1、2、3を表示します。Slurmでは確保済みのジョブ内で `--launcher slurm` を使います。

## 配列viewを作る

| 型 | viewを作る関数 | dtype定数 |
| --- | --- | --- |
| `double` | `cotocoa_view_f64` | `COTOCOA_FLOAT64` |
| `float` | `cotocoa_view_f32` | `COTOCOA_FLOAT32` |
| `int32_t` | `cotocoa_view_i32` | `COTOCOA_INT32` |

関数の引数は `(data, ndim, shape, order)` です。
連続配列のorderは `COTOCOA_ORDER_C` または `COTOCOA_ORDER_F` を指定します。

非連続配列は `cotocoa_array_view` を直接設定します。
`strides[d]` は、軸dの添字を1増やしたときの**バイト数**です。
負のstrideでは `data` を論理上の先頭要素へ向けてください。
受信側も送信元のメモリ配置を仮定せず、返されたstrideで参照します。

## 関数を選ぶ

| 用途 | 関数 |
| --- | --- |
| 開始・終了 | `cotocoa_open` / `cotocoa_close` |
| 計算用のcommunicator | `cotocoa_comm` |
| Stream送信・受信 | `cotocoa_send` / `cotocoa_receive` |
| Task送信・受信 | `cotocoa_submit` / `cotocoa_next_task` |
| 借用送信 | `cotocoa_borrow` / `cotocoa_submit_borrow` |
| 全送信の完了待ち | `cotocoa_flush` |
| 配列・メタデータ取得 | `cotocoa_message_view` / `cotocoa_message_metadata` |
| 処理完了・メッセージ解放 | `cotocoa_complete` / `cotocoa_message_release` |
| Ticketの確認・待機・解放 | `cotocoa_ticket_test` / `cotocoa_ticket_wait` / `cotocoa_ticket_release` |

全宣言は[公開ヘッダー](../include/cotocoa.h)にあります。
`cotocoa_receive` のnameへ `NULL` または `"*"` を渡すと全入力ポートを受信します。
Task・borrowは出力引数としてTicketを返します。
Ticketを複数箇所で所有する場合は `cotocoa_ticket_retain` で参照を増やし、それぞれでreleaseします。

送信オプションは `cotocoa_send_defaults()` で初期化します。
既定ではstepとoffsetが0、時刻なしです。
時刻を付ける場合は `options.has_time = 1` と `options.time` を両方設定します。

## 戻り値とエラー

| 戻り値 | 意味 |
| --- | --- |
| `COTOCOA_OK`（0） | 成功 |
| `COTOCOA_END`（1） | 受信終端 |
| `COTOCOA_INVALID`（−1） | 引数や設定が不正 |
| `COTOCOA_STATE`（−2） | 呼び出し順序や状態が不正 |
| `COTOCOA_MEMORY`（−3） | メモリ確保に失敗 |

エラーの説明は `cotocoa_error.message` に入ります。
継続できない場合は `cotocoa_abort` でジョブ全体を中断します。
MPI全体の整合が崩れるエラーでは、ライブラリから直接中断する場合もあります。

## CMakeでリンクする

上のプログラムを `transfer.c` に保存し、[インストール](INSTALL.md)済みのライブラリを使う例です。

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_coupling LANGUAGES C)
find_package(cotocoa 0.3 REQUIRED)
add_executable(transfer transfer.c)
target_link_libraries(transfer PRIVATE cotocoa::cotocoa)
```

CMake設定時にMPIコンパイラと `CMAKE_PREFIX_PATH` を指定してください。
借用送信と領域の組み立ては[配列の保持](DELIVERY-AND-SHARING.md)・[Assembler](ASSEMBLY.md)を参照してください。
