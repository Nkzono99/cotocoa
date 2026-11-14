# 起動検証と実行記録

check は YAML、実行ファイル、型、shape、接続・役割・容量を静的に検査する。
plan は rank 配置とメモリ量を表示し、native manifest を生成できる。
どちらも MPI を初期化せず、利用者のコマンドを実行しない。

```sh
cotocoa check coupling.yaml
cotocoa plan coupling.yaml --output coupling.manifest
cotocoa probe coupling.yaml --launcher slurm
cotocoa run coupling.yaml --launcher slurm --probe-first
```

probe は全役割を `cotocoa-probe` で起動し、同じ manifest、rank 配置、MPI/data ABI、
communicator、window の生成と終了を検査する。利用者の処理と個々の配列の値までは検査しない。
probeが起動するのはnative C層であり、mpi4pyや利用者コードのリンク環境は実際の連携例でも確認する。
run/probe の controller は確保済み計算資源で1回だけ起動する。
Slurm では設定の総数を `srun -n` に指定し、確保資源の範囲で起動する。
launcher 自体の選択は明示し、MPI 実装の自動推測は行わない。

run と probe は設定ファイルの隣の `.cotocoa/runs/<ID>/` に manifest、dispatch script、
run.json、各 rank の状態を残す。`--run-dir DIR` で新規ディレクトリを指定できる。

```sh
cotocoa status /path/to/.cotocoa/runs
cotocoa status /path/to/.cotocoa/runs/ID --json
```

実行記録には開始・終了、launcher、終了コード、rank ごとの program、処理段階、
最後の接続と step、送受信・完了・latest 破棄件数を含む。
rank 記録は最大1秒に1回と open/close/error 時に atomic に置換する。
CPU 計算や blocking MPI に滞在中の記録は更新されないため、最終観測の状態として読む。
controller が終了記録を残せなかった場合、同じホストでの status は controller-lost と表示する。
不足 rank の記録を成功とみなさない。記録の保存先に書けない場合は stderr で知らせる。

```yaml
runtime:
  timeout_seconds: 600
```

0（既定）は時間制限なし。正数は probe-first を含む run 全体の watchdog となる。
期限では launcher の process group に TERM、5秒後も残る場合に KILL を送り、
controller は失敗を返して記録を残す。Slurm の割り当て時間も別途設定する。

同じ値を native の ticket 待機、Coupler 受付応答、Coupler/Worker の poll の
待機上限にも使う。これらは Iprobe と progress loop で監視し、期限で rank、program、
接続、step、待機操作を表示して MPI_Abort する。
MPI_Init、collective、blocking Send/Get/flush や利用者の長い処理には native の
割込みを保証しないため、run の watchdog と scheduler の制限を併用する。
manifest を直接起動する場合、controller の watchdog はない。
MPI プロセス障害からの復旧と処理の再実行はこの API の責務に含めない。
