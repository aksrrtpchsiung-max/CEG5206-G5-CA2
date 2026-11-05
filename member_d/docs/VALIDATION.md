# v0.1 框架验证记录

日期：2026-11-05。基础提交：A 的 `0eaeb1518228985b134c6220c41252c52f1f537c`。
验证平台：macOS arm64，Apple Clang 17.0.0，Python 3.9，临时目录内的 CMake 4.4.4。
这是框架验证，不是项目性能实验；没有声称算法加速比。

已通过：

- Clang 直接构建默认 benchmark，无警告；五个 variant 全部显示 unavailable。
- 同时启用 A/B/C 代码路径的 C++ 语法检查；这不是与真实实现的链接验证。
- CMake Release configure/build 与 CTest（1 项 measurement 测试）。
- 测量单元测试：预热与计时调用数、两份输入交替、输出观测、median/IQR、溢出和缺失 kernel 检查。
- Python unittest：7 项通过，无跳过。覆盖 120 个正式 case 的确定性展开、调参/复用计划、
  非法倾斜图、scaling 线程列表、缺失实现不生成测量、匹配基线的汇总，以及测试专用 A 适配器的端到端输出。
- 五份配置计划：smoke=6，main=120，tuning=12，reuse=30，scaling 示例=44 cases。

端到端适配器只接受固定的 2 节点测试输入，存放于 tests/fake_a.cpp，需 D_TEST_ONLY 才能编译。
其数据写入临时目录并由测试删除，不上传测试耗时作为实验结果。
正式生产构建不注册或链接该适配器。

尚待真实算法交付后验证：A 的数据与独立 oracle、B 的 SIMD 指令和尾部、C 的 chunk/race/归并、
实际 workspace 和临时峰值、正式 ISA 与 OpenMP 工具链、完整实验和 profiling。
