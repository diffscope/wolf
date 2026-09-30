# wolf 语言域架构（索引）

本文是 wolf 语言域设计的**总架构与索引**：规定分层、决策与跨层不变量；各层的完整规则、
词汇表与错误语义在分层文档中展开。

## 本文地位与可信源顺序

冲突时按下列顺序裁决，**上位者恒胜**：

1. **[DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)**——上位规范；
2. **synthrt / dsinfer 实际代码**——框架能力的唯一事实来源。规范未描述而代码提供的能力，
   使用前必须在本文显式登记（见《依赖的框架超集》）；
3. **本文**——分层与跨层决策；
4. **分层文档**——层内规则全集。

原 `linguist-*-draft.md` 五份草案已在迁移完成后删除，其全部实质内容已并入本文档集（去向见
《文档地图》的迁移映射），论证过程可经 git 历史查阅。**这五份草案不再是有效的引用目标。**

## 锚点约定

| 树 | 基准 | 引用写法 |
| :-- | :-- | :-- |
| spec 2.4 | 本仓 `docs/ds-spec-2.4.md` | `spec 2.4:<行号>` |
| synthrt / dsinfer | synthrt 的 main 分支 | `synthrt/lib/Core/PackageLoader.cpp:38-56` |
| wolf | wolf 的 `linguistic-level-1-v2` 分支 | `src/lib/Linguist/LinguistContrib.cpp:94-95` |
| stdcorelib | vcpkg 端口，随版本变化 | 只记录文件与结论，不作固定锚点 |

synthrt 锚点一律带树内前缀（`synthrt/` 或 `dsinfer/`），两棵树位于同一仓库。代码行号为撰写时的
观察值，随分支演进可能漂移，以文件与符号为准。

---

## 1. 框架硬约束

下列事实**由代码确认**，不属于设计选择；改变这些事实需要修改框架。

### 1.1 可依赖的不变量

| # | 事实 | 锚点 |
| :-- | :-- | :-- |
| F1 | `inference` 是 synthrt 内置类别，插件 IID `org.openvpi.synthrt.plugin.InferenceInterpreter`，解释器基类 `srt::InferenceInterpreter` | `synthrt/lib/SVS/InferenceContrib.cpp:111,147`、`InferenceInterpreterPlugin.h:11` |
| F2 | `role` 在模块内唯一由框架强制 | spec 2.4:634；`synthrt/lib/Core/PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport` |
| F3 | `ContribExecutive::createChild(role, runtimeOptions)` 在**自身 spec 的 imports** 上按 role 取执行工厂并 adopt；`adoptChild` 不限制同 role 多子 | `synthrt/lib/Core/ContribExecutive.cpp:71-129` |
| F4 | 类别的 `createSpec` 在 **Probe** 执行，早于 provider 选择；`ContribCreateContext` 已可见 `imports()` | `PackageLoader.cpp:1371-1377`、`ContribCategory.h:60` |
| F5 | `vars` 由框架展开后**从声明对象中剥离**，类别与解释器都看不到它 | `PackageLoader.cpp:406` |
| F6 | ImportBinding 只从 `imports` 数组产生；数组之外的 ModuleReference 不产生任何绑定 | `PackageLoader.cpp:894-917` |
| F7 | `JsonObject = std::map<std::string, Value, std::less<>>`——成员按键排序，**声明顺序丢失** | stdcorelib `support/json.h:68` |
| F8 | `ContribImportValidator` 由**已加载的全部解释器**平铺跑遍事务内每个 spec | `PackageLoader.cpp:924-944` |

### 1.2 强加的设计约束

| # | 约束 | 后果 |
| :-- | :-- | :-- |
| C1 | `RuntimeOptions` 三元组必须等于**目标 spec** 三元组 | `G2PRuntimeOptions` 必须由构造参数接收目标变体；不得沿用 dsinfer 单变体契约的硬编码写法（`synthrt/lib/SVS/InferenceContrib.cpp:25-30`） |
| C2 | `srt::InferenceExecutive` 是单任务面（`state()/stop()/waitForFinished()`，`quit()/wait()` private-final） | 并发通过创建多个执行体实现，而非在一个执行体上运行多个任务；**资源缓存因此是架构必需件而非优化**（`InferenceExecutive.h:39-50`） |
| C3 | validator 仅来自**已加载**解释器 | 必须始终成立的规则不能只放在 Ready-2；依赖闭包内无 linguist 贡献时，wolf 的校验不会执行（`ContribPluginFactory.cpp:128-131`） |

### 1.3 依赖的框架超集

下列能力**框架提供而 spec 2.4 未描述**，本架构显式依赖它们：

- **跨类别 imports 校验**：spec 2.4:426 只授权「importing module 的 provider」校验 imports 集合，
  而框架的 `ContribImportValidator` 允许任何已加载解释器校验任何 spec（F8）。wolf 据此校验
  歌手声明。spec 未禁止额外校验（:444 授权 imports 集合校验失败即整次失败），故不构成违规，
  但受 C3 的盲区约束。

---

## 2. 层栈

```
L6  会话层          就绪态 / 预热 / 负缓存 / 执行体池 / 保留词      wolf 独占
────────────────────────────────────────────────────────────────
L5  宿主接入层      解析次序 / 缺省语言 / 缺依赖呈现 / 诊断        宿主可自定
────────────────────────────────────────────────────────────────
L4  运行时装配层    extension 挂载 / 执行体树 / 二元组注入 / 停等   wolf 独占
────────────────────────────────────────────────────────────────
L3  链推理契约层    G2P / S2P / Onset 的 IO 词汇与 exports         变体可换实现
────────────────────────────────────────────────────────────────
L2  语言组合契约层  role 基数 / 二元组命中 / phonemes / 歌手侧映射  变体可换实现
────────────────────────────────────────────────────────────────
L1  语言域身份层    linguist 类别 / (language, scheme) / ID 语法    wolf 独占
────────────────────────────────────────────────────────────────
L0  框架不变量      role 唯一 / 三元组 / 事务四相 / 监督树 / vars    synthrt，只可依赖
```

> **L6 位于 L5 之上**：L6 是宿主**持有**的对象，不是宿主**实现**的层。L5 仍是宿主自定的策略面
> （解析次序、呈现）；每个宿主都需要实现且只有一个正确答案的部分由 L6 收归语言域内部。详见
> [linguist-session.md](linguist-session.md)。

以下两个正交平面不属于主栈，各自独立演进：

- **变体面**：`configuration` 键汇与资源格式，含 `formatVersion` 自验；
- **发布面**：包身份、`[compatVersion, version]` 区间、分发渠道与更新纪律。

### 分层边界

| 层 | 拥有 | **不得**触碰 |
| :-- | :-- | :-- |
| L1 | 类别注册、条目 schema、`language`/`scheme` 形状、ID 语法、DataOnly 身份面 | `exports.phonemes` 语义、任何 `configuration`、任何推理契约 |
| L2 | role 存在性与目标契约、二元组命中、`exports.phonemes`、`configuration` 空、歌手侧语言映射语义 | 基数上界（归 L0）、任务并发形状（归 L4） |
| L3 | 逐单元 IO 词汇、`error` 值域、共现约束、`exports` 对齐面 | 任务并发形状（归 L4）、`configuration`（归变体面） |
| L4 | extension 挂载、执行体树、运行时选项注入、停等聚合 | 任何加载期失败事由（只产诊断） |
| L5 | 解析次序、策略、呈现 | 任何规范性要求（全层为建议级） |
| L6 | 歌手→语言快照、就绪态与负缓存、执行体池、保留词与空词处理、取消句柄 | `SynthUnit` 的所有权、项目模型（连音 / 切分 / 时值）、第二套转换数据模型、逐词结果记忆化 |

---

## 3. 决策台账

编号与论证过程见 [linguist-decisions.md](linguist-decisions.md)。现行结论：

| # | 决策 |
| :-- | :-- |
| A1 | G2P/S2P/Onset 沿用 synthrt 内置 `inference` 类别；wolf 只出契约头，不注册新模块类别、不新增宿主部署面 |
| A2 | Level 1 契约面为 3 份；`G2PModel` / `DictQuery` 降为 `pipe-chain` 变体的内部实现 |
| A3 | 语言身份 = linguist 声明的 `(language, scheme)` 二元组；**贡献 ID 从不被解析** |
| A4 | `id = <language>-<scheme>[-<qualifier>]` 是**书写惯例**，只在打包期 lint 比对，不作加载期校验；`qualifier` 无语义、不参与任何匹配 |
| A5 | G2P 与 S2P 同形声明 `exports.languages: [{language, scheme}]`，可省略（省略则可加载 + 宿主告警） |
| A6 | 文档按层一份，决策台账外置 |
| A7 | 二元组作为**类别追加字段**写在 linguist 声明根；`linguist` 声明白名单增加 2 项 |
| A8 | ~~缺省语言 = imports 声明序第一条~~ — 作废，由 A11 取代 |
| A9 | `configuration` 现行拒绝 Null 的行为保留；规范文本写「必须显式写 `{}`」 |
| A10 | `vars` 无需任何处理（F5）；旧文档中相反的注记为事实错误，删除 |
| A11 | 歌手侧新增 `languages`（ISO 句柄 → import role）与 `defaultLanguage` 两个**类别追加字段**，由 `SingerCategory` 解析；歌手 role 恢复自由命名，`linguist/` 前缀降为书写建议 |
| A12 | 唯一映射约束（每种语言至多一种注音体系）由 `languages` 的**映射键唯一性**在结构上保证（F7），wolf 不另行校验 |
| A13 | `defaultLanguage` 无加载期与运行时语义，纯宿主策略输入；语言域自身从不读它 |
| A14 | 并发通过**创建多个执行体**实现，而非在一个执行体上运行多个任务；一条链对应一棵子树与一个在飞任务（C2 的直接推论）。资源缓存因此成为架构必需件 |
| A15 | G2P/S2P/Onset 的 `RuntimeOptions` 必须由构造参数接收目标变体，不得硬编码（C1） |
| A16 | 运行时 IO **不携带语言参数**；执行体创建时经 `RuntimeOptions` 绑定单一二元组，生命周期内不变 |
| A17 | 运行时载荷按「非法状态不可表示」与「变化频率」两条判据精简：锁定层用 `optional` 而非布尔配对；链截断用单值枚举 `depth` 而非若干开关；执行体内恒定的诊断不逐词复制；无第二份初始化参数即不设 `initialize` |
| A18 | 发布物采用「解包即目录」形态，而非 `.dspk` 单文件，因为 synthrt main 的加载器只接受目录（§4《两处已知张力》第 2 条） |
| A19 | 发布载体为 **wolf 仓库的 Release**；16 个套件拆分为 12 个语言包、1 个共享后端包与 1 个直通包（A25） |
| A20 | 共享模型后端声明 `G2P` 契约（变体 `multig2p-onnx`），由 `pipe-chain` 的私有 role 使用。共享的大模型必须是独立包中的模块，而模块必须有契约；这是 A2 的佐证 |
| A21 | wolf 仓库有两个端口：`synthrt-main` 与 `wolf-lang-packages`。共享 overlay 的 `synthrt` 端口固定 refactor 线且由 lite 使用，因此两条线的端口并存 |
| A22 | `pipe-chain` 与 `algo-pinyin` 共用同一套打标条目形状 `{type, value, mode}`（迁移后共用 `Inferutil::Verifier`）；`enabled` 只保留步项级；`cleaner.operations` 扁平化为 `operations`；chain 的 steps 内联于 `configuration`，不引外部 chain 文件 |
| A23 | `algo-pinyin` 的引擎由绑定的 `(language, scheme)` 推导，**取消 `configuration.scheme` 键**：该键与语言身份的 `scheme` 同名而含义不同，且其信息可从绑定推导（A17 判据一） |
| A24 | `multig2p-onnx` 不设 `bundle` 路径键（bundle 与声明同目录）；`languageMap` 必选且与 `exports.languages` 对账；无 `default_language`（A16 之后不存在缺省） |
| A25 | Num / Punc / Unknown 合并为直通语言包 `wolf/lang-zxx`（`language: zxx`、`scheme: passthrough`）。三者均为无资源的纯直通处理，`zxx` 是 ISO 639-3 中表示「无语言内容」的正式代码。该包无依赖，同时作为发布链的引导包 |
| A26 | A11 落地前的临时测试路径：先实现 linguist 侧；桩 singer provider 经 `configuration` 过渡承载映射（读取集中于单一函数）；`synthrt-main` 端口携带补丁作为落地路径。A11 现已在 synthrt 分支 `onnxruntime-builds-uptake` 落地，端口固定该分支，不再携带补丁 |
| A27 | `scheme` 由 `language` 定域（匹配键恒为二元组）；取值逐语言定案——`eng` 取 `arpabet`（`cmu` 命名的是词典来源而非记法），八种非标准记法语言取 `ds`。**A49 已把其中 `por` / `kor` / `ita` 三种按 A45 定名，余五种仍为占位** |
| A28 | 语言包转换的中间产物与成品**不进入 git**：脚本、零资源的创作源与 `assets.cmake`（几十行文本）入库，转换输出写入 gitignored 暂存目录，成品发布到 release。脚本须固定 refactor 分支的源提交（`convert-g2p-packages.py` 中的 `SOURCE_REF`）以保证可溯源 |
| A29 | wolf 自带 `scripts/vcpkg-ports/` overlay，承载 `synthrt-main` 与 `wolf-lang-packages`；`overlay-ports` 为有序的两项（本仓在前、共享子模块在后）。**作用范围：只放 wolf 自身使用的端口**，lite 所需端口由 lite 仓库自行提供。不复用 `synthrt` 端口名，以免静默遮蔽共享 overlay 中的同名端口 |
| A30 | `algo-pinyin` 由语言包内的模块改为**共享后端包 `wolf/g2p-pinyin`**，cmn / yue 改为其上的 `pipe-chain`。引擎经进程全局状态解析词典根，且只在构造期读取，存在两个词典根时两个引擎均无法正确工作；而两份存量词典与 cpp-pinyin 自带的 `res/dict` 逐字节相同，属于引擎载荷而非语言内容。这是 A20 的第二个佐证 |
| A31 | B1 的解决不止于拆包：provider 域另持有**进程级词典根仲裁器**，以同一把锁保护 `setDictionaryPath` 与引擎构造，在 Acquire 期登记；出现不同的根时加载失败，并在诊断中给出两个根。静默失效由此变为确定性的加载期失败。（锁的范围后由 W8.5 收窄，保留的生命周期见 A72） |
| A32 | `algo-pinyin` 的引擎实例**每个后端执行体一份**，且**不进入 `ResourceCache`**：上游的转换方法声明为 `const`，却修改实例的暂存状态（B1-b）；缓存只接受只读解析产物，放入可变对象会破坏该不变式 |
| A33 | `pipe-chain` 的 `model` 步按**原词序中的极大连续段**分批送入后端，而不是把过滤后的词合并为一个序列；`batchSize` 缺省时不再切分。相邻关系是语义的一部分（`algo-pinyin` 的短语表） |
| A34 | 格式不合规的存量词典由**转换管线归一化**，读入端保持严格（只接受制表符分隔，不合规即加载失败）。据此修复 `fil_dict.txt` 的空格分列（旧栈未能读取其中任何一条）与 `;;;` 引文行 |
| A35 | `synthrt-main` 端口以可选 feature `onnx` 承载 dsinfer 与 ONNX 驱动。当时 main 线仍采用 `third-party/onnxruntime` 布局，因此**由端口放置头文件**而不修改上游（C1）；驱动在运行期 `dlopen` ORT，构建期只需头文件；CUDA 关闭，Windows 使用 DirectML，其余平台使用 CPU。头文件的放置已由 A80 取消 |
| A36 | `multig2p-onnx` 的驱动由**宿主**创建、初始化并注册为 Runtime Service，模块只按后端名查找。由此划分：驱动缺席是**安装环境**的属性，逐词报告 `DriverUnavailable` 并由链上的 `fallback` 兜底；模型无法打开是**包**的属性，判为加载失败 |
| A37 | `multig2p-onnx` Level 1 **只实现贪心解码**；`beamSize` / `topK` > 1 与非 0 的 `lengthPenalty` 一律判为加载失败，诊断指明不支持，不静默降级 |
| A38 | bundle 的保留符号（`<unk>` / `<pad>` / `<bos>` / `<eos>`）**按名称**在词表中查找，不使用导出惯例中的下标 |
| A39 | `lua` 变体的沙箱移除全部对外通道（包括旧栈遗漏的 `load` / `loadstring`）；`utf8` 库按 Lua 5.3 的完整接口补齐。脚本在 Acquire 期编译并校验入口，`lua_State` 每个执行体一份，不进入缓存（同 A32） |
| A40 | 带外部后端的变体按依赖是否存在**条件构建**；测试桩只声明本次构建未承载的三元组，使每个三元组始终只有一个 provider（例外为 A79 的 `stub-miscount`，该三元组只由桩声明） |
| A41 | 推理执行体一律实现协作式取消：`state()` 取 `Canceled`，返回已完成的部分，`stop()` 在无执行时为空操作。脚本变体另装计数钩子，因为脚本自身可能陷入无限循环，词边界检查对其无效 |
| A42 | `lua` 沙箱**关闭 JIT 引擎**并移除 `jit` 全局变量：实测 LuaJIT 的 count hook 在编译出的 trace 内不触发，且 `luaL_openlibs` 会重新启用编译器。旧栈安装了同一钩子却未关闭 JIT，其取消机制对唯一的目标场景无效 |
| A43 | 链与 `algo-pinyin` 保证转换词的 `pronunciation` 等于 `candidates` 的首项：引擎的候选取自逐字表，而读音可能取自短语表，两者在短语决定读音的位置不一致 |
| A44 | 链在**无 `fallback` 步**时，对没有任何步骤产出的转换词报告 `PhonemeGenerationFailed`：空发音且 error 为空会同时违反首项约束与「成功」的定义 |
| A45 | `scheme` 命名规则为 `<base>(-<qualifier>)*`：`<base>` 依次取标准记法名、社区通用名、描述该音素集本身的新造名，**禁用来源、生态、版本、「默认」等不携带信息的词**；`<qualifier>` 仅表示同族派生，并列的音素集各取 `<base>`。据此 `eng/plus` → `arpabet-plus`，而 `deu/marzipan` → `marzipan` 而非 `ds-marzipan`（发布文档 §2.1.1） |
| A46 | 共享 bundle 的**全部 12 个语言引用都进入 `languageMap`**，因为未被映射的音素集无法被任何声库使用。取 `ds` 的八种按 A45 属于待命名（P7），`ds` 为占位 |
| A47 | `pipe-chain` **禁止 `verify` 步出现在产出步之后**：分类决定产出步可以处理哪些词，因此必须前置；后置的 copy 标记会静默丢弃词典命中。该配置在加载期被拒绝，不为其定义语义 |
| A48 | `dict` 步与 `model` 步的音素集不必一致（实测 `eng` 44% / `ita` 75% / `fil` 100% 的词典行携带模型不产出的记号），链的符号集是各步的并集**加上兜底产出**。由于 `exports.symbols` 必须为全集，**该键与 `useOriginal` 原词兜底互斥**；12 条链全部使用原词兜底，因此当前均不能声明该键。要实现静态比对，须先放弃原词兜底（发布文档 §2.2） |
| A49 | P7 分批命名：`por` → `xsampa`、`kor` → `romaja`、`ita` → `xsampa-geminate` 按 A45 第 1 档落地；`deu` / `fra` / `spa` / `rus` / `fil` 既无标准记法也无通用名，保留 `ds` 占位待命名。只修改有确凿证据的项 |
| A50 | 语言包保留「查不到即输出原词」的行为，**接受 `exports.symbols` 无法声明、宿主无法在加载期比对音素**。这是旧栈的既有行为，改动会改变十二个包的实际输出；契约允许省略该键并由宿主告警，因此记为已知取舍而非缺陷 |
| A51 | `LinguistExecutive::stop()` **向下传递**到已创建的 G2P / S2P / Onset 子执行体。每个阶段对整批只调用一次，只作用于本层的停止请求须等待该调用自行返回；子执行体指针因此为原子变量，`stop()` 可以与转换并发到达 |
| A52 | 契约的 JSON Schema 放在 `docs/schemas/`（spec 2.4:596-602 的必需项），并由 `scripts/check-declarations.py` 对真实包执行，以保证发布物与加载器一致。据此收紧了加载器：`exports.languages` 的二元组按身份字段的同一文法校验，并拒绝未知键 |
| A53 | 转换出的包版本的第四位是**打包修订号**（前三位表示源资源的版本）。管线对同一输入产出不同结果时必须提升该位；否则两个内容不同的包使用同一版本，按目标版本求解的消费方得到哪一个取决于下载的是哪一个 |
| A54 | **后端包的 `compatVersion` 取修订号 0，依赖方指向该下沿**；~~语言包无人依赖，`compatVersion` 等于 `version`~~（**语言包这一分支已由 A68 推翻**）。修订之间变化的是打包，而不是被绑定的模块 ref 与契约。实证：依赖写成后端当前版本的字面值后，第一次提升修订号即导致九个语言包同时解析失败 |
| A55 | B3 分批：`eng` / `por` / `kor` / `ita` 四种已构成闭包（`scheme` 已定，G2P 产出空格分隔的音素，因此 S2P 使用 `direct`，Onset 省略）。`cmn` / `yue` / `jpn` 产出音节，其音节到音素的词典属于歌手包（A66）；其余五种等待 P7。闭包的 `exports.phonemes` 取自该语言自身的词典 |
| A56 | 层栈增加 **L6 会话层**（`wolf::LinguistSession`）：歌手到语言的快照、就绪态与负缓存、执行体池、保留词与空词处理、取消句柄。依据是 lite 已在 refactor 线上自行实现过同样的功能；这些功能不是宿主的业务判断，而是只有一个正确答案的语言域实现细节（`linguist-session.md`） |
| A57 | 会话**借用而不拥有 `SynthUnit`**，歌手用 `srt::ContribLocator` 标识，转换 IO 沿用 L4 的 `LinguistConvertInput/Result`；会话是生命周期层，不是第二套数据模型。`depth` 截断与逐词锁定已把 lite 的三个 API 合并为一个 |
| A58 | 就绪三态 `Ready / Cold / Unavailable`：`probe()` 无副作用，`warm()` 主动预热，**成功与失败同样缓存**，唯一的失效点是 `refresh()`。不设后台重试与超时，因为重新检查的时机由宿主决定 |
| A59 | 执行体池按 (歌手, 语言) 分键，**不设默认上限**：并发度是宿主线程池的属性。其代价有限：ONNX 驱动已按 `path` 与 `(size, hash)` 以引用计数共享 `SessionImage`，同一模型的权重只加载一份；随执行体重复的只有 cpp-pinyin 词表与 `lua_State`，两者因上游的可变状态而无法共享 |
| A60 | `exports` 增加开放位 `openSet`（缺省 `false`），与清单一起表示产出是否可能超出清单。原字段为必填，却只能作为建议值使用，增加开放位后成为可用的字段；`openSet` 为假时语义与原先完全一致，既有声明无需修改；取值由打包 lint 自动推导 |
| A61 | 保留词（缺省 `SP` / `AP`）由会话处理，产出 `mode=copy` 与单个保留音素；**空词与含空白词的判定属于 G2P 模块**（链推理契约 §3.4.2 已规定），提取共用组件 `classifyLyric()` 实现一次，三个变体均已使用。项目专属标记（连音、`+` 后缀）仍由宿主处理 |
| A62 | L6 设计经自检修正六处、删除三处：池须持有 `PackageHandle`（执行体必须先于其 Package 销毁）、`refresh()` 按代替换而不清空在飞条目、子执行体 `delete` 即脱离、`Cold` 不表示可用、保留词拦截须按 `depth` 补齐形状、~~一个 unit 一个会话~~（**最后一条已由 A78 推翻**）。删除 `LanguageStatus::binding`、`refresh()` 的 `Expected` 与池观测 API |
| A63 | L6 落地时的三处修正：① 歌手键为 `SingerRef { locator, version }`，因为 `ContribLocator` 不含版本，而同一声库的两个版本并存是常见情形，版本留空时存在歧义并被拒绝；② 绑定由 `WolfPipelineExtension::binding()` 直接提供，因为 `locate()` 返回的 locator 属于语言包，无法从歌手包解析；③ **创建执行体的操作移出会话锁**（需要加载词典与打开模型，在锁内执行会阻塞每帧的 `probe()`），先占用租约再释放锁。并发行为经 TSan 验证（`linguist-session.md` §12.1） |
| A64 | C1 / C2 落地。C1 的开放位 `openSet` 只加在**声明为全集**的三处（域契约 `phonemes`、G2P `symbols`、S2P `phonemes`），缺省 `false`，因此既有声明的语义不变；Onset 的 `knownPhonemes` 在契约中已规定为下界，增加开放位属于重复声明，**不增加**。取值由 `check-declarations.py` 从链的 `useOriginal` 兜底步推导，不依赖作者手工填写。C2 写入域契约 §5.0，理由是：`onsets` 与 `phonemes` 等长已确定长度，只剩取值一个自由度，交由宿主决定会使两个宿主对同一语言包给出不同的切分 |
| A65 | 最小构建必须能够**显式选出**（`WOLF_DISABLE_DSINFER` / `WOLF_DISABLE_LUAJIT`）。此前只能把 `find_` 缓存项强制设为 NOTFOUND，而 `find_path` 在下一次 configure 时会重新找到该依赖，最小构建因此静默变回完整构建，A40 所述「两种构建均已验证」的结论随之失效且未被发现。同批修复了实际的失效：夹具 `lang-chain` 中混入了 `lua` 变体模块，无 LuaJIT 时整个包加载失败，导致 `test_PipeChain` 的全部用例失败；该模块已拆为独立的 `lang-runaway` / `singer-runaway`，使用它的用例按构建条件跳过 |
| A66 | **B3 排除**：`cmn` / `yue` / `jpn` 的音节到音素词典是**歌手包内容**，不是语言包内容。spec 2.3 的示例把它写在歌手清单的 `languages[].dict` 中，按歌手配置目录解析，校验器也按歌手侧路径校验。语言包不应提供该词典，缺少它不构成缺口；组合路径（语言包提供 G2P，歌手包提供 S2P 与 onset）已实现，并由 `test_HostFlow` 覆盖。结论与 B2 相同：该阻塞项源于对旧栈形状的误读 |
| A67 | **端口首次实际安装成功**。此前端口从未安装成功：`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 中是硬错误，端口一经使用即失败，而此前的测试全部经 `WOLF_LANG_PACKAGES_SOURCE` 绕过端口，该错误未被触发。现改为直接写入 `copyright`，并收录各包自带的 License.txt，因为纯数据端口需要安装的许可文件随 feature 变化，固定的 FILE_LIST 无法表达。同批修复两处同源的不一致：端口 `vcpkg.json` 的 `version-string` 由发布脚本随 bundle 版本一起改写（此前 assets.cmake 由脚本生成，而版本号由人工维护，两者已相差一个版本，vcpkg 会把旧树当作新版本安装）；清单补上文档已引用但并不存在的 `lang-packages` feature（按需启用，不影响普通构建） |
| A68 | **语言包的 `compatVersion` 同样取修订号下沿**，A54 的语言包分支作废。依据为 spec §兼容性：该节列出的六项公开表面均未破坏时不应提升 `compatVersion`；`compatVersion == version` 是不实的兼容性声明，而规范规定「承诺不实属于 Package 缺陷」；是否有包依赖它也不是 `compatVersion` 的判定依据，A54 选错了判定维度。其事实前提同样不成立：A66 使歌手包必然依赖语言包，wolf 自己生成的夹具即写有 `wolf/lang-cmn 1.0.1.2`。实证：把 cmn 提升到 `1.0.1.3` 后，`test_HostFlow` 的六个用例全部报告「no installed Package satisfies dependency」。据此把「依赖指向 `compatVersion`」的 lint 升为**错误**，并要求回归测试使用「旧夹具 + 新包」的组合，因为夹具与包同批生成时无法发现此类失效，A54 的缺陷正是因此遗漏 |
| A69 | **不实现能力晚绑定**：绑定由 `imports[].ref` 在加载期完全确定，声库初始化完成时即确定。晚绑定可以在结构上消除版本耦合，但会把「目标不可用」推迟到 `warm()` 才暴露，该代价不可接受：安装后即可用是本层必须保持的性质。可选方案的范围由 spec 限定：2.4 明文不支持可选依赖，`imports[].ref` 必选且不设简写，`configuration` 不得承载跨模块的契约内容，类别追加字段的判据是「在解释器选出之前即需使用」，而 import 绑定发生在 Ready。**因此不实现包级降级，这是规范的结论而非取舍**；若要求缺少一种语言时整个声库仍可加载，只能由语言包自带 linguist，而不是依赖该语言包 |
| A70 | **降级只保留链级与音素级**。`linguist/s2p` 由「恰好 1 个」放宽为 `0..1`，缺席时 `maxDepth = Pronunciation`（onset 缺席时为 `Phonemes`，与原有行为一致）。放宽属于 `variant` 的权限：spec §imports 规定哪些 role 必须存在由导入方的 variant 决定，且导入方「**仍可**」严格要求；该变更属于「增加可选能力」，因此不提升 `compatVersion`。音素覆盖度由会话计算并通过 `coverage` / `missingPhonemes` 提供，**不设门限**（`fil` 100% / `ita` 75% / `eng` 44% 表明固定门限必然误判）；唯一的例外是 `coverage == 0 && !openSet`，会话直接判为 `Unavailable`：清单声明为全集而声库不支持其中任何音素，属于配置错误而非降级 |
| A71 | **初始化时确定的是绑定与形状，不包括预热**。A69 之后，绑定在加载期确定，`maxDepth` 直接从 `imports` 读取，覆盖度只是集合运算，三者都不需要打开模型；因此 `Cold` 收紧为「绑定已确定、形状已计算、仅未预热」，A58 的「不表示可用」限定为「不表示**已加载**」。预热不提前：声明 12 种语言的声库会在装包时打开 12 条链，这正是 A63 ③ 移出锁的开销。驱动缺席与模型无法打开仍只能在 `warm()` 时暴露（与 A36 的划分相同） |
| A72 | **进程级词典根的保留随配置对象存续**。`reserveRoot()` 返回 RAII 保留对象，由模块的 `Configuration` 持有，计数归零即释放该根。原先的实现是 Acquire 期一次不可撤销的全局写入，实测有两个后果：失败的加载永久占用该根（一个未装上的包导致可用的包无法加载），卸载也从不释放。配置对象本身即 spec §加载事务所要求的事务私有对象：加载失败时随事务销毁，卸载时随包销毁，abort 与 release 两条路径因此自动成立，无须另写三段式处理 |
| A73 | **`stop()` 取消当前或下一次转换**，由 `start()` 以 `exchange` 消费该位；两条退出路径都清除该位，因此池中的执行体不会继承取消状态。会话的 `enrol()` 改为返回布尔值，已取消的令牌不启动该批次。原先 `start()` 的第一行无条件清除该位，而会话恰好在此之前调用 `stop()`，因此**所有在 `convert()` 之前到达的取消都被静默丢弃**，而这正是最常见的调用顺序。不采用 `ITask` 的「只在运行时置位」：会话先租用执行体再启动转换，两步之间到达的取消必须生效 |
| A74 | **导出的链接接口只允许出现 config 会引入的目标**，由 `src/lib/CMakeLists.txt` 的配置期断言保证。`blake3` 与 `re2` 均归入 `LINKS_PRIVATE`。此前 `blake3` 位于公开 `LINKS`，而 config 只调用 `find_dependency(synthrt)`，按 README 的用法消费时在 generate 阶段失败；仓内测试直接链接构建树中的目标，不经过安装出的 config，因此无法发现该问题。CI 现另以 `.github/consumer` 检查安装包 |
| A75 | **执行体一律持有 `srt::ITask`**（提取 `wolf::ExecutiveTask<Input, Result>`），不再手写任务面。手写的十一份副本有三处相同的错误：`waitForFinished()` 直接返回，使卸载包时的等待立即结束；`startAsync` 有八处同步地就地回调，第九处直接 `detach`；没有一处拒绝第二次并发执行。第四处错误在本轮审计中发现：`chain` / `pinyin` / `multig2p` / `lua`×2 把 `quit()`/`wait()` 覆盖为空操作，**使 `srt::InferenceExecutive` 已实现的转发失效**，卸载包时这些执行体既不停止也不等待。实现一次而非复制十一份，与 A61 / A43 的做法相同 |
| A76 | **取消语义分为两级**：父执行体（`LinguistExecutiveImpl`）以 `exchange` 消费取消位，因此开始前到达的取消生效（A73）；子执行体（六个推理执行体）在每轮入口清除该位。区别在于由哪一级负责判定：子执行体的 `stop()` 由父执行体广播，其存续可能超过父执行体的当前一轮，若采用消费语义，残留的取消位会错误地取消下一轮。该结论有测试依据：把消费语义应用到子执行体时，`test_LuaVariants` 中一个既有的、行为正确的用例立即失败 |
| A77 | **声明根的未知键改为警告**（新增 `wolf::logCategory()`），`exports` / `configuration` / `options` 仍严格校验。模块声明根是上位规范定义的对象，规范规定「框架只验证其认识的字段；未知字段不得导致拒绝」；在此处拒绝未知键，会使框架日后新增的公共字段在较早的 wolf 构建上加载失败。该变更不削弱校验：拼错的必填字段仍会导致加载失败，因为对应的正确字段随之缺失 |
| A78 | **一个 unit 可以有多个会话，原限制取消**（推翻 A62 的对应条目）。原限制既未强制也未检查；而会话之间除由框架加锁的 `SynthUnit` 与两个自带锁的进程级单例外，不存在共享的可变状态。现由 `test_LinguistSession_TwoSessionsShareOneUnit` 验证：预热、释放、并发转换三方面互不干扰 |
| A79 | **桩无条件声明一个 `stub-miscount` 三元组**（另有按构建条件补充的 `multig2p-onnx`）。短批次的拒绝逻辑分布在三层，而四个仓库中没有任何模块会产出短批次，这三处检查此前均未经验证；桩按 `configuration.dropWords` 故意违反 provider ABI。使用新的变体名而不复用已有名称，因为桩在完整构建下不声明任何 G2P 三元组，复用会使用例只在最小构建中运行。同批整理测试：删除 `test_LinguistContrib_Reference`（其断言针对 synthrt 而非 wolf），并为两个看似重复的 `pipe-chain` 用例改名，以区分各自覆盖的路径（A44 的两条路径） |
| A80 | **ORT 改由 `onnxruntime-builds` 端口提供，synthrt 不再部署运行库**。上游分支 `onnxruntime-builds-uptake` 把 `onnxutil` 连接到该包的 imported target（头文件随目标传递），删除插件 CMake 中的 ORT 路径与 `runtimes/onnx/<flavor>` 复制，并删除 `scripts/setup-onnxruntime.cmake`（固定 1.17.3）与 `third-party/`；**此后两个仓库中均不出现 ORT 版本号**，版本由 overlay 单独控制。依据是驱动的 `DriverInitArgs::runtimePath` 由宿主提供，驱动不会在插件目录旁查找运行库，该副本的唯一使用者是 synthrt 自身的测试。wolf 端口中的头文件复制随之删除；新增 `test_MultiG2P_RunsOnARuntimeTheHostDeployed`，把 ORT 部署到宿主自选的目录后运行完整链路，并断言驱动从该目录加载 |

---

## 4. 符合性与改动清单

**结论：符合 spec 2.4。** synthrt 使用「个别必需字段」额度中的 2 项，其余为 wolf 侧的增补。

```
synthrt  synthrt/lib/SVS/SingerContrib.cpp:33-36   白名单 +2（languages, defaultLanguage）
         synthrt/lib/SVS/SingerContrib.cpp         createSpec 解析 + 三条结构校验
         synthrt/include/synthrt/SVS/SingerContrib.h   SingerSpec +2 成员 +2 访问器

wolf     src/lib/Linguist/LinguistContrib.cpp:27-29    白名单 +2（language, scheme）
         src/lib/Linguist/LinguistContrib.{h,cpp}      LinguistSpec +2 访问器、language/scheme 形状校验
         src/plugins/.../WolfLinguistProvider.cpp:94-152   validator 改读 languages 映射、增二元组命中
         src/plugins/.../WolfLinguistProvider.cpp:249-266  createExtensions 改挂载条件
         新文件                                        include/wolf/Api/Inferences/**（含 Common/1）
                                                       LinguistExecutive 具体实现类
                                                       wolf::ResourceCache
dsinfer  0 处
```

`WolfLinguistProvider::createExports` 为纯增量（现有逻辑不动）；`createConfiguration` 不改（A9）。

### 两处已知张力（记录在案）

1. **歌手侧校验依赖框架超集**（见 §1.3），并受 C3 盲区约束。
2. **spec 与实现对 Package 形态的规定不一致**：spec 2.4:59 定义 Package 为 `.dspk` ZIP，
   而 synthrt main 的加载器**只接受目录**，拒绝非目录路径（`PackageLoader.cpp:1059-1061`），
   搜索路径只枚举子目录（`:614`），main 也不再依赖任何解压库。发布物形态据此定为
   「解包即目录」（见 [linguist-distribution.md](linguist-distribution.md)）。

---

## 5. 声明形态总览

```json
// linguists/cmn-pinyin/linguist.json                                       ← L1 + L2
{ "interface": "org.openvpi.wolf.linguist.WolfLinguist", "level": 1, "variant": "wolf",
  "language": "cmn", "scheme": "pinyin",
  "exports": { "phonemes": "./phonemes.json" },
  "configuration": {},
  "imports": [ { "role": "linguist/g2p",   "ref": ":inference/g2p" },
               { "role": "linguist/s2p",   "ref": ":inference/s2p" },
               { "role": "linguist/onset", "ref": ":inference/onset" } ] }
```

```json
// inferences/g2p/inference.json                                            ← L3
{ "interface": "org.openvpi.wolf.inference.G2P", "level": 1, "variant": "algo-pinyin",
  "exports": { "languages": [ { "language": "cmn", "scheme": "pinyin" },
                              { "language": "yue", "scheme": "jyutping" } ],
               "symbols": "./symbols.json" },
  "configuration": { "scheme": "mandarin", "dictPath": "./dict" } }
```

```json
// singers/singer1/singer.json                                              ← L2 歌手侧
{ "interface": "org.openvpi.dsinfer.singer.DiffSinger", "level": 1, "variant": "openvpi",
  "languages": { "cmn": "lang/mandarin", "jpn": "lang/japanese" },
  "defaultLanguage": "cmn",
  "configuration": { "dict": "./dict.txt" },
  "imports": [ { "role": "singer/acoustic", "ref": ":inference/acoustic" },
               { "role": "lang/mandarin",   "ref": ":linguist/cmn-pinyin" },
               { "role": "lang/japanese",   "ref": "wolf/lang-jpn:linguist/jpn-romaji" } ] }
```

由 ISO 代码到实际 linguist 贡献 ID 的解析路径：
`"cmn"` → role `lang/mandarin` → 该 import 的 `locator().contributionId()` = `cmn-pinyin`，
全程在 DataOnly 模式下可读。

---

## 6. 校验矩阵

任何校验失败均使整次加载失败（spec 2.4:444）。框架没有告警通道，提示级检查全部由编辑期 lint 执行。

| 相 | 执行者 | 校验内容 | 层 |
| :-- | :-- | :-- | :-- |
| Probe | 框架 | `role` 在模块内唯一 ⇒ linguist 三个固定 role 的**基数上界结构性成立** | L0 |
| Probe | 框架 | `languages` 映射键唯一 ⇒ **每语言至多一个语言导入** | L0 |
| Probe | `LinguistCategory::createSpec` | 条目 schema；`language` 形如 `[a-z]{3}`；`scheme` 形状 | L1 |
| Probe | `SingerCategory::createSpec` | `languages` 形状；每个值命中本声明某条 import role；`defaultLanguage` 是其键 | L1 |
| Acquire | 组合 provider | `exports.phonemes` 形状；`configuration` 为空 object | L2 |
| Acquire | 三推理 provider | 各自 `exports`（含 `languages` 形状）与 `configuration` | L3 / 变体面 |
| Ready-1 | 被引目标 provider | 单条 import `options` | L2 / L3 |
| Ready-2 | wolf validator | linguist：`linguist/g2p` `linguist/s2p` 存在、目标 interface 相符、**自身二元组命中两者的 `exports.languages`**；singer：映射值指向的 import 目标类别为 `linguist`、**目标 `language` == 映射键** | L2 |
| Ready-3 | 组合 provider | pipeline extension 挂载 | L4 |
| 运行时 | 执行体 | 未映射的语言标签、IO 形状 | L4 |
| 打包 / 编辑期 | lint | `phonemes` ⊆ 各 stage 音素表交集；S2P 产出集 ⊆ 语言 `phonemes`；Onset 覆盖；未被映射引用的 linguist import；贡献 ID 书写惯例 | — |

---

## 7. 执行体树

```
SingerSpec ──extension──▶ WolfPipelineExecutive
                              └─ createChild(languages[tag])  ─▶ LinguistExecutive
                                     ├─ createChild("linguist/g2p")   ─▶ G2PExecutive    ┐
                                     ├─ createChild("linguist/s2p")   ─▶ S2PExecutive    ├ srt::InferenceExecutive
                                     └─ createChild("linguist/onset") ─▶ OnsetExecutive  ┘
```

- 全部经 F3 的 `createChild` 完成，无须修改框架；父子析构与 `quit`/`wait` 的传播由监督树承担；
- **一条链对应一棵子树与一个在飞任务**（C2）。宿主的 k 路并发即调用 `createLinguist` k 次；
- 跨子树的资源共享由 `wolf::ResourceCache` 承担；这是 C2 的直接推论，不是可选优化。

---

## 8. 文档地图

| 文档 | 覆盖 | 状态 |
| :-- | :-- | :-- |
| 本文 | 索引、层栈、跨层不变量、决策结论 | 现行 |
| [linguist-domain-contract.md](linguist-domain-contract.md) | **L1 + L2**：类别、身份、组合、歌手侧映射、裁决 | 现行 |
| [linguist-inference-contract.md](linguist-inference-contract.md) | **L3**：G2P / S2P / Onset 的 IO 词汇与 exports | 现行 |
| [linguist-runtime.md](linguist-runtime.md) | **L4 + L5**：执行体树、任务、诊断、降级、宿主接入 | 现行 |
| [linguist-variants.md](linguist-variants.md) | 变体面：`configuration` 键汇与资源格式 | 现行 |
| [linguist-distribution.md](linguist-distribution.md) | 发布面：包身份、版本兼容模型、更新纪律 | 现行 |
| [linguist-resource-cache.md](linguist-resource-cache.md) | 资源缓存（**架构必需件**，非优化） | 现行 |
| [plugin-internals.md](plugin-internals.md) | **插件内部**：骨架、版本与世代、解析与诊断纪律、契约身份、演进检查清单 | 现行（V1 待评审） |
| [linguist-decisions.md](linguist-decisions.md) | 决策台账（全部 A / D 编号与论证） | 现行 |
| [linguist-implementation-plan.md](linguist-implementation-plan.md) | 实施顺序、里程碑与验收判据 | 现行 |

### 旧 draft 迁移映射（存档）

五份草案已删除，本表记录其内容去向，供查阅 git 历史时定位：

| 旧文档 | 去向 |
| :-- | :-- |
| `linguist-level-1-draft.md` §前置说明 / `linguist` 类别 / WolfLinguist / Singer 侧配套 | → `linguist-domain-contract.md` |
| `linguist-level-1-draft.md` G2P / S2P / Onset 三章 | → `linguist-inference-contract.md` |
| `linguist-level-1-draft.md` G2PModel / DictQuery 两章 | → 作废（A2），素材转入 `linguist-variants.md` §pipe-chain |
| `linguist-level-1-draft.md` §公共语言包 | → `linguist-distribution.md` |
| `linguist-runtime-api-draft.md` | → `linguist-runtime.md`（§3.2 多任务形状按 C2 重写） |
| `linguist-g2p-variants-wolf-draft.md` §1-§7 | → `linguist-variants.md` |
| `linguist-g2p-variants-wolf-draft.md` §8 全部 | → `linguist-decisions.md` |
| `linguist-g2p-package-distribution-draft.md` | → `linguist-distribution.md` |
| `linguist-g2p-resource-cache-draft.md` | → `linguist-resource-cache.md` |

五份草案的最终版本见删除前的提交；本表列出的去向均已落实。
