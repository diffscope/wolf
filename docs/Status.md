# Wolf 项目状态

本文只记录**现状**：已实现的能力、所处位置与未决项。设计理由见 [LinguistDecisions.md](LinguistDecisions.md)（台账 A1–A80），变更历史见 git 历史。

## 当前定位

wolf 在 synthrt spec 2.4 之上建立开放的**语言域**：注册 `linguist` 贡献类别，发布语言组合契约与链推理契约，提供语言 Provider 插件与歌手语言 Pipeline，不把具体语言能力写入 synthrt 或 dsinfer。当前版本为 **0.2.0.0**（根 `CMakeLists.txt` 的 `project(VERSION)`）。

设计以 **[LinguistArchitecture.md](LinguistArchitecture.md)** 为索引，分七层展开（L0 框架不变量 / L1 语言域身份 / L2 语言组合契约 / L3 链推理契约 / L4 运行时装配 / L5 宿主接入 / L6 会话层），另有变体面与发布面两个正交平面。

## 文档

| 文档 | 覆盖范围 |
| :-- | :-- |
| [LinguistArchitecture.md](LinguistArchitecture.md) | 索引、层栈、框架硬约束（决策结论见台账） |
| [LinguistDomainContract.md](LinguistDomainContract.md) | L1 + L2 |
| [LinguistInferenceContract.md](LinguistInferenceContract.md) | L3 |
| [LinguistRuntime.md](LinguistRuntime.md) | L4 + L5 |
| [LinguistSession.md](LinguistSession.md) | L6（设计及落地时的修正，§12.1） |
| [LinguistVariants.md](LinguistVariants.md) | 变体面 |
| [LinguistDistribution.md](LinguistDistribution.md) | 发布面（渠道工程接线见资源仓文档） |
| [LinguistResourceCache.md](LinguistResourceCache.md) | 资源缓存 |
| [PluginInternals.md](PluginInternals.md) | 插件内部规范：骨架、解析与诊断规则、契约身份、演进检查清单 |
| [LinguistDecisions.md](LinguistDecisions.md) | 决策与论证 |

| [schemas/](schemas/) | 四个契约的 `exports` 与 `imports[].options` JSON Schema（spec 2.4 的必需项），以及两份共享定义 |

## 已实现的能力

**声明与加载**：`linguist` 贡献类别、`LinguistSpec` / `LinguistProvider` 插件体系；`WolfLinguistProvider` 解释 Level 1 声明，并提供 import validator、Spec Extension 与 Executive Factory；G2P / S2P / Onset 三份独立的推理契约，经三个固定 role 绑定；二元组（language, scheme）的命中校验在加载期执行。

**运行时**：歌手 → pipeline → linguist → 三个 inference 的执行体树；`depth` 截断与逐词锁定；端到端取消。转换运行中与**开始前**到达的取消请求均生效：`stop()` 向下传递，并由下一次 `start()` 消费。脚本变体另装计数钩子并关闭 JIT，响应时延的实测值只在 [LinguistRuntime.md](LinguistRuntime.md) §4.2 维护。资源缓存在执行体之间共享解析产物。

**L6 会话层** `wolf::LinguistSession`（`include/wolf/Session/LinguistSession.h`）：目录快照、三态就绪与双向缓存、按代管理的执行体池、保留标记处理、取消句柄。并发行为经 ThreadSanitizer 验证。

**契约开放位**：`exports.openSet` 表示清单是否为全集（域契约 §4.0），其取值由打包 lint 从链的兜底步推导；Onset 缺席时逐位为 `false`（域契约 §5.0）；空词判为 `skip`、含空白的词判为 `InvalidInput`，这两条规则集中在 `wolf::classifyLyric()`，由三个 G2P 变体共用。

**任务面**：十一个执行体均使用 synthrt 的统一 `srt::ITask` claim、generation、callback 和等待内核。同步与异步入口互斥，`stop()` 只影响已经建立的 claim，空闲取消不会污染下一次执行；callback 可以启动下一次执行、销毁执行体或被安全捕获异常。`wolf::ExecutiveTask` 只保留 body 与部分结果取消分类，`wolf::ExecutiveBase<Base, Input, Result>` 只保留停止标志、`onStop()` 和 `onSettled()` 钩子。各执行体实现 `runBatch()`，析构时调用 `quit()` 和 `wait()`。

wolf 新增公开 `wolf/Support/ErrorCode.h` 与独立 `wolf` error category。当前 `NoWorker` 用于异步 worker 创建失败，`ResourceInvalid` 用于资源解析器违反返回错误的约定；synthrt 的格式、参数与文件错误继续使用 synthrt 错误域。

**降级**：`linguist/s2p` 可以缺席，此时该语言的最大深度为 `Pronunciation`（域契约 §5.0.1）；`LanguageStatus` 提供 `maxDepth`、`hasSeparatePronunciationLayer`（发音层是否独立于音素层，域契约 §5.0.2、A81）与音素覆盖度，门限由宿主决定。唯一的例外是清单声明为全集而声库不支持其中任何音素的情形，会话直接将其判为不可用（A70）。

**变体**（收录表无缺口）：S2P `direct` / `dict` / `mapping` / `lua`；Onset `rule` / `lua`；G2P `pipe-chain` / `algo-pinyin` / `multig2p-onnx`；另有三种打标类型共用的 `Verifier`。`multig2p-onnx` 已用真实模型跑通，语言 id 实际传入模型（`test_MultiG2pInterpreter` 断言两种语言产生不同的读法）。

**资源**：13 个旧套件经转换管线转为 spec 2.4 格式，得到 12 个语言包与共享后端包 `wolf/g2p-multi`；转换管线另生成共享后端包 `wolf/g2p-pinyin`；连同手写的直通包 `wolf/lang-zxx`，共 **15 个包**，在完整构建与最小构建下均加载通过。当前发布（含各归档版本号与 SHA512）见 [LinguistDistribution.md](LinguistDistribution.md) §5.1 的当前发布表——完整事实只在彼处维护，本表不复制以免漂移。

## 构建与安装

- **依赖来源**：全部依赖经 vcpkg 安装，清单为 `scripts/vcpkg-manifest/vcpkg.json`。vcpkg 与 overlay 由 `scripts/vcpkg-workspace.cmake` 按 `scripts/vcpkg-sources.json` 检出到 `deps/`，依赖安装在 `deps/vcpkg/installed`。端口按序从两个 overlay 解析：本仓的 `scripts/vcpkg-ports/`（`synthrt-main`、`wolf-lang-packages`）优先于共享的 `stdware/vcpkg-overlay`。
- **清单特性**：`onnx`（必需，经 `synthrt-main[onnx]` 构建 dsinfer 与 ONNX 驱动）、`lang-packages`（经 `wolf-lang-packages` 端口安装已发布的语言包）、`tests`（Boost.Test）。
- **缓存变量**：`WOLF_LANG_PACKAGES_SOURCE` 指定解包后的语言包目录，非空时取代端口安装树；`WOLF_VOICEBANK_FIXTURE_SOURCE` 指定 `scripts/make-voicebank-fixture.py` 生成的歌手包夹具，只有 `test_HostFlow` 读取。缺少数据的测试以状态 77 退出，CTest 将其报告为跳过。`WOLF_DISABLE_LUAJIT` 显式选出最小构建。
- **安装布局**：插件安装在 `lib/synthrt-wolf/plugins/<category>/<name>/`，`<category>` 为 `inferenceinterpreters` 或 `linguistproviders`，构建树布局相同。宿主加入哪些目录见 [LinguistRuntime.md](LinguistRuntime.md) §1.1，CMake 包把安装的插件导出为导入目标，安装的插件清单见 [LinguistDistribution.md](LinguistDistribution.md) §7.1。CMake 包的版本兼容性为 `AnyNewerVersion`。
- **公开头与 Support**：安装的公开头为 `Api/`、`Linguist/`、`Session/`、`Support/` 与根头 `wolf/wolf_global.h`。`Support/` 是必须每进程唯一的资源缓存与日志类别，从 libwolf 以 `WOLF_EXPORT` 导出，属于公开 API。无状态帮助函数编入 `src/util` 下的静态库 `linguistutil` 与 `textutil`，执行体基类在只有头文件的 `executiveutil` 中（隐藏可见性，libwolf 与插件按需私有链接，不安装，不从任何二进制导出）。
- **ABI**：1.0 之前不作 ABI 保证；任何破坏 ABI 的变更与版本号变更同批提交（README《Versioning and ABI》）。

## 验证

| 项 | 结果 |
| :-- | :-- |
| 完整构建 | 2026-11-22：Windows Release 构建通过，CTest **41/41** 通过 |
| 最小构建（`-DWOLF_DISABLE_LUAJIT=ON`） | 当时的 15 个测试二进制（不含 `test_LuaInterpreters` 与 `test_MultiG2pInterpreter`）全部通过；现行该配置为 18 个，被排除的四个见 `src/tests/auto/CMakeLists.txt` 与 `src/tests/auto/plugins/CMakeLists.txt` 的注册条件，以及 `src/plugins/inferenceinterpreters/CMakeLists.txt` 对 lua 与 multig2p 两个插件的构建条件 |
| ThreadSanitizer | 全部用例通过。告警只出现在两个二进制中，且整条调用栈都位于**未插桩的第三方 `.so`** 内：`test_MultiG2pInterpreter`（`libonnxruntime.so`，既有告警）与 `test_LinguistRuntime`（`libsynthrt.so` 的 `ITask`；wolf 在本轮开始使用其 worker 路径后首次出现，见 Q7）。pinyin 并发预热与双会话两个用例均无告警 |
| Task TSan 复核 | 2026-11-18 以插桩的 synthrt、stdcorelib 与 stdcorelib-plugin 连续执行 `test_ITask` 100 次，12 个用例、70 条断言无报告。旧 `finish()` 通知路径已不存在，Q7 关闭 |
| 端口安装树 | `vcpkg install wolf-lang-packages[...]` 安装出 15 个包，当时的 17 个测试二进制以该树为数据源通过（A67） |
| 下游消费 | CI 在安装后构建 `.github/consumer`：按 README 的写法 `find_package(synthrt-wolf)` 并链接；检查 `synthrt::wolf` 目标与安装包中不含 Support 头文件，检查导入的插件目标，并运行程序，检查这些目标所在的两个类别目录。把私有依赖放回公开链接接口时，配置期断言即失败 |
| 版本区间 | 消费者夹具钉在修订 2，由安装的修订 3 供给：解析结果版本为 `1.0.0.3`，请求侧不是该版本（A68；用例为 `test_LinguistLoad.cpp` 的 `ServesAConsumerPinnedToAnOlderRevision`） |
| 归档 | `make-lang-release.py --verify` 解开归档后与源逐文件比对通过 |
| 宿主对接 | `test_HostFlow` 以真实迁移资源完成「取发音 → 用户改写 → 取音素与 onset」流程 |

**CI**（`.github/workflows/ci.yml`）：在 `ubuntu-24.04`（`x64-linux`）与 `windows-2022`（`x64-windows`）上构建；Linux 端以 `onnx`、`tests` 与 `lang-packages` 特性安装依赖，Windows 端只启用 `onnx` 与 `tests`。随后依次运行声明 lint 的自测、对 `build/test-fixtures/wolf-lang-zxx`（由 CI 的 `Generate the test fixture packages` 步骤产出）的 lint、发布 pin 与仓内副本的一致性检查（`check-release-assets.py`）、CTest 与安装包消费检查。`lang-packages` 是唯一**下载并校验**语言包发布资产的步骤，`test_ConvertedPackages` 因此只在 Linux 端真跑；`test_MultiG2pInterpreter`、`test_MultiG2pMaxLen` 与 `test_HostFlow` 需要 CI 不提供的共享 G2P 模型后端（该端口只在非缺省的 `multi` 特性下安装）或声库夹具，两端都报跳过（`.github/workflows/ci.yml:21-31,65-79,92-96`）。

## 工具

| 脚本 | 用途 |
| :-- | :-- |
| `scripts/convert-g2p-packages.py` | 把旧套件转换为 spec 2.4 格式，并生成 `wolf/g2p-pinyin` |
| `scripts/make-lang-release.py` | 打包、计算 SHA512、重新生成端口的 `assets.cmake` 与 `version-string`；`--verify` 解开归档与源逐文件比对，不一致时拒绝打包 |
| `scripts/make-voicebank-fixture.py` | 由语言包生成歌手包形状的夹具，补齐语言包不提供的 S2P 与 onset（发布文档 §3.2） |
| `scripts/make-test-fixtures.py` | 生成夹具包（41 包 / 141 文件），供当时的 17 个 ctest 用例中的 7 个读取；`--check` 与已有目录逐字节比对，不一致时非零退出 |
| `scripts/check-declarations.py` | 用 `docs/schemas/` 校验声明，并执行四项打包期 lint（含 `openSet` 推导与 S2P 表内容级恒等）。发布脚本在打包前调用，有错误时拒绝打包 |

夹具包不随仓库分发：生成到构建目录后由 `WOLF_TEST_FIXTURES_SOURCE` 指过去，数据缺失时相关用例以状态 77 报跳过（ctest 记为未运行）。`scripts/test_check_declarations.py` 未配置该变量时会现场生成到临时目录。

## 进度

里程碑：M0 调研 → M1 声明面 → M2 契约面 → M3 运行时面 → M3.5 发布链切片 → M4 真实解释器 → M5 其余包铺开 → M6 收尾 → M7 版本口径与降级 → M8 稳定性、规范性与并行安全。实施计划已归档到 `.cache/wolf/20261122-1720-LinguistImplementationPlan.md`。

**M4 已完成，M5 进行中，L6 已落地，M6 的 Q2 / Q3 / Q4 已落地，M7 与 M8 已全部落地。**

M5 剩余项：`eng` / `por` / `kor` / `ita` 四种语言已构成完整的语言闭包（A55）；其余八种中，五种等待 P7 定名，`cmn` / `yue` / `jpn` 由歌手包闭合（A66），语言包侧无待办项。

## 审计发现的缺陷（已全部修复）

四轴审核（稳定性 / 向后兼容性 / 规范性 / 并行加载安全性）的结果，按组列出；单条缺陷的论证与决策编号见 [LinguistDecisions.md](LinguistDecisions.md)。每条缺陷都有回归用例，且每个用例都已在撤掉修复的情况下确认会失败。

| 组 | 缺陷 | 处置（现行结论） |
| :-- | :-- | :-- |
| 任务面与取消 | X3、X5、S6 | `start()` 以 `exchange` 消费取消位，`enrol()` 返回布尔值；十一个执行体统一持有 `srt::ITask`（`wolf::ExecutiveTask`），不再手写任务面；子执行体创建失败不再停留在 `Running` |
| 链接接口与版本口径 | X1、X2、B4、B5 | 私有依赖移出公开链接接口，并以配置期断言约束（CI 另经 `.github/consumer` 检查安装包）；语言包 `compatVersion` 一律取修订号下沿，依赖未指向下沿时 lint 判为错误；确立 ABI 规则并随公开结构体变更升至 **0.1.0.0**（后续 L1 发音层查询的公开变更已随批升至 **0.2.0.0**）；声明根未知键改为警告 |
| 资源、契约与并发 | X4、X6、X7、X8、P3、X9 | 保留词拦截不吞用户锁定的音素层；缓存条目按阈值回收；契约违例在三层统一为整批拒绝；pinyin 词典根的保留改为 RAII 随模块 `Configuration` 存续，临界区收窄（构造移出进程级锁）；两个会话共用一个 unit 经测试证明安全并**取消该限制** |

**编号 ↔ 决策台账**：X1→A74，X2→A68，X3→A73，X5→A75，X8→A72，B4→D1，B5→A77，X9→A78；X4、S6 与 P3 无对应 A 编号，处置见本表；X6、X7、P3 的处置见实施计划 W8.9、W8.7、W8.5。**X9 即原编号 P5**，与未决项中的 P5（`.dspk` 形态）不是同一条目，故在缺陷表中改入 X 系列。

**并行加载**：包加载的并行度恒为 1，由 spec 规定；wolf 不提供并行加载（实施计划 D2）。

## 未决项

| # | 内容 | 归属 |
| :-- | :-- | :-- |
| **P7** | `deu` / `fra` / `spa` / `rus` / `fil` 五种语言的 `scheme` 命名。其余七种已定（A27 / A45 / A49）；这五种的现有证据不足以确定有依据的名称，暂用 `ds` 占位 | 需要生态知识，**已搁置** |
| P5 | `.dspk` 单文件形态，待 synthrt main 支持解压后再议 | 上游 |
| P6 | 旧声库 manifest 的 `g2pPackageVersion` / `g2pPackages` 映射 | 迁移期 |
| Q1 | 未被 `languages` 引用的 linguist import：现定为编辑期 lint 警告，不判加载失败 | 已定，可复议 |
| Q5 | DiffSinger 歌手的 `configuration` 仍强制要求 `dict` 路径，属旧 G2P 栈的遗留 | 语言域迁移完成后退役 |
| Q6 | A11 已在 synthrt 分支 `onnxruntime-builds-uptake` 落地，wolf 的 `synthrt-main` 端口已改为固定该分支；并入 synthrt main 由上游决定 | 上游 |

**移植阻塞项 B1–B4 已全部排除**：B1 收敛为共享后端包 `wolf/g2p-pinyin`（A30–A32）；B2 不影响任何存量资源；B3 与 B4 经复核不成立。B3 涉及的词典属于歌手包内容（A66）；对 B4，wolf 只依赖本仓 overlay 的 `synthrt-main`，从不引用共享 overlay 的 `synthrt`，该端口的取舍由 lite 决定。

## 外部基线

- synthrt：`synthrt-main` 端口固定 synthrt 的 `onnxruntime-builds-uptake` 分支（基于 synthrt main）；spec 2.4 的文本以 synthrt main 分支的 [`docs/ds-spec-2.4.md`](https://github.com/diffscope/synthrt/blob/main/docs/ds-spec-2.4.md) 为准；转换源为 synthrt `refactor` 分支上由 `convert-g2p-packages.py` 中固定的 `SOURCE_REF` 所指的提交。
- 随附插件：`linguistproviders/wolflinguist`，推理解释器 `chain` / `pinyin` / `s2p` / `onset`，条件构建的 `multig2p`（需要 dsinfer）与 `lua`（需要 LuaJIT），以及仅在测试构建中存在的桩。**发行的最小构建不含桩**，加载 `wolf/g2p-multi` 会失败（发布文档 §7.5）。桩另外无条件声明一个 `stub-miscount` 变体，供故意违反 provider ABI 的夹具使用（A79）。
- 新增依赖：`re2`（`\p{Han}` 等 Unicode 属性类，`std::regex` 不支持）、`cpp-pinyin` 与 `luajit`；`onnxruntime-builds` 经清单特性 `onnx` 引入，**ORT 版本完全由该端口决定**，wolf 与 synthrt 两个仓库中均不再出现 ORT 版本号（A80）。
- 词典与模型的音素集不一致：逐包统计，`fil` 100%、`ita` 75%、`eng` 44%，其余六种为 0。后两者早于本轮工作，且已随 `lang-v0.1.0.0` 发布（A48 / A50）。
