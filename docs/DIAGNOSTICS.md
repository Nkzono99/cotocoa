# 起動とトラブルシューティング

[ドキュメント一覧](README.md) · 関連：[設定ファイル](CONFIGURATION.md)

起動前にはcheckで設定を確認し、初めて使う環境ではprobeでMPIの接続を確かめます。
実行中の状態はstatusで確認できます。

## コマンド一覧

以下の `cotocoa` は、リポジトリ内の環境なら `.venv/bin/cotocoa` に読み替えてください。

| コマンド | 確認・実行すること |
| --- | --- |
| `cotocoa check coupling.yaml` | 設定のキー、接続、型、形状、実行ファイルの有無 |
| `cotocoa plan coupling.yaml` | MPI rank配置、送受信バッファの見積もり |
| `cotocoa probe coupling.yaml --launcher slurm` | 設定と同じrank配置でMPIの初期化・通信資源の作成・終了 |
| `cotocoa run coupling.yaml --launcher slurm` | 利用者のプログラムを起動 |
| `cotocoa status 実行ディレクトリ` | 実行状態と各rankの最後の状態 |

checkとplanはMPIを初期化せず、利用者のコマンドも実行しません。
probeは付属の確認用プログラムを起動します。
利用者の計算内容やPythonのmpi4py環境までは検査しないので、実際の送受信も確認してください。

## launcherを選ぶ

通常のMPI環境では `--launcher mpiexec` を指定します。
MPI実装が `mpiexec -n ... : -n ...` のMPMD形式に対応している必要があります。

Slurmでは、必要な資源を確保したジョブの中で `--launcher slurm` を指定します。
CoToCoAは資源の予約やジョブ投入を行いません。
CLIを1回だけ実行すると、内部で設定の総rank数を指定してsrunを呼びます。

利用者の全プログラムにCouplerの1rankを加えたタスク数を確保してください。
実行ファイル、設定、実行ディレクトリは、参加する計算ノードから参照できる場所に置きます。

起動確認と本実行を続けて行うには、次を使います。

```sh
cotocoa run coupling.yaml --launcher slurm --probe-first
```

Couplerやprobeを標準の探索先以外へ置いた場合は、
`--coupler /path/to/cotocoa-coupler`・`--probe /path/to/cotocoa-probe` で指定できます。

## 実行状態を調べる

runとprobeは、設定ファイルの隣の `.cotocoa/runs/<ID>/` に実行状況を保存します。
起動時に表示されるパスをstatusへ渡してください。

```sh
cotocoa status /path/to/.cotocoa/runs/ID
cotocoa status /path/to/.cotocoa/runs/ID --json
```

IDを省いて `.cotocoa/runs` を指定すると、その中の最新の実行を表示します。
保存先を変える場合は、run・probeに `--run-dir 新規ディレクトリ` を指定します。

表示には、実行の終了状態、各rankのプログラム名、最後の処理段階、step、
送信・受信・完了・latestによる破棄の件数が含まれます。
これは最後に観測した状態です。利用者の長い計算やMPI内部で待っている間は更新されない場合があります。

状態ファイルがないrankは、正常終了したとは判断できません。
`controller-lost` は、起動を管理していたプロセスが同じホスト上で見つからないことを示します。
Slurmのジョブ状態と標準エラー出力も合わせて確認してください。

## 時間制限を設ける

設定に次を加えると、起動確認を含むrun全体を600秒に制限できます。

```yaml
runtime:
  timeout_seconds: 600
```

時間超過時はlauncherの終了を要求し、5秒で終了しなければ強制終了します。
同じ設定値は、ライブラリ内のTicket待機や配送待ちの診断にも使われます。
MPIの初期化・一部の通信・利用者の計算を、その診断機能だけで中断できるとは限りません。
ジョブ全体の上限はSlurm側にも設定してください。

## よくある症状

| 症状 | 確認すること |
| --- | --- |
| `cannot load libcotocoa` | CMakeでビルドしたか。Pythonが使う `COTOCOA_LIBRARY` は正しいか |
| `command is not executable` | commandのパスと実行権限。相対パスの基準は設定ファイルの場所 |
| Coupler・probeが見つからない | 導入先binのPATH、または `--coupler`・`--probe` |
| Slurm allocationが必要と表示される | 予約済みのジョブ内でCLIを実行しているか |
| KUDPCログインノードでrunが拒否される | 計算ノードのジョブ内へ実行を移す |
| 起動時にrank数・MPIの不一致が出る | 総rank数、全プログラムとmpi4pyが使うMPI実装 |
| send・flush・closeで待ち続ける | consumerが終端まで受信しているか。全group rankがcompleteしているか |
| 特定ポートのreceiveが失敗する | 複数入力ならreceive-anyを使い、message.portで分岐する |
| 配列の型・形状が合わない | 設定のdtype・shape、offsetの範囲。暗黙の変換は行われない |
| Assemblerが未完成を報告する | 全sourceが同じstepを送り、領域を隙間なく埋めているか |
| Assemblerが保持数を超える | max_pending_stepsと、送信元ごとの進行差 |

MPIプロセスの障害後に同じ実行を復旧したり、Taskを自動再実行したりする機能はありません。
原因を修正し、新しいジョブとして起動してください。

## 既存のMPI起動手順へ組み込む

C・FortranのMPIプロセスだけで動かす場合は、事前に設定をmanifestへ変換できます。
manifestはライブラリが読む起動用ファイルです。

```sh
cotocoa plan coupling.yaml --output coupling.manifest
```

参加する全プロセスへ、同じmanifestの絶対パスを
`COTOCOA_MANIFEST` 環境変数で渡します。
プログラムのconfig引数にmanifestのパスを直接渡すこともできます。

起動順は、producerプログラム群、1rankの `cotocoa-coupler`、consumerプログラム群です。
producer群・consumer群の中はそれぞれプログラム名の辞書順で並べ、
planに示すrank数を割り当てます。Couplerにもmanifestのパスを引数として渡します。

C・FortranのAPI自身はYAMLを読みません。
設定を変更したらmanifestを再生成してください。
この起動方法ではCLIのrun全体の時間制限は働かないため、ジョブ管理側で制限します。
