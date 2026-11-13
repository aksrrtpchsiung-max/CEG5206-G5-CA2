# C 的 chunk 与输出交叉审查

审查对象为 C 的 `contract_adapter.cpp`，测试分别使用 B 的生产 NEON 代码和 C 自带的 x86 测试行内核。结果与这两个版本绑定。[C 源码 8a06e1b](https://github.com/aksrrtpchsiung-max/CEG5206-G5-CA2/blob/8a06e1b60c858907946397b04e617629e9db2329/member_c/src/contract_adapter.cpp) · [B 源码 2c8f04f](https://github.com/aksrrtpchsiung-max/CEG5206-G5-CA2/blob/2c8f04fe3e676e662776165426c0d0c45350a115/member_b/src/simd.cpp)

| 核对项 | 源码与测试结果 |
|---|---|
| Scan 与区间 | C 以商和余数计算每行块数，按行建立 exclusive offsets。A 独立从 CSR 行界限核对每个 descriptor。测试覆盖 degree=0、1、Te−1、Te、Te+1、2Te、2Te+1。 |
| 输出所有权 | 空行清零，单块行独占一个 Y 行；多块行的所有块各写一个连续且唯一的 partial slot。worker 的分配顺序与 descriptor 存储顺序独立。 |
| 归并 | C 的初始化、块计算和归并分别位于独立 OpenMP parallel-for 中，阶段之间有同步。归并按输出行分工，每行按 descriptor 顺序累计。 |
| 错误检测 | 人为制造缺边、重叠、越界、错误 row、错序、缺块、重复 slot、错误 C_s 和非法 scan，A 的审计器逐个拒绝。 |
| 真实输出 | 在 AArch64 QEMU 下使用 B 的 NEON 与 C 的真实三种调度。相同 state 先后处理 X0、X1、X0；检查带符号权重、空行、尾部和 hub，并在输出两端放置哨兵。1、2、4、8 线程都通过 A 的 oracle。 |

C 的 split factory 为 descriptor、worker 分配索引和 partial vectors 分别保留空间。A 核对 partial 的逻辑数量 `C_s*f`，并检查报告的容量字节足以覆盖这些向量。排序、worker 分桶等 prepare 临时分配与返回时仍保留的数组具有不同的生命周期。D 的 smoke 字段 `validation_retained_bytes` 描述保留量，正式峰值核算还需计入这些临时对象。

## 独立竞争诊断（历史提交）

在 WSL x86_64 重新编译 A 测试和 C 的生产 adapter，启用 GCC ThreadSanitizer，并链接 LLVM 的 OpenMP 运行库。Archer 在日志中确认加载并标注 OpenMP 同步关系。CPU 为 Intel i7-11800H。C 的 x86 测试行内核用于检查调度写入，可独立于 ARM NEON 执行。边界用例及 hub 用例在 1、2、4、8 线程下完成，诊断未报告数据竞争。

以下命令只适用于上述历史提交，所用 x86 测试替身已从最终 ARM 项目中移除。在安装了 GCC、LLVM libomp 和 Archer 的 Linux 环境可检出对应历史版本复查。将 `OMP_LIB` 设为 libomp 的完整路径，将 `ARCHER_LIB` 设为 libarcher 的完整路径。运行时应看到 Archer 已启用的提示。

```sh
cmake -S member_a -B member_a/build/race -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-no-pie" \
  -DA_C_SOURCE="$TEAM/member_c/src/contract_adapter.cpp" \
  -DA_C_TEST_ROW_SOURCE="$TEAM/member_c/tests/portable_b_shim.cpp" \
  -DOpenMP_CXX_FLAGS=-fopenmp -DOpenMP_CXX_LIB_NAMES=omp -DOpenMP_omp_LIBRARY="$OMP_LIB"
cmake --build member_a/build/race -j4
export OMP_TOOL=enabled OMP_TOOL_LIBRARIES="$ARCHER_LIB" ARCHER_OPTIONS=verbose=1
export TSAN_OPTIONS=halt_on_error=1:ignore_noninstrumented_modules=1
export OMP_DYNAMIC=FALSE A_TEST_PHYSICAL_CORES=8
member_a/build/race/tests/a_correctness variants
member_a/build/race/tests/a_correctness hub
```

竞争诊断结论限于上述执行路径与测试数据。生产 B/C 的数值验证来自独立的 ARM 联调。正式 ARM 硬件的运行库、性能和线程绑定由 C/D 在目标机记录。
