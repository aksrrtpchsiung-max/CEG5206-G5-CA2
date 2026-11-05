# D 接入与接口评审记录

状态：D 的实现方案与评审反馈，待 A/B/C 共同确认。依据 A 的接口草案 v0.1，未修改公共头文件。

## 由 D 按草案实现的部分

- 使用 CsrView / DenseView 和公共 RunConfig，不另建算法签名。
- 使用各成员 factory 返回的 PreparedKernel；prepare 单独计时，run 计完整调用。
- D 持有两份 X、Y 和验证 reference；state 随 prepared 销毁，并先于 graph 销毁。
- FP64 验证与 NaN 预填在计时外；交替两份 X 重复检查覆盖语义。
- 每个 case 换图、f、线程、tile 或 chunk 都重新 prepare，不复用失配状态。
- 所有实现关闭 fast-math 与 FMA contraction；scalar 独立源文件禁向量化。
- descriptor/partial/other 分开计费；vector 用 capacity；未知算法报错，不静默回退。

## 需要全组确认或补充

| 内容 | 待确认方 | 当前建议 |
|---|---|---|
| 公共声明 v0.1、Y 覆盖语义、prepare 生命周期 | 全组 | 继续共享 A 原头文件，变更共同升级版本 |
| A 源文件与构建 target | A/D | scalar 独立 translation unit；提供其余实现路径、依赖和验证命令 |
| SIMD ISA、FMA 策略及编译选项 | B/D | 所有方案同一数值策略，不在热路径回退；安全尾部 |
| B 的完整算子与行区间内核 | B/C/D | 二者都需交付，C 直接复用公共行区间接口 |
| static/dynamic/split 源码、线程库、chunk view | C/D | 先完成一个版本就接一个，不要求三个一起交付 |
| seeds、generator_version、CSV、摘要算法 | A/D | 沿用 A 提议；输入摘要用 FNV-1a64 native-v1；manifest/binary 用 SHA-256 |
| workspace capacity、prepare 临时峰值 | A/C/D | 当前接口只返回保留 workspace；另提供构建/prepare 的临时峰值证据 |
| phase timing、worker 边数/时间 | C/D | 单独诊断入口或 sidecar；不在正式热路径加原子日志，接口后续共同确定 |
| warm-start / 短调用 | 全组 | 目前单次完整调用、计时后扫描 Y；短到不可靠时先补 batching，再做正式结论 |

输入摘要按 row_ptr、col_idx、values、X0、X1 的原生字节顺序计算；仅用于同平台同配置的输入一致性检查，
不是跨端序标准序列化或安全哈希。摘要保存在每个 case 的 JSON 中。

## A 首次交付验收

提供可链接的 generate_graph / generate_features、validate_csr / validate_problem、prepare_scalar、
oracle_fp64、check_close、degree_stats；标量代码独立编译。先验证 A 的固定交接样例和边界测试，
再由 D 跑 smoke。generator 必须遵守已确认的随机序列及存储权重规则。
不能用从 CSR 解码的边列表替代独立 oracle 输入。

## B 首次交付验收

提供 prepare_simd 与 row_range_simd，说明 ISA 与 flags；f=1/17/65、空行和带符号权重通过独立检查。
默认输入只有自然对齐，没有 padding；尾部不得越界。未使用线程的完整 SIMD 算子仅接受 threads=1。

## C 首次交付验收

逐步提供 prepare_static / prepare_dynamic / prepare_split、chunk_plan_view 和线程构建依赖。
split 需通过 A 的 chunk 审计、FP64 数值检查和独立 race 检查；三者不能互相替代。
run 内不分配 scratch，不重新构建 descriptor；有必要的清理和全部归并必须在返回前完成。
明确实际边数分配方式、合并次序、workspace 的 capacity 字节数和临时峰值。

## 当前范围限制

v0.1 已实现接入框架及 smoke/single/scaling 调用路径；真实 A/B/C 尚未链接验证。
tuning 和 reuse 当前只有配置展开；执行器明确拒绝运行，待下一里程碑补齐几何均值选参、冻结记录、
复用总时间与独立重复测量。正式 profiling、图表和最终报告在算法集成后完成。
任何成员的确认须由其本人作出，本记录不代表代为批准。
