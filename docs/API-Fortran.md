# Fortranで使う

[ドキュメント一覧](README.md) · [共通仕様](HIGH-LEVEL-API.md)

`use cotocoa` で名前付きAPIを読み込みます。
既存の低水準APIを使う `use ctca` とは別のモジュールです。

## 送受信の例

次は[入門例](HIGH-LEVEL-USAGE.md)の設定で動くプログラムです。
引数に `producer` または `consumer` を渡します。

```fortran
program snapshot
  use iso_c_binding, only: c_double, c_int64_t
  use mpi
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(c_double) :: field(3,5,7)
  real(c_double), allocatable :: received(:,:,:)
  integer :: rank, ierr, step
  logical :: available
  character(len=64) :: name

  call get_command_argument(1, name)
  call ctx%open(trim(name))
  call MPI_Comm_rank(ctx%comm(), rank, ierr)

  if (trim(name) == 'producer') then
    do step = 1, 10
      field = real(step + rank, c_double)
      call ctx%send('potential', field, step=int(step,c_int64_t), &
                    offset=[int(rank*3,c_int64_t),0_c_int64_t,0_c_int64_t])
    end do
  else
    do
      call ctx%receive('potential', message, has_message=available)
      if (.not. available) exit
      call message%copy_to(received)
      print *, message%step(), message%source_rank(), sum(received)/size(received)
      call message%complete()
    end do
  end if

  call ctx%close()
end program snapshot
```

`copy_to` は型と次元が一致するallocatable配列へコピーします。
`complete` は処理完了を通知し、受信メッセージを解放します。
コピーした `received` はその後も使えます。

## 型と引数

| 設定のdtype | Fortranの宣言 |
| --- | --- |
| `int32` | `integer(c_int32_t)` |
| `float32` | `real(c_float)` |
| `float64` | `real(c_double)` |

これらのkindは `iso_c_binding` から読み込みます。
各型の1〜3次元配列を `send`・`copy_to` などに渡せます。
`step` と `offset` は `integer(c_int64_t)`、`time` は `real(c_double)` です。
offsetはFortranでも0始まりです。

| 呼び出し | 用途 |
| --- | --- |
| `ctx%open(name, config, status)` | 開始。config省略時は `cotocoa.yaml` |
| `ctx%comm()` | 自分のプログラム、またはワーカーグループのMPI communicator |
| `ctx%send(name, array, step, time, offset, status)` | Streamへsnapshotを送る |
| `ctx%borrow(name, array, ticket, step, time, offset, status)` | Streamへ借用送信 |
| `ctx%submit(name, array, ticket, step, time, offset, status)` | Taskへsnapshotを送る |
| `ctx%submit_borrow(name, array, ticket, step, time, offset, status)` | Taskへ借用送信 |
| `ctx%receive(name, message, has_message, status)` | Stream受信。終端ではhas_messageが偽 |
| `ctx%next_task(name, message, has_task, status)` | Task受信。終端ではhas_taskが偽 |
| `ctx%flush(status)` / `ctx%close(status)` | 全送信の完了待ち / 終了 |

`status`、`time`、`offset` は省略できます。
`send` ではstepが必須、それ以外の送信では省略時に0です。
offsetの省略時は各軸とも0、timeの省略時は時刻なしになります。
入力が複数ポートに分かれる場合は `name='*'` で受信します。

## メッセージと完了待ち

メタデータは `message%step()`、`message%offset()` などの関数で取得します。
使用できる名前は[メタデータ一覧](HIGH-LEVEL-API.md#配列とメタデータ)と対応します。
次元数は `%ndim()`、時刻の有無は `%has_time()` で調べます。

Ticketは `type(ctca_ticket)` で宣言します。

```fortran
! ctxを開いたproducer内。seedsは接続に合う型・形状の配列。
call ctx%submit('trace', seeds, ticket, step=10_c_int64_t)
call ticket%wait()
call ticket%release()
```

`ticket%test()` は完了状態をlogicalで返します。
`release` はハンドルの解放で、計算の取消しではありません。
借用送信の配列の宣言方法は[borrowの使い方](DELIVERY-AND-SHARING.md#送信時のコピーを省く)を参照してください。

Context、Message、Assemblerは内部の資源を所有するので、代入で複製しないでください。
Ticketの代入は参照を共有します。
通信の終了を変数の自動解放に任せず、`complete` と `close` を明示して呼びます。
`message%release()` は[Assemblerで完成したFrame](ASSEMBLY.md)の解放に使います。

## エラーを確認する

通常は `status` を省略すると、エラー時に診断を表示してジョブを中断します。
呼び出し側で情報を受け取る場合は `type(ctca_status)` を渡します。

```fortran
! statusはtype(ctca_status)で宣言する。
call ctx%flush(status=status)
if (status%code < 0) then
  print *, trim(status%message)
  call MPI_Abort(ctx%comm(), 1, ierr)
end if
```

statusのcodeは0が成功、受信終端は1、負数がエラーです。
不整合な受信など、継続できない状態ではstatusの有無によらず中断する場合があります。

## CMakeでリンクする

[インストール](INSTALL.md)したライブラリを使うプロジェクトの例です。
上のプログラムを `snapshot.f90` に保存した場合、次の設定でビルドできます。

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_coupling LANGUAGES C Fortran)
find_package(cotocoa 0.3 REQUIRED COMPONENTS Fortran)
add_executable(snapshot snapshot.f90)
target_link_libraries(snapshot PRIVATE cotocoa::cotocoa_fortran)
```

設定時にMPIコンパイラと `CMAKE_PREFIX_PATH` を指定してください。
Fortranの `.mod` は、ライブラリのビルドに使ったコンパイラと互換である必要があります。
