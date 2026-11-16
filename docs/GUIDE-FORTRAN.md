# Fortranから使う

[ドキュメント目次](README.md) ｜ ほかの言語：[C](GUIDE-C.md)・[Python](GUIDE-PYTHON.md)

Fortranのプログラムから配列を送る方法と、受け取る方法を説明します。
[チュートリアル](TUTORIAL.md)を済ませ、[しくみと用語](CONCEPTS.md)に目を通していることを前提にします。

このページの例は、[C](GUIDE-C.md)・[Python](GUIDE-PYTHON.md)のガイドと同じ構成です。
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

### 送信側（simulation.f90）

```fortran
program simulation
  use iso_c_binding, only: c_double, c_int64_t
  use mpi
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  real(c_double) :: potential(16, 8)
  integer :: rank, ierr, step

  call ctx%open('simulation')
  call MPI_Comm_rank(ctx%comm(), rank, ierr)

  do step = 1, 5
    ! ここで本来の計算を行う。この例では step + 0.1*rank で埋める。
    potential = step + 0.1_c_double * rank

    call ctx%send('potential', potential, &
                  step=int(step, c_int64_t), &
                  time=0.5_c_double * step, &
                  offset=[int(16 * rank, c_int64_t), 0_c_int64_t])
  end do

  call ctx%close()
end program simulation
```

### 受信側（analysis.f90）

```fortran
program analysis
  use iso_c_binding, only: c_double, c_int64_t
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(c_double), allocatable :: potential(:, :)
  integer(c_int64_t), allocatable :: offset(:)
  logical :: has_message

  call ctx%open('analysis')

  do
    call ctx%receive('potential', message, has_message)
    if (.not. has_message) exit

    call message%copy_to(potential)
    offset = message%offset()
    print '(a,i0,a,i0,a,i0,a,f0.3)', 'step=', message%step(), &
          ' source=', message%source_rank(), ' offset=', offset(1), &
          ' mean=', sum(potential) / size(potential)

    call message%complete()
  end do

  call ctx%close()
end program analysis
```

### 実行する

次のようにファイルを置き、[ビルド](#ビルドする)してから実行します。

```text
my_coupling/
  CMakeLists.txt
  simulation.f90
  analysis.f90
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

## 型の対応

`use cotocoa` で、次の型が使えるようになります。
型名は `ctca_` で始まりますが、従来APIの `ctca` モジュールとは別のものです。

| 型 | 用途 |
| --- | --- |
| `type(ctca_context)` | Context。CoToCoAへの参加を表す |
| `type(ctca_message)` | 受信したメッセージ |
| `type(ctca_ticket)` | 送ったものの完了を確認する控え（[Task](TASKS.md)・[borrow](BORROW.md)で使う） |
| `type(ctca_assembler)` | 分割された配列をまとめる道具（[Assembler](ASSEMBLY.md)） |
| `type(ctca_status)` | エラー情報を受け取るための型 |

配列と引数の型は、`iso_c_binding` のkindで宣言します。

| 設定ファイルの `dtype` | Fortranの宣言 |
| --- | --- |
| `float64` | `real(c_double)` |
| `float32` | `real(c_float)` |
| `int32` | `integer(c_int32_t)` |

| 引数 | 型 |
| --- | --- |
| `step`・`offset` | `integer(c_int64_t)` |
| `time` | `real(c_double)` |

既存のプログラムの配列宣言を書き換える必要はありません。
`double precision` や `real(8)` で宣言した配列は、一般的なコンパイラでは `real(c_double)` と同じ種別なので、そのまま渡せます。
同じように、`real`（単精度）は `real(c_float)`、通常の `integer` は `integer(c_int32_t)` として渡せます。
種別が合わない場合は、コンパイル時に「一致する手続きがない」というエラーになります。

`step` に通常の整数変数を渡すときは、例のように `int(step, c_int64_t)` で変換します。
定数は `10_c_int64_t` のように書きます。

## 送信する

```fortran
call ctx%send(name, array, step [, time] [, offset] [, status])
```

| 引数 | 必須 | 意味 |
| --- | --- | --- |
| `name` | 必須 | 自分の送信ポート名 |
| `array` | 必須 | 送る配列（1〜3次元）。型と形状は、接続の `dtype`・`shape` と一致させる |
| `step` | 必須 | この配列に付ける番号。受信側へそのまま伝わる |
| `time` | 省略可 | 物理時刻。省略すると「時刻なし」として届く |
| `offset` | 省略可 | 全体領域の中での開始位置。**0始まり**。省略時はすべて0 |
| `status` | 省略可 | [エラー処理](#エラー処理)を参照 |

`array` の下限は1でなくても構いません（`field(0:15, -1:6)` など）。渡した配列の大きさだけが使われます。
部分配列も渡せます。袖領域を持つ配列から内部だけを送るときは、`field(1:nx, 1:ny)` のように範囲を指定します。

`send` は配列をコピーしてから戻ります。戻った後は、配列を書き換えたり解放したりできます。

送信バッファがいっぱいのとき、`send` は受信側の完了通知を待ちます（[しくみ](CONCEPTS.md#1回の送受信で起きること)）。

送ったものがすべて受信側で処理されるまで待ちたいときは、`call ctx%flush()` を呼びます。
`ctx%close()` も、同じように待ってから終了します。

## 受信する

```fortran
call ctx%receive(name, message, has_message [, status])
```

| 引数 | 意味 |
| --- | --- |
| `name` | 自分の受信ポート名。入力ポートが複数ある場合は `'*'`（[複数の配列を受け取る](CONNECTIONS.md)） |
| `message` | 届いたメッセージが入る |
| `has_message` | メッセージが届けば真。すべての送信側が終了して、もう届くものがなければ偽 |

`receive` は、メッセージが届くか、終端になるまで待ちます。

### 配列を取り出す

```fortran
real(c_double), allocatable :: potential(:, :)
call message%copy_to(potential)
```

`copy_to` には、接続と同じ型・同じ次元数の `allocatable` 配列を渡します。
配列は届いた形状で自動的に割り付けられ、内容がコピーされます。
コピーした配列は、完了通知の後も、Contextを閉じた後も使えます。

### 付随情報を取り出す

| 呼び出し | 戻り値の型 | 内容 |
| --- | --- | --- |
| `message%step()` | `integer(c_int64_t)` | 送信側が付けたステップ番号 |
| `message%has_time()`・`message%time()` | `logical`・`real(c_double)` | 時刻が付いているか、その値 |
| `message%offset()` | `integer(c_int64_t)` の配列 | 全体領域の中での開始位置（0始まり） |
| `message%shape()`・`message%global_shape()` | `integer(c_int64_t)` の配列 | 届いた配列の形状、全体領域の形状 |
| `message%source_rank()` | `integer` | 送信側プログラムの何番目のプロセスが送ったか（0始まり） |
| `message%source()`・`message%port()` | `character(len=64)` | 送信側のプログラム名、受信ポート名 |

配列を返す関数（`offset` など）の要素数は、配列の次元数と同じです。
文字列は後ろが空白で埋まっているので、比較するときは `trim(message%port())` とします。
すべての項目は[リファレンス](REFERENCE.md#メッセージに付く情報)にあります。

### 完了を通知する

```fortran
call message%complete()
```

処理が終わったら、必ず `complete` を呼びます。次の `receive` は、その後に呼んでください。
`complete` を呼ぶまで、送信側の送信バッファは空きません。

## 既存のMPIプログラムへ組み込む

すでにMPIで並列化されているプログラムに組み込むときは、次の4点を変更します。

1. **Contextを開く。**
   `MPI_Init` の直後で `call ctx%open('プログラム名')` を呼びます。
   `MPI_Init` を呼ばずに `ctx%open` だけでも構いません。その場合は `ctx%open` がMPIを初期化します。
2. **`MPI_COMM_WORLD` を `ctx%comm()` に置き換える。**
   `MPI_COMM_WORLD` には相手のプログラムやCouplerも含まれるため、そのまま使うと通信が止まります
   （[理由](CONCEPTS.md#自分の計算には-ctxcomm-を使う)）。
   `ctx%comm()` は整数のコミュニケータを返すので、`use mpi` の手続きにそのまま渡せます。
3. **送受信を追加する。**
   送信側なら時間ステップのループの中に `ctx%send` を、受信側なら受信ループを追加します。
4. **Contextを閉じる。**
   最後に `call ctx%close()` を呼びます。
   自分で `MPI_Init` を呼んでいる場合は、その後でこれまでどおり `MPI_Finalize` を呼びます。
   `ctx%open` がMPIを初期化した場合は、`ctx%close` が `MPI_Finalize` まで行います。

`use mpi_f08` を使っている場合は、`comm%MPI_VAL = ctx%comm()` として `type(MPI_Comm)` へ変換します。

`ctx%open` の第2引数 `config` は、通常は省略します。
この引数が使われるのは、[`cotocoa run` を使わずに起動](RUNNING.md#cotocoa-runを使わずに起動する)し、
かつ環境変数 `COTOCOA_MANIFEST` を設定しない場合だけで、そのときはmanifestのパスを渡します。

### 連携しない実行も残す

`ctx%open` を呼ぶプログラムは、`cotocoa run` から、相手のプログラムやCouplerと一緒に起動する必要があります。
`mpiexec -n 4 ./simulation` のように単独で起動すると、`ctx%open` がエラーになります。

同じプログラムを連携なしでも実行したい場合は、連携するかどうかを入力パラメータなどで切り替え、
使うコミュニケータを変数に入れておきます。

```fortran
integer :: comm, ierr
logical :: coupled      ! 入力パラメータなどから決める

call MPI_Init(ierr)
if (coupled) then
  call ctx%open('simulation')
  comm = ctx%comm()
else
  comm = MPI_COMM_WORLD
end if

! 以降の計算では、MPI_COMM_WORLD の代わりに comm を使う。
! 送信は、連携するときだけ行う： if (coupled) call ctx%send(...)

if (coupled) call ctx%close()
call MPI_Finalize(ierr)
```

連携しないときは、これまでどおり `mpiexec` などで単独で起動できます。

## エラー処理

各手続きの `status` を省略すると、エラーが起きたときにメッセージを標準エラー出力へ表示し、ジョブ全体を中断します。
多くの場合は、この動作で十分です。

エラーを自分で処理したいときは、`type(ctca_status)` の変数を渡します。

```fortran
type(ctca_status) :: status

call ctx%flush(status=status)
if (status%code < 0) then
  print *, trim(status%message)
  call MPI_Abort(ctx%comm(), 1, ierr)
end if
```

`status%code` は、0が成功、負の値がエラーです。`receive` が終端に達したときは1になります。

受信した内容に矛盾があるなど、続行できないエラーでは、`status` を渡していてもジョブを中断します。

## 手続き一覧

| 呼び出し | 用途 |
| --- | --- |
| `ctx%open(name [, config] [, status])` | Contextを開く |
| `ctx%close([status])` | Contextを閉じる。送信側では、送ったものの完了を待つ |
| `ctx%comm()` | 自分のプログラム（Taskの受信側では自分のグループ）のコミュニケータ |
| `ctx%send(name, array, step [, time] [, offset] [, status])` | Streamへ配列を送る |
| `ctx%receive(name, message, has_message [, status])` | メッセージを受け取る |
| `ctx%flush([status])` | 送ったものがすべて完了するまで待つ |
| `ctx%submit(name, array, ticket [, step] [, time] [, offset] [, status])` | [Task](TASKS.md)を依頼する |
| `ctx%next_task(name, message, has_task [, status])` | [Task](TASKS.md)を受け取る |
| `ctx%borrow(name, array, ticket [, step] [, time] [, offset] [, status])` | Streamへ[コピーせずに送る](BORROW.md) |
| `ctx%submit_borrow(name, array, ticket [, step] [, time] [, offset] [, status])` | Taskを[コピーせずに依頼する](BORROW.md) |
| `message%copy_to(array [, status])` | 配列を取り出す |
| `message%complete([status])` | 完了を通知する |
| `ticket%test()`・`ticket%wait()`・`ticket%release()` | 完了したか調べる・完了を待つ・控えを手放す |

`send` だけは `step` が必須です。`submit`・`borrow`・`submit_borrow` では省略でき、省略時は0になります。

`type(ctca_context)`・`type(ctca_message)`・`type(ctca_assembler)` の変数は、代入でコピーしないでください。
`type(ctca_ticket)` は代入でき、代入先と代入元が同じ控えを共有します。

## ビルドする

[インストール](INSTALL.md#自分のプロジェクトから使う)したCoToCoAを使う `CMakeLists.txt` の例です。

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_coupling LANGUAGES C Fortran)
find_package(cotocoa 0.3 REQUIRED COMPONENTS Fortran)

add_executable(simulation simulation.f90)
target_link_libraries(simulation PRIVATE cotocoa::cotocoa_fortran)

add_executable(analysis analysis.f90)
target_link_libraries(analysis PRIVATE cotocoa::cotocoa_fortran)
```

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER=mpicc -DCMAKE_Fortran_COMPILER=mpifort \
  -DCMAKE_PREFIX_PATH="$HOME/.local/cotocoa"
cmake --build build
```

コンパイラは、CoToCoAをビルドしたときと同じものを指定します。
CMakeを使わない場合（Makefileなど）のオプションは、[インストール](INSTALL.md#cやfortranのプログラムをビルドできるようにする)にあります。

## 次に読むもの

- 複数の配列を送る、複数の相手とつなぐ → [複数の配列・複数の相手とつなぐ](CONNECTIONS.md)
- 独立した計算をワーカーへ割り振る → [計算をワーカーへ依頼する](TASKS.md)
- 分割された配列を受信側でまとめる → [分割された配列をまとめる](ASSEMBLY.md)
- 止まった・エラーが出た → [困ったとき](TROUBLESHOOTING.md)
