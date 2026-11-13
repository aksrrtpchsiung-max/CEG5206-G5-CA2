# 编译与联调

以下命令从仓库根目录运行。生产库依赖 C++17 标准库。测试使用 Python 3 将共享 JSON 样例转换为构建目录中的 C++ 常量。命令示例使用支持 `ctest --test-dir` 的 CMake。

## 本机运行

Windows 本机已用 Visual Studio 2022 Build Tools、MSVC 19.43、CMake 3.30.5 与 Python 3.11 构建。若普通 PowerShell 找不到 `cmake`，在 Developer PowerShell 中运行，或使用这台机器上的完整路径。

```powershell
$bin = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
& "$bin/cmake.exe" -S member_a -B member_a/build/windows -G "Visual Studio 17 2022" -A x64
& "$bin/cmake.exe" --build member_a/build/windows --config Release
& "$bin/ctest.exe" --test-dir member_a/build/windows -C Release --output-on-failure
& ./member_a/build/windows/tests/Release/a_correctness.exe handoff
```

Linux 原生 GCC 或 macOS Clang 使用以下命令。普通开发只需前四行。最后一行生成 A 的完整正确性记录，覆盖 24 个 workload 与两份 X。

```sh
cmake -S member_a -B member_a/build/native -DCMAKE_BUILD_TYPE=Release
cmake --build member_a/build/native -j4
ctest --test-dir member_a/build/native --output-on-failure
member_a/build/native/tests/a_correctness handoff
mkdir -p member_a/out
member_a/build/native/tests/a_correctness grid > member_a/out/grid.csv
```

可单独运行 `validation`、`generator`、`comparison`、`chunks`、`handoff`、`variants`、`hub`。失败返回非零退出码。`hub` 输出两个 feature seed 及重复调用的误差和定位。`grid` 是较大的可选校验，最大用例需要约 361 MiB 存放保留数组。

## 链接 A 的库

调用方使用 `add_subdirectory(member_a)` 和 `target_link_libraries(your_target PRIVATE ceg5206_a)`，再包含 `<ceg5206/contracts.hpp>`。仅需要声明时可以链接 `ceg5206_contracts`。子项目模式只提供库，独立测试由 A 单独构建。

```cpp
using namespace ceg5206;
const RunConfig cfg{1, 32, 128};
constexpr FeatureCount f = 17;
auto data = a::generate_graph({GraphFamily::uniform, 128, 1024, 5206001});
const auto g = data.csr.view();
auto features = a::generate_features(g.n, f, 5206101);
std::vector<float> result(features.size());
std::vector<double> ref(features.size());
DenseConstView x{g.n, f, {features.data(), features.size()}};
DenseView y{g.n, f, {result.data(), result.size()}};
auto valid = a::validate_problem(g, x, y);
if (!valid.ok()) throw std::runtime_error(valid.message);
auto prepared = a::prepare_scalar(g, f, cfg);
prepared.run(g, x, y, cfg, prepared.state.get());
a::oracle_fp64({g.n, {data.oracle_edges.data(), data.oracle_edges.size()}},
               x, {g.n, f, {ref.data(), ref.size()}});
auto report = a::check_close({g.n, f, {result.data(), result.size()}},
                             {g.n, f, {ref.data(), ref.size()}});
if (!report.passed) throw std::runtime_error("scalar mismatch");
```

示例使用标准头 `<stdexcept>` 和 `<vector>`。graph、X、Y 和 reference 必须在相应 view 使用期间存活。生成器返回的 `timings` 包含三个独立阶段，单位均为秒。

## 联调 B、C 的生产代码

在项目根目录构建。测试直接链接 A、B、C 的生产库，覆盖 FP64 校验、chunk 审计及多线程输出。

```sh
bash member_d/scripts/build_team.sh
ctest --test-dir build --output-on-failure -R 'a_'
python3 member_d/scripts/demo.py --out results/demo
```

测试默认使用 1、2、4 个线程；设置 `A_TEST_PHYSICAL_CORES` 可增加本机实际核数的用例。
历史 QEMU 联调数据保存在 [team-smoke.csv](../evidence/team-smoke.csv)。
