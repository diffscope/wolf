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

**没有任何一项阻塞 M1。** 两项决策各自只阻塞后段的里程碑，一项只读调研可以立即并行进行。

| 项 | 内容 | 阻塞 |
| :-- | :-- | :-- |
| **d1** | 是否接受「改 synthrt `SingerCategory`」（A11），以及是否走端口 patch 承载 | M3.5 起（M1 由 A26 过渡路径绕开） |
| **d2** | `deu`/`fra`/`spa`/`rus`/`fil` 五种语言的 `scheme` 命名（P7）；其余七种语言已定，见 A27 与 A49 | M5（不影响 M1–M4） |
| **r1** | **B2 存量清点**：遍历四个仓库的 onset rule JSON，统计是否使用字面音素段 | 影响 M4 排期与发布说明 |

r1 是只读调研，可在 M0 期间完成，结论写回 [linguist-variants.md](linguist-variants.md) §3.3。
若存量资源已使用字面段，B2 的修复即属于**破坏性变更**，须在 M4 中单独设置开关并写入发布说明。

---

### M1 — 声明面

**目标：不编写推理代码，跑通「一个语言包 + 一个歌手」的声明加载全流程。**

| # | 工作包 | 改动 | 依据 |
| :-- | :-- | :-- | :-- |
| W1.1 | L1 身份字段 | `src/lib/Linguist/LinguistContrib.cpp` 白名单 +2（`language`/`scheme`）、`createSpec` 增形状校验；`LinguistContrib.h` 的 `LinguistSpec` +2 成员 +2 访问器 | A7、域契约 §2 |
| W1.2 | synthrt 歌手字段 | `synthrt/lib/SVS/SingerContrib.cpp` 白名单 +2、`createSpec` 增三条结构校验；`SingerContrib.h` 的 `SingerSpec` +2 成员 +2 访问器 | A11、域契约 §10.5 |
| W1.3 | L2 裁决改写 | `WolfLinguistProvider.cpp` 的 validator 改读 `languages` 映射（不再看 role 前缀/后缀）；`createExtensions` 挂载条件改为「`languages` 非空且其 role 指向 linguist 贡献」 | A11、A12、运行时 §2.1 |
| W1.4 | 负面夹具 | 手写最小 Package 树，专供构造加载失败用例（正向路径用 `lang-zxx`） | §1.2 |
| W1.5 | **`lang-zxx` 创作** | 按新格式手写（零资源，入库为 source） `wolf/lang-zxx` 的 `desc.json` + `linguists/zxx-passthrough/linguist.json` + 两个推理模块声明；G2P 正则合并为 `\p{N}\|\p{P}\|[.]+` | A25、发布 §2.3 |
| W1.6 | 桩 singer provider | 只需覆盖 `createConfiguration`（约 30 行）；测试歌手经 `configuration.languages` 承载映射，读取集中在 `readSingerLanguages()` 中 | A26 |
| W1.7 | 声明面测试 | `lang-zxx` 正例 + 手写负面夹具（见下） | §1.2 |

**W1.3 的注意事项**：二元组命中校验（域契约 §5.3）依赖类型化的 `exports.languages`，属于 M2。M1
只校验 `languages` 映射的结构、目标类别以及 `language` 字段的一致性，命中校验留作 TODO。

**W1.7 的负面用例清单**（每条都应加载失败，且错误文本可定位）：

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

**目标：三份契约的 C++ 面成型，并有一个能被框架真正加载的桩解释器。**

| # | 工作包 | 改动 | 依据 |
| :-- | :-- | :-- | :-- |
| W2.1 | 共享类型 | 新增 `include/wolf/Api/Inferences/Common/1/CommonApiL1.h`：`LanguageScheme` 与其比较 | 链契约 §6.1 |
| W2.2 | 三份 payload 体系 | `G2P/1`、`S2P/1`、`Onset/1` 从「仅常量」补齐为 Exports / ImportOptions / RuntimeOptions / InitArgs / StartInput / Result / Executive | 链契约 §6.2 |
| W2.3 | **变体参数化的 RuntimeOptions** | 三份 `RuntimeOptions` 由构造参数接收目标变体，**不得硬编码** | A15、链契约 §6.3 |
| W2.4 | 桩解释器插件 | 新增 `src/plugins/inferenceinterpreters/stub/`：派生 `srt::InferenceInterpreter`，嵌 `org.openvpi.synthrt.plugin.InferenceInterpreter`；G2P 回显、S2P 按空格切分、Onset 全 `false` | §1.1 |
| W2.5 | 二元组命中校验 | 补上 M1 挂的 TODO：linguist 自身二元组须命中 g2p/s2p 目标的 `exports.languages` | 域契约 §5.3 |

**W2.4 的作用不限于测试**：它是对 A1（三份契约使用 synthrt 内置的 `inference` 类别）的**首次
实际验证**：插件能否被该类别的 factory 发现、IID 是否匹配、执行工厂能否创建，均由该工作包验证。

**退出判据**：桩插件放进宿主为 `inference` 配置的搜索路径后，夹具包的三个推理模块能完成
Probe 选择与 Acquire；`exports.languages` 类型化解析通过；二元组不匹配的夹具加载失败。

---

### M3 — 运行时面（穿刺验证）

**目标：三级执行体树实际运行。这是整个 L4 的基础，必须在铺开真实解释器之前验证。**

| # | 工作包 | 改动 | 依据 |
| :-- | :-- | :-- | :-- |
| W3.1 | LinguistExecutive 实现类 | provider 侧具体类（不再直接 `new` API 类）；`createChild` 三跳；注入 `(language, scheme)` 与**目标变体** | 运行时 §3、A15 |
| W3.2 | 任务面 | 单任务面 `start`/`startAsync`/`stop`/`waitForFinished`/`state`；逐词分层锁定输入；`depth` 截断 | 运行时 §4、A17 |
| W3.3 | 停等聚合 | `quit()` 只停本节点在飞任务；`wait()` 同理（子节点由监督树负责，**不得自行递归**） | 运行时 §3.2 |
| W3.4 | 诊断 | 词级 `hitStage`；执行体级 `binding()` / `g2pContribution()` | 运行时 §6、A17 |
| W3.5 | 资源缓存 | `wolf::ResourceCache`：内容寻址键 + 路径快路径 + provider 域归属 | A14、缓存文档 |

**W3.5 的排期理由**：A14 规定并发通过创建多个执行体实现，缓存因此是**架构必需件**。若在真实
解释器完成后再补充，每个变体都须先实现一遍无缓存的资源装载，再修改一遍。

**退出判据**（穿刺用例）：

1. 夹具歌手 → `createPipeline` → `createLinguist("cmn")` → 一条 3 词的输入跑出 phonemes 与 onsets；
2. 同一歌手并发开 3 个 `LinguistExecutive`，互不干扰；
3. **同一资源被两个执行体使用时读盘次数 = 1**（缓存的可执行验收）；
4. 提前 `delete` 子执行体后父仍可用；包释放时 quit/wait 顺序正确（quit 自顶向下、wait 自底向上）。

---

### M3.5 — 发布链垂直切片

**目标：用内容成本最低的包先跑通「打包 → release → 端口 → 消费」整条链。**

| # | 工作包 | 说明 |
| :-- | :-- | :-- |
| W3a.0 | **本仓 overlay** | 建 `scripts/vcpkg-ports/`，manifest 的 `overlay-ports` 改为有序两项（本仓在前、共享子模块在后）（A29） |
| W3a.1 | `synthrt-main` 端口 | 放在本仓 overlay；`SYNTHRT_BUILD_DSINFER=OFF`；A11 落地时以 `PATCHES` 携带其补丁（A26）。[现状：端口固定 synthrt 分支 `onnxruntime-builds-uptake`，不携带补丁；dsinfer 由 feature `onnx` 启用] |
| W3a.2 | `lang-zxx` 打包与 release | zip（解包即目录，A18）+ `manifest.json`；tag `lang-v<bundleVersion>`。打包脚本在此时建立，M5 的 W5.0 在其基础上扩展 |
| W3a.3 | `wolf-lang-packages` 端口 | 放在本仓 overlay，沿用共享 overlay 中 `ffmpeg-builds` 的纯数据端口模式；此时只有 `zxx` 一个 feature |
| W3a.4 | wolf 测试接线 | 由清单 feature 控制；未启用时 `SKIP`；备用入口为缓存变量 `WOLF_LANG_PACKAGES_SOURCE` |

供 lite 使用的 `wolf` 端口**不在本方案范围内**：wolf 仓库的 overlay 只承载 wolf 自身使用的端口，
lite 所需的端口由 lite 仓库自己的 vcpkg 配置提供（A29 的作用范围原则）。

**M3.5 的排期理由**：发布链有许多与内容无关的失败模式（资产命名、SHA512 校验、解包布局、端口
变量在不同 vcpkg 版本间的差异、feature 依赖的写法）。先用一个零资源的包暴露这些问题，比等 12 个包
全部转换完成后再一次性验证更安全，因为届时须同时处理内容与管道两类问题。

**端口位于本仓（A29），因此这一步只需在单个仓库中提交一次**：发布脚本就地更新 `assets.cmake`，
不再需要「生成 → 复制到 overlay 仓库 → 推送 overlay → 两个仓库各自更新子模块指针」的跨仓四步。

**退出判据（已达成，A67）**：`vcpkg install --x-feature=lang-packages` 后，wolf 的测试从端口安装树
取得 `wolf/lang-zxx` 并完成 `Load`；不带该 feature 时不下载任何数据且测试 `SKIP`。

---

### M4 — 真实解释器

**移植来源见 [linguist-variants.md](linguist-variants.md) §7（一律取 synthrt `origin/refactor` 上
由 `convert-g2p-packages.py` 中固定的 `SOURCE_REF` 所指的提交）。顺序按外部依赖由少到多排列。**

| 序 | 工作包 | 阻塞项 | 说明 |
| :-: | :-- | :-- | :-- |
| 1 ✅ | **S2P 三变体** `dict`/`direct`/`mapping` | — | 已落地。`lua` 变体见第 7 项 |
| 2 ✅ | **Onset `rule`** | ~~B2~~ | 已落地，按目标语义重写。清点确认无存量资源使用字面段，不属于破坏性变更。`lua` 变体见第 7 项 |
| 3 ✅ | **`Verifier` 共享组件** | — | 已落地为 `wolf::Verifier`（由 `re2` 支持 `\p{Han}` 等属性类）。三种类型齐全，未知类型在读入期报错 |
| 4 ✅ | **G2P `algo-pinyin`** | ~~B1~~ | 已落地。**B1 由 A30 的共享后端包 + A31 的进程级仲裁器解决**：cmn / yue 经一个词典根并存，异根即加载失败。引擎按 `languageMap` 由绑定二元组选（A23 保持） |
| 5 ✅ | **G2P `pipe-chain`** | — | 已落地，`lang-zxx` 不再依赖桩。五种 step 齐备；`model` 步按极大连续段分批（A33）；`dict` 步采用严格 TSV 与 `word(n)` 归并（A34） |
| 6 ✅ | **G2P `multig2p-onnx`** | ~~ONNX 驱动~~ | 已落地。驱动由宿主注册为 Runtime Service（A36）；解码为贪心，beam 相关键的取值受限（A37）；`languageMap` 必选并与 `exports.languages` 对账。已用真实模型端到端跑通 |
| 7 ✅ | **两个 `lua` 变体** | ~~LuaJIT~~ | 已落地（A39）。四个仓库中没有任何生产用 Lua 脚本资源，因此该项用于补齐变体表，而非支持存量资源 |

**每个变体的统一验收**：

- 以 refactor 的对照输入产生与旧栈**逐字节一致**的输出，**已定案的行为修正除外**（这些修正必须
  逐条列出，且各有一个专门用例）：Onset 字面段、`fallback` 的 error 语义、multig2p 未映射语言、
  chain 打标的 `array`/`dict` 型；
- 拒绝未知 `configuration` 键；
- 资源装载经过 `ResourceCache`。

**可并行的工作**：1 与 2 互不依赖，可以并行；3 完成后，4 与 5 可以并行；6 依赖驱动就绪，其调研可以
最先启动。

---

### M5 — 包与端口

| # | 工作包 | 依赖 | 说明 |
| :-- | :-- | :-- | :-- |
| W5.0 | **转换脚本** | — | 从 `origin/refactor` 的固定提交（`SOURCE_REF`）读入，输出到 gitignored 的 `build/lang-packages/`；同时产出 `manifest.json` 与 `assets.cmake`。**产物一律不进入 git**（A28） |
| W5.1 | **其余 12 个包与后端包** | M4、M0-d2 | 12 个语言包与 `wolf/g2p-multi`（`lang-zxx` 已在 M1 完成）；每个包新写 `desc.json`、`linguist.json` 与三个推理模块的 `exports`。原先预计占主要工作量的「每种语言的默认 S2P 与 Onset」已随 B3 排除（A66）：九种语言使用 `direct`，`cmn`/`yue`/`jpn` 所需的词典属于歌手包内容 |
| W5.2 | 发布脚本铺开 | W5.1 | 按语言打包 zip（解包即目录，A18）并生成 `manifest.json` 与 `assets.cmake`；tag `lang-v<bundleVersion>`。原计划中 `eng` 因许可未核实而不列入清单；当前发布 `lang-v0.1.2.0` 已包含 `wolf/lang-eng` |
| W5.3 | 端口 features 铺开 | W5.2 | 由 M3.5 的单个 feature 扩展为与全部语言一一对应（同一仓库，随 release 一并提交） |

**W5.1 是本里程碑的主要工作量**，且是唯一无法从旧栈复制的部分：旧栈中 S2P 与 Onset 是声库
manifest 的字段而非模块，公共语言包要成为完整闭包必须从头编写。**排期时不得按「12 个包 × 修改键名」
估算。**

**退出判据（已达成，A67）**：`vcpkg install --x-feature=lang-packages` 后，wolf 的包加载测试取得
真实语言包数据并通过；不带该 feature 时不下载任何数据且测试 `SKIP`。

---

### M6 — 收尾

| # | 项 | 依据 |
| :-- | :-- | :-- |
| W6.1 | `exports` 与 `imports[].options` 的 JSON Schema 发布物 | spec 2.4:596-602 的硬性要求（Q3） |
| W6.2 | 打包期 lint 工具 | 域契约 §12 的比对清单 + 贡献 ID 书写惯例（A4、Q4） |
| W6.3 | 文档回写实测值 | `stop()` 的词边界响应时延建议值（Q2）；B2 清点结论；各语言 `scheme` 定案 |
| W6.4 | 旧栈遗留退役 | DiffSinger 歌手 `configuration` 的 `dict` 键（Q5）；旧声库 manifest 的 `g2pPackageVersion` 映射（P6） |

### M7 — 版本口径与降级

本里程碑源于一次审计：A54 为语言包规定的 `compatVersion` 口径会使每一次重新打包都破坏全部已发布
的声库（A68）。修复后同时划定降级的边界：A69 否决了运行期能力解析，因此版本兼容只剩
`compatVersion` 一个机制，该机制必须严格执行。

| # | 工作包 | 依赖 | 说明 | 验收 |
| :-- | :-- | :-- | :-- | :-- |
| W7.1 ✅ | **语言包 `compatVersion` 下沿** | — | `convert-g2p-packages.py` 删除 `compat_floor(...) if is_backend else version` 分支，一律取下沿；重新打包一版 bundle（A68） | 以一份写死旧版本的歌手包 `desc.json`，对**已提升修订号**的语言包加载通过 |
| W7.2 ✅ | **依赖下沿 lint 升为错误** | W7.1 | `check-declarations.py`：`dependencies[].version` 不等于目标包的 `compatVersion` 时报错，并给出应写的值；`make-lang-release.py` 据此拒绝打包（A68） | 当前 15 个包无错误；人为把版本写高一级时拒绝打包 |
| W7.3 ✅ | **`linguist/s2p` 放宽为 `0..1`** | — | `WolfImportValidator` 的下界只留 `linguist/g2p`；`LinguistExecutiveImpl::start()` 按 onset 缺席的同款方式处理 s2p 缺席（A70、域契约 §5.1） | 只带 G2P 的 linguist 能加载；`convert(depth=Onsets)` 返回到 pronunciation 为止而非报错 |
| W7.4 ✅ | **`LanguageStatus` 增 `maxDepth`** | W7.3 | 由 `imports` 集合直接读出，`probe()` 保持无副作用（A70、A71） | `probe()` 对缺 s2p 的语言报 `Pronunciation`，且调用前后 `ResourceCache::parseCount()` 不变 |
| W7.5 ✅ | **音素覆盖度** | W7.4 | `LinguistSession::setSingerPhonemes()`；`LanguageStatus` 增加 `coverage` / `coverageKind` / `missingPhonemes`；`coverage == 0 && !openSet` 判为 `Unavailable`，其余比例只报告数值（A70） | 以 Status 中已统计的比例作为断言值：`fil` 100% / `ita` 75% / `eng` 44%；`openSet` 为真时 `coverageKind` 取下界而非精确值 |

**已全部落地。** W7.1 / W7.2 属于紧急修复，最先实施；W7.3–W7.5 与之无依赖，并行完成。

**退出判据（已达成）**：打包修订号提升到 3 并重新打包 bundle 0.1.2.0 之后，写死旧版本 `1.0.1.2` 的歌手包仍加载通过，全部 17 个测试二进制通过。

---

### M8 — 稳定性、规范性与并行安全

来源为对现有实现的四轴审核（稳定性 / 向后兼容性 / 规范性 / 并行加载安全性），条目编号沿用
`Status.md` 的 X 系列。

**第一批（P0，四项互不依赖）——已全部落地**

| # | 工作包 | 改动 | 验收（已达成） |
| :-- | :-- | :-- | :-- |
| W8.1 ✅ | 修复下游消费（X1） | `blake3` 归入 `LINKS_PRIVATE`；增加配置期断言约束导出接口（A74） | 空白消费者工程按 README 的写法 `find_package`、链接并运行通过；把 `blake3` 放回公开接口时配置立即失败。CI 现以 `.github/consumer` 执行该检查 |
| W8.2 ✅ | pinyin 词典根保留的事务化（X8） | 保留改为 RAII，由模块 `Configuration` 持有（A72） | `test_PinyinBackend` 增加两个用例：在失败的加载之后与卸载之后，另一个根均可用；禁用该析构逻辑时两个用例均失败 |
| W8.3 ✅ | 取消语义修正（X3） | `start()` 以 `exchange` 消费取消位；`enrol()` 返回布尔值（A73）。同批修复子执行体创建失败时状态停留在 `Running` 的问题（X 系列 S6） | `test_LinguistSession_HonoursACancelledToken` 改为实际断言：预置取消后，各词的发音、音素与 onset 均为空 |
| W8.4 ✅ | 保留词不吞锁定层（X4） | 拦截条件加 `!word.locked.has_value()` | 新用例：`SP` 带锁定音素层时输出等于锁定值，无锁定层时仍由会话应答 |

**第二批（P1）——已落地**

| # | 工作包 | 依赖 | 改动 | 验收 |
| :-- | :-- | :-- | :-- | :-- |
| W8.5 ✅ | pinyin 临界区收窄（P3） | W8.2（同一文件） | 目录检查移出锁；根的设定使用 `once_flag` 与 release/acquire 语义；引擎构造移出锁。A31 已说明该全局量只在构造期被**读取**一次 | 并发用例：两个线程同时预热 `cmn` 与 `yue`，耗时接近两者的最大值而非总和；TSan 无告警 |
| W8.6 ✅ | 执行体改为持有 `srt::ITask`（X5） | — | 十一个执行体各持有一个 `ITask` 成员并转发，参照 dsinfer 的 `DurationInference`；删除手写的 `m_state` / `m_stopRequested` / 空 `waitForFinished` / 直接 `detach`。转发与停止标志后统一收入 `wolf::ExecutiveBase` | `waitForFinished()` 阻塞至转换结束；第二次 `startAsync` 被拒绝；转换进行中卸载包时阻塞至转换结束 |

**第三批（P2–P3）——已落地**

| # | 工作包 | 改动 | 验收 |
| :-- | :-- | :-- | :-- |
| W8.7 ✅ | 契约违例处理统一（X7） | `LinguistExecutiveImpl` 对 G2P / S2P 返回的短批次按 `chain::convertRun` 的规则整批标记错误，不再静默截断 | 桩模块故意少返回一个词时，转换报告错误而非返回空发音 |
| W8.8 ✅ | 声明根键白名单（B5 / C2） | 未知的**框架公共字段**由拒绝改为忽略并记录警告；类别层字段仍严格校验 | 声明中加入一个假想的未来公共字段后仍能加载 |
| W8.9 ✅ | 缓存条目回收（X6） | 过期的 `weak_ptr` 在同键命中时或按阈值清理 | 反复 load / release 后 `entries` 不单调增长 |
| W8.10 ✅ | 一个 unit 一个会话的限制（P5） | 结论为**取消该限制**：会话之间无共享的可变状态，且该限制既未强制也未检查（A78） | `test_LinguistSession_TwoSessionsShareOneUnit`：预热、释放、并发转换三方面互不干扰 |

**跨里程碑规则**

- **D1 — ABI**：`LanguageStatus` / `LanguageEntry` / `SingerEntry` / `SingerRef` / `CancelToken`
  以及全部 `Api::*L1` 的 payload 都是头文件中的值类型，增加字段即破坏 ABI。W7.4 / W7.5 与 M8 的
  任何公开结构体变更**必须与一次 `WOLF_VERSION` 变更同批发布**，并在 README 中写明 1.0 之前不作
  ABI 保证。`LinguistSession` 采用 pimpl，是唯一不受影响的类型。[现状：版本已升至 0.1.0.0，CMake
  包的版本兼容性为 `ExactVersion`，README 已增加《Versioning and ABI》]
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
