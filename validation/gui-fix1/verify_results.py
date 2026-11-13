#!/usr/bin/env python3
"""Recompute public numeric evidence. No ROS, camera, network or third-party imports."""
import csv
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import statistics

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def load(name):
    return json.loads((HERE/name).read_text(encoding='utf-8'))

def near(a,b,tolerance=1e-8):
    assert math.isclose(a,b,rel_tol=tolerance,abs_tol=tolerance),(a,b)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--implementation-root', type=Path,
                        help='Checkout of the tested v0.3.0-startup-init-fix1 source')
    tested_source = parser.parse_args().implementation_root
    summary=load('summary.json')
    manifests = [(ROOT, load('data-manifest.json')['data_files'])]
    if tested_source:
        manifests.append((tested_source, load('implementation-manifest.json')['files']))
    for directory, items in manifests:
        for item in items:
            data = (directory / item['path']).read_bytes()
            assert len(data) == item['bytes'], item['path']
            assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']
    seconds=summary['measurement_seconds']
    with gzip.open(HERE/'arrivals.csv.gz','rt',encoding='ascii',newline='') as stream:
        arrivals=[int(r['t_receive_ns']) for r in csv.DictReader(stream)]
    assert all(0<=x<seconds*1e9 for x in arrivals)
    assert all(b>a for a,b in zip(arrivals,arrivals[1:]))
    measured=summary['topic_metrics']['image_raw']
    assert len(arrivals)==measured['received_frames']==71188
    near(len(arrivals)/seconds,measured['fps_by_window'])
    gaps=[b-a for a,b in zip([0]+arrivals,arrivals+[int(seconds*1e9)])]
    near(max(gaps)/1e6,measured['maximum_gap_ms_including_edges'])
    bins=[0]*(seconds//5)
    for value in arrivals: bins[value//5_000_000_000]+=1
    rates=[x/5 for x in bins]
    near(min(rates),measured['five_second_min_fps'])
    assert sum(x<19 for x in rates)==measured['five_second_windows_below19']==35
    with (HERE/'diagnostics.csv').open(newline='',encoding='utf-8') as stream:
        diagnostics=list(csv.DictReader(stream))
    assert len(diagnostics)==summary['diagnostic_samples']==3600
    assert all(0<=float(r['time_minutes'])<60 for r in diagnostics)
    warnings=[r for r in diagnostics if int(r['diagnostic_level'])==1]
    assert len(warnings)==summary['warnings'][0]['count']==133
    assert all(r['state']=='degraded' for r in warnings)
    with (HERE/'resources.csv').open(newline='',encoding='utf-8') as stream:
        resources=list(csv.DictReader(stream))
    for role in ['capture_node','viewer']:
        rows=[r for r in resources if r['role']==role]
        assert len(rows)==1766
        stats=summary['resources'][role]
        near(statistics.mean(float(r['cpu_percent_one_core100']) for r in rows),stats['cpu_one_core_percent']['mean'])
        near(statistics.mean(float(r['rss_mib']) for r in rows),stats['rss_mib']['mean'])
        near(statistics.mean(float(r['rss_mib']) for r in rows if float(r['time_minutes'])<5),stats['first5min_rss_mean_mib'])
        near(statistics.mean(float(r['rss_mib']) for r in rows if float(r['time_minutes'])>=55),stats['last5min_rss_mean_mib'])
    print(json.dumps({'scope':'OFFLINE_PUBLIC_NUMERIC_RECOMPUTATION_ONLY','passed':True,
        'composite_arrivals':len(arrivals),'diagnostics':len(diagnostics),'rate_WARN_samples':len(warnings),
        'mean_FPS':len(arrivals)/seconds,'minimum_fixed_5s_FPS':min(rates),
        'maximum_receive_gap_ms':max(gaps)/1e6,'resource_rows':len(resources),
        'historical_implementation_verified': tested_source is not None,
        'new_hardware_runs':0},indent=2))

if __name__=='__main__': main()
