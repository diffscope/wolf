# wolf 语言域架构（索引）

本文是 wolf 语言域设计的**总架构与索引**：钉分层、决策与跨层不变量，各层的规则全集、
词汇表与错误语义在分层文档中展开。

## 本文地位与可信源顺序

冲突时按下列顺序裁决，**上位者恒胜**：

1. **[DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)**——上位规范；
2. **synthrt / dsinfer 实际代码**——框架能力的唯一事实来源。规范未描述而代码提供的能力，
   使用前必须在本文显式登记（见《依赖的框架超集》）；
3. **本文**——分层与跨层决策；
4. **分层文档**——层内规则全集。

原 `linguist-*-draft.md` 五份草案已于迁移完成后删除——其全部实质内容已并入本文档集（落点见
《文档地图》的迁移映射），论证过程经 git 历史回溯。**它们不再是有效引用目标。**

## 锚点口径

| 树 | 观察点 | 引用写法 |
| :-- | :-- | :-- |
| spec 2.4 | 本仓 `docs/ds-spec-2.4.md`（blob `3d0c6568`） | `spec 2.4:<行号>` |
| synthrt / dsinfer | `origin/main` `3c7549d` | `synthrt/lib/Core/PackageLoader.cpp:38-56` |
| wolf | 代码锚点 `cecedba`（其后的 `fa6e81d` 仅新增文档，无代码改动） | `src/lib/Linguist/LinguistContrib.cpp:94-95` |
| stdcorelib | vcpkg 端口，随版本漂移 | 只记文件与结论，不作硬锚 |

synthrt 锚点一律带树内前缀（`synthrt/` 或 `dsinfer/`），两棵树同处一仓。

---

## 1. 框架硬约束

方案建立在下列**从代码读出**的事实上。它们不是设计选择，改动它们即需改框架。

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
| C1 | `RuntimeOptions` 三元组必须等于**目标 spec** 三元组 | `G2PRuntimeOptions` 必须由构造参数接收目标变体；不可照抄 dsinfer 单变体契约的硬编码写法（`synthrt/lib/SVS/InferenceContrib.cpp:25-30`） |
| C2 | `srt::InferenceExecutive` 是单任务面（`state()/stop()/waitForFinished()`，`quit()/wait()` private-final） | 并发 = 多开执行体，不是多开任务；**资源缓存因此是架构必需件而非优化**（`InferenceExecutive.h:39-50`） |
| C3 | validator 仅来自**已加载**解释器 | 「必须永远成立」的规则不能只放 Ready-2；依赖闭包内无 linguist 贡献时 wolf 的校验根本不跑（`ContribPluginFactory.cpp:128-131`） |

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

> **L6 在 L5 之上而非之下**：它是宿主**持有**的对象，不是宿主**实现**的层。L5 仍是宿主自定的
> 策略面（解析次序、呈现），L6 把那些「每个宿主都得写一遍且只有一个正确答案」的部分收回语言域
> 内部。详见 [linguist-session.md](linguist-session.md)。

正交两面，不进主栈、各自独立演进：

- **变体面**：`configuration` 键汇与资源格式，含 `formatVersion` 自验；
- **发布面**：包身份、`[compatVersion, version]` 区间、分发渠道与更新纪律。

### 分层红线

| 层 | 拥有 | **不得**触碰 |
| :-- | :-- | :-- |
| L1 | 类别注册、条目 schema、`language`/`scheme` 形状、ID 语法、DataOnly 身份面 | `exports.phonemes` 语义、任何 `configuration`、任何推理契约 |
| L2 | role 存在性与目标契约、二元组命中、`exports.phonemes`、`configuration` 空、歌手侧语言映射语义 | 基数上界（归 L0）、任务并发形状（归 L4） |
| L3 | 逐单元 IO 词汇、`error` 值域、共现约束、`exports` 对齐面 | 任务并发形状（归 L4）、`configuration`（归变体面） |
| L4 | extension 挂载、执行体树、运行时选项注入、停等聚合 | 任何加载期失败事由（只产诊断） |
| L5 | 解析次序、策略、呈现 | 任何规范性要求（全层为建议级） |
| L6 | 歌手→语言快照、就绪态与负缓存、执行体池、保留词与空词收口、取消把手 | `SynthUnit` 的所有权、项目模型（连音 / 切分 / 时值）、第二套转换数据模型、逐词结果记忆化 |

---

## 3. 决策台账

编号与论证过程见 [linguist-decisions.md](linguist-decisions.md)。现行结论：

| # | 决策 |
| :-- | :-- |
| A1 | G2P/S2P/Onset 沿用 synthrt 内置 `inference` 类别；wolf 只出契约头，不注册新模块类别、不新增宿主部署面 |
| A2 | Level 1 契约面 = 3 份；`G2PModel` / `DictQuery` 退为 `pipe-chain` 变体内部事务 |
| A3 | 语言身份 = linguist 声明的 `(language, scheme)` 二元组；**贡献 ID 从不被解析** |
| A4 | `id = <language>-<scheme>[-<qualifier>]` 是**书写惯例**，只在打包期 lint 比对，不作加载期校验；`qualifier` 无语义、不参与任何匹配 |
| A5 | G2P 与 S2P 同形声明 `exports.languages: [{language, scheme}]`，可省略（省略则可加载 + 宿主告警） |
| A6 | 文档按层一份，决策台账外置 |
| A7 | 二元组作**类别追加字段**写在 linguist 声明根；`linguist` 声明白名单 +2 |
| A8 | ~~缺省语言 = imports 声明序第一条~~ — 作废，由 A11 取代 |
| A9 | `configuration` 现行拒绝 Null 的行为保留；规范文本写「必须显式写 `{}`」 |
| A10 | `vars` 无需任何处理（F5）；旧文档中相反的注记为事实错误，删除 |
| A11 | 歌手侧新增 `languages`（ISO 句柄 → import role）与 `defaultLanguage` 两个**类别追加字段**，由 `SingerCategory` 解析；歌手 role 恢复自由命名，`linguist/` 前缀降为书写建议 |
| A12 | 唯一映射约束（每语言至多一种注音体系）由 `languages` **映射键唯一性**结构性承载（F7），wolf 不写该校验 |
| A13 | `defaultLanguage` 无加载期与运行时语义，纯宿主策略输入；语言域自身从不读它 |
| A14 | 并发靠**多开执行体**而非多开任务；一条链 = 一棵子树 = 一个在飞任务（C2 的直接推论）。资源缓存因此升为架构必需件 |
| A15 | G2P/S2P/Onset 的 `RuntimeOptions` 必须由构造参数接收目标变体，不得硬编码（C1） |
| A16 | 运行时 IO **不携带语言参数**；执行体创建时经 `RuntimeOptions` 绑定单一二元组，生命周期内不变 |
| A17 | 运行时载荷按「非法状态不可表示」与「变化频率」两条尺子精简：锁定层用 `optional` 而非布尔配对；链截断用单值枚举 `depth` 而非若干开关；执行体内恒定的诊断不逐词复制；无第二份初始化参数即不设 `initialize` |
| A18 | 发布物是「解包即目录」而非 `.dspk` 单文件——main 的加载器只受理目录（架构《已知张力》2） |
| A19 | 发布宿主为 **wolf 仓 Release**；16 套件切为 12 语言包 + 1 共享后端包 + 1 直通包（A25） |
| A20 | 共享模型后端声明 `G2P` 契约（变体 `multig2p-onnx`）由 `pipe-chain` 私有 role 消费——共享大模型必须成为独立包中的模块，而模块必须有契约。A2 的正面佐证 |
| A21 | wolf 仓两个端口：`synthrt-main` 与 `wolf-lang-packages`。共享 overlay 现有的 `synthrt` 端口 pin 的是 refactor 线且 lite 在用，故分线并存 |
| A22 | `pipe-chain` 与 `algo-pinyin` 共用同一套打标条目形状 `{type, value, mode}`（迁移后共用 `Inferutil::Verifier`）；`enabled` 只保留步项级；`cleaner.operations` 扁平化为 `operations`；chain 的 steps 内联于 `configuration`，不引外部 chain 文件 |
| A23 | `algo-pinyin` 的引擎由绑定的 `(language, scheme)` 推导，**取消 `configuration.scheme` 键**——它与语言身份的 `scheme` 撞名不同义，且信息可从绑定推导（A17 尺子一） |
| A24 | `multig2p-onnx` 不设 `bundle` 路径键（bundle 与声明同目录）；`languageMap` 必选且与 `exports.languages` 对账；无 `default_language`（A16 之后不存在缺省） |
| A25 | Num / Punc / Unknown 合并为直通语言包 `wolf/lang-zxx`（`language: zxx`、`scheme: passthrough`）——三者均为纯直通零资源，`zxx` 是 ISO 639-3 的「无语言内容」正式代码。零依赖，兼作发布链引导包 |
| A26 | A11 落地前的临时测试路径：先做 linguist 侧 / 桩 singer provider 经 `configuration` 过渡承载（读取封单点） / `synthrt-main` 端口带 patch 作为落地路径 |
| A27 | `scheme` 由 `language` 定域（匹配键恒为二元组）；取值逐语言定案——`eng` 取 `arpabet`（`cmu` 命名的是词典来源而非记法），八种非标准记法语言取 `ds`。**A49 已把其中 `por` / `kor` / `ita` 三种按 A45 定名，余五种仍为占位** |
| A28 | 语言包转换的中间产物与成品**不进 git**：脚本、零资源创作源与 `assets.cmake`（几十行文本）入库，转换输出落 gitignored 暂存目录，成品上 release。脚本须固定 refactor 的 commit SHA 以保溯源 |
| A29 | wolf 自带 `scripts/vcpkg-ports/` overlay，承载 `synthrt-main` 与 `wolf-lang-packages`；`overlay-ports` 有序两项（本仓在前、共享子模块在后）。**作用域：只放 wolf 自己消费的端口**，lite 所需端口由 lite 仓自行提供。不复用 `synthrt` 端口名以免静默遮蔽 |
| A30 | `algo-pinyin` 由「语言包内模块」改为**共享后端包 `wolf/g2p-pinyin`**，cmn / yue 改为其上的 `pipe-chain`。引擎经进程全局态解析词典根且只在构造期读取，两份根即两败俱伤；而两份存量词典与 cpp-pinyin 自带 `res/dict` 字节相同——它是引擎载荷，不是语言内容。A20 的第二个佐证 |
| A31 | B1 的收口不止拆包：provider 域另持**进程级词典根仲裁器**，一把锁罩住 `setDictionaryPath` 与引擎构造，Acquire 期登记，异根即加载失败并点名两根。把静默失灵换成确定性的加载期失败 |
| A32 | `algo-pinyin` 的引擎实例**每个后端执行体一份**，且**不进 `ResourceCache`**——上游转换方法为 `const` 却改写实例暂存（B1-b），缓存的契约是只读解析产物，塞入可变对象会破坏该不变式 |
| A33 | `pipe-chain` 的 `model` 步按**原词序中的极大连续段**分批送入后端，而非把过滤后的词拍平；`batchSize` 缺省为不再切分。相邻性是语义的一部分（`algo-pinyin` 的短语表） |
| A34 | 不合式的存量词典由**转换管线归一化**，读入端保持严格（只认制表符、不合式即加载失败）。据此修复 `fil_dict.txt` 的空格分列（旧栈一条也没读到过）与 `;;;` 引文行 |
| A35 | `synthrt-main` 端口以可选 feature `onnx` 承载 dsinfer 与 ONNX 驱动：main 线仍取 `third-party/onnxruntime` 布局，故**由端口铺放头文件**而非改上游（C1）；驱动运行期 `dlopen` ORT，构建期只需头文件；CUDA 关闭，Windows 走 DirectML、其余走 CPU |
| A36 | `multig2p-onnx` 的驱动由**宿主**创建、初始化并注册为 Runtime Service，模块只按后端名查取。据此分野：驱动缺席是**安装环境**的属性，逐词报 `DriverUnavailable` 并由链上 `fallback` 兜底；模型打不开是**包**的属性，加载失败 |
| A37 | `multig2p-onnx` Level 1 **只做贪心解码**；`beamSize` / `topK` > 1 与非 0 `lengthPenalty` 一律加载失败并指名不支持，不静默降级 |
| A38 | bundle 的保留符号（`<unk>` / `<pad>` / `<bos>` / `<eos>`）**按名字**在词表中查，不用导出惯例的下标 |
| A39 | `lua` 变体的沙箱移除全部对外通道（补齐旧栈遗留的 `load` / `loadstring`）；`utf8` 按 Lua 5.3 的完整形状补。脚本在 Acquire 期编译并校验入口，`lua_State` 每执行体一份、不进缓存（同 A32） |
| A40 | 带外部后端的变体按依赖存在与否**条件构建**；测试桩只声明本次构建未承载的三元组，使一个三元组永远只有一个 provider |
| A41 | 推理执行体一律实现合作式取消：`state()` 取 `Canceled`、返回已完成部分、`stop()` 在无执行时为空操作。脚本变体另装计数钩子——词边界对能自己死循环的实现不成立 |
| A42 | `lua` 沙箱**关闭 JIT 引擎**并移除 `jit` 全局：实测 LuaJIT 的 count hook 在编译出的 trace 内不触发，且 `luaL_openlibs` 会把编译器重新打开。旧栈装了同一钩子却从未关 JIT，取消机制对其唯一目标场景无效 |
| A43 | 链与 `algo-pinyin` 保证「转换词的 `pronunciation` 即 `candidates` 首项」：引擎的候选取自逐字表而读音可能取自短语表，两者恰在短语决定读音处分歧 |
| A44 | 链在**无 `fallback` 步**时，仍以 `PhonemeGenerationFailed` 上报无人产出的转换词——空发音配 error 为空会同时违反首项约束与「成功」之义 |
| A45 | `scheme` 命名规则 `<base>(-<qualifier>)*`：`<base>` 依次取标准记法名 / 社区公名 / 描述该集本身的造名，**禁用来源、生态、版本、「默认」之类的空词**；`<qualifier>` 仅表同族派生，兄弟集各取 `<base>`。据此 `eng/plus` → `arpabet-plus`，而 `deu/marzipan` → `marzipan` 而非 `ds-marzipan`（发布文档 §2.1.1） |
| A46 | 共享 bundle 的**全部 12 个语言引用都进 `languageMap`**——无人映射的音素集就是任何声库都点不到的音素集。八个取 `ds` 的按 A45 属待造名（P7），`ds` 为占位 |
| A47 | `pipe-chain` **禁止 `verify` 步出现在产出步之后**：分类决定产出步能碰哪些词，故须前置；后置的 copy 标记会无声丢弃词典命中。加载期拒绝，不给它定义语义 |
| A48 | `dict` 步与 `model` 步的音素集不必一致（实测 `eng` 44% / `ita` 75% / `fil` 100% 的词典行携带模型不产出的记号），链的符号集是各步并集**加兜底产出**。因 `exports.symbols` 须为全集，**它与 `useOriginal` 原词兜底互斥**——12 条链全用原词兜底，故当前一个都不能声明。想静态可比对须先放弃原词兜底（发布文档 §2.2） |
| A49 | P7 分批定名：`por` → `xsampa`、`kor` → `romaja`、`ita` → `xsampa-geminate` 按 A45 第 1 档落地；`deu` / `fra` / `spa` / `rus` / `fil` 既无标准记法又无公名，保留 `ds` 占位待造名。只改有硬证据的 |
| A50 | 语言包保留「查不到即输出原词」，**接受 `exports.symbols` 无法声明、宿主做不了加载期音素比对**。这是旧栈的既有行为，改动会改变十二个包的实际输出；契约本就允许省略该键并由宿主告警，故记为已知取舍而非缺陷 |
| A51 | `LinguistExecutive::stop()` **向下传递**到已创建的 G2P / S2P / Onset 子执行体。一个阶段整批一次调用，只停在本层的请求要等该调用自行返回；子执行体指针因此为原子，`stop()` 可与转换并发到达 |
| A52 | 契约的 JSON Schema 落 `docs/schemas/`（spec 2.4:596-602 的必须项），并由 `scripts/check-declarations.py` 对真实包执行——发布物只有与加载器一致才值得发布。据此收紧了加载器：`exports.languages` 的二元组现按身份字段的同一文法校验，并拒绝未知键 |
| A53 | 转换出的包版本第四位是**打包修订号**（前三位说源资源）。管线对同一输入产出不同结果即须抬它——否则两个内容不同的包顶着同一版本，按目标点求解的消费方拿到哪个全看下载到哪个 |
| A54 | **后端包的 `compatVersion` 取修订号 0，依赖方指向该下沿**；~~语言包无人依赖，`compatVersion` 等于 `version`~~（**语言包这一分支已由 A68 推翻**）。修订之间变的是打包而非被绑定的模块 ref 与契约。实证：把依赖写成后端当前版本字面量后，第一次抬修订号即让九个语言包同时解析失败 |
| A55 | B3 分批：`eng` / `por` / `kor` / `ita` 四种已闭包（`scheme` 已定，G2P 本就产出空格分隔音素故 S2P 用 `direct`，Onset 省略）。`cmn` / `yue` / `jpn` 产出音节，其音节→音素词典归歌手包（A66）；余五种等 P7。闭包的 `exports.phonemes` 取自该语言自己的词典 |
| A56 | 层栈增 **L6 会话层**（`wolf::LinguistSession`）：歌手→语言快照、就绪态与负缓存、执行体池、保留词与空词收口、取消把手。依据是 lite 已在 refactor 线自建过一套——这些不是宿主的业务判断而是语言域的实现细节，只有一个正确答案（`linguist-session.md`） |
| A57 | 会话**借用而不拥有 `SynthUnit`**，歌手用 `srt::ContribLocator` 标识，转换 IO 沿用 L4 的 `LinguistConvertInput/Result`——会话是生命周期层，不是第二套数据模型。`depth` 截断与逐词锁定已把 lite 的三个 API 压成一个 |
| A58 | 就绪三态 `Ready / Cold / Unavailable`：`probe()` 无副作用，`warm()` 主动预热，**成功与失败同样缓存**，唯一失效点是 `refresh()`。不设后台重试与超时——「何时重看」是宿主知道的事 |
| A59 | 执行体池按 (歌手, 语言) 分键、**不设默认上限**：并发度是宿主线程池的属性。代价比看上去小——ONNX 驱动已按 `path` 与 `(size, hash)` 引用计数共享 `SessionImage`，同一模型权重只载一份；真正随执行体重复的只有 cpp-pinyin 词表与 `lua_State`，两者因上游可变状态而无法共享 |
| A60 | `exports` 增开放位 `openSet`（缺省 `false`）：清单 + 「产出是否可能越界」。把一个必填却只能当建议值的字段变成能用的——`openSet` 为假时语义与今日完全一致，既有声明无需改动；取值由打包 lint 自动推导 |
| A61 | 保留词（缺省 `SP` / `AP`）收进会话产出 `mode=copy` + 单个保留音素；**空词与含空白词的判定归 G2P 模块**（链推理契约 §3.4.2 已规定），抽共用组件 `classifyLyric()` 写一次，三个变体均已实现。项目专属标记（连音、`+` 后缀）仍归宿主 |
| A62 | L6 设计经自检修正六处、砍三处：池须持 `PackageHandle`（执行体必须先于其 Package 销毁）、`refresh()` 按代换而不清空在飞条目、子执行体 `delete` 即脱离、`Cold` 不是可用承诺、保留词拦截须按 `depth` 补齐形状、~~一 unit 一会话~~（**最后一条已由 A78 推翻**）。砍掉 `LanguageStatus::binding`、`refresh()` 的 `Expected` 与池观测 API |
| A63 | L6 落地时的三处修正：① 歌手键为 `SingerRef { locator, version }`——`ContribLocator` 不含版本，而同一声库两版并存是常态，留空即歧义并拒绝；② 绑定由 `WolfPipelineExtension::binding()` 直接给出，因 `locate()` 的 locator 属语言包、从歌手包解析不到；③ **建执行体移出会话锁**（要载词典开模型，锁内做会让每帧的 `probe()` 卡住），租约先占再放锁。并发经 TSan 验证（`linguist-session.md` §12.1） |
| A64 | C1 / C2 落地。C1 的开放位 `openSet` 只加在**自称是全集**的三处（域契约 `phonemes`、G2P `symbols`、S2P `phonemes`），缺省 `false` 故既有声明语义不变；Onset 的 `knownPhonemes` 契约本就写明是下界，再加即重复声明，**不加**。取值由 `check-declarations.py` 从链的 `useOriginal` 兜底步推导——作者记得写不是机制。C2 写进域契约 §5.0，并给出理由：`onsets` 与 `phonemes` 等长已定死长度，只剩取值一个自由度，留给宿主就是让两个宿主对同一语言包给出不同切分 |
| A65 | 最小构建须能被**显式选出**（`WOLF_DISABLE_DSINFER` / `WOLF_DISABLE_LUAJIT`）。此前只能把 `find_` 缓存项强制成 NOTFOUND，而 `find_path` 下一次 configure 即搜回来——最小构建悄悄变回完整构建，A40 的「两种构建都验证过」因此在无人察觉中失效。同批修掉真正的失效：夹具 `lang-chain` 里混入了 `lua` 变体模块，无 LuaJIT 时整包加载失败，连累 `test_PipeChain` 全部用例；已拆为独立的 `lang-runaway` / `singer-runaway`，用它的那一例按构建条件跳过 |
| A66 | **B3 排除**：`cmn` / `yue` / `jpn` 的音节→音素词典是**歌手包内容**，不是语言包内容——spec 2.3 的示例把它写在歌手清单的 `languages[].dict`，按歌手配置目录解析，校验器亦按歌手侧路径校验。语言包本就不该出这份词典，缺它不是缺口；组合路径（语言包出 G2P、歌手包出 S2P 与 onset）已实现并由 `test_HostFlow` 覆盖。与 B2 同款结论：阻塞项来自对旧栈形状的误读 |
| A67 | **端口首次真实跑通**。此前从未安装成功过：`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 里是硬错误，端口一旦被使用即失败，而全程经 `WOLF_LANG_PACKAGES_SOURCE` 绕开，谁都没碰过。改为直接写 `copyright`（并把各包自带的 License.txt 收进去——纯数据端口该装哪些许可随 feature 而变，固定 FILE_LIST 表达不了）。同批修掉两处同源漂移：端口 `vcpkg.json` 的 `version-string` 由发布脚本随 bundle 版本一起改写（此前 assets.cmake 是生成的、版本号是手维护的，已经差了一个版本，vcpkg 会拿旧树当新版发），以及清单补上文档早已引用却不存在的 `lang-packages` feature（opt-in，不牵连普通构建） |
| A68 | **语言包的 `compatVersion` 同取修订号下沿**，A54 的语言包分支作废。理由回到 spec §兼容性：那里列的六项公开表面一项没破就不该抬，`compatVersion == version` 是一句不实的承诺，而规范明说「承诺不实属于 Package 缺陷」；且「有没有人依赖我」根本不是它的输入，A54 取错了轴。事实前提也不成立——A66 让歌手包必然依赖语言包，wolf 自己生成的夹具就写着 `wolf/lang-cmn 1.0.1.2`。实证：把 cmn 抬到 `1.0.1.3`，`test_HostFlow` 六例全报「no installed Package satisfies dependency」。据此把「依赖指向 `compatVersion`」的 lint 升为**错误**，并要求回归测试用「旧夹具 + 新包」的组合——同批生成永远测不出来，A54 那次正是这样漏过去的 |
| A69 | **不做能力晚绑定**：绑定由 `imports[].ref` 在加载期完全确定，声库初始化完成即有答案。晚绑定能从结构上消掉版本耦合，但把「目标不可用」挪到 `warm()`，该代价不接受——装上即可用是这一层要守住的性质。搜索空间由 spec 夹死：可选依赖 2.4 明文不支持、`imports[].ref` 必选且不设简写、`configuration` 不得承载跨模块契约内容、类别追加字段的判据是「在解释器选出前就要用上」而 import 绑定在 Ready。**故包级降级不做，且是规范结论不是取舍**；要让缺一个语言不拖垮整个声库，只能靠语言包自带 linguist 而不依赖它 |
| A70 | **降级只剩链级与音素级**。`linguist/s2p` 由「恰好 1」放宽为 `0..1`，缺席即 `maxDepth = Pronunciation`（onset 缺席即 `Phonemes`，已是今日行为）。放宽是 `variant` 的权限——spec §imports 写明「哪些 role 必须存在」由导入方的 variant 规定，且导入方「**仍可**」严格要求；且属「增加可选能力」，故不抬 `compatVersion`。音素覆盖度由会话算并交出 `coverage` / `missingPhonemes`，**不设门限**（`fil` 100% / `ita` 75% / `eng` 44% 说明固定门限必然误伤）；唯一例外 `coverage == 0 && !openSet` 由会话直接判 `Unavailable`——清单自称全集而声库一个都唱不了，那不是降级是接错了 |
| A71 | **「初始化即定」定的是绑定与形状，不是预热**。A69 之后绑定在加载期已定、`maxDepth` 从 `imports` 直接读、覆盖度只是集合运算，三者都不需开模型，故 `Cold` 收紧为「绑定已定、形状已算、仅未预热」，A58 的「不是可用承诺」限定为「不是**已加载**的承诺」。不把预热提前：声明 12 种语言的声库会在装包时开 12 条链，那正是 A63 ③ 移出锁的那笔开销。驱动缺席、模型打不开仍只能在 `warm()` 暴露（同 A36 的分野） |
| A72 | **进程级词典根的保留随配置对象存活**。`reserveRoot()` 返回 RAII 保留，由模块 `Configuration` 持有，计数归零即忘记根。原先是 Acquire 期一次不可撤销的全局写入，实测两个后果：失败的加载永久占住根（一个没装上的包废掉能用的包）、卸载也从不归还。配置对象本身就是 spec §加载事务要的那个事务私有物——失败随事务销毁、卸载随包销毁，abort 与 release 两条路径都自动成立，不必另写三段式 |
| A73 | **`stop()` 取消当前或下一次转换**，由 `start()` 以 `exchange` 消费该位，两条退出路径都清位故池中执行体不继承。会话的 `enrol()` 改为返回布尔，已取消的令牌不启动这一批。原先 `start()` 第一行无条件清位，而会话恰在其前调 `stop()`，于是**任何在 `convert()` 前落下的取消都被静默丢弃**——而那正是最常见的次序。不采 `ITask` 的「只在运行时置位」：会话先租后启，两步之间的窗口里落下的取消必须算数 |
| A74 | **导出的链接接口只许出现 config 会带入的目标**，由 `src/lib/CMakeLists.txt` 的配置期断言守住。`blake3` 与 `re2` 同归 `LINKS_PRIVATE`。此前 `blake3` 在公开 `LINKS` 而 config 只 `find_dependency(synthrt)`，README 的用法照抄即在 generate 阶段失败；仓内测试直接链构建树目标、从不走安装出的 config，所以永远发现不了 |
| A75 | **执行体一律持 `srt::ITask`**（抽出 `wolf::ExecutiveTask<Input, Result>`），不再手写任务面。手写的十一份拷贝一模一样地错三处：`waitForFinished()` 直接返回故卸包等待被当场满足、`startAsync` 八处同步就地回调而第九处裸 `detach`、无一拒绝第二次并发执行。更糟的第四处是本轮才查出的——`chain` / `pinyin` / `multig2p` / `lua`×2 把 `quit()`/`wait()` 覆盖成空操作，**废掉了 `srt::InferenceExecutive` 本来就接好的转发**，卸包时这些执行体既不停也不等。抽一次而非抄十一遍，同 A61 / A43 |
| A76 | **取消语义分两级**：父（`LinguistExecutiveImpl`）`exchange` 消费故开始前的取消算数（A73），子（六个推理执行体）在每轮入口清位。差别在谁把门——子的 `stop()` 是父广播下来的，可能比父那一轮活得更久，消费语义会让陈旧的位毒掉下一轮。不是推演：把消费语义套到子上时，`test_LuaVariants` 里一条早就写好的用例当场变红，而那条用例是对的 |
| A77 | **声明根的未知键改为警告**（新增 `wolf::logCategory()`），`exports` / `configuration` / `options` 仍严格。模块声明根是上位规范定义的对象，规范写明「框架只验证其认识的字段；未知字段不得导致拒绝」；在这里拒绝等于让框架日后新增的公共字段在早于它的 wolf 构建上加载失败。没有损失：拼错必填字段仍失败，因为它本该是的那个字段随即缺失 |
| A78 | **一 unit 多会话，限制取消**（推翻 A62 的那一条）。原限制既没强制也没检查，两头不落好；而会话之间除框架加锁的 `SynthUnit` 与两个自锁的进程级单例外无共享可变状态。改为由 `test_LinguistSession_TwoSessionsShareOneUnit` 钉住：预热、释放、并发转换三面互不干扰 |
| A79 | **桩无条件带一个 `stub-miscount` 三元组**（此外才是按构建条件补的 `multig2p-onnx`）。短批次拒绝写在三层，而四个仓里没有任何模块能产出短批次，三道闸门谁也没验证过；由桩按 `configuration.dropWords` 故意违反 provider ABI。用新变体名而非复用：桩在完整构建下不声明任何 G2P 三元组，复用会让用例只在最小构建里跑。同批整理测试面：删 `test_LinguistContrib_Reference`（断言的是 synthrt 而非 wolf），并给两条读起来像重复的 `pipe-chain` 用例改名点清分工（A44 的两条路径） |
| A80 | **ORT 改由 `onnxruntime-builds` 端口供给，synthrt 不再部署运行库**。上游分支 `onnxruntime-builds-uptake` 把 `onnxutil` 接到该包的 imported target（头随目标传递），插件 CMake 里的 ORT 路径与 `runtimes/onnx/<flavor>` 拷贝全删，`scripts/setup-onnxruntime.cmake`（钉着 1.17.3）与 `third-party/` 一并清掉——**两个仓里此后 ORT 版本号 0 处**，版本由 overlay 单独控制。依据是驱动的 `DriverInitArgs::runtimePath` 由宿主给、驱动从不自己去插件旁找，那份拷贝的唯一消费者是 synthrt 自己的测试。wolf 端口的头文件搬运随之删除；新增 `test_MultiG2P_RunsOnARuntimeTheHostDeployed` 把 ORT 部署到宿主自选目录再跑通全链，并断言驱动确实从那里加载 |

---

## 4. 符合性与改动清单

**结论：不违背 spec 2.4。** synthrt 花费「个别刚需字段」额度 2 项，其余为 wolf 侧增补。

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
   而 synthrt main 的加载器**只受理目录**——非目录即拒（`PackageLoader.cpp:1059-1061`），
   搜索路径只枚举子目录（`:614`），main 亦不再依赖任何解压库。发布物形态据此定为
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

「iso 代码 → 实际 linguist 贡献 ID」的闭合路径：
`"cmn"` → role `lang/mandarin` → 该 import 的 `locator().contributionId()` = `cmn-pinyin`，
全程 DataOnly 可读。

---

## 6. 校验矩阵

失败一律使整次加载失败（spec 2.4:444）。框架无告警通道，提示级检查全部归编辑期 lint。

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

- 全程经 F3 的 `createChild` 完成，零框架改动；父子析构与 `quit`/`wait` 传播由监督树承载；
- **一条链 = 一棵子树 = 一个在飞任务**（C2）。宿主并发 k 路即 `createLinguist` k 次；
- 跨子树的资源共享由 `wolf::ResourceCache` 承担——这是 C2 的直接推论，不是可选优化。

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

五份草案已删除，本表记录其内容去向，供 git 历史回溯时定位：

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

五份草案的最后版本见删除前的提交；本表列出的去向已全部落地。
