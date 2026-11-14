# CoToCoA 高水準 API 設計案

状態: 設計案。以下の Fortran/C/Python API、CLI、設定形式は未実装。
対象: `Nkzono99/cotocoa` の独自拡張。
設計の基点: `8f705f8c6aa8e7c51b98f50c211dd360d247191a`。

Fortran・C・Python で書かれたプログラム間で、名前を付けた配列を安全に渡す。
Fortran 同士、C 同士、Python 同士を含め、各言語が送信側にも受信側にもなれる。
利用者が書くのは送信、受信、解析処理と接続設定とし、area ID、progid、
整数リクエスト配列、Coupler の通常の配送ループをライブラリに移す。

既存の C 通信実装をバックエンドとして使い、C の `ctca.h`、Fortran の `ctca`、
Python の `ctca4py.CTCAWorker` は維持する。新 API は C の `cotocoa.h` と
`cotocoa_*` 関数、Fortran の `cotocoa` モジュール、Python の `cotocoa` パッケージ
で提供する。最初の対象は一方向のデータ連携であり、既存の非同期計算依頼も
別の Task API として扱う。言語の組み合わせを後続段階へ先送りしない。

## 対象言語と共通実装

| 送信側 ＼ 受信側 | Fortran | C | Python |
| --- | --- | --- | --- |
| Fortran | 初版の対象 | 初版の対象 | 初版の対象 |
| C | 初版の対象 | 初版の対象 | 初版の対象 |
| Python | 初版の対象 | 初版の対象 | 初版の対象 |

この表は送信側と受信側に選べる言語を示す。各接続の方向と役割は設定で定める。
同じ言語同士でも、別プログラムの producer と consumer として接続する。
同一プロセスで両役割を担う双方向・循環連成は、別途の拡張とする。

### 言語共通の実装層

Context、名前解決、配列 descriptor、snapshot バッファ、完了通知、MPI の所有権、
内部プロトコルは共通の C 層に置く。Fortran は `iso_c_binding`、Python は
ネイティブ binding を通して同じ C 層を呼び、C 利用者にも同じ高水準操作を公開する。
既存の Python Worker wrapper に他言語の利用者を接続する構成にはしない。

標準 Coupler もネイティブ実行ファイルとして提供する。設定の正規化は起動前に
共通の manifest へまとめ、各 binding が別々の解釈を実装しない。
CLI の実装に Python を使う場合でも、Fortran/C 同士の通信のために MPI ジョブ内で
Python プロセスを起動する必要はない。事前生成した manifest と既存 launcher で
ネイティブプログラムだけを起動できる経路を用意する。

## 1. 開発先と既存実装の前提

[Nkzono99/cotocoa](https://github.com/Nkzono99/cotocoa) は GitHub 上で
`fork: true`、親が `tnanri/cotocoa` と確認できる。設計の基点は確認時の
fork の `master` と一致する。本家への変更依頼を必要としない構成にする。
既存の著作権表示と MIT ライセンスは保持する。

この作業用 checkout の Git 設定は次のとおり。これらは `.git/config` の
ローカル設定であり、別の clone へは引き継がれない。

| 項目 | 設定 |
| --- | --- |
| `origin` | `https://github.com/Nkzono99/cotocoa.git` |
| `remote.pushDefault` | `origin` |
| `upstream` の取得先 | `https://github.com/tnanri/cotocoa.git` |
| `upstream` の push 先 | `disabled://upstream-read-only` |
| 設計ブランチ | `design/high-level-api`、push remote は `origin` |

通常の `git push upstream` は失敗する設定。直接 URL を指定した push や
GitHub の操作まで禁止するアクセス制御ではない。開発時のリモート更新と
必要な PR の宛先は、明示的に `Nkzono99/cotocoa` を指定する。

現状の問題はソースに基づいて次のように整理できる。

| 現状と根拠 | 新 API での扱い |
| --- | --- |
| `include/ctca.h`: 役割別関数、整数パラメーター、area ID | 共通 Context、接続名、型付き配列 |
| `test/sample2_coupler.f90`: 配送と配列の offset を手書き | 設定を読む標準 Coupler |
| `src/ctca.c`: 各 `*_init_detail` が `MPI_Init`、各 finalize が `MPI_Finalize` を呼ぶ | MPI の初期化状態と所有権を管理 |
| `src/ctca.c`: `req_sendreq` は Coupler の受領応答を待つ | 送信の完了と解析の完了を区別 |
| `src/ctca.c`: リクエスト表やデータバッファ不足で `MPI_Abort` | 有限個の送信枠と受付前の待機 |
| `src/ctca.c`: `cpl_pollreq` と `wrk_pollreq` は受信サイズの事前一致を要求 | 固定長の内部ヘッダーと登録領域を利用 |
| `ctca4py/ctca4py/ctca4py.py`: 共有ライブラリの個人環境への絶対パス | インストール先からの探索と明示的な上書き設定 |
| `docs/USAGE_from_Python.md`: 型、要素数、完了通知を手動指定 | schema から受信配列を構築し、完了を構造化 |

現状分析の根拠はこの checkout のソースと同梱ドキュメントとする。

## 2. 最初に提供する操作

| 用途 | Fortran | C | Python |
| --- | --- | --- | --- |
| 開始 | `ctx%open(name, config=...)` | `cotocoa_open` | `with Context(name, config=...)` |
| 配列の送信 | `ctx%send(name, array, step=..., ...)` | `cotocoa_send` | `ctx.send(name, array, step=..., ...)` |
| 配列の受信 | `ctx%receive(name, msg, has_message=...)` | `cotocoa_receive` | `ctx.receive(name)` の反復 |
| メッセージの完了 | `msg%complete()` | `cotocoa_complete` | `with message:` の正常終了 |
| 全件の処理待ち | `ctx%flush()` | `cotocoa_flush` | `ctx.flush()` |
| 終了 | `ctx%close()` | `cotocoa_close` | Context の正常終了 |

3 言語で開始、送信、受信、完了、終了の意味を共通にする。送信の戻りは元の配列を
変更できる時点、メッセージの完了は受信側の処理が終わった時点、flush は呼び出し
rank が送った全件の処理が終わった時点を示す。受信側の言語により保証を変えない。

公開 Fortran 型は `ctca_context`、`ctca_message`、エラー情報を返す `ctca_status`
を基本とする。C は Context と Message を opaque handle として公開し、配列は
dtype、次元数、shape、byte stride、データへの読み取り専用ポインターを持つ
`cotocoa_array_view` で記述する。Python の `Context` は設定に応じてどちらの役割にも
なれる。Requester/Worker を Python の送受信能力の制限に対応させない。

Fortran の `send` は `int32`、`real32`、`real64` の rank 1〜3 の配列を generic procedure
で受ける設計。最初の実装・検証は `real64` の rank 3 に限定し、対応型を
capability として明示する。C と Python も同じ型と次元数の範囲を保証する。
未対応の型を暗黙に変換しない。

Fortran の各手続きの省略可能な `status=` に code、operation、接続名、説明を返す。
`status` を省略したエラーは、開始前なら通常のエラー終了、通信開始後なら
診断を出してジョブ全体を中断する。エラーを無視して通信を続けない。
`status` による回復は、リクエストの発行前で通信状態を変えていない局所的な
入力検証エラーに限る。collective 中や内部プロトコルの不整合では、`status` の
有無によらずジョブ全体を中断し、一部の rank だけを先へ進めない。
C の各操作は戻り値と `cotocoa_error` で同じエラー分類を返す。Python は同じ
分類を例外で表す。C の `cotocoa_abort` は診断付きでジョブを中断し、呼び出し元へ戻らない。

### Fortran の送信側

以下は提案 API の利用例。既存の `ctca` では実行できない。
同名の設定で 2 rank の producer を起動する想定とする。

```fortran
program pic
  use iso_fortran_env, only: real64, int64
  use mpi
  use cotocoa, only: ctca_context
  implicit none

  type(ctca_context) :: ctx
  real(real64) :: phi(16, 8, 4)
  integer(int64) :: step
  integer :: rank, ierr

  call ctx%open("pic", config="cotocoa.yaml")
  call MPI_Comm_rank(ctx%comm(), rank, ierr)

  do step = 1_int64, 10_int64
    phi = real(step, real64) + real(rank, real64)
    call ctx%send("potential", phi, step=step, &
                  time=real(step, real64)*0.01_real64, &
                  offset=[int(rank, int64)*16_int64, 0_int64, 0_int64])
  end do

  call ctx%close()
end program pic
```

`open` と `close` は全役割で対応する開始・終了を行う集団操作。
`send` と `flush` は producer の呼び出し rank だけの操作で、他の producer rank
との暗黙の barrier を入れない。ここでは各 rank が局所配列を 1 件ずつ送る。

`ctx%comm()` は当該プログラム内の communicator を返す。初版は既存の
`use mpi` に対応する整数ハンドルとし、C の `cotocoa_comm(ctx)` は `MPI_Comm`、
Python の `ctx.comm` は `mpi4py.MPI.Comm` を返す。
シミュレーション自身の collective はこの communicator を使う。
MPMD 全体を含む `MPI_COMM_WORLD` を計算コードの collective に使う既存実装では、
communicator の差し替えも必要になる。組み込み量が常に数行で済むとは保証しない。

### Fortran の受信側

次の consumer を `analysis` として設定すれば、上の producer と Fortran 同士で
接続できる。受信完了時には `has_message` が偽となり、エラーとは区別する。

```fortran
program analysis
  use iso_fortran_env, only: real64
  use cotocoa, only: ctca_context, ctca_message
  implicit none

  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(real64), allocatable :: phi(:, :, :)
  logical :: available

  call ctx%open("analysis", config="cotocoa.yaml")
  do
    call ctx%receive("potential", message, has_message=available)
    if (.not. available) exit
    call message%copy_to(phi)
    print *, message%step(), sum(phi)/real(size(phi), real64)
    call message%complete()
  end do
  call ctx%close()
end program analysis
```

`copy_to` は型と次元数を検証し、shape に合わせて allocatable 配列を確保する。
コピー先の値は Message の完了・解放後も保持される。

### C の送信・受信

以下の送信関数は `pic` として open 済みの Context を受け取る例。
`cotocoa_view_f64` は C 順の連続配列から descriptor を構成する補助関数であり、
descriptor を渡す `cotocoa_send` は Fortran/Python と同じ snapshot を保証する。

```c
#include <stdint.h>
#include <cotocoa.h>

void publish(cotocoa_context *ctx, const double phi[16][8][4],
             int64_t step, int64_t x_offset)
{
    const int64_t shape[3] = {16, 8, 4};
    cotocoa_array_view field = cotocoa_view_f64(
        &phi[0][0][0], 3, shape, COTOCOA_ORDER_C);
    cotocoa_send_options options = cotocoa_send_defaults();
    cotocoa_error error = {0};

    options.step = step;
    options.offset[0] = x_offset;
    if (cotocoa_send(ctx, "potential", &field, &options, &error)
        != COTOCOA_OK)
        cotocoa_abort(ctx, &error);
}
```

受信側は `analysis` として open 済みの Context に対して次の操作を行う。
`COTOCOA_END` は正常なストリーム終端とし、エラーと区別する。

```c
void consume(cotocoa_context *ctx)
{
    cotocoa_message *message = NULL;
    cotocoa_error error = {0};

    for (;;) {
        int rc = cotocoa_receive(ctx, "potential", &message, &error);
        if (rc == COTOCOA_END)
            break;
        if (rc != COTOCOA_OK)
            cotocoa_abort(ctx, &error);

        cotocoa_array_view field;
        if (cotocoa_message_view(message, &field, &error) != COTOCOA_OK)
            cotocoa_abort(ctx, &error);
        /* field.data、shape、stride を使って解析する。 */
        if (cotocoa_complete(message, &error) != COTOCOA_OK)
            cotocoa_abort(ctx, &error);
        cotocoa_message_release(message);
    }
}
```

`cotocoa_complete` は通信上の処理完了、`cotocoa_message_release` は受信側の
ローカルオブジェクトの解放を表す。C の view は Message を解放するまで有効で、
完了通知だけでは無効にならない。受信バッファは読み取り専用とし、変更が必要なら
別の配列へコピーする。C の各 rank も Fortran/Python と同じ集団操作に参加する。

### Python の送信側

```python
import numpy as np
from cotocoa import Context

with Context("pic", config="cotocoa.yaml") as ctx:
    rank = ctx.comm.Get_rank()
    for step in range(1, 11):
        phi = np.full((16, 8, 4), step + rank, dtype=np.float64)
        ctx.send("potential", phi, step=step, time=step * 0.01,
                 offset=(rank * 16, 0, 0))
```

### Python の受信側

```python
from cotocoa import Context

with Context("analysis", config="cotocoa.yaml") as ctx:
    for message in ctx.receive("potential"):
        with message:
            print(message.step, message.source_rank, message.array.mean())
```

メッセージには `array`、`step`、`time`、`sequence`、`source_rank`、`shape`、
`global_shape`、`offset`、`axes`、`units` がある。`sequence` は接続・送信 rank
ごとに単調増加するライブラリ管理の通し番号であり、物理的な `step` と区別する。
`time` を省略した送信は受信側で `None` となる。
`units` と `time_unit` は設定に従うメタデータで、自動の単位変換は行わない。

正常な `with message:` の終了で 1 回だけ完了通知を送る。
`message.array` は書き込み不可の受信側所有コピーで、完了後も参照を保持できる。
元の計算配列・送信枠を直接参照しない。保持した受信コピーのメモリ量は利用者が
管理する。次の受信へ進む前に現在のメッセージを完了する必要がある。
Fortran と C でも、通信上の完了とローカルコピーの寿命を同じように分離する。

初版の consumer は 1 接続だけを受ける。`receive(name)` を複数呼んで他の接続を
読み飛ばす挙動は設けない。consumer が複数 rank の場合、receive と完了は
その worker group の集団操作で、同じメッセージを全 rank に配る。
終了までループを回すことを基本とし、途中の `break` や処理中の例外は
診断付きのジョブ中断とする。自動完了して成功扱いにはしない。
Fortran/C の未完了メッセージを残した終了にも同じ規則を適用する。

## 3. 接続設定と起動

```yaml
schema: 1

programs:
  pic:
    command: ["./pic"]
    ranks: 2
  analysis:
    command: ["python", "analysis.py"]
    ranks: 1

connections:
  - name: potential
    from: pic.potential
    to: analysis.potential
    dtype: float64
    shape: [16, 8, 4]
    axes: [x, y, z]
    global_shape: [32, 8, 4]
    units: V
    time_unit: s
    mode: snapshot
    distribution: shards
    delivery: all
    buffer_slots: 2
```

コマンドは引数配列として扱い、shell の式として評価しない。実行パスは設定
ファイルのディレクトリを基準とする。標準 Coupler は CLI が 1 rank 挿入する。
この例の総 rank 数は `2 + 1 + 1 = 4`。consumer は `ranks` 全体で 1 group
を構成する。初版の設定で指定できる producer プログラムは 1 つとする。

接続定義に言語は指定しない。上の `pic` または `analysis` の `command` を
Fortran/C の実行ファイルや Python スクリプトに差し替えても、配列 schema、
配送、完了の契約は同じ。Fortran 同士なら `analysis.command: ["./analysis"]`
とし、Python の送信側なら `pic.command: ["python", "pic.py"]` とする。

複数の接続は、それぞれ別の consumer プログラムへ送る場合に対応する。
同じ送信ポートからの fan-out、複数 producer、consumer 内の複数 group による
負荷分散は後続段階とする。初版では該当する設定を check で拒否する。

| コマンド案 | 挙動 |
| --- | --- |
| `cotocoa check cotocoa.yaml` | 設定、対応機能、実行パス、rank 数、サイズ計算を静的検証 |
| `cotocoa plan cotocoa.yaml` | Coupler を含む rank 配置、バッファ量、起動 argv を表示。`--output` で実行用 manifest を保存 |
| `cotocoa run cotocoa.yaml --launcher <profile>` | 確認済みの launcher profile で MPMD 起動 |

check と plan は MPI を初期化せず、ユーザープログラムを実行しない。
check だけで MPI の実行時互換性や collective の整合までは保証しない。
`run` 前の短い MPI probe で、使用 MPI、ライブラリ ABI、各 rank の設定 digest、
役割と接続を確認する。probe 自体も計算資源内で実行する。

起動方法は MPI 実装や環境によるため、launcher は明示的な profile とする。
汎用的な Slurm/PMI 対応を推測して実行しない。KUDPC では login node 上の
`run` を拒否し、適切な allocation 内での `tssrun` / `srun` 等に接続する
profile を実機確認してから提供する。資源確保や batch submit は別の責務とする。

初版に `status` は含めない。まず rank と接続を含む標準エラー出力、起動計画、
送信・完了件数を提供し、永続的な状態照会は後続段階で設計する。

## 4. データと時間の契約

`shape` は各送信 rank の局所配列の論理次元。初版は接続内で固定する。
軸の意味は `axes` で宣言し、受信側の `array.shape` も同じ順序とする。
Fortran の column-major、C の row-major、NumPy の C/F 順の違いで軸を逆転させない。
例えば論理 shape `(3, 5, 7)` と `axes: [x, y, z]` は、どの言語でも同じ
x/y/z の要素数を意味する。Fortran の `(i+1, j+1, k+1)`、C の `[i][j][k]`、
Python の `[i, j, k]` が同じ論理要素を指す。

内部の連続配置は Fortran 順とし、送信側の descriptor の byte stride に従って
共通 C 層で packing する。C の補助関数は C 順の stride を、Fortran は自身の
配列配置を、Python は NumPy の dtype/shape/strides を descriptor へ渡す。
受信した C の view にも実際の stride を付け、C 順だと仮定した cast は行わない。
Python は同じ論理 shape と Fortran 順で reshape し、Fortran の `copy_to` も
同じ軸順へコピーする。C 順の受信配列が必要な場合は、指定順へコピーする
共通の補助操作を提供する。
非連続スライスは送信時に詰め替え、暗黙のコピーが起きる条件を文書化する。

`offset` は Fortran から指定する場合も 0 始まりの global 座標とする。
shape、global shape、offset、step、sequence の論理整数は 64 bit。
`offset + shape <= global_shape`、積の overflow、dtype とサイズの一致を検証する。
ghost cell は自動判定しない。利用者が送る部分配列に対応した shape と offset
を宣言し、最初の実装では可変 shape、空の局所領域、AMR は未対応とする。

`distribution: shards` では局所領域が個別のメッセージとして届く。
全 rank の同一 step の自動 gather、領域の重複排除、完全な global snapshot
の組み立ては行わない。個々のメッセージの snapshot が安全でも、異なる step の
shard を集めれば同時刻の場にはならない。全体を必要とする利用者は
`step` と全 source rank の到着を照合する必要がある。

`delivery: all` は正常稼働する 1 ジョブ内で、受理した全件を破棄せず 1 回ずつ
処理へ渡す契約。接続・送信 rank ごとの送信順を維持する。
rank 間の全順序、障害後の再配信、永続化、解析処理の副作用の exactly-once は
保証しない。初版は接続ごとに 1 worker group とし、処理完了も直列化する。

`delivery: latest` は未対応として拒否する。追加時には送信元ごとの置換範囲、
破棄件数、処理中のメッセージの保持、複数 shard の同時刻性を別途定める。

## 5. snapshot と処理待ちの実装方針

名前を付けた配列を、ライブラリ所有の登録済みリングバッファへコピーする。
Coupler は固定長の制御情報だけを配送し、Worker がその登録領域を RMA で読む。
既存の役割構成を維持し、通常の利用者には RMA の area ID を公開しない。

1. `open` で設定を正規化し、全役割が同じ順序で各接続の領域を登録する。
   producer は `buffer_slots * product(shape)` 要素の固定領域を持ち、
   Coupler と Worker も対応する登録 collective に参加する。
2. `send` は接続・送信 rank の空き枠を確保する。空きがなければ既存の
   完了を待ち、枠を確保する前に新しいリクエストを発行しない。
3. 入力配列を枠へコピーし、公開メモリへの可視性を同期してから、
   `CTCAR_sendreq_hdl` で制御情報を送る。戻り時点で入力配列を再利用できる。
4. 標準 Coupler は `CTCAC_pollreq` と `CTCAC_enqreq` で配送する。
   アプリケーションが整数配列を解釈する処理は不要。
5. Worker は `CTCAW_pollreq` の情報から型・領域・枠・要素数を決定し、
   対応する `CTCAW_readarea_*` で受信側のコピーへ読み出す。
   複数 rank の group は root が取得して group 内に配布する。
6. 処理後に group 全体が `CTCAW_complete` を呼ぶ。producer は対応する
   handle の完了を確認して、その枠だけを再利用する。

どの言語でも `send` の戻りは受信側の処理完了ではない。`req_sendreq` が Coupler の受領応答を
待つこと、送信枠が埋まれば待機することから、完全に nonblocking な関数でもない。
計算処理を Worker に待たず依頼できる範囲を「非同期」と呼ぶ。
初版はバックグラウンドスレッドを使わず、send、flush、close 等の通信操作で
progress を進める。

初版は処理完了まで送信枠を保持する。受信コピー取得直後に早く解放する
最適化は、処理完了通知とバッファ解放通知を分離してから追加する。
利用者が更新する配列を直接公開して snapshot と呼ぶ実装にはしない。

### 容量の上限

各接続の producer rank 数を R、送信枠数を S、配列の byte 数を B とすると、
送信側の配列保持量は接続ごとの `R*S*B` の総和となる。
この例では `2*2*(16*8*4*8) = 16,384 bytes`。受信コピーや MPI の内部メモリは
この値に含まない。plan には各項目を分けて表示する。

producer の handle 表を当該 rank の送信枠総数以上、Coupler の queue を
全 producer rank の送信枠総数以上に設定する。metadata-only の配送なので
Coupler の配列用 slot は消費しない。これにより受付数を内部の容量以下に保つ。
既存の requester の handle entry 制限（12 bit、初期化条件では 4095 未満）も
check で検証する。無制限の queue や自動的な上限拡張は設けない。

### 内部プロトコル

制御情報は版を持つ固定長ヘッダーとする。初版の案は 32 bit 整数 48 語。
magic、protocol version、設定 digest、operation/connection ID、送信元 rank、
slot と generation、sequence、step、time の有無と値、shape、offset、
要素数を含める。64 bit 値は明示的に分割・復元し、浮動小数の時刻を整数へ
数値変換しない。C struct の padding に依存する転送形式にはしない。

全役割の `maxintparams` を同じ値に設定する。現行の既定値 10 は流用しない。
可変サイズの配列をそのまま既存の `*_pollreq_with*` に混在させる実装は、
受信前のサイズ固定を要求する現在の API と整合しないため採用しない。
初版は MPI ABI、整数/実数のサイズ、byte order が一致するプロセス群を対象とし、
不一致を起動時に拒否する。

ヘッダーと manifest の一致、slot の範囲、generation、要素数を検証してから
RMA 読み出しを行う。MPI window の可視性は unified memory model を暗黙に
仮定せず、公開配列と完了状態表の両方について `MPI_Win_sync` 等の必要な
同期をバックエンドで明示する。既存の volatile な status polling も確認対象。

## 6. MPI の所有権と終了

既存コードはプロセス内の static 状態を持つため、初版は 1 process あたり
1 Context、1 回の open/close に限定する。Context を閉じた後の reopen、
複数インスタンス、任意 communicator を使う独立した連携は未対応とする。

新しい初期化経路では `MPI_Initialized` / `MPI_Finalized` を調べる。
未初期化なら初期化して所有権を記録し、既に初期化済みならその MPI を借用する。
window と内部 communicator は必ず解放し、`MPI_Finalize` は所有する場合だけ
呼ぶ。mpi4py が初期化した MPI や既存シミュレーション側の MPI は終了しない。
既存の低水準入口の契約を維持する経路と、新 Context の経路を分けて実装する。

この変更は高水準 wrapper だけでは完結しない。fork 内の C バックエンドに
所有権を扱う内部初期化・終了処理が必要。本家の変更や承認は前提としない。
Python の import 時点で通信を開始せず、`__del__` に collective の終了処理を
任せない。Fortran の型 finalizer にも通信上の完了や collective な解放を任せない。
全言語の binding と C バックエンドは同じ MPI 実装・互換 ABI を使用する。
C/Fortran アプリケーション自身による初期化、mpi4py による初期化、Context 自身に
よる初期化の各経路で、所有権と communicator の契約を揃える。

正常終了では producer の `close` が自 rank の送信を flush した後に終了を通知する。
全 producer が終了し、Coupler の配送と Worker の処理が空になってから、
Worker の iterator を終了させる。全役割が window の解放に参加する。
利用者が未完了データを保持したまま close して黙って破棄する仕様にはしない。

受信前の検証失敗や利用者処理の例外は、接続、rank、step、処理段階を出力して
fail-fast とする。MPI 障害復旧は初版の対象外。既存の blocking な poll/wait に
単に timeout 引数を付けても中断は保証できないため、タイムアウト対応は
probe/progress 経路の変更と合わせて後続段階で実装する。

## 7. Task API と高度な共有領域

Task API は Stream API と別の名前を使う。送信側の `submit(operation, input, ...)`
は `ctca_ticket` を返し、Worker の `tasks(operation)` で取得した task の
`complete()` に対応する。`ticket%test()` と `ticket%wait()` は処理完了を示す。
入力の型と shape、配送先、保持期間は schema と snapshot の仕組みを共有する。

Task API も 3 言語すべての送信・受信を対象とする。Fortran は `submit/next_task`、
C は `cotocoa_submit/cotocoa_next_task`、Python は `submit/tasks` を設け、
完了を表す ticket の test/wait と task の complete に同じ意味を持たせる。

初版の Task API は後続実装とする。`wait` は計算結果を返す操作ではない。
戻り値を運ぶ result channel、失敗の伝播、取消し、複数 worker group の
負荷分散は、それぞれの通信契約を決めてから追加する。
Task については複数 group に並列配送した場合の完了順を保証しない。

既存の `regarea/readarea/writearea` は低水準 API として使い続けられる。
高水準の借用バッファ API は、寿命、読み出し権、更新の禁止期間、解放通知を
設計した段階で追加する。「ゼロコピー」という語で転送経路のコピー削減と
アプリケーション側の所有権の問題を一括りにしない。

## 8. 実装順序と受け入れ条件

### M0: 共通 C 層と導入経路

- 共通 C 層の高水準 ABI、opaque handle、配列 descriptor、エラー分類を定める。
- 同じ MPI でバックエンドと Fortran/C/Python の各 binding をビルド・起動できるようにする。
- Python の固定パス、配布対象、import 経路、共有ライブラリ探索を整える。
  リポジトリ直下に Python の build metadata がない現状のインストール案内も直す。
- 既存サンプルを互換性確認の基準にし、MPI 自己初期化と借用の両方を確認する。
- ネイティブ Coupler と事前生成 manifest により、Fortran/C 同士のジョブを
  Python の MPI プロセスなしで起動する経路を用意する。

### M1: 全言語の最小連携

- `real64`、3 次元の固定 shape、1 producer program、1 consumer program、
  snapshot/all/shards の設定を共通の最小機能として実装する。
- Fortran/C/Python の各言語で open、send、receive、complete、flush、close を提供する。
- Fortran→Fortran、C→C、Python→Python と、異言語間の両方向を含む全 9 通りを
  初版の必須検証とする。Fortran→Python だけの成功では M1 完了としない。
- 標準 Coupler、静的 check、plan、確認済みの launcher profile を提供する。
- 手書き Coupler、area ID、progid、リクエスト配列を使わない完全実行例を追加する。
  最小は producer 1 + Coupler 1 + consumer 1 の 3 rank。
- 各言語の producer/consumer を差し替えられる例と、Fortran 同士・C 同士の
  起動設定を提供する。
- 2 producer rank と複数 rank の consumer group の例で、全 shard の shape、
  offset、step、値、group 内の配布・完了の一致を確認する。

### M2: 利用範囲の拡張

- `int32/real32` と rank 1/2、非連続スライスの packing を 3 言語へ追加する。
- 接続ごとに異なる consumer への複数接続を追加する。
- Task API、エラー状況の照会、全体の step の再構成を個別の契約として追加する。
- latest、fan-out、複数 worker group、高度な RMA は独立した機能として検証する。

最低限の実行検証では次を確認する。MPI テストは計算ノード上で行う。

| 検証 | 合格条件 |
| --- | --- |
| 言語の組み合わせ全 9 通り | 各言語の送信・受信・完了・正常終端が同じ契約で動作 |
| 送信直後に元配列を上書き | 全言語の受信側で送信時の値を保持 |
| 遅い consumer、送信枠 2、100 回送信 | 有界メモリで待機し、欠落・重複・queue abort がない |
| 非対称な shape `(3, 5, 7)`、C/F 順 | 全 9 通りで各論理添字の値が一致し、軸の逆転がない |
| 2 producer rank | source rank と offset が保持され、各 rank 内の順序が一致 |
| 複数 rank の consumer group | 全言語で同じメッセージが配布され、集団的な完了が成立 |
| 異なる dtype/shape、未対応 delivery | collective 開始前の check、または送信前の検証で説明付きエラー |
| MPI 初期化済み / 未初期化 | 全言語で二重初期化がなく、借用時は close 後も MPI を利用可能 |
| Fortran/C 同士のネイティブ起動 | Python の MPI プロセスを必要とせず、事前生成 manifest で実行可能 |
| Message の完了後のローカルデータ | Fortran のコピー、C の未解放 view、Python の保持参照が有効 |
| 0 件送信 / 最後の処理が遅い | 全役割が正常に終了し、未完了データを破棄しない |
| 受信処理のエラー / 未完了の途中終了 | 全言語で診断付きでジョブが失敗し、成功通知や無期限の正常終了待ちをしない |
| 既存の低水準サンプル | 既存 API の正常動作を保持 |

高水準 API の追加を既存 API の書き換えと混ぜず、各段階で利用例と実測結果を
更新する。この設計段階では API の実装完了や MPI 実行検証済みとは扱わない。
