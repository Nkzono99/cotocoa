# API 0.3 の検証記録

KUDPC System A の計算ノードで検証した。環境は `SysA/2022`、`intel/2023.2`、
`intelmpi/2023.2`（MPI 2021.12）、Python 3.12。
記載した最終ジョブはすべて scheduler の `COMPLETED, ExitCode=0:0` を確認している。
異常系は対象の MPI step が非ゼロ終了し、controller が期待する診断と状態を確認したら合格とする。

| ジョブ | 検証内容 | 結果 |
| --- | --- | --- |
| 9178240 | borrow stream/Task 54ケース、3言語のglobal assembly 27ケース | 全件通過 |
| 9179280 | CTest、pytest 37件、2 Python packageの導入、C-only、移動後のC/Fortran CMake consumer | 全件通過 |
| 9179291 | 既存14 + 型・次元27 + 複数接続/Task20 + latest9 の70 MPIケース | 全件通過 |
| 9179296 | 借用登録の共有、部分重複拒否、detach後の再登録、latestの入力保持と破棄件数 | 2ケース通過 |
| 9179298 | 修正した旧packageの導入と `fpm build --compiler mpiifort --c-compiler mpiicc` | 通過 |
| 9179312 | 旧ctca4pyのint32/float32/float64 attached payload、RMA read/write | 3型とも通過 |
| 9179644 | Fortranの正負strideをsnapshot/borrow/Task/Assemblerで使う | 27ケース通過 |
| 9179645 | installed probe、借用fan-outと早期ticket解放、3言語Task例、watchdog、native待機上限、rank不一致、旧Python、失敗後のprobe | 14ケース通過 |
| 9179647 | commit `83fe152` のclean archive、native全target、pytest37件、CTest、実wheel、借用Fortran→PythonとAssembler | 通過 |
| 9179660 | Assemblerの8枚の3D tile、時刻付き・順不同sourceの組み立て、公開Task/snapshot設定のcheck | 通過 |

複数接続の20ケースでは、stream/Taskそれぞれ全9言語間に9型・次元のportを同時に使う。
これは162通りの言語・型・次元・kindの組合せとなる。Taskの2 worker groups、
ticketのコピー・早期解放・Context終了後の待機、複数producer programsも検査した。

借用54ケースは各送信言語で全3型×3次元をstreamとTaskで使い、受信言語を交替させる。
追加のfan-outはC/Fortran/Pythonの各送信側で確認した。Pythonの入力参照は
利用者がticketを捨てても保持され、flush後に解放されることをweakrefでも検査した。

latestは50件/source、2 producer ranks、意図的に遅いconsumerを用い、
中間の欠落を許しながら処理中の値を保持し、両sourceの最終値50が届くことを検査した。
借用latestでも同じ寿命と最終値を確認し、Couplerの破棄件数が正であることを照合した。

Assemblerはrank間の進行差をboundedにするためテストのproducerにstep barrierを置く。
型、shape、全sourceのstep、global offset、全体の値を3言語で比較した。
ローカル契約テストでは時刻不一致、重複、重なり、穴、容量超過、欠落、byte limitを拒否し、
3軸にoffsetのある8枚の3D tileを順不同に組み立てる。

clean archiveは既存build、`.venv`、生成ログ、未追跡ファイルを含めず `git archive HEAD` から作る。
そのソースでnativeを新規ビルドし、2つの実wheelをsource外のsite directoryへ導入する。
元のeditable packageを使わずにimportと旧Workerのライブラリ探索を検査し、
新規native probe、負strideの借用Fortran、wheel版Python、global assemblyを5 rankで連携させた。

## 再実行

利用可能なシステム・キューを確認してから計算資源を使う。以下は今回の指定例。
controllerはallocation内で1回だけ実行し、`srun`でcontroller自体を複製しない。

```sh
module switch SysB SysA
tssrun -p gr20001a -t 0:10:00 --rsc p=1:t=2:c=2 \
  bash -lc 'cd /path/to/cotocoa; bash tests/verify_build.sh'
sbatch tests/kudpc-final.sbatch
sbatch tests/kudpc-borrow.sbatch
sbatch tests/kudpc-borrow-edges.sbatch
sbatch tests/kudpc-fortran-strides.sbatch
sbatch tests/kudpc-diagnostics.sbatch
sbatch tests/kudpc-clean.sbatch
```

verify_build.shはビルド、CTest、pytest、Python導入、C-only、relocatable installを検査し、
diagnostics scriptに渡す `build/verified-install-prefix` を保存する。
MPI scriptsは `--rsc p=5:t=1:c=1`。clean archiveのnative buildも2並列以下に制限する。
ログ・入力・実行記録はbuild内に残る。サポート範囲を超えるMPI実装や異なるcompilerの
Fortran module互換性は、このテスト結果から推定しない。
