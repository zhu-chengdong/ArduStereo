#!/usr/bin/env python3
"""Recompute published numeric evidence using stdlib only; no ROS/camera I/O."""
import bisect
import csv
import gzip
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def density(events,t):
    return (bisect.bisect_right(events,t)-bisect.bisect_right(events,t-5_000_000_000))/5


def main():
    frozen=load(ROOT/'validation/implementation-manifest.json')
    for item in frozen['files']:
        raw=(ROOT/item['path']).read_bytes()
        assert len(raw)==item['bytes'] and hashlib.sha256(raw).hexdigest()==item['sha256'],item['path']
    for item in load(ROOT/'validation/data_manifest.json')['files']:
        raw=(ROOT/item['path']).read_bytes()
        assert len(raw)==item['bytes'] and hashlib.sha256(raw).hexdigest()==item['sha256'],item['path']
    results=load(ROOT/'validation/results/hardware_summary.json')
    measured={}
    for name,expected in results['runs'].items():
        with gzip.open(ROOT/('validation/data/'+name+'_timing.csv.gz'),'rt',newline='') as f:
            frames=[(int(r['t_receive_ns']),int(r['t_header_ns'])) for r in csv.DictReader(f)]
        events=[t for t,h in frames]
        assert all(b>a for a,b in zip(events,events[1:]))
        valid=[(t,h) for t,h in frames if 0<=t<expected['valid_seconds']*10**9]
        fps=(len(valid)-1)/((valid[-1][0]-valid[0][0])/1e9)
        assert len(valid)==expected['received_composite'] and abs(fps-expected['callback_fps'])<1e-8
        rates=[density(events,t*10**9) for t in range(1,int(expected['valid_seconds']))]
        assert min(rates)==expected['common1s_grid_min5s_fps']
        assert sum(x<18 for x in rates)==expected['common1s_grid_low_windows']
        diag=ROOT/('validation/data/'+name+'_diagnostics.csv')
        if diag.exists():
            with diag.open(newline='') as f: rows=list(csv.DictReader(f))
            actual={i for i,r in enumerate(rows) if int(r['level'])!=0}
            rebuilt={i for i,r in enumerate(rows) if density(events,int(r['t_query_ns']))<18}
            assert actual==rebuilt and len(actual)==expected['diagnostic_warning_samples']
        measured[name]={'frames':len(valid),'fps':fps,'min5s_fps':min(rates),'low_windows':sum(x<18 for x in rates)}
    replay={}
    for name,expected in load(ROOT/'validation/results/replay_expected.json').items():
        events=[];queries=[]
        with gzip.open(ROOT/('validation/replay_inputs/'+name+'.txt.gz'),'rt') as f:
            for line in f:
                kind,time=line.split()
                (events if kind=='E' else queries).append(int(time))
        values=[]
        for t in queries:
            since=max(0,t-5_000_000_000)
            n=bisect.bisect_right(events,t)-bisect.bisect_right(events,since)
            values.append(n/((t-since)/1e9))
        warnings=sum(v<18 and t>=5_000_000_000 for v,t in zip(values,queries))
        assert len(queries)==expected['queries'] and warnings==expected['warning_samples']
        assert min(values)==expected['minimum_fps']
        replay[name]={'queries':len(queries),'warnings':warnings,'min_fps':min(values)}
    print(json.dumps({'status':'PASS_PUBLISHED_TIMING_EVIDENCE','measured':measured,'numeric_replays':replay},indent=2))


if __name__=='__main__':main()
