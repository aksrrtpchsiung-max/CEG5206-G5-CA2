# Member D: 统一实验入口与交接约定 v0.1

## 当前交付

基于 A 的 `member-a/interface-contract-v0.1` 分支，公共声明继续使用
`member_a/include/ceg5206/contracts.hpp`，未复制、修改或冻结 A 的接口。
本目录是 D 的实现和供全组评审的配套约定，不代表 B/C 已同意，也不包含 A/B/C 的算法实现。

- 一个入口：`scripts/run.py list | plan | run`。
- C++17 runner：统一注册五类算法，分离 prepare、验证、完整调用计时。
- 24 个正式 workload、线程扩展、调参和图复用的配置模板。
- 原始 JSON、五类 CSV、运行 manifest、错误记录及内存统计口径。
- 默认可独立构建；缺失算法显示 unavailable，不能伪造或回退成其他算法。
- 框架单元测试与测试专用适配器；不含真实性能结果。

本次实际检查结果见 [框架验证记录](docs/VALIDATION.md)。

**现在可以：**评审接口、构建框架、检查可用算法、生成实验计划。
**算法交付后：**先接入 A，跑 smoke 验证和计时链路，再逐个接入 B/C。
**后续里程碑：**tuning/reuse 的实际执行、自动选参、短调用 batching、阶段与线程 profiling、完整正确性边界矩阵和最终绘图。
v0.1 对 tuning/reuse 只生成计划，执行时明确报错，不把普通重复计时冒充这些实验。

## 在仓库根目录使用

推荐 CMake（需要本机安装 CMake 和 C++17 编译器）：

```bash
cmake -S member_d -B member_d/build -DCMAKE_BUILD_TYPE=Release
cmake --build member_d/build
ctest --test-dir member_d/build --output-on-failure
```

没有 CMake 时，Clang 可先构建**默认未接算法的入口**：

```bash
mkdir -p member_d/build
clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off -I member_a/include -I member_d/include member_d/src/benchmark.cpp -o member_d/build/benchmark
```

```bash
python3 member_d/scripts/run.py list
python3 member_d/scripts/run.py plan --config member_d/configs/smoke.json --out member_d/out/smoke-plan.json
python3 member_d/scripts/run.py plan --config member_d/configs/main.json --out member_d/out/main-plan.json
```

默认五个算法均为 false，这是当前代码实际状态。main 计划包含
24 workloads × 5 variants = 120 个单线程 case，**没有执行算法或生成测量值**。
smoke 是小规模集成检查；包含 f=1/17/65，但不能替代 A 的带符号权重、空行、自环等固定正确性样例。

## 后续怎样接入真实实现

不要让各成员修改一份私有头文件。按共同确认后的公共版本实现 factory，源文件列表传给 D 的构建即可。

| 标准 ID | factory | 交接方 |
|---|---|---|
| scalar | `a::prepare_scalar` | A |
| simd | `b::prepare_simd` | B |
| row_static | `c::prepare_static` | C |
| row_dynamic | `c::prepare_dynamic` | C |
| split_row | `c::prepare_split` | C |

A 还需提供 generator、validator、FP64 oracle、check_close、degree_stats；C 需提供 chunk_plan_view，A 需提供 audit_chunk_plan。
具体见 [接入约定](docs/HANDOFF.md)。

CMake 接入 A 的示意（替换路径，不能照抄占位路径运行）：

```bash
cmake -S member_d -B member_d/build-a -DD_ENABLE_A=ON '-DD_A_SOURCES=/absolute/path/graph.cpp;/absolute/path/check.cpp' '-DD_SCALAR_SOURCES=/absolute/path/scalar.cpp'
cmake --build member_d/build-a
```

`D_A_SOURCES` 不得包含 scalar 源文件。scalar 单独编译，仅该编译单元禁自动向量化；其他源码保留优化。
B、C 分别使用 `D_ENABLE_B / D_B_SOURCES`、`D_ENABLE_C / D_C_SOURCES`。
C 当前依赖 B 的行区间内核；B/C 的实验均需 A 的输入和检查模块。
OpenMP 实现增加 `-DD_USE_OPENMP=ON`；额外库使用 `D_EXTRA_LIBRARIES`，线程库和 ISA 选项需交接时说明。

## 算法接入后的首次真实联调

1. 复制 `configs/hardware.example.json`，填写实际硬件、编译参数和运行策略。
2. 使用 `list --binary ...` 确认 scalar 可用。
3. 执行以下命令；输出目录必须尚不存在，以防不同批次结果混用。

```bash
python3 member_d/scripts/run.py run --config member_d/configs/smoke.json --binary member_d/build-a/benchmark --metadata member_d/configs/hardware.local.json --out member_d/out/first-real-smoke
```

smoke 产物标记为 `integration_only`。正式 main/scaling 需在实际调参后把所选参数写回配置并标记
`parameter_status: frozen_after_tuning`，同时补齐峰值内存审查。不要只修改标记而跳过调参。
`scaling.example.json` 假设 P=4，必须按实际选用的物理核数修改 P 和线程列表；tuning 使用 min(4,P)。
seed 是 A 提议的待确认工程约定；调参图与正式图的种子已经分开。

## 输出与计时

每个 case 是一个独立进程；在该进程内只 prepare 一次，验证后预热 5 次、计时至少 10 次。
两份预生成 X 按 repetition 的奇偶交替使用。每次计时恰好包住完整 `PreparedKernel.run`；
清零、调度、同步和归并属于 run，不能挪到计时外。输出校验和在每次计时后计算，
会影响下一次调用的缓存状态；本框架所有方案采用相同 warm-start 流程，不声称冷缓存测量。

- `manifest.json`：完整配置、硬件记录、commit、工作区是否有改动、二进制 SHA-256、运行状态。
- `cases/<case_id>.json`：真实结果、输入摘要、两份 X 的验证、每次耗时、统计、内存、degree。
- `calls.csv / validation.csv / summary.csv / setup_memory.csv / degree.csv`：可直接分析的分表。
- `failure.json`：失败的 case、退出码和诊断；运行失败时 manifest 标为 failed，已有结果仅是部分结果。

中位数和 IQR 用排序后 `(N-1)*q` 的线性插值；加速比仅在匹配输入的基线存在时填写。
没有基线时留空。详见 [数据与测量约定](docs/RESULTS.md)。

## 验证框架

```bash
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I member_a/include -I member_d/include member_d/tests/test_measurement.cpp -o member_d/build/test_measurement
member_d/build/test_measurement
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -fno-fast-math -ffp-contract=off -DD_ENABLE_A -DD_TEST_ONLY -I member_a/include -I member_d/include member_d/src/benchmark.cpp member_d/tests/fake_a.cpp -o member_d/build/test_fixture_runner
python3 -m unittest discover -s member_d/tests -v
```

`tests/fake_a.cpp` 只为验证 D 的连接流程，必须显式定义 `D_TEST_ONLY` 才能编译，未加入生产 CMake target。
它不是 A 的算法或 oracle 实现；相关临时数据由测试自动删除，不构成性能证据。
