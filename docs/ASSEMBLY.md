# step ごとの領域の組み立て

Assembler は1つの `kind: stream, delivery: all` 接続の shard をローカルで集める。
通信と独立しているため、consumer group の各 rank が同じ操作を行える。
各 source rank は step を単調増加で送る。同じ step の全 source が揃い、
global_shape を隙間・重なりなく埋めたら1枚のフレームを返す。
shape、軸、単位、送信元、時刻の有無と値を照合し、異なる step を混ぜない。
時刻は完全一致で比較し、自動の補間や許容誤差を設けない。

```python
from cotocoa import Assembler

with Assembler(max_pending_steps=2, max_frame_bytes=256*1024*1024) as assembler:
    for message in ctx.receive("field"):
        with message:
            frame = assembler.push(message)
            if frame is not None:
                analyze(frame.array, step=frame.step)
```

push はコピーだけを行い、入力 Message を完了させない。例の `with message` の終了が
送信側への完了通知となる。完成した Frame は通信上の完了通知を必要とせず、
Python では NumPy が配列を所有する。
入力の完了は shard の取り込み完了を意味し、全フレームの解析完了とは別である。

```fortran
type(ctca_assembler) :: assembler
type(ctca_message) :: message, frame
logical :: ready
call assembler%open(2,268435456_c_int64_t)
! receive ループ内:
call assembler%push(message,frame,ready)
call message%complete()
if (ready) then
  call frame%copy_to(global_field)
  call frame%release()
end if
! receive 終端後:
call assembler%finish()
call assembler%close()
```

C は `cotocoa_assembler_create/push/finish/release` を使う。
push の frame 出力が NULL 以外なら完成。`cotocoa_message_view/metadata` で読み、
`cotocoa_message_release` で解放する。完成 frame に complete を呼ばない。
Fortran の Context/Message/Assembler は所有権を持つため intrinsic assignment で複製しない。

max_pending_steps は1〜1024。配列の保持量は最大で
`max_pending_steps * max_frame_bytes` に source rank ごとの追跡情報を加えた量となる。
容量超過、重複した source/step、後退した step、重なり、全 source 到着後の穴を
説明付きで拒否する。finish は未完了 step と不足 source 数を報告する。
close/release はローカル領域を破棄する操作であり、正常終了時はその前に finish する。

source 間の進行差が容量を超える場合は、送信側の flush と producer communicator の
barrier などで進行を揃える。組み立て機能自体は送信元を同期しない。
latest と複数 worker group の Task は全 source の受信を保証しないため対象外。
