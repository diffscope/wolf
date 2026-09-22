# wolf 语言域决策台账

本文承载语言域设计的**全部决策与论证**，规范文本不得复述。分层文档只写结论，追问理由来这里。

编号规则：**A 系列**为现行架构决策（2026-09-08 重构轮），**D 系列**为历史草案轮次的决策
（保留谱系，多数已被 A 系列取代）。**编号永久不复用、不因清理重排。**

锚点口径见 [linguist-architecture.md](linguist-architecture.md)。

---

## A 系列：现行架构决策

### A1 — 推理三契约沿用 synthrt 内置 `inference` 类别

**决策**：G2P / S2P / Onset 的模块是 `inference` 贡献；解释器派生 `srt::InferenceInterpreter`，
插件嵌 `org.openvpi.synthrt.plugin.InferenceInterpreter`，落宿主既有 `inference` 搜索路径。
wolf 只发布契约头，不注册新模块类别。

**依据**：`synthrt/lib/SVS/InferenceContrib.cpp:111,147`、`InferenceInterpreterPlugin.h:11`。

**备选与否决理由**：
- *wolf 自注册 `linguist.g2p` 等三个类别*——可自定执行体面（含多任务），但每类别要求宿主多配
  一条 `setPluginPaths`（`ContribCategory` 构造签名要求模块类别自带 IID），且要自建
  executive / task 体系，与 dsinfer 生态不同构，并失去 `validateCompatibilityWith` 钩子。否决。
- *混合（G2P 自注册、S2P/Onset 留 inference）*——同一条链跨两种类别，部署面与心智模型都变
  复杂。否决。

**代价（已接受）**：`srt::InferenceExecutive` 是单任务面，见 A14。

---

### A2 — Level 1 契约面收敛为 3 份

**决策**：Level 1 只钉 G2P / S2P / Onset。`G2PModel`（模型后端）与 `DictQuery`（词典查询）
不立契约，降为 `pipe-chain` 变体的内部事务，经该变体自己的 `imports` 与 `configuration` 表达。

**依据**：两者在 wolf 与 synthrt 两侧均为**零代码、零消费方**，属纯推演产物；把无实现无消费方
的契约放进发布承诺面，收益为零而演进成本为正。

**重启判据**：出现「需要跨实现互操作的模型后端或词典服务」的真实案例时，按 spec 2.4:750-753
判据另立 interface。原 D15-D18 的拓扑论证过程经 git 历史回溯。

**取代**：D15、D16、D17、D18。

---

### A3 — 语言身份 = 声明的 `(language, scheme)` 二元组

**决策**：语言身份由 linguist 声明的两个字段承载；**贡献 ID 从不被解析**。

**动因**：原设计把「贡献 ID / 语言句柄 / 注音体系」三件事压进一个字符串 `cmn-pinyin`，再靠
字符串解析取回，因而被迫规定「各字段内不得含连字符」「自定义字段不被剥除」「匹配一律全 ID
恒等」。多语种多注音体系恰恰是被这套规则**卡住**的：G2P 必须逐一枚举所有 linguist 贡献 ID
（含第三方 qualifier），出现 `eng-arpabet-plus` 这类第三方 ID 无法对接官方 G2P 的死角。

**收益**：
1. G2P / S2P 作者不必知道任何 linguist 贡献的 ID；
2. 第三方语言贡献只要 `scheme` 相同即自动对接官方模块；
3. 「拼音 G2P 接粤拼 S2P」在加载期硬失败（原设计完全不拦）。

**取代**：原《语言贡献 ID 形态约定》全部字符串规则。

---

### A4 — ID 语法 `<language>-<scheme>[-<qualifier>]` 为书写惯例，只做 lint

**决策**：贡献 ID 惯例为 `language + "-" + scheme`，或以 `language + "-" + scheme + "-"`
开头且余部非空。`qualifier` 无语义、不参与任何匹配。**该惯例只在打包期 lint 比对，不作加载期
校验。**

**动因**：同一二元组可能出现变种、不同作者、精简版等多张贡献，需要第三段区分。

**关键性质**：该规则是**前缀比对**而非解析，因此 `scheme` 内部允许连字符不产生歧义——
`cmn-pinyin-lite` 在 (`scheme=pinyin`, `qualifier=lite`) 与 (`scheme=pinyin-lite`, 无 qualifier)
两种声明下都合法且各自自洽，因为没有任何一方从 ID 反推语义。

**为何不硬校验**（2026-09-08 按 A17 的判据回审后降级）：ID 的前缀**不携带任何信息量**——
真值在 `language` / `scheme` 两字段里，本契约族的任何匹配、绑定、路由都不读 ID。硬校验买到的
只是「ID 可预测」，付出的却是 spec 2.4:506-508 明确给 Package 的命名自由（「同一份模块目录被
两个 Package 收录时两边可以各自命名」）。信息量为零的副本不值得一条加载期规则。

---

### A5 — G2P 与 S2P 同形声明 `exports.languages`，可省略

**决策**：两者用同一键、同一形状 `array<{language, scheme}>`，语义互为对偶（能产出 / 能消费）。
可省略；省略即放弃该侧静态保障，模块可加载但宿主应告警。

**收益**：补上了原设计**根本不存在**的一个检查——把拼音 G2P 接到粤拼 S2P 词典上，原设计只有
警告级的 `phonemes` 比对，运行时才会大面积未命中。

**可省略的理由**：`direct`（按空格拆分）与 `lua` 变体本质上对任何体系都成立，强制声明会逼出
假声明。

---

### A6 — 文档按层一份，台账外置

**决策**：规范文本内不留 D 编号、攻防记录与轮次叙事。

**动因**：旧五份草案含约 30 条决策台账，其中变体草案的台账节占该文档近一半篇幅，且是跨文档
漂移的主要来源（复核发现的多处行号与事实漂移集中在台账区）。

---

### A7 — 二元组作**类别追加字段**写在 linguist 声明根

**决策**：`language` / `scheme` 写在声明根，由 `LinguistCategory` 在 Probe 解析；
`linguist` 声明白名单 +2。

**依据**：spec 2.4:529-535 的第二层（由贡献类别规定、对该类别下所有模块生效、由类别自己解析），
其立论正是「在解释器被选出来之前就要用上」。同层先例为 `singer` 的
`avatar` / `background` / `demoAudio`（`synthrt/lib/SVS/SingerContrib.cpp:33-36`）。

**备选与否决理由**：*写进 `exports`*——白名单零改动（`createExports` 不拒未知键），且与
G2P / S2P 的 `exports.languages` 形状呼应。但身份将绑定到 `WolfLinguist` 这一个 interface：
若今后 `linguist` 类别下出现第三方 interface，wolf 的歌手侧校验对它失效；且校验时机从 Probe
推迟到 Acquire。**权衡后取类别层**——代价只是白名单里两个字符串。

---

### A8 — ~~缺省语言 = imports 声明序第一条 `linguist/*`~~（作废）

原决策试图在不改 synthrt 的前提下表达缺省语言。被 A11 取代：用户要求编辑器可用的显式字段。

保留一条结论供参考：`imports` 是保序的（spec 2.4:634；`stdc::vlarray<ContribImport>`），
所以「声明序第一条」在技术上可行；作废原因是表达力而非可行性。

---

### A9 — `configuration` 必须显式写 `{}`

**决策**：保留 wolf 现行行为（省略时框架交付 Null，provider 判 `InvalidFormat`），改**规范
文本**为「必须显式写为空对象」。

**依据**：省略时 `context.manifestConfiguration` 保持缺省 Null（`PackageLoader.cpp:1316-1319`）。
契约有权要求其所辖模块显式提供某个公共可选字段，这是契约层加严，不是对 spec 2.4:522 的违反。

**为何不改代码**：这是「文本让步」比「代码改动」更省的少数情形之一，且现行行为已在测试中固化。

---

### A10 — `vars` 无需任何处理

**决策**：`linguist` 白名单不必增补 `vars`；旧文档中「白名单未含 `vars`，故模块级字符串变量
暂请写在 `desc.json`，该行为不符上位规范」的注记是**事实错误**，删除。

**依据**：框架在把声明交给类别与解释器之前已 `object.erase("vars")`
（`synthrt/lib/Core/PackageLoader.cpp:406`，调用点 `:1274`，早于 `createSpec` 的 `:1377`）。
任何类别的字段白名单都不会遇到 `vars`；模块级 `vars` 今天就能正常工作。

**连带**：synthrt 的 `inference` / `singer` 白名单同样不含 `vars`，同样不构成缺陷。原判定
「三处同样违规」撤回。

---

### A11 — 歌手侧新增 `languages` 与 `defaultLanguage`

**决策**：歌手声明根增两个类别追加字段，由 `SingerCategory`（synthrt 侧）解析：

- `languages`: `map<语言句柄, 本声明内的 import role>`；
- `defaultLanguage`: 句柄，必须是 `languages` 的键；`languages` 非空时必填。

歌手 `role` 恢复自由命名，`linguist/` 前缀降为书写建议。

**动因**：
1. 编辑器需要一个可选用的缺省语种字段；
2. 需要显式表达「标准 iso 代码 ↔ 实际 linguist 贡献 ID」的对应。

**结构性收益**：`role` 回归 spec 2.4:648 定义的「导入方为该条目指定的本地 slot」。原设计把
语言句柄编码进 role 后缀，是因为歌手声明白名单封死了新字段——那是「没得选」，不是「更好」。

**为何值是 role 而不是 ModuleReference**：ImportBinding 只从 `imports` 数组产生
（`PackageLoader.cpp:894-917`），数组之外的 ref 不产生绑定，运行时拿不到执行工厂。这是框架
事实，不是风格选择。

**为何 `defaultLanguage` 必须显式**：`JsonObject = std::map<std::string, Value, std::less<>>`
（stdcorelib `support/json.h:68`），成员按键排序、声明顺序丢失，object 里不存在「第一个」。

**职责切分**：synthrt 只验形状与「role 在本声明的 imports 中存在」
（`ContribCreateContext::imports()`，`ContribCategory.h:60`），**完全不知道 linguist 是什么**；
「键形如 `[a-z]{3}`」「目标类别为 `linguist`」「目标 `language` == 键」三条归 wolf 的 Ready-2
validator。这使两个字段对任何语言体系通用，而非把 wolf 领域知识塞进框架。

**成本**：synthrt `SingerContrib.{h,cpp}` 白名单 +2、解析 +2、访问器 +2。这是本轮唯一动用
「synthrt 个别刚需字段」额度的地方。

**取代**：D9、D29 的 role 后缀方案；A8。

---

### A12 — 唯一映射约束由映射键唯一性承载

**决策**：「同一声库中每个语言只支持一种注音体系」由 `languages` 的 JSON object 键唯一性
结构性成立，wolf **不写**该校验。

**依据**：`JsonObject` 是 `std::map`，每个语言句柄至多一条映射 ⇒ 至多一个语言导入 ⇒ 至多一种
`scheme`。

**已知缺口（不影响本约束）**：源 JSON 中的重复键被 map 静默折叠，而非按 spec 2.4:72
「同一层不得出现重复 key，违反时整份声明无效」报错。这是 stdcorelib 的符合性缺口；本约束
依赖的不变量（每键至多一条）不受影响。

**演进口**：若将来需要「同声库同语言双体系」，spec 2.4:644 的多段 role 文法支持
`linguist/cmn/bopomofo` 形态，但那会让本约束失效，属**显式的 Level 递增**，不是预留后门。

---

### A13 — `defaultLanguage` 无加载期与运行时语义

**决策**：除「必须是 `languages` 的键」这条结构约束外，该字段不参与任何加载期裁决与运行时
行为。语言域自身从不读它，它只是作者写给宿主的意图声明。

---

### A14 — 并发靠多开执行体，不靠多开任务

**决策**：一条链 = 一棵执行体子树 = 一个在飞任务。`LinguistExecutive` 与
`srt::InferenceExecutive` 保持同构的单任务面。

**依据**：`srt::InferenceExecutive` 只提供 `state()` / `stop()` / `waitForFinished()`，
`quit()` / `wait()` private-final（`synthrt/include/synthrt/SVS/InferenceExecutive.h:39-50`）。
G2P / S2P / Onset 执行体各自只能承载一个在飞任务，给 `LinguistExecutive` 一个多任务面只会是
骗人的接口。`adoptChild` 不限制同 role 多子（`ContribExecutive.cpp:71-109`），故多开执行体
是框架支持的并发路径。

**取代**：原运行时草案中「executive 量产多个相互独立、可并发入队的 task」的形状。

**连带（重要）**：资源缓存从「优化」升为**架构必需件**——k 路并发即 k 组 G2P/S2P/Onset 执行体，
无跨执行体共享就是 k 份词典与 k 组 ONNX session。

---

### A15 — `RuntimeOptions` 必须携带目标变体

**决策**：G2P / S2P / Onset 的 `RuntimeOptions` 类由构造参数接收目标变体，不得硬编码。

**依据**：`synthrt/lib/SVS/InferenceContrib.cpp:25-30` 逐格比对
`runtimeOptions.variant() != target.variant()`。dsinfer 的 `DurationRuntimeOptions` 硬编码
`"onnx"` 是因为该契约单变体，**不可照抄**——本契约族三份契约都是多变体。

---

### A16 — 运行时 IO 不携带语言参数

**决策**：G2P 的 Level 1 运行时词汇表**不含** `languageId` 输入；执行体在创建时经
`RuntimeOptions` 绑定单一 `(language, scheme)`，生命周期内不变。

**动因**：A14 之后，每条链一棵子树，子树天然对应一个语言，运行时再传语言是冗余的。原设计的
《运行时路径》整节（未传入回落绑定值 / 传入且恒等 / 传入但不一致判 `InvalidInput` 的依序
判定）随之消失。

**代价**：多语言 G2P 需为每个语言各开一个执行体，资源共享交给缓存（A14 连带）。

---

### A17 — 运行时载荷的精简尺子

**决策**：运行时载荷按两条尺子裁剪，凡不过关者删除或改形。

**尺子一：非法状态不可表示。**

| 原形 | 问题 | 改后 |
| :-- | :-- | :-- |
| `pronunciationLocked: bool` + `pronunciation: string` | 「未锁定」与「锁定为空串」不可区分；两字段可矛盾 | `pronunciation: optional<string>` |
| `phonemesLocked: bool` + `phonemes` + `onsets` | 同上，且三者可各自缺席，而 `phonemes`/`onsets` 必须同时给出且等长 | `locked: optional<{phonemes, onsets}>` |
| `options: {needPhonemes: bool, needOnsets: bool}` | 「要 onset 但不要音素」不是合法状态，却可表示 | `depth: enum {Pronunciation, Phonemes, Onsets}` |

**尺子二：按变化频率分置，恒定量不逐词复制。**

原设计把 `binding`（生效的二元组）与 `g2pContribution`（G2P 模块定位符）放进**每个词**的
诊断载荷。但 A16 之后一个执行体绑定一个二元组、持有一个 G2P 子执行体，这两项在执行体的
整个生命周期内**恒定**——逐词携带是纯复制。改为执行体级访问器，只有 `hitStage` 留在词级。

**顺带删除**：`LinguistExecutive::initialize` 与 `LinguistInitArgs`。执行体在创建时已由
`LinguistRuntimeOptions` 完成全部绑定，Level 1 没有第二份初始化参数可携；子执行体各自的
`initialize` 由 wolf provider 内部调用，不外露。留一个空的初始化入口只是形制模仿 dsinfer，
不承载信息。

**通用判据**（后续设计沿用）：一个字段要进载荷，必须同时满足——① 它的取值不能由载荷中其他
字段推导；② 它的变化频率与所在载荷的粒度相符；③ 它的每种取值组合都是合法状态。

---

### A18 — 发布物是「解包即目录」，不是 `.dspk` 单文件

**决策**：语言包的 release 资产为 zip，解包后即 Package root 目录；包搜索路径下每个子目录是
一个 Package。

**依据**：spec 2.4:59 把 Package 定义为 `.dspk` ZIP，但 **synthrt main 的加载器只受理目录**——
`PackageLoader.cpp:1055-1061` 非目录即返回「Package path is not a directory」，搜索路径扫描
只收集子目录（`:614`），且 main 的 CMake 已不依赖任何解压库。`.dspk` 单文件资产今天无法被加载。

**登记为 spec 与实现的不一致**（架构文档《已知张力》第 2 条）。main 支持解压后只需换打包方式，
发布模型其余部分不变。

---

### A19 — 发布宿主为 wolf 仓 Release，按语言切包

**决策**：

- 发布宿主 = **wolf 仓库的 GitHub Releases**，不另设资源仓。资源 tag 用 `lang-v<bundleVersion>`
  与代码 tag `v<x.y.z>` 隔开命名空间；
- 现有 16 个套件切成三类：**12 个语言包** `wolf/lang-<iso>`、**1 个共享后端包**
  `wolf/g2p-multi`、**1 个直通包** `wolf/lang-zxx`（Num / Punc / Unknown 三合一，见 A25）；
- 一次 release = 一次全量快照（14 个资产 + `manifest.json`），各包 `version` 独立演进。

**取代**：D-P1（独立资源仓 `wolf-g2p-packages`）。原方案的理由是「资源版本节奏与代码发布解耦、
许可证隔离」——tag 命名空间隔离已达成前者，`manifest.json` 的可机检边界达成后者，不必多一个仓。

---

### A20 — 共享模型后端声明 G2P 契约，佐证 A2

**决策**：`wolf/g2p-multi` 中的 multig2p 模块声明 `org.openvpi.wolf.inference.G2P`，变体
`multig2p-onnx`，由 `pipe-chain` 经自身 `imports` 的私有 role 消费。

**为什么这不需要 G2PModel 契约**：共享一个 19 MB 的模型**必须**让它成为独立包中的模块
（spec 2.4《依赖项》的模块复用场景正为此设），而模块必须有契约。既然它要有契约，声明 G2P
即可——它同时也可被某个语言直接用作 `linguist/g2p`，那不是漏洞：既然声明了 G2P 契约就必须
满足 G2P 契约，「纯模型、无编排」的语言链是一种合法配置。

这比原方案「另立 G2PModel 契约 + 靠命名隔离防直引」少一份契约、少一层机制，是 A2 的正面佐证。

---

### A21 — 三端口拓扑，synthrt 端口需分线

**决策**：

| 端口 | 类型 | 说明 |
| :-- | :-- | :-- |
| synthrt（main 线） | 源码构建 | 新框架 |
| `wolf` | 源码构建 | 依赖上者 |
| `wolf-lang-packages` | 纯数据 | 从 wolf release 取语言包 |

**落点见 A29**：前两者落 wolf 仓内的 overlay，第三个（供 lite 消费的 `wolf` 端口）待 lite
迁移时再议。共享子模块 `stdware/vcpkg-overlay` 继续提供通用第三方端口。

**分线问题（必须先解决）**：overlay 中已有的 `synthrt` 端口 pin 的是
`REF 814bf81` / `HEAD_REF localization/passthrough-keys`——即 **refactor 线**（旧栈，
`srt-g2p`/`srt-s2p`/`plugins/G2P`），lite 正在消费。wolf 需要的是 **main 线**，两者产出包集
不同，**不能共用一个端口名**。

**取方案**：wolf 仓内 overlay 新增 `synthrt-main`，与共享 overlay 里 lite 在用的 `synthrt`
并存互不可见，lite 不受影响。备选「直接 bump 共享 overlay 的 `synthrt` 到 main」被否——那会让
两仓必须同时改，任一侧卡住另一侧。

lite 将来要用新框架时，**由 lite 在自己仓的 vcpkg 里提供所需端口**（A29 的作用域原则），
wolf 侧不为此预留动作。

**`wolf-lang-packages` 复刻同 overlay 中 `ffmpeg-builds` 的既有范式**（`assets.cmake` +
portfile + config 模板 + usage + 生成脚本），不发明新模式。

---

### A22 — 变体键汇的三处合并

对着 synthrt `origin/refactor` 的实现逐条核过 `pipe-chain` 与 `algo-pinyin` 之后，按 A17 的
判据合并三处：

**① 打标条目形状统一。** 两个变体做的是同一件事——逐词分类为 `convert`/`copy`——却各有一套
名字：chain 用 `tagger` / `action`，pinyin 用 `verify` / `mode`。且 chain 的实现只编译 `regex`
型，`array` / `dict` **静默丢弃**，而 pinyin 侧用的 `Inferutil::Verifier` 三型俱全、未知型即
报错。

统一到 **`{type, value, mode}`**（Verifier 组件的既有形状，改动量为零），chain 的打标步类型名
由 `tagAndValidate` 改为 `verify`，并改用同一组件——同时把「静默丢弃两种类型」这个缺陷一并
消掉。容器名按语境取：chain 是步参数 `entries`（不与步名 `verify` 重名），algo-pinyin 是
`configuration.verify`。

**② `enabled` 单轨。** 旧栈有两处独立的 `enabled`：step 项级（整步不入列，`G2pPipeline`）与
`params.enabled`（步内停用，`DictStep`/`ModelStep`）。两者表达同一件事，**只保留步项级**。

**③ `cleaner` 嵌套扁平化。** `cleaner` 对象只有 `operations` 一个成员，那层嵌套不携带信息，
扁平为 `operations`。同时删掉 `normalizeTones`——它被解析后**从不使用**（死参数）。

**顺带**：chain 的 `steps` 内联于 `configuration`，不引外部 `chain.json`。`configuration` 本就
是 variant 全权规定的块，`formatVersion` 可直接写在它顶层（变体文档 §2.2），再套一层文件只多
一级路径基准而不多任何表达力——旧草案的 chain.json 方案正是在这一层写错了路径基准。

---

### A23 — `algo-pinyin` 的引擎由绑定推导，取消 `configuration.scheme`

**决策**：cpp-pinyin 的引擎（`Pinyin` / `Jyutping`）由本模块绑定的 `(language, scheme)` 直接
选定，`configuration` 不设选择键。

> **A30 后的形态**：模块成为多语言后端，故「绑定 → 引擎」这一跳经必选的 `configuration.languageMap`
> 表达，`exports.languages` 与它逐项对账。**本条的实质未变**——选择键仍不存在，引擎仍由绑定
> 二元组推出；变的只是可服务的二元组不止一个，需要一张显式的映射表（同 A24）。

**动因两条**：

1. **撞名不同义**：旧草案拟用 `configuration.scheme`（取值 `mandarin` / `cantonese`），与语言
   身份的 `scheme`（注音体系，取值 `pinyin` / `jyutping`）在同一份文档里必然误读；
2. **可从绑定推导**（A17 尺子一）：写出来只会制造「`scheme: mandarin` 配
   `exports.languages: [{yue, jyutping}]`」这类可表示的非法状态。

**~~阻塞项 B1~~（已由 A30 / A31 解决）**：cpp-pinyin 的词典路径是进程全局态，旧栈两个语言包各带
一份根，故同进程内互相覆盖。收口方式见 A30（拆为共享后端包，一个进程一个根）与 A31（进程级
仲裁器，异根即加载失败）。

---

### A24 — `multig2p-onnx` 的三处收敛

**决策**（详见变体文档 §5）：

1. **不设 `bundle` 路径键**——`bundle.json` 必须与模块声明同目录。一份声明对应一个 bundle，
   路径可由声明位置确定，写个键只多一种表达同一件事的方式；
2. **`languageMap` 必选**，形状与 `exports.languages` 同族（`{language, scheme, ref}`），是
   契约二元组到 bundle 内部语言引用（`eng/default` 之类）的唯一通道。两者必须逐项对账——一个
   是契约面、一个是实现面，跨面不可互相推导，因此各自声明；
3. **无 `default_language`**——A16 之后执行体绑定单一二元组，不存在缺省。旧栈「未映射语言
   静默回退默认语言」（只记录缺失下标、产出循环从不检查）是缺陷，改为绑定期即失败。

`bundle.json` 的 `bundle_version` 即本变体的资源格式版本，按变体文档 §2.2 处理（现为字符串
`"1.0"` 且只校验非空，收敛为正整数 + 上限校验）。其余顶层键为发布侧元数据，契约不解释。

---

### A25 — Num / Punc / Unknown 合并为直通语言包 `wolf/lang-zxx`

**决策**：三个套件合并为**一个**公共语言包，贡献 `zxx-passthrough`，
`language: "zxx"`、`scheme: "passthrough"`。

**合并不丢信息的依据**（对着 `origin/refactor` 的 `config.json` 亲核）：三者都是**纯直通、
零资源**——各一个 tagger 正则打 `copy` 标（`(\p{N})` / `(\p{P})` / `([.]+)`），再接
`fallback: useOriginal`。除正则外完全相同，且旧栈的 `tag` 字段（`number`/`punctuation`/
`unknown`）**无任何消费方**。契约面用 `mode=copy` 表达「原样保留」，宿主不需要知道命中的是
哪一类。

**为什么用 `zxx`**：它是 ISO 639-3 的正式代码，含义为「无语言内容」（no linguistic content），
数字、标点、游离符号正合此义。因此**不必为它们破坏 `language` 的 `[a-z]{3}` 规则**，也不必
动用私用码（`qaa`-`qtz`）那种不透明写法。

**取代**：原判「三者出域、去向列为开放问题」。原判的理由是「`num`/`punc` 不是 ISO 639-3 代码」
——那是把三者**分别**建模时的结论；合并后只需要一个代码，而 `zxx` 恰好存在且语义精确。

**边界不变**：`SP` / `AP`、连音 `-`、拆音续音符 `+` 等保留记号仍归宿主预过滤（运行时文档
§4.3）；歌词中的数字与标点走本包。

**连带收益**：本包零词典、零模型、零许可问题，却完整覆盖「`desc.json` + 语言声明 + 两个推理
模块 + 歌手映射」全链路，因此被排为发布链的**引导包**（实施方案 M3.5）——用内容成本最低的包
先把整条发布链打通，比等 12 个包都转换完再一次性验证要安全得多。

---

### A26 — A11 落地前的临时测试路径

**决策**：A11（synthrt `SingerCategory` 加 `languages` / `defaultLanguage`）落地前，按下列三条
并行推进，**不阻塞 M1**：

1. **先做 linguist 侧**——M1 中约七成用例（`language`/`scheme` 形状、ID 惯例、
   `exports.phonemes`、role 基数、二元组命中）全在 `linguist` 类别内，那是 wolf 自有类别，
   **零 synthrt 改动**；
2. **过渡承载：桩 singer provider + `configuration`**——`configuration` 本就在
   `SingerCategory` 白名单内，其内容由 variant 全权规定。wolf 出一个测试用 singer provider
   插件（`SingerProvider` 基类已实现 `createExports`/`createImportOptions`/
   `createImportBinding`，桩只需覆盖 `createConfiguration`，约 30 行），测试歌手把映射放进
   `configuration.languages`；
3. **落地路径：端口带 patch**——`vcpkg_from_github(... PATCHES ...)` 是 vcpkg 标准机制。把
   A11 的两个字段作为补丁随 `synthrt-main` 端口分发，语义精确、CI 可复现、不必等上游合并。

**收敛纪律**：第 2 条的读取必须封进**单个函数**（`readSingerLanguages(const SingerSpec &)`），
今天读 `manifestConfiguration()`，A11 落地后改读 `spec.languages()`——切换点只有一处。

**为什么不直接长期用 `configuration`**：那是 A11 已否决的备选（与「`configuration` 不携带任何
语言列表」冲突，且只对特定 singer variant 成立，第三方 singer 契约无法复用）。本条只把它降为
**过渡承载**，不改变 A11 的目标形态。

**补记（2026-09-13）**：A11 已在 synthrt 落地（上游分支 `onnxruntime-builds-uptake`：
`SingerSpec::languages()` / `defaultLanguage()`，形状与 role 存在性由 `SingerCategory` 在开包时校验；
spec 2.4 歌手模块的声明文件一节同步）。wolf 侧按收敛纪律只改了一处：`readSingerLanguages()` 改读 `SingerSpec`，
`configuration` 分支删除，全部测试歌手声明的两字段上提到声明根；`check-declarations.py` 对仍留在
`configuration` 里的映射报错，避免被加载器静默忽略。第 3 条的端口 patch 不再需要：`synthrt-main`
端口直接钉在该分支的提交上。

---

### A27 — `scheme` 由 `language` 定域，取值逐语言定案

**决策一：`scheme` 的作用域。** 匹配键恒为二元组 `(language, scheme)`，不是 `scheme` 单格。
互换承诺因此只在**同一 `language` 下**成立：`(cmn, pinyin)` 与 `(deu, pinyin)` 是两个互不相干的
二元组，同名不构成任何承诺。这条原先在域契约 §2.1 表述含糊，现已明写——它是决策二的前提。

**决策二：取值。** 对着 `origin/refactor` 的词典内容逐个判定：

| `language` | `scheme` | 依据 |
| :-- | :-- | :-- |
| `cmn` / `yue` | `pinyin` / `jyutping` | cpp-pinyin 的两个引擎 |
| `jpn` | `romaji` | `kana2romaji.txt` |
| `eng` | `arpabet` | `ds_cmudict-07b.txt` 为 `aa l ow` 形态，即小写 ARPABET |
| `zxx` | `passthrough` | A25 |
| `por` / `kor` / `ita` | `xsampa` / `romaja` / `xsampa-geminate` | A49 已定 |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位，P7） | 见下 |

**`eng` 取 `arpabet` 而非 `cmu`（用户拍板，且与字段语义一致）**：`cmu` 命名的是**词典来源**
（CMUdict），而 `scheme` 按定义是**记法**。同一份 CMUdict 可转写成别的记法，同一套 ARPABET 也
可来自别的词典——两者是正交的。生态既有的 `eng-cmu` 写法在发布注记中显式说明对应关系。

**其余八种取 `ds` 的理由**（本段为当时判断；A45 之后 `por` / `kor` / `ita` 三种已重新定名，见 A49）：实测它们**都不是标准记法**——`deu` 作 `q aa`（`q` 为声门塞音）、
`fra` 作 `ss` / `jj`（叠写）、`ita` 作 `a1` / `o1`（带重音数字）、`spa` 作 `a B`、`por` 作
`S` / `Z`（SAMPA 风）、`rus` 作 `aa ay d nn i ll`、`fil` 作 `D A J` / `Q`。各是 DiffSinger 生态
该语言的自有音素集。**用 `ipa` / `sampa` / `xsampa` 命名会误导**，因为它们不是那些记法。`ds`
的含义是「DiffSinger 生态该语言的既定音素集」；由决策一，八者同名不混淆，且将来的 `(deu, ipa)`
会正确地不匹配。

---

### A28 — 转换产物不进 git

**决策**：语言包的转换中间产物与成品**一律不进 wolf 的 git 历史**。

| 落点 | 内容 |
| :-- | :-- |
| wolf git | 转换脚本；`wolf/lang-zxx` 的创作源（零资源约 2 KB，是 source 而非 build output）；负面用例夹具 |
| gitignored 暂存 | 转换的全部输出，落 `build/lang-packages/`（现有 `.gitignore` 的 `build/` 已覆盖） |
| wolf release | zip 资产 + `manifest.json` |
| wolf git | `assets.cmake`（生成物，但只是文件名与 SHA512 的几十行文本，随 release 在同一提交里更新——见 A29） |

**理由**：语言资源是 133k 行词典与 19 MB 模型，入库即永久增重，而 release 资产是它们更合适的
载体。这也是 A19 选择「发布宿主为 wolf Release」之后的必然配套——若成品同时进 git，等于同一份
内容存两份且其中一份永远删不掉。

**溯源纪律（必须执行）**：转换脚本必须**固定 `origin/refactor` 的 commit SHA**，首次 release
notes 记录该 SHA。否则 refactor 分支来日删除或被 GC 后，源资源的唯一副本只剩在 release zip 内，
转换无法复现。**这是本决策唯一的实质风险，缓解成本近乎为零，不得省略。**

---

### A29 — wolf 自带 in-repo overlay，承载两个自有端口

**决策**：wolf 仓内新增 `scripts/vcpkg-ports/`，承载 **`synthrt-main`** 与
**`wolf-lang-packages`** 两个端口；`scripts/vcpkg-manifest/vcpkg.json` 的 `overlay-ports` 写成
有序两项——本仓 overlay 在前，共享子模块在后：

```json
"overlay-ports": [ "../vcpkg-ports", "../vcpkg/ports" ]
```

共享子模块 `stdware/vcpkg-overlay` 继续提供通用第三方端口（qmsetup、stdcorelib、boost-test…），
不放 wolf 自有的两个。

**理由三条**：

1. **`synthrt-main` 是过渡端口，只有 wolf 需要**——lite 仍在 refactor 线，而这个端口还要携带
   A11 的 patch。把一个只服务单一消费方、且带补丁的端口塞进 lite 也消费的共享仓，是给别人
   增加噪音；
2. **`wolf-lang-packages` 的 `assets.cmake` 每次 release 都重新生成**。端口在本仓，发布脚本
   就能在**同一个提交**里更新它——原方案的「生成 → 拷进 overlay 仓 → 推 overlay → 两仓各自
   bump 子模块指针」四步跨仓同步随之消失；
3. **端口与其产物同仓，版本关系不会错位**。跨仓时 overlay 的某个提交对应哪一次 release 只能
   靠人记，同仓则由提交本身固定。

**为什么不复用 `synthrt` 这个端口名**：wolf 的 overlay 排在前面，同名端口会**静默遮蔽**共享
overlay 里的那个。遮蔽在 manifest 上完全看不出来，任何人对比 wolf 与 lite 「都依赖 synthrt」
时都会被误导。取显式的 `synthrt-main`。

**作用域原则**：**wolf 仓的 overlay 只承载 wolf 自己消费的端口。** 其他仓（lite）需要什么
端口，由那个仓在自己的 vcpkg 里提供。因此本方案不产出「供 lite 消费的 `wolf` 端口」，也不为
「把端口提升到共享 overlay」预留步骤。

这条原则让每个仓的依赖面自洽——看一个仓的 manifest 与 overlay 就知道它需要什么，不必跨仓推断。

**已知代价**：lite 将来若要消费 wolf 的语言包，其端口定义（含 `assets.cmake` 的文件名与
SHA512）会与本仓的那份重复，且两份会各自漂移。届时 lite 可自建、可引用本仓 overlay 路径、
也可推动提升到共享 overlay——**是 lite 侧的取舍，不在本方案范围**。此处记下重复风险，供彼时
决策者知情。

---

### A30 — `algo-pinyin` 改为共享后端包，cmn / yue 改为链

**决策**：cpp-pinyin 引擎与其词典树发布为独立包 **`wolf/g2p-pinyin`**（变体 `algo-pinyin`，
契约仍是 G2P），`wolf/lang-cmn` 与 `wolf/lang-yue` 改为 `pipe-chain`，经 `imports` 的私有 role
`backend` 消费它。原 `configuration.dictPath` 改名 `dictRoot`，新增 `formatVersion` 与
`languageMap`（必选，与 `exports.languages` 对账）。

**理由**由三条实测事实推出（锚点在变体文档 §6.1，取 cpp-pinyin `3924631`）：

1. 词典根是**进程全局态**，但**只在 `ChineseG2p` 构造期被读一次**——构造后实例自持四张词表，
   不再触碰全局。所以问题不是「有全局态」，是「一个进程里有两个根」；
2. 两个引擎是**同一根下的两个子目录**（`mandarin` / `cantonese`），一个根本来就能同时喂饱
   两者。旧栈两个语言包各带一份根，才是冲突的直接成因；
3. 两份存量词典与 cpp-pinyin 自带的 `res/dict` **逐文件字节相同**（11 个文件中 10 个完全一致，
   唯一差异是 `mandarin/trans_word.txt` 少一行 `吒:咤`，即包比上游旧一版）。它是**引擎载荷**，
   出现在语言包里本身就是旧栈的分层错误。

**为什么用「共享后端」这个形状而不是别的修法**。候选有四：

| 方案 | 效果 |
| :-- | :-- |
| 收录纪律：约定所有 `algo-pinyin` 模块共用一个根 | 跨厂商不可执行，且与 spec 2.4 的包自包含相抵 |
| 上游改造：把根改成构造参数 | 治本，但不在我们控制内，落地前无用 |
| **共享后端包** | 结构上消灭第二个根，且与 `multig2p-onnx` 同形 |
| 仅进程级仲裁 | 把静默失灵变成清晰失败，但**不能让 cmn 与 yue 都可用** |

取**第三 + 第四**：拆包让正常情形正确，仲裁让异常情形可诊断（A31）。第二条并行推进，落地后
仲裁退化为空操作。

**注意它与 `g2p-multi` 的动机不同**：后者是为共享 19 MB 模型省体积；本包的两个词典子目录互不
重叠，拆包**不去重任何字节**。它换来的是「一个进程一个根」这条结构事实。

**副产品**：cmn / yue 与其余 11 个语言结构一致——每个语言都是「`pipe-chain` 覆在共享后端上」。
打标从引擎移入链的 `verify` 步（A22 已使两者条目形状完全相同，迁移零翻译），后端因此彻底语言
无关。

**发布侧联动**：包版本随 cpp-pinyin 端口版本走，词典在打包时取自该端口的 `share/cpp-pinyin/dict`
——词典与读它的引擎是一件东西，不该各自编号，也不该进 git（A28）。

---

### A31 — 进程级词典根仲裁器

**决策**：`algo-pinyin` 插件内持一个 provider 域的进程级仲裁器，登记本进程唯一的词典根：同根
放行，异根即**加载失败**并在诊断中同时点名两个根；登记与引擎构造共用一把锁。

**为什么拆包之后还要它**。A30 把「必然两个根」降为「正常情况下一个根」，剩下三个口子：

1. 第三方另发一个 pinyin 后端包；
2. 声库逐项覆写后端模块，指向别处；
3. `setDictionaryPath` 与构造之间的**形式数据竞争**——即使两处写同一个值，无同步的并发读写在
   C++ 内存模型下就是未定义行为。

一把锁同时罩住「设全局」与「读全局的构造」，把 1、3 一并关掉；2 由异根检测拦下。

**登记点在 Acquire**，不在执行体创建：两个根冲突是关于**包**的事实，与绑定无关，理应在承载
它们的那次加载里失败。这也符合三级失败模型——**把静默失灵换成确定性的加载期失败**。

同处补两条旧栈缺的守卫：构造后校验 `initialized()`（旧栈只在 `start` 期报运行时错误）；保留
旧栈的词典目录预检，其注释记着一次真实事故——路径不存在时 `Pinyin::Pinyin()` 会无限挂起，把
模块永久卡在 Loading。

---

### A32 — 引擎实例每执行体一份，且不进资源缓存

**决策**：每个后端执行体各持一个引擎实例；`algo-pinyin` 的解析产物**不进 `ResourceCache`**。

**理由**：`ChineseG2p` 的全部转换方法是 `const`，却经 `d_ptr` 改写实例暂存
（`ChineseG2p_p.h:55-56`、`:67-73`，被 `ChineseG2p.cpp:181` 等六处调用）。单个实例因此不能
承接并发转换——旧栈 `PinyinG2pTaskImplBase::start` 只取 `shared_lock`，实为放行并发写。记作
**B1-b**。

资源缓存的契约是「**只读**解析产物」（资源缓存文档 §1），塞进一个可变对象会破坏该不变式。
比起让缓存悄悄破例，**把该变体排除在外并写明理由**更诚实。代价是内存 ×k（每份约 1–2 MiB 的
词表），换来无锁真并行。

考虑过的替代：进程内共享一个实例 + 每次调用互斥。内存 ×1、临界区只是哈希查表，争用可忽略——
但共享的是可变对象，仍需为它在缓存文档里破例。上游若把暂存改为局部变量，第三条路才成立：
词表进缓存、暂存留执行体。

---

### A33 — `model` 步按极大连续段分批

**决策**：`pipe-chain` 的 `model` 步只作用于 `mode=convert`、未丢弃、尚无发音的词，但送入后端
时按**原词序中的极大连续段**切分，不把过滤后的词拍平成一张表；`batchSize` 缺省为不再切分。

**理由**：后端可以跨相邻词取上下文。`algo-pinyin` 的短语表就是——「银行」与「银 · 行」读音
不同。被别的词隔开的两个词不是邻居，把它们拼进同一次调用会产出错误读音。旧栈的
`PinyinG2pTaskImplBase` 用 `groupLyrics` 按连续同 mode 段分组，正是这个道理；旧栈的
`ModelStep` 反而是拍平后按 `batchSize` 切，因为它服务的是逐词模型。

`batchSize` 缺省不切分，因此对上下文敏感的后端不必设置它；逐词模型（`multig2p-onnx`）显式设置
也不受影响。**相邻性是语义的一部分**，这条要写进契约而不是留给实现巧合。

---

### A34 — 不合式词典由转换管线归一化，读入端保持严格

**决策**：`dict` 步的词典读入**严格**——分隔符只认制表符，行内缺列即加载失败（另有 BOM 剥离与
CMU 式 `word(n)` 归并两条约定）。存量中不合式的词典由 `scripts/convert-g2p-packages.py` 改写为
规范 TSV，**不靠读入端放宽**。

**实测清点**（10 份现网词典）：

| 文件 | 情况 |
| :-- | :-- |
| `fil_dict.txt` | 24752 行**全部用空格分列**，零制表符 |
| `kor_dict.txt` | 开头 9 行 `;;;` BibTeX 引文 |
| `ds_cmudict-07b.txt` | 开头 1 行 `;;;` |
| 其余 7 份 | 规范 TSV |

**其中第一条是一个此前无人发觉的缺陷**：旧栈的 `PhonemeDict::load` 只认制表符且**静默跳过**
不合式行（`PhonemeDict.cpp:188-196`），所以菲律宾语词典的 24752 条**一条也没有被读到过**——
该语言的 `dict` 步一直是空转，全部落到兜底。

两条路可选：读入端接受空白分列（fil 立刻可用，但把格式定义放宽成「随便什么空白」），或修数据
（读入端保持一把尺子）。取后者——**转换管线本就是为了把旧资源带进新格式而存在的**，这正是它
该做的事；而静默跳过换成加载失败，同类问题以后第一次就会被发现。

---

### A35 — `synthrt-main` 的 `onnx` feature：ORT 载荷由端口就位

**决策**：`synthrt-main` 端口新增可选 feature `onnx`，打开 `SYNTHRT_BUILD_DSINFER` 与 ONNX
驱动，并依赖共享 overlay 已有的 `onnxruntime-builds`。不启用时行为不变（dsinfer 关闭）。

**约束下的解法**。两条 synthrt 线的 ORT 接线不同：`refactor` 已改造为
`find_package(onnxruntime-builds)`，而 `main`（`3c7549d`）仍取
`${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime/default/include`，目录不存在即
`return()`（`dsinfer/util/onnxutil/CMakeLists.txt:1-8`）。改上游违反 C1，故**由端口在 configure
前把头文件铺到 main 期望的位置**。

**只需头文件**：驱动用 `stdc::SharedLibrary` 在运行期 `dlopen` 并手动 resolve `OrtGetApiBase`
（`Runtime/OnnxRuntime.cpp:133-171`，配合 `ORT_API_MANUAL_INIT`），链接期不引 ORT 库。运行期把
`share/onnxruntime-builds/runtime/default/` 指给驱动即可。

**flavor 取默认**：`onnxruntime-builds` 的默认 flavor 在 Windows 是 DirectML NuGet、其余平台是
GitHub 的 CPU 包，正是所需；`cuda12` 是可选 feature，不启用。另显式传
`DSINFER_ENABLE_CUDA=OFF`（上游缺省为 ON）。实测产出的 `libonnxdriver.so` 不 NEEDED 任何 CUDA
库。

**做成 opt-in 而非常开**：不做 multig2p 的构建不必拖 ORT 载荷与 dsinfer 编译。

dsinfer 在 main 线随 synthrt 一起安装自己的 CMake 包（`lib/cmake/dsinfer`）。端口对两个包各做一次
config fixup，并保留父目录，这样消费方直接 `find_package(dsinfer CONFIG)` 取 `dsinfer::dsinfer`。
驱动本身不是链接目标，按插件搜索路径在运行期取用，与其他驱动一致。

### A36 — 驱动归属决定降级边界

**决策**：`multig2p-onnx` 的 ONNX 驱动由**宿主**创建、初始化（execution provider 与 ORT 路径）
并经 `SynthUnit::addRuntimeService` 注册；模块按后端名查 Runtime Service，自己绝不加载驱动。

**这条归属线同时划出两种失败的边界**：

| 情形 | 归属 | 处置 |
| :-- | :-- | :-- |
| 进程内没有驱动 | 安装环境 | 包正常加载，逐词 `DriverUnavailable`，链上 `fallback` 兜底 |
| 驱动在、模型打不开 | 包 | 执行体创建失败，加载失败 |

**为什么缺驱动不判加载失败**：那会让一台没装驱动的机器上整个语言包不可用——而九个语言包都依赖
这一个后端，等于九种语言一起失效。契约本就备了逐词的 `DriverUnavailable` 通道，正是为这种
情形；`pipe-chain` 的 `fallback` 步则把它收敛成「原词直通」。

**为什么模型打不开就判失败**：驱动都在了模型还打不开，说明包本身坏了。这不是环境问题，装什么
都不会好。

旧栈两种情形都只告警并回退 copy，于是「模型缺失」与「驱动缺失」在现象上不可分。

---

### A37 — Level 1 只做贪心解码，beam 相关键取值受限

**决策**：`multig2p-onnx` 只实现贪心解码。`beamSize` > 1、`topK` > 1、`lengthPenalty` ≠ 0
一律**加载失败并指名不支持**。

**理由两条**：

1. **唯一存在的资源不需要它**。`wolf/g2p-multi` 的配置是 `beamSize: 1, topK: 1,
   lengthPenalty: 0.0`；
2. **接受键却忽略它会谎报模块能力**。旧栈的 beam search 有 366 行，且**无任何现网资源触及**，
   因而没有对照输出可验。照搬一份验不了的实现，比一句「不支持」更危险——前者会让人以为它管用。

`topK > 1` 的真正候选必须靠 beam search 产出，故两个键同受此限，不存在「贪心 + topK」的中间态。

**将来补上 beam search 属实现增强**：`configuration` 键汇不变、`formatVersion` 不变、`level`
不变，只是原本失败的取值开始工作。这也是为什么现在拒绝它是安全的——放宽不是破坏性变更。

---

### A38 — 保留符号按名字查，不按下标

**决策**：bundle 词表的四个保留符号（`<unk>` / `<pad>` / `<bos>` / `<eos>`）在词表中**按名字
查询**，缺任一即加载失败。

**理由**：旧栈硬编码 `unk=0 / pad=1 / bos=2 / eos=3`，那是导出惯例而非格式约束。一份挪过位置
的词表不会报错，而会以四个错误 id 跑完整个解码——产出的是**看起来合理的音素序列**，既不像
崩溃也不像空结果，是最难发现的一类故障。按名字查把它变成加载期的一句话。

同处顺带把 `lookup` 从线性扫描（758 符号 × 每字符一次）换成哈希表。

---

### A39 — 脚本沙箱的边界与 `utf8`

**决策**：`lua` 变体的沙箱在 `luaL_openlibs` 后移除 `io` / `os` / `debug` / `package` /
`require` / `module` / `dofile` / `loadfile` / `load` / `loadstring` / `collectgarbage`；补一个
`utf8` 库；脚本在 Acquire 期编译并校验入口全局函数；`lua_State` 每执行体一份，不进缓存。

**语言包是数据，数据不该能打开文件。** 旧栈的移除清单**漏了 `load` 与 `loadstring`**——两者都
能从字符串构造并运行新代码，等于给沙箱留了一扇门。本实现补上，并有用例断言这些名字在脚本里
全部为 `nil`。

**`utf8` 补的是完整形状**：解释器是 LuaJIT（Lua 5.1 语义），不带 `utf8`；非 ASCII 记法的脚本
连遍历自己的输入都做不到。既然取了标准名字，就该给 Lua 5.3 的完整语义（`char` / `codepoint` /
`codes` / `len` / `offset` / `charpattern`）——形似的子集会让照手册写 `utf8.offset` 的作者撞上
一个不存在的函数。

**加载期编译**：编译失败或未定义入口函数即包加载失败，不等到第一次转换。Onset 脚本返回表长度
与输入不符同样即时报错——契约是一个音素配一个标记，短表会让词尾静默失标。

**不进缓存**：被共享的会是执行上下文而非只读解析产物，与资源缓存文档 §1 的不变式冲突（A32 的
同一把尺子）。共用的只是模块读入一次的源文本。

---

### A40 — 带外部后端的变体条件构建

**决策**：`multig2p-onnx`（需 dsinfer）与 `lua`（需 LuaJIT）按依赖存在与否条件构建。测试桩的
`plugin.json` 由构建生成，**只声明本次构建未承载的三元组**。

**理由**：两个变体各自拖一份可观的外部依赖，而不做模型推理的构建不该为此付代价（A35 把 ONNX
做成 opt-in 的同一条理由）。

**桩的声明必须随之收缩**，否则真插件与桩会同时可发现，落到「首个全匹配者当选」的目录序上——
那正是 §1.2 收录纪律要避免的可表示歧义。生成 `plugin.json` 让「本次构建缺什么，桩就补什么」
成为构建期事实，不靠人维护两份清单同步。

两种构建都经验证：完整构建 14 个测试，最小构建（无 dsinfer、无 LuaJIT）12 个测试，且 15 个包
在两种构建下都加载通过。

### A41 — 推理执行体的取消口径

**决策**：五个推理解释器的执行体一律实现合作式取消。三条口径：

1. **取消不是失败**——`state()` 取 `Canceled`，已完成的词照常返回。调用方需要知道链走到哪里，
   而一个 `Error` 只说「没走成」；
2. **`stop()` 在无执行时是空操作**——一个越过自己那次执行的取消请求若留到下一批，「停掉一个
   已结束的批次」就会静默吃掉下一个批次，这是最难查的一类故障；
3. **「词边界」对能自己死循环的实现不成立**，此类实现须另备打断手段（A42）。

**为何此前缺失**：运行时文档写明「实现须在**词边界**响应」，而五个执行体的 `stop()` 一律
`return {}`——即请求被接受却从不生效。表级查表的批次以微秒计，所以没人会注意到；`lua` 则可以
永久挂起。这是一条被文档要求、被实现遗漏的口径，复核时补齐。

---

### A42 — 脚本沙箱必须关掉 JIT，且移除 `jit` 全局

**决策**：`lua` 沙箱在 `luaL_openlibs` **之后**调用
`luaJIT_setmode(..., LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`，并把 `jit` 一并移除。

**这是实测结论，不是推理**。用最小复现程序验过三种配置：

| 配置 | 裸 `while true do end` + count hook |
| :-- | :-- |
| JIT 开 | **永久挂起**（钩子在编译出的 trace 内不触发） |
| `luaJIT_setmode(OFF)` 在 `luaL_openlibs` **之前** | **永久挂起**（开库把编译器又打开了） |
| `luaJIT_setmode(OFF)` 在 `luaL_openlibs` **之后** | 钩子触发，调用被打断 |

**反直觉之处**：跑得够久、值得被 JIT 编译的循环，恰好就是最需要能被打断的那个。钩子在冷代码上
工作、在热代码上失效——测试若只用短脚本会全程绿灯。

**`jit` 全局也得拿掉**：否则脚本一句 `jit.on()` 就把编译器打开，自行拆掉停止开关。沙箱的其余
移除项防的是「跑出去」，这一项防的是「关掉刹车」。

**代价**：解释执行。这些脚本每词只做几次字符串操作，开销不值一提。

**旧栈同样装了 count hook，且全树零处 `luaJIT_setmode`**——其取消机制对它唯一存在的目标场景
（失控脚本）无效。

---

### A43 — 转换词的 `pronunciation` 即 `candidates` 首项

**决策**：`pipe-chain` 与 `algo-pinyin` 都保证这条不变式；链在消费后端结果时也就地归一化。

**成因**：cpp-pinyin 的候选取自**逐字表**，而读音可能取自**短语表**——两者恰在「短语决定了
读音」处分歧。「银行」的「行」读 `hang`（短语表），而逐字候选是 `["xing", "hang"]`，首项
`xing` ≠ 读音。链推理契约 §3.4.4 明写 `convert` 时「`pronunciation` 即 `candidates` 的首个
元素」，故这是实打实的违约。

**处置**：把读音提到首位，其余候选保序跟随、不重复。信息一点不少，只是顺序对齐了契约。

**链侧也做同样归一化**：链把后端结果**作为自己的输出**重新发出，那条不变式就是链自己的义务，
不该寄望于每个后端都没写错。

---

### A44 — 无 `fallback` 步时仍须上报未产出

**决策**：链在结果装配处补一道判定——`mode=convert`、发音为空、`error` 为空的词，一律置
`PhonemeGenerationFailed`。

**成因**：此前只有 `fallback` 步会设这个 error。**一条不配 `fallback` 步的链**（变体文档 §4.2
明确允许）里，词典未命中的词会以「发音为空 + error 为空」产出——同时违反两条：契约要求转换词
的发音是候选首项（两者皆空，无从成立），且 `error` 为空即宣告成功。

调用方由此无法区分「这个词没查到」与「这个词的读音就是空」。变体文档 §4.2 早已写明「链未配置
`fallback` 步时同上」，实现漏了这一句。

### A45 — `scheme` 命名规则

**决策**：`scheme := <base> ( "-" <qualifier> )*`，取值规则见发布文档 §2.1.1。要点两条：

1. **`<base>` 必须描述音素集本身**——标准记法名、社区公名，或描述其特征的造名。**禁用来源、
   生态、版本、「默认」之类的空词**；
2. **`<qualifier>` 仅表同族派生**。兄弟关系的两套集各取自己的 `<base>`。

**触发这条规则的具体问题**：共享 bundle 为德语带了两套音素集，`deu/default` 与
`deu/marzipan`。把后者命名为 `ds-marzipan` 会宣告「marzipan 是 ds 的一个变体」——而逐符号清点
表明它们是并列的两套（`deu/marzipan` 有 `ueh oeh ei au eu xh tsh dsh rh rx vf cl`，与
`deu/default` 的 ARPABET 形状几乎不交）。**可表示的假关系比说不出话更糟**。

反过来 `eng/plus` 确实是 ARPABET 加了 `ax dr dx tr` 四个符号，故 `arpabet-plus` 名副其实——
规则允许它，正因为那条从属关系是真的。

**这条规则同时判了 `ds` 的问题**：它说的是「谁在用这套音素」，不是「这套音素是什么」，因而既
无法把同语言的两套集区分开，也不告诉任何人这套音素长什么样。逐符号复核后 `por` / `kor` / `ita`
三种已按第 1 档定名（A49），余下五种仍待 P7 按第 3 档造名，`ds` 是这期间的占位值。

**扩展性**：新记法进第 1 档，新社区集进第 2 档，同族新分支加 `<qualifier>`；三者都不改动既有
取值，也不需要中心登记表。唯一性只需在语言内成立（二元组由 `language` 定域）。

---

### A46 — bundle 的全部语言引用都进 `languageMap`

**决策**：共享模型的 12 个内部语言引用**全部**映射为契约二元组并写入 `exports.languages`。

**理由**：契约只要求「映射的 `ref` 必须存在于 bundle」，不要求反向——但**一套无人映射的音素集
就是任何声库都点不到的音素集**。资源已经在包里、模型已经会生成，藏起来不省任何字节，只是让
它不可用。

先前只映射九个 `*/default` 的做法，理由是「另类集需要各自的 scheme 名，而当时没有」。A45 之后
这个理由消失了：三套里两套有公名，一套是真派生。

---

### A47 — `verify` 步不得出现在产出步之后

**决策**：`pipe-chain` 在加载期拒绝任何把 `verify` 排在 `dict` / `model` / `fallback` 之后的链。

**成因**：分类决定产出步**能碰哪些词**，所以它属于产出之前。排在之后的 `verify` 会重新裁决
已经拿到发音的词，而把这样的词标成 `copy` 会让它输出原词、无声丢弃词典命中——一条读起来像
「再精修一遍」的链，实际在扔掉已经做完的工作。

**为什么是拒绝而不是定义语义**：两种可能的语义各有道理（「标记说了算」与「已产出的优先」），
而没有任何现网链需要后置 `verify`。给一个无人使用的构造挑一种语义，等于把一枚随时会被误读的
硬币留在声明面上；拒绝它则把歧义消灭在加载期。

`format` 不是产出步，故 `verify` 排在 `format` 之后合法且有用——那时还没有任何发音存在，而
分类可以看清洗后的文本。

### A48 — 链模块的 `exports.symbols` 是各步产出的并集

**决策**：一条 `pipe-chain` 的 `exports.symbols` 按**各产出步音素集的并集**声明；`dict` 与
`model` 两步的音素集**不要求一致**。

**成因**：A34 让 fil 的词典首次可读后，暴露出「同一条链吐两套音素」。逐包清点表明这不是 fil
独有：

| 语言 | 词典行携带模型不产出的记号 |
| :-- | :-- |
| `fil` | 100%（整套大写集，与模型的小写集几乎不交） |
| `ita` | 75%（`a1 e1 i1 o1 u1` 重音标记） |
| `eng` | 44%（`ax dx`，即 `eng/plus` 的扩充符号；另有 `_r`） |
| 其余六种 | 0 |

**关键事实：`eng` 与 `ita` 的错配早已存在，且已随 `lang-v0.1.0.0` 发布**——它们的词典本就是
活的。只有 fil 的词典此前从未被读到。因此「关掉 fil 的词典以保持输出统一」缺乏依据：那会留下
两个更大的错配不动，只是把最显眼的一个藏起来。

**处置**：词典保持可读。这是包的内容缺陷，归 M5——而现在暴露出来，好过发布之后才发现。

**但「M5 按并集声明 `exports.symbols`」这条推论只对一半**，补正如下。链的符号全集是各产出步
输出的并集**再加上兜底步能产出的东西**，而链推理契约 §3.1 要求 `symbols` 是「可能输出的原子
符号**全集**」——不完整的声明就是假声明。于是：

> **`exports.symbols` 与 `useOriginal` 原词兜底互斥。** 原词兜底能把任意歌词当发音产出，符号
> 集因此无界，`symbols` 只能省略（§3.1 允许省略，宿主应告警）。

实测**全部 12 条转换出的链都用 `useOriginal`**，故它们目前一个都不能声明 `symbols`。想要静态
可比对的语言包必须先放弃原词兜底（改用固定的 `defaultPronunciation`，或不配兜底步），这是
M5 要先做的取舍，不是写不写一份清单的问题。（`openSet` 后来让这条不再是沉默，见 A60。）

声库侧的义务不变：其音素表须覆盖链实际产出的并集，否则问题会推到运行期。

**顺带的 P7 输入**：`eng` 词典说的是比 `arpabet` 略富的方言，语言包该绑
`(eng, arpabet)` 还是 `(eng, arpabet-plus)` 需要生态知识，与余下五种 `ds` 的造名一并定。

### A49 — P7 分批定名，只改有硬证据的

**决策**：按 A45 逐符号核过之后，三种语言定名，五种保留 `ds` 占位。

| 语言 | 取值 | 档位与依据 |
| :-- | :-- | :-- |
| `por` | `xsampa` | 第 1 档。`E J L O R S X Z dZ tS` 十个特征符逐个是 X-SAMPA，鼻化用 `~` |
| `kor` | `romaja` | 第 1 档。修正罗马字的 `eo` `eu`、紧音 `jj kk pp ss tt`，大小写分初声/终声 |
| `ita` | `xsampa-geminate` | 第 1 档 + 限定。X-SAMPA 底，叠写表双辅音（`dZZ tSS JJ LL SS EE OO`） |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位） | 第 3 档，须造名——**证据不足以造出有依据的名字** |

**为什么不一次改完**。五个待定的各有各的麻烦：

- `deu` 是 ARPABET **记法**扩德语音（51 个符号里 38 个是 ARPABET 原符）。叫 `arpabet` 说得通，
  但那会让另一套德语 ARPABET 变体将来无法与它区分；
- `fra` 与 `rus` 都以叠写辅音区分辅元音，造名如 `doubled` 只抓住一个特征，未必是这套集最该被
  记住的性质；
- `spa` 一半是 X-SAMPA 浊擦音 `B D G`、一半是正字法二合字母 `ch ll rr gn`，叫 `xsampa` 言过
  其实；
- `fil` 与 ARPABET **零交集**（大小写敏感），也不是 X-SAMPA，是一套自成体系的大写集——看不出
  该以什么特征命名。

**一个造错的名字比一个空名更难改**：空名摆明了待办，错名会被当成事实引用。故只落地有硬证据的
三条，其余留在 P7 并附逐套音素清点（发布文档 §2.2）。

---

### A50 — 保留「查不到即输出原词」，放弃加载期音素比对

**决策**：十二个语言包的链保留 `fallback` 的 `useOriginal`，因而不声明 `exports.symbols`，
宿主无法在加载期比对音素。

**取舍的两端**：

| | 保留原词输出 | 改为固定串 |
| :-- | :-- | :-- |
| 用户看到的结果 | 与旧栈一致 | **变了**：查不到的词不再回原词 |
| `exports.symbols` | 无法声明（原词无界） | 可声明 |
| 音素不匹配何时暴露 | 运行期 | 加载期 |

取前者。**行为一致性优先**：原词输出是旧栈一直以来的行为，改掉它会改变全部十二个语言包的可观察
输出，而收益只是把一类诊断从运行期提前到加载期。链推理契约 §3.1 本就允许省略 `symbols`（宿主
应告警），所以这是契约预留的合法路径，不是绕过。

记为**已知取舍**而非缺陷：将来若某个语言包要静态可比对，它自己改用固定的
`defaultPronunciation` 即可，不需要动契约或其他包。

### A51 — `stop()` 向下传递到子执行体

**决策**：`LinguistExecutive::stop()` 在置位自身标志之外，同时对已创建的 G2P / S2P / Onset 子
执行体各调一次 `stop()`。三个子执行体指针改为原子。

**成因**：A41 让五个推理执行体都实现了取消，但**宿主唯一能停的是 linguist 执行体，而它不往下
传**——于是那些实现一次也到不了。语言执行体把**整批词**一次交给某个阶段（见
`LinguistExecutiveImpl::start` 的一次性 `g2pInput`），所以只停在本层的请求要等那次调用自己
返回。对表查询而言那是微秒，对模型或脚本而言那正是值得打断的等待。

**实证**：一条 S2P 为死循环脚本的测试链，去掉传递即挂起（实测 25 秒超时），加上即在 50 毫秒内
报 `Canceled`。

**指针原子化**：`stop()` 可在另一线程于转换创建子执行体的同时到达。原先的裸指针在此处是数据
竞争，且传递必须看得见「已经创建了哪几个」。

### A52 — 契约 Schema 是发布物，且必须与加载器一致

**决策**：四个契约的 `exports` 与 `imports[].options` JSON Schema 落 `docs/schemas/`；
`scripts/check-declarations.py` 拿它们校验真实包的每一份声明。

spec 2.4:596-602 把这两份 Schema 列为 interface 契约的**必须**内容，此前一直缺。

**一致性是重点，不是附带**。写完 Schema 后逐条与加载器对表，发现三处发布物比实现严格：

| 项 | Schema | 加载器（当时） |
| :-- | :-- | :-- |
| `exports.languages[].language` | `^[a-z]{3}$` | 任意字符串 |
| `exports.languages[].scheme` | `^[a-z0-9]+(-[a-z0-9]+)*$` | 任意字符串 |
| 二元组内的未知键 | 拒绝 | 忽略 |

**收紧加载器而不是放松 Schema**：一个 `language: "english"` 的二元组永远匹配不上任何语言——
语言身份那侧就按 `[a-z]{3}` 校验——所以它是一条**没有可达效果的声明**，加载期点名比让它静默
存在好。二元组内的未知键同理：键写错时真正的键就缺席了，而缺席已由 `required` 抓住，故未知键
只会是噪声。

两条身份文法因此从 `LinguistContrib.cpp` 的匿名命名空间搬到 `ManifestValues`——身份字段与
exports 里的二元组本就该答同一条规则，各留一份拷贝迟早会分叉。

**校验器自带边界声明**：它只实现这些 Schema 用到的关键字，遇到别的**报错而不是放行**——一个
悄悄跳过没看懂的约束的校验器，比没有校验器更坏。

**与 Q4 的关系**：该脚本就是打包期 lint 工具，不单独立项。域契约 §12 中**只看声明本身**的两项
（未被 `languages` 引用的 import、贡献 ID 书写惯例）已接入为**警告**；另两项（音素集交集裁决、
`symbols` 的归属口径）需要声库或模型的音素表，本就定为编辑器运行期职责，打包期看不见。

**严格度与运行时对齐**：schema 不符判**错**（加载器同样拒绝），lint 项判**警告**（加载器不据此
失败）。`make-lang-release.py` 在打包前调用它并**在有错时拒绝打包**——归档是不合式声明还便宜的
最后一刻。

### A53 — 第四位版本号是打包修订号

**决策**：转换出的包版本形如 `<源版本三位>.<打包修订号>`。管线对同一输入产出不同结果时，抬第
四位。

**为什么需要它**：前三位描述的是 `origin/refactor` 里那份源资源，而我们的管线会独立于它演进
——归一化词典、补 `languageMap`、加 linguist 贡献，每一次都让「同一份源资源」产出不同的包。
没有这一位，两个内容不同的包会顶着同一个版本号，而按目标点求解的消费方拿到哪一个，取决于它
下载到了哪一个。这是最难复现的一类问题。

第四位也因此有了确定含义，不再只是「补齐到四段」的填充。

---

### A54 — 后端包promise 区间，依赖方指向下沿

**决策**：后端包（`g2p-multi`、`g2p-pinyin`）的 `compatVersion` 取修订号 **0**；语言包的
`compatVersion` 等于 `version`；依赖方把目标点写成后端的 `compatVersion`。**其中语言包那一分支
已由 A68 推翻——它同样取修订号 0，本条只余后端包与「依赖方指向下沿」两句仍然有效。**

**理由**：打包修订之间变的是**打包**，不是依赖方绑定的东西——模块 `ref` 与其契约没动，所以按
修订 0 构建的消费方仍应被修订 3 服务。~~语言包则无人依赖，且 `cmn` / `yue` 的模块声明形态确实
变了（发布纪律第 4 条），故各自是一个点。~~ **「语言包无人依赖」在写下本条的同一批工作里就已被
A66 推翻，见 A68。**

**这条是被加载器逼出来的，不是推演出来的**：依赖版本原先写成后端当前版本的字面量，第一次抬
修订号，九个语言包同时报「no installed Package satisfies dependency wolf/g2p-multi」。区间
机制本就是为这种情形存在的，先前只是没有用上。

**边界**：改变了声明形态的修订仍属破坏性更新，须手工抬高下沿——自动化只覆盖「修订是纯打包
变化」这个常例。

---

### A55 — B3 分批：四种语言先闭包

**决策**：`eng` / `por` / `kor` / `ita` 补齐 `linguist` 贡献与 `direct` S2P，成为完整语言闭包。

**B3 不是铁板一块**，拆开后三档分明：

| 档 | 语言 | 状态 |
| :-- | :-- | :-- |
| 可直接闭包 | `eng` `por` `kor` `ita` | G2P 本就产出空格分隔音素 ⇒ S2P 用 `direct`；`scheme` 已定（A49）；Onset 契约允许省略且无规则资源 |
| 等定名 | `deu` `fra` `spa` `rus` `fil` | 同上，但 `scheme` 仍是占位（P7） |
| 需内容决策 | `cmn` `yue` `jpn` | G2P 产出的是**音节**，S2P 需要真实的音节→音素词典，全仓没有 |

> **后续（A66）**：第三档的前提被推翻了。那份词典是**歌手包的内容**而非语言包的，全仓没有正是
> 因为它不该在这里。这三种只出 `inference` 是终局形态，不是待补的缺口。

**`exports.phonemes` 取自该语言自己的词典**——那是这门语言工作在哪套音素上的权威答案。模型
产出的词、以及「查不到原样返回」的词都可能落在清单外；域契约 §4.1 把这份清单定为**对齐基准**
且 Level 1 不做加载期强制校验，正是这条让 A50 的取舍能够成立。若哪天要把它变成硬约束，得先
放弃原词返回。

**S2P 声明它服务的二元组**，而不按变体文档「`direct` 应省略 `exports.languages`」的通例省略：
那条通例针对的是通用模块，而装在语言包里的 `direct` 就是为这门语言服务的，声明它才能让二元组
匹配在加载期跑起来，而不是留给宿主告警。

### A56 — 层栈增 L6 会话层

**决策**：新增 `wolf::LinguistSession`，位于 L5 之上，收纳歌手→语言快照、就绪态与负缓存、
执行体池、保留词与空词处置、取消把手。完整设计见 [linguist-session.md](linguist-session.md)。

**动因不是推演，是现场证据**：ds-editor-lite 已经在 refactor 线上自建了一套
（`VoicebankSession` + `GetPronunciationTask` / `GetPhonemeNameTask` 里的附属逻辑）。它补的
每一件——`readyLanguages` / `failedS2pLanguages` 两张集合、SP/AP 特判、按语言分组、分阶段
就绪——都不是 lite 的业务判断，而是语言域的实现细节。

**只有一个正确答案的问题，不该每个宿主答一遍**：「失败缓存到什么时候」「并发开几个执行体」
「SP 算 copy 还是 skip」——三个宿主就是三种不一致的行为，而其中至多一种是对的。

**框架不提供这一层**：`srt::SynthUnit` 只有包、插件与 Runtime Service 的注册，没有扫描、
没有就绪态、没有路由缓存（`SynthUnit.h:25-95`）。所以要么 wolf 做，要么每个宿主各做一遍。

**L6 在 L5 之上而非之下**：它是宿主**持有**的对象，不是宿主**实现**的层。L5 仍是策略面。

---

### A57 — 会话借用 SynthUnit，不另立数据模型

**决策**三条：

1. **借用而不拥有 `SynthUnit`**——宿主可能还挂着别的类别与服务，包的打开与关闭是宿主对搜索
   路径与安装的判断；
2. **歌手用 `srt::ContribLocator` 标识**，不另发明键。那是框架自己的身份，拿得到 `SingerSpec`
   的宿主直接取 `spec.locator()`，不必在两套身份之间翻译；
3. **转换 IO 沿用 L4 的 `LinguistConvertInput` / `LinguistConvertResult`**。

第 3 条值得说明：lite 现在有三个入口（`convertG2p`、`convertS2p`、逐词预置发音），而 L4 的
`depth` 截断加逐词锁定（A17）**已经把它们压成一个**——要发音就停在 `Pronunciation`，要音素就
走到 `Onsets`，用户改过的发音就逐词预置。会话若再立一套 IO，等于把已经收敛的东西重新摊开。

---

### A58 — 就绪三态与双向缓存

**决策**：`Ready` / `Cold` / `Unavailable`；`probe()` 无副作用，`warm()` 主动预热；**成功与
失败同样缓存**；唯一失效点是 `refresh()`。

**失败缓存是必需品不是优化**：lite 用 `failedS2pLanguages` 挡住的是——一个 500 音符的片段，
一次语言级失败会变成 500 次重试。

**`probe()` 必须无副作用**：宿主的 UI 可能每帧问一次「这个语言能用吗」，那不该触发模型加载。
今天宿主唯一的问法是「试着建一个执行体」，即查询与动作不可分。

**不设后台重试与超时**：「什么时候该重新看」是宿主知道的事（装了新包、切了搜索路径），会话
不猜。唯一失效点因此是显式的 `refresh()`。

**`reason` 直接取自失败点的诊断串，不重新措辞**——加载器已经把话说清楚了，再包一层只会让人
查两次。

---

### A59 — 执行体池不设默认上限

**决策**：池按 (歌手 locator, 语言句柄) 分键，`convert()` 取空闲租约、无空闲则新建，随
`refresh()` 清空；**不设默认上限**。

**理由**：并发度是宿主线程池的属性，会话跟着走即可；写死一个上限只会在宿主并发更高时变成
隐形的串行点。

**代价比看上去小，因为最贵的东西已经共享了**：ONNX 驱动按 `path` 与 `(size, hash)` 两级索引、
引用计数持有 `SessionImage`（内含 `Ort::Session`），同一模型文件被 N 个 `InferenceSession`
打开**权重只载一份**（`SessionSystem.h:20-46`、`Session.cpp:523-660`）。

**这条实测同时更正了资源缓存文档的前提**：该文 §1 写「k 路并发 = k 份词典与 **k 组模型会话**」
——后半句对 ONNX 不成立。真正随执行体重复的只有 cpp-pinyin 词表（A32）与 `lua_State`（A39），
而这两样恰恰是因上游的可变状态而**无法**共享的，不是设计选择。

---

### A60 — `exports` 增开放位 `openSet`

**决策**：`exports` 同一 object 上增 `openSet: bool`（缺省 `false`）。为假时清单即全集，语义与
今日完全一致；为真时清单是已知部分，产出可能越界。取值由打包 lint 自动推导。

**问题**：链推理契约 §3.1 要求 `symbols` 是「原子符号**全集**」，域契约 §4 要求 `phonemes` 是
全集**且必填**。而十二条链的末步都是「查不到即输出原词」，产出无界——`symbols` 一条都声明不了
（A48/A50），`phonemes` 只能当建议值填（A55）。

**一个必填字段实际是建议值，这个味道必须去掉。** 三条路比较：

| 方案 | 结果 |
| :-- | :-- |
| 改为可选（与 `symbols` 一致） | 改动最小，但宿主什么也得不到 |
| 保持必填、只改措辞为「对齐基准」 | 不动代码，但必填字段仍是建议值 |
| **加开放位** | 字段变诚实，且宿主**第一次**能在加载期说出有用的话 |

取第三条。它让宿主能说「这个语言可能产出你的声库不认识的音素」——今天它只能沉默，因为字段
要么不存在、要么在撒谎。

**既有声明零改动**：缺省 `false` 即今日语义。

---

### A61 — 保留词归会话，空词与含空白词归模块

**决策**按「是契约义务还是生态惯例」切分：

| 输入 | 处置 | 归属 |
| :-- | :-- | :-- |
| 空串或仅含空白 | `mode = skip` | **G2P 模块**（链推理契约 §3.4.2 第 1 条已规定） |
| 含空白的非空词 | 原词透传 + `error = InvalidInput` | **G2P 模块**（同上第 2 条） |
| 保留标记（缺省 `SP` / `AP`） | `mode = copy` + 单个保留音素 | **会话**（生态惯例，非契约） |

**前两条是既有的契约义务，而三个 G2P 变体原先没有一个实现它**——空词经 `verify` 落入 `copy`
产出空发音，含空白的词被当普通词送进词典，`mode = skip` 是一个定义了却无人产出的枚举值。现已
实现：判定抽成共用组件 `wolf::classifyLyric()` 写一次（与 `Verifier` 同款理由：同一条规则的
两份拷贝迟早分叉），三个变体在任何步骤之前调用它。落地细节见 [linguist-session.md](linguist-session.md) §8.3。

**第三条不是契约义务**，所以不能塞进模块：保留标记集合随生态而变，写死在 G2P 变体里就是把
宿主惯例固化成契约。作为会话选项、缺省 `{SP, AP}`，用别的记法的宿主可以替换。

**项目专属标记仍归宿主**——连音符、`+` 后缀、切分标记是 lite 的工程模型，不是语言域的概念。

### A62 — L6 设计的自检结论

**决策**：在实现之前按三条尺子自检设计——能否稳定实现、拓展接口是否够、有无过度冗余——并据此
修正。完整清单见 [linguist-session.md](linguist-session.md) §12。

**两处不补即会崩**：

1. **池必须持 `PackageHandle`。** pipeline 建自 `SingerSpec&`，而框架明写执行体必须在其贡献所属
   Package 释放之前销毁（`ContribExecutive.h:31-33`）。原稿只声明「不代管包的打开与关闭」，
   漏掉了池**事实上**延长包存活这一面。**「不代管」不等于「不持有」**，所以还要给宿主显式的
   释放点 `release(singer)`；
2. **`refresh()` 不能清空在飞条目。** 原稿写「池随 refresh 清空」，而重扫与转换必然并发——用户
   装包时编辑器不会停下。把排它推给宿主，等于把一个必然发生的竞态交出去。改为按代管理：换代后
   空闲条目立即弃、已租条目归还时弃。

**一处非常规所有权必须写明**：`createChild` 返回的子执行体仍归父所有，`delete` 才是「脱离」
（`ContribExecutive.h:70-76`）。池的驱逐路径因此是 `delete`；不 delete 就会在 pipeline 下无限
累积，因为 `createLinguist` 不做记忆化，每次调用都新建
（`WolfPipelineExecutive.cpp:22-39`）。

**三处砍掉的冗余**：`LanguageStatus::binding`（目录的 `LanguageEntry` 已带）、`refresh()` 的
`Expected<void>`（包能加载即已过 Ready-2 校验，重扫无失败路径，而**永不失败的 `Expected` 是
噪声**）、池的观测 / 收缩 API（`release()` 为释放点本就必需，一件机制两用）。

**三处明确为决定而非遗漏**：不提供异步入口（宿主已有线程池与任务模型，再包一层 future 只会
多一套要对齐的取消与线程语义）、不报进度（lite 两个任务都用 indeterminate 条，没有分母）、
不缓存逐词结果（那是记忆化，与资源去重不是一回事）。

**两条以代码核实而非推演**：`createLinguist` 不记忆化，故池可成立；预置了 `pronunciation` 的词
标为 `copy` 但不带 `locked`，因而仍走 S2P 与 Onset，故 lite 的 `convertS2p` 能映射为「一个词、
预置发音、`depth = Onsets`」。

---

### A63 — L6 落地时对设计的三处修正

**决策**：实现按设计走，但有三处不得不改。都记下来，因为每一处都是设计当时看错了一件事，而不是
实现走了捷径。

**1. 歌手的键必须带版本。** A56 的设计写「不另发明键：`ContribLocator` 是框架自己的身份（包
id + 版本 + 类别 + 贡献 id）」。**括号里那句是错的**——`ContribLocator` 的头文件明写它不含
Package 版本、也不做依赖解析。而同一声库两个版本同时加载是常态，lite 自己就为此有一条
`G2pVersionAmbiguous`。改为 `SingerRef { locator, version }`：版本留空表示「唯一加载的那个」，
加载了不止一个时**拒绝**，并把「有好几个」与「一个都没有」分开说——两者的修法不同，合成一句话
等于让人查两次。目录填的是具体版本，宿主原样递回目录条目永不歧义。

> 这一条也修了一个实现自己的错：`release()` 的就绪缓存清扫最初漏比版本，于是释放一个版本会
> 顺带把另一个版本报成 Cold。两个循环现在共用同一个谓词，写一次。

**2. 目录取绑定不能靠解析 `locate()` 的结果。** 它返回的是目标贡献**自己的** locator，属于语言
包，从歌手包 `resolve()` 不到。给 `WolfPipelineExtension` 加 `binding(language)`：绑定在读清单时
就定了，问它不创建任何东西，`probe()` 因此仍然无副作用。

**3. 建执行体必须在会话锁外。** A56 只说「会话自身线程安全」，没说锁的粒度。第一版把建执行体也
放在锁内——那一步要载词典、开模型，可能是秒级的，期间宿主每帧的 `probe()` 都会卡住，正好是池
要避免的事。改为两段：锁内只做便宜的事，建执行体在锁外，而**租约在放锁之前就已占住**，所以
并发的 `refresh()` 不会把脚下的 slot 抽掉。

**已用 ThreadSanitizer 验证**，不是推演：8 线程同歌手同语言各转 60 轮、转换途中 200 次
`refresh()`、转换途中 2000 次 `probe()`，TSan 下干净。全套测试里只有 `test_MultiG2P` 报 race，
栈在 `libonnxruntime.so` 内部（主线程分配、ORT 工作线程释放），wolf 的帧只是调用方——未插桩库
的已知误报。

---

### A64 — C1 / C2 的落地范围

**C1（`openSet`）只加在自称是全集的三处**：域契约 `phonemes`、G2P `symbols`、S2P `phonemes`。
Onset 的 `knownPhonemes` **不加**——它的契约本来就写明是覆盖面的下界（通配段覆盖任意输入），
再加一个「可能超出」是把同一句话说两遍，正是复核要求避免的冗余。

**取值由打包 lint 推导，不靠作者自觉**：`check-declarations.py` 读语言引的那条链，链带
`useOriginal` 的兜底步而 `openSet` 未声明即告警，反之亦然。作者记得写不是机制。

**缺省 `false` 故既有声明语义不变**——这是这个位能加进已发布契约的前提。拼错的值判错而不按
`false` 处理：两者中 `false` 是更危险的那个答案。

**C2（Onset 缺席即逐位 `false`）不能留给宿主**。`onsets` 与 `phonemes` 等长是既有约束，长度
已经定死，剩下的只有取值一个自由度；把它留给宿主，就是让两个宿主对同一个语言包给出不同的切分。
实现一直如此，此前只是没写进契约——**只活在代码里的规定不是规定**。

---

### A65 — 最小构建须能被显式选出

**决策**：加 `WOLF_DISABLE_DSINFER` / `WOLF_DISABLE_LUAJIT` 两个开关。

**动因是一次静默失效**。A40 声称「两种构建都验证过」，而此后它长期是坏的，没有任何东西报错：

1. 夹具 `lang-chain` 在实现取消时混入了一个 `lua` 变体模块。**无 LuaJIT 时整包加载失败**，
   于是 `test_PipeChain` 的每一个用例都跟着挂，而不只是要跑失控脚本的那一个。已拆为独立的
   `lang-runaway` / `singer-runaway`，用它的那一例按构建条件跳过；
2. 没人发现，是因为**当时根本没法选出最小构建**——唯一的办法是把 `find_` 缓存项强制成
   NOTFOUND，而 `find_path` 下一次 configure 就搜回来了。最小构建目录于是悄悄变回完整构建，
   重新构建它看起来还是全绿。

**教训与 A67 同源**：一条没有人走的路，和一条走不通的路，从外面看是一样的。

---

### A66 — B3 排除：那份词典是歌手包的内容

**结论**：`cmn` / `yue` / `jpn` 的音节→音素词典**不是语言包的交付物**，B3 不成立。

**证据在 spec 2.3 自己身上**：它的歌手清单示例把这份词典写在 `configuration.languages[].dict`
（`"../assets/opencpop-extension.txt"`），路径按**歌手配置目录**解析；校验器同样按歌手侧路径
校验，并规定 `s2pMode == "dict"` 时必须有 `dict` 或 `s2pFile`。**全仓找不到这份词典，正是因为
它不该在这里。**

**因此三种语言只出 `inference` 是终局形态**，不是待补的缺口：语言包出 G2P，歌手包出 S2P 与
onset，两侧在一个进程里合成一条链。这条路径已实现并有测试——`test_HostFlow` 用真实迁移的
cmn / yue G2P 加一个歌手包形状的夹具走完整条链。

**与 B2 同款结论**：阻塞项来自对旧栈形状的误读。值得记下的不是「又消解了一个」，而是**阻塞项
本身要定期复核**——它记录的是当时的理解，而理解会变。

---

### A67 — 端口首次真实跑通

**发现**：`wolf-lang-packages` 端口**从未安装成功过**。失败与 release 公不公开无关——
`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 里是硬错误，端口一旦被使用就失败。

**为什么长期没人发现**：测试全程经 `WOLF_LANG_PACKAGES_SOURCE` 指向本地副本绕开端口，而 P8
（release 未公开）又提供了「装不上很正常」这个现成解释。**一条应急通道悄悄变成了唯一通道。**

**改法**：直接写 `copyright`，并把各包自带的 License.txt 收进去——纯数据端口该装哪些许可随
选中的 feature 而变，固定的 `FILE_LIST` 表达不了这件事。

**验法不需要公开 release**：把归档预置进 vcpkg 的 `downloads/`，`vcpkg_download_distfile`
校验 SHA512 通过即跳过取件，portfile 其余部分照常执行。结果：15 个包装进
`share/wolf/packages/`，wolf 的 17 个测试指向该树全部通过——**W3a / W5 的退出判据至此首次
达成**，只剩 HTTP 取件一段真正等 release 公开。

**同源的两处漂移一并修掉**：

- 端口 `vcpkg.json` 的 `version-string` 是手维护的，紧挨着生成的 `assets.cmake`，已经差了一版
  （0.1.0.0 对 0.1.1.0）。vcpkg 会把旧 assets 建出的树当新版发。现由发布脚本随 bundle 版本一起
  改写；
- 清单缺 `lang-packages` feature，而实施计划两处退出判据都写着
  `vcpkg install --x-feature=lang-packages`。**一条无法执行的验收判据等于没有判据。**已补，
  opt-in，不牵连普通构建。

### A68 — 语言包的 `compatVersion` 同取修订号下沿

**决策**：语言包的 `compatVersion` 与后端包同口径，取**兼容区间下沿**——最早一个仍然满足下述
六项公开表面的修订，实践上即修订号 0。A54 中「语言包 `compatVersion` 等于 `version`」的分支
作废。

**理由回到 spec 2.4 §兼容性的原文**。它先列出兼容区间内必须保持的六项公开表面（已有贡献的
类别与模块 ID、已有模块的 `interface` 与 `level`、已公开的 `exports` 能力语义、既有 `options`
写法、契约规定的运行时 IO 与错误语义、贡献类别规定的必选字段），随后写明：

> 当前 Package **可以增加贡献和可选能力**，也可以修改不影响上述公开表面的实现细节、私有
> `configuration`、模型与算法。……**任何破坏上述承诺的版本**都必须将 `compatVersion` 提高到
> 不再覆盖受影响的旧版本。

于是两点：

1. 打包修订号变动不破坏那六项中的任何一项，按规范**就不该**抬 `compatVersion`。写
   `compatVersion == version` 等于声明「我与我之前的任何版本都不兼容」——那是一句不实的
   承诺，而同节明说「承诺不实属于 Package 缺陷」。
2. 「有没有人依赖我」不是 `compatVersion` 的输入。它是对**自身公开表面**的承诺，与依赖图
   无关。A54 用依赖关系来推兼容口径，取错了轴。

**A54 的事实前提也本就不成立**：A66 把 `cmn` / `yue` / `jpn` 的 linguist 与 S2P 判归歌手包，
歌手包因此必须在 `dependencies` 里指向语言包。wolf 自己生成的夹具就写着
`dependencies: [{ "id": "wolf/lang-cmn", "version": "1.0.1.2" }]`——「语言包无人依赖」在写下
A54 的同一批工作里已经被推翻了。

**实证**：把 `wolf-lang-cmn` 的 `version` 与 `compatVersion` 一起抬到 `1.0.1.3`（A53 规定该抬
第四位的常例），`test_HostFlow` 六个用例全部报
`no installed Package satisfies dependency wolf/lang-cmn`。这与 A54 为后端包记下的实证是同一个
失效，只是留在了语言包这一侧。

**连带纪律**：

- `convert-g2p-packages.py` 去掉 `compat_floor(...) if is_backend else version` 的分支，一律
  取下沿；
- 「依赖方指向目标的 `compatVersion`」由 `check-declarations.py` 升为**错误**而非警告，发布
  脚本据此拒绝打包。A69 否掉晚绑定之后，这是这条线上唯一的机制，不能是软提示；
- 回归测试必须用「**旧夹具 + 新包**」的组合。夹具与包一起重新生成时这个失效不会出现，A54
  那次正是这样漏过去的。

---

### A69 — 不做能力晚绑定：语言模块在声库初始化期彻底定死

**决策**：不引入按 `(language, scheme)` × `(interface, level)` 的运行期能力解析。语言模块的
绑定由声明中的 `imports[].ref` 在**加载期完全确定**；声库初始化完成时，每个语言能不能用、能
用到哪一层，必须已经有答案。

**被否掉的方案**：让 role 缺席即表示「由环境按二元组解析」，从而使歌手包不再依赖语言包。它
确实能从结构上消掉版本耦合，代价是把「目标不可用」从加载期挪到 `warm()`——声库装得上，某个
语言却可能到预热时才发现用不了。**这个代价不接受**：装上即可用是这一层要守住的性质。

**搜索空间是被 spec 2.4 夹死的**，一并记下，免得重新提出：

| 想法 | 为什么不行 |
| :-- | :-- |
| `dependencies[].required = false`，缺包即降级 | 2.4 变更表明文：2.3 的可选依赖「**不支持**，所有依赖均为强制依赖」 |
| `imports` 条目省略 `ref` | §`imports`：`ref` 必选，且「不设简写」 |
| 在 `configuration` 里加绑定键 | §三个语法块的归属：「需要参与跨模块契约的内容应由 `exports` 公开，而不是作为 `configuration` 中的契约字段」 |
| 加一个类别追加的根级字段 | 该层的分界是「在解释器被选出来之前就要用上」（§模块声明的三层），而 import 绑定发生在 Ready、在 Acquire 之后，不满足该判据 |

**因此包级降级不做，且这是规范结论而非取舍**：依赖强制 ⇒ 语言包缺席必然让整个声库装不上。
要让缺一个语言不拖垮整个声库，只能靠**不依赖它**——即语言包自带 linguist（`eng` / `ita` /
`kor` / `por` / `zxx` 的形态），而不是靠运行期解析。这也把 A55 剩下的收尾工作从「可做可不做」
提到了「唯一出路」：一个语言包只要自带 linguist，它就不再是任何声库的必需依赖。

---

### A70 — 降级只保留链级与音素级，且都在初始化期算定

**决策**：

1. **链级**：`linguist/s2p` 由「恰好 1」放宽为 `0..1`。缺席时该语言的最深深度是
   `Depth::Pronunciation`；`linguist/onset` 缺席时是 `Depth::Phonemes`（后者已是今日行为，
   见 A64 与域契约 §5.0）。`linguist/g2p` 仍然必选——没有它就没有语言。
2. **音素级**：会话计算 linguist `exports.phonemes` 对宿主给出的声库音素表的覆盖度，交出
   `coverage` 与 `missingPhonemes`，**不设门限**。唯一例外是 `coverage == 0 && !openSet`，
   由会话直接判 `Unavailable`。
3. 两者都在初始化期算定，`probe()` 直接回答，不触发任何加载。

**放宽 S2P 是变体的权限，不是 spec 改动**。spec §`imports`：

> `role` 是导入方为该条目指定的本地 slot。导入模块通过 `role` 区分各项用途，并**由自己的
> `variant` 规定哪些 role 必须存在**以及每个 role 接受哪一种目标契约。……导入方**仍可**严格
> 要求自己契约规定的 role 存在且只指向规定的目标契约。

是「仍可」，不是「必须」。`wolf` 变体收紧或放宽自己的下界都在授权范围内。

**为什么这是加法而不是破坏**：spec §兼容性把「增加贡献和可选能力」列为兼容区间内允许的变化。
放宽一个必需 role 只让原先失败的声明变为可加载，既有声明的行为逐字不变，因此不触发任何
`compatVersion` 抬升。

**为什么门限不归 wolf**：Status 里那组逐包清点（`fil` 100%、`ita` 75%、`eng` 44%、其余六种
0%）说明任何固定门限都会误伤——44% 对一个以英文为主的工程可能完全够用。而
`coverage == 0 && !openSet` 是另一回事：清单自称是全集，声库一个都唱不了，那不是降级，是接错
了。这是 wolf 在这条线上唯一自定的判断。

**A50 的归属不变**：音素比对仍属运行期而非加载期。变的只是算法收进会话，宿主拿到的是结论而
不是原料——与 A56 立 L6 的理由同一条：每个宿主都要算，而它只有一个正确答案。

---

### A71 — 「初始化即定」定的是绑定与形状，不是预热

**决策**：`Cold` 保留，但语义收紧为「**绑定已定、`maxDepth` 与覆盖度已算，仅未预热**」。A58
写的「`Cold` 只说目录里有这条路由，且尚未试过」在 A69 之后不再准确，相应限定为「不是**已加
载**的承诺」。

**为什么可以收紧**：不做晚绑定后，绑定在加载期已由 `ref` 完全确定；`maxDepth` 由 `imports`
集合直接读出；覆盖度只是一次集合运算。这三样在初始化期全部有答案，且都不需要打开任何模型。
`Ready` 与 `Cold` 的差别因此只剩「资源是否已在内存里」。

**为什么不把预热也搬到初始化**：`Cold → Ready` 这一步要付的不是词典——词典类资源全部在 Acquire
期由各变体的 `createConfiguration` 解析完毕（chain 的 `dict` / `verify`、s2p 的表、onset 的规则、
multig2p 的 bundle 与 vocabulary、lua 的脚本源），装包时就已经在内存里，且经 `ResourceCache`
跨执行体共享。预热真正付的是**每执行体一份且按 A32 / A39 / A59 无法共享的那三样**：cpp-pinyin
引擎（`createEngine`）、ONNX session（`Decoder::open`）、`lua_State`（`Sandbox::create`）。一个
声明 12 种语言的声库若在初始化期全部预热，就是一次性付清这 12 份。A63 ③ 正是为这笔开销把建
执行体移出会话锁的；提前到初始化只是把它挪到更早、更无法回避的位置。「彻底处理好」要的是
**确定性**，不是**已加载**——前者靠清单就能给，后者只能靠真的去建。

**仍然只能在 `warm()` 暴露的**：驱动没装、模型打不开、词典读不出。这些是安装环境与包内容的
属性（A36 已有同款分野），清单里看不出来，任何设计都不可能在初始化期回答。`Cold → Unavailable`
这条边因此保留。

---

### A72 — 进程级词典根的保留随配置对象存活

**决策**：`PinyinEngineRegistry::reserveRoot()` 返回一个 RAII 的 `RootReservation`，由该模块的
`Configuration` 对象持有；保留计数归零时根被忘记。

**它修的是什么**：保留原先是一次不可撤销的全局写入，发生在 `createConfiguration`（Acquire）里。
后果有二，都已实测复现：

- 一次**失败**的加载会把根永久留下。此后进程里任何指向别的根的 pinyin 包都装不上——一个根本
  没装上的包，废掉了一个能用的包；
- **卸载**也从不归还根。装过一次 cmn 的进程，此后再不能换根。

**为什么这个形状是对的**：spec 2.4 §加载事务要求「Acquire 或 Ready 创建的每项资源和状态变化都
必须保持事务私有且立即写入完成日志，以便 rollback 完整撤销」，并要求修改共享目标的 provider
提供等价于 prepare / commit / abort 的机制。配置对象**本身**就是那个事务私有物：加载失败它随事务
销毁，加载成功它随模块实例存活，卸载时它随包销毁。把保留挂上去，abort 与 release 两条路径就都
是自动的，不需要另写三段式，也不可能漏写其中一段。

**保留是可重入的**：同一个根的第二个模块拿到自己的一份保留，计数归零才忘记。异根仍然当场失败
并点名两个根，A31 的裁决不变。

**测试**：`test_PinyinBackend` 增两例——失败的加载之后另一个根可用、卸载之后另一个根可用。两例
都在同一个 `SynthUnit` 内加载，因为销毁 unit 会卸掉解释器插件、连带重置那个单例，用两个 unit
会恰好掩盖住要测的泄漏。

---

### A73 — `stop()` 取消的是当前或下一次转换

**决策**：`LinguistExecutiveImpl::stop()` 置位，`start()` 用 `exchange(false)` **消费**该位：
入口读到即立刻以 `Canceled` 返回，运行中读到则按既有的分段检查返回已完成部分；两条路径都清位，
所以被放回池中的执行体不会继承上一次的取消。会话侧 `CancelToken::enrol()` 相应改为返回布尔，
已取消的令牌让 `convert()` 根本不启动这一批。

**它修的是什么**：`start()` 原先第一行是 `m_stopRequested = false`，而会话在其之前刚调过
`stop()`。于是**任何在 `convert()` 之前落下的取消都被静默丢弃**——而这正是最常见的次序：用户
点停止，排队中的那一批才开始跑。实测：预置取消后整条链跑到 onsets。原有用例的注释声称测的就是
这件事，但断言里没有一句碰它。

**为什么不选「`stop()` 只在运行时生效」**：那是 `srt::ITask` 的做法（`requestAsyncCancellation`
只在 `running` 时置位），对一个自己管理排队的调用方是够的。会话不是那样的调用方：它先租执行体、
再启动，两步之间必然有窗口，而窗口里落下的取消必须算数。

**补记（子执行体同一口径）**：推理解释器的执行体原先在批次入口把停止位清零，理由是父级广播的停止
可能在自己无事可做时到达，留着会毒害下一批。代价是一个真实窗口：父级检查过自己的停止位、把子执行体
发布出去、子执行体入口清零——落在这中间的停止连同脚本沙箱的中断一起被抹掉，整批不可中断地跑完。
现在子执行体与父级同一口径：入口读到未消费的停止即以 `Canceled` 返回，停止位由任务面在**返回后**
消费。「陈旧停止毒害下一批」改由父级化解：linguist 执行体发现某阶段报 `Canceled` 而本次转换并未
请求停止，就知道那是上一次留下的、且已被该阶段的这次启动消费，于是重试一次该阶段。两个窗口都关上，
且不需要新的跨插件接口。

---

### A74 — 导出的链接接口只许出现 config 会带入的目标

**决策**：`blake3` 与 `re2` 同归 `LINKS_PRIVATE`（两者都只出现在 `.cpp` 里），并在
`src/lib/CMakeLists.txt` 加一条配置期断言：`INTERFACE_LINK_LIBRARIES` 里每个带 `::` 的目标都必须
在 `wolfConfig.cmake.in` 会带入的名单中，否则配置直接失败并说明该怎么办。

**它修的是什么**：本轮之前 `blake3` 在公开 `LINKS` 里，而 `wolfConfig.cmake.in` 只
`find_dependency(synthrt)`。于是 README「How to Use」那段照抄下来就在 generate 阶段失败：
*the link interface of target "wolf::wolf" contains BLAKE3::blake3 but the target was not found*。
**仓内测试永远发现不了**——它们直接链构建树里的目标，从不走安装出来的 config。

**断言而不是文档**：这一类漂移的特征是「改的人改的是 A 处，坏的是 B 处，而 B 处没人跑」。把
不变式写成配置期的硬失败，是唯一能让改动者当场看见的位置。

---

### A75 — 执行体一律持 `srt::ITask`，不再手写任务面

**决策**：抽出 `wolf::ExecutiveTask<Input, Result>`（`include/wolf/Support/ExecutiveTask.h`），
十一个执行体各持一个并转发；`quit()` / `wait()` 的空覆盖一律删除，让
`srt::InferenceExecutive` 自己的转发生效。

**它修的是什么**：手写的任务面有三处一致的错，十一份拷贝一模一样地错：

1. `waitForFinished()` 直接返回 `{}`，于是框架卸包时的等待被当场满足，转换还在跑；
2. `startAsync` 在八处是**同步就地回调**（与 API 头注释「回调在工作线程上跑」矛盾），第九处
   `LinguistExecutiveImpl` 裸 `detach` 一个线程、谁也等不到；
3. 没有一处拒绝第二次并发执行，而契约写的是一个执行体一次只carry一轮。

更糟的是第四点，是这一轮才查出来的：`chain` / `pinyin` / `multig2p` / `lua`×2 把
`quit()` / `wait()` 覆盖成 `return {}`，**把 `srt::InferenceExecutive` 本来就接好的
`quit()→stop()`、`wait()→waitForFinished()` 废掉了**。卸包时这些执行体既不被停也不被等。上游
把事做对了，wolf 把它盖掉了。

**为什么抽一次而不是抄十一遍**：与 A61（`classifyLyric`）、A43（`Verifier`）同一条理由。十一份
拷贝已经证明会一起错，而且是一模一样地错。

**桥的形状**：body 与「是否被取消」两个可调用对象，而不是每个变体配一个伴生类（dsinfer 的
`DurationTask` 是那种形状，但它只有五个变体且各自要持驱动会话）。任务成员**声明在最后**并在
执行体析构函数里显式等待，因为成员按声明逆序销毁，而基类的 `~ITask` 等待发生时派生部分已经没了。

---

### A76 — 取消语义分两级：父消费，子在入口清

**决策**：`LinguistExecutiveImpl`（父）用 `exchange` **消费**取消位，故开始前落下的取消算数
（A73）；六个推理执行体（子）在每轮入口**清掉**自己的取消位。

**为什么不统一**：差别在谁把门。会话租下父执行体、只在租约期内 `stop()` 它，所以「消费」是对的。
而子执行体的 `stop()` 是父**广播**下来的，可能比父自己那一轮活得更久——父因预置取消提前返回
时子根本没跑，却留着一个陈旧的取消位。若子也用消费语义，那个位会毒掉下一轮。

**这不是推演出来的**：把消费语义一并套到子执行体上之后，`test_LuaVariants` 里一条早就写好的
用例当场变红——它钉的正是「停一个没在跑的执行体不该取消任何东西」。那条用例是对的。

---

### A77 — 声明根的未知键改为警告，并给 wolf 一个日志类目

**决策**：`LinguistCategory` 不再因声明根里的未知键拒绝加载，改为经 `wolf::logCategory()`
（新增，`include/wolf/Support/Logging.h`）记一条警告。`exports`、`configuration` 与
`imports[].options` 仍然严格——那三处的 schema 确实归 wolf。

**理由**：模块声明的根是**上位规范定义的对象**，而规范对这类对象写明「框架只验证其认识的字段；
未知字段不得导致拒绝」。一个类目在这里拒绝，等于让框架**日后新增的任何公共字段**在早于它的
wolf 构建上加载失败——这是 wolf 没有资格给自己上游施加的前向兼容破坏。

**没有损失**：拼错一个**必填**字段仍然失败，因为它本该是的那个字段随即缺失；只有拼错可选字段
从错误降为警告，而那正是规范要的那笔交换。

---

### A78 — 一 unit 多会话，限制取消

**决策**：删掉 A62 记的「一 unit 一会话」，代之以一条用例。

**为什么**：那条限制既没有强制也没有检查，是两头不落好——宿主真建了第二个会话，只会在运行时
才发现。而它根本不必要：会话之间除了框架加锁的 `SynthUnit`、以及两个自己上了锁的进程级单例
（`ResourceCache`、`PinyinEngineRegistry`）之外没有共享可变状态，各自建自己的 pipeline、池与
缓存。`test_LinguistSession_TwoSessionsShareOneUnit` 钉住这一点：预热一个不会让另一个自称已热，
释放一个不会打扰另一个，两个会话并发转换结果互不串。

---

### A79 — 桩带一个不与真实变体竞争的三元组

**决策**：测试桩无条件声明 `org.openvpi.wolf.inference.G2P` / 1 / **`stub-miscount`**，此外才是
按构建条件补上的 `multig2p-onnx`（A40 的规则不变：一个三元组永远只有一个 provider）。

**为什么需要它**：短批次拒绝现在写在三层——链变体对它的后端、linguist 执行体对它的各阶段、
会话对执行体。**而四个仓里没有任何一个模块能产出短批次**，于是这三道闸门谁也没验证过。一道防
线只有在某样东西证明过它会响的时候才算数，所以由桩按声明请求（`configuration.dropWords`）故意
违反 provider ABI。

**为什么是新变体名而不是复用现有的**：桩在完整构建下不声明任何 G2P 三元组（`multig2p-onnx` 归
真解释器），所以复用会让这条用例只在最小构建里跑。`stub-miscount` 没有任何真实实现认领，两种
构建下都可用，也永远不会与谁竞争同一个三元组。

**测试面的同批整理**：删掉 `test_LinguistContrib_Reference`——它断言的是
`srt::ContribLocator::fromString` 对任意字符串的行为，属于 synthrt 而不是 wolf；把两条读起来
像重复的 `pipe-chain` 用例改名点清分工，它们其实是 A44 的两条不同路径（一条链的兜底步给不出
东西，另一条链根本没有兜底步）；并修掉 `test_LinguistSession_RefreshesUnderAConversion` 的时序
不稳——它原本可能在工作线程被调度到之前就跑完全部 200 次 `refresh()`，那样既可能误报失败，也
可能什么都没测到却报通过。现在先等到第一次转换完成再开始 refresh，并要求转换次数**大于一**。

**这条是逐提交回放查出来的**：把本轮每个提交依次检出并在 `-j8` 下跑完整套件，那条用例红了一次。
逐提交验证的价值不只是「每个提交能编译」。

---

### A80 — ORT 由 `onnxruntime-builds` 端口供给，synthrt 不再部署运行库

**决策**：synthrt 只消费 ORT 的**头**（经该包的 imported target），不部署 ORT 的**运行库**；
wolf 端口手工铺头文件的那段随之删除。落在上游分支 `onnxruntime-builds-uptake`
（叉自 `main@3c7549d`），wolf 的 `synthrt-main` 端口钉在该分支的提交上。

**要断的那根线**：`scripts/setup-onnxruntime.cmake` 里写着 `_version_ort "1.17.3"` 与整套
SHA512，而生态里实际装的端口是 1.24.4——**换 ORT 版本要改 synthrt 仓**。删掉它、删掉
`third-party/`、把两处 CMake 接到 `find_package(onnxruntime-builds)` 之后，**两个仓里 ORT 版本
号 0 处**，唯一来源是端口的 `versions.cmake`。

**为什么可以不部署**：`DriverInitArgs::runtimePath` 是**宿主传入的目录**，驱动自己从不去
`runtimes/onnx/` 找（`OnnxDriver.cpp:68`，空路径退回系统加载器）。那份构建期拷贝的**唯一真实
消费者是 synthrt 自己的驱动测试**，它把刚被给出的路径又拼了一遍
（`test_InferenceDriverFactory.cpp:224`）。让测试改问端口要目录，拷贝就没有任何消费者了——
`runtimes/onnx/default` 这个字符串同时从插件 CMake 与测试 C++ 中消失。

**归属由此清楚**：部署归拥有应用的那一方——嵌入时是编辑器（那里 ORT 与 synthrt 是两个独立
port），独立构建时是测试自己。库与插件两边都不管。

**同批修掉两处既有缺陷**，因为本改动做出了树本身守不住的承诺：`SYNTHRT_BUILD_DSINFER` 默认为
ON，而 CLI 无条件 `add_dependencies(... onnxdriver)`、驱动测试无条件声明并 include
`OnnxTensor.h`——于是「插件不会被建」这句警告后面紧跟一个 generate 错误，开测试则编译失败。
两处均改为以插件目标存在为条件。**不是顺手扩大范围**：不修的话新写的降级路径当场就是假的。

**不做的**：不改端口（归 lite 管）；不补 refactor 的 `onnxdriver-payload.cmake`（它声明的是
synthrt 部署的载荷，载荷没了就无对象可声明）；不给 `find_package` 加版本下限（那等于把版本号
写回 synthrt）；不透出 CUDA flavor。

**验证**：synthrt 三种配置（缺端口+默认、缺端口+dsinfer+tests、有端口+dsinfer+tests）分别为
配置构建通过 / 15 例通过 / 16 例通过，且驱动测试打印 `Initialized ONNX Runtime 1.24.4`；wolf
两种构建 17 / 15 全绿。新增 `test_MultiG2P_RunsOnARuntimeTheHostDeployed`：把 ORT 部署到宿主
自选目录（`/tmp` 下，既非端口目录也非插件旁）再跑通全链，并断言驱动**确实**从那里加载——把
期望值换成端口目录时该例立刻变红，故非空断言。

---

---

## 现行未决项

| # | 项 | 状态 |
| :-- | :-- | :-- |
| Q1 | 未被 `languages` 引用的 linguist import：现定为编辑期 lint 警告、不判加载失败 | 已定，可复议 |
| Q7 | `srt::ITask::startAsync` 的 `finish()` 在**释放互斥量之后**才 `notify_all()`，等待方因此可能在通知进行中析构条件变量。TSan 在 `test_LinguistRuntime` 报出一处，但整条栈都在未插桩的 `libsynthrt.so` 内，与既有的 `libonnxruntime.so` 那处同类；按 `shared_ptr` 引用计数推演存在合法的 happens-before 边，故**倾向于误报**。要定论需要一个插桩过的 synthrt 构建 | 上游，待定论 |
| Q2 | `stop()` 在词边界响应的时延上界 | **已回写**：实测约 50 µs（最坏情形，运行时文档 §4.2）。仍不写入规范——上界是钩子的指令预算而非计时器 |
| Q3 | `exports` 与 `imports[].options` 的 JSON Schema 发布物（spec 2.4:596-602 要求） | **已产出**：`docs/schemas/`，四个契约各两份，另两份共享形状。由 `scripts/check-declarations.py` 对真实包执行 |
| Q4 | wolf 打包期 lint 工具是否单独立项 | **已定**：不单独立项，`scripts/check-declarations.py` 承担，并由 `make-lang-release.py` 在打包前调用。schema 一致性 + §12 中只看声明的两项（未引用 import、贡献 ID 惯例）已接入；另两项需外部音素表，归编辑器运行期 |
| Q6 | A11 的上游合并时点 | **已落地**于 synthrt 分支 `onnxruntime-builds-uptake`（A26 补记），端口钉在该分支；并入 synthrt main 待上游 |
| Q5 | DiffSinger 歌手 `configuration` 仍强制要求 `dict` 路径（`dsinfer/plugins/singerproviders/diffsinger/DiffSingerProvider.cpp:171-185`），旧 G2P 栈遗留 | 语言域迁移完成后应退役，不在本轮范围 |

---

## D 系列：历史台账处置

原 D1-D30 出自变体草案的台账节。**其实质内容已全部并入新文档**（下表末列给出落点），五份
草案已删除，论证过程经 git 历史回溯。当前判定：

| 原编号 | 处置 | 落点 |
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
| D11 | **推翻**。原判「基数上界未执行」不成立：role 在模块内唯一由框架强制（spec 2.4:634；`PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport`），三个固定 role 的上界结构性成立，不是缺口，不需要收敛，也不产生「就地加严须在发布注记列明」的义务 | — |
| D12 | 作废——`ds-dict` G2P 化路线随 A2 取消 | — |
| D13 | 已并入 | 变体 §3.3 的「已知实现缺陷」注、Status B2 |
| D14 | 被 L4 诊断通道取代 | `linguist-runtime.md` §6 |
| D19-D23 | 已并入 | 链推理契约 §2.1 / §4.1 / §5.1 的可省略条款 |
| D20 | 作废——`lstm-onnx` 备案，A2 之后归 `pipe-chain` 变体内部事务，不再是待收录契约 | — |
| D26-D28、D-P1~P8 | D-P1 被 A19 取代（独立资源仓 → wolf 仓 Release）；其余已并入 | `linguist-distribution.md` |
| D-R1~D-R6 | D-R2 被 A14 部分取代（单一任务门保留，多任务形状作废）；其余已并入 | `linguist-runtime.md` §8 |

## 复核轮次记录处置

旧变体草案的九轮攻防复核记录（对照面快照、防线记功、下轮攻击者须知）**不迁移**，经 git
历史回溯。其中确认有效的事实断言已并入 A 系列的依据栏。

一处需要更正的复核结论：多轮复核声明的「synthrt origin/main 观察点 `a060af0` 即 HEAD、
未漂移」已失效——main 现为 `3c7549d`（*Update registry usage*，2026-09-04）。该提交改动了
`ContribCategory.h` 第 158 行下方的注册表导出宏；已复核全部被引锚点在 `3c7549d` 上仍然命中。
