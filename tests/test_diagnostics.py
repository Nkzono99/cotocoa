import json
from pathlib import Path
from cotocoa.cli import main,_launch_command

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
