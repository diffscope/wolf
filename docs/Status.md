# Wolf 项目状态

本文只记录**现状**：已实现的能力、所处位置与未决项。设计理由见
[linguist-decisions.md](linguist-decisions.md)（台账 A1–A80），变更历史见 git 历史。

## 当前定位

wolf 在 synthrt spec 2.4 之上建立开放的**语言域**：注册 `linguist` 贡献类别，发布语言组合契约与
链推理契约，提供语言 Provider 插件与歌手语言 Pipeline，不把具体语言能力写入 synthrt 或 dsinfer。
当前版本为 **0.1.0.0**（根 `CMakeLists.txt` 的 `project(VERSION)`）。

设计以 **[linguist-architecture.md](linguist-architecture.md)** 为索引，分七层展开
（L0 框架不变量 / L1 语言域身份 / L2 语言组合契约 / L3 链推理契约 / L4 运行时装配 / L5 宿主接入
/ L6 会话层），另有变体面与发布面两个正交平面。

## 文档

| 文档 | 覆盖范围 |
| :-- | :-- |
| [linguist-architecture.md](linguist-architecture.md) | 索引、层栈、框架硬约束、决策台账 |
| [linguist-domain-contract.md](linguist-domain-contract.md) | L1 + L2 |
| [linguist-inference-contract.md](linguist-inference-contract.md) | L3 |
| [linguist-runtime.md](linguist-runtime.md) | L4 + L5 |
| [linguist-session.md](linguist-session.md) | L6（设计及落地时的修正，§12.1） |
| [linguist-variants.md](linguist-variants.md) | 变体面 |
| [linguist-distribution.md](linguist-distribution.md) | 发布面（渠道工程接线见资源仓文档） |
| [linguist-resource-cache.md](linguist-resource-cache.md) | 资源缓存 |
| [plugin-internals.md](plugin-internals.md) | 插件内部规范：骨架、解析与诊断规则、契约身份、演进检查清单 |
| [linguist-decisions.md](linguist-decisions.md) | 决策与论证 |
| [linguist-implementation-plan.md](linguist-implementation-plan.md) | 实施顺序、工作包、验收判据 |
| [schemas/](schemas/) | 四个契约的 `exports` 与 `imports[].options` JSON Schema（spec 2.4 的必需项），以及两份共享定义 |

## 已实现的能力

**声明与加载**：`linguist` 贡献类别、`LinguistSpec` / `LinguistProvider` 插件体系；
`WolfLinguistProvider` 解释 Level 1 声明，并提供 import validator、Spec Extension 与
Executive Factory；G2P / S2P / Onset 三份独立的推理契约，经三个固定 role 绑定；二元组
（language, scheme）的命中校验在加载期执行。

**运行时**：歌手 → pipeline → linguist → 三个 inference 的执行体树；`depth` 截断与逐词锁定；
端到端取消。转换运行中与**开始前**到达的取消请求均生效：`stop()` 向下传递，并由下一次 `start()`
消费。脚本变体另装计数钩子并关闭 JIT，实测响应时延约 50 µs。资源缓存在执行体之间共享解析产物。

**L6 会话层** `wolf::LinguistSession`（`include/wolf/Session/LinguistSession.h`）：目录快照、
三态就绪与双向缓存、按代管理的执行体池、保留标记处理、取消句柄。并发行为经 ThreadSanitizer 验证。

**契约开放位**：`exports.openSet` 表示清单是否为全集（域契约 §4.0），其取值由打包 lint 从链的
兜底步推导；Onset 缺席时逐位为 `false`（域契约 §5.0）；空词判为 `skip`、含空白的词判为
`InvalidInput`，这两条规则集中在 `wolf::classifyLyric()`，由三个 G2P 变体共用。

**任务面**：十一个执行体均持有 `srt::ITask` 并转发任务面调用（`wolf::ExecutiveTask`，A75）。
`waitForFinished()` 阻塞至转换结束，第二次并发的 `startAsync` 被拒绝，卸载包时等待转换结束。
转发逻辑与停止标志只实现一次，位于 `wolf::ExecutiveBase<Base, Input, Result>`
（`include/wolf/Support/ExecutiveBase.h`，不安装）：各执行体只实现 `runBatch()`，按需覆盖
`onStop()`（脚本变体在此中断解释器，linguist 执行体在此停止子执行体）与 `onSettled()`，并在
各自的析构函数中先调用 `finish()`。

**降级**：`linguist/s2p` 可以缺席，此时该语言的最大深度为 `Pronunciation`（域契约 §5.0.1）；
`LanguageStatus` 提供 `maxDepth` 与音素覆盖度，门限由宿主决定。唯一的例外是清单声明为全集而
声库不支持其中任何音素的情形，会话直接将其判为不可用（A70）。

**变体**（收录表无缺口）：S2P `direct` / `dict` / `mapping` / `lua`；Onset `rule` / `lua`；
G2P `pipe-chain` / `algo-pinyin` / `multig2p-onnx`；另有三种打标类型共用的 `Verifier`。
`multig2p-onnx` 已用真实模型跑通，语言 id 实际传入模型（`test_MultiG2P` 断言两种语言产生不同的读法）。

**资源**：13 个旧套件经转换管线转为 spec 2.4 格式，得到 12 个语言包与共享后端包
`wolf/g2p-multi`；转换管线另生成共享后端包 `wolf/g2p-pinyin`；连同手写的直通包 `wolf/lang-zxx`，
共 **15 个包**，在完整构建与最小构建下均加载通过。当前发布为 **`lang-v0.1.2.0`**
（`scripts/vcpkg-ports/wolf-lang-packages/assets.cmake` 的 `WOLF_LANG_PACKAGES_BUNDLE_VERSION`），
含 15 个归档：

| 包 | 版本 |
| :-- | :-- |
| `wolf/g2p-multi` | 1.0.0.4 |
| `wolf/g2p-pinyin` | 1.0.2.4 |
| `wolf/lang-cmn`、`wolf/lang-yue` | 1.0.1.4 |
| `wolf/lang-deu`、`eng`、`fil`、`fra`、`ita`、`kor`、`por`、`rus`、`spa` | 1.0.0.4 |
| `wolf/lang-jpn` | 0.0.1.4 |
| `wolf/lang-zxx` | 1.0.0.0 |

## 构建与安装

- **依赖来源**：全部依赖经 vcpkg 安装，清单为 `scripts/vcpkg-manifest/vcpkg.json`。端口按序从两个
  overlay 解析：本仓的 `scripts/vcpkg-ports/`（`synthrt-main`、`wolf-lang-packages`）优先于共享
  子模块 `scripts/vcpkg/ports`。`synthrt-main` 固定 synthrt 的 `onnxruntime-builds-uptake` 分支。
- **清单特性**：`onnx`（经 `synthrt-main[onnx]` 构建 dsinfer 与 ONNX 驱动）、`lang-packages`
  （经 `wolf-lang-packages` 端口安装已发布的语言包）、`tests`（Boost.Test）。
- **缓存变量**：`WOLF_LANG_PACKAGES_SOURCE` 指定解包后的语言包目录，非空时取代端口安装树；
  `WOLF_VOICEBANK_FIXTURE_SOURCE` 指定 `scripts/make-voicebank-fixture.py` 生成的歌手包夹具，
  只有 `test_HostFlow` 读取。缺少数据的测试以状态 77 退出，CTest 将其报告为跳过。
  `WOLF_DISABLE_DSINFER` 与 `WOLF_DISABLE_LUAJIT` 显式选出最小构建。
- **安装布局**：插件安装在 `lib/plugins/wolf/<category>/<name>/`，`<category>` 为
  `inferenceinterpreters` 或 `linguistproviders`，构建树布局相同。`wolfConfig.cmake` 定义
  `WOLF_PLUGINS_DIR`（release 树）；仅在存在 `debug/` 前缀树时另定义 `WOLF_PLUGINS_DIR_DEBUG`。
  CMake 包的版本兼容性为 `ExactVersion`。
- **公开头与 Support**：安装的公开头为 `Api/`、`Linguist/`、`Session/`。`wolf/Support` 的头文件
  不安装：无状态帮助函数编入静态库 `wolfsupport`（隐藏可见性，libwolf 与每个插件私有链接，
  不从任何二进制导出）；必须每进程唯一的资源缓存与日志类别留在 libwolf，以
  `WOLF_INTERNAL_EXPORT` 导出，仅供本仓插件使用，不属于公共 API，任何版本都不作 ABI 保证。
- **ABI**：1.0 之前不作 ABI 保证；任何破坏 ABI 的变更与版本号变更同批提交（README《Versioning
  and ABI》）。

## 验证

| 项 | 结果 |
| :-- | :-- |
| 完整构建 | CTest 注册 17 个测试二进制，共 124 个用例（`src/tests/auto` 中的 `BOOST_AUTO_TEST_CASE`）；曾在 `-j8` 满负载下连续运行 12 轮，结果稳定 |
| 最小构建（`-DWOLF_DISABLE_DSINFER=ON -DWOLF_DISABLE_LUAJIT=ON`） | 15 个测试二进制（不含 `test_LuaVariants` 与 `test_MultiG2P`）全部通过 |
| ThreadSanitizer | 全部用例通过。告警只出现在两个二进制中，且整条调用栈都位于**未插桩的第三方 `.so`** 内：`test_MultiG2P`（`libonnxruntime.so`，既有告警）与 `test_LinguistRuntime`（`libsynthrt.so` 的 `ITask`；wolf 在本轮开始使用其 worker 路径后首次出现，见 Q7）。pinyin 并发预热与双会话两个用例均无告警 |
| 端口安装树 | `vcpkg install wolf-lang-packages[...]` 安装出 15 个包，17 个测试二进制以该树为数据源通过（A67） |
| 下游消费 | CI 在安装后构建 `.github/consumer`：按 README 的写法 `find_package(wolf)` 并链接；检查 `wolf::wolf` 目标、`WOLF_PLUGINS_DIR` 下的两个类别目录，以及安装包中不含 Support 头文件。把私有依赖放回公开链接接口时，配置期断言即失败 |
| 版本区间 | 写死旧修订 `1.0.1.2` 的歌手包，对 v1.0.1.3 / compat 1.0.1.0 的语言包加载通过（A68） |
| 归档 | `make-lang-release.py --verify` 解开归档后与源逐文件比对通过 |
| 宿主对接 | `test_HostFlow` 以真实迁移资源完成「取发音 → 用户改写 → 取音素与 onset」流程 |

**CI**（`.github/workflows/ci.yml`）：在 `ubuntu-24.04`（`x64-linux`）与 `windows-2022`
（`x64-windows`）上以 `onnx` 与 `tests` 特性安装依赖并构建；依次运行声明 lint 的自测、对
`packages/wolf-lang-zxx` 的 lint、CTest 与安装包消费检查。CI 不启用 `lang-packages` 特性，
依赖语言包数据的测试在 CI 中报告为跳过。

## 工具

| 脚本 | 用途 |
| :-- | :-- |
| `scripts/convert-g2p-packages.py` | 把旧套件转换为 spec 2.4 格式，并生成 `wolf/g2p-pinyin` |
| `scripts/make-lang-release.py` | 打包、计算 SHA512、重新生成端口的 `assets.cmake` 与 `version-string`；`--verify` 解开归档与源逐文件比对，不一致时拒绝打包 |
| `scripts/make-voicebank-fixture.py` | 由语言包生成歌手包形状的夹具，补齐语言包不提供的 S2P 与 onset（发布文档 §3.2） |
| `scripts/check-declarations.py` | 用 `docs/schemas/` 校验声明，并执行三项打包期 lint（含 `openSet` 推导）。发布脚本在打包前调用，有错误时拒绝打包 |

## 进度

里程碑：M0 调研 → M1 声明面 → M2 契约面 → M3 运行时面 → M3.5 发布链切片 → M4 真实解释器
→ M5 其余包铺开 → M6 收尾 → M7 版本口径与降级 → M8 稳定性、规范性与并行安全。
详见[实施计划](linguist-implementation-plan.md)。

**M4 已完成，M5 进行中，L6 已落地，M6 的 Q2 / Q3 / Q4 已落地，M7 与 M8 已全部落地。**

M5 剩余项：`eng` / `por` / `kor` / `ita` 四种语言已构成完整的语言闭包（A55）；其余八种中，五种
等待 P7 定名，`cmn` / `yue` / `jpn` 由歌手包闭合（A66），语言包侧无待办项。

## 审计发现的缺陷（已全部修复）

四轴审核（稳定性 / 向后兼容性 / 规范性 / 并行加载安全性）的结果。每条缺陷都有回归用例，且每个
用例都已在撤掉修复的情况下确认会失败。

| # | 缺陷 | 修复 |
| :-- | :-- | :-- |
| X1 | `BLAKE3::blake3` 位于公开 `LINKS`，下游 `find_package(wolf)` 因此失败 | 改为 `LINKS_PRIVATE`，并以配置期断言约束导出接口（A74） |
| X2 | 语言包 `compatVersion == version`，每次重新打包都破坏已发布的声库 | 一律取修订号为 0 的下沿；依赖未指向下沿时 lint 判为**错误**（A68） |
| X3 | 转换开始前到达的取消被 `start()` 清除 | `start()` 改为以 `exchange` 消费；`enrol()` 返回布尔值（A73） |
| X4 | 保留词拦截忽略用户锁定的音素层 | 拦截条件增加 `!word.locked.has_value()` |
| X5 | `startAsync` / `waitForFinished` 任务面失效；另有五处把 `quit()`/`wait()` 覆盖为空操作，使上游已实现的转发失效 | 提取 `wolf::ExecutiveTask`，十一个执行体均持有 `srt::ITask` 并转发（A75） |
| X6 | 资源缓存索引只增不减 | 插入时按阈值清理过期条目与路径记忆表（W8.9） |
| X7 | 契约违例在三层的处理不一致，执行体静默截断结果 | 短批次一律拒绝，与链变体和会话的处理一致（W8.7） |
| X8 | 失败的加载永久占用 pinyin 词典根，卸载也不释放 | 保留改为 RAII，随模块 `Configuration` 存活（A72） |
| B4 | ABI 规则未记录 | 确立规则 D1，并随本批公开结构体变更把版本升至 **0.1.0.0**；README 增加《Versioning and ABI》 |
| B5 | 声明根键白名单会拒绝框架将来新增的公共字段 | 改为警告，并新增 `wolf::logCategory()`（A77） |
| P3 | pinyin 引擎构造全程持有进程级锁，cmn / yue 的预热在进程内串行 | 锁只覆盖一次性的路径发布，构造移出锁（W8.5） |
| P5 | 两个 `LinguistSession` 共用一个 unit 既无检查也无测试 | 证明其安全并**取消该限制**（A78） |

**并行加载**：包加载的并行度恒为 1，由 spec 规定；wolf 不提供并行加载（实施计划 D2）。

## 未决项

| # | 内容 | 归属 |
| :-- | :-- | :-- |
| **P7** | `deu` / `fra` / `spa` / `rus` / `fil` 五种语言的 `scheme` 命名。其余七种已定（A27 / A45 / A49）；这五种的现有证据不足以确定有依据的名称，暂用 `ds` 占位 | 需要生态知识，**已搁置** |
| P8 | 发布公开。`lang-v0.1.2.0` 已于 2026-09-28 发布，仓库已公开，端口的匿名安装已验证 | **已解决** |
| Q7 | `srt::ITask::startAsync` 中的 `finish()` 在释放互斥量之后才调用 `notify_all()`；TSan 报告一处，但调用栈全部位于未插桩的 `libsynthrt.so` 内，倾向于误报，定论需要插桩构建的 synthrt | 上游 |
| P4 | lite 消费 wolf 语言包时的端口重复。lite 仓库在其 `scripts/vcpkg-ports/wolf-lang-packages` 保留本仓端口的副本，语言包发布新版本时整体复制 | **已解决**（lite 侧） |
| P5 | `.dspk` 单文件形态，待 synthrt main 支持解压后再议 | 上游 |
| P6 | 旧声库 manifest 的 `g2pPackageVersion` / `g2pPackages` 映射 | 迁移期 |
| Q1 | 未被 `languages` 引用的 linguist import：现定为编辑期 lint 警告，不判加载失败 | 已定，可复议 |
| Q5 | DiffSinger 歌手的 `configuration` 仍强制要求 `dict` 路径，属旧 G2P 栈的遗留 | 语言域迁移完成后退役 |
| Q6 | A11 已在 synthrt 分支 `onnxruntime-builds-uptake` 落地，wolf 的 `synthrt-main` 端口已改为固定该分支；并入 synthrt main 由上游决定 | 上游 |

**移植阻塞项 B1–B4 已全部排除**：B1 收敛为共享后端包 `wolf/g2p-pinyin`（A30–A32）；B2 不影响
任何存量资源；B3 与 B4 经复核不成立。B3 涉及的词典属于歌手包内容（A66）；对 B4，wolf 只依赖本仓
overlay 的 `synthrt-main`，从不引用共享 overlay 的 `synthrt`，该端口的取舍由 lite 决定。

## 外部基线

- synthrt：`synthrt-main` 端口固定 synthrt 的 `onnxruntime-builds-uptake` 分支（基于 synthrt main）；
  spec 2.4 的文本以本仓 `docs/ds-spec-2.4.md` 为准；转换源为 synthrt `refactor` 分支上由
  `convert-g2p-packages.py` 中固定的 `SOURCE_REF` 所指的提交。
- 随附插件：`linguistproviders/wolf`，推理解释器 `chain` / `pinyin` / `s2p` / `onset`，条件构建的
  `multig2p`（需要 dsinfer）与 `lua`（需要 LuaJIT），以及仅在测试构建中存在的桩。
  **发行的最小构建不含桩**，加载 `wolf/g2p-multi` 会失败（发布文档 §7.5）。桩另外无条件声明
  一个 `stub-miscount` 变体，供故意违反 provider ABI 的夹具使用（A79）。
- 新增依赖：`re2`（`\p{Han}` 等 Unicode 属性类，`std::regex` 不支持）、`cpp-pinyin` 与
  `luajit`；`onnxruntime-builds` 经清单特性 `onnx` 引入，**ORT 版本完全由该端口决定**，
  wolf 与 synthrt 两个仓库中均不再出现 ORT 版本号（A80）。
- 词典与模型的音素集不一致：逐包统计，`fil` 100%、`ita` 75%、`eng` 44%，其余六种为 0。后两者
  早于本轮工作，且已随 `lang-v0.1.0.0` 发布（A48 / A50）。
