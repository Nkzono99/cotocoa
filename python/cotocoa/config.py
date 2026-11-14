"""Static validation and manifest compilation; never initializes MPI."""
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


def _mapping(loader,node,deep=False):
    result={}
    for key_node,value_node in node.value:
        key=loader.construct_object(key_node,deep=deep)
        if not isinstance(key,str) or key in result:
            raise ConfigError(f'duplicate or non-string setting: {key!r}')
        result[key]=loader.construct_object(value_node,deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,_mapping)


def _keys(value,allowed,required,where):
    if not isinstance(value,dict):
        raise ConfigError(f'{where} must be a mapping')
    unknown=set(value)-set(allowed)
    missing=set(required)-set(value)
    if unknown or missing:
        raise ConfigError(f'{where}: unknown={sorted(unknown)}, missing={sorted(missing)}')


def _integer(value,where,maximum):
    if type(value) is not int or not 1<=value<=maximum:
        raise ConfigError(f'{where} must be an integer from 1 to {maximum}')
    return value


def _name(value,where):
    if not isinstance(value,str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_-]{0,62}',value):
        raise ConfigError(f'{where} must be a name of at most 63 ASCII characters')
    if value=='__coupler__':
        raise ConfigError('__coupler__ is reserved')
    return value


def _label(value,where):
    if not isinstance(value,str) or not value or len(value.encode('utf-8'))>63:
        raise ConfigError(f'{where} must contain 1 to 63 UTF-8 bytes')
    if any(ord(ch)<32 or ord(ch)==127 for ch in value):
        raise ConfigError(f'{where} cannot contain control characters')
    return value


def _shape(value,where):
    if not isinstance(value,list) or not 1<=len(value)<=3:
        raise ConfigError(f'{where}: expected one to three dimensions')
    return [_integer(x,where,2**63-1) for x in value]


def _bytes(c):
    return math.prod(c['shape'])*{'int32':4,'float32':4,'float64':8}[c['dtype']]


@dataclass(frozen=True)
class Plan:
    config: Path
    producers: tuple
    consumers: tuple
    programs: dict
    connections: tuple
    runtime: dict

    @property
    def producer(self):
        return self.producers[0]

    @property
    def consumer(self):
        return self.consumers[0]

    @property
    def connection(self):
        return self.connections[0]

    @property
    def ranks(self):
        return sum(p['ranks'] for p in self.programs.values())+1

    @property
    def snapshot_bytes(self):
        return sum(_bytes(c) for c in self.connections)

    @property
    def producer_buffer_bytes(self):
        return sum(self.programs[c['from'].split('.')[0]]['ranks']*c['buffer_slots']*_bytes(c)
                   for c in self.connections)

    @property
    def consumer_buffer_bytes(self):
        return sum(p['ranks']*max(_bytes(c) for c in self.connections if c['to'].split('.')[0]==name)
                   for name,p in self.programs.items() if name in self.consumers)

    @property
    def queue_capacity(self):
        return sum(self.programs[c['from'].split('.')[0]]['ranks']*c['buffer_slots'] for c in self.connections)

    def manifest(self):
        canonical=json.dumps({'programs':self.programs,'connections':self.connections,'runtime':self.runtime},
                             sort_keys=True,separators=(',',':'))
        digest=hashlib.sha256(canonical.encode()).hexdigest()
        ids={name:i for i,name in enumerate(self.programs)}
        lines=['COTOCOA_MANIFEST 3',digest,str(self.runtime['timeout_seconds']),
               f'{len(self.programs)} {len(self.connections)}']
        for name,p in self.programs.items():
            role=0 if name in self.producers else 2
            lines.extend([name,f'{role} {p["ranks"]} {p["group_size"]}'])
        for c in self.connections:
            ndim=len(c['shape'])
            source,source_port=c['from'].split('.')
            target,target_port=c['to'].split('.')
            dtype={'float64':1,'float32':2,'int32':3}[c['dtype']]
            kind=0 if c['kind']=='stream' else 1
            delivery=0 if c['delivery']=='all' else 1
            mode=0 if c['mode']=='snapshot' else 1
            lines.extend([c['name'],f'{ids[source]} {ids[target]}',source_port,target_port,
                          f'{dtype} {ndim} {c["buffer_slots"]} {kind} {delivery} {mode}',
                          ' '.join(map(str,c['shape']+[1]*(3-ndim))),
                          ' '.join(map(str,c['global_shape']+[1]*(3-ndim))),
                          *(c['axes']+[f'axis_{d}' for d in range(ndim,3)]),c['units'],c['time_unit']])
        return '\n'.join(lines)+'\n'


def load_config(path,*,check_executables=True):
    path=Path(path).resolve()
    if path.stat().st_size>1048576:
        raise ConfigError('configuration exceeds 1 MiB')
    try:
        data=yaml.load(path.read_text(encoding='utf-8'),Loader=UniqueLoader)
    except yaml.YAMLError as exc:
        raise ConfigError(f'invalid YAML: {exc}') from exc
    _keys(data,{'schema','programs','connections','runtime'},{'schema','programs','connections'},'config')
    if type(data['schema']) is not int or data['schema']!=1:
        raise ConfigError('unsupported schema; expected 1')
    runtime=data.get('runtime',{})
    _keys(runtime,{'timeout_seconds'},set(),'runtime')
    timeout=runtime.get('timeout_seconds',0)
    if type(timeout) not in (int,float) or not math.isfinite(timeout) or timeout<0:
        raise ConfigError('timeout_seconds must be a nonnegative finite number')
    runtime={'timeout_seconds':float(timeout)}
    programs=data['programs']
    if not isinstance(programs,dict) or not 2<=len(programs)<=64:
        raise ConfigError('programs must contain 2 to 64 producer/consumer programs')
    normalized={}
    for name,p in programs.items():
        _name(name,'program')
        _keys(p,{'command','ranks','group_size'},{'command','ranks'},name)
        ranks=_integer(p['ranks'],f'{name}.ranks',1000000)
        group=_integer(p.get('group_size',ranks),f'{name}.group_size',1000000)
        if ranks%group:
            raise ConfigError(f'{name}: ranks must be divisible by group_size')
        command=p['command']
        if not isinstance(command,list) or not command or any(not isinstance(x,str) or not x or '\0' in x for x in command):
            raise ConfigError(f'{name}.command must be a non-empty argv list')
        command=list(command)
        if '/' in command[0]:
            command[0]=os.path.abspath(path.parent/command[0])  # Preserve venv symlinks.
        else:
            executable=shutil.which(command[0])
            if executable: command[0]=executable
            elif check_executables: raise ConfigError(f'{name}: executable not found: {command[0]}')
        if check_executables and (not Path(command[0]).is_file() or not os.access(command[0],os.X_OK)):
            raise ConfigError(f'{name}: command is not executable: {command[0]}')
        normalized[name]={'command':command,'ranks':ranks,'group_size':group}
    raw=data['connections']
    if not isinstance(raw,list) or not 1<=len(raw)<=256:
        raise ConfigError('connections must contain 1 to 256 entries')
    connections=[]; names=set(); producers=set(); consumers=set(); requests={name:0 for name in programs}
    fields={}; destinations=set()
    for entry in raw:
        _keys(entry,{'name','from','to','dtype','shape','axes','global_shape','units','time_unit',
                     'mode','distribution','delivery','buffer_slots','kind'},
                    {'name','from','to','dtype','shape','axes','global_shape'},'connection')
        c=dict(entry)
        _name(c['name'],'connection.name')
        if c['name'] in names: raise ConfigError(f'duplicate connection name: {c["name"]}')
        names.add(c['name'])
        endpoints=[]
        for key in ('from','to'):
            if not isinstance(c[key],str) or c[key].count('.')!=1:
                raise ConfigError(f'connection.{key} must be program.port')
            program,port=c[key].split('.')
            _name(program,key); _name(port,key)
            if program not in programs: raise ConfigError(f'connection.{key}: unknown program {program}')
            endpoints.append(program)
        source,target=endpoints
        if source==target: raise ConfigError('producer and consumer must be different programs')
        producers.add(source); consumers.add(target)
        for key,default,allowed in (('kind','stream',{'stream','task'}),('mode','snapshot',{'snapshot'}),
                                    ('delivery','all',{'all'}),('distribution','shards',{'shards'})):
            value=c.setdefault(key,default)
            if not isinstance(value,str) or value not in allowed:
                raise ConfigError(f'unsupported {key}: {value!r}')
        if not isinstance(c['dtype'],str) or c['dtype'] not in {'int32','float32','float64'}:
            raise ConfigError('dtype must be int32, float32 or float64')
        c['shape']=_shape(c['shape'],'shape'); c['global_shape']=_shape(c['global_shape'],'global_shape')
        ndim=len(c['shape'])
        if len(c['global_shape'])!=ndim or any(g<s for s,g in zip(c['shape'],c['global_shape'])):
            raise ConfigError('global_shape must cover and match the rank of local shape')
        if math.prod(c['shape'])>(2**31-1)//8:
            raise ConfigError('snapshot exceeds the transport element limit')
        if not isinstance(c['axes'],list) or len(c['axes'])!=ndim:
            raise ConfigError('axes must name each logical dimension')
        c['axes']=[_name(x,'axis') for x in c['axes']]
        if len(set(c['axes']))!=ndim: raise ConfigError('axis names must be unique')
        for key in ('units','time_unit'): c[key]=_label(c.get(key,'1'),key)
        c['buffer_slots']=_integer(c.get('buffer_slots',2),'buffer_slots',4094)
        requests[source]+=c['buffer_slots']
        if requests[source]>4094: raise ConfigError(f'{source}: total outgoing buffer_slots exceeds 4094')
        signature=tuple(json.dumps(c[k],sort_keys=True) for k in
                        ('dtype','shape','global_shape','axes','units','time_unit','kind','mode'))
        if c['from'] in fields:
            if fields[c['from']]!=signature or c['kind']=='task':
                raise ConfigError('fan-out requires identical stream schemas; task ports have one destination')
        fields[c['from']]=signature
        if (c['from'],c['to']) in destinations: raise ConfigError('duplicate connection endpoints')
        destinations.add((c['from'],c['to']))
        connections.append(c)
    if producers & consumers: raise ConfigError('programs cannot mix producer/consumer roles or form cycles')
    if producers | consumers != set(programs): raise ConfigError('every program must be connected')
    for name,p in normalized.items():
        if name in producers and p['group_size']!=p['ranks']:
            raise ConfigError('producer programs use all their ranks in one group')
        if name in consumers and p['group_size']!=p['ranks'] and any(
                c['to'].split('.')[0]==name and c['kind']=='stream' for c in connections):
            raise ConfigError('multiple worker groups require task-only incoming connections')
    producers=tuple(sorted(producers)); consumers=tuple(sorted(consumers))
    ordered={name:normalized[name] for name in (*producers,*consumers)}
    plan=Plan(path,producers,consumers,ordered,tuple(connections),runtime)
    if plan.queue_capacity>=2**31-1: raise ConfigError('Coupler request capacity overflows')
    return plan
