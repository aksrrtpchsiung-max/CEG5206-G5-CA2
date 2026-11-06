# Member A · CSR baseline and correctness

A 提供可链接的 C++17 库 `ceg5206_a`。它生成 uniform / skewed 输入、校验 CSR、计算单线程 FP32 基线，并用从原始边列表独立计算的 FP64 结果检查各成员的实现。

从仓库根目录运行以下命令即可编译 A、验证共享样例并运行七组正确性测试。Windows 使用多配置生成器时，构建与测试均选择 Release。

```sh
cmake -S member_a -B member_a/build/local -DCMAKE_BUILD_TYPE=Release
cmake --build member_a/build/local --config Release
ctest --test-dir member_a/build/local -C Release --output-on-failure
```

| 需要做的事 | 入口 |
|---|---|
| 构建、运行某个用例、与 B/C/D 联调 | [使用说明](docs/A_USAGE.md) |
| 查询接口、所有权、计时边界与种子 | [v0.1 约定](docs/INTERFACES.md)、[C++17 声明](include/ceg5206/contracts.hpp) |
| 核对带符号权重、空行和 split 输出 | [手算样例](fixtures/contract_v0_1.json) |
| 查看 24 组输入、高度 hub 误差及报告素材 | [报告说明](docs/A_REPORT_NOTES.md)、[原始基线校验](evidence/scalar-grid.csv) |
| 检查 C 的边界、输出所有权及竞争检测范围 | [C 交叉审查](docs/REVIEW_C.md) |
| 核对实际交付及小组联调版本 | [贡献记录](docs/CONTRIBUTIONS.md) |

库实现在 `src/`，测试在 `tests/`。可复现的数值记录在 `evidence/`。本地构建、完整日志与临时集成目录分别放在已忽略的 `build/`、`out/`、`.work/`。
