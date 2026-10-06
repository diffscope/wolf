# wolf 语言域实施方案

本文规定**实施顺序与验收判据**：把设计文档中的工作项编排为有依赖关系的里程碑，并说明每一步的
排期理由与完成判据。

设计依据见 [linguist-architecture.md](linguist-architecture.md) 及其分层文档；决策编号（A*）见
[linguist-decisions.md](linguist-decisions.md)。**本文不重复设计结论**，只记录工作内容、实施顺序与
验收方式。

---

## 1. 排序原则

共四条（§1.1–§1.4），另附转换产物的约束（§1.5），按重要性排列。后文每个里程碑的位置都可以追溯
到其中一条。

### 1.1 骨架先行，真实实现后置

**先用桩解释器跑通「歌手 → pipeline → 语言 → 三个推理执行体」整条链，再逐个替换为真实变体。**

理由：真实解释器占全部工作量的大部分，且是唯一带外部依赖的部分（cpp-pinyin、ONNX 驱动、LuaJIT、
19 MB 模型）。声明面与执行体树的错误若推迟到真实解释器阶段才暴露，调试时须同时面对两层未知因素。

可行性已核实：`InferenceInterpreterPlugin.h` 是 synthrt main 的**公开头文件**
（`synthrt/include/synthrt/SVS/InferenceInterpreterPlugin.h`），wolf 可以实现一个测试专用的
推理解释器插件，为三份契约各提供一个平凡实现。

### 1.2 正向验证与负面用例的数据来源

**正向路径使用真实语言包**：转换后的 `wolf/lang-zxx` 从 M1 起即作为测试底座，M3.5 起经真实的
release 与 vcpkg 端口安装获取。理由：测试对象即将要发布的产物，格式迁移与发布链的问题在最早期
暴露，且不产生一次性的夹具工作。

**引导包选用 `wolf/lang-zxx`**（A25）：该包无词典、无模型、无许可问题，却完整覆盖
「`desc.json` + 语言声明 + 两个推理模块 + 歌手映射」全链路。因此「使用真实包」的要求
**不必**等待 `scheme` 取值定案、B3（后已排除，A66）或 19 MB 模型，只需把内容成本最低的包排在
最前。

**负面用例仍使用手写夹具**：`language` 形状非法、`languages` 键与目标 `language` 不符、缺少
`linguist/s2p`、`exports.phonemes` 元素重复等。这些包不可能作为真实包发布，只能作为仓内测试数据。

因此 M1–M3 只依赖 `lang-zxx` 的转换，不依赖其余 12 个包，也不依赖任何真实解释器。

### 1.3 阻塞项的排期

B1（cpp-pinyin 全局词典路径）与 B2（Onset 字面段死代码）各自只阻塞一个变体，在该变体的工作包
中解决。但 **B2 的存量资源清点是只读调研，其结果决定修复是否属于破坏性变更**，这会影响发布节奏，
因此提前到 M0 并行进行。

### 1.4 synthrt 改动与端口的排期

本方案对 synthrt 的唯一改动是为 `SingerCategory` 增加两个字段（A11）。

该改动**不阻塞 M1 开工**（A26）：M1 中约七成用例位于 wolf 自有的 `linguist` 类别内，无须修改
synthrt；歌手侧的少量用例由**桩 singer provider 经 `configuration` 过渡承载**，读取集中在单个函数
`readSingerLanguages()` 中，A11 落地后只需修改一处。

A11 原定的落地路径是**端口携带补丁**（`vcpkg_from_github(... PATCHES ...)`）：语义精确，CI 可
复现，无须等待上游合并。端口因此从 M5 的收尾项提前为 A11 的承载工具，但仍不阻塞本地开发。
[现状：A11 已在 synthrt 分支 `onnxruntime-builds-uptake` 落地；`synthrt-main` 端口以
`vcpkg_from_git` 固定该分支，不携带补丁]

### 1.5 转换产物与源提交的固定

语言资源包括 13.3 万行词典与 19 MB 模型。转换在 gitignored 的 `build/lang-packages/` 中进行，成品
直接发布到 release，`assets.cmake` 放在 overlay 端口中；wolf 的 git 只保留转换脚本、`lang-zxx` 的
零资源创作源与负面夹具（A28）。

相应的**溯源要求不可省略**：脚本必须固定 `origin/refactor` 的源提交（`convert-g2p-packages.py` 中的
`SOURCE_REF`），首次 release notes 记录该提交。否则该分支将来被删除后，源资源的唯一副本只存在于
release zip 中，转换无法复现。

---

## 2. 里程碑总览

| # | 里程碑 | 产出 | 依赖 | 退出判据 |
| :-- | :-- | :-- | :-- | :-- |
| **M0** | 前置调研 | B2 清点结果（+ 两项决策，均不在关键路径） | — | 见 §3 |
| **M1** | 声明面 | 语言身份、歌手映射、加载期裁决、`lang-zxx` 创作 | — | `lang-zxx` 经 `DataOnly` 与 `Load` 双模式加载通过；负面用例全部按预期失败 |
| **M2** | 契约面 | 三份契约的类型化 payload + 桩解释器 | M1 | 桩插件被 synthrt 的 `inference` 类别发现并加载 |
| **M3** | 运行时面 | 执行体树打通 + 资源缓存 | M2 | 一条最小链跑出音素序列；同资源两执行体读盘 1 次 |
| **M3.5** | 发布链垂直切片 | `lang-zxx` 的 release + 端口 + 测试接线 | M3 | 干净环境经 `vcpkg install` 取得 `lang-zxx` 并加载通过 |
| **M4** ✅ | 真实解释器 | 九个变体从 refactor 移植 | M3 | 每变体有对照用例，输出与旧栈逐字节一致（除已定案的行为修正） |
| **M5** | 其余包铺开 | 12 语言包 + 2 后端包、端口 features 铺开 | M4、M0-d2 | `vcpkg install` 后测试取得全部真实包数据并通过——**已达成**（A67）。四种语言已闭包（A55），`cmn`/`yue`/`jpn` 由歌手包闭合（A66） |
| **M6** | 收尾 | JSON Schema、lint 工具、文档回写 | M5 | 见 §3 |

**关键路径**：M1 → M2 → M3 → M3.5 → M4（S2P → Onset → Verifier → algo-pinyin → pipe-chain →
multig2p）→ M5。**M0 的两项决策均不在关键路径上**：d1 由 A26 的过渡路径绕开，d2 到 M5 才构成
阻塞。因此 M1 可以立即开工。

---

## 3. 逐里程碑

### M0 — 前置调研

**没有任何一项阻塞 M1。** 两项决策各自只阻塞后段的里程碑，一项只读调研可以立即并行进行；三者现状：

- **d1**（A11，是否改 synthrt `SingerCategory`，以及是否走端口 patch 承载）：M1 由 A26 的过渡路径
  绕开，M3.5 起才构成依赖；**已落地**——端口以 `vcpkg_from_git` 固定 synthrt 分支
  `onnxruntime-builds-uptake`，不携带补丁。
- **d2**（`deu` / `fra` / `spa` / `rus` / `fil` 五种语言的 `scheme` 命名，即 P7）：阻塞 M5 的收尾，
  不影响 M1–M4；其余七种已定（A27 / A45 / A49），五种**已搁置**（Status 的未决项）。
- **r1**（B2 存量清点：遍历四个仓库的 onset rule JSON，统计是否使用字面音素段）：结论已写回
  [linguist-variants.md](linguist-variants.md) §3.3——无存量资源使用字面段，B2 的修复因此不属于
  破坏性变更。

**退出判据**：三项结论均已记录在文档中（P7 除外，已搁置）。

---

### M1 — 声明面

**目标：不编写推理代码，跑通「一个语言包 + 一个歌手」的声明加载全流程。**（W1.1–W1.7 已全部落地）

交付面：L1 身份字段与形状校验（`linguist` 白名单 +2，A7）；歌手侧两个类别追加字段（A11，M1 期间由
桩 singer provider 经 `configuration` 过渡承载，读取集中于 `readSingerLanguages()`，A26）；L2 裁决改读
`languages` 映射（A11 / A12）；负面夹具与 `lang-zxx` 的创作（零资源、入库为 source，A25）。**二元组
命中校验**（域契约 §5.3）依赖类型化的 `exports.languages`，因此属于 M2 的 W2.5——M1 只校验 `languages`
映射的结构、目标类别以及 `language` 字段的一致性，命中校验留作 TODO。

**负面用例清单**（每条都应加载失败，且错误文本可定位）：

- `language` 不是 `[a-z]{3}`；`scheme` 形状非法；
- `languages` 的值指向不存在的 role；指向的 import 目标不是 linguist 贡献；
- `languages` 的键 ≠ 目标声明的 `language`；
- `defaultLanguage` 不是 `languages` 的键；`languages` 非空却缺 `defaultLanguage`；
- linguist 缺 `linguist/g2p` 或 `linguist/s2p`；
- `configuration` 省略（现行按 A9 判失败）；`exports.phonemes` 缺失/元素重复/路径所指非数组。

**同时必须验证的「不应失败」项**：同一 Package 内 `cmn-pinyin` 与 `cmn-bopomofo` 并存可加载
（多注音体系）；歌手只映射其中一个；未被 `languages` 引用的 linguist import 不判失败（Q1）。

**退出判据**：夹具在 `DataOnly` 与 `Load` 两种模式下都通过；`LinguistSpec::language()/scheme()`
在 `DataOnly` 下可读；上列反例逐条按预期失败。

---

### M2 — 契约面

**目标：三份契约的 C++ 面成型，并有一个能被框架真正加载的桩解释器。**（W2.1–W2.5 已全部落地）

交付面：`include/wolf/Api/Inferences/Common/1/CommonApiL1.h` 的 `LanguageScheme`；`G2P/1`、`S2P/1`、
`Onset/1` 从「仅常量」补齐为 Exports / ImportOptions / RuntimeOptions / InitArgs / StartInput /
Result / Executive；三份 `RuntimeOptions` 由构造参数接收目标变体、**不得硬编码**（A15）；桩解释器插件
`src/plugins/inferenceinterpreters/stub/` 是对 A1 的**首次实际验证**（插件能否被 `inference` 类别的
factory 发现、IID 是否匹配、执行工厂能否创建）；W2.5 补上 M1 挂的二元组命中校验。

**退出判据**：桩插件放进宿主为 `inference` 配置的搜索路径后，夹具包的三个推理模块能完成 Probe 选择
与 Acquire；`exports.languages` 类型化解析通过；二元组不匹配的夹具加载失败。

---

### M3 — 运行时面（穿刺验证）

**目标：三级执行体树实际运行。这是整个 L4 的基础，必须在铺开真实解释器之前验证。**（W3.1–W3.5 已全部落地）

交付面：provider 侧的具体执行体类与 `createChild` 三跳（注入 `(language, scheme)` 与目标变体，A15）；
单任务面（`start` / `startAsync` / `stop` / `waitForFinished` / `state`）与逐词分层锁定、`depth` 截断
（A17）；停等聚合（`quit()` 只停本节点在飞任务，`wait()` 同理，子节点交监督树，不得自行递归）；词级
`hitStage` 与执行体级 `binding()` / `g2pContribution()`；`wolf::ResourceCache`。**缓存不能推迟**：A14
规定并发通过创建多个执行体实现，若在真实解释器完成后再补，每个变体都要先实现一遍无缓存的资源装载。

**退出判据**（穿刺用例）：

1. 夹具歌手 → `createPipeline` → `createLinguist("cmn")` → 一条 3 词的输入跑出 phonemes 与 onsets；
2. 同一歌手并发开 3 个 `LinguistExecutive`，互不干扰；
3. **同一资源被两个执行体使用时读盘次数 = 1**（缓存的可执行验收）；
4. 提前 `delete` 子执行体后父仍可用；包释放时 quit/wait 顺序正确（quit 自顶向下、wait 自底向上）。

---

### M3.5 — 发布链垂直切片

**目标：用内容成本最低的包先跑通「打包 → release → 端口 → 消费」整条链。**（W3a.0–W3a.4 已全部落地）

交付面：本仓 overlay `scripts/vcpkg-ports/` 与有序两项的 `overlay-ports`（A29）；`synthrt-main` 端口
（固定 synthrt 分支 `onnxruntime-builds-uptake`，`dsinfer` 由 feature `onnx` 启用）；`lang-zxx` 的打包
（zip「解包即目录」，A18，配 `manifest.json`，tag `lang-v<bundleVersion>`）、发布脚本与
`wolf-lang-packages` 纯数据端口（此时只有 `zxx` 一个 feature）；由清单 feature 控制的测试接线，未启用
时 `SKIP`，备用入口为缓存变量 `WOLF_LANG_PACKAGES_SOURCE`。发布链的问题与内容无关，用一个零资源的包
先暴露它们，比等 12 个包全部转换完成后再一次性验证更安全。

供 lite 使用的 `wolf` 端口**不在本方案范围内**：wolf 仓库的 overlay 只承载 wolf 自身使用的端口，
lite 所需的端口由 lite 仓库自己的 vcpkg 配置提供（A29 的作用范围原则）。

**退出判据（已达成，A67）**：`vcpkg install --x-feature=lang-packages` 后，wolf 的测试从端口安装树
取得 `wolf/lang-zxx` 并完成 `Load`；不带该 feature 时不下载任何数据且测试 `SKIP`。

---

### M4 — 真实解释器

**移植来源见 [linguist-variants.md](linguist-variants.md) §8（一律取 synthrt `origin/refactor` 上
由 `convert-g2p-packages.py` 中固定的 `SOURCE_REF` 所指的提交）。** 顺序按外部依赖由少到多排列，
七个工作包均已落地：

1. **S2P 三变体** `dict` / `direct` / `mapping`；
2. **Onset `rule`**——按目标语义重写；B2 清点确认无存量资源使用字面段，不属于破坏性变更；
3. **`Verifier` 共享组件**——`wolf::Verifier`（由 `re2` 支持 `\p{Han}` 等属性类），三种类型齐全，
   未知类型在读入期报错；
4. **G2P `algo-pinyin`**——**B1 由 A30 的共享后端包 + A31 的进程级仲裁器解决**：cmn / yue 经一个
   词典根并存，异根即加载失败；引擎按 `languageMap` 由绑定二元组选（A23 保持）；
5. **G2P `pipe-chain`**——五种 step 齐备，`lang-zxx` 不再依赖桩；`model` 步按极大连续段分批（A33），
   `dict` 步采用严格 TSV 与 `word(n)` 归并（A34）；
6. **G2P `multig2p-onnx`**——驱动由宿主注册为 Runtime Service（A36）；解码为贪心，beam 相关键的取值
   受限（A37）；`languageMap` 必选并与 `exports.languages` 对账；已用真实模型端到端跑通；
7. **两个 `lua` 变体**——已落地（A39）；四个仓库中没有任何生产用 Lua 脚本资源，因此该项用于补齐变体
   表，而非支持存量资源。

**可并行**：1 与 2 互不依赖；3 完成后，4 与 5 可以并行；6 依赖驱动就绪，其调研可最先启动。

**每个变体的统一验收**：

- 以 refactor 的对照输入产生与旧栈**逐字节一致**的输出，**已定案的行为修正除外**（这些修正必须
  逐条列出，且各有一个专门用例）：Onset 字面段、`fallback` 的 error 语义、multig2p 未映射语言、
  chain 打标的 `array`/`dict` 型；
- 拒绝未知 `configuration` 键；
- 资源装载经过 `ResourceCache`。

---

### M5 — 包与端口

（W5.0–W5.3 已全部落地）交付面：**转换脚本**——从 `origin/refactor` 的固定提交（`SOURCE_REF`）读入，
输出到 gitignored 的 `build/lang-packages/`，产出 `manifest.json` 与 `assets.cmake`，**产物一律不进入
git**（A28）；**其余 12 个语言包与后端包 `wolf/g2p-multi`**——每个包新写 `desc.json`、`linguist.json`
与三个推理模块的 `exports`；**发布脚本铺开**——按语言打包 zip（解包即目录，A18）并生成 `manifest.json`
与 `assets.cmake`，tag `lang-v<bundleVersion>`，当前发布 `lang-v0.1.2.0` 已包含 `wolf/lang-eng`；
**端口 features 铺开**——与全部语言一一对应，随 release 在本仓一并提交。

W5.1 是本里程碑的主要工作量，也是唯一无法从旧栈复制的部分：旧栈中 S2P 与 Onset 是声库 manifest 的
字段而非模块。该工作量已随 B3 排除而大幅缩小（九种语言使用 `direct`，`cmn` / `yue` / `jpn` 的音节词典
属于歌手包内容，A55 / A66），**但排期时仍不得按「12 个包 × 修改键名」估算**。

**退出判据（已达成，A67）**：`vcpkg install --x-feature=lang-packages` 后，wolf 的包加载测试取得
真实语言包数据并通过；不带该 feature 时不下载任何数据且测试 `SKIP`。

---

### M6 — 收尾

四项收尾工作（W6.1–W6.4）：`exports` 与 `imports[].options` 的 JSON Schema 发布物（spec 2.4:596-602
的硬性要求，Q3）；打包期 lint 工具（域契约 §12 中只依赖声明本身的清单：未引用的 import、贡献 ID
书写惯例与 S2P 表内容级恒等，A4 / Q4 / A81）；文档回写
实测值（`stop()` 的词边界响应时延建议值，Q2；B2 清点结论；各语言 `scheme` 定案）；旧栈遗留退役
（DiffSinger 歌手 `configuration` 的 `dict` 键，Q5；旧声库 manifest 的 `g2pPackageVersion` 映射，P6）。

### M7 — 版本口径与降级

本里程碑源于一次审计：A54 为语言包规定的 `compatVersion` 口径会使每一次重新打包都破坏全部已发布
的声库（A68）。修复后同时划定降级的边界：A69 否决了运行期能力解析，因此版本兼容只剩
`compatVersion` 一个机制，该机制必须严格执行。

（W7.1–W7.5 已全部落地）现行口径：语言包的 `compatVersion` 一律取修订号下沿
（`convert-g2p-packages.py` 删除 `compat_floor(...) if is_backend else version` 分支），
`check-declarations.py` 把「依赖的版本不等于目标包的 `compatVersion`」升为**错误**并给出应写的值，
`make-lang-release.py` 据此拒绝打包；`linguist/s2p` 放宽为 `0..1`（`WolfImportValidator` 的下界只留
`linguist/g2p`，域契约 §5.1），缺席时 `maxDepth` 取 `Pronunciation`，`probe()` 保持无副作用；
音素覆盖度经 `LinguistSession::setSingerPhonemes()` 与 `LanguageStatus` 的 `coverage` /
`coverageKind` / `missingPhonemes` 提供，`coverage == 0 && !openSet` 判为 `Unavailable`，其余比例只
报告数值、不设门限（A68 / A70 / A71）。验收侧的断言值取自 Status 中已统计的比例：`fil` 100% /
`ita` 75% / `eng` 44%；`openSet` 为真时 `coverageKind` 取下界而非精确值。

**退出判据（已达成）**：打包修订号提升到 3 并重新打包 bundle 0.1.2.0 之后，写死旧版本 `1.0.1.2` 的
歌手包仍加载通过，全部 17 个测试二进制通过。（以上为里程碑当时值：现行打包修订号为 4，现行版本区间
用例见 [Status.md](Status.md) 的「版本区间」行，现行包版本见
[linguist-distribution.md](linguist-distribution.md) §5.1。）

---

### M8 — 稳定性、规范性与并行安全

来源为对现有实现的四轴审核（稳定性 / 向后兼容性 / 规范性 / 并行加载安全性），条目编号沿用
`Status.md` 的 X / B / P 系列。十个工作包已全部落地，**缺陷的现行处置与「X 编号 ↔ A 编号」对照见该文
的缺陷表**，此处只保留编号对应与验收要点：

- **W8.1（X1，A74）** 修复下游消费：`blake3` 归入 `LINKS_PRIVATE`，并以配置期断言约束导出接口；
  空白消费者工程按 README 的写法 `find_package`、链接并运行通过，把 `blake3` 放回公开接口时配置
  立即失败（CI 现以 `.github/consumer` 执行该检查）；
- **W8.2（X8，A72）** pinyin 词典根保留的事务化：保留改为 RAII、由模块 `Configuration` 持有；
  `test_PinyinBackend` 断言失败的加载之后与卸载之后另一个根均可用；
- **W8.3（X3，A73；同批修 S6）** 取消语义修正：`start()` 以 `exchange` 消费取消位，`enrol()` 返回
  布尔值；`test_LinguistSession_HonoursACancelledToken` 改为实际断言；
- **W8.4（X4）** 保留词不吞锁定层：拦截条件加 `!word.locked.has_value()`；
- **W8.5（P3；与 W8.2 同一文件）** pinyin 临界区收窄：目录检查与引擎构造移出锁，根的设定使用
  `once_flag` 与 release/acquire 语义；并发预热 `cmn` / `yue` 的耗时接近两者的最大值而非总和；
- **W8.6（X5，A75）** 十一个执行体各持有一个 `srt::ITask` 成员并转发（参照 dsinfer 的
  `DurationInference`），转发与停止标志统一收入 `wolf::ExecutiveBase`；
- **W8.7（X7）** 契约违例处理统一：`LinguistExecutiveImpl` 对短批次按 `chain::convertRun` 的规则
  整批标记错误，不再静默截断；
- **W8.8（B5，C2）** 声明根键白名单：未知的框架公共字段由拒绝改为忽略并记录警告，类别层字段仍严格
  校验；
- **W8.9（X6）** 缓存条目回收：过期的 `weak_ptr` 在同键命中时或按阈值清理，反复 load / release 后
  `entries` 不单调增长；
- **W8.10（X9，A78）** 取消「一个 unit 一个会话」的限制，由
  `test_LinguistSession_TwoSessionsShareOneUnit` 覆盖。

**验收**：以上回归用例均已在撤掉对应修复的情况下确认会失败（见 `Status.md` 的缺陷表与验证节）。

**跨里程碑规则**

- **D1 — ABI**：`LanguageStatus` / `LanguageEntry` / `SingerEntry` / `SingerRef`
  以及全部 `Api::*L1` 的 payload 都是头文件中的值类型，增加字段即破坏 ABI。W7.4 / W7.5 与 M8 的
  任何公开结构体变更**必须与一次 `WOLF_VERSION` 变更同批发布**，并在 README 中写明 1.0 之前不作
  ABI 保证。`LinguistSession` 与 `CancelToken` 采用 pimpl，是仅有的两个不受影响的类型。[现状：
  版本已随本轮公开结构体变更升至 0.2.0.0（此前为 0.1.0.0），CMake 包的版本兼容性为
  `ExactVersion`，README 已增加《Versioning and ABI》]
- **D2 — 并行加载**：**包加载的并行度恒为 1**，由 spec §加载事务规定（「load 与 release 事务
  必须串行执行」），synthrt 以 `SynthUnit::openPackage` 全程持有 `loadMutex` 强制执行。wolf 不提供
  并行加载。语言域的并行发生在执行体构建与转换这一层：`warm()` 线程安全且按语言独立，宿主可以
  自行并行调用；W8.5 之后 pinyin 不再是例外。

---

## 4. 风险与先验证项

按验证收益由早到晚排列：越早验证，收益越大。

| 风险 | 暴露时机 | 前置验证 |
| :-- | :-- | :-- |
| **A1 的类别归属不成立**（桩插件无法被 `inference` 类别发现或加载） | M2 | 由 W2.4 验证。若不成立，A1 须回退为 wolf 自行注册类别，L3/L4 全部重新设计 |
| **`createChild` 三跳不可行**（linguist spec 的 import 无法取得 inference 类别的执行工厂） | M3 | W3.1 的穿刺用例。这是 L4 的基础 |
| ~~B1 无解~~ | ~~M4-4~~ | **已消解**。实测 `setDictionaryPath` 仍存在，但该全局量只在构造期被读取一次，且两个引擎使用同一个根，因此拆包即可解决；仲裁器处理根不一致的情形（A30 / A31） |
| ~~B2 是破坏性变更~~ | ~~M4-2~~ | **已消解**：清点确认无存量资源使用字面段 |
| ~~B3 工作量超预期~~ | ~~M5~~ | **已消解**：九种语言的 G2P 已产出空格分隔的音素，使用 `direct` 并省略 Onset 即可，四种已构成闭包（A55）；`cmn`/`yue`/`jpn` 的音节词典属于歌手包内容，语言包侧无待办项（A66） |
| **`.dspk` 与目录形态的分歧**（spec 与实现不一致，A18） | M5 | 已登记；若 synthrt main 日后支持解压，只更换打包方式，模型不变 |
| **`compatVersion` 下沿是版本兼容的唯一机制**：A69 否决晚绑定后不存在第二道保障 | 每一次重新打包 | W7.2 把 lint 升为错误，发布脚本据此拒绝打包；W7.1 的回归测试必须使用「旧夹具 + 新包」的组合，因为夹具与包同批生成时无法发现该失效（A68） |
| **wolf 自身的进程级状态缺少约束**（pinyin 词典根是已知的一处） | 任何一次失败的加载 | W8.2 已把它绑定到配置对象。新增进程级状态时必须同样绑定到某个随事务销毁的对象，因为 spec §加载事务不允许 Acquire 留下不可撤销的副作用（A72） |
