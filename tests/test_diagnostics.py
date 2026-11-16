import json
from pathlib import Path
from cotocoa.cli import main,_launch_command,_rank_lines,_rank_records

def test_slurm_uses_requested_rank_count_and_quotes(tmp_path):
    command=_launch_command('slurm',[(2,['/path with spaces/program','a; literal'])],tmp_path,2,'run')
    assert command[:4]==['srun','--kill-on-bad-exit=1','-n','2']
    assert "exec '/path with spaces/program' 'a; literal'" in (tmp_path/'run-dispatch.sh').read_text()

def test_status_reads_persistent_rank_records(tmp_path,capsys):
    run=tmp_path/'run'
    run.mkdir()
    (run/'run.json').write_text(json.dumps({'state':'finished','ranks':2}))
    rank={'rank':0,'program':'producer','phase':'closed','step':8,'sent':8,'received':0,'completed':0,'dropped':0,'detail':''}
    (run/'rank-000000.json').write_text(json.dumps(rank))
    assert main(['status',str(tmp_path),'--json'])==0
    value=json.loads(capsys.readouterr().out)
    assert value['state']=='finished' and value['missing_ranks']==[1]
    assert value['rank_status']==[rank]

def test_rank_records_are_fixed_size_and_reread_after_a_torn_read(tmp_path,monkeypatch):
    rank={'rank':1,'program':'consumer','phase':'processing','step':3,'sent':0,'received':3,'completed':2,'dropped':0,'detail':''}
    path=tmp_path/'rank-000001.json'
    path.write_text(json.dumps(rank).ljust(4095)+'\n')  # The native writer pads each record.
    assert _rank_records(tmp_path)==[rank]
    lines,missing=_rank_lines([rank],3)
    assert lines[0]=='rank=1 consumer processing step=3 sent=0 received=3 completed=2 dropped=0'
    assert missing==[0,2] and lines[-1]=='ranks without a status record: [0, 2]'
    reads=iter(['{"rank":1,"prog',json.dumps(rank)])
    monkeypatch.setattr(Path,'read_text',lambda self,*args,**kwargs:next(reads))
    monkeypatch.setattr('cotocoa.cli.time.sleep',lambda seconds:None)
    assert _rank_records(tmp_path)==[rank]
