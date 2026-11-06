# 成员 B：特征分块与 SIMD 聚合

B 负责沿特征维度对加权 CSR 聚合进行分块和向量化，并处理最后不足一个向量的特征。每条边提供一个权重，内核用它乘以邻居节点的特征，再将结果累加到当前节点的输出行。

本实现使用 A 的公共头文件 `ceg5206/contracts.hpp`（v0.1），提供两个入口。

| 接口 | 用途 |
|---|---|
| `b::prepare_simd` | 供 D 准备单线程算子。调用返回对象的 `run` 会计算整张图，并覆盖输出矩阵 Y。 |
| `b::row_range_simd` | 供 C 计算同一行中指定边区间的贡献。每次调用覆盖输出缓冲区中的全部 `f` 个特征，既可写入完整输出行，也可写入分段结果。 |

**运行平台为支持 NEON 的 AArch64 CPU**，每条向量指令可同时处理 4 个 FP32 特征。已在 Apple Silicon 上验证，其他指令集会在编译阶段报错。团队正式实验的平台仍待确定。

## 构建与验证

在仓库根目录执行以下命令。验证脚本只需要支持 C++17 的 Clang 和 Python 3。

```bash
python3 member_b/scripts/verify.py

# 若还需检查并发调用时的数据竞争，改用这一条
python3 member_b/scripts/verify.py --tsan
```

脚本先运行优化构建和 ASan/UBSan 检查，再检查生成的汇编，确认使用了 SIMD 指令，且浮点乘法与加法分别执行。随后构建性能对照程序，并将 B 的实际实现链接到 D 的测试入口。联调时由测试适配器提供 A 的固定样例。生成的可执行文件、汇编文件和样例头文件位于 Git 忽略的 `member_b/build/` 目录。

如果需要将 B 作为静态库链接到其他程序，可以用 CMake 构建 `ceg5206_b`。

```bash
# 优化构建与正确性测试
cmake -S member_b -B member_b/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build member_b/build-release -j
ctest --test-dir member_b/build-release --output-on-failure

# 检查越界访问和未定义行为
cmake -S member_b -B member_b/build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DB_SANITIZE=ON
cmake --build member_b/build-asan -j
ctest --test-dir member_b/build-asan --output-on-failure
```

仅构建内核时，可同时指定 `-DBUILD_TESTING=OFF` 和 `-DB_BUILD_PROBE=OFF`，跳过测试及性能对照程序。内核本身只依赖公共头文件、C++17 编译器和 AArch64 NEON。启用 `B_SANITIZE` 后，自定义诊断程序还应链接 `ceg5206_b_policy`。

## 计算规则

- **权重与精度**：按 CSR 中的边顺序，使用已存储的权重进行 FP32 乘法和累加。归一化权重由调用方预先计算，自环只在图中显式存储时参与计算。
- **分块与尾部**：宽度为 16、32、64 的特征块分别使用 4、8、16 个向量累加器。其他正整数宽度使用输出缓冲区保存中间和。块末尾剩余的 1 至 3 个特征逐个计算，只访问有效元素，因此输入和输出只需满足 `float` 的自然对齐要求。
- **输出覆盖**：空行和空边区间输出全零。每次调用都会写入全部目标元素。调用方可以交替传入两份形状相同的 X，也可以用 NaN 预填 Y 来检查漏写。
- **并发与内存**：内核由调用线程执行，无需堆分配或持久工作区。C 可让多个线程同时读取同一张图和 X，每次调用须独占自己的输出行或分段缓冲区。
- **输入校验**：`prepare_simd` 要求线程数为 1，特征数、分块宽度和最大边块长度均为正，并检查 CSR 数组形状及稠密数组大小是否可寻址。调用方负责在计算前检查完整 CSR、X、缓冲区重叠情况和边区间边界。

## 文件与交接资料

| 文件 | 用途 |
|---|---|
| [聚合内核](src/simd.cpp) | B 的完整实现。D 集成时只需编译这一份 B 源文件。 |
| [正确性测试](tests/test_simd.cpp) | 对照 FP64 参考结果，验证尾部、输出覆盖、分段求和、并发和内存访问边界。 |
| [样例转换脚本](tests/fixture_header.py) | 直接读取 A 的 JSON 交接样例，生成测试用头文件。 |
| [联调测试适配器](tests/d_fixture_a.cpp) | 为 B 与 D 的链接测试提供固定输入，仅在显式启用测试标志时编译。 |
| [性能对照程序](bench/) | 在相同输入上比较标量、逐边 SIMD 和特征分块 SIMD。 |
| [验证脚本](scripts/verify.py) | 使用 Clang 复现正确性检查和 D 入口联调。 |
| [计时脚本](scripts/probe.py) | 保存原始计时、汇总 CSV，以及硬件和编译环境记录。 |
| [C/D 交接说明](docs/HANDOFF.md) | 接口调用示例、对象生命周期、输出所有权和链接参数。 |
| [设计与性能分析](docs/DESIGN.md) | 循环顺序、寄存器使用、访存量估算、汇编检查和本机实测对比。 |
| [验证记录](docs/VALIDATION.md) | 已执行的测试、通过情况和待联调事项。 |
| [Apple M5 本机数据](evidence/apple-m5-n8192-d8/) | 300 次原始调用的耗时、汇总结果和运行环境。 |

## 复现本机性能对照

以下命令使用 8,192 个节点、平均每个节点 8 条边。每次运行请指定新的输出目录，以便保留各次结果。

```bash
python3 member_b/scripts/verify.py
python3 member_b/scripts/probe.py --nodes 8192 --degree 8 --out member_b/out/probe-01
```

这组数据用于分析 B 的特征分块在本机上的效果，使用 B 的测试图生成器和对照内核。全组的 24 组正式工作负载实验和拆行参数选择，需要在 A 的图生成器、C 的调度实现与 D 的实验流程接通后完成。
