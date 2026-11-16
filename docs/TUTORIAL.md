# チュートリアル：最初の連携を動かす

[ドキュメント目次](README.md) ｜ 前：[インストール](INSTALL.md) ｜ 次：[しくみと用語](CONCEPTS.md)

付属の例を動かしながら、CoToCoAで連携するときに書くものを一通り見ていきます。
読み終えると、次のことが分かります。

- 設定ファイル（YAML）に何を書くか
- 送る側・受け取る側のプログラムに何を書くか
- どうやって起動し、結果をどう確認するか
- 片方のプログラムを別の言語に替えるにはどうするか

[インストール](INSTALL.md)を終え、リポジトリの直下に `build/` と `.venv/` がある状態から始めます。
コマンドはすべてリポジトリの直下で実行します。

## この例で動かすもの

2プロセスで動く送信側プログラムが、電位に見立てた3次元配列を10ステップ分送ります。
1プロセスの受信側プログラムが、届いた配列の平均値を表示します。

```text
                  one MPI job (4 processes)

  producer rank 0 ---+
  field(3,5,7)       |      +---------+
  x = 0..2           +----> | Coupler | ----> consumer rank 0
                     |      +---------+       print the mean of
  producer rank 1 ---+                        each received array
  field(3,5,7)
  x = 3..5
```

送信側は領域をx方向に2分割していて、各プロセスが自分の担当分（3×5×7）を送ります。
全体の領域は6×5×7です。

送信側と受信側のほかに、配送を中継する **Coupler** が1プロセス動きます。
Couplerは付属のプログラムで、自分で書く必要はありません。
Couplerが中継するのは「送信があった」という通知だけで、配列そのものは送信側から受信側へ直接渡ります。
この例で必要なMPIプロセスは、2 + 1 + 1 = **4** です。

## 1. 設定ファイルを読む

[examples/snapshot/cotocoa.yaml](../examples/snapshot/cotocoa.yaml) が、この連携の設定です。

```yaml
schema: 1
programs:
  producer:
    command: ["../../build/snapshot_fortran", "producer"]
    ranks: 2
  consumer:
    command: ["../../build/snapshot_fortran", "consumer"]
    ranks: 1
connections:
  - name: potential
    from: producer.potential
    to: consumer.potential
    dtype: float64
    shape: [3, 5, 7]
    global_shape: [6, 5, 7]
    axes: [x, y, z]
    units: V
    time_unit: s
```

実際のファイルには、この後に既定値を明示した4行（`mode`・`distribution`・`delivery`・`buffer_slots`）が続きます。
省略しても同じ動作になるので、ここでは省いています。

設定は2つの部分からなります。

**`programs`：動かすプログラム。**
`producer` と `consumer` はプログラムに付けた名前です。
名前は自由に付けられます（言語別のガイドでは、同じ役割のプログラムに `simulation`・`analysis` と名付けています）。
`command` は起動するコマンド、`ranks` はMPIプロセス数です。
この例では同じ実行ファイルを、引数を変えて送信側・受信側の両方に使っています。
`command` の相対パスは、設定ファイルのあるディレクトリ（`examples/snapshot/`）から見たパスです。

**`connections`：プログラム間をつなぐ線。**
`from: producer.potential` は「`producer` というプログラムの、`potential` という名前の出口」を表します。
`to: consumer.potential` は「`consumer` の、`potential` という名前の入口」です。
この出入口の名前を **ポート** と呼びます。プログラムのコードでは、ポート名を指定して送受信します。

残りの項目は、この線を流れる配列の形式です。

| 項目 | この例の値 | 意味 |
| --- | --- | --- |
| `dtype` | `float64` | 要素の型 |
| `shape` | `[3, 5, 7]` | 1プロセスが1回に送る配列の形状 |
| `global_shape` | `[6, 5, 7]` | 全プロセス分を合わせた領域の形状 |
| `axes` | `[x, y, z]` | 各軸の名前 |
| `units` | `V` | 値の単位（受信側へ伝える注記） |
| `time_unit` | `s` | `send` に付けられる時刻の単位。この例では時刻を付けていない |

どちらのプログラムが送信側で、どちらが受信側かは、`from` と `to` から決まります。

## 2. 設定を確認する

```sh
.venv/bin/cotocoa check examples/snapshot/cotocoa.yaml
```

```text
valid: producer -> consumer; 4 ranks including Coupler
```

`check` は、設定の書き間違いや実行ファイルの有無を調べます。MPIプログラムは起動しません。
表示の「4 ranks including Coupler」が、ジョブに必要なMPIプロセス数です。

## 3. 実行する

`cotocoa run` が、送信側・Coupler・受信側をまとめて1つのMPIジョブとして起動します。
起動方法を `--launcher` で選びます。

**自分のPCなど、`mpiexec` を直接使える環境：**

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher mpiexec
```

**Slurmを使うスーパーコンピュータ：**
4プロセス以上を確保したジョブの中で、次のコマンドを**1回だけ**実行します。

```sh
.venv/bin/cotocoa run examples/snapshot/cotocoa.yaml --launcher slurm
```

`cotocoa run` は、内部で `srun` を呼んで4プロセスを起動します。
`cotocoa run` 自体を `srun -n 4` で起動しないでください。4つの `cotocoa run` が別々に動いてしまいます。

KUDPC用のジョブスクリプト [examples/snapshot/kudpc.sbatch](../examples/snapshot/kudpc.sbatch) も用意しています。
キュー名を自分が使えるものに替えて投入します。

```sh
sbatch -p キュー名 examples/snapshot/kudpc.sbatch
```

このスクリプトの出力は `build/cotocoa-snapshot.ジョブID.out` に書かれます。

## 4. 結果を見る

次のような出力が得られます（数値の桁揃えはコンパイラによって異なります）。

```text
cotocoa: run record: /path/to/cotocoa/examples/snapshot/.cotocoa/runs/20261116T001416Z-1a2b3c4d
 step=    1  source=    1  mean=   2.00000000000000
 step=    1  source=    0  mean=   1.00000000000000
 step=    2  source=    1  mean=   3.00000000000000
 step=    2  source=    0  mean=   2.00000000000000
 ...
 step=   10  source=    1  mean=   11.0000000000000
 step=   10  source=    0  mean=   10.0000000000000
```

1行目は、この実行の記録が保存された場所です。後で `cotocoa status` に渡すと、実行状態を確認できます。

2行目以降が受信側の表示で、1行が受信した配列1つに対応します。

- `step` は、送信側が付けたステップ番号（1〜10）です。
- `source` は、送信側の何番目のプロセスが送ったかを表します（0または1）。
- `mean` は、届いた配列の平均値です。送信側は配列を `step + source` で埋めているので、平均もその値になります。

2プロセスが10回ずつ送るので、受信は合計**20件**です。
送信側の2プロセスの配列は、1つにまとめられずに**別々に届く**ことに注目してください。
同じプロセスからの配列はステップ順に届きますが、`source=0` と `source=1` のどちらが先に届くかは実行ごとに変わります。

## 5. コードを読む

この例のFortranプログラム [examples/snapshot/fortran.f90](../examples/snapshot/fortran.f90) を、部分ごとに見ていきます。

### 開始：Contextを開く

```fortran
type(ctca_context) :: ctx

call get_command_argument(1, name)     ! 'producer' または 'consumer'
call ctx%open(trim(name))
call MPI_Comm_rank(ctx%comm(), rank, ierr)
```

`ctx%open` に、設定ファイルの `programs` に書いた自分の名前を渡します。
これでCoToCoAに参加し、送受信ができる状態になります。この状態を表す変数 `ctx` を **Context** と呼びます。
型名は `ctca_` で始まりますが、`use cotocoa` で読み込む型です。従来APIの `ctca` モジュールとは関係ありません。

MPIの初期化（`MPI_Init`）は `ctx%open` が行うので、自分で呼ぶ必要はありません。
すでに `MPI_Init` を呼んでいるプログラムでは、そのまま残して構いません
（[既存のMPIプログラムへ組み込む](GUIDE-FORTRAN.md#既存のmpiプログラムへ組み込む)）。

`ctx%comm()` は、**自分のプログラムのプロセスだけ**を含むMPIコミュニケータを返します。
`MPI_COMM_WORLD` にはCouplerや相手のプログラムも含まれるため、
プロセス番号の取得や自分の計算のためのMPI通信には、`MPI_COMM_WORLD` の代わりに `ctx%comm()` を使います。
ここでは `rank` に、送信側なら0または1が入ります。

### 送信側：配列を送る

```fortran
real(c_double) :: field(3,5,7)

do step = 1, 10
  field = real(step+rank, c_double)
  call ctx%send('potential', field, step=int(step,c_int64_t), &
                offset=[int(rank*3,c_int64_t), 0_c_int64_t, 0_c_int64_t])
end do
```

`ctx%send` の引数は次のとおりです。

| 引数 | この例の値 | 意味 |
| --- | --- | --- |
| ポート名 | `'potential'` | 設定の `from: producer.potential` のポート名 |
| 配列 | `field` | 送る配列。型と形状は設定の `dtype`・`shape` と一致させる |
| `step` | 1〜10 | この配列に付けるステップ番号。受信側へそのまま伝わる |
| `offset` | `[rank*3, 0, 0]` | この配列が全体領域のどこから始まるか（**0始まり**） |

`offset` は、rank 0なら `[0,0,0]`、rank 1なら `[3,0,0]` です。
これによって受信側は、届いた配列が全体領域 `[6,5,7]` のどの部分かを知ることができます。

`send` は配列の内容をコピーしてから戻ります。
戻った後は、受信側の処理を待たずに `field` を書き換えて次のステップへ進めます。

### 受信側：配列を受け取る

```fortran
type(ctca_message) :: message
real(c_double), allocatable :: received(:,:,:)
logical :: available

do
  call ctx%receive('potential', message, has_message=available)
  if (.not. available) exit
  call message%copy_to(received)
  print *, 'step=',message%step(),' source=',message%source_rank(), &
           ' mean=',sum(received)/size(received)
  call message%complete()
end do
```

受信側は「受け取る → 処理する → 完了を知らせる」を繰り返します。

1. `ctx%receive` は、配列が1つ届くまで待ちます。届いたものを **メッセージ** と呼びます。
   メッセージには、配列と、`step` や送信元などの付随情報が入っています。
2. `message%copy_to(received)` で、配列を自分の変数へ取り出します。`received` は自動的に割り付けられます。
3. `message%step()` や `message%source_rank()` で、付随情報を取り出します。
4. 処理が終わったら `message%complete()` を呼びます。これを **完了通知** と呼びます。

完了通知は省略できません。
送信側は、送った配列のコピーを、受信側が完了通知するまで保持しています。
完了通知がないとコピーがたまり、やがて送信側の `send` が待ったままになります。

すべての送信側プログラムが終了すると、`ctx%receive` は `available` に偽を返します。
ここでループを抜けます。受信側は、こうして終わりが知らされるまで受信を続けてください。

### 終了：Contextを閉じる

```fortran
call ctx%close()
```

送信側の `close` は、送った配列がすべて受信側で処理されるのを待ってから終了します。
`ctx%open` がMPIを初期化した場合は、`close` がMPIの終了処理（`MPI_Finalize`）も行います。

## 6. 受信側をPythonに替える

送信側のFortranプログラムはそのままで、受信側だけをPythonに替えてみます。
受信側のPythonプログラムは [examples/snapshot/python.py](../examples/snapshot/python.py) の後半です。

```python
with Context(name) as ctx:
    for message in ctx.receive("potential"):
        with message:
            print(message.step, message.source_rank, message.array.mean())
```

Fortran版と同じ流れです。`with message:` のブロックを抜けるときに、自動で完了通知が行われます。

設定ファイルで変わるのは、`consumer` の `command` の1行だけです。
変更済みの設定を [examples/snapshot/fortran-to-python.yaml](../examples/snapshot/fortran-to-python.yaml) に用意しています。

```yaml
  consumer:
    command: ["../../.venv/bin/python", "python.py", "consumer"]
    ranks: 1
```

実行方法はこれまでと同じです。

```sh
.venv/bin/cotocoa run examples/snapshot/fortran-to-python.yaml --launcher mpiexec
```

```text
1 0 1.0
1 1 2.0
2 0 2.0
2 1 3.0
...
10 0 10.0
10 1 11.0
```

送信側のFortranプログラムには手を加えていません。
相手がどの言語で書かれているかは設定ファイルだけで決まり、コードには現れないためです。

ほかの組み合わせも、`command` を次の表から選ぶだけで試せます。

| 言語 | `producer` の `command` | `consumer` の `command` |
| --- | --- | --- |
| Fortran | `["../../build/snapshot_fortran", "producer"]` | `["../../build/snapshot_fortran", "consumer"]` |
| C | `["../../build/snapshot_c", "producer"]` | `["../../build/snapshot_c", "consumer"]` |
| Python | `["../../.venv/bin/python", "python.py", "producer"]` | `["../../.venv/bin/python", "python.py", "consumer"]` |

Cの受信側は、平均値の代わりに配列の先頭の値を表示します（この例ではどちらも同じ値です）。
ソースは [c.c](../examples/snapshot/c.c) と [python.py](../examples/snapshot/python.py) にあります。

## 7. うまく動かないとき

| 症状 | 確認すること |
| --- | --- |
| `command is not executable` と表示される | `build/` にサンプルがビルドされているか。[インストール](INSTALL.md)の手順2を確認する |
| `cannot load libcotocoa` と表示される | `build/libcotocoa.so` があるか |
| `--launcher slurm requires an existing Slurm allocation` と表示される | Slurmのジョブの中で実行しているか |
| `run/probe must execute on compute resources` と表示される | KUDPCのログインノードで実行している。ジョブの中で実行する |
| 何も表示されないまま終わらない | 確保したプロセス数が4以上か。`cotocoa status examples/snapshot/.cotocoa/runs` で状態を見る |

ほかの症状は[困ったとき](TROUBLESHOOTING.md)を参照してください。

## 次に読むもの

- 送受信の流れや「完了通知」の意味をもう少し詳しく知る → [しくみと用語](CONCEPTS.md)
- 自分のプログラムに組み込む → [Fortran](GUIDE-FORTRAN.md)・[C](GUIDE-C.md)・[Python](GUIDE-PYTHON.md)のガイド
- 配列の形状やプロセス数を変える → [設定ファイル](CONFIGURATION.md)
- 分割して届いた配列を全体の配列 `[6,5,7]` にまとめる → [分割された配列をまとめる](ASSEMBLY.md)
