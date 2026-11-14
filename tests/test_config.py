import copy
import subprocess
import sys

import pytest
import yaml

from cotocoa.config import ConfigError, load_config


@pytest.fixture
def config():
    return {"schema": 1, "programs": {
        "producer": {"command": ["/bin/true"], "ranks": 2},
        "consumer": {"command": ["/bin/true"], "ranks": 2}},
        "connections": [{"name": "potential", "from": "producer.field", "to": "consumer.field",
                         "dtype": "float64", "shape": [3, 5, 7], "global_shape": [6, 5, 7],
                         "axes": ["x", "y", "z"], "units": "V", "time_unit": "s", "buffer_slots": 2}]}


def write(tmp_path, config):
    path = tmp_path / "coupling.yaml"
    path.write_text(yaml.safe_dump(config))
    return path


def test_plan_and_manifest(tmp_path, config):
    plan = load_config(write(tmp_path, config))
    assert plan.ranks == 5
    assert plan.producer_buffer_bytes == 3360
    assert plan.manifest().splitlines()[0] == "COTOCOA_MANIFEST 2"
    assert len(plan.manifest().splitlines()[1]) == 64
    assert plan.manifest() == load_config(plan.config).manifest()


@pytest.mark.parametrize("key,value", [
    ("delivery", "latest"), ("dtype", "float16"), ("buffer_slots", 4095),
    ("buffer_slots", True), ("shape", [0, 5, 7]), ("shape", [3, 5]),
    ("shape", [2**40, 5, 7]), ("axes", ["x", "x", "z"]),
    ("units", "V\ninjected"), ("global_shape", [2, 5, 7]),
    ("from", "missing.field"), ("to", "producer.field"), ("typo", 1),
])
def test_invalid_contract_rejected(tmp_path, config, key, value):
    config["connections"][0][key] = value
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, config))


def test_duplicate_settings_rejected(tmp_path):
    path = tmp_path / "duplicate.yaml"
    path.write_text("schema: 1\nschema: 1\n")
    with pytest.raises(ConfigError, match="duplicate"):
        load_config(path)


def test_missing_executable_and_fanout(tmp_path, config):
    original = copy.deepcopy(config)
    config["programs"]["producer"]["command"] = ["./missing"]
    with pytest.raises(ConfigError, match="executable"):
        load_config(write(tmp_path, config))
    original["connections"] *= 2
    with pytest.raises(ConfigError, match="one connection"):
        load_config(write(tmp_path, original))


def test_import_and_check_do_not_initialize_mpi(tmp_path, config):
    path = write(tmp_path, config)
    code = ("import sys; import cotocoa; from cotocoa.cli import main; "
            f"assert main(['check', {str(path)!r}]) == 0; "
            "assert 'mpi4py.MPI' not in sys.modules")
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_plan_writes_manifest(tmp_path, config):
    from cotocoa.cli import main
    path = write(tmp_path, config)
    out = tmp_path / "compiled.manifest"
    assert main(["plan", str(path), "--output", str(out)]) == 0
    assert out.read_text() == load_config(path).manifest()


def test_executable_symlink_preserves_virtual_environment(tmp_path, config):
    executable = tmp_path / "python"
    executable.symlink_to(sys.executable)
    config["programs"]["producer"]["command"] = ["./python", "analysis.py"]
    plan = load_config(write(tmp_path, config))
    assert plan.programs["producer"]["command"][0] == str(executable)
    assert plan.programs["producer"]["command"][0] != str(executable.resolve())


@pytest.mark.parametrize('dtype,unit,code', [('float64',8,1),('float32',4,2),('int32',4,3)])
@pytest.mark.parametrize('ndim', [1,2,3])
def test_supported_array_schema(tmp_path, config, dtype, unit, code, ndim):
    c = config['connections'][0]
    c['dtype'] = dtype
    c['shape'] = [3,5,7][:ndim]
    c['global_shape'] = [6,5,7][:ndim]
    c['axes'] = ['x','y','z'][:ndim]
    plan = load_config(write(tmp_path,config))
    assert plan.snapshot_bytes == [3,15,105][ndim-1]*unit
    assert plan.manifest().splitlines()[-1] == f'{code} {ndim}'
