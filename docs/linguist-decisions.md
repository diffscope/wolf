# wolf 语言域决策台账

本文记录语言域设计的**全部决策与论证**，规范文本不得复述。分层文档只写结论，决策理由在本文查阅。

编号规则：**A 系列**为现行架构决策（2026-09-08 重构轮），**D 系列**为历史草案轮次的决策
（保留谱系，多数已被 A 系列取代）。**编号永久不复用，也不因清理而重排。**

锚点口径见 [linguist-architecture.md](linguist-architecture.md)。

---

## A 系列：现行架构决策

### A1 — 推理三契约沿用 synthrt 内置 `inference` 类别

**决策**：G2P / S2P / Onset 的模块是 `inference` 贡献；解释器派生 `srt::InferenceInterpreter`，
插件嵌入 `org.openvpi.synthrt.plugin.InferenceInterpreter`，位于宿主既有的 `inference` 搜索路径。
wolf 只发布契约头，不注册新模块类别。

**依据**：`synthrt/lib/SVS/InferenceContrib.cpp:111,147`、`InferenceInterpreterPlugin.h:11`。

**备选与否决理由**：
- *wolf 自注册 `linguist.g2p` 等三个类别*：可自定执行体接口（含多任务），但每个类别要求宿主
  多配置一条 `setPluginPaths`（`ContribCategory` 的构造签名要求模块类别自带 IID），且须自建
  executive / task 体系，与 dsinfer 生态不同构，并失去 `validateCompatibilityWith` 钩子。否决。
- *混合（G2P 自注册、S2P/Onset 留在 inference）*：同一条链跨两种类别，部署面与概念模型都更
  复杂。否决。

**代价（已接受）**：`srt::InferenceExecutive` 是单任务接口，见 A14。

---

### A2 — Level 1 契约面收敛为 3 份

**决策**：Level 1 只固定 G2P / S2P / Onset。`G2PModel`（模型后端）与 `DictQuery`（词典查询）
不设立契约，降为 `pipe-chain` 变体的内部事务，经该变体自身的 `imports` 与 `configuration` 表达。

**依据**：两者在 wolf 与 synthrt 两侧均为**零代码、零消费方**，属纯推演产物。把既无实现也无
消费方的契约放进发布承诺面，收益为零而演进成本为正。

**重启判据**：出现「需要跨实现互操作的模型后端或词典服务」的真实案例时，按 spec 2.4:750-753
的判据另立 interface。原 D15-D18 的拓扑论证过程可经 git 历史回溯。

**取代**：D15、D16、D17、D18。

---

### A3 — 语言身份 = 声明的 `(language, scheme)` 二元组

**决策**：语言身份由 linguist 声明的两个字段承载；**贡献 ID 从不被解析**。

**动因**：原设计把「贡献 ID / 语言句柄 / 注音体系」三项压进一个字符串 `cmn-pinyin`，再通过
字符串解析取回，因而必须规定「各字段内不得含连字符」「自定义字段不被剥除」「匹配一律按全 ID
恒等」。这套规则恰恰阻碍了多语种、多注音体系：G2P 必须逐一枚举所有 linguist 贡献 ID
（含第三方 qualifier），`eng-arpabet-plus` 这类第三方 ID 无法对接官方 G2P。

**收益**：
1. G2P / S2P 作者无须了解任何 linguist 贡献的 ID；
2. 第三方语言贡献只要 `scheme` 相同，即可自动对接官方模块；
3. 「拼音 G2P 接粤拼 S2P」在加载期硬失败（原设计完全不拦截）。

**取代**：原《语言贡献 ID 形态约定》的全部字符串规则。

---

### A4 — ID 语法 `<language>-<scheme>[-<qualifier>]` 为书写惯例，只做 lint

**决策**：贡献 ID 的惯例为 `language + "-" + scheme`，或以 `language + "-" + scheme + "-"`
开头且余部非空。`qualifier` 无语义，不参与任何匹配。**该惯例只在打包期由 lint 比对，不作加载期
校验。**

**动因**：同一二元组可能对应变种、不同作者、精简版等多个贡献，需要第三段加以区分。

**关键性质**：该规则是**前缀比对**而非解析，因此 `scheme` 内部的连字符不产生歧义。
`cmn-pinyin-lite` 在 (`scheme=pinyin`, `qualifier=lite`) 与 (`scheme=pinyin-lite`, 无 qualifier)
两种声明下都合法且各自自洽，因为任何一方都不从 ID 反推语义。

**不作硬校验的理由**（2026-09-08 按 A17 的判据复审后降级）：ID 的前缀**不携带任何信息**。
真值在 `language` / `scheme` 两个字段中，本契约族的任何匹配、绑定、路由都不读取 ID。硬校验
带来的只是「ID 可预测」，代价却是 spec 2.4:506-508 明确赋予 Package 的命名自由（「同一份模块
目录被两个 Package 收录时两边可以各自命名」）。信息量为零的副本不值得一条加载期规则。

---

### A5 — G2P 与 S2P 同形声明 `exports.languages`，可省略

**决策**：两者使用同一个键、同一形状 `array<{language, scheme}>`，语义互为对偶（能产出 / 能消费）。
该键可省略；省略即放弃该侧的静态保障，模块可以加载，但宿主应告警。

**收益**：补上原设计**完全缺失**的一项检查。在原设计中，把拼音 G2P 接到粤拼 S2P 词典上只触发
警告级的 `phonemes` 比对，运行时才会出现大面积未命中。

**可省略的理由**：`direct`（按空格拆分）与 `lua` 变体本质上适用于任何体系，强制声明会导致
虚假声明。

---

### A6 — 文档按层一份，台账外置

**决策**：规范文本内不保留 D 编号、攻防记录与轮次叙事。

**动因**：旧五份草案含约 30 条决策台账，其中变体草案的台账节占该文档近一半篇幅，且是跨文档
漂移的主要来源（复核发现的多处行号与事实漂移集中在台账区）。

---

### A7 — 二元组作**类别追加字段**写在 linguist 声明根

**决策**：`language` / `scheme` 写在声明根，由 `LinguistCategory` 在 Probe 阶段解析；
`linguist` 声明白名单增加 2 项。

**依据**：spec 2.4:529-535 规定的第二层（由贡献类别规定、对该类别下所有模块生效、由类别自己
解析），其判据正是「在解释器被选出来之前就要用上」。同层先例为 `singer` 的
`avatar` / `background` / `demoAudio`（`synthrt/lib/SVS/SingerContrib.cpp:33-36`）。

**备选与否决理由**：*写进 `exports`*：白名单无须改动（`createExports` 不拒绝未知键），且与
G2P / S2P 的 `exports.languages` 形状呼应。但身份将绑定到 `WolfLinguist` 这一个 interface：
若今后 `linguist` 类别下出现第三方 interface，wolf 的歌手侧校验对其失效；且校验时机从 Probe
推迟到 Acquire。**权衡后采用类别层**，代价仅是白名单中的两个字符串。

---

### A8 — ~~缺省语言 = imports 声明序第一条 `linguist/*`~~（作废）

原决策试图在不改 synthrt 的前提下表达缺省语言，已被 A11 取代：用户要求一个可供编辑器使用的
显式字段。

保留一条结论供参考：`imports` 保持声明顺序（spec 2.4:634；`stdc::vlarray<ContribImport>`），
因此「声明序第一条」在技术上可行；作废原因是表达力，而非可行性。

---

### A9 — `configuration` 必须显式写 `{}`

**决策**：保留 wolf 现行行为（省略时框架交付 Null，provider 判 `InvalidFormat`），将**规范
文本**改为「必须显式写为空对象」。

**依据**：省略时 `context.manifestConfiguration` 保持缺省的 Null（`PackageLoader.cpp:1316-1319`）。
契约有权要求其所辖模块显式提供某个公共可选字段，这是契约层的加严，不违反 spec 2.4:522。

**不改代码的理由**：此处修改文本的成本低于修改代码，且现行行为已由测试固定。

---

### A10 — `vars` 无需任何处理

**决策**：`linguist` 白名单无须增补 `vars`。旧文档中「白名单未含 `vars`，故模块级字符串变量
暂请写在 `desc.json`，该行为不符上位规范」的注记是**事实错误**，予以删除。

**依据**：框架在把声明交给类别与解释器之前已执行 `object.erase("vars")`
（`synthrt/lib/Core/PackageLoader.cpp:406`，调用点 `:1274`，早于 `createSpec` 的 `:1377`）。
任何类别的字段白名单都不会遇到 `vars`，模块级 `vars` 现已正常工作。

**连带**：synthrt 的 `inference` / `singer` 白名单同样不含 `vars`，同样不构成缺陷。原判定
「三处同样违规」撤回。

---

### A11 — 歌手侧新增 `languages` 与 `defaultLanguage`

**决策**：歌手声明根增加两个类别追加字段，由 `SingerCategory`（synthrt 侧）解析：

- `languages`: `map<语言句柄, 本声明内的 import role>`；
- `defaultLanguage`: 句柄，必须是 `languages` 的键；`languages` 非空时必填。

歌手 `role` 恢复自由命名，`linguist/` 前缀降为书写建议。

**动因**：
1. 编辑器需要一个可选用的缺省语种字段；
2. 需要显式表达「标准 ISO 代码 ↔ 实际 linguist 贡献 ID」的对应关系。

**结构性收益**：`role` 回归 spec 2.4:648 的定义，即「导入方为该条目指定的本地 slot」。原设计
把语言句柄编码进 role 后缀，是因为歌手声明白名单不允许新字段；该方案出于限制，而非更优。

**值取 role 而不取 ModuleReference 的理由**：ImportBinding 只从 `imports` 数组产生
（`PackageLoader.cpp:894-917`），数组之外的 ref 不产生绑定，运行时无法获得执行工厂。这是框架
事实，而非风格选择。

**`defaultLanguage` 必须显式的理由**：`JsonObject = std::map<std::string, Value, std::less<>>`
（stdcorelib `support/json.h:68`），成员按键排序，声明顺序丢失，object 中不存在「第一个」成员。

**职责切分**：synthrt 只校验形状与「role 在本声明的 imports 中存在」
（`ContribCreateContext::imports()`，`ContribCategory.h:60`），**不涉及任何 linguist 语义**；
「键形如 `[a-z]{3}`」「目标类别为 `linguist`」「目标 `language` == 键」三条归 wolf 的 Ready-2
validator。因此两个字段对任何语言体系通用，框架中不引入 wolf 的领域知识。

**成本**：synthrt `SingerContrib.{h,cpp}` 白名单增 2 项、解析增 2 处、访问器增 2 个。这是本轮
唯一动用「synthrt 个别刚需字段」额度之处。

**取代**：D9、D29 的 role 后缀方案；A8。

---

### A12 — 唯一映射约束由映射键唯一性承载

**决策**：「同一声库中每个语言只支持一种注音体系」由 `languages` 的 JSON object 键唯一性
在结构上保证，wolf **不编写**该校验。

**依据**：`JsonObject` 是 `std::map`，每个语言句柄至多一条映射 ⇒ 至多一个语言导入 ⇒ 至多一种
`scheme`。

**已知缺口（不影响本约束）**：源 JSON 中的重复键被 map 静默折叠，而非按 spec 2.4:72
「同一层不得出现重复 key，违反时整份声明无效」报错。这是 stdcorelib 的符合性缺口；本约束
依赖的不变量（每键至多一条）不受影响。

**演进路径**：若将来需要「同声库同语言双体系」，spec 2.4:644 的多段 role 文法支持
`linguist/cmn/bopomofo` 形态，但这会使本约束失效，属于**显式的 Level 递增**，而非预留的后门。

---

### A13 — `defaultLanguage` 无加载期与运行时语义

**决策**：除「必须是 `languages` 的键」这条结构约束外，该字段不参与任何加载期裁决与运行时
行为。语言域自身从不读取该字段，它仅是作者向宿主声明的意图。

---

### A14 — 并发由多个执行体承载，不由多个任务承载

**决策**：一条链 = 一棵执行体子树 = 一个在飞任务。`LinguistExecutive` 与
`srt::InferenceExecutive` 保持同构的单任务接口。

**依据**：`srt::InferenceExecutive` 只提供 `state()` / `stop()` / `waitForFinished()`，
`quit()` / `wait()` 为 private final（`synthrt/include/synthrt/SVS/InferenceExecutive.h:39-50`）。
G2P / S2P / Onset 执行体各自只能承载一个在飞任务，为 `LinguistExecutive` 提供多任务接口会造成
名不副实的接口。`adoptChild` 不限制同一 role 下的多个子执行体（`ContribExecutive.cpp:71-109`），
因此创建多个执行体是框架支持的并发路径。

**取代**：原运行时草案中「executive 量产多个相互独立、可并发入队的 task」的形状。

**连带（重要）**：资源缓存从「优化」升为**架构必需件**。k 路并发即 k 组 G2P/S2P/Onset 执行体，
若无跨执行体共享，即产生 k 份词典与 k 组 ONNX session。

---

### A15 — `RuntimeOptions` 必须携带目标变体

**决策**：G2P / S2P / Onset 的 `RuntimeOptions` 类由构造参数接收目标变体，不得硬编码。

**依据**：`synthrt/lib/SVS/InferenceContrib.cpp:25-30` 逐一比对
`runtimeOptions.variant() != target.variant()`。dsinfer 的 `DurationRuntimeOptions` 硬编码
`"onnx"`，原因是该契约只有单一变体，**不可照搬**：本契约族的三份契约都是多变体。

---

### A16 — 运行时 IO 不携带语言参数

**决策**：G2P 的 Level 1 运行时词汇表**不含** `languageId` 输入；执行体在创建时经
`RuntimeOptions` 绑定单一 `(language, scheme)`，生命周期内不变。

**动因**：A14 之后每条链对应一棵子树，子树天然对应一个语言，运行时再传入语言是冗余的。原设计的
《运行时路径》整节（未传入则回落绑定值 / 传入且恒等 / 传入但不一致判 `InvalidInput` 的依序
判定）随之删除。

**代价**：多语言 G2P 须为每个语言各创建一个执行体，资源共享由缓存承担（见 A14 的连带条款）。

---

### A17 — 运行时载荷的精简判据

**决策**：运行时载荷按两条判据裁剪，不满足者删除或改形。

**判据一：非法状态不可表示。**

| 原形 | 问题 | 改后 |
| :-- | :-- | :-- |
| `pronunciationLocked: bool` + `pronunciation: string` | 「未锁定」与「锁定为空串」不可区分；两字段可能矛盾 | `pronunciation: optional<string>` |
| `phonemesLocked: bool` + `phonemes` + `onsets` | 同上，且三者可各自缺席，而 `phonemes`/`onsets` 必须同时给出且等长 | `locked: optional<{phonemes, onsets}>` |
| `options: {needPhonemes: bool, needOnsets: bool}` | 「要 onset 但不要音素」不是合法状态，却可表示 | `depth: enum {Pronunciation, Phonemes, Onsets}` |

**判据二：按变化频率分置，恒定量不逐词复制。**

原设计把 `binding`（生效的二元组）与 `g2pContribution`（G2P 模块定位符）放进**每个词**的
诊断载荷。但 A16 之后一个执行体绑定一个二元组、持有一个 G2P 子执行体，这两项在执行体的
整个生命周期内**恒定**，逐词携带属于纯复制。改为执行体级访问器，只有 `hitStage` 保留在词级。

**同时删除**：`LinguistExecutive::initialize` 与 `LinguistInitArgs`。执行体在创建时已由
`LinguistRuntimeOptions` 完成全部绑定，Level 1 没有第二份初始化参数需要携带；子执行体各自的
`initialize` 由 wolf provider 内部调用，不对外暴露。保留一个空的初始化入口只是在形式上模仿
dsinfer，不承载信息。

**通用判据**（后续设计沿用）：一个字段进入载荷，必须同时满足以下三条：① 其取值不能由载荷中
其他字段推导；② 其变化频率与所在载荷的粒度相符；③ 其每种取值组合都是合法状态。

---

### A18 — 发布物为解包即得的目录，不是 `.dspk` 单文件

**决策**：语言包的 release 资产为 zip，解包后即为 Package root 目录；包搜索路径下每个子目录是
一个 Package。

**依据**：spec 2.4:59 把 Package 定义为 `.dspk` ZIP，但 **synthrt main 的加载器只受理目录**：
`PackageLoader.cpp:1055-1061` 对非目录返回「Package path is not a directory」，搜索路径扫描
只收集子目录（`:614`），且 main 的 CMake 已不依赖任何解压库。`.dspk` 单文件资产目前无法被加载。

**登记为 spec 与实现的不一致**（架构文档《已知张力》第 2 条）。main 支持解压后只需更换打包方式，
发布模型的其余部分不变。

---

### A19 — 发布宿主为 wolf 仓 Release，按语言切包

**决策**：

- 发布宿主 = **wolf 仓库的 GitHub Releases**，不另设资源仓。资源 tag 用 `lang-v<bundleVersion>`，
  与代码 tag `v<x.y.z>` 分属不同命名空间；
- 当时的 16 个套件切成三类：**12 个语言包** `wolf/lang-<iso>`、**1 个共享后端包**
  `wolf/g2p-multi`、**1 个直通包** `wolf/lang-zxx`（Num / Punc / Unknown 三合一，见 A25）；
- 一次 release = 一次全量快照（14 个资产 + `manifest.json`），各包 `version` 独立演进。

> **后续（A30）**：新增共享后端包 `wolf/g2p-pinyin`，一次 release 现为 15 个资产
> （当前发布的版本号见 [linguist-distribution.md](linguist-distribution.md) §5.1，不在本处维护以免漂移）。

**取代**：D-P1（独立资源仓 `wolf-g2p-packages`）。原方案的理由是「资源版本节奏与代码发布解耦、
许可证隔离」。tag 命名空间隔离已实现前者，`manifest.json` 的可机检边界已实现后者，无须增加一个仓库。

---

### A20 — 共享模型后端声明 G2P 契约，佐证 A2

**决策**：`wolf/g2p-multi` 中的 multig2p 模块声明 `org.openvpi.wolf.inference.G2P`，变体
`multig2p-onnx`，由 `pipe-chain` 经自身 `imports` 的私有 role 消费。

**无须 G2PModel 契约的理由**：共享一个 19 MB 的模型**必须**使其成为独立包中的模块
（spec 2.4《依赖项》的模块复用场景即为此而设），而模块必须有契约。既然该模块须有契约，声明 G2P
即可。该模块同时也可被某个语言直接用作 `linguist/g2p`，这不构成漏洞：声明了 G2P 契约的模块必须
满足 G2P 契约，「纯模型、无编排」的语言链是一种合法配置。

与原方案「另立 G2PModel 契约 + 依靠命名隔离防止直接引用」相比，本方案少一份契约、少一层机制，
是 A2 的正面佐证。

---

### A21 — 三端口拓扑，synthrt 端口需分线

**决策**：

| 端口 | 类型 | 说明 |
| :-- | :-- | :-- |
| synthrt（main 线） | 源码构建 | 新框架 |
| `wolf` | 源码构建 | 依赖上者 |
| `wolf-lang-packages` | 纯数据 | 从 wolf release 获取语言包 |

**落点见 A29**：synthrt（main 线）与 `wolf-lang-packages` 位于 wolf 仓内的 overlay；供 lite 消费的
`wolf` 端口待 lite 迁移时再议。共享子模块 `stdware/vcpkg-overlay` 继续提供通用第三方端口。

**分线问题（必须先解决）**：共享 overlay 中已有的 `synthrt` 端口固定在 **refactor 线**的提交上
（`HEAD_REF localization/passthrough-keys`，与 `convert-g2p-packages.py` 中固定的 `SOURCE_REF`
为同一提交），即旧栈（`srt-g2p`/`srt-s2p`/`plugins/G2P`），lite 正在消费该端口。wolf 需要的是
**main 线**，两者产出的包集不同，**不能共用一个端口名**。

> **2026-10-03 补记**：上面「`HEAD_REF localization/passthrough-keys`，与 `SOURCE_REF` 为同一提交」已过期——
> 共享 overlay 的 `synthrt` 端口现为 `HEAD_REF refactor`（`REF 0e3940dc79fdf5a943f18f14a5f01776226a93c4`），
> `convert-g2p-packages.py` 的 `SOURCE_REF`（`814bf81e6cd86b6670b2635032d842a997001a55`）是它的**祖先**。
> 本节结论（不能共用一个端口名、wolf 另建 `synthrt-main`）不受影响。

**采用方案**：wolf 仓内 overlay 新增 `synthrt-main`，与共享 overlay 中 lite 使用的 `synthrt`
并存且互不可见，lite 不受影响。备选方案「直接把共享 overlay 的 `synthrt` 升级到 main」被否决：
该方案要求两个仓库同时修改，任一侧受阻都会阻塞另一侧。

lite 将来采用新框架时，**由 lite 在自身仓库的 vcpkg 中提供所需端口**（A29 的作用域原则），
wolf 侧不为此预留动作。

**`wolf-lang-packages` 沿用同一 overlay 中 `ffmpeg-builds` 的既有范式**（`assets.cmake` +
portfile + config 模板 + usage + 生成脚本），不引入新模式。

---

### A22 — 变体键汇的三处合并

对照 synthrt `origin/refactor` 的实现逐条核对 `pipe-chain` 与 `algo-pinyin` 之后，按 A17 的
判据合并三处：

**① 打标条目形状统一。** 两个变体执行同一操作，即逐词分类为 `convert`/`copy`，却各有一套
名称：chain 用 `tagger` / `action`，pinyin 用 `verify` / `mode`。此外 chain 的实现只编译 `regex`
型，`array` / `dict` 被**静默丢弃**，而 pinyin 侧使用的 `Inferutil::Verifier` 三型俱全，未知型即
报错。

统一为 **`{type, value, mode}`**（Verifier 组件的既有形状，无须改动），chain 的打标步类型名
由 `tagAndValidate` 改为 `verify`，并改用同一组件，从而同时消除「静默丢弃两种类型」的缺陷。
容器名按语境选取：chain 为步参数 `entries`（不与步名 `verify` 重名），algo-pinyin 为
`configuration.verify`。

**② `enabled` 单轨。** 旧栈有两处独立的 `enabled`：step 项级（整步不入列，`G2pPipeline`）与
`params.enabled`（步内停用，`DictStep`/`ModelStep`）。两者表达同一语义，**只保留步项级**。

**③ `cleaner` 嵌套扁平化。** `cleaner` 对象只有 `operations` 一个成员，该层嵌套不携带信息，
扁平化为 `operations`。同时删除 `normalizeTones`，该参数被解析后**从未使用**。

**另外**：chain 的 `steps` 内联于 `configuration`，不引用外部 `chain.json`。`configuration` 本身
就是由 variant 全权规定的块，`formatVersion` 可直接写在其顶层（变体文档 §2.2）；再套一层文件
只多一级路径基准，不增加表达力。旧草案的 chain.json 方案正是在这一层写错了路径基准。

---

### A23 — `algo-pinyin` 的引擎由绑定推导，取消 `configuration.scheme`

**决策**：cpp-pinyin 的引擎（`Pinyin` / `Jyutping`）由本模块绑定的 `(language, scheme)` 直接
选定，`configuration` 不设选择键。

> **A30 后的形态**：模块成为多语言后端，因此「绑定 → 引擎」这一步经必选的
> `configuration.languageMap` 表达，`exports.languages` 与之逐项对账。**本条的实质未变**：选择键
> 仍不存在，引擎仍由绑定二元组推出；变化仅在于可服务的二元组不止一个，因而需要一张显式的映射表
> （同 A24）。

**动因**：

1. **同名异义**：旧草案拟用 `configuration.scheme`（取值 `mandarin` / `cantonese`），与语言
   身份的 `scheme`（注音体系，取值 `pinyin` / `jyutping`）在同一份文档中必然引起误读；
2. **可从绑定推导**（A17 判据一）：显式写出只会产生「`scheme: mandarin` 配
   `exports.languages: [{yue, jyutping}]`」这类可表示的非法状态。

**~~阻塞项 B1~~（已由 A30 / A31 解决）**：cpp-pinyin 的词典路径是进程全局状态，旧栈两个语言包
各带一份词典根，因此在同一进程内互相覆盖。解决方式见 A30（拆为共享后端包，一个进程一个根）与
A31（进程级仲裁器，异根即加载失败）。

---

### A24 — `multig2p-onnx` 的三处收敛

**决策**（详见变体文档 §5）：

1. **不设 `bundle` 路径键**：`bundle.json` 必须与模块声明位于同一目录。一份声明对应一个 bundle，
   路径可由声明位置确定，增设一个键只会多出一种表达同一信息的方式；
2. **`languageMap` 必选**，形状与 `exports.languages` 同族（`{language, scheme, ref}`），是
   契约二元组到 bundle 内部语言引用（`eng/default` 之类）的唯一通道。两者必须逐项对账：一个
   属于契约面，一个属于实现面，跨面不可互相推导，因此各自声明；
3. **无 `default_language`**：A16 之后执行体绑定单一二元组，不存在缺省语言。旧栈「未映射语言
   静默回退默认语言」（只记录缺失下标，产出循环从不检查）是缺陷，改为绑定期即失败。

`bundle.json` 的 `bundle_version` 即本变体的资源格式版本，按变体文档 §2.2 处理（原为字符串
`"1.0"` 且只校验非空，收敛为正整数 + 上限校验）。其余顶层键为发布侧元数据，契约不作解释。

---

### A25 — Num / Punc / Unknown 合并为直通语言包 `wolf/lang-zxx`

**决策**：三个套件合并为**一个**公共语言包，贡献 `zxx-passthrough`，
`language: "zxx"`、`scheme: "passthrough"`。

**合并不丢失信息的依据**（对照 `origin/refactor` 的 `config.json` 逐项核对）：三者都是**纯直通、
零资源**，各用一个 tagger 正则打 `copy` 标（`(\p{N})` / `(\p{P})` / `([.]+)`），再接
`fallback: useOriginal`。三者除正则外完全相同，且旧栈的 `tag` 字段（`number`/`punctuation`/
`unknown`）**无任何消费方**。契约面用 `mode=copy` 表达「原样保留」，宿主无须区分命中的类别。

**选用 `zxx` 的理由**：`zxx` 是 ISO 639-3 的正式代码，含义为「无语言内容」（no linguistic
content），与数字、标点、游离符号的性质相符。因此**无须为这三类输入破坏 `language` 的
`[a-z]{3}` 规则**，也无须使用私用码（`qaa`-`qtz`）这类不透明写法。

**取代**：原判定「三者移出语言域，去向列为开放问题」。原判定的理由是「`num`/`punc` 不是
ISO 639-3 代码」，那是把三者**分别**建模时的结论；合并后只需一个代码，而 `zxx` 恰好存在且语义
精确。

**边界不变**：`SP` / `AP`、连音 `-`、拆音续音符 `+` 等保留记号仍归宿主预过滤（运行时文档
§4.3）；歌词中的数字与标点由本包处理。

**连带收益**：本包零词典、零模型、无许可问题，却完整覆盖「`desc.json` + 语言声明 + 两个推理
模块 + 歌手映射」全链路，因此被列为发布链的**引导包**（实施方案 M3.5）。先用内容成本最低的包
打通整条发布链，比等 12 个包全部转换完成后再一次性验证更安全。

---

### A26 — A11 落地前的临时测试路径

**决策**：A11（synthrt `SingerCategory` 增加 `languages` / `defaultLanguage`）落地前，按下列三条
并行推进，**不阻塞 M1**：

1. **先完成 linguist 侧**：M1 中约七成用例（`language`/`scheme` 形状、ID 惯例、
   `exports.phonemes`、role 基数、二元组命中）全部位于 `linguist` 类别内，该类别由 wolf 自有，
   **无须修改 synthrt**；
2. **过渡承载：桩 singer provider + `configuration`**：`configuration` 本就在
   `SingerCategory` 白名单内，其内容由 variant 全权规定。wolf 提供一个测试用 singer provider
   插件（`SingerProvider` 基类已实现 `createExports`/`createImportOptions`/
   `createImportBinding`，桩只需覆盖 `createConfiguration`，约 30 行），测试歌手把映射放进
   `configuration.languages`；
3. **落地路径：端口附带 patch**：`vcpkg_from_github(... PATCHES ...)` 是 vcpkg 的标准机制。把
   A11 的两个字段作为补丁随 `synthrt-main` 端口分发，语义精确、CI 可复现，且无须等待上游合并。

**收敛纪律**：第 2 条的读取必须封装在**单个函数**（`readSingerLanguages(const SingerSpec &)`）
中。该函数当时读取 `manifestConfiguration()`，A11 落地后改为读取 `spec.languages()`，切换点
只有一处。

**不长期使用 `configuration` 的理由**：这是 A11 已否决的备选（与「`configuration` 不携带任何
语言列表」冲突，且只对特定 singer variant 成立，第三方 singer 契约无法复用）。本条只把它降为
**过渡承载**，不改变 A11 的目标形态。

**补记（2026-09-13）**：A11 已在 synthrt 落地（上游分支 `onnxruntime-builds-uptake`：
`SingerSpec::languages()` / `defaultLanguage()`，形状与 role 存在性由 `SingerCategory` 在开包时
校验；spec 2.4 歌手模块的声明文件一节已同步）。wolf 侧按收敛纪律只修改一处：
`readSingerLanguages()` 改为读取 `SingerSpec`，删除 `configuration` 分支，全部测试歌手声明的两个
字段上移到声明根；`check-declarations.py` 对仍留在 `configuration` 中的映射报错，避免其被加载器
静默忽略。第 3 条的端口 patch 不再需要：`synthrt-main` 端口直接固定在该分支的提交上。

---

### A27 — `scheme` 由 `language` 定域，取值逐语言定案

**决策一：`scheme` 的作用域。** 匹配键恒为二元组 `(language, scheme)`，而非单独的 `scheme`。
互换承诺因此只在**同一 `language` 下**成立：`(cmn, pinyin)` 与 `(deu, pinyin)` 是两个互不相干的
二元组，同名不构成任何承诺。此条原先在域契约 §2.1 中表述含糊，现已明确写出，它是决策二的前提。

**决策二：取值。** 对照 `origin/refactor` 的词典内容逐个判定：

| `language` | `scheme` | 依据 |
| :-- | :-- | :-- |
| `cmn` / `yue` | `pinyin` / `jyutping` | cpp-pinyin 的两个引擎 |
| `jpn` | `romaji` | `kana2romaji.txt` |
| `eng` | `arpabet` | `ds_cmudict-07b.txt` 为 `aa l ow` 形态，即小写 ARPABET |
| `zxx` | `passthrough` | A25 |
| `por` / `kor` / `ita` | `xsampa` / `romaja` / `xsampa-geminate` | A49 已定 |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位，P7） | 见下 |

**`eng` 取 `arpabet` 而非 `cmu` 的理由（由用户确定，且与字段语义一致）**：`cmu` 指的是**词典
来源**（CMUdict），而 `scheme` 按定义是**记法**。同一份 CMUdict 可转写为其他记法，同一套 ARPABET
也可来自其他词典，两者正交。生态中既有的 `eng-cmu` 写法在发布注记中显式说明对应关系。

**其余八种取 `ds` 的理由**（本段为当时的判断；A45 之后 `por` / `kor` / `ita` 三种已重新定名，
见 A49）：实测表明它们**都不是标准记法**：`deu` 作 `q aa`（`q` 为声门塞音）、`fra` 作
`ss` / `jj`（叠写）、`ita` 作 `a1` / `o1`（带重音数字）、`spa` 作 `a B`、`por` 作
`S` / `Z`（SAMPA 风格）、`rus` 作 `aa ay d nn i ll`、`fil` 作 `D A J` / `Q`。各自是 DiffSinger
生态中该语言的自有音素集。**以 `ipa` / `sampa` / `xsampa` 命名会造成误导**，因为它们并非这些
记法。`ds` 的含义是「DiffSinger 生态中该语言的既定音素集」；由决策一，八者同名不致混淆，且将来
的 `(deu, ipa)` 会正确地不与之匹配。

---

### A28 — 转换产物不进 git

**决策**：语言包的转换中间产物与成品**一律不进入 wolf 的 git 历史**。

| 位置 | 内容 |
| :-- | :-- |
| wolf git | 转换脚本；`wolf/lang-zxx` 的创作源（零资源，约 2 KB，属于源文件而非构建产物）；负面用例夹具 |
| gitignored 暂存区 | 转换的全部输出，位于 `build/lang-packages/`（现有 `.gitignore` 的 `build/` 已覆盖） |
| wolf release | zip 资产 + `manifest.json` |
| wolf git | `assets.cmake`（生成物，但只是文件名与 SHA512 构成的几十行文本，随 release 在同一提交中更新，见 A29） |

**理由**：语言资源包括 133k 行词典与 19 MB 模型，入库即永久增加仓库体积，release 资产是更合适的
载体。这也是 A19 选择「发布宿主为 wolf Release」的必然配套：若成品同时进入 git，同一份内容将存
两份，且 git 中的一份无法删除。

**溯源纪律（必须执行）**：转换脚本必须**固定 `origin/refactor` 的提交**（即
`convert-g2p-packages.py` 中的 `SOURCE_REF`），首次 release notes 记录该提交。否则 refactor
分支日后被删除或被 GC 后，源资源的唯一副本只存在于 release zip 内，转换无法复现。**这是本决策
唯一的实质风险，缓解成本极低，不得省略。**

---

### A29 — wolf 自带仓内 overlay，承载两个自有端口

**决策**：wolf 仓内新增 `scripts/vcpkg-ports/`，承载 **`synthrt-main`** 与
**`wolf-lang-packages`** 两个端口；`scripts/vcpkg-manifest/vcpkg.json` 的 `overlay-ports` 写为
有序两项，本仓 overlay 在前，共享子模块在后：

```json
"overlay-ports": [ "../vcpkg-ports", "../vcpkg/ports" ]
```

共享子模块 `stdware/vcpkg-overlay` 继续提供通用第三方端口（qmsetup、stdcorelib、boost-test 等），
不包含 wolf 自有的两个端口。

**理由**：

1. **`synthrt-main` 是过渡端口，只有 wolf 需要**：lite 仍在 refactor 线上，而该端口当时还需携带
   A11 的 patch。把一个只服务单一消费方且带补丁的端口放进 lite 也消费的共享仓，会给其他消费方
   增加无关内容；
2. **`wolf-lang-packages` 的 `assets.cmake` 每次 release 都重新生成**。端口位于本仓时，发布脚本
   可在**同一个提交**中更新它；原方案「生成 → 复制进 overlay 仓 → 推送 overlay → 两仓各自更新
   子模块指针」的四步跨仓同步随之消除；
3. **端口与其产物同仓，版本关系不会错位**。跨仓时 overlay 的某个提交对应哪一次 release 只能
   依靠人工记录，同仓则由提交本身确定。

**不复用 `synthrt` 端口名的理由**：wolf 的 overlay 排在前面，同名端口会**静默遮蔽**共享
overlay 中的同名端口。这种遮蔽在 manifest 上完全不可见，对比 wolf 与 lite「都依赖 synthrt」时
会造成误导。因此采用显式名称 `synthrt-main`。

**作用域原则**：**wolf 仓的 overlay 只承载 wolf 自己消费的端口。** 其他仓（lite）所需的端口由
该仓在自己的 vcpkg 中提供。因此本方案不产出「供 lite 消费的 `wolf` 端口」，也不为「把端口提升到
共享 overlay」预留步骤。

该原则使每个仓的依赖面自洽：一个仓的 manifest 与 overlay 即完整描述其依赖，无须跨仓推断。

**已知代价**：lite 将来若要消费 wolf 的语言包，其端口定义（含 `assets.cmake` 的文件名与
SHA512）会与本仓的定义重复，且两份会各自漂移。届时 lite 可以自建端口、引用本仓 overlay 路径，
或推动提升到共享 overlay。**这是 lite 侧的取舍，不在本方案范围内**。此处记录重复风险，供届时的
决策者参考。

---

### A30 — `algo-pinyin` 改为共享后端包，cmn / yue 改为链

**决策**：cpp-pinyin 引擎及其词典树发布为独立包 **`wolf/g2p-pinyin`**（变体 `algo-pinyin`，
契约仍为 G2P），`wolf/lang-cmn` 与 `wolf/lang-yue` 改为 `pipe-chain`，经 `imports` 的私有 role
`backend` 消费它。原 `configuration.dictPath` 更名为 `dictRoot`，新增 `formatVersion` 与
`languageMap`（必选，与 `exports.languages` 对账）。

**理由**由三条实测事实推出（锚点在变体文档 §6.1，取 cpp-pinyin `3924631`）：

1. 词典根是**进程全局状态**，但**只在 `ChineseG2p` 构造期被读取一次**：构造后实例自持四张词表，
   不再访问全局状态。因此问题不在于存在全局状态，而在于一个进程中存在两个根；
2. 两个引擎对应**同一根下的两个子目录**（`mandarin` / `cantonese`），一个根即可同时满足两者。
   旧栈两个语言包各带一份根，这才是冲突的直接成因；
3. 两份存量词典与 cpp-pinyin 自带的 `res/dict` **逐文件字节相同**（11 个文件中 10 个完全一致，
   唯一差异是 `mandarin/trans_word.txt` 少一行 `吒:咤`，即包内版本比上游旧一版）。词典属于**引擎
   载荷**，出现在语言包中本身就是旧栈的分层错误。

**选择「共享后端」形状的理由**。候选方案有四种：

| 方案 | 效果 |
| :-- | :-- |
| 收录纪律：约定所有 `algo-pinyin` 模块共用一个根 | 跨厂商不可执行，且与 spec 2.4 的包自包含原则相抵触 |
| 上游改造：把根改为构造参数 | 从根本上解决问题，但不在本项目控制范围内，落地前无效 |
| **共享后端包** | 在结构上消除第二个根，且与 `multig2p-onnx` 同形 |
| 仅进程级仲裁 | 把静默失效变为明确失败，但**无法使 cmn 与 yue 同时可用** |

采用**第三 + 第四**种：拆包保证正常情形正确，仲裁保证异常情形可诊断（A31）。第二种并行推进，
落地后仲裁退化为空操作。

**与 `g2p-multi` 的动机不同**：后者是为共享 19 MB 模型以节省体积；本包的两个词典子目录互不
重叠，拆包**不去重任何字节**，所换取的是「一个进程一个根」这一结构事实。

**附带结果**：cmn / yue 与其余 11 个语言结构一致，每个语言都是「`pipe-chain` 覆盖在共享后端之上」。
打标从引擎移入链的 `verify` 步（A22 已使两者条目形状完全相同，迁移无须转换），后端因此完全与
语言无关。

**发布侧联动**：包版本随 cpp-pinyin 端口版本变化，词典在打包时取自该端口的 `share/cpp-pinyin/dict`。
词典与读取它的引擎是一个整体，不应分别编号，也不应进入 git（A28）。

---

### A31 — 进程级词典根仲裁器

**决策**：`algo-pinyin` 插件内持有一个 provider 域的进程级仲裁器，登记本进程唯一的词典根：同根
放行，异根即**加载失败**，诊断中同时列出两个根；登记与引擎构造共用一把锁。

**拆包之后仍需仲裁器的理由**。A30 把「必然两个根」降为「正常情况下一个根」，剩余三个缺口：

1. 第三方另行发布一个 pinyin 后端包；
2. 声库逐项覆写后端模块，指向其他位置；
3. `setDictionaryPath` 与构造之间的**形式数据竞争**：即使两处写入同一个值，无同步的并发读写在
   C++ 内存模型下也是未定义行为。

一把锁同时覆盖「设置全局状态」与「读取全局状态的构造」，从而关闭第 1、3 项；第 2 项由异根检测
拦截。

**登记点在 Acquire**，不在执行体创建时：两个根冲突是关于**包**的事实，与绑定无关，应在承载
它们的那次加载中失败。这也符合三级失败模型，即**把静默失效替换为确定性的加载期失败**。

同处补充两条旧栈缺失的守卫：构造后校验 `initialized()`（旧栈只在 `start` 期报运行时错误）；保留
旧栈的词典目录预检，其注释记录了一次真实事故：路径不存在时 `Pinyin::Pinyin()` 会无限挂起，使
模块永久停留在 Loading 状态。

---

### A32 — 引擎实例每执行体一份，且不进资源缓存

**决策**：每个后端执行体各持一个引擎实例；`algo-pinyin` 的解析产物**不进入 `ResourceCache`**。

**理由**：`ChineseG2p` 的全部转换方法均为 `const`，却经 `d_ptr` 改写实例内的暂存数据
（`ChineseG2p_p.h:55-56`、`:67-73`，被 `ChineseG2p.cpp:181` 等六处调用）。因此单个实例不能
承接并发转换；旧栈 `PinyinG2pTaskImplBase::start` 只取 `shared_lock`，实际放行了并发写入。记为
**B1-b**。

资源缓存的契约是「**只读**解析产物」（资源缓存文档 §1），放入可变对象会破坏该不变式。
与其让缓存隐式破例，**把该变体排除在外并写明理由**更为明确。代价是内存 ×k（每份约 1–2 MiB
的词表），换取无锁的真并行。

曾考虑的替代方案：进程内共享一个实例，每次调用加互斥。内存 ×1，临界区只是哈希查表，争用可忽略；
但共享的是可变对象，仍须在缓存文档中为其破例。若上游把暂存改为局部变量，第三种方案才成立：
词表进入缓存，暂存保留在执行体中。

---

### A33 — `model` 步按极大连续段分批

**决策**：`pipe-chain` 的 `model` 步只作用于 `mode=convert`、未丢弃、尚无发音的词，但送入后端
时按**原词序中的极大连续段**切分，不把过滤后的词压平成一张表；`batchSize` 缺省时不再切分。

**理由**：后端可以跨相邻词获取上下文。`algo-pinyin` 的短语表即是如此：「银行」与「银 · 行」读音
不同。被其他词隔开的两个词不相邻，把它们拼进同一次调用会产出错误读音。旧栈的
`PinyinG2pTaskImplBase` 用 `groupLyrics` 按连续同 mode 段分组，依据相同；旧栈的 `ModelStep`
则是压平后按 `batchSize` 切分，因为它服务的是逐词模型。

`batchSize` 缺省时不切分，因此对上下文敏感的后端无须设置它；逐词模型（`multig2p-onnx`）显式设置
也不受影响。**相邻性是语义的一部分**，此条须写入契约，不能依赖实现的偶然行为。

---

### A34 — 不合式词典由转换管线归一化，读入端保持严格

**决策**：`dict` 步的词典读入**严格**：分隔符只接受制表符，行内缺列即加载失败（另有 BOM 剥离与
CMU 式 `word(n)` 归并两条约定）。存量中不合式的词典由 `scripts/convert-g2p-packages.py` 改写为
规范 TSV，**不依靠放宽读入端**。

**实测清点**（10 份现有词典）：

| 文件 | 情况 |
| :-- | :-- |
| `fil_dict.txt` | 24752 行**全部用空格分列**，无制表符 |
| `kor_dict.txt` | 开头 9 行 `;;;` BibTeX 引文 |
| `ds_cmudict-07b.txt` | 开头 1 行 `;;;` |
| 其余 7 份 | 规范 TSV |

**其中第一条是此前未被发现的缺陷**：旧栈的 `PhonemeDict::load` 只接受制表符且**静默跳过**
不合式行（`PhonemeDict.cpp:188-196`），因此菲律宾语词典的 24752 条**从未被读取**，该语言的
`dict` 步一直空转，全部落入兜底。

可选方案有两种：读入端接受空白分列（fil 立即可用，但格式定义放宽为任意空白），或修正数据
（读入端保持单一标准）。采用后者：**转换管线的职责正是把旧资源迁入新格式**；同时静默跳过改为
加载失败后，同类问题今后在首次出现时即被发现。

---

### A35 — `synthrt-main` 的 `onnx` feature：ORT 载荷由端口就位

**决策**：`synthrt-main` 端口新增可选 feature `onnx`，打开 `SYNTHRT_BUILD_DSINFER` 与 ONNX
驱动，并依赖共享 overlay 已有的 `onnxruntime-builds`。不启用时行为不变（dsinfer 关闭）。

> **后续（A80）**：synthrt 已改为自行 `find_package(onnxruntime-builds)`，下文「由端口铺设头文件」
> 一段已从端口中删除。

**约束下的解法**。两条 synthrt 线的 ORT 接线不同：`refactor` 已改造为
`find_package(onnxruntime-builds)`，而当时的 `main` 仍从
`${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime/default/include` 取头文件，目录不存在即
`return()`（`dsinfer/util/onnxutil/CMakeLists.txt:1-8`）。修改上游违反 C1，因此**由端口在
configure 前把头文件铺设到 main 期望的位置**。

**只需头文件**：驱动用 `stdc::SharedLibrary` 在运行期 `dlopen` 并手动解析 `OrtGetApiBase`
（`Runtime/OnnxRuntime.cpp:133-171`，配合 `ORT_API_MANUAL_INIT`），链接期不引入 ORT 库。运行期
把 `share/onnxruntime-builds/runtime/default/` 传给驱动即可。

**flavor 取默认值**：`onnxruntime-builds` 的默认 flavor 在 Windows 上是 DirectML NuGet，在其余
平台上是 GitHub 的 CPU 包，符合需要；`cuda12` 是可选 feature，不启用。另显式传入
`DSINFER_ENABLE_CUDA=OFF`（上游缺省为 ON）。实测产出的 `libonnxdriver.so` 的 NEEDED 中不含任何
CUDA 库。

**做成 opt-in 而非常开**：不需要 multig2p 的构建无须承担 ORT 载荷与 dsinfer 编译。

dsinfer 在 main 线上随 synthrt 一起安装自己的 CMake 包（`lib/cmake/dsinfer`）。端口对两个包各做
一次 config fixup，并保留父目录，因此消费方可直接 `find_package(dsinfer CONFIG)` 获得
`dsinfer::dsinfer`。驱动本身不是链接目标，按插件搜索路径在运行期加载，与其他驱动一致。

### A36 — 驱动归属决定降级边界

**决策**：`multig2p-onnx` 的 ONNX 驱动由**宿主**创建、初始化（execution provider 与 ORT 路径）
并经 `SynthUnit::addRuntimeService` 注册；模块按后端名查询 Runtime Service，自身从不加载驱动。

**该归属线同时划定两种失败的边界**：

| 情形 | 归属 | 处置 |
| :-- | :-- | :-- |
| 进程内没有驱动 | 安装环境 | 包正常加载，逐词返回 `DriverUnavailable`，由链上 `fallback` 兜底 |
| 驱动存在、模型无法打开 | 包 | **会话期执行体创建失败**（conversion-time executive creation failure），包仍为已加载 |

> **本行的失败不叫「加载失败」**，两者的发生点与后果都不同。规范里的「加载失败」指**加载事务**
> 失败并按完成日志反序 rollback（spec 2.4:420-429 的 Probe / Acquire / Ready / Commit，spec
> 2.4:444、spec 2.4:454-464），包不得 Commit，**整包因此不可用**。而模型是在**执行体创建**时才
> 打开的，那是包 Commit 之后、由 `warm()` / `convert()` 触发的**会话期**动作
> （`src/lib/Session/LinguistSession.cpp:471` 的 `build()`，其失败在 `:595` 记入失败缓存）。
> **会话期执行体创建失败**只把该 (歌手, 语言) 记为 `Unavailable`，`reason` 取自失败层的诊断
> （`recordFailure` `:482-491` 写入失败缓存；`probe()` 命中缓存即据此报告，`:734-744`）；失败缓存
> 的键是 (歌手, 语言) 二元组（`:573`、`:734`），因此不波及其他语言；包本身仍为已加载状态，
> `probe()` 仍能在目录中找到该歌手，而不是报「no such singer」。
>
> **会话期失败有两个来源，二者都进同一张失败缓存**：一是**执行体创建失败**（模型打不开、词典
> 读不到，`build()` 的失败在 `:593-597` 记录），二是**执行体整批运行失败**（`convert()` 里
> `start()` 返回错误，`:861-871` 记录；逐词失败只是转换结果，不记录）。两者都让该 (歌手, 语言)
> 变 `Unavailable` 直到下一次 `refresh()`；**取消不是失败**，也不记录（被取消的运行仍然返回结果，
> 只是任务状态置 `Canceled`，`include/wolf/Support/ExecutiveTask.h:52-71`）。

**缺少驱动不判加载失败的理由**：否则在未安装驱动的机器上整个语言包不可用；而九个语言包都依赖
这一个后端，相当于九种语言同时失效。契约已为此类情形预备逐词的 `DriverUnavailable` 通道，
`pipe-chain` 的 `fallback` 步再把它收敛为「原词直通」。

**模型无法打开即判失败的理由**：驱动存在而模型仍无法打开，表明包本身已损坏。这不是环境问题，
安装任何组件都无法修复。**但这一损坏在加载期无从发现**：加载事务只校验声明、`exports` 与
`configuration`（spec 2.4:425-426），模型要到执行体创建时才打开，因此它是会话期失败而不是加载失败
（同上表的术语区分）。

旧栈在两种情形下都只告警并回退为 copy，因此「模型缺失」与「驱动缺失」在现象上无法区分。

---

### A37 — Level 1 只做贪心解码，beam 相关键取值受限

**决策**：`multig2p-onnx` 只实现贪心解码。`beamSize` > 1、`topK` > 1、`lengthPenalty` ≠ 0
一律**加载失败，并在诊断中指明不支持**。

**理由**：

1. **现有的唯一资源不需要该功能**。`wolf/g2p-multi` 的配置是 `beamSize: 1, topK: 1,
   lengthPenalty: 0.0`；
2. **接受键却忽略其值会错误地表述模块能力**。旧栈的 beam search 有 366 行，且**没有任何现有资源
   用到它**，因而没有可供对照的输出。照搬一份无法验证的实现比明确拒绝更危险，因为前者会使用户
   误以为该功能可用。

`topK > 1` 的真正候选必须由 beam search 产出，因此两个键受同一限制，不存在「贪心 + topK」的
中间状态。

**将来补充 beam search 属于实现增强**：`configuration` 键汇不变、`formatVersion` 不变、`level`
不变，只是原本失败的取值开始生效。因此现阶段拒绝这些取值是安全的：放宽不是破坏性变更。

---

### A38 — 保留符号按名称查询，不按下标

**决策**：bundle 词表的四个保留符号（`<unk>` / `<pad>` / `<bos>` / `<eos>`）在词表中**按名称
查询**，缺少任一即加载失败。

**理由**：旧栈硬编码 `unk=0 / pad=1 / bos=2 / eos=3`，这是导出惯例而非格式约束。调整过位置
的词表不会报错，而会以四个错误的 id 完成整个解码，产出**看似合理的音素序列**，既不像崩溃也不像
空结果，属于最难发现的一类故障。按名称查询把这类故障变为一条加载期诊断。

同时把 `lookup` 从线性扫描（758 个符号 × 每字符一次）改为哈希表。

---

### A39 — 脚本沙箱的边界与 `utf8`

**决策**：`lua` 变体的沙箱在 `luaL_openlibs` 之后移除 `io` / `os` / `debug` / `package` /
`require` / `module` / `dofile` / `loadfile` / `load` / `loadstring` / `collectgarbage`；补充一个
`utf8` 库；脚本在 Acquire 期编译并校验入口全局函数；`lua_State` 每执行体一份，不进入缓存。

**语言包是数据，数据不应具备打开文件的能力。** 旧栈的移除清单**遗漏了 `load` 与 `loadstring`**，
两者都能从字符串构造并运行新代码，使沙箱存在缺口。本实现补上了这两项，并有用例断言这些名称在
脚本中全部为 `nil`。

**`utf8` 提供完整接口**：解释器是 LuaJIT（Lua 5.1 语义），不带 `utf8`；使用非 ASCII 记法的脚本
甚至无法遍历自身的输入。既然采用标准名称，就应提供 Lua 5.3 的完整语义（`char` / `codepoint` /
`codes` / `len` / `offset` / `charpattern`）。形似的子集会使按手册编写 `utf8.offset` 的作者调用
一个不存在的函数。

**加载期编译**：编译失败或未定义入口函数即包加载失败，不推迟到第一次转换。Onset 脚本返回的表
长度与输入不符同样立即报错：契约规定每个音素对应一个标记，较短的表会使词尾静默缺失标记。

**不进入缓存**：被共享的将是执行上下文而非只读解析产物，与资源缓存文档 §1 的不变式冲突（与 A32
依据相同）。各执行体共用的只是模块读入一次的源文本。

---

### A40 — 带外部后端的变体条件构建

**决策**：`multig2p-onnx`（需要 dsinfer）与 `lua`（需要 LuaJIT）按依赖是否存在条件构建。测试桩的
`plugin.json` 由构建生成，**只声明本次构建未承载的三元组**。

**理由**：两个变体各自依赖一份体量可观的外部依赖，不做模型推理的构建不应为此承担代价（与 A35
把 ONNX 做成 opt-in 的理由相同）。

**桩的声明必须随之收缩**，否则真插件与桩会同时可被发现，结果取决于「首个全匹配者当选」的目录
顺序，这正是 §1.2 收录纪律要避免的可表示歧义。生成 `plugin.json` 使「本次构建缺少的三元组由桩
补充」成为构建期事实，无须人工维护两份清单的同步。

两种构建当时均经验证：完整构建 14 个测试，最小构建（无 dsinfer、无 LuaJIT）12 个测试，且 15 个包
在两种构建下都加载通过。（A65 记录了此后最小构建的失效与修复。）

### A41 — 推理执行体的取消口径

**决策**：五个推理解释器的执行体一律实现协作式取消。口径有三条：

1. **取消不是失败**：`state()` 取 `Canceled`，已完成的词照常返回。调用方需要了解链执行到的位置，
   而 `Error` 只表示执行未成功；
2. **无执行时 `stop()` 为空操作**：若越过本次执行的取消请求保留到下一批，「停止一个已结束的
   批次」会静默取消下一个批次，这是最难排查的一类故障；
3. **「词边界」对可能自身陷入死循环的实现不成立**，此类实现须另备中断手段（A42）。

> **后续（A73 补记）**：第 2 条已修订。子执行体现在把入口处未消费的停止请求视为对本批的取消，
> 陈旧停止请求由父级 linguist 执行体识别并重试该阶段。

**此前缺失的原因**：运行时文档写明「实现须在**词边界**响应」，而五个执行体的 `stop()` 一律
`return {}`，即请求被接受却从不生效。表查询的批次以微秒计，因此未被察觉；`lua` 则可能永久
挂起。这是一条文档要求而实现遗漏的口径，已在复核时补齐。

---

### A42 — 脚本沙箱必须关闭 JIT，且移除 `jit` 全局

**决策**：`lua` 沙箱在 `luaL_openlibs` **之后**调用
`luaJIT_setmode(..., LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`，并同时移除 `jit`。

**这是实测结论，而非推理结论**。用最小复现程序验证了三种配置：

| 配置 | 裸 `while true do end` + count hook |
| :-- | :-- |
| JIT 开启 | **永久挂起**（钩子在编译出的 trace 内不触发） |
| `luaJIT_setmode(OFF)` 在 `luaL_openlibs` **之前** | **永久挂起**（开库操作重新打开了编译器） |
| `luaJIT_setmode(OFF)` 在 `luaL_openlibs` **之后** | 钩子触发，调用被中断 |

**反直觉之处**：运行时间足够长、值得被 JIT 编译的循环，恰恰最需要能被中断。钩子在冷代码上
有效、在热代码上失效，因此只使用短脚本的测试会全部通过。

**`jit` 全局同样须移除**：否则脚本调用 `jit.on()` 即可重新打开编译器，使停止机制失效。沙箱的其余
移除项防止脚本越出沙箱，此项防止脚本关闭中断机制。

**代价**：解释执行。这些脚本每词只做几次字符串操作，开销可忽略。

**旧栈同样安装了 count hook，且全树没有任何 `luaJIT_setmode` 调用**，其取消机制对唯一的目标场景
（失控脚本）无效。

---

### A43 — 转换词的 `pronunciation` 即 `candidates` 首项

**决策**：`pipe-chain` 与 `algo-pinyin` 都保证这条不变式；链在消费后端结果时也就地归一化。

**成因**：cpp-pinyin 的候选取自**逐字表**，而读音可能取自**短语表**，两者恰在「由短语决定读音」
之处分歧。「银行」的「行」读 `hang`（短语表），而逐字候选是 `["xing", "hang"]`，首项 `xing`
≠ 读音。链推理契约 §3.4.4 明确规定 `convert` 时「`pronunciation` 即 `candidates` 的首个
元素」，因此这是确定的违约。

**处置**：把读音移到首位，其余候选保持顺序跟随且不重复。信息不减少，只是顺序与契约对齐。

**链侧执行同样的归一化**：链把后端结果**作为自身的输出**重新发出，该不变式因此是链自身的义务，
不能依赖每个后端都正确实现。

---

### A44 — 无 `fallback` 步时仍须上报未产出

**决策**：链在结果装配处增加一道判定：`mode=convert`、发音为空、`error` 为空的词，一律置为
`PhonemeGenerationFailed`。

**成因**：此前只有 `fallback` 步会设置该 error。在**不配置 `fallback` 步的链**（变体文档 §4.2
明确允许）中，词典未命中的词会以「发音为空 + error 为空」产出，同时违反两条约束：契约要求转换词
的发音是候选首项（两者皆空，无从成立），且 `error` 为空即表示成功。

调用方因此无法区分「该词未查到」与「该词的读音为空」。变体文档 §4.2 早已写明「链未配置
`fallback` 步时同上」，实现遗漏了这一条。

### A45 — `scheme` 命名规则

**决策**：`scheme := <base> ( "-" <qualifier> )*`，取值规则见发布文档 §2.1.1。要点有两条：

1. **`<base>` 必须描述音素集本身**：标准记法名、社区通用名，或描述其特征的新造名。**禁止使用
   来源、生态、版本、「默认」之类不描述音素集的词**；
2. **`<qualifier>` 仅表示同族派生**。并列关系的两套音素集各取自己的 `<base>`。

**触发该规则的具体问题**：共享 bundle 为德语提供了两套音素集，`deu/default` 与
`deu/marzipan`。把后者命名为 `ds-marzipan` 会表示「marzipan 是 ds 的一个变体」，而逐符号清点
表明两者是并列的两套（`deu/marzipan` 含 `ueh oeh ei au eu xh tsh dsh rh rx vf cl`，与
`deu/default` 的 ARPABET 形状几乎不相交）。**可表示的虚假关系比缺少名称危害更大**。

反之，`eng/plus` 确实是 ARPABET 增加 `ax dr dx tr` 四个符号，因此 `arpabet-plus` 名副其实；
规则允许该命名，正是因为这一从属关系真实存在。

**该规则同时判定了 `ds` 的问题**：`ds` 描述的是音素集的使用者，而非音素集本身，因而既无法区分
同一语言的两套音素集，也不提供音素集形态的任何信息。逐符号复核后，`por` / `kor` / `ita` 三种
已按第 1 档定名（A49），其余五种仍待 P7 按第 3 档造名，`ds` 是此期间的占位值。

**扩展性**：新记法归入第 1 档，新社区集归入第 2 档，同族新分支增加 `<qualifier>`；三者都不改动
既有取值，也不需要中心登记表。唯一性只需在语言内成立（二元组由 `language` 定域）。

---

### A46 — bundle 的全部语言引用都进 `languageMap`

**决策**：共享模型的 12 个内部语言引用**全部**映射为契约二元组并写入 `exports.languages`。

**理由**：契约只要求「映射的 `ref` 必须存在于 bundle」，不要求反向成立；但**无映射的音素集
无法被任何声库选用**。资源已在包内、模型已能生成，隐藏它们不节省任何字节，只会使其不可用。

先前只映射九个 `*/default`，理由是「其他音素集需要各自的 scheme 名，而当时尚无」。A45 之后
该理由不再成立：三套中两套有通用名，一套是真实派生。

---

### A47 — `verify` 步不得出现在产出步之后

**决策**：`pipe-chain` 在加载期拒绝任何把 `verify` 排在 `dict` / `model` / `fallback` 之后的链。

**成因**：分类决定产出步**可作用于哪些词**，因此属于产出之前的步骤。排在之后的 `verify` 会重新
裁决已经获得发音的词，把这样的词标为 `copy` 会使其输出原词并静默丢弃词典命中。这样的链看似
「再精修一遍」，实际上丢弃了已完成的工作。

**选择拒绝而非定义语义的理由**：两种可能的语义各有依据（「以标记为准」与「已产出者优先」），
而没有任何现有链需要后置 `verify`。为一个无人使用的构造选定一种语义，会在声明面上留下一个容易
被误读的构造；拒绝它则在加载期消除歧义。

`format` 不是产出步，因此 `verify` 排在 `format` 之后合法且有用：此时尚无任何发音，而分类可以
基于清洗后的文本。

### A48 — 链模块的 `exports.symbols` 是各步产出的并集

**决策**：一条 `pipe-chain` 的 `exports.symbols` 按**各产出步音素集的并集**声明；`dict` 与
`model` 两步的音素集**不要求一致**。

**成因**：A34 使 fil 的词典首次可读，随即暴露出「同一条链产出两套音素」的问题。逐包清点表明
这并非 fil 独有：

| 语言 | 词典行含模型不产出的记号的比例 |
| :-- | :-- |
| `fil` | 100%（整套大写集，与模型的小写集几乎不相交） |
| `ita` | 75%（`a1 e1 i1 o1 u1` 重音标记） |
| `eng` | 44%（`ax dx`，即 `eng/plus` 的扩充符号；另有 `_r`） |
| 其余六种 | 0 |

**关键事实：`eng` 与 `ita` 的错配早已存在，且已随 `lang-v0.1.0.0` 发布**，因为它们的词典一直
有效。只有 fil 的词典此前从未被读取。因此「关闭 fil 的词典以保持输出统一」缺乏依据：这会保留两个
更大的错配，只隐藏最显眼的一个。

**处置**：词典保持可读。这是包的内容缺陷，归 M5；在发布前暴露优于发布后发现。

**但「M5 按并集声明 `exports.symbols`」这一推论只有一半成立**，补正如下。链的符号全集是各产出步
输出的并集**再加上兜底步可能产出的内容**，而链推理契约 §3.1 要求 `symbols` 是「可能输出的原子
符号**全集**」，不完整的声明即为虚假声明。因此：

> **`exports.symbols` 与 `useOriginal` 原词兜底互斥。** 原词兜底可把任意歌词作为发音产出，符号
> 集因此无界，`symbols` 只能省略（§3.1 允许省略，宿主应告警）。

实测**全部 12 条转换出的链都使用 `useOriginal`**，因此它们目前都不能声明 `symbols`。需要静态
可比对的语言包必须先放弃原词兜底（改用固定的 `defaultPronunciation`，或不配置兜底步）；这是
M5 须先作出的取舍，而非是否编写一份清单的问题。（此后 `openSet` 使宿主能够获知这一情况，见 A60。）

声库侧的义务不变：其音素表须覆盖链实际产出的并集，否则问题会推迟到运行期。

**附带的 P7 输入**：`eng` 词典使用的是比 `arpabet` 略丰富的变体，语言包应绑定
`(eng, arpabet)` 还是 `(eng, arpabet-plus)` 需要生态知识，与其余五种 `ds` 的造名一并确定。

### A49 — P7 分批定名，只改有确凿证据的取值

**决策**：按 A45 逐符号核对之后，三种语言定名，五种保留 `ds` 占位。

| 语言 | 取值 | 档位与依据 |
| :-- | :-- | :-- |
| `por` | `xsampa` | 第 1 档。`E J L O R S X Z dZ tS` 十个特征符均为 X-SAMPA，鼻化用 `~` |
| `kor` | `romaja` | 第 1 档。修正罗马字的 `eo` `eu`、紧音 `jj kk pp ss tt`，大小写区分初声/终声 |
| `ita` | `xsampa-geminate` | 第 1 档 + 限定。以 X-SAMPA 为基础，叠写表示双辅音（`dZZ tSS JJ LL SS EE OO`） |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位） | 第 3 档，须造名；**现有证据不足以造出有依据的名称** |

**不一次性定名的理由**。五个待定取值各有问题：

- `deu` 以 ARPABET **记法**扩展德语音（51 个符号中 38 个是 ARPABET 原符号）。命名为 `arpabet`
  可以成立，但将来另一套德语 ARPABET 变体将无法与之区分；
- `fra` 与 `rus` 都以叠写辅音区分辅元音，`doubled` 之类的造名只描述一个特征，未必是该音素集最
  重要的性质；
- `spa` 一半是 X-SAMPA 浊擦音 `B D G`，一半是正字法二合字母 `ch ll rr gn`，命名为 `xsampa`
  与事实不符；
- `fil` 与 ARPABET **无交集**（区分大小写），也不是 X-SAMPA，是一套自成体系的大写集，无法确定
  应以何种特征命名。

**错误的名称比空缺的名称更难修改**：空缺明确表示待办，错误的名称会被当作事实引用。因此只落地
有确凿证据的三条，其余留在 P7，并附逐套音素清点（发布文档 §2.2）。

---

### A50 — 保留「查不到即输出原词」，放弃加载期音素比对

**决策**：十二个语言包的链保留 `fallback` 的 `useOriginal`，因而不声明 `exports.symbols`，
宿主无法在加载期比对音素。

**取舍的两端**：

| | 保留原词输出 | 改为固定串 |
| :-- | :-- | :-- |
| 用户看到的结果 | 与旧栈一致 | **改变**：查不到的词不再输出原词 |
| `exports.symbols` | 无法声明（原词无界） | 可声明 |
| 音素不匹配的暴露时机 | 运行期 | 加载期 |

采用前者。**行为一致性优先**：原词输出是旧栈一贯的行为，改变它会改变全部十二个语言包的可观察
输出，而收益只是把一类诊断从运行期提前到加载期。链推理契约 §3.1 允许省略 `symbols`（宿主应
告警），因此这是契约预留的合法路径，而非规避。

记为**已知取舍**而非缺陷：将来若某个语言包需要静态可比对，由该包改用固定的
`defaultPronunciation` 即可，无须修改契约或其他包。

### A51 — `stop()` 向下传递到子执行体

**决策**：`LinguistExecutive::stop()` 除置位自身标志外，同时对已创建的 G2P / S2P / Onset 子
执行体各调用一次 `stop()`。三个子执行体指针改为原子类型。

**成因**：A41 使五个推理执行体都实现了取消，但**宿主只能停止 linguist 执行体，而该执行体不向下
传递**，因此这些实现从未被触发。语言执行体把**整批词**一次交给某个阶段（见
`LinguistExecutiveImpl::start` 中一次性构造的 `g2pInput`），因此只作用于本层的停止请求须等该次
调用自行返回。对表查询而言这只需微秒，对模型或脚本而言这正是需要中断的等待。

**实证**：一条 S2P 为死循环脚本的测试链，去掉传递即挂起（实测 25 秒超时），加上传递后在 50 毫秒内
报告 `Canceled`。

**指针原子化**：`stop()` 可能在另一线程上、与转换创建子执行体同时到达。原先的裸指针在此处存在
数据竞争，且传递操作须能观察到已创建的子执行体。

### A52 — 契约 Schema 是发布物，且必须与加载器一致

**决策**：四个契约的 `exports` 与 `imports[].options` JSON Schema 位于 `docs/schemas/`；
`scripts/check-declarations.py` 用它们校验真实包的每一份声明。

spec 2.4:596-602 把这两份 Schema 列为 interface 契约的**必需**内容，此前一直缺失。

**一致性是重点，而非附带要求**。编写 Schema 后逐条与加载器对照，发现三处发布物比实现严格：

| 项 | Schema | 加载器（当时） |
| :-- | :-- | :-- |
| `exports.languages[].language` | `^[a-z]{3}$` | 任意字符串 |
| `exports.languages[].scheme` | `^[a-z0-9]+(-[a-z0-9]+)*$` | 任意字符串 |
| 二元组内的未知键 | 拒绝 | 忽略 |

**收紧加载器而非放松 Schema**：`language: "english"` 的二元组永远无法匹配任何语言，因为语言身份
一侧按 `[a-z]{3}` 校验，所以它是一条**不产生任何效果的声明**，在加载期指出优于静默保留。二元组内
的未知键同理：键名拼错时正确的键即缺席，而缺席已由 `required` 检出，因此未知键只构成噪声。

两条身份文法因此从 `LinguistContrib.cpp` 的匿名命名空间移到 `ManifestValues`：身份字段与
exports 中的二元组应遵循同一条规则，各保留一份副本终将产生分歧。

**校验器自带边界声明**：校验器只实现这些 Schema 用到的关键字，遇到其他关键字时**报错而非放行**。
静默跳过无法识别的约束的校验器，比没有校验器更有害。

**与 Q4 的关系**：该脚本即打包期 lint 工具，不单独立项。域契约 §12 中**只依赖声明本身**的两项
（未被 `languages` 引用的 import、贡献 ID 书写惯例）已接入为**警告**；另两项（音素集交集裁决、
`symbols` 的归属口径）需要声库或模型的音素表，已定为编辑器运行期的职责，打包期无法检查。

**严格度与运行时对齐**：schema 不符判为**错误**（加载器同样拒绝），lint 项判为**警告**（加载器
不据此失败）。`make-lang-release.py` 在打包前调用该脚本，并**在有错误时拒绝打包**。打包前是纠正
不合式声明成本最低的最后时机。

### A53 — 第四位版本号是打包修订号

**决策**：转换出的包版本形如 `<源版本三位>.<打包修订号>`。管线对同一输入产出不同结果时，递增
第四位。

**必要性**：前三位描述的是 `origin/refactor` 中的源资源，而本项目的转换管线独立于源资源演进：
归一化词典、补充 `languageMap`、增加 linguist 贡献，每一次都使同一份源资源产出不同的包。若无
第四位，两个内容不同的包将共用同一个版本号，按目标版本求解的消费方获得哪一个包取决于其下载到
的是哪一个。这类问题最难复现。

第四位因此具有确定的含义，而不只是补齐四段的填充。

---

### A54 — 后端包声明兼容区间，依赖方指向下沿

**决策**：后端包（`g2p-multi`、`g2p-pinyin`）的 `compatVersion` 取修订号 **0**；语言包的
`compatVersion` 等于 `version`；依赖方把目标版本写为后端的 `compatVersion`。**其中关于语言包
的一条已被 A68 推翻（语言包同样取修订号 0），本条只有后端包与「依赖方指向下沿」两句仍然有效。**

**理由**：打包修订之间变化的是**打包**，而非依赖方绑定的对象。模块 `ref` 及其契约未变，因此按
修订 0 构建的消费方仍应由修订 3 服务。~~语言包则无人依赖，且 `cmn` / `yue` 的模块声明形态确实
变了（发布纪律第 4 条），故各自是一个点。~~ **「语言包无人依赖」在记录本条的同一批工作中即已被
A66 推翻，见 A68。**

**本条由加载器行为促成，而非推演得出**：依赖版本原先写为后端当前版本的字面量，第一次递增修订号
时，九个语言包同时报告「no installed Package satisfies dependency wolf/g2p-multi」。区间机制正是
为此类情形而设，此前只是未被使用。

**边界**：改变了声明形态的修订仍属破坏性更新，须手工提高下沿；自动化只覆盖「修订是纯打包变化」
这一常见情形。

---

### A55 — B3 分批：四种语言先闭包

**决策**：`eng` / `por` / `kor` / `ita` 补齐 `linguist` 贡献与 `direct` S2P，成为完整的语言闭包。

**B3 可拆分**，拆分后分为三档：

| 档 | 语言 | 状态 |
| :-- | :-- | :-- |
| 可直接闭包 | `eng` `por` `kor` `ita` | G2P 已产出空格分隔的音素 ⇒ S2P 用 `direct`；`scheme` 已定（A49）；Onset 契约允许省略且无规则资源 |
| 等待定名 | `deu` `fra` `spa` `rus` `fil` | 同上，但 `scheme` 仍是占位（P7） |
| 需要内容决策 | `cmn` `yue` `jpn` | G2P 产出的是**音节**，S2P 需要真实的音节→音素词典，全仓没有 |

> **后续（A66）**：第三档的前提已被推翻。该词典是**歌手包的内容**而非语言包的内容，全仓没有
> 该词典正是因为它不属于本仓。这三种语言只提供 `inference` 是最终形态，而非待补的缺口。

**`exports.phonemes` 取自该语言自身的词典**，词典是该语言所用音素集的权威来源。模型产出的词以及
「查不到即原样返回」的词都可能超出该清单；域契约 §4.1 把该清单定为**对齐基准**，且 Level 1 不做
加载期强制校验，A50 的取舍正依赖于此。若将来要使其成为硬约束，须先放弃原词返回。

**S2P 声明其服务的二元组**，而不按变体文档「`direct` 应省略 `exports.languages`」的通例省略：
该通例针对通用模块，而语言包内的 `direct` 专为该语言服务，声明二元组才能使二元组匹配在加载期
执行，而非留给宿主告警。

### A56 — 层栈增 L6 会话层

**决策**：新增 `wolf::LinguistSession`，位于 L5 之上，承载歌手→语言快照、就绪状态与负缓存、
执行体池、保留词与空词处置、取消句柄。完整设计见 [linguist-session.md](linguist-session.md)。

**动因来自现场证据，而非推演**：ds-editor-lite 已在 refactor 线上自建了一套同类实现
（`VoicebankSession` + `GetPronunciationTask` / `GetPhonemeNameTask` 中的附属逻辑）。其中补充的
每一项，即 `readyLanguages` / `failedS2pLanguages` 两个集合、SP/AP 特判、按语言分组、分阶段
就绪，都不是 lite 的业务判断，而是语言域的实现细节。

**只有一个正确答案的问题不应由每个宿主各自实现**：「失败缓存保留多久」「并发创建几个执行体」
「SP 算 copy 还是 skip」：三个宿主会产生三种不一致的行为，其中至多一种正确。

**框架不提供这一层**：`srt::SynthUnit` 只负责包、插件与 Runtime Service 的注册，不提供扫描、
就绪状态或路由缓存（`SynthUnit.h:25-95`）。因此该层要么由 wolf 提供，要么由每个宿主各自实现。

**L6 位于 L5 之上而非之下**：L6 是宿主**持有**的对象，而非宿主**实现**的层。L5 仍是策略面。

---

### A57 — 会话借用 SynthUnit，不另立数据模型

**决策**有三条：

1. **借用而不拥有 `SynthUnit`**：宿主可能还挂载了其他类别与服务，包的打开与关闭取决于宿主对搜索
   路径与安装的判断；
2. **歌手用 `srt::ContribLocator` 标识**，不另行定义键。这是框架自身的身份，持有 `SingerSpec`
   的宿主直接取 `spec.locator()`，无须在两套身份之间转换；
3. **转换 IO 沿用 L4 的 `LinguistConvertInput` / `LinguistConvertResult`**。

第 3 条的说明：lite 当时有三个入口（`convertG2p`、`convertS2p`、逐词预置发音），而 L4 的
`depth` 截断加上逐词锁定（A17）**已将三者合并为一个**：需要发音时停在 `Pronunciation`，需要音素
时执行到 `Onsets`，用户修改过的发音逐词预置。会话若再另立一套 IO，会使已收敛的接口重新分散。

---

### A58 — 就绪三态与双向缓存

**决策**：`Ready` / `Cold` / `Unavailable`；`probe()` 无副作用，`warm()` 主动预热；**成功与
失败同样缓存**；唯一的失效点是 `refresh()`。

**失败缓存是必需件而非优化**：lite 用 `failedS2pLanguages` 防止的情形是，对于一个含 500 个音符的
片段，一次语言级失败会变成 500 次重试。

**`probe()` 必须无副作用**：宿主的 UI 可能每帧查询一次某语言是否可用，该查询不应触发模型加载。
在此之前，宿主唯一的查询方式是尝试创建一个执行体，即查询与动作不可分。

**不设后台重试与超时**：重新检查的时机由宿主掌握（安装了新包、切换了搜索路径），会话不作推测。
因此唯一的失效点是显式的 `refresh()`——失败缓存只由它清除，`release()` 明确保留
（`LinguistSession.cpp:646` 清空，`:697-699` 保留）。**宿主侧重试一条失败路由的入口因此就是
重新扫描声库、让会话重建目录**，不存在单独的「清除失败缓存」接口；lite 的对应操作即重扫声库
（`SynthrtEngine::refreshVoicebanks()`，详见 [linguist-session.md](linguist-session.md) §4 与 §11）。

**`reason` 以失败层自己的诊断为主体，但不只是它的文本**：宿主只拿得到字符串，拿不到错误对象，
因此命中失败缓存时由 `describeFailure()` 渲染**错误码的 kind（`error.code().message()`）与整条
cause 链（`error.toString()`）**（`LinguistSession.cpp:236-252` 的辅助函数，`probe()` 在 `:744`
使用它）；message 本身就是该 code 的罐头文本时省略 kind，以免读成 "file not found: file not
found"。诊断已足够明确，再加一层人工包装只会增加排查成本。

---

### A59 — 执行体池不设默认上限

**决策**：池按 (歌手 locator, 语言句柄) 分键，`convert()` 取空闲租约，无空闲时新建，随
`refresh()` 清空；**不设默认上限**。

**理由**：并发度是宿主线程池的属性，会话随之适配即可；写死的上限在宿主并发更高时会成为隐性的
串行点。

**实际代价小于表面代价，因为开销最大的资源已被共享**：ONNX 驱动按 `path` 与 `(size, hash)` 两级
索引、以引用计数持有 `SessionImage`（内含 `Ort::Session`），同一模型文件被 N 个 `InferenceSession`
打开时**权重只加载一份**（`SessionSystem.h:20-46`、`Session.cpp:523-660`）。

**该实测同时更正了资源缓存文档的前提**：该文档 §1 写「k 路并发 = k 份词典与 **k 组模型会话**」，
后半句对 ONNX 不成立。真正随执行体重复的只有 cpp-pinyin 词表（A32）与 `lua_State`（A39），这两者
都因上游的可变状态而**无法**共享，并非设计选择。

---

### A60 — `exports` 增开放位 `openSet`

**决策**：在 `exports` 的同一 object 上增加 `openSet: bool`（缺省 `false`）。为假时清单即全集，
语义与原先完全一致；为真时清单只是已知部分，产出可能超出清单。取值由打包 lint 自动推导。

**问题**：链推理契约 §3.1 要求 `symbols` 是「原子符号**全集**」，域契约 §4 要求 `phonemes` 是
全集**且必填**。而十二条链的末步都是「查不到即输出原词」，产出无界，因此 `symbols` 都无法声明
（A48/A50），`phonemes` 只能作为建议值填写（A55）。

**必填字段实际只是建议值，这一不一致必须消除。** 三种方案比较：

| 方案 | 结果 |
| :-- | :-- |
| 改为可选（与 `symbols` 一致） | 改动最小，但宿主得不到任何信息 |
| 保持必填、只把措辞改为「对齐基准」 | 无须改代码，但必填字段仍是建议值 |
| **增加开放位** | 字段如实描述产出，且宿主**首次**能在加载期给出有用的诊断 |

采用第三种。开放位使宿主能够提示「该语言可能产出声库不支持的音素」；在此之前宿主无法给出任何
提示，因为字段要么不存在，要么与实际产出不符。

**既有声明无须改动**：缺省 `false` 即原有语义。

---

### A61 — 保留词归会话，空词与含空白词归模块

**决策**按「属于契约义务还是生态惯例」切分：

| 输入 | 处置 | 归属 |
| :-- | :-- | :-- |
| 空串或仅含空白 | `mode = skip` | **G2P 模块**（链推理契约 §3.4.2 第 1 条已规定） |
| 含空白的非空词 | 原词透传 + `error = InvalidInput` | **G2P 模块**（同上第 2 条） |
| 保留标记（缺省 `SP` / `AP`） | `mode = copy` + 单个保留音素 | **会话**（生态惯例，非契约） |

**前两条是既有的契约义务，而三个 G2P 变体原先都未实现**：空词经 `verify` 落入 `copy` 并产出空
发音，含空白的词被当作普通词送进词典，`mode = skip` 是一个已定义却无人产出的枚举值。现已实现：
判定逻辑抽为共用组件 `wolf::classifyLyric()`，只实现一次（理由同 `Verifier`：同一条规则的两份
副本终将产生分歧），三个变体在任何步骤之前调用它。实现细节见
[linguist-session.md](linguist-session.md) §8.3。

**第三条不是契约义务**，因此不能放入模块：保留标记集合随生态变化，写死在 G2P 变体中会把宿主
惯例固化为契约。该集合作为会话选项，缺省为 `{SP, AP}`，使用其他记法的宿主可以替换。

**项目专属标记仍归宿主**：连音符、`+` 后缀、切分标记属于 lite 的工程模型，不是语言域的概念。

### A62 — L6 设计的自检结论

**决策**：在实现之前按三条判据自检设计（能否稳定实现、扩展接口是否足够、有无过度冗余），并据此
修正。完整清单见 [linguist-session.md](linguist-session.md) §12。

**两处不修正即会导致崩溃**：

1. **池必须持有 `PackageHandle`。** pipeline 由 `SingerSpec&` 构建，而框架明确规定执行体必须在
   其贡献所属 Package 释放之前销毁（`ContribExecutive.h:31-33`）。原稿只声明「不代管包的打开与
   关闭」，遗漏了池**实际上**延长包生命周期这一点。**「不代管」不等于「不持有」**，因此还须向宿主
   提供显式的释放点 `release(singer)`；
2. **`refresh()` 不能清空在飞条目。** 原稿写「池随 refresh 清空」，而重扫与转换必然并发：用户安装
   包时编辑器不会停止工作。把互斥责任推给宿主，等于把一个必然发生的竞态交给宿主处理。改为按代
   管理：换代后空闲条目立即丢弃，已租出的条目在归还时丢弃。

**一处非常规所有权必须写明**：`createChild` 返回的子执行体仍归父执行体所有，`delete` 才表示
「脱离」（`ContribExecutive.h:70-76`）。池的驱逐路径因此是 `delete`；不 delete 会使子执行体在
pipeline 下无限累积，因为 `createLinguist` 不做记忆化，每次调用都新建
（`WolfPipelineExecutive.cpp:22-39`）。

**删除的三处冗余**：`LanguageStatus::binding`（目录的 `LanguageEntry` 已携带）、`refresh()` 的
`Expected<void>`（包能加载即已通过 Ready-2 校验，重扫无失败路径，而**永不失败的 `Expected` 属于
噪声**）、池的观测 / 收缩 API（`release()` 作为释放点本已必需，一个机制兼顾两种用途）。

**三处明确为决定而非遗漏**：不提供异步入口（宿主已有线程池与任务模型，再包装一层 future 只会
多出一套需要对齐的取消与线程语义）、不报告进度（lite 的两个任务都使用 indeterminate 进度条，
没有分母）、不缓存逐词结果（那属于记忆化，与资源去重不同）。

**两条经代码核实而非推演**：`createLinguist` 不做记忆化，因此池的设计成立；预置了
`pronunciation` 的词标为 `copy` 但不带 `locked`，因而仍经过 S2P 与 Onset，所以 lite 的
`convertS2p` 可映射为「一个词、预置发音、`depth = Onsets`」。

---

### A63 — L6 落地时对设计的三处修正

**决策**：实现遵循设计，但有三处必须修改。三处均予记录，因为每一处都源于设计时的判断错误，而非
实现上的简化。

**1. 歌手的键必须包含版本。** A56 的设计写「不另发明键：`ContribLocator` 是框架自己的身份（包
id + 版本 + 类别 + 贡献 id）」。**括号内的描述有误**：`ContribLocator` 的头文件明确说明其不含
Package 版本，也不做依赖解析。而同一声库的两个版本同时加载是常见情形，lite 为此专门设有
`G2pVersionAmbiguous`。改为 `SingerRef { locator, version }`：版本留空表示「唯一已加载的版本」，
已加载多个版本时**拒绝**，并分别报告「存在多个版本」与「不存在任何版本」，因为两者的修复方法
不同，合并为一条诊断会增加排查成本。目录填写的是具体版本，宿主原样传回目录条目时永不产生歧义。

> 这一条同时修正了一个实现错误：`release()` 对就绪缓存的清理最初未比较版本，因此释放一个版本会
> 连带把另一个版本报告为 Cold。两个循环现在共用同一个谓词，只实现一次。

**2. 目录获取绑定不能依靠解析 `locate()` 的结果。** `locate()` 返回的是目标贡献**自身的**
locator，该 locator 属于语言包，无法从歌手包 `resolve()`。为 `WolfPipelineExtension` 增加
`binding(language)`：绑定在读取清单时即已确定，查询绑定不创建任何对象，因此 `probe()` 仍无副作用。

**3. 创建执行体必须在会话锁外进行。** A56 只写明「会话自身线程安全」，未规定锁的粒度。第一版把
创建执行体也放在锁内；该步骤需要加载词典、打开模型，可能耗时数秒，期间宿主每帧的 `probe()` 都会
阻塞，这正是池要避免的情形。改为两段：锁内只做低开销操作，创建执行体在锁外进行，而**租约在释放
锁之前已被占用**，因此并发的 `refresh()` 不会移除正在使用的 slot。

**已用 ThreadSanitizer 验证，而非推演**：8 个线程对同一歌手同一语言各转换 60 轮、转换过程中
执行 200 次 `refresh()`、转换过程中执行 2000 次 `probe()`，TSan 下无报告。全套测试中只有
`test_MultiG2P` 报告 race，其调用栈位于 `libonnxruntime.so` 内部（主线程分配、ORT 工作线程释放），
wolf 的帧只是调用方，属于未插桩库的已知误报。

---

### A64 — C1 / C2 的落地范围

**C1（`openSet`）只加在声明为全集的三处**：域契约 `phonemes`、G2P `symbols`、S2P `phonemes`。
Onset 的 `knownPhonemes` **不加**：其契约已写明该清单是覆盖面的下界（通配段覆盖任意输入），
再增加「可能超出」的标记属于重复表述，正是复核要求避免的冗余。

**取值由打包 lint 推导，不依赖作者记忆**：`check-declarations.py` 读取语言所引用的链，若链带有
`useOriginal` 兜底步而未声明 `openSet` 即告警，反之亦然。依赖作者记忆不构成机制。

**缺省 `false`，因此既有声明的语义不变**：这是该字段能加入已发布契约的前提。拼错的值判为错误，
不按 `false` 处理，因为两种解释中 `false` 风险更高。

**C2（Onset 缺席即逐位 `false`）不能交给宿主决定**。`onsets` 与 `phonemes` 等长是既有约束，长度
已经确定，只剩取值一个自由度；若交给宿主决定，两个宿主可能对同一个语言包给出不同的切分。实现
一直如此，此前只是未写入契约；**只存在于代码中的规定不构成规范**。

---

### A65 — 最小构建须能被显式选择

**决策**：增加 `WOLF_DISABLE_DSINFER` / `WOLF_DISABLE_LUAJIT` 两个开关。

**动因是一次静默失效**。A40 记录「两种构建都经验证」，而此后最小构建长期处于损坏状态，且没有
任何报错：

1. 夹具 `lang-chain` 在实现取消时混入了一个 `lua` 变体模块。**无 LuaJIT 时整包加载失败**，
   因此 `test_PipeChain` 的所有用例都失败，而不只是运行失控脚本的那一个用例。该夹具已拆出独立的
   `lang-runaway` / `singer-runaway`，使用它们的用例按构建条件跳过；
2. 该问题未被发现，是因为**当时无法选出最小构建**：唯一的办法是把 `find_` 缓存项强制设为
   NOTFOUND，而 `find_path` 在下一次 configure 时会重新找到依赖。最小构建目录因此在不知不觉中
   变回完整构建，重新构建后测试仍全部通过。

**与 A67 同源的教训**：从外部观察，一条无人使用的路径与一条无法使用的路径并无区别。

---

### A66 — B3 排除：音节→音素词典属于歌手包

**结论**：`cmn` / `yue` / `jpn` 的音节→音素词典**不是语言包的交付物**，B3 不成立。

**证据来自 spec 2.3 本身**：其歌手清单示例把该词典写在 `configuration.languages[].dict`
（`"../assets/opencpop-extension.txt"`），路径按**歌手配置目录**解析；校验器同样按歌手侧路径
校验，并规定 `s2pMode == "dict"` 时必须有 `dict` 或 `s2pFile`。**全仓找不到该词典，正是因为它
不属于本仓。**

**因此三种语言只提供 `inference` 是最终形态**，而非待补的缺口：语言包提供 G2P，歌手包提供 S2P 与
onset，两侧在同一进程中组成一条链。该路径已实现并有测试：`test_HostFlow` 用真实迁移的
cmn / yue G2P 加一个歌手包形状的夹具走完整条链。

**与 B2 结论相同**：阻塞项源于对旧栈形状的误读。值得记录的不是又消除了一个阻塞项，而是**阻塞项
本身须定期复核**：阻塞项记录的是当时的理解，而理解会变化。

---

### A67 — 端口首次真实跑通

**发现**：`wolf-lang-packages` 端口**此前从未安装成功**。失败与 release 是否公开无关：
`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 中是硬错误，端口一经使用即失败。

**长期未被发现的原因**：测试全程经 `WOLF_LANG_PACKAGES_SOURCE` 指向本地副本，绕开了端口；而 P8
（release 未公开）又为「无法安装」提供了现成的解释。**一条应急通道在不知不觉中成了唯一通道。**

**修正方式**：直接写入 `copyright`，并把各包自带的 License.txt 收入其中。纯数据端口需要安装的
许可证随所选 feature 变化，固定的 `FILE_LIST` 无法表达这一点。

**验证无须公开 release**：把归档预置到 vcpkg 的 `downloads/` 中，`vcpkg_download_distfile`
校验 SHA512 通过后即跳过下载，portfile 的其余部分照常执行。结果：15 个包安装到
`share/wolf/packages/`，wolf 的 17 个测试指向该目录树全部通过。**W3a / W5 的退出判据至此首次
达成**，当时只有 HTTP 下载一段须等待 release 公开。

**同源的两处漂移一并修正**：

- 端口 `vcpkg.json` 的 `version-string` 原为手工维护，与生成的 `assets.cmake` 相邻，已落后一个
  版本（0.1.0.0 对 0.1.1.0）。vcpkg 会把由旧 assets 构建的树当作新版本发布。现由发布脚本随
  bundle 版本一起改写；
- 清单缺少 `lang-packages` feature，而实施计划中两处退出判据都写着
  `vcpkg install --x-feature=lang-packages`。**无法执行的验收判据等同于没有判据。**现已补充，
  为 opt-in，不影响普通构建。

### A68 — 语言包的 `compatVersion` 同取修订号下沿

**决策**：语言包的 `compatVersion` 与后端包口径相同，取**兼容区间下沿**，即最早一个仍满足下述
六项公开表面的修订，实践中即修订号 0。A54 中「语言包 `compatVersion` 等于 `version`」一条作废。

**理由依据 spec 2.4 §兼容性的原文**。该节先列出兼容区间内必须保持的六项公开表面（已有贡献的
类别与模块 ID、已有模块的 `interface` 与 `level`、已公开的 `exports` 能力语义、既有 `options`
写法、契约规定的运行时 IO 与错误语义、贡献类别规定的必选字段），随后写明：

> 当前 Package **可以增加贡献和可选能力**，也可以修改不影响上述公开表面的实现细节、私有
> `configuration`、模型与算法。……**任何破坏上述承诺的版本**都必须将 `compatVersion` 提高到
> 不再覆盖受影响的旧版本。

由此得出两点：

1. 打包修订号的变动不破坏上述六项中的任何一项，按规范**不应**提高 `compatVersion`。写
   `compatVersion == version` 等于声明该包与此前的任何版本都不兼容，这是一项不实的声明，而同节
   明确规定「承诺不实属于 Package 缺陷」。
2. 是否存在依赖方不是 `compatVersion` 的输入。`compatVersion` 是对包**自身公开表面**的声明，与
   依赖图无关。A54 以依赖关系推导兼容口径，所依据的维度有误。

**A54 的事实前提本身也不成立**：A66 把 `cmn` / `yue` / `jpn` 的 linguist 与 S2P 判归歌手包，
歌手包因此必须在 `dependencies` 中指向语言包。wolf 自己生成的夹具即写有
`dependencies: [{ "id": "wolf/lang-cmn", "version": "1.0.1.2" }]`；「语言包无人依赖」在记录
A54 的同一批工作中即已被推翻。

**实证**：把 `wolf-lang-cmn` 的 `version` 与 `compatVersion` 一起提高到 `1.0.1.3`（A53 规定的
递增第四位的常见情形），`test_HostFlow` 的六个用例全部报告
`no installed Package satisfies dependency wolf/lang-cmn`。这与 A54 为后端包记录的实证是同一种
失效，只是出现在语言包一侧。

**连带纪律**：

- `convert-g2p-packages.py` 去掉 `compat_floor(...) if is_backend else version` 分支，一律取下沿；
- 「依赖方指向目标的 `compatVersion`」由 `check-declarations.py` 升级为**错误**而非警告，发布
  脚本据此拒绝打包。A69 否决晚绑定之后，这是该问题上唯一的机制，不能只是提示；
- 回归测试必须使用「**旧夹具 + 新包**」的组合。夹具与包一起重新生成时该失效不会出现，A54 那次
  正是因此漏检。

---

### A69 — 不做能力晚绑定：语言模块在声库初始化期完全确定

**决策**：不引入按 `(language, scheme)` × `(interface, level)` 的运行期能力解析。语言模块的
绑定由声明中的 `imports[].ref` 在**加载期完全确定**；声库初始化完成时，每个语言是否可用、可用
到哪一层，必须已有结论。

**被否决的方案**：以 role 缺席表示「由环境按二元组解析」，从而使歌手包不再依赖语言包。该方案
确实能从结构上消除版本耦合，代价是把「目标不可用」从加载期推迟到 `warm()`：声库可以安装，某个
语言却可能到预热时才发现不可用。**该代价不可接受**：安装后即可用是这一层必须保证的性质。

**可选方案的范围受 spec 2.4 严格限制**，一并记录，以免重复提出：

| 方案 | 否决理由 |
| :-- | :-- |
| `dependencies[].required = false`，缺包即降级 | 2.4 变更表明确写明：2.3 的可选依赖「**不支持**，所有依赖均为强制依赖」 |
| `imports` 条目省略 `ref` | §`imports`：`ref` 必选，且「不设简写」 |
| 在 `configuration` 中增加绑定键 | §三个语法块的归属：「需要参与跨模块契约的内容应由 `exports` 公开，而不是作为 `configuration` 中的契约字段」 |
| 增加一个类别追加的根级字段 | 该层的判据是「在解释器被选出来之前就要用上」（§模块声明的三层），而 import 绑定发生在 Ready 阶段、Acquire 之后，不满足该判据 |

**因此不做包级降级，且这是规范结论而非取舍**：依赖是强制的 ⇒ 语言包缺席必然导致整个声库无法
安装。要使缺少一个语言不影响整个声库，只能**不依赖该语言包**，即语言包自带 linguist（`eng` /
`ita` / `kor` / `por` / `zxx` 的形态），而非依靠运行期解析。这也使 A55 剩余的收尾工作从可选
变为唯一途径：语言包只要自带 linguist，就不再是任何声库的必需依赖。

---

### A70 — 降级只保留链级与音素级，且都在初始化期确定

**决策**：

1. **链级**：`linguist/s2p` 由「恰好 1」放宽为 `0..1`。缺席时该语言的最大深度是
   `Depth::Pronunciation`；`linguist/onset` 缺席时为 `Depth::Phonemes`（后者已是原有行为，
   见 A64 与域契约 §5.0）。`linguist/g2p` 仍然必选，因为缺少 G2P 即不构成语言。
2. **音素级**：会话计算 linguist `exports.phonemes` 对宿主提供的声库音素表的覆盖度，给出
   `coverage` 与 `missingPhonemes`，**不设阈值**。唯一的例外是 `coverage == 0 && !openSet`，
   此时会话直接判为 `Unavailable`。
3. 两者都在初始化期确定，`probe()` 直接返回结果，不触发任何加载。

**放宽 S2P 属于变体的权限，而非 spec 改动**。spec §`imports`：

> `role` 是导入方为该条目指定的本地 slot。导入模块通过 `role` 区分各项用途，并**由自己的
> `variant` 规定哪些 role 必须存在**以及每个 role 接受哪一种目标契约。……导入方**仍可**严格
> 要求自己契约规定的 role 存在且只指向规定的目标契约。

原文为「仍可」而非「必须」。`wolf` 变体收紧或放宽自身的下界都在授权范围内。

**这是增量变化而非破坏性变化**：spec §兼容性把「增加贡献和可选能力」列为兼容区间内允许的变化。
放宽一个必需 role 只使原先失败的声明变为可加载，既有声明的行为完全不变，因此不需要提高
`compatVersion`。

**阈值不归 wolf 决定的理由**：Status 中的逐包清点（`fil` 100%、`ita` 75%、`eng` 44%、其余六种
0%）表明任何固定阈值都会误判：44% 对以英文为主的工程可能已经足够。而
`coverage == 0 && !openSet` 属于另一种情形：清单声明为全集，声库却一个音素都不支持，这不是降级，
而是连接错误。这是 wolf 在此问题上唯一自行规定的判断。

**A50 的归属不变**：音素比对仍属运行期而非加载期。变化仅在于算法移入会话，宿主获得结论而非
原始数据。理由与 A56 设立 L6 相同：每个宿主都需要计算，而计算只有一个正确结果。

---

### A71 — 「初始化即确定」指绑定与形状，不包括预热

**决策**：保留 `Cold`，但语义收紧为「**绑定已确定、`maxDepth` 与覆盖度已计算，仅未预热**」。
A58 所写的「`Cold` 只说目录里有这条路由，且尚未试过」在 A69 之后不再准确，相应限定为「不表示
**已加载**」。

**可以收紧的理由**：不做晚绑定后，绑定在加载期已由 `ref` 完全确定；`maxDepth` 由 `imports`
集合直接读出；覆盖度只是一次集合运算。三者在初始化期都有结果，且都无须打开任何模型。`Ready` 与
`Cold` 的差别因此只在于资源是否已加载到内存。

**不把预热也移到初始化期的理由**：`Cold → Ready` 这一步的开销不在词典。词典类资源全部在 Acquire
期由各变体的 `createConfiguration` 解析完毕（chain 的 `dict` / `verify`、s2p 的表、onset 的规则、
multig2p 的 bundle 与 vocabulary、lua 的脚本源），安装包时即已在内存中，且经 `ResourceCache`
跨执行体共享。预热的实际开销是**每执行体一份、按 A32 / A39 / A59 无法共享的三项资源**：cpp-pinyin
引擎（`createEngine`）、ONNX session（`Decoder::open`）、`lua_State`（`Sandbox::create`）。一个
声明 12 种语言的声库若在初始化期全部预热，就须一次性承担这 12 份开销。A63 第 3 条正是为这项开销
把创建执行体移出会话锁；提前到初始化期只是把开销移到更早且无法回避的位置。初始化期要求的是
**确定性**，而非**已加载**：前者由清单即可提供，后者只能通过实际创建获得。

**只能在 `warm()` 中暴露的问题**：驱动未安装、模型无法打开、词典无法读取。这些是安装环境与包内容
的属性（A36 已有同样的区分），无法从清单中得知，任何设计都不能在初始化期给出结论。因此保留
`Cold → Unavailable` 这条状态转移。

---

### A72 — 进程级词典根的保留随配置对象存活

**决策**：`PinyinEngineRegistry::reserveRoot()` 返回一个 RAII 的 `RootReservation`，由该模块的
`Configuration` 对象持有；保留计数归零时注销该根。

**所修正的问题**：保留原先是一次不可撤销的全局写入，发生在 `createConfiguration`（Acquire）中。
后果有两项，均已实测复现：

- 一次**失败**的加载会永久保留该根。此后进程中任何指向其他根的 pinyin 包都无法安装，即一个未能
  安装的包导致了一个可用的包无法使用；
- **卸载**也从不释放根。加载过一次 cmn 的进程此后无法更换根。

**该形状的依据**：spec 2.4 §加载事务要求「Acquire 或 Ready 创建的每项资源和状态变化都
必须保持事务私有且立即写入完成日志，以便 rollback 完整撤销」，并要求修改共享目标的 provider
提供等价于 prepare / commit / abort 的机制。配置对象**本身**即为该事务私有对象：加载失败时它随事务
销毁，加载成功时它随模块实例存活，卸载时它随包销毁。把保留关联到配置对象后，abort 与 release
两条路径都自动完成，无须另写三段式机制，也不会遗漏其中一段。

**保留可重入**：同一个根的第二个模块获得自己的一份保留，计数归零时才注销。异根仍然立即失败，
并在诊断中列出两个根，A31 的裁决不变。

**测试**：`test_PinyinBackend` 增加两个用例：失败的加载之后另一个根可用、卸载之后另一个根可用。
两个用例都在同一个 `SynthUnit` 内加载，因为销毁 unit 会卸载解释器插件并连带重置该单例，使用两个
unit 恰好会掩盖待测的泄漏。

---

### A73 — `stop()` 取消当前或下一次转换

**决策**：`LinguistExecutiveImpl::stop()` 置位，`start()` 用 `exchange(false)` **消费**该位：
入口处读到即立即以 `Canceled` 返回，运行中读到则按既有的分段检查返回已完成部分；两条路径都清除
该位，因此放回池中的执行体不会继承上一次的取消。会话侧 `CancelToken::enrol()` 相应改为返回布尔值，
已取消的令牌使 `convert()` 不启动这一批。

**所修正的问题**：`start()` 原先的第一行是 `m_stopRequested = false`，而会话恰在此前调用过
`stop()`。因此**任何在 `convert()` 之前发出的取消都被静默丢弃**，而这正是最常见的次序：用户
点击停止，排队中的一批才开始运行。实测：预置取消后整条链仍运行到 onsets。原有用例的注释声称测试
的就是这一行为，但断言中没有任何一条检查它。

**不采用「`stop()` 只在运行时生效」的理由**：这是 `srt::ITask` 的做法（`requestAsyncCancellation`
只在 `running` 时置位），对自行管理排队的调用方已经足够。会话不属于这类调用方：会话先租用执行体、
再启动，两步之间必然存在时间窗口，窗口内发出的取消必须生效。

**补记（子执行体采用同一口径）**：推理解释器的执行体原先在批次入口把停止位清零，理由是父级广播的
停止可能在子执行体空闲时到达，若保留会影响下一批。代价是一个真实存在的窗口：父级检查过自身的停止
位、发布子执行体、子执行体在入口清零，落在其间的停止连同脚本沙箱的中断一起被清除，整批不可中断地
运行完毕。现在子执行体与父级采用同一口径：入口处读到未消费的停止即以 `Canceled` 返回，停止位由
任务接口在**返回后**消费。「陈旧停止影响下一批」改由父级处理：linguist 执行体发现某阶段报告
`Canceled` 而本次转换并未请求停止时，即判定该停止为上一次遗留、且已被该阶段的本次启动消费，于是
重试一次该阶段。两个窗口都已关闭，且无须新增跨插件接口。

---

### A74 — 导出的链接接口只允许出现 config 会引入的目标

**决策**：`blake3` 与 `re2` 同归 `LINKS_PRIVATE`（两者都只出现在 `.cpp` 中），并在
`src/lib/CMakeLists.txt` 增加一条配置期断言：`INTERFACE_LINK_LIBRARIES` 中每个带 `::` 的目标都
必须在 `wolfConfig.cmake.in` 会引入的名单中，否则配置失败，并在诊断中给出修正方法。

> **现状**：`re2` 现由静态库 `wolfsupport` 私有链接，`blake3` 仍在 libwolf 的 `LINKS_PRIVATE` 中；
> 该断言保持不变。

**所修正的问题**：本轮之前 `blake3` 在公开的 `LINKS` 中，而 `wolfConfig.cmake.in` 只执行
`find_dependency(synthrt)`。因此照 README「How to Use」一节操作会在 generate 阶段失败：
*the link interface of target "wolf::wolf" contains BLAKE3::blake3 but the target was not found*。
**仓内测试无法发现该问题**，因为测试直接链接构建树中的目标，从不经过安装后的 config。

**采用断言而非文档**：这类漂移的特征是修改发生在一处，失效出现在另一处，而后者无人执行。把
不变式写成配置期的硬失败，是唯一能使修改者立即发现问题的位置。

---

### A75 — 执行体一律持有 `srt::ITask`，不再手写任务接口

**决策**：抽出 `wolf::ExecutiveTask<Input, Result>`（`include/wolf/Support/ExecutiveTask.h`，
仓内头文件，不安装），十一个执行体各持有一个并转发调用；`quit()` / `wait()` 的空覆盖一律删除，
使 `srt::InferenceExecutive` 自身的转发生效。

**所修正的问题**：手写的任务接口有三处相同的错误，十一份副本以完全相同的方式出错：

1. `waitForFinished()` 直接返回 `{}`，因此框架卸载包时的等待立即被满足，而转换仍在运行；
2. `startAsync` 在八处是**同步就地回调**（与 API 头注释「回调在工作线程上执行」矛盾），第九处
   `LinguistExecutiveImpl` 直接 `detach` 一个线程，没有任何一方能等待该线程；
3. 没有一处拒绝第二次并发执行，而契约规定一个执行体同一时刻只承载一轮执行。

第四处问题更为严重，且是本轮才查出的：`chain` / `pinyin` / `multig2p` / `lua`×2 把
`quit()` / `wait()` 覆盖为 `return {}`，**使 `srt::InferenceExecutive` 已实现的
`quit()→stop()`、`wait()→waitForFinished()` 转发失效**。卸载包时这些执行体既不被停止也不被
等待。上游的实现正确，wolf 的覆盖使其失效。

**抽出一次而非复制十一份的理由**：与 A61（`classifyLyric`）、A43（`Verifier`）理由相同。十一份
副本已经证明会以完全相同的方式同时出错。

**桥接的形状**：body 与「是否被取消」两个可调用对象，而非每个变体配一个伴生类（dsinfer 的
`DurationTask` 采用伴生类形状，但它只有五个变体，且各自须持有驱动会话）。任务成员**声明在最后**，
并在执行体析构函数中显式等待，因为成员按声明逆序销毁，而基类 `~ITask` 执行等待时派生部分已被
销毁。

---

### A76 — 取消语义分两级：父级消费，子级在入口清除

**决策**：`LinguistExecutiveImpl`（父级）用 `exchange` **消费**取消位，因此开始前发出的取消生效
（A73）；六个推理执行体（子级）在每轮入口**清除**自身的取消位。

> **后续（A73 补记）**：子级在入口清除取消位的做法已被取代。子执行体现与父级采用同一口径，陈旧
> 停止由父级识别并重试该阶段；本条仅作为历史记录保留。

**不统一口径的理由**：差别在于由哪一方控制。会话租用父执行体，只在租约期内对其调用 `stop()`，
因此消费语义正确。而子执行体的 `stop()` 由父级**广播**，可能比父级本轮执行存续更久：父级因预置
取消而提前返回时，子执行体根本未运行，却保留了一个陈旧的取消位。若子级也采用消费语义，该位会
取消下一轮。

**本条来自测试结果，而非推演**：把消费语义同时应用于子执行体后，`test_LuaVariants` 中一条早已
存在的用例立即失败，该用例固定的正是「停止一个未运行的执行体不应取消任何执行」。该用例是正确的。

---

### A77 — 声明根的未知键改为警告，并为 wolf 设立日志类别

**决策**：`LinguistCategory` 不再因声明根中的未知键拒绝加载，改为经 `wolf::logCategory()`
（新增，`include/wolf/Support/Logging.h`）记录一条警告。`exports`、`configuration` 与
`imports[].options` 仍然严格校验，因为这三处的 schema 确实归 wolf 规定。

> **现状**：`wolf::logCategory()` 位于 libwolf，以 `WOLF_INTERNAL_EXPORT` 导出，仅供本仓插件
> 使用，不属于公共 API；`wolf/Support` 头文件不安装。

**理由**：模块声明的根是**上位规范定义的对象**，而规范对这类对象写明「框架只验证其认识的字段；
未知字段不得导致拒绝」。类别在此处拒绝未知键，会使框架**日后新增的任何公共字段**在较早的 wolf
构建上加载失败，这是 wolf 无权对上游施加的前向兼容破坏。

**无损失**：拼错一个**必填**字段仍然失败，因为正确的字段随之缺失；只有拼错可选字段从错误降为
警告，而这正是规范所要求的权衡。

---

### A78 — 一个 unit 可承载多个会话，取消原有限制

**决策**：删除 A62 记录的「一个 unit 一个会话」，代之以一条用例。

**理由**：该限制既无强制也无检查，宿主若创建第二个会话，只会在运行时才发现问题。而该限制本无
必要：会话之间除框架加锁的 `SynthUnit` 以及两个自行加锁的进程级单例（`ResourceCache`、
`PinyinEngineRegistry`）之外没有共享的可变状态，各自创建自己的 pipeline、池与缓存。
`test_LinguistSession_TwoSessionsShareOneUnit` 固定了这一点：预热一个会话不会使另一个会话报告
已预热，释放一个会话不影响另一个会话，两个会话并发转换的结果互不干扰。

---

### A79 — 桩提供一个不与真实变体竞争的三元组

**决策**：测试桩无条件声明 `org.openvpi.wolf.inference.G2P` / 1 / **`stub-miscount`**，此外再按
构建条件补充 `multig2p-onnx`（A40 的规则不变：一个三元组始终只有一个 provider）。

**必要性**：短批次拒绝现在实现于三层：链变体对其后端、linguist 执行体对其各阶段、会话对执行体。
**而四个仓库中没有任何模块能产出短批次**，因此这三道检查都未经验证。一道检查只有在被证明能够
触发之后才有效，因此由桩按声明请求（`configuration.dropWords`）有意违反 provider ABI。

**采用新变体名而不复用现有变体名的理由**：桩在完整构建下不声明任何 G2P 三元组（`multig2p-onnx`
归真实解释器），因此复用会使该用例只在最小构建中运行。`stub-miscount` 没有任何真实实现认领，
在两种构建下都可用，也永远不会与其他实现竞争同一个三元组。

**同批整理的测试面**：删除 `test_LinguistContrib_Reference`，该用例断言的是
`srt::ContribLocator::fromString` 对任意字符串的行为，属于 synthrt 而非 wolf；把两条看似重复的
`pipe-chain` 用例重命名以明确分工，二者实际对应 A44 的两条不同路径（一条链的兜底步不产出结果，
另一条链没有兜底步）；并修正 `test_LinguistSession_RefreshesUnderAConversion` 的时序不稳定：该
用例原本可能在工作线程被调度之前就执行完全部 200 次 `refresh()`，既可能误报失败，也可能未测到
目标行为却报告通过。现在该用例先等待第一次转换完成再开始 refresh，并要求转换次数**大于一**。

**该问题由逐提交回放发现**：把本轮每个提交依次检出并在 `-j8` 下运行完整套件，该用例失败了一次。
逐提交验证的价值不限于确认每个提交都能编译。

---

### A80 — ORT 由 `onnxruntime-builds` 端口供给，synthrt 不再部署运行库

**决策**：synthrt 只消费 ORT 的**头文件**（经该包的 imported target），不部署 ORT 的**运行库**；
wolf 端口中手工铺设头文件的一段随之删除。改动位于上游分支 `onnxruntime-builds-uptake`
（从 synthrt main 分出），wolf 的 `synthrt-main` 端口固定在该分支的提交上。

**须解除的耦合**：`scripts/setup-onnxruntime.cmake` 中写有 `_version_ort "1.17.3"` 与整套
SHA512，而生态中实际安装的端口是 1.24.4，即**更换 ORT 版本须修改 synthrt 仓库**。删除该文件、
删除 `third-party/`、把两处 CMake 改接 `find_package(onnxruntime-builds)` 之后，**两个仓库中不再
出现 ORT 版本号**，唯一来源是端口的 `versions.cmake`。

**（已执行）**：synthrt `e94e022`（`onnxruntime-builds-uptake`）已删除该文件与 `third-party/`，两处 CMake 改接
`find_package(onnxruntime-builds)`；该文件在 synthrt 当前树中已不存在，wolf 的 `synthrt-main` 端口固定在这个
分支上，本节其余文字保留为当时的问题陈述。

**可以不部署的理由**：`DriverInitArgs::runtimePath` 是**宿主传入的目录**，驱动自身从不在
`runtimes/onnx/` 中查找（`OnnxDriver.cpp:68`，空路径时退回系统加载器）。构建期的那份拷贝**唯一
的实际消费者是 synthrt 自身的驱动测试**，该测试把已传入的路径又重新拼接了一遍
（`test_InferenceDriverFactory.cpp:224`）。改为由测试向端口查询目录后，该拷贝不再有任何消费者，
`runtimes/onnx/default` 这一字符串同时从插件 CMake 与测试 C++ 中消失。

**归属由此明确**：部署归拥有应用的一方，嵌入时是编辑器（编辑器中 ORT 与 synthrt 是两个独立
port），独立构建时是测试自身。库与插件都不负责部署。

**同批修正两处既有缺陷**，因为本改动引入了现有构建树无法满足的保证：`SYNTHRT_BUILD_DSINFER`
默认为 ON，而 CLI 无条件执行 `add_dependencies(... onnxdriver)`，驱动测试无条件声明并 include
`OnnxTensor.h`；因此「插件不会被构建」的警告之后紧接着出现 generate 错误，启用测试则编译失败。
两处均改为以插件目标存在为条件。**这不是范围扩大**：若不修正，新写的降级路径即无法工作。

**不做的事项**：不修改共享 overlay 的端口（归 lite 管理）；不补充 refactor 的
`onnxdriver-payload.cmake`（它声明的是 synthrt 部署的载荷，载荷已不存在，无对象可声明）；不为
`find_package` 增加版本下限（那等于把版本号写回 synthrt）；不暴露 CUDA flavor。

**验证**：synthrt 三种配置（缺端口 + 默认、缺端口 + dsinfer + tests、有端口 + dsinfer + tests）
分别为配置构建通过 / 15 例通过 / 16 例通过，且驱动测试打印 `Initialized ONNX Runtime 1.24.4`；wolf
两种构建 17 / 15 全部通过。新增 `test_MultiG2P_RunsOnARuntimeTheHostDeployed`：把 ORT 部署到宿主
自选目录（`/tmp` 下，既非端口目录也非插件所在目录）后运行全链，并断言驱动**确实**从该目录加载；
把期望值换成端口目录时该用例立即失败，因此该断言有效。

---

---

## 现行未决项

| # | 项 | 状态 |
| :-- | :-- | :-- |
| Q1 | 未被 `languages` 引用的 linguist import：现定为编辑期 lint 警告，不判加载失败 | 已定，可复议 |
| Q2 | `stop()` 在词边界响应的时延上界 | **已回写**：实测约 50 µs（最坏情形，运行时文档 §4.2）。仍不写入规范，因为上界由钩子的指令预算决定，而非计时器 |
| Q3 | `exports` 与 `imports[].options` 的 JSON Schema 发布物（spec 2.4:596-602 要求） | **已产出**：`docs/schemas/`，四个契约各两份，另有两份共享形状。由 `scripts/check-declarations.py` 对真实包执行 |
| Q4 | wolf 打包期 lint 工具是否单独立项 | **已定**：不单独立项，由 `scripts/check-declarations.py` 承担，并由 `make-lang-release.py` 在打包前调用。schema 一致性与 §12 中只依赖声明的两项（未引用的 import、贡献 ID 惯例）已接入；另两项需要外部音素表，归编辑器运行期 |
| Q5 | DiffSinger 歌手 `configuration` 仍强制要求 `dict` 路径（`dsinfer/plugins/singerproviders/diffsinger/DiffSingerProvider.cpp:171-185`），属旧 G2P 栈遗留 | 语言域迁移完成后应退役，不在本轮范围内 |
| Q6 | A11 的上游合并时点 | **已落地**于 synthrt 分支 `onnxruntime-builds-uptake`（A26 补记），端口固定在该分支上；并入 synthrt main 有待上游 |
| Q7 | `srt::ITask::startAsync` 的 `finish()` 在**释放互斥量之后**才调用 `notify_all()`，等待方因此可能在通知进行中析构条件变量。TSan 在 `test_LinguistRuntime` 中报告一处，但整条调用栈都位于未插桩的 `libsynthrt.so` 内，与既有的 `libonnxruntime.so` 报告同类；按 `shared_ptr` 引用计数推演存在合法的 happens-before 关系，因此**倾向于判定为误报**。定论需要一个插桩的 synthrt 构建 | 上游，待定论 |

---

## D 系列：历史台账处置

原 D1-D30 出自变体草案的台账节。**其实质内容已全部并入新文档**（下表末列给出位置），五份
草案已删除，论证过程可经 git 历史回溯。当前判定：

| 原编号 | 处置 | 位置 |
| :-- | :-- | :-- |
| D1、D2、D15-D18 | 被 A2 取代（三契约拓扑作废，回到 3 份） | — |
| D3 | 已并入 | 变体 §4.4「`model` 绑定」行、§5.6「语言引用词法」行 |
| D4 | 已并入 | 变体 §4.4「配置入口」行 |
| D5 | 已并入 | 变体 §4.4「`model` 绑定」行 |
| D6 | 已并入 | 变体 §5.6 全表 |
| D7 | 已并入 | 变体 §6.7 全表 |
| D8 | 已并入 | 链推理契约 §3.4.5 共现约束（`mode=skip` 契约优先） |
| D9、D29 | 被 A11 取代（role 后缀方案 → `languages` 映射） | — |
| D10 | 已并入 | 变体 §7 移植来源表 |
| D11 | **推翻**。原判定「基数上界未执行」不成立：role 在模块内的唯一性由框架强制（spec 2.4:634；`PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport`），三个固定 role 的上界在结构上成立，不是缺口，无须收敛，也不产生「就地加严须在发布注记列明」的义务 | — |
| D12 | 作废：`ds-dict` G2P 化路线随 A2 取消 | — |
| D13 | 已并入 | 变体 §3.3 的「已知实现缺陷」注、Status B2 |
| D14 | 被 L4 诊断通道取代 | `linguist-runtime.md` §6 |
| D19-D23 | 已并入 | 链推理契约 §2.1 / §4.1 / §5.1 的可省略条款 |
| D20 | 作废：`lstm-onnx` 备案，A2 之后归 `pipe-chain` 变体内部事务，不再是待收录契约 | — |
| D26-D28、D-P1~P8 | D-P1 被 A19 取代（独立资源仓 → wolf 仓 Release）；其余已并入 | `linguist-distribution.md` |
| D-R1~D-R6 | D-R2 被 A14 部分取代（单一任务门保留，多任务形状作废）；其余已并入 | `linguist-runtime.md` §8 |

## 复核轮次记录处置

旧变体草案的九轮攻防复核记录（对照面快照、防线记录、下轮攻击者须知）**不迁移**，可经 git
历史回溯。其中确认有效的事实断言已并入 A 系列的依据栏。

一处需要更正的复核结论：多轮复核声明的「synthrt origin/main 观察点即 HEAD、未漂移」已失效。
其后 main 前进了一个提交（*Update registry usage*，2026-09-04），该提交改动了 `ContribCategory.h`
第 158 行下方的注册表导出宏；已复核全部被引锚点在该提交上仍然命中。
