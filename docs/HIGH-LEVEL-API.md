# CoToCoA 高水準 API 0.3 の設計と契約

対象は `Nkzono99/cotocoa` の独自拡張。設計の基点は
`8f705f8c6aa8e7c51b98f50c211dd360d247191a`。
本書は実装後の契約を示す。導入とコード例は [利用ガイド](HIGH-LEVEL-USAGE.md)、
検証結果は [実装状況](IMPLEMENTATION-STATUS.md) を参照する。

## 目的と構成

Fortran・C・Python の各言語を producer と consumer の両方に使える。
同じ言語同士を含め、全9通りを対象とする。利用者は名前付きポート、配列、
step/時刻/領域の位置と処理を書く。area ID、progid、整数 request header と
通常の Coupler 配送ループは共通の C 層へ移す。

| 役割 | C | Fortran | Python |
| --- | --- | --- | --- |
| セッション | `cotocoa_context` | `ctca_context` | `Context` |
| stream snapshot | `cotocoa_send` | `%send` | `.send` |
| stream borrow | `cotocoa_borrow` | `%borrow` | `.borrow` |
| Task snapshot | `cotocoa_submit` | `%submit` | `.submit` |
| Task borrow | `cotocoa_submit_borrow` | `%submit_borrow` | `.submit(borrow=True)` |
| 受信 | `cotocoa_receive/next_task` | `%receive/next_task` | `.receive/tasks` |
| 完了 | `cotocoa_complete` | message `%complete` | message `.complete` / context manager |
| 完了待ち | ticket `test/wait`、`flush` | ticket `%test/wait`、`%flush` | ticket `.test/wait`、`.flush` |
| 全体の配列 | `cotocoa_assembler_*` | `ctca_assembler` | `Assembler` |

既存の `ctca.h`、Fortran `ctca`、Python `ctca4py.CTCAWorker` は残す。
新 C ABI は `cotocoa.h`、Fortran は `cotocoa` モジュール、Python は `cotocoa` パッケージ。
Python と Fortran は同じ C 層を呼ぶ。標準 Coupler と probe は native 実行ファイル。
check/plan/run/probe/status の CLI は Python で実装するが、manifest を事前生成すれば
C/Fortran 同士の MPI ジョブ内に Python プロセスを含める必要はない。

## 接続と配列

YAML schema 1 を起動前に manifest へ変換する。2〜64 programs、1〜256 connections を
受け付け、各 program の command/ranks/group_size、各接続の名前、from/to、kind、dtype、
shape/global_shape、axes、units/time_unit、mode、distribution、delivery、buffer_slots を検査する。
不明な設定キー、重複キー、未接続 program、循環、曖昧な task fan-out は拒否する。
実行コマンドの相対パスは設定ファイルを基準に解決し、shell 展開しない。

配列は native の int32、float32、float64、固定1〜3次元。
論理添字の順序を軸の順序とし、C/F の記憶配置から独立させる。
転送・受信内部は第一添字が最も速い canonical 配置。dtype、shape、軸、単位を
自動変換しない。C/Python は正負と0の byte stride、Fortran は型・次元ごとの
generic と配列 section を扱う。snapshot は packing し、borrow は descriptor を公開する。

`distribution: shards` は各 producer rank の局所領域を source_rank と offset 付きで渡す。
global_shape は領域の上限であり、配送自体は shard の完全性を保証しない。
全体が必要な consumer は [Assembler](ASSEMBLY.md) で照合する。
step は int64、time は任意の有限な float64。時刻の単位と軸・値の単位は schema の文字列。

同じ producer port を複数 consumer へ接続できる。型・shape・軸・単位・mode・kind は
全宛先で一致させ、各宛先で delivery と送信枠数を選ぶ。
複数 producer programs と複数入力 port も可能。
入力 port が複数ある consumer は receive-any を使い、受信 Message の port/name/source で分岐する。
名前指定 receive は全入力がその port の場合だけ使える。特定 port を待って他の
メッセージを無制限に退避する動作はしない。

## 配送・所有権・Task

[配送と保持の詳細](DELIVERY-AND-SHARING.md) に従う。
`all` は正常な1ジョブ内で各受理メッセージを1回ずつ渡し、stream は接続・source rank ごとの
順序を保つ。rank 間の全順序と外部副作用の exactly-once は保証しない。
`latest` は同じ接続・source の未配送分を破棄し、処理中の値を保持する。
sequence は増加し、中間値が飛ぶ。最後の値は flush/close が待つ。

snapshot の send/submit は戻る前に入力をライブラリへコピーする。
borrow の公開中は利用者が元配列を保持し、完了まで変更しない。
Python は元 ndarray の参照を Context へ保持する。C/Fortran は利用者が寿命を管理する。
borrow ticket の完了は全宛先の処理完了、または latest の未配送破棄を表す。
ticket release は取消しではない。

受信側はローカル snapshot を所有する。consumer group の各 rank に同じ値を配布し、
receive/complete は group collective とする。1 group に未完了 Message は1件。
完了まで送信側の枠または借用登録を保持する。
C の view は Message の release まで有効。Fortran の copy_to と Python の array は
呼び出し側のコピーなので Message/Context の終了後も保持できる。

Task は `kind: task, delivery: all` とし、1 operation に1宛先。
consumer の ranks を group_size で分割して空いた group に依頼する。
group 内の配布と complete は collective。group 間の処理完了順は保証しない。
ticket test/wait は処理の完了を示し、結果値を返す RPC ではない。
複数 group を使う consumer は Task 専用入力とする。
[Task と複数 port](TASKS-AND-PORTS.md) に実行例と詳細を示す。

## 内部実装と容量

snapshot は各接続の登録済みリングに packing する。Coupler は48個の32bit整数からなる
制御 header だけを配送し、consumer root が RMA で読み、group に配布する。
header は protocol 2、magic、manifest digest、接続、source、slot、sequence、step、
time、offset、shape、dtype/dim と、borrow 用の絶対アドレス・stride を持つ。
64bit 値は2語へ分割し、C struct の padding を転送形式に使わない。

borrow は動的 MPI window と attach/detach を使い、受信 root の derived datatype で
stride を解釈する。登録範囲の参照数を管理し、処理中の領域を detach しない。
MPI window の公開領域と完了状態表は明示的に同期する。

送信枠を使い切ったら完了を待つ。producer rank ごとの全出力枠数は4094以下、
Coupler queue は全 producer rank の全枠数から求め、int の上限未満とする。
既存 request 符号化の制約から producer rank 合計は524288以下。
局所 payload は `INT_MAX/8` 要素以下。各入力配列は byte extent の overflow も検査する。
無制限の送信 queue や背景スレッドは設けない。

plan は送信枠、受信配列、Python の追加コピーの保持量を表示する。
borrow の元配列、利用者が保持するコピー、Assembler、MPI の内部メモリは別途必要。
manifest の現行出力は版3で、以前の単一接続の版1・2も native reader で受け付ける。
全 rank は同じ manifest と互換な MPI/data ABI を使い、起動時に照合する。

## ライフサイクルと診断

既存バックエンドの static 状態に合わせ、1 process に1 Context、1回の open/close とする。
MPI 未初期化なら Context が初期化して所有し、close で終了する。
既存シミュレーションや mpi4py が初期化済みなら借用し、close 後もその MPI を使える。
内部 communicator と window はどちらの経路でも解放する。
Contextの通信操作は各processの1つの呼び出しthreadから行う。
import や Python/Fortran の finalizer に collective 終了を任せない。

producer close は flush 後に終了を通知する。全 producer が終了し、配送・処理が
空になったら consumer の受信を終了する。consumer は終端まで受信してから close する。
未完了 Message を持つ close、group 内の受信契約不一致、Python 処理例外は診断付きで abort する。

[起動と診断](DIAGNOSTICS.md) は static check、native probe、明示 launcher、
run watchdog、native progress の待機上限、永続 run/rank 状態の組合せで提供する。
blocking MPI や利用者計算を native timeout で中断できるとは扱わない。
MPI プロセス障害は fail-fast とし、復旧や再配信を行わない。

## この版の対象外

- 同一 program/process の producer と consumer の兼任、双方向・循環連成。
- 同一 process の複数 Context、reopen、任意 communicator の独立セッション。
- AMR、可変 dtype/shape、構造体、複素数、暗黙の単位変換・補間・再分割。
- Task の任意戻り値、result channel、取消し、処理失敗からの再実行。
- MPI 障害からの復旧、永続メッセージ、外部副作用の exactly-once。

これらは初版の公開契約へ含めない。既存の低水準 regarea/readarea/writearea が必要な
用途では、その API を引き続き使える。

## fork の運用

origin は `https://github.com/Nkzono99/cotocoa.git`。
GitHub 上で親が `tnanri/cotocoa` の fork であることを作業開始時に確認した。
fork の既定ブランチは高水準APIを含む `main` とし、従来の `master` も保持する。
この設定はfork内だけの変更であり、本家の既定ブランチや履歴は変更しない。
この checkout は `remote.pushDefault=origin`、作業 branch の pushRemote も origin。
upstream の push URL は `disabled://upstream-read-only` とし、本家への push/PR は行わない。
これらの Git 設定はローカルであり、別の clone へ自動では引き継がれない。
既存の著作権表示と MIT ライセンスを保持する。
