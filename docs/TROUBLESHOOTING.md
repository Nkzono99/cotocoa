# 困ったとき

[ドキュメント目次](README.md) ｜ 関連：[起動と実行状態の確認](RUNNING.md)

症状から原因と対処を探すためのページです。

| 症状 | 見る節 |
| --- | --- |
| `cotocoa check` や起動の直後にエラーが出る | [起動できない](#起動できない) |
| エラーは出ないが、止まったまま進まない | [止まったまま進まない](#止まったまま進まない) |
| 実行中にエラーメッセージが出て中断される | [実行中のエラーメッセージ](#実行中のエラーメッセージ) |
| 動くが、届くのが遅い | [届くのが遅い](#届くのが遅い) |
| 動くが、届いた値がおかしい | [届いた値がおかしい](#届いた値がおかしい) |

CoToCoAが出すメッセージは、`cotocoa:` で始まります。
プログラムの実行中に出るものは、次のように、どのプロセスで起きたかが付いています。

```text
cotocoa: rank=3 program=analysis: receive: incomplete message or multiple ports/kinds; use receive-any
```

`rank` はジョブ全体でのプロセス番号、`program` は設定ファイルのプログラム名です。

## 起動できない

### 設定ファイルのエラー（cotocoa check）

| メッセージ | 原因と対処 |
| --- | --- |
| `unknown=[...], missing=[...]` | 知らない項目名（`unknown`）や、足りない必須項目（`missing`）がある。綴りを[設定ファイル](CONFIGURATION.md)と照らし合わせる |
| `duplicate or non-string setting` | 同じ項目を2回書いている |
| `executable not found`・`command is not executable` | `command` の実行ファイルが見つからない、または実行権限がない。相対パスは設定ファイルのあるディレクトリが基準 |
| `connection.from must be program.port` | `from`・`to` は `プログラム名.ポート名` の形で書く |
| `unknown program` | `from`・`to` のプログラム名が `programs` にない |
| `programs cannot mix producer/consumer roles or form cycles` | 同じプログラムが `from` と `to` の両方に現れている。1つのプログラムは送信側か受信側のどちらか |
| `every program must be connected` | どの接続にも現れないプログラムが `programs` にある |
| `global_shape must cover and match the rank of local shape` | `global_shape` の次元数が `shape` と違う、または `shape` より小さい軸がある |
| `fan-out requires identical stream schemas; task ports have one destination` | 同じ送信ポートから出る接続の `dtype`・`shape` などが揃っていない。またはTaskの送信ポートに宛先が2つある |
| `latest requires at least two buffer_slots` | `delivery: latest` の接続で `buffer_slots: 1` にしている |
| `tasks require delivery: all` | Taskの接続に `delivery: latest` を書いている |
| `multiple worker groups require task-only incoming connections` | `group_size` で分けた受信側プログラムに、Streamの接続をつないでいる |

### cotocoa runのエラー

| メッセージ | 原因と対処 |
| --- | --- |
| `cotocoa-coupler is not executable; build/install it or pass its explicit path` | Couplerが見つからない。[ビルド](INSTALL.md)したか確認する。別の場所へインストールした場合は `PATH` に加えるか、`--coupler` で指定する |
| `--launcher slurm requires an existing Slurm allocation` | Slurmのジョブの外で実行している。ジョブスクリプトの中か、対話ジョブの中で実行する |
| `run/probe must execute on compute resources, not a KUDPC login/transfer node` | KUDPCのログインノードで実行している。計算ノードで実行する |
| `cannot load libcotocoa; build with CMake or set COTOCOA_LIBRARY` | Pythonからライブラリを読み込めない。`build/libcotocoa.so` があるか確認する。別の場所にある場合は環境変数 `COTOCOA_LIBRARY` にパスを設定する |

### 起動直後のエラー

**`startup mismatch: program=... expected=... ranks=... expected_ranks=... or manifest differs`**

プロセスの並びが、設定ファイルと合っていません。次を確認してください。

- コードでContextを開くときの名前が、設定ファイルのプログラム名と一致しているか。
  `program=` が実際に渡された名前、`expected=` が設定から期待される名前です。
- `command` に書いたプログラムが、本当にそのプログラム名でContextを開くものか（送信側と受信側の取り違えなど）。
- `ranks=` と `expected_ranks=` が違う場合は、起動されたプロセス数が設定と合っていません。
  確保した資源が足りているか確認してください。

**`incompatible native MPI/data ABI across ranks`**

プログラムによって、使っているMPIや環境が違っています。
CoToCoAのライブラリ、各プログラム、Pythonのmpi4pyが、すべて同じMPIでビルドされているか確認してください。

**`cannot open native manifest`・`invalid or unsupported native manifest`**

プログラムを、`cotocoa run` を使わずに単独で起動しています（`mpiexec -n 4 ./simulation` など）。
Contextを開くプログラムは、相手のプログラムやCouplerと一緒に `cotocoa run` から起動してください。
連携しない実行も同じプログラムで行いたい場合は、[連携しない実行も残す](GUIDE-FORTRAN.md#連携しない実行も残す)を参照してください。

**どのプログラムのせいか分からないとき**

`cotocoa probe 設定ファイル --launcher ...` を実行します（[probe](RUNNING.md#mpi環境を確かめるprobe)）。
`probe` が成功するなら、MPI環境とプロセスの確保は正常で、原因は利用者のプログラムの側にあります。

## 止まったまま進まない

まず、`cotocoa status` で各プロセスの状態を見ます（[実行状態を調べる](RUNNING.md#実行状態を調べる)）。
ジョブの実行中に、ログインノードなど別の端末から実行できます。

```sh
cotocoa status /path/to/.cotocoa/runs
```

見るのは、各プロセスが**待っているかどうか**です。

| 段階の表示 | そのプロセスの状態 |
| --- | --- |
| `ticket-completion` | 送信側が、受信側の完了通知を待っている |
| `worker-poll` | 受信側が、次のメッセージを待っている（StreamでもTaskでもこの名前） |
| それ以外（`open`・`sent`・`processing`・`completed` など） | CoToCoAの中では待っていない。**利用者のコードを実行している** |
| 記録がない | まだContextを開いていない |

待っている間の表示は約1秒ごとに更新されるので、信頼できます。
一方、利用者のコードを実行しているプロセスの段階と件数は、少し前の状態のままになっていることがあります
（たとえば、メッセージを受け取って処理中なのに `open`、`received=0` と表示される）。

この見方で、原因の見当がつきます。

| 送信側 | 受信側 | 考えられる原因 |
| --- | --- | --- |
| `ticket-completion` | 待っていない | 受信側が利用者のコードの中にいる。処理が長いか、完了通知（`complete`）を呼んでいないか、次の受信を呼んでいない |
| 待っていない | `worker-poll`、または待っていない | 送信側が利用者のコードの中にいる。計算が長いか、`MPI_COMM_WORLD` を使った通信で止まっている。送信側がMPIを呼ばない間は、受信側は配列を読み取れずに待つ（[しくみ](CONCEPTS.md#1回の送受信で起きること)） |
| 一部が `closed` | `worker-poll` | 別の送信側プログラムがまだ動いている。受信の終端は、すべての送信側が終了したときに届く |
| 記録がない | 記録がない | どれかのプログラムがContextを開いていない。全プログラムが開くまで、ほかのプログラムは待つ |

よくある原因を、順に説明します。

### 受信側が完了通知をしていない

受信したメッセージを完了通知しないと、送信側の送信バッファが空かず、送信側が `send` や `close` で待ち続けます。

- Python：`with message:` を使っているか。使わない場合は `message.complete()` を呼んでいるか。
- Fortran：`call message%complete()` を呼んでいるか。
- C：`cotocoa_complete` と `cotocoa_message_release` の両方を呼んでいるか。

### 受信側の一部のプロセスしか受信していない

受信側プログラムを複数プロセスで動かす場合、グループ内の**全プロセス**が、同じ順序で受信と完了通知を呼ぶ必要があります。
「rank 0だけが受信する」という書き方をすると、全プロセスが揃うのを待って止まります
（[受信側を複数プロセスで動かす](CONCEPTS.md#受信側を複数プロセスで動かす)）。

### MPI_COMM_WORLDを使っている

`MPI_COMM_WORLD` には、相手のプログラムやCouplerのプロセスも含まれています。
自分のプログラムの中で `MPI_COMM_WORLD` を使って `MPI_Barrier`、`MPI_Bcast`、`MPI_Allreduce` などを呼ぶと、
相手が応じないため、そこで止まります。
Contextから得られるコミュニケータに置き換えてください
（[自分の計算には ctx.comm を使う](CONCEPTS.md#自分の計算には-ctxcomm-を使う)）。

利用しているライブラリが内部で `MPI_COMM_WORLD` を使っている場合も、同じことが起きます。
コミュニケータを指定できるライブラリでは、Contextのコミュニケータを渡してください。

### Contextを開かないプログラムがある

Contextを開く処理は、ジョブ内の全プロセスが揃って行います。
設定ファイルに書いたプログラムのうち1つでもContextを開かないと、ほかの全プログラムが開く処理の中で待ち続けます。
Contextを開く前に長い初期化処理をしている場合も、その間ほかのプログラムは待つことになります。

### 原因が分からないとき

止まったジョブを手で止める代わりに、設定ファイルに[時間制限](RUNNING.md#時間制限を設ける)を書いておく方法があります。

```yaml
runtime:
  timeout_seconds: 60
```

制限時間を過ぎると `cotocoa run` がジョブを中断し、その時点の各プロセスの状態が記録に残ります。
中断の後で `cotocoa status` を実行し、上の表と照らし合わせてください。
短いテスト用の設定で試すときは、時間制限を常に書いておくと、計算資源を無駄にせずに済みます。

## 実行中のエラーメッセージ

### 送信側

| メッセージ | 原因と対処 |
| --- | --- |
| `no stream output port '...'`・`no task output port '...'` | そのポート名の送信ポートがない。設定ファイルの `from` のポート名と一致しているか。Streamの接続に `submit`、Taskの接続に `send` を使っていないか |
| `send ...: array type/rank differs from schema` | 配列の型か次元数が、接続の `dtype`・`shape` と違う |
| `send ...: invalid shape/offset at axis N` | 第N軸（0始まり）で、配列の大きさが `shape` と違う。または `offset + shape` が `global_shape` を超えている。`offset` は0始まり |
| `port ... requires borrowed input`・`port ... requires snapshot input` | `mode: borrow` の接続に通常の送信を使った。または `mode: snapshot` の接続にborrowを使った（[borrow](BORROW.md)） |
| `borrowed regions overlap without sharing an existing attachment; wait for the older ticket` | 完了待ちのborrowと、一部だけ重なる範囲をborrowした（[重なる範囲](BORROW.md#重なる範囲を同時にborrowする場合)） |
| `send requires a native int32/float32/float64 NumPy array with one to three dimensions` | （Python）渡したものがNumPy配列でない、または型が対応外。`int64` の配列やリストは、`np.asarray(x, dtype=...)` で変換する |
| `send/submit requires a producer Context and port` | 受信側のプログラムで送信しようとした |

### 受信側

| メッセージ | 原因と対処 |
| --- | --- |
| `receive: incomplete message or multiple ports/kinds; use receive-any` | 次のどれか。(1) 前のメッセージを完了通知する前に次を受信した。(2) 入力ポートが複数あるのに、ポート名を指定して受信した。ポート名を指定しない受信を使う（[複数の配列を受け取る](CONNECTIONS.md#a-複数の配列を同じ相手へ送る)）。(3) Stream の入力がある受信側で `tasks`・`next_task` を使った |
| `consumer/Coupler must receive end-of-stream before close` | 受信側が、終端まで受信せずにContextを閉じた。受信ループを途中で抜けていないか確認する（[終了のしかた](CONCEPTS.md#終了のしかた)） |
| `cannot release an incomplete message` | （C）`cotocoa_complete` を呼ばずに `cotocoa_message_release` を呼んだ |
| `message already completed` | 同じメッセージを2回完了通知した |
| `receive requires a consumer Context` | 送信側のプログラムで受信しようとした |
| `copy_to: destination type or rank differs from the message` | （Fortran）`copy_to` に渡した配列の型か次元数が、接続と違う |

### Assembler

| メッセージ | 原因と対処 |
| --- | --- |
| `requires a reliable stream with valid source metadata` | Taskや `delivery: latest` の接続のメッセージを渡した |
| `pending step capacity exceeded; finish older frames first` | 送信プロセス間で送信済みステップのずれが大きい（[max_pending_steps](ASSEMBLY.md#max_pending_steps)） |
| `invalid shape/offset or frame exceeds max_frame_bytes` | 全体配列が `max_frame_bytes` より大きい |
| `duplicate or decreasing step from source rank` | 同じ送信プロセスが、同じ `step` を2回送った。または `step` が増えていない |
| `overlapping shards`・`all sources arrived but global frame has holes` | 同じステップの配列の `offset` が重なっている、またはすき間がある。送信側の `offset` を確認する |
| `shards of a step have different times` | 同じステップなのに、プロセスによって `time` が違う |
| `N unfinished step(s); step S has A/B sources` | 終了時に、揃わないステップが残った。全送信プロセスが同じ回数だけ送っているか確認する |

### その他

| メッセージ | 原因と対処 |
| --- | --- |
| `run exceeded timeout_seconds=...`・`timeout waiting for ...` | [時間制限](RUNNING.md#時間制限を設ける)を超えた。止まっている場合は[止まったまま進まない](#止まったまま進まない)を参照。正常に動いているのに出る場合は、`timeout_seconds` を大きくする |
| `only one Context lifetime is supported per process`・`Context supports one open/close lifetime` | 1つのプロセスでContextを2回開こうとした |
| `program ... failed: ...`・`processing ... failed: ...` | （Python）`with` ブロックの中で例外が発生した。続くメッセージが元の例外の内容 |

## 届くのが遅い

送信側が `send` を呼んでから、受信側に届くまでに時間がかかる場合があります。
受信側が配列を読み取れるのは、送信側のプロセスがMPIの処理をしている間だけだからです
（[しくみ](CONCEPTS.md#1回の送受信で起きること)）。
送信側が、MPIを呼ばない長い計算をしている間は、受信側は待つことになります。
送信側が次に `send` を呼ぶか、自分の計算のためのMPI通信を行うと、受信が進みます。エラーではありません。

## 届いた値がおかしい

| 症状 | 確認すること |
| --- | --- |
| 軸が入れ替わったように見える | 形状を言語に合わせて逆順にしていないか。軸の順序は全言語で共通（[軸の順序](CONCEPTS.md#軸の順序はどの言語でも同じ)） |
| （Cの受信側）値の位置がずれる | 受信した配列を、Cの多次元配列にキャストして読んでいないか。`strides` を使って読む（[配列を読む](GUIDE-C.md#配列を読む)） |
| 全体領域の一部しか届かない | 領域分割された配列は、プロセスごとに別々に届く。まとめるには[Assembler](ASSEMBLY.md)を使う |
| `offset` が1ずれている | `offset` はFortranでも0始まり |
| ステップが飛ぶ | 接続が `delivery: latest` になっていないか（[受信が追いつかないとき](DELIVERY.md)） |
| （borrow）途中で書き換わったような値が届く | 完了を待たずに、送った配列を書き換えていないか（[いつ配列を書き換えてよいか](BORROW.md#いつ配列を書き換えてよいか)） |
| （Python）`message.array` に書き込めない | 受信した配列は読み取り専用。`message.array.copy()` でコピーする |

## それでも解決しないとき

次の情報があると、原因を調べやすくなります。

- 設定ファイルと、`cotocoa check` の出力
- `cotocoa status --json 記録ディレクトリ` の出力
- プログラムの標準エラー出力（`cotocoa:` で始まる行）
- 使っているMPIとコンパイラの種類・バージョン
