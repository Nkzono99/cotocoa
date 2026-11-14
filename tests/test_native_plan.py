from pathlib import Path
import subprocess
import pytest
from cotocoa.config import load_config
from test_config import config,write

checker=Path(__file__).resolve().parents[1]/'build/tests/plancheck'
pytestmark=pytest.mark.skipif(not checker.exists(),reason='build native plancheck first')

def check(tmp_path,text):
    path=tmp_path/'native.manifest'
    path.write_text(text)
    return subprocess.run([str(checker),str(path)],capture_output=True,text=True)

def test_current_and_corrupt_manifests(tmp_path,config):
    manifest=load_config(write(tmp_path,config)).manifest()
    assert check(tmp_path,manifest).returncode==0
    for text in (manifest+'junk\n',manifest.replace('0 2 2\n','0 9999999999999999999999999 2\n'),
                 manifest.replace('3 5 7\n','-3 5 7\n',1),manifest[:100],
                 manifest.replace('0 2 2\n','0 524289 524289\n')):
        result=check(tmp_path,text)
        assert result.returncode!=0 and 'invalid' in result.stderr

@pytest.mark.parametrize('version',[1,2])
def test_single_route_compatibility(tmp_path,version):
    manifest=f'COTOCOA_MANIFEST {version}\n'+('a'*64)+'\nproducer 2\nconsumer 2\nfield\nfield\nfield\n3 5 7\n6 5 7\n2\nx\ny\nz\nV\ns\n'
    if version==2: manifest+='1 3\n'
    assert check(tmp_path,manifest).returncode==0
    bad=manifest.replace('producer 2','producer 999999999999999999999999999999')
    assert check(tmp_path,bad).returncode!=0
