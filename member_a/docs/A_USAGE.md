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

需要 B 的 AArch64 NEON 编译目标以及支持 OpenMP 的工具链。在已有合并工作树中，`TEAM` 指向含 `member_b`、`member_c`、`member_d` 的目录。若只检出了 A 分支，可用以下命令重建此次联调的只读快照。

```sh
TEAM="$PWD/member_a/.work/team"
mkdir -p "$TEAM"
git archive 2c8f04f member_b | tar -xf - -C "$TEAM"
git archive 8a06e1b member_c | tar -xf - -C "$TEAM"
git archive e6921eb member_d | tar -xf - -C "$TEAM"
mkdir -p "$TEAM/member_a"
cp -R member_a/src member_a/include member_a/CMakeLists.txt "$TEAM/member_a/"
```

在原生 AArch64 Linux 上直接构建。x86_64 Linux 可安装发行版提供的 AArch64 交叉编译器和 QEMU，在 configure 命令中额外传入 `-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ '-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64;-L;/usr/aarch64-linux-gnu'`。若工具链位于独立 sysroot，应使用其实际路径。

```sh
cmake -S member_a -B member_a/build/team -DCMAKE_BUILD_TYPE=Release \
  -DA_B_SOURCE="$TEAM/member_b/src/simd.cpp" \
  -DA_C_SOURCE="$TEAM/member_c/src/contract_adapter.cpp"
cmake --build member_a/build/team -j4
ctest --test-dir member_a/build/team --output-on-failure
```

C 的测试默认请求 1、2、4 个线程；另设 `A_TEST_PHYSICAL_CORES` 可加入机器实际使用的 P。核数 8 的此次联调覆盖 1、2、4、8。测试会先审计每个 split plan，再让各 variant 在同一 prepared state 上运行 X0 → X1 → X0。

## D 的独立入口

D 的 v0.1 将 scalar 编译成单独的 object target，其余 A 源文件直接加入 benchmark。在 **AArch64 Linux** 且 `TEAM` 指向上述目录时，命令如下。D 的入口采用 GCC / Clang 编译选项。

```sh
A="$PWD/member_a"
D="$TEAM/member_d"
BUILD="$A/build/d-team"
OTHER="$A/src/validation.cpp;$A/src/csr.cpp;$A/src/generator.cpp;$A/src/oracle.cpp;$A/src/chunk_audit.cpp"
cmake -S "$D" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DD_ENABLE_A=ON -DD_A_SOURCES="$OTHER" -DD_SCALAR_SOURCES="$A/src/scalar.cpp" \
  -DD_ENABLE_B=ON -DD_B_SOURCES="$TEAM/member_b/src/simd.cpp" \
  -DD_ENABLE_C=ON -DD_C_SOURCES="$TEAM/member_c/src/contract_adapter.cpp" -DD_USE_OPENMP=ON
cmake --build "$BUILD" --target benchmark -j4
python3 "$D/scripts/run.py" list --binary "$BUILD/benchmark"
```

从 D 的 `configs/smoke.json` 复制一份，设置 `nodes=[1024]`、`mean_degrees=[8]`、`threads=[1,2]`、`max_chunk_edges=[16]`，并在 `variants` 中列出全部五个 ID。保留 `phase=smoke`、两种 family、三个 feature width 和原有种子。按实际机器填写 D 提供的 `hardware.example.json`，记录编译器和 flags，并用以下命令运行。每次指定一个新输出目录。

```sh
python3 "$D/scripts/run.py" run --binary "$BUILD/benchmark" \
  --config "$BUILD/smoke.json" --metadata "$BUILD/hardware.json" \
  --out "$A/out/d-team-run1"
```

本次在 x86_64 WSL 上使用 QEMU 启动 ARM benchmark，D 的 `--binary` 指向启动器。其 manifest 中单独保存 guest ELF 的 SHA-256。原生 ARM 机器直接传 benchmark 路径。每个 case 的两份 X 校验、验证摘要和定位见[联调记录](../evidence/team-smoke.csv)。
