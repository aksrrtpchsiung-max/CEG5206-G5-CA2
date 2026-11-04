# Member A: reference, validation and graph inputs

本目录集中存放 A 的工作。依据 `【F】Group5 Team Project Proposal.pdf`（5 页，本地保留），当前共享内容为**接口提案和交接样例**，还未实现计算内核、图生成器或实验程序。

## 从哪里开始

- [接口约定 v0.1](docs/INTERFACES.md)：给 B、C、D 阅读的数据、函数、内存、正确性及计时协议。
- [C++17 接口头文件](include/ceg5206/contracts.hpp)：约定的类型和函数声明；A、B、C 共用同一份，避免各自复制修改。
- [交接样例](fixtures/contract_v0_1.json)：包含 CSR、独立边列表、输入、手算 FP64 结果、chunk 和 partial 结果。

接口状态：**A 提出的可评审版本，尚未获得 B、C、D 确认**。PDF 是 proposal，原文也说明范围需团队和教师同意。文档区分 PDF 要求与本次补充的工程约定；本版不包含 B、C、D 的实现。

## 职责边界

| 成员 | 主责 | 与 A 的交接 |
|---|---|---|
| A | CSR 校验、单线程标量基线、图生成器、独立 FP64 oracle | 提供合法输入、基线、检查器和固定样例；交叉检查 chunk 边界与 split-row 输出 |
| B | feature tiling、SIMD、尾部处理 | 使用共同 CSR / dense view，提供 SIMD 行区间内核和单线程完整算子 |
| C | 行调度、scan、chunk、partial reduction | 使用 B 的行区间内核，提供 static / dynamic / split-row 算子和可检查的 chunk 描述 |
| D | 集成、内存核算、实验、profiling | 调用统一入口，保存种子、参数、验证日志和计时结果 |

对应原文：第 1 页 §2 计算契约，第 2 页 §3 Tasks 1–5，第 3 页 §4 实验协议，第 4 页 §5 成员分工，第 5 页 §7 交付要求。

## 目录和使用方式

```text
member_a/
  README.md
  .gitignore
  CMakeLists.txt                 # 目前仅导出 header-only 接口 target
  docs/INTERFACES.md
  include/ceg5206/contracts.hpp
  fixtures/contract_v0_1.json
```

后续 A 的实现放 `src/`，测试放 `tests/`，构建和输出放已忽略的 `build/`、`out/`，临时文件放已忽略的 `.work/`。不在仓库根目录散放 A 的脚本和输出，不新建 B、C、D 的实现目录。

D 集成时可使用 `add_subdirectory(member_a)` 和 `target_link_libraries(<target> PRIVATE ceg5206_contracts)`，然后 `#include <ceg5206/contracts.hpp>`。该 target 仅传播 include 路径与 C++17 要求；此时调用声明的算法函数仍会缺少实现，不能当作可运行的 A 库。后续增加实现 target 和独立验证入口。

验证记录：交接样例的边列表与 CSR 一致性、手算输出、chunk 覆盖及 partial 归并已用 Python 独立检查。当前环境没有可用的 C++ 编译器或 CMake，因此尚未进行 C++ 编译与链接验证，后续实现前需补齐。
