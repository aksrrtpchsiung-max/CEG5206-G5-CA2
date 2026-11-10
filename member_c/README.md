# 第五组项目：成员 C 部分

本目录包含成员 C 负责部分的独立实现，主要功能包括：

- 静态和动态的整行调度；
- 基于排他扫描（exclusive scan）的行分块构造；
- 确定性的按边数负载均衡分配；
- 无数据竞争的 split-row 私有部分结果缓冲区；
- 每个输出行由一个归约负责人按固定顺序执行 partial reduction；
- 图结构、特征、边界情况和调度方式的测试。

独立代码完成了项目步骤 1–6。`src/contract_adapter.cpp` 还按照共享接口契约
v0.1 实现了步骤 7–8 所需的正式入口：

- `ceg5206::c::prepare_static`；
- `ceg5206::c::prepare_dynamic`；
- `ceg5206::c::prepare_split`；
- `ceg5206::c::chunk_plan_view`。

生产适配层直接调用 `ceg5206::b::row_range_simd`，可以在 AArch64 NEON
平台上与成员 B 的正式实现链接。当前提供的 GitHub 快照只包含成员 A 的接口声明和
固定测试样例，不包含 A 的正式实现。因此，本机 x64 测试使用独立的 edge-list
FP64 oracle，并先用成员 A 提供的手算样例校验该 oracle。这个适配器用于当前的集成
验证，不能代替成员 A 后续提供的正式库。

## 接口边界

`CSRGraph` 遵循项目提案中的数据约定：

- 行偏移 `row_ptr` 使用 64 位无符号整数；
- 列编号 `col_idx` 使用 32 位无符号整数；
- 存储权重使用 FP32；
- 特征矩阵和输出矩阵使用行优先的连续 FP32 数组。

独立调度层接受 `ChunkKernel` 函数指针。当前包含的 `scalar_chunk_kernel`
只是占位实现。成员 B 可以提供相同函数签名的 SIMD 内核，而不需要修改整行调度或
分块调度代码。

完整图校验和特征有限值校验位于计时聚合调用之外。`ChunkPlan` 保存了一份
`row_ptr` 副本，防止图的行结构变化后错误地复用旧计划。

共享契约适配层则使用公共头文件中的 `CsrView`、`DenseConstView`、`DenseView`
和 `RunConfig`，并向成员 D 暴露统一的 `prepare/run` 生命周期。

## 构建和测试

需要支持 C++17 的编译器和 CMake。在本目录中执行：

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

测试范围包括：

- 空行和全空图；
- 显式 self-loop；
- 带符号权重；
- 高度数节点；
- 特征宽度 `f=1/17/65`；
- 非法 CSR 输入；
- exclusive scan 偏移和 chunk 边界；
- 计划复用保护；
- 边数统计；
- 不同线程数下的确定性归约；
- 重复调用时的状态清理和旧数据泄漏检查。

当前版本已经在 MSVC 2022、C++17、`/W4`、OpenMP 环境下完成以下验证：

- CMake 从零配置和构建成功；
- CTest 中 2 个测试全部通过；
- 共享契约集成测试连续运行 100 次通过；
- AddressSanitizer 检查通过。

## 共享契约集成

`src/contract_adapter.cpp` 在 `prepare` 阶段完成 descriptor、partial storage
以及确定性的按边数任务分配。计时范围内的 `run` 不分配 C 模块自有的临时存储，
也不会重新构建调度计划。

Split-row 执行过程如下：

1. 空行和拆分行的输出在调用内部初始化；
2. 每个 chunk 调用 B 的行区间内核，并写入互不重叠的输出区域；
3. 所有 chunk 完成后进行同步；
4. 每个拆分行只由一个 owner 写入最终输出；
5. partial 按 descriptor 顺序以 FP32 归并。

`tests/portable_b_shim.cpp` 仅用于当前 Windows x64 工作站的接口验证。成员 B 的
正式生产代码明确要求 AArch64 NEON，因此 shim 生成的所有性能结果都标记为
`portable_x64_validation_only`，不能作为成员 B 的 SIMD 性能结果。

共享契约测试覆盖：

- 成员 A 提供的带符号权重固定样例；
- 精确的 chunk-plan 布局；
- 以 NaN 预填输出后的重复调用；
- 独立 edge-list FP64 检查；
- `f=1/17/65`；
- 包含 256 条边的高度数节点；
- 多种线程调度；
- workspace 字节核算；
- 错误 state 类型拒绝。

## 性能实验

`bench/contract_benchmark.cpp` 提供可复现的集成与性能测试入口。当前 x64
预备实验结果保存在：

- `results/preliminary_x64_contract.csv`：机器可读的原始结果；
- `results/README.md`：实验配置、结果摘要和限制说明。

预备实验使用两个交替的特征数组，每个变体先通过独立 FP64 oracle，再进行 5 次
预热和 10 个计时批次。短调用会自动批处理到约 20 ms，以减少计时器分辨率和调度
抖动的影响。

这些结果只能用于确认调度逻辑和初步行为，不能作为最终报告中的 NEON 性能数据。

## 在真实 B 内核上构建

在具有 AArch64 NEON、Clang 或 GCC 以及 OpenMP 的机器上，可以关闭 x64 shim，
并启用成员 B 的正式实现：

```text
cmake -S . -B build-arm \
  -DC_BUILD_PORTABLE_CONTRACT_TESTS=OFF \
  -DC_BUILD_REAL_B_INTEGRATION=ON
cmake --build build-arm --config Release
ctest --test-dir build-arm -C Release --output-on-failure
```

默认的 B 源文件路径指向共享仓库中的
`member_b/src/simd.cpp`。如目录位置不同，可通过 `CEG5206_B_SOURCE` 指定。

## 仍需团队交接的内容

1. 成员 A 发布正式源码后，用其 `oracle_fp64`、图生成器和校验器替换测试侧适配器。
   当前 GitHub 快照明确说明这些实现尚未提供。
2. 在团队约定的 AArch64 NEON 机器上，将本适配层与
   `member_b/src/simd.cpp` 链接并重新运行测试及性能实验。当前 x64 shim 计时仅供预备
   检查。
3. 成员 A 的生成器和校验器可用后，接入成员 D 的完整实验运行器，执行最终调参、
   24 组主实验、复用实验和正式 profiling。

当前包含的 benchmark 是可复现的集成与 crossover 冒烟实验，不是最终报告所需的
完整实验数据集。
