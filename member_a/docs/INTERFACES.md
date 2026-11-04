# A / B / C / D 接口约定 v0.1

状态：**A 的接口提案，供组员评审；尚未确认或冻结。** 当前只交付协议、声明与样例，不代表算法已经实现。源依据为根目录 `【F】Group5 Team Project Proposal.pdf`。下文“PDF 要求”来自原文；“v0.1 约定”是为可实现、可联调而补充的决定。冲突应通过共同修订处理，不能各自改一份头文件。

## 1. 范围与交付关系

PDF 第 1 页 §2：只计算加权稀疏乘稠密 `Y = A X`，不做训练、激活、采样或 GPU。

```text
A: canonical edge list ──→ CSR + X ──→ A scalar / B SIMD / C schedules
          │                                              │
          └────────────→ FP64 oracle ──→ correctness ←──── Y
                                      │
D: own buffers + prepare outside timer + time full calls + save results
```

| 交接 | A 提供 | 对方提供 / 遵守 | 首次验收 |
|---|---|---|---|
| A → B | CSR / dense view、合法输入、scalar、oracle、比较器 | SIMD `RowRangeFn`，单线程完整入口；不依赖额外 padding | `f=1,17,65` 和空行、带符号权重通过 |
| A ↔ C | 同一输入和检查器；`audit_chunk_plan` | static / dynamic / split 三个入口，可读 chunk view，明确输出所有权 | 边覆盖一次、无重复 partial slot、split 输出通过 oracle |
| A → D | 生成器、种子规则、degree stats、基线、验证日志字段 | 分配 / 生命周期、计时、内存核算、运行配置和 CSV | 同一输入跑各实现；验证在计时外；完整调用计时 |
| B → C | 由双方实现对接，A 仅定义语义 | `row_range_simd` 同时服务完整行和 chunk | 完整区间与分段区间加和在容差内一致 |

A 不接手 B 的 ISA / SIMD、C 的调度实现或 D 的完整 benchmark / profiling。A 的 C 交叉检查是正式职责，不等于代写 C。

## 2. 数据契约

权威声明：[contracts.hpp](../include/ceg5206/contracts.hpp)。公共命名空间 `ceg5206`，实现分别位于 `a` / `b` / `c`。

### 2.1 CSR 与方向

PDF 要求（第 1 页 §2）：

```text
for row i:
    Y[i,r] = sum(values[e] * X[col_idx[e],r])
             e in [row_ptr[i], row_ptr[i+1])
```

- `row` 是输出 / 聚合目标节点，`col` 是输入特征来源节点，不能转置。
- `row_ptr`：64-bit 无符号偏移；`col_idx`：32-bit 无符号节点 ID；权重、X、Y：FP32。累计使用 FP32；oracle 使用 FP64。
- 行内列 ID 严格递增，不允许重复；列 ID 在 `[0,n)`。
- self-loop 只有显式边才存在。普通 benchmark 图不生成 self-loop；正确性样例必须覆盖显式 self-loop。
- `degree(i)=row_ptr[i+1]-row_ptr[i]`；空行输出全零。
- benchmark 非空行权重为存储后的 FP32 `1/degree(i)`；小型测试允许有限的有界带符号权重。内核不得再除 degree。

v0.1 约定：

| 字段 | 长度 / 约束 |
|---|---|
| `CsrView.n` | `0 <= n <= UINT32_MAX`；这是本版的保守节点数上限 |
| `row_ptr` | `n+1` 个元素，首项 0，单调不减，末项 `m`，每项不超过 `m` |
| `col_idx` / `values` | 恰好各 `m` 个元素；`m=values.size` |
| `Span<T>` | `size` 是元素数；非零长度要求非空、有效的指针 |
| `n=0,m=0` | 合法，`row_ptr=[0]`，其他数组长度 0；仍要求 `f>=1` |
| `n>0,m=0` | 合法，全部空行；输出全部写零 |

所有长度、`n*f`、`C_s*f`、字节数与偏移转换在分配前检查溢出和 `size_t` 可表示性。不把 `uint64_t` 偏移截成 `int`。view 不拥有内存，原始存储的长度及有效性由调用方保证；校验器无法证明任意裸指针实际分配了多少内存。

### 2.2 Dense 与内存所有权

- `DenseConstView` / `DenseView` 分别读 X、写 Y；`ReferenceView` 写 FP64 oracle 结果。
- `values.size == n*f`，连续 row-major，元素位置 `i*f+r`，`f>=1`。v0.1 不提供 stride、padding 或隐式转置。
- 仅保证类型自然对齐；B 若使用向量指令，必须使用安全的未对齐访问或自行处理对齐，尾部不能越界。
- X、CSR 在调用中只读；Y 必须与 X、CSR 和 workspace 不重叠，不支持 in-place。
- D / 调用方拥有 X、Y、reference；`CsrGraph` 拥有 CSR；C 的 `KernelState` 拥有调度描述和 scratch。
- 每次完整调用**覆盖** Y，不依赖原值，也不允许调用方预清零后将其移出计时。允许用 NaN / 非零值预填 Y 检查遗漏写入；因此输入校验不检查旧 Y 的数值有限性。
- X / 权重须有限。非法输入在计时前拒绝；不得静默排序输入、去重或补 self-loop。`build_csr` 是明确的预处理例外：复制并排序边列表，但重复边仍报错。

## 3. 统一 prepare / run 生命周期

v0.1 约定的共同入口：

```cpp
using PrepareFn = PreparedKernel (*)(CsrView, FeatureCount, const RunConfig&);
using AggregationFn = void (*)(CsrView, DenseConstView, DenseView,
                              const RunConfig&, KernelState*);
```

`PreparedKernel` 是可移动、不可复制的对象：`run` 指向完整算子，`state` 为 `unique_ptr<KernelState>`，`workspace` 记录拥有的 workspace 字节数。默认构造的对象不可调用；成功的 prepare 必须返回非空 `run`。基线可以返回空 state 和全零 workspace。

| 标准实现 ID | factory（需对应成员实现） | 线程规则 |
|---|---|---|
| `scalar` | `a::prepare_scalar` | `threads=1`；scalar 编译单元单独禁用自动向量化 |
| `simd` | `b::prepare_simd` | `threads=1` |
| `row_static` | `c::prepare_static` | `threads>=1` |
| `row_dynamic` | `c::prepare_dynamic` | `threads>=1` |
| `split_row` | `c::prepare_split` | `threads>=1` |

调用次序：

1. A 生成 canonical edges、CSR 和两份不同 feature seed 的 X；D 分配 Y 与验证用 reference。
2. A `validate_csr` / `validate_problem` 检查数据；构建时的 shape / overflow / 参数错误直接拒绝。
3. 在计时外调用各成员 factory。C 在这里分配描述符、partials 和 worker 状态。D 单独记录 prepare 时间。A scalar 的 factory 验证 `threads=1`，返回 `scalar_csr`。
4. split factory 返回后，调用 `c::chunk_plan_view(*prepared.state)`，交给 `a::audit_chunk_plan`；此 view 不复制 descriptor，仅用于检查。
5. 在计时外运行 oracle 并验证每个 variant；至少覆盖每个将使用的 X。失配不进入正式结果集。
6. warm-up 后，计时完整 `prepared.run(graph, x, y, cfg, prepared.state.get())`。每次 run 负责本次状态初始化、计算、同步、归并，并在全部输出完成后返回。
7. reference / oracle edge list 可在验证后释放；保留各 X 的验证记录。结束后销毁 prepared，再销毁 graph。

prepare 将状态绑定到**同一份未修改的 CSR 存储、n、f 和全部 RunConfig 字段**。换图、修改图、换 f / 线程数 / tile / chunk 上限须重新 prepare。X、Y 可换地址，但 shape 和不重叠规则必须保持；因此可以交替两份 X。不得并发使用同一个 prepared state；独立并发调用需要独立 state。

RunConfig 中的 32 / 128 仅为初始示例值，不是已调参结果。`threads`、`tile_features`、`max_chunk_edges` 必须为正。A 忽略 tile 和 chunk 的计算效果；B 忽略 chunk 的计算效果。D 在调参后对优化实现使用相同 tile。

错误协议：`validate_*` / `audit_chunk_plan` 返回 `ValidationResult`，`ok()` 是唯一通过标志，`message` 仅用于诊断。无法定位的 row / index 为 `kNoIndex`；CSR index 表示数组索引，chunk 审计 index 表示 descriptor 索引。构造 / prepare / oracle / comparator 参数不合法抛 `std::invalid_argument`，长度运算溢出抛 `std::overflow_error`，内存不足可抛 `std::bad_alloc`。热路径 run 的前提是已验证的合法输入和匹配的 state，不重复扫描验证；不得通过静默回退改变实验 variant。

## 4. A 的接口与数值策略

| API | 行为与验收要点 |
|---|---|
| `validate_csr` | 长度、偏移、最终 m、列界限、行排序、重复边、有限权重；先检查安全访问条件，再解引用 |
| `validate_edges` | 独立检查 n 上限、长度 / 存储有效性、节点界限、重复 `(row,col)` 和有限权重；不要求输入已排序 |
| `validate_problem` | 包含 CSR 校验，再校验 X/Y shape、长度、溢出、非空指针、X 有限和 Y 与输入的重叠 |
| `build_csr` | 接受合法的任意顺序边列表；保留每个 FP32 权重，按 `(row,col)` 排序建 CSR；不改动原边列表、不合并重复 |
| `scalar_csr` | 单线程按 row → edge → feature 顺序计算；每次先清零相应输出；乘加和累计为 FP32；state 为 nullptr |
| `oracle_fp64` | 在计时外检查 edges、X / Yref 的 shape、长度、溢出、不重叠及输入有限性；逐边按列表顺序 scatter 到 FP64 Yref；不读取 CSR、不复用 scalar / B 内核；覆盖输出 |
| `check_close` | 返回逐元素误差报告；不在 timed run 中调用 |
| `generate_graph` / `generate_features` | 按 §7 的确定性规则生成；不做 benchmark 计时 |
| `degree_stats` | 提供实际 degree 的 n、m、min、max、mean、median、p90、p99、empty_rows |
| `audit_chunk_plan` | 审计 §6 的结构约束，不实现调度、不替代运行后的 oracle 比较 |

oracle 的输入来自**构建 CSR 前保存的 canonical edge list**。不能把待验证 CSR 解码成边列表再声称独立。构建器用一份 canonical 数据建立 CSR，oracle 用另一条索引路径遍历原边列表；手写样例的预期值再检查共享输入本身。oracle 权重为 `double(stored_fp32_weight)`，X 为 `double(stored_fp32_feature)`，乘法和累加都用 double；不能在 FP64 中重新计算 `1/degree`，否则比较的是另一组输入。

PDF 第 2 页 Task 1 给 bounded mean case 的起始准则：

```text
abs(actual - reference) <= 1e-5 + 1e-4 * abs(reference)
```

v0.1：默认 atol / rtol 如上。带符号的小型有界测试先用同一准则并保留日志；严重抵消或 hub 失配要分析，不自动放宽容差。所有实验使用相同数值编译策略；本版提议关闭 fast-math、reassociation 与 FMA contraction，baseline 和优化实现保持一致。B/D 确认工具链具体选项；若要测试 FMA，单独修订策略并重跑所有 variant。

比较器约定：

- shape 必须相同、tolerance 有限且非负；非法参数抛错。非有限 actual / reference 一律失败，误差最大值设为 infinity。
- `max_abs_error` 为绝对误差最大值；相对误差分母为 `abs(reference)`，reference 为零时 exact match 记 0，否则 infinity。
- `max_scaled_error = max(abs_error / (atol+rtol*abs(reference)))`；分母 0 时误差 0 记 0，否则 infinity。该值不超过 1 才满足容差。
- `checked=n*f`，`failed` 统计未通过元素；`passed=(failed==0)`。worst 坐标按 scaled error 最大值确定，平局取 row-major 首个。
- `n=0` 时 checked/failed/max errors 均为 0，passed 为 true，worst 坐标为 `kNoIndex`。

## 5. 给 B 的行区间内核

```cpp
void b::row_range_simd(CsrView graph, DenseConstView x, NodeId row,
    RowOffset edge_begin, RowOffset edge_end, Span<float> out,
    FeatureCount tile_features);
```

- 区间为半开 `[edge_begin,edge_end)`，必须包含于该 row 的 CSR 区间；允许空区间，输出全零。
- `out.size == x.f`，独占可写，不能与任何输入重叠；其值应被**覆盖**为该区间贡献和，不能向未知旧值累加。
- full row 使用整个行区间并输出到 Y 的该行；split chunk 使用子区间并输出到对应 partial slot。
- FP32 运算，保持 weight 原值；沿 feature 连续维向量化，处理 f 小于 tile 和不能整除 tile 的尾部，不越界写。
- 该入口不创建线程、不分配、不验证全图，不含 OpenMP 调度。C 可从多个 worker 并发调用它，但各次 out 必须不相交。
- B 的完整 `simd` 算子仍须处理所有空行并覆盖 Y；不是仅暴露一个未清零的局部累加函数。

## 6. 给 C 的 chunk / partial 契约

PDF 第 2 页 Task 4 规定 `c_i=ceil(degree(i)/Te)`，空行 0 chunk，以 exclusive scan 建偏移；unsplit 直接写 Y，split 写私有 partial，再单 owner 归并。

v0.1 将布局固定为：

1. `Te=max_chunk_edges>0`；避免 `degree+Te-1` 溢出，使用 `degree/Te + (degree%Te != 0)`。
2. `row_chunk_ptr` 长度 `n+1`，首项 0，末项为 descriptor 总数；相邻差为 `c_i`。
3. descriptor 按 row 升序、同一行按 edge_begin 升序保存。第 j 块起点 `row_ptr[i]+j*Te`，终点加 `min(Te, row_end-begin)`；非空块大小不超过 Te。整行无缝覆盖一次。
4. `c_i==0`：无 descriptor，完整 run 负责 Y[i,:]=0。
5. `c_i==1`：`partial_slot=kNoIndex`，该块独占写 Y[i,:]。
6. `c_i>1`：该行**所有** chunk（含第一块）各有唯一 partial slot；不允许第一块同时写 Y。slot 按全局 descriptor 顺序连续编号 `0..C_s-1`，`split_chunk_count=C_s`。
7. partial 逻辑布局 `[C_s][f]`，每块占 `f` 个连续 FP32；物理上无 padding。本版若需 padding，应升级布局契约和内存公式。
8. 聚合阶段完成后同步；每个 split row 只由一个归并 owner 写 Y，按该行 descriptor 升序，以 FP32 从零累计 partial。不同 split row 可由不同 worker 并行归并。

完整 run 应覆盖所有会读取的 partial 元素并重置必要状态。若实现依赖清零，清零属于 run；若使用 B 的 overwrite 入口完整覆盖 partial，则无需冗余清零，但必须验证重复调用无旧状态泄漏。C 不得在 timed run 内分配 scratch 或构建 descriptor。根据 chunk 边数进行 worker 分配；具体调度策略由 C 实现并向 D 说明，不能只按 row 数宣称 edge-balanced。

`c::chunk_plan_view` 仅接受 split state，返回的只读 view 有效期到 state 销毁 / 改变为止；其他 state 传入应在计时外报 invalid_argument。A 审计 counts、scan、边界、覆盖、行归属、slot 规则、C_s 和溢出；D/C 的 race diagnostics 检查并发竞争；最后 A 的 oracle 检查数值。这三者不能相互替代。

partials 的逻辑大小精确为 `4*f*C_s` 字节，而非 `4*f*总chunk数`。C 的 workspace 还包括 row_chunk_ptr、descriptor、worker 分区和其他 scratch，不能只报告 partials。

## 7. 给 D 的生成器与复现约定

PDF 第 3 页：固定种子、有向图、恰好 m 条边，无重复邻居、无 self-loop，邻居排序；uniform 与 skewed 使用相同 n、m。skewed 将约半数边分给随机选择的 `ceil(0.01*n)` 个节点，其余给其他节点；这不是 power-law 声明。

v0.1 的确定性细节：

- `uniform`：`q=m/n`、`r=m%n`，节点 ID 前 r 行 degree=q+1，其余 q。正式 grid 的 m 可整除 n，因此全部相等。n=0 仅允许 m=0。
- `skewed`：要求 n>=2，h=`(n+99)/100`；不放回抽 h 个 hub 并按 ID 排序；hub 组分配 `floor(m/2)` 条边，另一组分配其余。各组用商 / 余数均分，余数优先给该组 ID 较小的行。
- 每行 degree 必须不超过 n-1，且 m 不超过 n(n-1)。即使总体容量足够，若 skewed 某组容量不足仍报错，不偷偷削减 m、换家族或调整比例。
- 随机引擎为 `std::mt19937_64(seed)`，graph 和 feature 使用各自独立的引擎实例。为跨标准库复现，不使用 `uniform_*_distribution` 或 `std::shuffle`。
- 无偏整数 `draw(bound)`（bound>0）：用 uint64 的模 2^64 运算令 `threshold=(-bound)%bound`；不断取 engine 输出 u，直到 `u>=threshold`，返回 `u%bound`。
- 不放回抽样使用 Floyd：从 j=population-k 到 population-1，取 t=draw(j+1)，若 t 已选则插入 j，否则插入 t；最后排序。不得通过遍历无序容器决定输出或随机调用顺序。
- skewed 先用 graph 引擎抽 hub；随后两家族都按 row ID 升序抽邻居。每行在 population=n-1 中抽 degree 个索引，索引 t 映射成节点 `t < row ? t : t+1`，跳过自身；再按列升序输出。
- 每行 weight 为 `float(1.0 / double(degree))`，canonical edges 按 `(row,col)` 保存，随后调用独立构建器建立 CSR。
- `GeneratedGraph.timings` 用单调时钟返回三个不重叠阶段的秒数：抽 degree / hub / 邻居并形成 canonical 边记录的 sampling、填入 FP32 权重的 weight_preparation、调用构建器的 csr_construction。各阶段自身的必要分配计入该阶段，验证和额外 oracle snapshot 复制不计入；保留 canonical edges 可以通过移动容器完成。D 可直接获取 setup 分项，不需要用整个 generate_graph 的墙钟时间冒充 CSR 构造时间。
- feature 引擎每个元素取一个输出的高 24 位 u，映射 `float(u) * 2^-23 - 1.0f`，按 row-major 填充，范围 `[-1,1)`，满足 proposal 的 `[-1,1]` 要求。
- `degree_stats` 的 median 为排序后中央值（偶数取两数均值）；p90/p99 使用 nearest-rank，即 rank=ceil(q*n)，1-based。n=0 时所有统计量为 0。

建议 seed manifest（本版工程约定，待 D 确认；不是 PDF 指定值）：

| 用途 | graph_seed | feature_seed_0 | feature_seed_1 |
|---|---:|---:|---:|
| 正式 uniform | 5206001 | 5206101 | 5206102 |
| 正式 skewed | 5206002 | 5206101 | 5206102 |
| tuning uniform | 5206901 | 5206911 | 5206912 |
| tuning skewed | 5206902 | 5206911 | 5206912 |

同家族不同 n/m 可复用 graph seed，但 config 必须完整保存；不同 f 用相同 feature seed 是确定性的前缀复用。tuning seed 与正式 seed 不混用。D 保存 generator_version=`a-v0.1`、contract_version、n/m/f、family、所有 seed、配置和输入摘要；摘要算法由 D 固定并记录。

### 7.1 实验与计时边界

PDF 第 3 页要求，D 负责实现：

- 正式 workload：n ∈ {8192,65536}，m/n ∈ {8,32}，f ∈ {16,64,256}，两个 family，共 24 个；所有 variant 先做单线程比较。
- scaling：n=65536、m/n=32，两个 family、f ∈ {16,256}，p 为 1,2,4,...<=P，并补入 P（去重），P 为使用的物理核数。
- tuning：n=16384、m/n=16、f=64，两份独立种子图，Tf ∈ {16,32,64}、Te ∈ {128,512}；在 min(4,P) 线程按两图完整 split 调用时间的几何均值选一个组合，随后冻结。
- 5 次 warm-up，至少 10 次 timed repetition，报告 median 和 IQR；很短调用可 batch，输出必须可观察。
- per-call 排除分配、生成图/特征、文件 I/O、验证、descriptor 构建；包含输出 / 必要 partial 清理、调度、同步、聚合、归并。完整 run 的墙钟时间是主结果，各阶段计时仅作诊断，不要求相加等于主结果。
- reuse 使用 R ∈ {1,10,100}，交替两份预生成 X：`T(R)=shared_prep+variant_prep+sum(call_time)`；shared prep 包含 CSR 构造、权重准备，variant prep 包含 descriptor 构造。图抽样时间另报；特征生成排除。oracle 边列表复制等验证专用工作不得混入 variant setup。
- 同一算子语义、图、X、数值 flags 和固定线程数下比较。A scalar 只在自身编译单元禁向量化，仍保持普通优化，不能让整个程序退化为未优化编译。
- CPU / ISA / 缓存、编译器版本和 flags、affinity、频率策略由 D 记录；race diagnostics 与 performance build 分开。

### 7.2 内存与结果字段

所有 variant 遵守 PDF 的 1 GiB（2^30 bytes）图 / 特征 / 输出 / workspace 数组预算。D 按实际同时存活的对象计费，不只是理论 payload：

- CSR 逻辑 payload：`8*(n+1) + 8*m`；单份 X 和 Y 分别 `4*n*f`。
- reuse 若同时保留两份 X，则两份都计入：X+Y 合计 `12*n*f`。
- `WorkspaceBytes.descriptors / partials / other` 互不重复：描述和索引、partial buffer、其他 kernel-owned 状态分别报告；总和为该 variant 的峰值保留 workspace。vector 用 capacity 而非仅 size，struct 用实际 sizeof（含 padding）；栈上的小型 SIMD accumulator 不计为数组 allocation，但应在分析中说明。
- FP64 reference、独立 edges 和其他验证临时数组若同时存活也占内存；D 另报 validation peak，正式计时前可释放。不要把 reference 当作性能运行必备数组，也不要隐藏验证阶段超限。
- 内存估算不等于 DRAM traffic。预算不满足时由 D 对所有 variant 一致缩减 grid 并记录原因。

下列是 D 的日志字段约定，不要求 A 代写 benchmark。时间单位秒，字节字段为整数；主 CSV 使用 UTF-8，稳定的 snake_case 名称。

| 记录 | 必须字段 |
|---|---|
| 配置 | contract_version, generator_version, run_id, variant, family, n, m, f, graph_seed, feature_seed_0, feature_seed_1, threads, tile_features, max_chunk_edges |
| 验证 | run_id, variant, feature_seed, passed, checked, failed, atol, rtol, max_abs_error, max_rel_error, max_scaled_error, worst_row, worst_feature |
| 原始调用 | run_id, repetition, batch_calls, feature_seed, call_seconds；batch 时另记录交替起始 seed / 次序，不能把两个 X 的 batch 说成单一 seed |
| 汇总 | run_id, median_seconds, iqr_seconds, scalar_speedup, thread_speedup, nominal_gflops |
| setup / memory | run_id, graph_sampling_seconds, weight_preparation_seconds, csr_construction_seconds, shared_prep_seconds, variant_prep_seconds, graph_bytes, feature_bytes, output_bytes, descriptor_bytes, partial_bytes, other_workspace_bytes, validation_peak_bytes |
| degree | run_id, degree_min, degree_max, degree_mean, degree_median, degree_p90, degree_p99, empty_rows |

无有效 worst 坐标时 CSV 留空，不能把 `kNoIndex` 当真实节点；错误中的 infinity 写 `inf`。`nominal_gflops=2*m*f/(1e9*t)` 不计 merge 的额外操作。scalar_speedup 用同输入的单线程 scalar 时间，thread_speedup 用同 variant 的单线程时间。

## 8. 首次联调样例与确认清单

[contract_v0_1.json](../fixtures/contract_v0_1.json) 是**小型 signed-weight correctness case**，不是 mean benchmark。f=3，Te=2，包含一条空行、显式 self-loop、负权重和一个 split row。JSON `partial_slot:null` 对应 C++ `kNoIndex`。oracle_edges 是直接写出的独立边记录，不要求消费者从 CSR 重建。

各成员首次联调：读取同一份 CSR / X，完整 run 输出必须匹配 expected_y；对 partial 样例验证 row-range 的覆盖语义，对 chunk 样例验证 scan / slot / 归并顺序；将 Y 预填非零并重复调用。这个样例只验证接口，不替代 f=1/17/65、Te 边界、高度 skew 和并发测试。

| 需要确认 | 负责人 | 当前状态 |
|---|---|---|
| 共用本头文件、CSR 方向、数据类型、overwrite 与数值策略 | A/B/C/D | 提案已写，待共同确认 |
| SIMD ISA、可用工具链、FMA policy 与安全尾部 | B/D | 未决定；不阻碍数据接口评审 |
| RowRangeFn、split layout、确定性 merge、prepare/state 生命周期 | B/C | 提案已写，待确认 |
| 生成器细节、seed、日志字段、预算和计时边界 | A/D | 提案已写，待确认 |
| 在支持 C++17 的机器 compile / link 并运行交接样例 | 各实现所有者，D 集成 | 未执行；本仓库当前只有声明 |

通过评审后以同一 commit 中的头文件和文档为准。修改类型、布局、输出语义或随机序列时升级 contract / generator version，并重跑所有成员的交接测试。PDF 第 4 页要求 Week 8 对主实现细节达成一致；此处不假设团队已经批准。
