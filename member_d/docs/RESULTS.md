# 实验与结果记录约定 v0.1

## 算法生命周期和计时

生成 canonical edges/CSR/X → 校验 → prepare（单独计时）→ split chunk 审计 →
两份 X 各重复验证 → 释放 oracle edges/reference → warmup → 完整 run 重复计时 → 保存。
计时使用 C++ steady_clock。调用之间观测全部 Y；不假设全部数据驻留缓存。
main/single 用相同 seeds/config 构建等价输入，每个 variant 独立进程可避免同时保留多个 workspace。

主结果不含生成图/特征、分配、验证、文件 I/O 和 prepare；包含 run 内的全部必要初始化、调度、同步与 merge。
graph sampling、weight preparation、CSR construction、variant prepare 分开报告。
shared_prep = weight_preparation + csr_construction；sampling 另报。

## 记录表

所有 CSV 为 UTF-8、snake_case，时间秒、容量字节；缺失坐标/基线写空，非有限相对误差写 inf。
每张表带 case 配置、case_id 与 run_id；run_id 在当前批次内取 case_id，跨批次结合 manifest 区分。
case_id 是规范化配置 SHA-256 的前 16 位；不把它当输入内容摘要。

| 表 | 主要字段 |
|---|---|
| 公共配置 | contract_version, generator_version, phase, variant, family, n, m, f, graph_seed, feature_seed_0/1, threads, tile_features, max_chunk_edges, warmup, repetitions, memory_budget_bytes |
| calls | repetition（从 0 开始）, batch_calls（当前 1）, feature_seed, call_seconds |
| validation | feature_seed, passed, checked, failed, atol, rtol, max_abs_error, max_rel_error, max_scaled_error, worst_row, worst_feature |
| summary | median_seconds, iqr_seconds, scalar_speedup, thread_speedup, nominal_gflops, checksum |
| setup_memory | graph_sampling_seconds, weight_preparation_seconds, csr_construction_seconds, shared_prep_seconds, variant_prep_seconds, graph_bytes, feature_bytes, output_bytes, descriptor_bytes, partial_bytes, other_workspace_bytes, validation_retained_bytes, performance_retained_bytes |
| degree | degree_min/max/mean/median/p90/p99, empty_rows |

scalar_speedup = 同输入 scalar 单线程中位数 / 当前中位数。
thread_speedup = 同输入同 variant 单线程中位数 / 当前中位数。
两者均需匹配 family/n/m/f/seeds/tile/chunk；没有匹配基线留空。
nominal_gflops = 2*m*f/(1e9*t)，不计归并的额外操作。
IQR = Q(.75)-Q(.25)，排序后 index=(N-1)*q，线性插值。

## 内存口径及明确限制

预算不超过 2^30 字节。逻辑下界：CSR=8*(n+1)+8*m；两份 X 加 Y=12*n*f；
partial=4*f*C_s，C_s 只计被拆分行的 chunks。
实际用 vector capacity*sizeof(T) 与成员提供的 workspace 统计，不能用逻辑 size 隐藏 padding 或预留容量。
运行前检查逻辑下界、生成后的容量、prepare 后容量和 reference 容量；参考与边列表释放后再进行性能计时。

**validation_retained_bytes 是已知数组的同时保留量，不是实际峰值。**
公共接口目前不能获取 generator/prepare 内部临时数组的峰值，也不能在 prepare 返回前禁止超额申请。
因此不能把该值命名为 validation_peak_bytes。正式实验前需 A/C 补充峰值依据，
由 D 记录检查方式及结果后才能设置 hardware metadata 的 peak_memory_verified=true。
该字段是人工审查声明，不是此框架已经自动证明了峰值；元数据 notes 需记录证据。
RSS 可能包含运行时/线程栈等，需与项目数组预算区分。allocated bytes 不能解释成 DRAM 流量。

## 实验配置与阶段

- smoke：仅首次联调，不用于最终性能结论；缺失算法或验证失败即停止。
- main：24 workloads，五个版本，各单线程，共 120 cases。
- scaling.example：P=4 的示例；实际使用的物理核数确定后更新 P 和线程列表。
- tuning：独立图种子，6 参数组合×2 图；使用 min(4,P)，后续执行器按完整 split 时间几何均值选参。
- reuse：R=1/10/100 的计划；后续实现共享准备与专属准备加 R 次完整调用，两份 X 交替。

后两者 v0.1 仅展开计划，不能执行。main/scaling 当前可在真实算法接入、参数实际冻结且峰值审查后执行。
完整运行前还需完成固定样例、空图、孤立点、显式 self-loop、signed weights、hub、chunk 边界、尾部和 race 检查。
不把 smoke 的通过当成整个项目正确性验收。

失败写 failure.json，manifest 标 failed，已完成 case 可用于诊断但不代表完整实验通过。
输出目录必须为新目录。没有算法时不创建性能目录；没有使用模拟耗时填充正式结果。
