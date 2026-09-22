# Wolf 项目状态

本文只写**现状**：能力、位置、未决项。**为什么这样做**一律在
[linguist-decisions.md](linguist-decisions.md)（台账 A1–A80），**做过什么**在 git 历史。

## 当前定位

wolf 在 synthrt spec 2.4 之上建立开放的**语言域**：注册 `linguist` 贡献类别、发布语言组合与
链推理契约、提供语言 Provider 插件与歌手语言 Pipeline，不把具体语言能力写入 synthrt 或 dsinfer。

设计以 **[linguist-architecture.md](linguist-architecture.md)** 为索引，分七层展开
（L0 框架不变量 / L1 语言域身份 / L2 语言组合契约 / L3 链推理契约 / L4 运行时装配 / L5 宿主接入
/ L6 会话层），外加变体面与发布面两个正交平面。

## 文档

| 文档 | 覆盖 |
| :-- | :-- |
| [linguist-architecture.md](linguist-architecture.md) | 索引、层栈、框架硬约束、决策台账 |
| [linguist-domain-contract.md](linguist-domain-contract.md) | L1 + L2 |
| [linguist-inference-contract.md](linguist-inference-contract.md) | L3 |
| [linguist-runtime.md](linguist-runtime.md) | L4 + L5 |
| [linguist-session.md](linguist-session.md) | L6（设计 + 落地时的修正，§12.1） |
| [linguist-variants.md](linguist-variants.md) | 变体面 |
| [linguist-distribution.md](linguist-distribution.md) | 发布面（渠道工程接线归资源仓文档） |
| [linguist-resource-cache.md](linguist-resource-cache.md) | 资源缓存 |
| [plugin-internals.md](plugin-internals.md) | 插件内部：骨架、解析与诊断纪律、契约身份、演进清单 |
| [linguist-decisions.md](linguist-decisions.md) | 决策与论证 |
| [linguist-implementation-plan.md](linguist-implementation-plan.md) | 顺序、工作包、验收判据 |
| [schemas/](schemas/) | 四个契约的 `exports` 与 `imports[].options` JSON Schema（spec 2.4 必须项） |

## 已实现的能力

**声明与加载**：`linguist` 贡献类别、`LinguistSpec` / `LinguistProvider` 插件体系；
`WolfLinguistProvider` 解释 Level 1 声明并提供 import validator、Spec Extension 与
Executive Factory；G2P / S2P / Onset 三份独立推理契约，经三个固定 role 绑定；二元组
（language, scheme）命中校验在加载期执行。

**运行时**：歌手 → pipeline → linguist → 三个 inference 的执行体树；`depth` 截断与逐词锁定；
取消端到端可用，运行中与**开始前**落下的都算数（`stop()` 向下传递并由下一次 `start()` 消费，
脚本变体另装计数钩子并关掉 JIT，实测约 50 µs 应答）；
资源缓存跨执行体共享解析产物。

**L6 会话层** `wolf::LinguistSession`（`include/wolf/Session/LinguistSession.h`）：目录快照、
三态就绪与双向缓存、按代管理的执行体池、保留标记收口、取消把手。并发经 ThreadSanitizer 验证。

**契约开放位**：`exports.openSet` 说明清单是否即全集（域契约 §4.0），由打包 lint 从链的兜底步
推导；Onset 缺席即逐位 `false`（域契约 §5.0）；空词 → `skip`、含空白 → `InvalidInput` 收在
`wolf::classifyLyric()`，三个 G2P 变体共用。

**任务面**：十一个执行体一律持 `srt::ITask` 并转发（`wolf::ExecutiveTask`，A75）——`waitForFinished()`
真的等，第二次并发 `startAsync` 被拒，卸包会等到转换结束。

**降级**：`linguist/s2p` 可缺席，该语言的最深深度即 `Pronunciation`（域契约 §5.0.1）；
`LanguageStatus` 交出 `maxDepth` 与音素覆盖度，门限归宿主，唯一例外是「清单自称全集而声库一个都
唱不了」由会话直接判不可用（A70）。

**变体**（收录表无缺口）：S2P `direct` / `dict` / `mapping` / `lua`；Onset `rule` / `lua`；
G2P `pipe-chain` / `algo-pinyin` / `multig2p-onnx`；另有三型打标共享的 `Verifier`。
`multig2p-onnx` 跑通真实模型，语言 id 确实抵达模型（`test_MultiG2P` 钉住两种语言不塌成同一读法）。

**资源**：13 个旧套件经转换管线转为 spec 2.4 格式，连同 `lang-zxx` 与两个后端包共 **15 个包**，
在完整与最小两种构建下都加载通过。

## 验证

| 项 | 结果 |
| :-- | :-- |
| 完整构建 | 17 个测试二进制、105 个用例全绿；`-j8` 满负载连跑 12 轮无不稳 |
| 最小构建（`-DWOLF_DISABLE_DSINFER=ON -DWOLF_DISABLE_LUAJIT=ON`） | 15 个测试二进制全绿 |
| ThreadSanitizer | 全部用例通过。告警只出现在两个二进制，且整条栈都在**未插桩的第三方 `.so`** 内：`test_MultiG2P`（`libonnxruntime.so`，既有）与 `test_LinguistRuntime`（`libsynthrt.so` 的 `ITask`，因 wolf 本轮才开始走它的 worker 路径而首次可见，见 Q7）。pinyin 并发预热与双会话两例均净 |
| 端口安装树 | `vcpkg install wolf-lang-packages[...]` 装出 15 个包，17 个测试指向该树通过（A67） |
| 下游消费 | 空白工程按 README 原样 `find_package(wolf)` + 链接 + 运行通过；把私有依赖放回公开接口时配置当场失败 |
| 版本区间 | 写死旧修订 `1.0.1.2` 的歌手包，对 v1.0.1.3 / compat 1.0.1.0 的语言包加载通过（A68） |
| 归档 | `make-lang-release.py --verify` 解开后与源逐文件比对通过 |
| 宿主对接 | `test_HostFlow` 用真实迁移资源走完「取发音 → 用户改写 → 取音素与 onset」 |

## 工具

| 脚本 | 用途 |
| :-- | :-- |
| `scripts/convert-g2p-packages.py` | 旧套件 → spec 2.4 格式，并生成 `wolf/g2p-pinyin` |
| `scripts/make-lang-release.py` | 打包、算 SHA512、重生端口的 `assets.cmake` 与 `version-string`；`--verify` 解开归档与源逐文件比对，不符即拒绝打包 |
| `scripts/make-voicebank-fixture.py` | 由语言包生成歌手包形状的夹具，补上语言包不出的 S2P 与 onset（发布文档 §3.2） |
| `scripts/check-declarations.py` | 用 `docs/schemas/` 校验声明，并跑三项打包期 lint（含 `openSet` 推导）。打包前由发布脚本调用，有错即拒绝打包 |

## 位置

里程碑：M0 调研 → M1 声明面 → M2 契约面 → M3 运行时面 → M3.5 发布链切片 → M4 真实解释器
→ M5 其余包铺开 → M6 收尾 → M7 版本口径与降级 → M8 稳定性、规范性与并行安全。
详见[实施计划](linguist-implementation-plan.md)。

**M4 完成，M5 进行中，L6 已落地，M6 的 Q2 / Q3 / Q4 已落地，M7 与 M8 全部落地。**

M5 剩余：`eng` / `por` / `kor` / `ita` 四种已成完整语言闭包（A55）；余下八种中五种等 P7 定名，
`cmn` / `yue` / `jpn` 由歌手包闭合（A66），语言包侧无待办。

## 审计所得缺陷（已全部修复）

四轴审核（稳定性 / 向后兼容性 / 规范性 / 并行加载安全性）的结果。每条都带回归用例，且每条都先在
停掉修复的情况下确认过用例会挂。

| # | 内容 | 修法 |
| :-- | :-- | :-- |
| X1 | 下游 `find_package(wolf)` 因 `BLAKE3::blake3` 进了公开 `LINKS` 而直接失败 | 归 `LINKS_PRIVATE`，并加配置期断言守住导出接口（A74） |
| X2 | 语言包 `compatVersion == version`，每次重新打包打断已发布声库 | 一律取修订号 0 下沿；依赖不指向下沿由 lint 判**错误**（A68） |
| X3 | 预置的取消被 `start()` 抹掉 | `start()` 改为 `exchange` 消费；`enrol()` 返回布尔（A73） |
| X4 | 保留词拦截吞掉用户锁定的音素层 | 拦截条件加 `!word.locked.has_value()` |
| X5 | `startAsync` / `waitForFinished` 这一面是坏的；另有五处把 `quit()`/`wait()` 覆盖成空操作，废掉了上游本已接好的转发 | 抽 `wolf::ExecutiveTask`，十一个执行体一律持 `srt::ITask` 并转发（A75） |
| X6 | 资源缓存索引只增不减 | 插入时按阈值清理过期条目与路径备忘（W8.9） |
| X7 | 契约违例三层口径不一致，执行体静默截断 | 短批次一律拒绝，与链变体和会话同口径（W8.7） |
| X8 | 失败的加载永久占住 pinyin 词典根，卸载也从不归还 | 保留改为 RAII，随模块 `Configuration` 存活（A72） |
| B4 | ABI 纪律未记账 | 立纪律 D1，并随本批公开结构体变更把版本抬到 **0.1.0.0**；README 增《Versioning and ABI》 |
| B5 | 声明根键白名单会拒绝未来的框架公共字段 | 改为警告，并新增 `wolf::logCategory()`（A77） |
| P3 | pinyin 引擎构造全程持进程级锁，cmn / yue 预热全进程串行 | 锁只覆盖路径发布一次，构造移出锁（W8.5） |
| P5 | 两个 `LinguistSession` 共用一个 unit 无人拦、无测试 | 证明其安全并**取消该限制**（A78） |

**并行加载**：包加载的并行度恒为 1，spec 所定，wolf 不提供也不追求（实施计划 D2）。

## 未决项

| # | 内容 | 归属 |
| :-- | :-- | :-- |
| **P7** | `deu` / `fra` / `spa` / `rus` / `fil` 五种的 `scheme` 造名。其余七种已定（A27 / A45 / A49），这五种证据不足以造出有依据的名字，暂留 `ds` 占位 | 需生态知识，**已搁置** |
| **P8** | release 何时公开。技术侧已就绪：bundle 0.1.2.0（打包修订号 3）已切好并验证，端口已实测可装 | 待决定 |
| Q7 | `srt::ITask::startAsync` 的 `finish()` 在释放互斥量后才 `notify_all()`；TSan 报出一处但栈全在未插桩的 `libsynthrt.so` 内，倾向误报，定论需插桩过的 synthrt | 上游 |
| P4 | lite 消费 wolf 语言包时的端口重复问题 | lite 侧 |
| P5 | `.dspk` 单文件形态——待 main 支持解压后再议 | 上游 |
| P6 | 旧声库 manifest 的 `g2pPackageVersion` / `g2pPackages` 映射 | 迁移期 |
| Q1 | 未被 `languages` 引用的 linguist import：现定为编辑期 lint 警告、不判加载失败 | 已定，可复议 |
| Q5 | DiffSinger 歌手 `configuration` 仍强制要求 `dict` 路径，旧 G2P 栈遗留 | 语言域迁移完成后退役 |
| Q6 | A11 已在 synthrt 分支 `onnxruntime-builds-uptake` 落地，wolf 已切换数源；并入 synthrt main 待上游 | 上游 |

**移植阻塞项 B1–B4 已全部排除**：B1 收敛为共享后端包 `wolf/g2p-pinyin`（A30–A32）；B2 无存量
资源受影响；B3 与 B4 复核后不成立——前者那份词典本就是歌手包内容（A66），后者 wolf 只依赖自有
overlay 的 `synthrt-main`，从不引用共享 `synthrt`，是 lite 采纳时的决策。

## 已知外部事实

- synthrt `origin/main` 观察点 `3c7549d`；wolf 锚点 `cecedba`；spec 2.4 blob `3d0c6568`；
  转换源 `origin/refactor` 固定于 `814bf81`。
- 随附插件：`linguistproviders/wolf`，推理解释器 `chain` / `pinyin` / `s2p` / `onset`，条件构建的
  `multig2p`（需 dsinfer）与 `lua`（需 LuaJIT），以及仅测试构建的桩。
  **发行的最小构建不含桩**，加载 `wolf/g2p-multi` 会失败（发布文档 §7.5）。桩另外无条件声明
  一个 `stub-miscount` 变体，供故意违反 provider ABI 的夹具使用（A79）。
- 依赖新增 `re2`（`\p{Han}` 等 Unicode 属性类，`std::regex` 无法胜任）、`cpp-pinyin` 与
  `luajit`；`onnxruntime-builds` 经 manifest feature `onnx` 引入，**ORT 版本完全由该端口决定**，
  两个仓里都不再出现版本号（A80）。
- 词典与模型的音素集不一致：逐包清点 `fil` 100%、`ita` 75%、`eng` 44%，其余六种为 0。后两者
  早于本轮工作且已随 `lang-v0.1.0.0` 发布（A48 / A50）。
