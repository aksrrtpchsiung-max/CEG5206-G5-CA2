#!/usr/bin/env python3
"""Build the English experimental report from audited campaign data and figures."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from xml.sax.saxutils import escape
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak, KeepTogether


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign',type=Path,required=True)
    parser.add_argument('--figures',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();folder=args.report
    facts=json.loads((folder/'audited_facts.json').read_text())
    hardware=json.loads((args.campaign/'main/manifest.json').read_text())['metadata']
    selection=facts['selected'];tf=int(selection['tile_features']);te=int(selection['max_chunk_edges'])
    def rows(phase,table):
        with (args.campaign/phase/f'{table}.csv').open() as f:return list(csv.DictReader(f))
    data={p:rows(p,'summary') for p in ('main','scaling','reuse','ablation','diagnostics')}
    memory={p:rows(p,'setup_memory') for p in data}
    def pick(phase,v,fam,n,d,f,p=1,r=1):
        return next(x for x in data[phase] if x['variant']==v and x['family']==fam and int(x['n'])==n and int(x['m'])==n*d and int(x['f'])==f and int(x['threads'])==p and int(x.get('reuse_calls',1))==r)
    def ms(x):return float(x)*1000
    def gmean(a):return math.exp(statistics.mean(math.log(x) for x in a))
    core=['scalar','simd','row_static','row_dynamic','split_row'];labels=dict(scalar='Scalar',simd='Tiled NEON',row_static='Static rows',row_dynamic='Dynamic rows',split_row='Split rows',edge_major='Edge major NEON')
    scale=[]
    for fam in ('uniform','skewed'):
        for f in (16,256):
            choices=[x for x in data['scaling'] if x['family']==fam and int(x['f'])==f and x['variant'] in core[2:]]
            best=min(choices,key=lambda x:float(x['median_seconds']))
            scalar=pick('scaling','scalar',fam,65536,32,f)
            scale.append([fam.title(),str(f),labels[best['variant']],best['threads'],f"{ms(best['median_seconds']):.3f}",f"{float(scalar['median_seconds'])/float(best['median_seconds']):.2f}x"])
    # Compare split to the better whole-row schedule at identical p, graph and f.
    split_ratios=[]
    for fam in ('uniform','skewed'):
        for f in (16,256):
            for p in (1,2,4,8,10):
                whole=min(float(pick('scaling',v,fam,65536,32,f,p)['median_seconds']) for v in ('row_static','row_dynamic'))
                split=float(pick('scaling','split_row',fam,65536,32,f,p)['median_seconds'])
                split_ratios.append(dict(family=fam,f=f,threads=p,ratio=whole/split))
    wins=sum(x['ratio']>1 for x in split_ratios)
    tiling=[]
    for x in data['ablation']:
        if x['variant']=='simd':
            edge=pick('ablation','edge_major',x['family'],int(x['n']),int(x['m'])//int(x['n']),int(x['f']))
            tiling.append(float(edge['median_seconds'])/float(x['median_seconds']))
    reuse_table=[]
    for fam in ('uniform','skewed'):
        for v in core:
            p=1 if v in ('scalar','simd') else 4
            x=pick('reuse',v,fam,8192,32,64,p,100)
            reuse_table.append([fam.title(),labels[v],str(p),f"{ms(x['median_seconds']):.3f}",f"{ms(x['reuse_total_seconds']):.2f}"])
    # Layout primitives also produce an editable English Markdown counterpart.
    styles=getSampleStyleSheet()
    styles.add(ParagraphStyle(name='ReportTitle',fontName='Helvetica-Bold',fontSize=25,leading=29,textColor=colors.HexColor('#142e49'),spaceAfter=17))
    styles.add(ParagraphStyle(name='ReportHeading',fontName='Helvetica-Bold',fontSize=17,leading=21,spaceAfter=13,textColor=colors.HexColor('#142e49')))
    styles.add(ParagraphStyle(name='ReportBody',fontName='Helvetica',fontSize=10,leading=14.3,spaceAfter=9))
    styles.add(ParagraphStyle(name='CaptionText',fontName='Helvetica',fontSize=8.4,leading=11,textColor=colors.HexColor('#485768'),spaceAfter=10))
    styles.add(ParagraphStyle(name='CellText',fontName='Helvetica',fontSize=8.2,leading=10.6))
    story=[];md=[]
    def para(text):
        md.append(text+'\n');story.append(Paragraph(escape(text),styles['ReportBody']))
    def page(title):
        if story:story.append(PageBreak())
        md.append('\n## '+title+'\n');story.append(Paragraph(escape(title),styles['ReportHeading']))
    def table(headers,content,widths=None):
        md.append('| '+' | '.join(headers)+' |\n| '+' | '.join(['---']*len(headers))+' |')
        md.extend('| '+' | '.join(str(v) for v in row)+' |' for row in content);md.append('')
        cells=[[Paragraph(escape(str(v)),styles['CellText']) for v in row] for row in [headers]+content]
        t=Table(cells,colWidths=widths,repeatRows=1,hAlign='LEFT')
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#e8eff6')),('VALIGN',(0,0),(-1,-1),'TOP'),('LINEBELOW',(0,0),(-1,0),.6,colors.HexColor('#9eb0c1')),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f5f7fa')]),('LEFTPADDING',(0,0),(-1,-1),6),('RIGHTPADDING',(0,0),(-1,-1),6),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]))
        story.extend([t,Spacer(1,12)])
    def figure(name,caption,width=505):
        p=args.figures/(name+'.png');img=Image(str(p));ratio=img.imageHeight/img.imageWidth
        img.drawWidth=width;img.drawHeight=width*ratio;story.append(img)
        story.append(Paragraph(escape(caption),styles['CaptionText']));md.append(f'![{caption}](../figures/{name}.png)\n')
    story.append(Paragraph('Sparse Feature Aggregation<br/>on Apple M4',styles['ReportTitle']))
    md.append('# Sparse Feature Aggregation on Apple M4\n')
    para('CEG5206 Group 5 Experimental Report')
    para('Native CPU measurements on 10 November 2026')
    para('We evaluate the weighted sparse matrix dense matrix product Y = AX used for mean neighborhood aggregation. We integrate the scalar CSR reference, tiled AArch64 NEON kernel, static and dynamic whole-row schedules, and edge-balanced split-row reduction. The study uses controlled graph families, identical stored FP32 inputs, an independent FP64 oracle, and complete-call timings on an Apple M4 CPU.')
    para(f"Across the 24 single-thread workloads, tiled NEON achieves a geometric-mean {facts['speedups']['simd']['geomean']:.2f}x speedup over scalar, ranging from {facts['speedups']['simd']['minimum']:.2f}x to {facts['speedups']['simd']['maximum']:.2f}x. A separate tuning set selects Tf={tf} and Te={te}. Split-row wins against the better whole-row schedule in {wins} of the 20 scaling comparisons; its benefit depends on scheduling and partial-result costs.")
    table(['Evidence','Result'],[
        ['Native campaign',f"{facts['total_cases']} cases and {facts['total_samples']:,} recorded timing batches"],
        ['Numerical validation',f"{facts['validation_records']} stored reports; {facts['failed_outputs']} failed outputs"],
        ['Largest scaled error',f"{facts['max_scaled_error']:.6f} of the tolerance allowance"],
        ['Largest requested C++ allocation peak',f"{facts['cpp_peak_bytes']/2**20:.2f} MiB"],
        ['Largest process peak RSS',f"{facts['rss_peak_bytes']/2**20:.2f} MiB"]],[220,285])
    para('The measurements support the implemented CPU aggregation operator only. They do not measure GNN training, model accuracy, GPU execution, or performance on other processors. Performance conclusions use native M4 runs; prior emulated ARM checks are not used as speed evidence.')
    page('Problem and implementation')
    para('For output node i and feature r, Y[i,r] is the sum over stored CSR edges e of values[e] times X[col_idx[e],r]. For mean aggregation, each nonempty row stores the FP32 value 1/degree(i). Empty rows produce zero. Self-loops contribute only when explicitly stored. The formal generated graphs exclude self-loops and duplicate neighbors.')
    para('CSR uses 64-bit row offsets, 32-bit column IDs, and FP32 values. X and Y are contiguous row-major FP32 arrays. Production multiplication and accumulation use FP32. The independent oracle starts from the original edge list captured before CSR construction and promotes stored FP32 operands to FP64 before multiplication and accumulation.')
    table(['Variant','Execution'],[
        ['Scalar','Single thread; row then edge then feature loops; automatic vectorization disabled only in scalar.cpp.'],
        ['Tiled NEON',f'Vectorize four contiguous FP32 features at a time; keep a Tf={tf} feature tile live while traversing neighbors; handle tails safely.'],
        ['Static rows','Assign whole rows with OpenMP schedule(static); each row owns its output.'],
        ['Dynamic rows','Assign whole rows with OpenMP schedule(dynamic,1); a dominant row remains indivisible.'],
        ['Split rows',f'Build ceil(degree/Te) chunks and exclusive offsets with Te={te}; assign chunks by edge counts; split chunks write private partials, then one owner merges each row in descriptor order.']],[100,405])
    para('C constructs a deterministic longest-processing-time greedy assignment: sort chunks by descending edge count and assign each to the least-loaded worker bin, breaking ties deterministically. Its preparation is serial; it is not claimed to be a parallel scan implementation. Unsplit rows write directly to Y. Split-row execution has separate initialization, aggregation, and merge parallel regions.')
    para('Course connections include locality and feature tiling from L02 and L03, exclusive offsets from L04, and CSR storage, SIMD, reductions, and graph load balance from L06. The experiment connects these concepts through measured tradeoffs rather than assuming every transformation improves runtime.')
    page('Hardware and experimental method')
    table(['Item','Recorded setting'],[
        ['Processor','Apple M4; 4 performance cores plus 6 efficiency cores; 10 physical/logical cores'],
        ['Memory and OS','24 GiB unified memory; macOS 15.7.7'],
        ['Caches','P cores: 192 KiB L1I, 128 KiB L1D, 16 MiB shared L2; E cores: 128 KiB L1I, 64 KiB L1D, 4 MiB shared L2'],
        ['Compiler','Apple Clang 17.0.0; C++17; CMake 4.4.3; libomp 23.1.1'],
        ['Numerical flags','-O3 -DNDEBUG -fno-fast-math -ffp-contract=off; scalar adds -fno-vectorize -fno-slp-vectorize'],
        ['Thread policy','OMP_DYNAMIC=FALSE; OMP_PROC_BIND=FALSE; OMP_WAIT_POLICY=ACTIVE; KMP_BLOCKTIME=200'],
        ['Frequency and affinity','OS controlled, no frequency lock or hard P/E core pinning; low power mode disabled'],
        ['Power evidence',f"Per-case sources: {facts['power_sources']}; battery percentage range {facts['battery_range']}."]],[115,390])
    para('The formal grid crosses n in {8192,65536}, mean degree in {8,32}, f in {16,64,256}, and uniform/skewed graphs: 24 workloads. Skewed graphs assign half the edges to ceil(0.01n) randomly selected rows and half to the remaining rows. The model is controlled degree skew, not a claimed power law. Random seeds and actual degree statistics are saved.')
    para('Each case is a separate process. Arrays are allocated before timed aggregation. Both X arrays are validated twice in alternating order, with NaN-prefilled Y. Preparation is timed 10 times, destroying the previous state before each trial; its median is reported. After at least five warmups, calibration selects a power-of-two number of call groups targeting 20 ms per batch, capped at 1024 groups. Fifteen recorded batches follow. Very small smoke fixtures may reach the cap before the target.')
    para('Each batch times complete calls, including necessary clearing, scheduling, synchronization, and merging. X alternates continuously within the batch. The final Y is scanned after timing to make outputs observable. Reported per-call latency divides batch time by actual call count. Median and IQR describe the 15 batch averages, not 15 isolated cold calls. Case order is randomized with a saved seed. No allocations, file I/O, or oracle calls are included in the aggregation interval.')
    para('One X spans 0.5 MiB at n=8192 and f=16, and 64 MiB at n=65536 and f=256. Alternating two large X arrays exceeds the reported L2 capacities, so warm starts do not imply whole-input cache residency. This footprint comparison does not measure actual DRAM traffic.')
    page('Correctness and memory evidence')
    para('Native production A+B+C passes all seven A correctness suites, including empty graphs, isolated nodes, signed weights, explicit self-loops, feature tails, chunk boundaries, repeated-state reuse, and high-degree hubs. The same seven suites pass AddressSanitizer and UndefinedBehaviorSanitizer. C passes its two native tests with the real B kernel. B passes optimized, sanitizer, assembly, concurrency, and D handoff checks. D passes ten Python tests and two CTest tests for measurement and allocation accounting.')
    para(f"Across the campaign, the {facts['validation_records']} stored validation records total {facts['checked_outputs']:,} output-element comparisons and report zero failures; this total includes repeated workloads across phases. The largest absolute error is {facts['max_abs_error']:.3e}; the largest scaled error is {facts['max_scaled_error']:.6f}, below the acceptance boundary of 1. The elementwise criterion is abs(actual-reference) <= 1e-5 + 1e-4*abs(reference). Input byte digests agree across implementations for all {facts['input_digest_groups']} matching input groups.")
    figure('08_peak_memory','Figure 1. Maximum memory across measured cases. Requested C++ allocations include temporary containers. RSS also includes allocator, runtime, stacks, and mapped process memory; the two quantities have different meanings.',width=495)
    para('The operator-new tracker records a high-water mark of requested C++ allocation sizes across generation, preparation, validation, and measurement. It includes container capacity and temporary allocations but excludes malloc/mmap allocations made internally by the runtime. Process peak RSS is independently recorded with getrusage. Retained graph, two X arrays, Y, descriptor, partial, and other-workspace bytes are also reported separately. All observed C++ peaks remain within the 1 GiB array budget; no grid reduction was needed.')
    para('ASan/UBSan do not prove race freedom. Native tests exercise concurrent ownership; A also supplied a separate x86 ThreadSanitizer plus Archer review of the production C adapter. This report does not claim a new native M4 ThreadSanitizer result. Production C changes in this branch are compile-guarded diagnostic hooks; normal execution retains the original ownership and synchronization.')
    page('Parameter selection')
    para('Tuning uses separate-seed uniform and skewed graphs with n=16384, mean degree=16, f=64 and four threads. We evaluate Tf in {16,32,64} and Te in {128,512}. The selection score is the geometric mean of the two full-call medians. The selected pair is frozen before the main, scaling, reuse, ablation, and diagnostic phases; test results are not used to choose parameters.')
    figure('01_tuning','Figure 2. Six parameter pairs evaluated on independent tuning graphs. Lower full-call geometric-mean latency is better.')
    scores=rows('tuning','parameter_scores')
    scores.sort(key=lambda x:float(x['geometric_mean_seconds']))
    table(['Tf','Te','Uniform us','Skewed us','Geometric mean us'],[[x['tile_features'],x['max_chunk_edges'],f"{float(x['uniform_seconds'])*1e6:.1f}",f"{float(x['skewed_seconds'])*1e6:.1f}",f"{float(x['geometric_mean_seconds'])*1e6:.1f}"] for x in scores],[45,45,125,125,165])
    para(f'The selected pair is Tf={tf}, Te={te}. The frozen configuration and tuning-manifest SHA-256 are retained. A single selected pair keeps optimized schedules comparable; it does not establish a globally optimal choice for every graph, feature width, processor, or thread count. Small score differences should be interpreted alongside within-run variation.')
    page('Single thread results across all workloads')
    para(f"Tiled NEON has a geometric-mean {facts['speedups']['simd']['geomean']:.2f}x scalar speedup. Static, dynamic, and split-row routes at one thread achieve geometric means of {facts['speedups']['row_static']['geomean']:.2f}x, {facts['speedups']['row_dynamic']['geomean']:.2f}x, and {facts['speedups']['split_row']['geomean']:.2f}x. These routes share the SIMD kernel; these gains should not be attributed to parallelism.")
    figure('02_main_speedups','Figure 3. Every formal workload at one thread. Values compare complete-call medians against the same workload and inputs in A scalar. Uniform and skewed graphs have matched node and edge counts.',width=470)
    page('SIMD and feature tiling attribution')
    para('To distinguish vector arithmetic from tiling, we additionally integrate B\'s edge-major NEON control into the same D runner. It updates Y for each edge without feature tiling, while the tiled kernel holds an output tile live. All controls use actual A-generated inputs, the same FP64 validation, numerical flags, and timing protocol; B\'s earlier M5 probe data are not mixed into this campaign.')
    figure('09_simd_tiling_ablation','Figure 4. Independent ablation at n=65536 and mean degree=32. Both NEON implementations use explicit four-lane vector operations; their difference includes loop order and accumulator reuse.')
    para(f"Across all 24 ablation workloads, tiled NEON is {gmean(tiling):.2f}x faster than edge-major NEON in geometric mean, with ratios from {min(tiling):.2f}x to {max(tiling):.2f}x. A ratio below 1 means tiling is slower for that workload. Holding an output tile live can reduce output load/store activity, but f larger than Tf repeats CSR metadata traversal. Allocated bytes alone do not measure either cache misses or DRAM traffic.")
    figure('03_single_thread_latency','Figure 5. Native single-thread latency at n=65536. Error bars span the 25th to 75th percentiles of the batch-average samples. Both axes use logarithmic spacing.',width=450)
    page('Thread scaling on heterogeneous cores')
    figure('04_thread_scaling','Figure 6. Speedup relative to the same implementation at one thread, for n=65536 and mean degree=32. The dashed line is a linear reference, not an expected bound for the mixed P/E core CPU.')
    table(['Family','f','Fastest parallel route','Threads','Latency ms','Scalar speedup'],scale,[66,27,148,48,92,124])
    para(f'Split-row is faster than the better whole-row route in {wins}/20 matched scaling comparisons; the whole-row-to-split ratios range from {min(x["ratio"] for x in split_ratios):.2f}x to {max(x["ratio"] for x in split_ratios):.2f}x. The comparison holds graph, inputs, f, threads, and numerical policy fixed. These are descriptive median comparisons, not significance tests. No single schedule is assumed to win universally.')
    para('Four requested threads do not guarantee execution on exactly four performance cores. At eight and ten threads, the OS may use a different mix of performance and efficiency cores. Thread migration, bandwidth contention, and active-wait synchronization can limit scaling. The measurements cannot isolate those causes without additional affinity and hardware-counter evidence.')
    page('Worker distribution and degree skew')
    figure('07_worker_edge_distribution','Figure 7. Median maximum-to-mean processed edge count per worker, from the separate diagnostic executable. Every recorded worker-edge vector sums to m.')
    para('Static scheduling assigns contiguous row ranges, so degree variation can cause unequal edge counts. Dynamic scheduling redistributes entire rows; faster workers may intentionally process more edges. C\'s split route assigns chunks to logical worker bins according to edge counts, then executes every bin even when the runtime supplies fewer physical workers.')
    para('The imbalance metric measures edge distribution, not elapsed-time balance. On a heterogeneous CPU, equal edges per worker may still yield unequal completion times, while unequal dynamic edge counts can be appropriate. It is therefore insufficient to explain performance solely from a lower maximum-to-mean edge ratio. Feature access locality, scheduling overhead, synchronization, and merge work also matter.')
    degrees=rows('main','degree');unique={}
    for x in degrees:unique.setdefault((x['family'],x['n'],x['m']),x)
    table(['Family','n','Mean degree','Median','p99','Maximum'],[[k[0].title(),f'{int(k[1]):,}',f'{float(v["degree_mean"]):.0f}',v['degree_median'],v['degree_p99'],v['degree_max']] for k,v in sorted(unique.items())],[90,85,100,70,80,80])
    page('Aggregation and merge diagnostics')
    figure('06_split_phase_breakdown','Figure 8. Split-row diagnostic medians for the same eight cases. Left: overall phases in milliseconds; labels give the sum of phase medians. Right: initialization and merge in microseconds, with each value labeled. T denotes requested threads. The panels use different units. These instrumented measurements are separate from the primary production timings.')
    para('The diagnostic target is built separately with CEG5206_D_DIAGNOSTICS. The production target contains no diagnostic phase timers or worker-edge increments. Each phase timer includes the associated OpenMP region and its end synchronization. Worker counting can perturb cache and scheduling behavior, so phase values explain structure and overhead but are not substituted into the primary speedup results.')
    para('The uniform degree-32 cases have no split rows when Te exceeds 32. They still pay descriptor construction, worker-bin traversal, and phase-launch overhead in the split route, even when partial storage is zero. Skewed cases create private partials for the high-degree rows. Their logical partial size is 4*f*C_s bytes, where C_s counts chunks belonging to split rows only.')
    setup=[]
    for fam in ('uniform','skewed'):
        for f in (16,256):
            x=next(x for x in memory['scaling'] if x['variant']=='split_row' and x['family']==fam and int(x['f'])==f and int(x['threads'])==4)
            setup.append([fam.title(),str(f),f"{ms(x['variant_prep_seconds']):.3f}",f"{int(x['descriptor_bytes'])/2**20:.3f}",f"{int(x['partial_bytes'])/2**20:.3f}"])
    table(['Family','f','Prepare median ms','Descriptors MiB','Partials MiB'],setup,[80,30,135,130,130])
    para('Preparation includes the scan/descriptor layout, sorting, greedy assignment, and partial allocation. The median of ten preparations is reported; graph sampling is separate. The complete per-call measurement includes all initialization and merge costs. The three split phases use separate parallel regions, so better edge balance must compensate for additional execution overhead.')
    page('Graph reuse and setup amortization')
    figure('05_graph_reuse','Figure 9. Shared setup plus variant preparation plus a measured R-call sequence. n=8192, mean degree=32, f=64. Scalar and SIMD use one thread; parallel routes use four.')
    para('For R in {1,10,100}, each measured group contains exactly R full calls alternating X0/X1. A batch can contain several such groups to reach timer resolution; dividing batch time by the number of groups yields the R-call sequence estimate. State is prepared once and reused throughout. We report T(R)=shared setup+median variant preparation+median sequence time. This is a warm-start amortization study, not R independent cold processes.')
    table(['Family','Variant','Threads','Per call ms','Total at R 100 ms'],reuse_table,[75,140,50,100,140])
    para('Shared setup comprises stored-weight preparation and CSR construction; sampling and feature generation are excluded from T(R), matching the interface convention. Sampling is still recorded separately. Shared setup is one observation per case while variant preparation uses a ten-trial median; small setup differences between independently run variants are measurement noise, not algorithmic differences. Reuse amortizes preparation but does not remove split-row per-call synchronization or merging.')
    page('Conclusions reproducibility and contributions')
    para(f"On this M4 campaign, tiled NEON is consistently faster than scalar on the tested grid, with geometric-mean speedup {facts['speedups']['simd']['geomean']:.2f}x. Splitting improves edge-count balance but beats the better whole-row route in {wins}/20 tested scaling comparisons. The appropriate schedule depends on feature width, degree structure, thread count, and the cost of repeated parallel regions and partial reduction. The project demonstrates tradeoffs rather than a predetermined speedup.")
    para('Limits: one processor and one campaign; synthetic controlled graphs; one common tuned pair; no hard affinity or frequency control; heterogeneous cores; warm-start batches with two feature arrays; no native M4 race-detector result; no cache-miss or DRAM-bandwidth counters; and no full GNN or accuracy evaluation. Within-run IQR does not quantify cross-day or cross-machine uncertainty. Power source and charge are retained per case, and any transition is visible in the evidence.')
    para('Reproduction starts from this integrated branch. Run bash member_d/scripts/build_team.sh, then bash member_d/scripts/run_campaign.sh with a fresh output directory. The campaign executes tuning before freezing configurations. Run analyze.py and write_report.py with the campaign, figures, and report paths. See member_d/README.md for complete commands and dependencies. Each phase retains configuration, ordered cases, source commit/patch, binary SHA-256, environment, raw calls, validation, setup, memory, and degree records. evidence_sha256.json indexes the evidence files.')
    table(['Member','Contribution represented in this report'],[
        ['A','CSR validation/build, deterministic input generation, scalar baseline, independent FP64 oracle, chunk audit, correctness suites.'],
        ['B','Tiled NEON and row-range primitives, tail handling, edge-major ablation control, SIMD tests and assembly checks.'],
        ['C','Static/dynamic whole-row scheduling, scan-based chunks, edge-count assignment, private partials and deterministic reduction.'],
        ['D','Native integration, batching/reuse measurement, parameter freeze, allocation tracking, separate diagnostic hooks, campaign execution, input/evidence audits, English figures and report.']],[55,450])
    para('References: [1] Group 5 Team Project Proposal, computation contract and experimental design. [2] CEG5206 lecture topics cited by the proposal: locality/tiling, scan, CSR, SIMD, reduction and graph balance. [3] A implementation 43842aa, B implementation 2c8f04f, C implementation 8a06e1b, and D integration commits recorded in each manifest. [4] Native evidence and validation logs under member_d/evidence/apple-m4. A\'s prior QEMU/TSan records are supporting validation only, not native performance data.')
    pdf=folder/'CEG5206_Group5_Experimental_Report.pdf'
    def footer(canvas,doc):
        canvas.saveState();canvas.setFont('Helvetica',8);canvas.setFillColor(colors.HexColor('#687789'))
        canvas.drawString(44,25,'CEG5206 Group 5 | Native Apple M4 experiments')
        canvas.drawRightString(A4[0]-44,25,str(doc.page));canvas.restoreState()
    doc=SimpleDocTemplate(str(pdf),pagesize=A4,rightMargin=44,leftMargin=44,topMargin=38,bottomMargin=43,title='Sparse Feature Aggregation on Apple M4',author='CEG5206 Group 5',subject='Native correctness and performance evaluation of weighted CSR aggregation')
    doc.build(story,onFirstPage=footer,onLaterPages=footer)
    (folder/'report.md').write_text('\n'.join(md).rstrip()+'\n')
    (folder/'interpretation.json').write_text(json.dumps(dict(split_wins=wins,split_comparisons=20,split_ratios=split_ratios,tiling_geomean=gmean(tiling),tiling_min=min(tiling),tiling_max=max(tiling),best_scaling=scale),indent=2)+'\n')
    print(pdf)


if __name__=='__main__':main()
