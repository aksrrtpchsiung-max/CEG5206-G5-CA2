# A 的贡献与验收记录

2026 年 11 月 6 日。A 实现并交付团队共享的输入、基线和正确性检查，并复核 C 的 chunk 边界及 split 输出。

| 交付内容 | 实现或记录 |
|---|---|
| CSR、edge list、dense 校验与构建 | [validation.cpp](../src/validation.cpp)、[csr.cpp](../src/csr.cpp) |
| 单线程 FP32 基线、独立 FP64 oracle 与误差定位 | [scalar.cpp](../src/scalar.cpp)、[oracle.cpp](../src/oracle.cpp) |
| 固定种子图、特征、degree 统计和 setup 分项时间 | [generator.cpp](../src/generator.cpp) |
| split plan 审计与 C 的代码、数值、竞争复核 | [chunk_audit.cpp](../src/chunk_audit.cpp)、[审查记录](REVIEW_C.md) |
| 可复现的构建、测试、报告素材 | [运行方法](A_USAGE.md)、[方法与结果](A_REPORT_NOTES.md) |

| 运行环境 | 实际验收 |
|---|---|
| Windows x64，MSVC 19.43 Release，VS CMake 3.30.5 | 7/7 CTest 通过，标量乘加指令为 `mulss` / `addss` |
| WSL Ubuntu x86_64，GCC 15.2 Release，CMake 4.2.3 | 7/7 CTest 通过；24 组 scalar workload × 两个 feature seed 通过 |
| AArch64 Linux guest，GCC 15.2、OpenMP、QEMU 10.2.1 | 7/7 CTest 通过；真实 B/C 与 A 的 oracle 对接，D 的 48 个 smoke 用例完成 |
| WSL x86_64，GCC TSan + LLVM libomp / Archer | C 的生产 adapter 与 C 测试行内核在边界、hub 用例中通过竞争诊断 |

上面的 ARM 联调使用 B 的 `2c8f04f`、C 的 `8a06e1b`、D 的 `e6921eb`；共享公共头文件来自 `0eaeb15`。B 提供 NEON，C 提供调度和归并，D 提供 benchmark 和数据导出。A 编写测试、运行联调并保存验证数据。D smoke 的 guest ELF SHA-256 为 `252ab43a1cb422b8e15859345d80e276f1c61f360fbc97688103de8adbba938b`。GCC 忽略 B 源码中的三个 unroll 提示，数值验证通过；正式机器的指令和性能以该机实测为准。

原始记录分别覆盖[scalar grid](../evidence/scalar-grid.csv)、[各实现 hub 误差](../evidence/hub-arm.csv)和[D 的逐输入验证](../evidence/team-smoke.csv)。接口冻结和正式硬件选择属于团队决策。
