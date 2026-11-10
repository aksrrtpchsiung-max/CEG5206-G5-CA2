#!/usr/bin/env python3
"""Audit a completed campaign and generate English publication figures."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from collections import Counter
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import TwoSlopeNorm

LABEL = dict(scalar='Scalar', simd='Tiled NEON', row_static='Static rows', row_dynamic='Dynamic rows', split_row='Split rows', edge_major='Edge-major NEON')
COLOR = dict(scalar='#68717d', simd='#2364aa', row_static='#7955a1', row_dynamic='#168a7b', split_row='#d17b16', edge_major='#bf526a')
CORE = ['scalar', 'simd', 'row_static', 'row_dynamic', 'split_row']
PARALLEL = CORE[2:]
plt.rcParams.update({'font.family':'DejaVu Sans', 'font.size':10, 'axes.spines.top':False, 'axes.spines.right':False,
                     'axes.titlesize':11, 'axes.labelsize':10, 'figure.dpi':140, 'savefig.dpi':220})


def read_csv(path):
    with path.open() as f:
        return list(csv.DictReader(f))


def geometric(values):
    return math.exp(statistics.mean(math.log(float(x)) for x in values))


def generate(campaign, figures, report):
    phases = ['smoke','tuning','main','scaling','reuse','ablation','diagnostics']
    tables, records, all_records, counters = {}, {}, [], {}
    for phase in phases:
        folder = campaign / phase
        manifest = json.loads((folder/'manifest.json').read_text())
        if manifest['status'] != 'completed':
            raise ValueError(f'Incomplete phase: {phase}')
        tables[phase] = read_csv(folder/'summary.csv')
        cfgs = {c['case_id']:c for c in manifest['cases']}
        records[phase] = []
        for path in sorted((folder/'cases').glob('*.json')):
            result = json.loads(path.read_text()); case=cfgs[path.stem]
            if len(result['calls']) != case['repetitions'] or len(result['validation']) != 2:
                raise ValueError('Missing observations')
            if not all(v['passed'] and v['failed']==0 for v in result['validation']):
                raise ValueError('Failed validation')
            if result['setup_memory']['cpp_allocation_peak_bytes'] > case['memory_budget_bytes']:
                raise ValueError('Array budget exceeded')
            for row in result['calls']:
                if not math.isfinite(row['call_seconds']) or row['call_seconds'] <= 0:
                    raise ValueError('Invalid timing')
            item=dict(case=case, result=result)
            records[phase].append(item); all_records.append(item)
        if len(records[phase]) != manifest['case_count']:
            raise ValueError('Missing cases')
        counters[phase]=dict(cases=len(records[phase]), samples=sum(len(r['result']['calls']) for r in records[phase]))
    # Same seeds and shapes must actually generate identical input bytes, across variants and phases.
    digests = {}
    for item in all_records:
        c, r=item['case'],item['result']
        key=tuple(c[k] for k in ('family','n','m','f','graph_seed','feature_seed_0','feature_seed_1'))
        if key in digests and digests[key] != r['input_digest']:
            raise ValueError('Input digest mismatch between implementations')
        digests[key]=r['input_digest']
    figures.mkdir(parents=True, exist_ok=True); report.mkdir(parents=True, exist_ok=True)
    def save(fig, name):
        fig.savefig(figures/f'{name}.png', bbox_inches='tight', facecolor='white')
        fig.savefig(figures/f'{name}.svg', bbox_inches='tight', facecolor='white')
        plt.close(fig)
    def pick(phase, variant, family, n, degree, f, threads=1, reuse=1):
        return next(item for item in records[phase] if item['case']['variant']==variant and item['case']['family']==family
                    and item['case']['n']==n and item['case']['m']==n*degree and item['case']['f']==f
                    and item['case']['threads']==threads and item['case'].get('reuse_calls',1)==reuse)
    def latency(item): return item['result']['summary']['median_seconds']
    # 1: parameter tuning.
    scores=read_csv(campaign/'tuning/parameter_scores.csv')
    matrix=np.array([[float(next(s for s in scores if int(s['tile_features'])==tf and int(s['max_chunk_edges'])==te)['geometric_mean_seconds'])*1e6 for tf in (16,32,64)] for te in (128,512)])
    fig,ax=plt.subplots(figsize=(7.1,2.7),layout='constrained')
    im=ax.imshow(matrix,cmap='Blues'); ax.set_xticks(range(3),['16','32','64']); ax.set_yticks(range(2),['128','512'])
    ax.set_xlabel('Feature tile width Tf');ax.set_ylabel('Maximum chunk edges Te')
    best=np.unravel_index(np.argmin(matrix), matrix.shape)
    for (i,j),x in np.ndenumerate(matrix): ax.text(j,i,f'{x:.1f} us'+('\nSelected' if (i,j)==best else ''),ha='center',va='center',color='white' if x>matrix.mean() else '#172a3a')
    fig.colorbar(im,ax=ax,label='Full-call latency (us)')
    save(fig,'01_tuning')
    # 2: all main workloads.
    work=list(__import__('itertools').product(('uniform','skewed'), (8192,65536),(8,32),(16,64,256)))
    speed=np.array([[latency(pick('main','scalar',*w))/latency(pick('main',v,*w)) for v in CORE[1:]] for w in work])
    fig,ax=plt.subplots(figsize=(8.1,8.1),layout='constrained')
    im=ax.imshow(speed,aspect='auto',cmap='RdBu',norm=TwoSlopeNorm(vmin=min(.5,float(speed.min())*.95),vcenter=1,vmax=max(1.1,float(speed.max()))))
    ax.set_xticks(range(4),[LABEL[v] for v in CORE[1:]])
    ax.set_yticks(range(24),[f'{fam.title()}  n={n:,}  d={d}  f={f}' for fam,n,d,f in work],fontsize=8)
    for (i,j),x in np.ndenumerate(speed):ax.text(j,i,f'{x:.2f}x',ha='center',va='center',fontsize=8,color='white' if x>3.8 else '#102330')
    fig.colorbar(im,ax=ax,label='Speedup over scalar, one thread')
    save(fig,'02_main_speedups')
    # 3: actual single-thread latencies with sample quartiles.
    fig,axs=plt.subplots(2,2,figsize=(9,5.6),layout='constrained')
    for ax,(fam,d) in zip(axs.flat,__import__('itertools').product(('uniform','skewed'),(8,32))):
        for v in CORE:
            rows=[pick('main',v,fam,65536,d,f) for f in (16,64,256)]
            med=np.array([latency(x)*1e3 for x in rows]);lo=[];hi=[]
            for x,m in zip(rows,med):
                q=np.quantile([c['call_seconds'] for c in x['result']['calls']],[.25,.75])*1e3
                lo.append(max(0,m-q[0]));hi.append(max(0,q[1]-m))
            ax.errorbar([16,64,256],med,yerr=[lo,hi],marker='o',label=LABEL[v],color=COLOR[v],capsize=2,linewidth=1.4)
        ax.set_title(f'{fam.title()}, n=65,536, mean degree={d}');ax.set_xscale('log',base=4);ax.set_xticks([16,64,256],['16','64','256']);ax.set_yscale('log');ax.set_xlabel('Feature width f');ax.set_ylabel('Full-call latency (ms)');ax.grid(alpha=.2)
    handles,labels=axs.flat[0].get_legend_handles_labels();fig.legend(handles,labels,loc='outside lower center',ncol=5,fontsize=8)
    save(fig,'03_single_thread_latency')
    # 4: scaling, relative to each variant's own one-thread time.
    fig,axs=plt.subplots(2,2,figsize=(9,5.6),layout='constrained')
    for ax,(fam,f) in zip(axs.flat,__import__('itertools').product(('uniform','skewed'),(16,256))):
        for v in PARALLEL:
            values=[latency(pick('scaling',v,fam,65536,32,f,p)) for p in (1,2,4,8,10)]
            ax.plot([1,2,4,8,10],[values[0]/x for x in values],'-o',color=COLOR[v],label=LABEL[v])
        ax.plot([1,10],[1,10],'--',color='#c0c5cb',label='Linear reference');ax.set_title(f'{fam.title()}, f={f}');ax.set_xticks([1,2,4,8,10]);ax.set_xlabel('Requested threads');ax.set_ylabel('Speedup over own one-thread time');ax.grid(alpha=.2)
    handles,labels=axs.flat[0].get_legend_handles_labels();fig.legend(handles,labels,loc='outside lower center',ncol=4,fontsize=8)
    save(fig,'04_thread_scaling')
    # 5: reuse total includes graph setup and variant setup.
    fig,axs=plt.subplots(1,2,figsize=(9,3.5),layout='constrained')
    for ax,fam in zip(axs,('uniform','skewed')):
        for v in CORE:
            p=1 if v in ('scalar','simd') else 4
            vals=[pick('reuse',v,fam,8192,32,64,p,r)['result']['summary']['reuse_total_seconds']*1e3 for r in (1,10,100)]
            ax.plot([1,10,100],vals,'-o',label=f'{LABEL[v]} ({p}T)',color=COLOR[v])
        ax.set_xscale('log');ax.set_yscale('log');ax.set_xticks([1,10,100],['1','10','100']);ax.set_title(fam.title());ax.set_xlabel('Calls on one graph R');ax.set_ylabel('Setup plus R-call sequence (ms)');ax.grid(alpha=.2)
    handles,labels=axs[0].get_legend_handles_labels();fig.legend(handles,labels,loc='outside lower center',ncol=3,fontsize=8)
    save(fig,'05_graph_reuse')
    # 6: phase instrumentation of actual C source, separate from production timings.
    rows=[pick('diagnostics','split_row',fam,65536,32,f,p) for fam,f,p in __import__('itertools').product(('uniform','skewed'),(16,256),(4,10))]
    # Separate units reveal small phases without enlarging their share of the total.
    fig,(overall,detail)=plt.subplots(1,2,figsize=(10.5,4.8),sharey=True,
                                    gridspec_kw={'width_ratios':[1,1.05]},layout='constrained')
    positions=np.arange(len(rows));left=np.zeros(len(rows))
    phase_values={field:np.array([statistics.median(z[field] for z in r['result']['diagnostics']) for r in rows])
                  for field in ('initialization_seconds','aggregation_seconds','merge_seconds')}
    phase_styles=[('initialization_seconds','Initialization','#7d8998'),
                  ('aggregation_seconds','Aggregation','#2364aa'),
                  ('merge_seconds','Merge','#d17b16')]
    for field,label,color in phase_styles:
        values=phase_values[field]*1e3
        overall.barh(positions,values,left=left,height=.62,label=label,color=color)
        left+=values
    for pos,total in zip(positions,left):
        overall.text(total+.65,pos,f'{total:.2f}',va='center',fontsize=10,color='#263647')
    overall.set_xlim(0,left.max()*1.18)
    overall.set_yticks(positions,[f"{x['case']['family'].title()} | f={x['case']['f']} | {x['case']['threads']}T" for x in rows],fontsize=10)
    overall.invert_yaxis()
    overall.set_title('(a) Overall phase medians',fontsize=12)
    overall.set_xlabel('Phase latency (ms)',fontsize=11)
    offsets={'initialization_seconds':-.17,'merge_seconds':.17}
    for field,label,color in (phase_styles[0],phase_styles[2]):
        values=phase_values[field]*1e6
        detail.barh(positions+offsets[field],values,height=.29,label=label,color=color)
        for pos,value in zip(positions+offsets[field],values):
            detail.text(value+2,pos,f'{value:.1f}',va='center',fontsize=10,color='#263647')
    detail.set_xlim(0,max(phase_values['initialization_seconds'].max(),phase_values['merge_seconds'].max())*1e6*1.23)
    detail.set_title('(b) Initialization and merge',fontsize=12)
    detail.set_xlabel('Phase latency (us)',fontsize=11)
    for ax in (overall,detail):
        ax.grid(axis='x',alpha=.2)
        ax.set_axisbelow(True)
        ax.tick_params(axis='y',length=0)
    handles,labels=overall.get_legend_handles_labels()
    fig.legend(handles,labels,loc='outside lower center',ncol=3,fontsize=10,frameon=False)
    save(fig,'06_split_phase_breakdown')
    # 7: edge imbalance measures distribution, not time imbalance on heterogeneous cores.
    fig,axs=plt.subplots(2,2,figsize=(9,5.3),layout='constrained')
    imbalance_rows=[]
    for ax,(fam,f) in zip(axs.flat,__import__('itertools').product(('uniform','skewed'),(16,256))):
        for index,v in enumerate(PARALLEL):
            vals=[]
            for p in (4,10):
                row=pick('diagnostics',v,fam,65536,32,f,p)
                samples=[max(z['worker_edges'])/(sum(z['worker_edges'])/p) for z in row['result']['diagnostics']]
                ratio=statistics.median(samples);vals.append(ratio)
                imbalance_rows.append(dict(family=fam,f=f,threads=p,variant=v,median_max_to_mean_edges=ratio))
                if any(sum(z['worker_edges'])!=row['case']['m'] for z in row['result']['diagnostics']):raise ValueError('Diagnostic edge coverage mismatch')
            ax.bar(np.arange(2)+(index-1)*.24,vals,width=.24,label=LABEL[v],color=COLOR[v])
        ax.axhline(1,color='#777',linestyle='--',linewidth=.8);ax.set_xticks(range(2),['4','10']);ax.set_title(f'{fam.title()}, f={f}');ax.set_xlabel('Requested threads');ax.set_ylabel('Max / mean worker edges');ax.grid(axis='y',alpha=.2)
    h,l=axs.flat[0].get_legend_handles_labels();fig.legend(h,l,loc='outside lower center',ncol=3,fontsize=8);save(fig,'07_worker_edge_distribution')
    with (report/'worker_imbalance.csv').open('w',newline='') as fp:
        w=csv.DictWriter(fp,fieldnames=list(imbalance_rows[0]));w.writeheader();w.writerows(imbalance_rows)
    # 8: peak memory across all measured cases.
    fig,ax=plt.subplots(figsize=(8,3.6),layout='constrained')
    for offset,field,label,color in [(-.18,'cpp_allocation_peak_bytes','C++ requested allocation peak','#2364aa'),(.18,'process_peak_rss_bytes','Process peak RSS','#d17b16')]:
        vals=[max(x['result']['setup_memory'][field] for x in all_records if x['case']['variant']==v)/2**20 for v in CORE]
        ax.bar(np.arange(5)+offset,vals,width=.36,label=label,color=color)
    ax.axhline(1024,color='#9a3b3b',linestyle='--',label='1 GiB array budget (reference)');ax.set_ylim(0,1100);ax.set_xticks(range(5),[LABEL[v] for v in CORE]);ax.set_ylabel('Peak memory (MiB)');ax.legend(fontsize=8,ncol=2);ax.grid(axis='y',alpha=.2);save(fig,'08_peak_memory')
    # 9: separate SIMD arithmetic and feature tiling using B's explicit edge-major control.
    fig,axs=plt.subplots(1,2,figsize=(9,3.6),layout='constrained')
    for ax,fam in zip(axs,('uniform','skewed')):
        for v in ('edge_major','simd'):
            vals=[latency(pick('ablation','scalar',fam,65536,32,f))/latency(pick('ablation',v,fam,65536,32,f)) for f in (16,64,256)]
            ax.plot([16,64,256],vals,'-o',color=COLOR[v],label=LABEL[v])
        ax.axhline(1,color='#aaa',linestyle='--');ax.set_xscale('log',base=4);ax.set_xticks([16,64,256],['16','64','256']);ax.set_title(fam.title());ax.set_xlabel('Feature width f');ax.set_ylabel('Speedup over scalar');ax.grid(alpha=.2)
    h,l=axs[0].get_legend_handles_labels();fig.legend(h,l,loc='outside lower center',ncol=2,fontsize=9);save(fig,'09_simd_tiling_ablation')
    # Audited facts for report generation.
    validation=[v for x in all_records for v in x['result']['validation']]
    speedups={v:[latency(pick('main','scalar',*w))/latency(pick('main',v,*w)) for w in work] for v in CORE[1:]}
    comparisons=[]
    for fam,f,p in __import__('itertools').product(('uniform','skewed'),(16,256),(1,2,4,8,10)):
        for v in PARALLEL:
            row=pick('scaling',v,fam,65536,32,f,p)
            comparisons.append(dict(family=fam,f=f,threads=p,variant=v,median_ms=latency(row)*1e3,
                                    own_thread_speedup=latency(pick('scaling',v,fam,65536,32,f,1))/latency(row)))
    with (report/'scaling_results.csv').open('w',newline='') as fp:
        w=csv.DictWriter(fp,fieldnames=list(comparisons[0]));w.writeheader();w.writerows(comparisons)
    power=Counter(x['result'].get('power',{}).get('source','unknown') for x in all_records)
    percent=[x['result'].get('power',{}).get('percent') for x in all_records];percent=[v for v in percent if v is not None]
    facts=dict(phases=counters,total_cases=len(all_records),total_samples=sum(v['samples'] for v in counters.values()),
               validation_records=len(validation),checked_outputs=sum(v['checked'] for v in validation),failed_outputs=sum(v['failed'] for v in validation),
               max_abs_error=max(v['max_abs_error'] for v in validation),max_scaled_error=max(v['max_scaled_error'] for v in validation),
               cpp_peak_bytes=max(x['result']['setup_memory']['cpp_allocation_peak_bytes'] for x in all_records),
               rss_peak_bytes=max(x['result']['setup_memory']['process_peak_rss_bytes'] for x in all_records),
               speedups={v:dict(geomean=geometric(a),minimum=min(a),maximum=max(a)) for v,a in speedups.items()},
               scaling=comparisons,worker_imbalance=imbalance_rows,power_sources=dict(power),battery_range=[min(percent),max(percent)] if percent else None,
               selected=json.loads((campaign/'tuning/parameter_scores.csv').read_text()) if False else min(scores,key=lambda x:float(x['geometric_mean_seconds'])),
               input_digest_groups=len(digests))
    (report/'audited_facts.json').write_text(json.dumps(facts,indent=2)+'\n')
    checksums={str(p.relative_to(campaign)):hashlib.sha256(p.read_bytes()).hexdigest() for p in campaign.rglob('*') if p.is_file()}
    (report/'evidence_sha256.json').write_text(json.dumps(checksums,indent=2)+'\n')
    print(json.dumps({k:v for k,v in facts.items() if k not in ('scaling','worker_imbalance')},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);p.add_argument('--figures',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();generate(a.campaign,a.figures,a.report)
