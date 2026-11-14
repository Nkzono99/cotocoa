"""Static validation and compilation; no MPI or native-library imports."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil

import yaml


class ConfigError(ValueError):
    pass


class UniqueLoader(yaml.SafeLoader):
    pass


def _mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str) or key in result:
            raise ConfigError(f"duplicate or non-string setting: {key!r}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)


def _keys(value, allowed, required, where):
    if not isinstance(value, dict):
        raise ConfigError(f"{where} must be a mapping")
    unknown = set(value) - set(allowed)
    missing = set(required) - set(value)
    if unknown or missing:
        raise ConfigError(f"{where}: unknown={sorted(unknown)}, missing={sorted(missing)}")


def _integer(value, where, maximum):
    if type(value) is not int or not 1 <= value <= maximum:
        raise ConfigError(f"{where} must be an integer from 1 to {maximum}")
    return value


def _name(value, where):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]{0,62}", value):
        raise ConfigError(f"{where} must be a name of at most 63 ASCII characters")
    if value == "__coupler__":
        raise ConfigError("__coupler__ is reserved")
    return value


def _label(value, where):
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > 63:
        raise ConfigError(f"{where} must contain 1 to 63 UTF-8 bytes")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
        raise ConfigError(f"{where} cannot contain control characters")
    return value


def _shape(value, where):
    if not isinstance(value, list) or len(value) != 3:
        raise ConfigError(f"{where}: this version supports exactly three dimensions")
    return [_integer(x, where, 2**63-1) for x in value]


@dataclass(frozen=True)
class Plan:
    config: Path
    producer: str
    consumer: str
    programs: dict
    connection: dict

    @property
    def ranks(self):
        return sum(p["ranks"] for p in self.programs.values()) + 1

    @property
    def snapshot_bytes(self):
        return math.prod(self.connection["shape"]) * 8

    @property
    def producer_buffer_bytes(self):
        return self.programs[self.producer]["ranks"] * self.connection["buffer_slots"] * self.snapshot_bytes

    def manifest(self):
        c = self.connection
        canonical = json.dumps({"programs": self.programs, "connection": c},
                               sort_keys=True, separators=(",", ":"))
        digest = hashlib.sha256(canonical.encode()).hexdigest()
        lines = ["COTOCOA_MANIFEST 1", digest,
                 f"{self.producer} {self.programs[self.producer]['ranks']}",
                 f"{self.consumer} {self.programs[self.consumer]['ranks']}",
                 c["name"], c["from"].split(".")[1], c["to"].split(".")[1],
                 " ".join(map(str, c["shape"])),
                 " ".join(map(str, c["global_shape"])), str(c["buffer_slots"]),
                 *c["axes"], c["units"], c["time_unit"]]
        return "\n".join(lines) + "\n"


def load_config(path, *, check_executables=True):
    path = Path(path).resolve()
    if path.stat().st_size > 65536:
        raise ConfigError("configuration exceeds 64 KiB")
    try:
        data = yaml.load(path.read_text(encoding="utf-8"), Loader=UniqueLoader)
    except yaml.YAMLError as exc:
        raise ConfigError(f"invalid YAML: {exc}") from exc
    _keys(data, {"schema", "programs", "connections"}, {"schema", "programs", "connections"}, "config")
    if type(data["schema"]) is not int or data["schema"] != 1:
        raise ConfigError("unsupported schema; expected 1")
    programs = data["programs"]
    if not isinstance(programs, dict) or len(programs) != 2:
        raise ConfigError("this version requires one producer and one consumer program")
    normalized = {}
    for name, program in programs.items():
        _name(name, "program")
        _keys(program, {"command", "ranks"}, {"command", "ranks"}, name)
        _integer(program["ranks"], f"{name}.ranks", 1000000)
        command = program["command"]
        if not isinstance(command, list) or not command or any(not isinstance(x, str) or not x or "\0" in x for x in command):
            raise ConfigError(f"{name}.command must be a non-empty argv list")
        command = list(command)
        if "/" in command[0]:
            # Preserve executable symlinks: resolving a venv's python symlink
            # launches its base interpreter and silently loses the environment.
            command[0] = os.path.abspath(path.parent / command[0])
        else:
            executable = shutil.which(command[0])
            if executable:
                command[0] = executable
            elif check_executables:
                raise ConfigError(f"{name}: executable not found: {command[0]}")
        if check_executables and (not Path(command[0]).is_file() or not os.access(command[0], os.X_OK)):
            raise ConfigError(f"{name}: command is not executable: {command[0]}")
        normalized[name] = {"command": command, "ranks": program["ranks"]}
    connections = data["connections"]
    if not isinstance(connections, list) or len(connections) != 1:
        raise ConfigError("this version requires exactly one connection; fan-out and multiplexing are unsupported")
    c = dict(connections[0]) if isinstance(connections[0], dict) else connections[0]
    _keys(c, {"name", "from", "to", "dtype", "shape", "axes", "global_shape", "units",
              "time_unit", "mode", "distribution", "delivery", "buffer_slots"},
             {"name", "from", "to", "dtype", "shape", "axes", "global_shape"}, "connection")
    _name(c["name"], "connection.name")
    endpoints = []
    for key in ("from", "to"):
        if not isinstance(c[key], str) or c[key].count(".") != 1:
            raise ConfigError(f"connection.{key} must be program.port")
        program, port = c[key].split(".")
        _name(program, key)
        _name(port, key)
        if program not in programs:
            raise ConfigError(f"connection.{key}: unknown program {program}")
        endpoints.append(program)
    if endpoints[0] == endpoints[1]:
        raise ConfigError("producer and consumer must be different programs")
    defaults = {"mode": "snapshot", "distribution": "shards", "delivery": "all", "dtype": "float64"}
    for key, expected in defaults.items():
        if c.setdefault(key, expected) != expected:
            raise ConfigError(f"unsupported {key}: {c[key]!r}; this version requires {expected}")
    c["shape"] = _shape(c["shape"], "shape")
    c["global_shape"] = _shape(c["global_shape"], "global_shape")
    if any(g < s for s, g in zip(c["shape"], c["global_shape"])):
        raise ConfigError("global_shape must cover the local shape")
    if math.prod(c["shape"]) > (2**31-1)//8:
        raise ConfigError("snapshot exceeds the current 2 GiB transport limit")
    if not isinstance(c["axes"], list) or len(c["axes"]) != 3:
        raise ConfigError("axes must name three logical axes")
    c["axes"] = [_name(x, "axis") for x in c["axes"]]
    if len(set(c["axes"])) != 3:
        raise ConfigError("axis names must be unique")
    for key in ("units", "time_unit"):
        c[key] = _label(c.get(key, "1"), key)
    c["buffer_slots"] = _integer(c.get("buffer_slots", 2), "buffer_slots", 4094)
    if programs[endpoints[0]]["ranks"]*c["buffer_slots"] > 2**31-1:
        raise ConfigError("Coupler request capacity overflows")
    return Plan(path, *endpoints, normalized, c)
