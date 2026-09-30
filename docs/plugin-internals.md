# 插件内部规范

**状态：V1（待评审）**。本文规定 `src/plugins/` 内部的写法与约定：骨架、配置与资源解析、错误与
诊断、契约身份、演进检查清单。层语义、契约词汇、变体键汇与发布面各有专文，本文不重复，只引用
（见 [Status.md](Status.md) 的文档表）。

约定：标注 **[规范性]** 的条目是要求，违反即违规；标注 **[现状]** 的条目是对当前实现的事实描述，
不构成要求。冲突时的权威顺序为 `ds-spec-2.4.md` > `linguist-*.md`（层与变体面）> 本文 > 代码注释。
**定位方式**：引用文档时只给小节，引用代码时给文件与符号，必要时附行号。小节是稳定锚点，文档的
行号随修改而失效；代码行号同样会漂移，以符号为准。

## 1. 权威锚点

| 面 | 权威 | 本文引用处 |
| :-- | :-- | :-- |
| 路径解析的两级基准 | [linguist-variants.md](linguist-variants.md) §2.1 | R3 |
| 格式版本自验 | 同上 §2.2 | R1、R3、R5 |
| 拒绝未知键的适用范围 | 同上 §2.3 | R6 |
| S2P / Onset 的豁免 | 同上 §3.3 | R3 |
| `rule` 文件 schema | 同上 §3.3「`rule` 文件结构」 | R9、R3 |
| 兜底成功与 `error` | 同上 §3.3 兜底段落 | R17 |
| 三级失败与诊断通道 | [linguist-runtime.md](linguist-runtime.md) §5.1、§5.2、§6 | R16、R17 |
| 无原生版本字段资源的版本载体 | [linguist-resource-cache.md](linguist-resource-cache.md) §2 | R2、R5 |
| role 的开放集 | [linguist-domain-contract.md](linguist-domain-contract.md) §5 | R22 |
| 包身份三方比对与导入选项时机 | synthrt `lib/Core/PackageLoader.cpp` 的 `validatePayloadIdentity`（三方比对）与 `createImportOptions`（导入选项时机） | R23、R24 |
| 契约头常量 | `include/wolf/Api/**` 的 `API_INTERFACE` / `API_LEVEL` / `API_VARIANT` | R23 |

## 2. 骨架与构建 [现状]

七个推理解释器插件结构相同：`Configuration : srt::ContribConfiguration`、
`Executive : ExecutiveBase<XxxApi::XxxExecutive, Input, Result>`（任务面与停止标志位于基类，执行体
只实现 `runBatch()`，并在析构函数中先调用 `finish()`）、`Interpreter : srt::InferenceInterpreter`、
`XxxPlugin final : srt::InferenceInterpreterPlugin`。连同 linguist provider 与测试用的歌手 provider，
九个插件的 CMake 一律经 `wolf_add_interpreter_plugin(<target> [NO_INSTALL] [NO_MANIFEST]
[LINKS_PRIVATE ...])`（`src/plugins/CMakeLists.txt`）声明。该函数以所在目录名为插件名，完成
`wolf_add_plugin`、`stdc_add_plugin_metadata`、`plugin.json` 在构建树中的配置期复制及其安装。
插件安装在 `lib/plugins/wolf/<category>/<name>/`，构建树布局相同。统一骨架因此不是待办项，本文只
规定骨架之上容易出现偏差的部分。

**Support 的链接方式 [规范性]**：`include/wolf/Support` 不安装，其符号不属于公开 ABI。无状态的
工具（`ManifestValues`、`ContractValues`、`Verifier`、`Files`、`Utf8`、`InputRules`）编入静态库
`wolfsupport`（隐藏可见性、PIC），libwolf 与每个插件各自私有链接一份。**必须进程内唯一**的两项
留在 libwolf，以 `WOLF_INTERNAL_EXPORT`（`Support/SupportGlobal.h`）导出：`ResourceCache`（多个
插件共享同一份解析产物，且缓存以 libwolf 的控制块重新持有产物，必须比插件存续更久）与
`logCategory()`（按名称注册的日志类别）。这些符号只供本仓随附插件链接，不属于公开 API，任何版本
都不作 ABI 保证。插件的 CMake 因此一律为 `LINKS wolf` + `LINKS_PRIVATE wolfsupport`（由
`wolf_add_interpreter_plugin` 统一给出）。新增的有状态共享设施归入 libwolf 并标注
`WOLF_INTERNAL_EXPORT`，新增的无状态工具归入 `wolfsupport`。

### 验证通道 [现状]

修改 `src/plugins/**` 后，至少通过以下两项检查，两者均可在本仓内离线运行：

| 检查 | 运行方式 | 最近一次记录（2026-09-21） |
| :-- | :-- | :-- |
| 编译器检查 | 两棵已配置的树增量构建：`cmake --build build/cmake`（Release）、`cmake --build build/cmake-debug`（Debug）；两者均为 `WOLF_BUILD_TESTS=ON` | 更新 mtime 强制重新编译 31 个插件源文件与头文件，两棵树各自重新编译受影响的编译单元（共 40 个编译动作、18 次链接），**退出码 0，无告警** |
| 自动测试 | `ctest --test-dir build/cmake` | 当时 **17/17 通过**（7.00 s）；完整构建现注册 17 个测试二进制 |

测试按层放在 `src/tests/auto/`，各层的职责写在该目录 `CMakeLists.txt` 的注释中（Support 为纯单元；
Linguist 为类别与声明；Inference 为链与变体解释器；Runtime 为执行树与会话；EndToEnd 为生成包的
走查）。新增插件或变体时按 `wolf_add_test(<层>/<文件> wolf)` 增加一项（该宏同时把目标记入统一
设置环境的清单），不得依赖人工运行。

生成物的自洽性也在此检查：语言包由 `test_ConvertedPackages` 走查（修改声明或生成脚本后必须运行）。
与**宿主**的联动不属于本仓：宿主能装载与列举的内容由宿主仓的测试负责。

## 3. 版本与世代

**[规范性] R1 三态处置**：任何**由资源声明**的版本字段（`formatVersion`、`bundle_version`）一律
按三态处置：**缺失或非正整数 ⇒ 加载失败**（`InvalidFormat`）；**超过实现上限 ⇒
`FeatureNotSupported`**，且诊断必须同时给出声明值、支持上限与升级指引。依据 §2.2。`formatVersion`
的实现为 `readFormatVersion`（`include/wolf/Support/ContractValues.h`，由 `chain` 与 `pinyin` 调用）；
`bundle_version` 的实现位于 `multig2p/Bundle.cpp`。新写的实现沿用其文案。kind 世代常量不在此列，
因为它没有声明值，见 R1a。

**[规范性] R1a kind 世代常量不是版本字段**：形如 `s2p-dict-tsv@1` 的世代常量是**解析器的缓存
身份**，资源侧没有承载它的字段（表文件与声明中都没有）。它作为 `parserGeneration` 参与缓存键
（`include/wolf/Support/ResourceCache.h` 的 `acquire`，该处注释说明其用途：格式变更后旧条目不再
命中，无须逐条失效），**提升它本身就是失效机制**（R5）。因此它**不适用三态处置**，因为不存在可
校验的声明值。不合法的**形状**由解析层的严格性拒绝：未知键（R6）、同一世代内字段数双向严格
（R10）、文本规整（R11）。

**[规范性] R2 载体归属**：

| 资源族 | 版本载体 | 依据 |
| :-- | :-- | :-- |
| 变体 `configuration` 键汇 | `formatVersion` | §2.2 |
| 家族配置文件（经路径键定位的 JSON，如 `chain.json`） | `formatVersion`；S2P/Onset 的 rule 文件见 R3 | §2.2 |
| 资源 bundle（multig2p `bundle.json`） | 文件内的 `bundle_version`，上限为 `Bundle.h` 的 `SUPPORTED_VERSION` | §5.3 |
| 无原生版本字段的表（s2p dict/mapping TSV、chain 词典 TSV） | **kind 语法世代常量**（形如 `s2p-dict-tsv@1`） | [resource-cache](linguist-resource-cache.md) §2 |
| Lua 脚本 | 无（脚本即代码） | — |
| `lua` 插件的 `configuration` | **豁免**（无版本载体） | 定案说明见下 |
| wolf linguist 的 `configuration` | **豁免**（无版本载体） | 定案说明见下 |

**豁免说明（`lua` 与 wolf linguist 的 `configuration`，已定案）**：这两族不设 `formatVersion`，判据
与 R3 相同：**新增必填键不得导致既有分发产物加载失败**。经清点，现有实现中这两族的
`configuration` **均无**该字段（`lua/main.cpp`、`WolfLinguistProvider.cpp` 各 0 处），将其设为必填
会立即导致既有 lua 模块与既有 linguist 配置加载失败。形状演进仍有两项约束：**键名与拼写**由 R6
「拒绝未知键」约束；**契约语义**由契约 `level` 的提升约束（R22，新增核心 role 必须递增 level）。
脚本文件本身不是配置，见上表「Lua 脚本」行。

**[规范性] R3 家族配置文件的版本字段是否必填，按存量判定**：只有在**新增必填键不会导致既有分发
产物加载失败**时，`formatVersion` 才可以设为必填。判定依据为清点结果：公开分发的 **7 款声库包**
各带 4 份 onset `rule` 文件（共 **28 份**，其中 `assets/eng.json` 的文件名不含 `onset`，按文件名
搜索会遗漏四分之一），顶层键一律只有 `phonemeTypes` 与 `rules`，**没有一份**带 `formatVersion`；
而 rule 文件在 Acquire 期解析（`onset/main.cpp` 中经 `ResourceCache::acquire<RuleTable>` 读取），
解析失败即**包级加载失败**。因此该文件的 `formatVersion` 为**可选**：缺省视为 1，声明时按 R1
校验；形状演进由缓存世代常量承载（`onset/main.cpp` 的 `RULE_KIND = "onset-rule-json@1"`），读入端
以「拒绝未知键」约束拼写演进。实现见 `OnsetRules.h` 的 `RuleTable::FORMAT_VERSION` 及其注释。

**[规范性] R4 单一真相源**：同一版本或世代常量在一个插件内**只允许定义一处**，其余位置引用该
定义；上限常量的注释必须说明提升它的后果。[现状：multig2p 的 `@1` 世代位于 `main.cpp` 的
`BUNDLE_KIND` / `VOCABULARY_KIND`，`bundle_version` 的上限位于 `Bundle.h` 的 `SUPPORTED_VERSION`；
两者是**两类事实**且无交叉引用，这是设立 R5 的原因]

**[规范性] R5 形状变更只通过提升世代实现**：`formatVersion` 只覆盖 `configuration` 及其经路径键
引用的家族配置文件；`configuration` 之外的表与资源由 kind 世代常量承载（R2）。两套世代**各自独立
提升**，任何一方的**形状**变更必须提升对应的那一份。**不得通过放宽解析器实现形状变更**：同一
世代内的字段数按 R10 严格校验，新增列属于新世代。§2.3 的「拒绝未知键」只约束键名与拼写，不意味
着形状永不新增列。

## 4. 配置与资源解析

**[规范性] R6 拒绝未知键**：`configuration`、`exports` 与家族配置文件一律拒绝未知键，诊断形如
`unknown <对象> key: <key>` 或 `<上下文> has an unknown key: <key>`。依据 §2.3。[现状：
`OnsetRules.cpp` 的 rule 文件与规则对象已实现]

**[规范性] R7 打开失败的错误码**：资源或表文件缺失 ⇒ `FileNotFound`；文件存在但**无法打开或读取
中断** ⇒ `FileNotOpen`。判定依据为**路径是否存在**（带 `error_code` 的 `fs::exists`），而不是把
打开失败一律视为缺失，因为一次打开失败同时覆盖两种故障，而两者的处置不同：文件缺失属于打包错误，
无法读取属于宿主环境问题。修改错误码是**对外可观察的行为变更**，按破坏性微调声明。
[现状：**实现只有一处**：`include/wolf/Support/Files.h` 的 `fileErrorCode(path)` 判定错误码，
`fileError(path, what)` 生成消息（缺失 ⇒ `<what> not found: <路径>`；存在但无法打开 ⇒
`failed to open the <what>: <路径>`），`openForReading(file, path, what)` 打开文件并在失败时返回
`fileError` 的结果，且先把目录判为 `FileNotOpen`（在 POSIX 上把目录作为流打开会成功，随后每次读取
都失败，调用方会误作空文件处理）。chain/s2p/onset 的表、Verifier 的 dict、lua 脚本、multig2p 的
bundle 与词表、`readStringSet` 的路径形式全部经 `openForReading`；`ResourceCache` 保留本层的消息
措辞（说明失败的步骤），错误码同样经 `fileErrorCode` 判定。读取中断统一为
`failed to read the <what>: <路径>`。**在当前框架下，插件侧的这两个分支属于防御性代码**：实测
（第八轮 Case B、第十轮 Case C）表明，文件缺失与无法读取的故障在插件解析之前即由 `ResourceCache`
报告，见 R8 的边界段]

**[规范性] R8 诊断必须包含文件身份**：格式一律为「**完整路径（经 `stdc::path::to_utf8`）与位置**：
原因」；行式文件给出行号，JSON 给出解析器报告的行列。只写文件名时，多个包中的同名表无法区分。
[现状：三处均已包含路径与位置：chain 行号、s2p 行号、onset 的 JSON 行列；此前 onset 完全不含路径、
chain 不含行号]

**R8 的边界（第八轮、第十轮实测；2026-09-22 全链路审计第一批已更正归因）**：

1. **文件缺失** ⇒ 由 `ResourceCache` 的尺寸检查报告：`failed to interpret module configuration:
   <路径>: failed to size a resource`。**包声明**的资源如此（第八轮 Case A 的对照组），**声库在自身
   角色配置中指定的资产**也如此（第十轮 Case C：删除副本中的 `assets/*_onset.json`）。在这条路径上
   插件**已被调用**：`failed to interpret module configuration` 是 synthrt 在插件
   `createConfiguration` **之后**附加的上下文（`PackageLoader.cpp` 的 `withContext`），而
   `failed to size a resource` 来自 wolf `src/lib/Support/ResourceCache.cpp` 的 `statFile`。
   `acquireErased` 在任何解析之前先调用 `statFile`，因此插件的 `fileError` 不会执行；
2. **文件存在但无法读取**（如路径指向目录或文件被占用）⇒ 由散列步骤报告
   （`<路径>: failed to open a resource for hashing`，同一文件的 `hashFile`）；
3. **文件存在且可读、但内容非法** ⇒ 由插件报告，诊断**包含完整路径与位置**（行号或 JSON 行列），
   第八轮 Case A（词表多列）与 Case B（onset 规则的 JSON 损坏）即属此类。

路径类故障（缺失、无法读取）的消息原先不含文件路径。缺口位于 wolf 自身的库
（`src/lib/Support/ResourceCache.cpp`），不在 synthrt，因此登记为可修项（2026-09-22 用户裁定 D1）。
[现状：已修复。`statFile` 与 `hashFile` 的三条消息经 `unreadable()` 以 `<路径>: ` 开头，错误码经
`fileErrorCode` 判定为 `FileNotFound` 或 `FileNotOpen`]。R7 的 `fileError` 分支仍属**防御性**代码：
在现有调用路径上不可达（`acquireErased` 先于插件的解析执行 stat），但语义必须正确。

**[规范性] R9 空表判为失败，但**仅限 TSV 表**，且按**使用者**判定**：chain 词典为空 ⇒
`InvalidFormat`（样板为 `ChainTables.cpp` 的 `holds no entries`）。**s2p 的 dict/mapping 不判空**，
这是**已定的差异，不是缺陷**：空表表示无映射，词级失败交由链上的 `fallback` 处理；且现有语料中 s2p
的 `variant` 全为 `direct`、`configuration` 为空（不声明表），没有样本可作为改判依据。修改前须先有
语料证据。**本条不适用于规则文件**：onset 的空 `rules` 是合法语义（「无任何匹配时输出全
`false`」，§3.3），实现有意不对其判空（只对 `phonemeTypes` 判空）。

**[规范性] R10 同一世代内字段数双向严格**：缺列与多列均拒绝。样板为 `S2PTables.cpp` 的
`twoColumns`。[现状：**已对齐**：chain 与 s2p 均拒绝多列与缺列，且四条消息一致（`missing tab
separator` / `multiple tab separators` / `empty first column` / `empty second column`）；对齐前
chain 把首个制表符之后的全部内容归入发音，经 `splitPronunciation` 后会产出含制表符的音素名]

**[规范性] R11 文本规整**：只剥除首行 BOM 与行尾 `\r`（`Files.h` 的 `stripLineDecorations`）；不支持
注释行；**不得**对键列做隐式 trim（空白属于数据，需要拒绝时显式拒绝）。

**[规范性] R12 读取完整性**：读取循环结束后检查 `file.bad()`（以 `rdbuf()` 整体读取的写法同样须在
读取后检查）。样板为 `S2PTables.cpp` 中两个表读取函数末尾的检查。[现状：chain、s2p、onset **均已
检查**]

**[规范性] R13 JSON 读取收敛到单一实现**：帮助函数放在库侧并带上下文参数，以保留各处既有的诊断
措辞。收敛前须对齐各实现的细节差异（如助记名、宽松实参），不得凭印象假设其一致。
**[决定：本目标内暂不实施]**。[现状：清单与契约值的读取已集中在 `wolfsupport`
（`ManifestValues`、`ContractValues`）；JSON 文件读取尚未统一，例如 `multig2p/Bundle.cpp` 仍自带
`readJson`。落点为 `wolfsupport`，签名待 P4 确定。实施时作为**独立批次**，并单独征求同意]

**[规范性] R14 大小写折叠按 Unicode 表执行**，或把声明面收窄到可确定的区间；禁止用码位奇偶等
启发式规则代替 Unicode 语义。[现状：`ChainTables.cpp` 的 `lowercaseCodePoint` 对 U+0100–U+017E
按奇偶处理，在 U+0138–U+0149 与 U+0178–U+017E 区间方向错误；另外，声库包的 `phonemeTypes` 中存在
**仅大小写不同**的类型名（如 `n`/`N`、`ap`/`AP`），因此键比较**必须区分大小写**]

**[规范性] R15 值用作路径前先经闭集校验**：形如 `ref` 的短标识符在拼入路径前必须经白名单校验。
样板为 `PinyinEngines.cpp` 的 `PinyinEngineRegistry::isKnownRef` 及其调用处。[现状：
`pathFromManifest`（`ManifestValues.cpp`）只归一反斜杠，不处理 `..` ⇒ bundle 的 `files` 可以越出
包目录]

## 5. 错误、诊断与日志

**[规范性] R16 失败按三级通道归属**：词级 → `LinguistWordOutput::error`；批级 → `start` /
`startAsync` 返回的 `Expected`；加载级 → Package `Load` 失败（依据 `linguist-runtime.md` §5.1）。
**异常路径必须经批级 `Expected` 报告**，不得表现为「产出为空且无 error」（§5.1 补充规则）；L4 组合
层不新增加载期失败事由（§5.2）。[现状：lua 的**运行期**失败沿用加载级的 `InvalidFormat`
（`LuaSandbox.cpp` 中入口函数执行失败与返回值形状错误的分支），与分级不一致]

**[规范性] R17 词级诊断只经 `hitStage` 报告**，不得写入词级 `error`；兜底成功时 `error` 必须为空且
`hitSource = fallback`（§3.3 兜底段落、`linguist-runtime.md` §6）。

**[规范性] R18 日志**使用 `wolf::logCategory()`；历史沿革只在库侧记录一处，插件侧只写不变量。

## 6. 线程、取消与生命周期

**[规范性] R19 取消时返回与输入等长的结果并逐词标注**（契约 `G2PApiL1.h` 规定失败词保留位置）。
[现状：pinyin 的 `runBatch` 在 `stopRequested()` 为真时返回空的 words；multig2p 先对结果执行
`resize`；两者不一致]

**[规范性] R20 停止谓词统一为 `m_stopRequested.exchange(false)`**。[现状：该谓词只在
`include/wolf/Support/ExecutiveBase.h` 中实现一次，各执行体经基类读取停止标志]

**[规范性] R21 禁止静默丢项**：对契约枚举的 `switch`，**穷尽列举全部枚举值**（依赖 `-Wswitch` 在
编译期检查）**或**显式以 `default` 报错，两者均可；**禁止 `default: break;`**，因为它会关闭
`-Wswitch`，使现有的编译期检查失效。[现状：**第十轮全量清点，四处全部合规**：chain `main.cpp:480`
的 `classifyLyric`（3/3 `LyricVerdict`）、chain `main.cpp:501` 的 `step.kind`（5/5 `StepKind`，
枚举定义在同文件 `:46-52`）、pinyin `main.cpp:156` 的 `verdicts[i]`（3/3，含 `Accept`：**每个输入
索引必然产出一个词**）、`LinguistExecutiveImpl.cpp:264` 的 `hitSource`（5/5，`Unspecified` 在其
case **内**为空操作）；且 `src/plugins` 内**不存在任何 `default` 分支**，本条的禁止项不适用。原先
「pinyin 对 `LyricVerdict` 无 `default` 且会在循环末尾丢词」的描述**已随分批重写失效，此处更正**]

## 7. 契约身份与分派

**[规范性] R22 role 的命名空间**：以 `linguist/` 开头的 role 名属于本契约族的保留命名空间；Level 1
核心集恒为 `linguist/g2p`、`linguist/s2p`、`linguist/onset`；**新增核心 role 必须递增契约
`level`**，不得以 `configuration` 键或资源 `formatVersion` 承载（L1 的 linguist `configuration`
必须为空 object，见 `WolfLinguistProvider.cpp` 的 `the wolf linguist configuration must be empty at
Level 1`）。**不以 `linguist/` 开头的 role 按 [domain-contract](linguist-domain-contract.md) §5
「其他」放行，不判加载失败**。validator 对未识别 role 静默放行（`WolfLinguistProvider.cpp` 中按
`ROLE_G2P` / `ROLE_S2P` / `ROLE_ONSET` 分派的三个分支之后没有 `else`），**这是承重行为，不是缺陷**，
不得改为拒绝。

**[规范性] R23 `level` 判定写成集合成员判定**：不得写成与单一常量的等值比较。[现状：全仓 15 处
固定为 L1；其中 `WolfLinguistProvider.cpp` 对 `LinguistApi::API_LEVEL` 的五处比较影响最大。L2 组合
被 L1 声库导入时，synthrt 在加载期即获取导入选项（`lib/Core/PackageLoader.cpp` 的
`createImportOptions`），而该处要求三元组完全相等（同文件的 `validatePayloadIdentity`），结果是
以 unsupported contract 加载失败，而不是降级]

**[规范性] R24 manifest 与代码的对应关系由构建或测试保证**：`plugin.json` 的 interpreters 条目
应由受支持档位集合**生成或比对**，不手工抄写（先例：`stub/CMakeLists.txt` 以 `file(CONFIGURE)`
从 `plugin.json.in` 生成 manifest）。插件返回的 payload 由框架按三元组比对（synthrt
`lib/Core/PackageLoader.cpp` 的 `validatePayloadIdentity`），因此返回值的一致性已有保障，声明与代码
的一致性仍需本条保证。[现状：插件手写的 `plugin.json` 与 `create()` 接受的集合之间无自动比对]

## 8. 演进检查清单

1. 新增 **level 或变体**：修改 `plugin.json` 三元组、代码中的 `VARIANT` 与接口常量、目录名、CMake
   `CATEGORY`、契约头常量；`create()` 使用**一次组合判定**，并使诊断包含三个分量（正例为
   `chain/main.cpp` 的 `create()`；偏差样本为 `lua/main.cpp` 的 `create()`，其先单独判定变体）。
2. 同一插件的多个变体：**共用参数化的 Configuration**（正例为 `stub/StubExecutives.cpp` 的
   `StubConfiguration`、`singerproviders/stub/main.cpp` 的 `Configuration`；反例为 `lua/main.cpp`
   的 `S2PConfiguration` 与 `OnsetConfiguration` 两份同形类）。
3. 修改 **TSV 表语法**：提升 kind 世代常量（R5）。
4. 修改**家族配置文件格式**：按 R3 判定是否必填，并同步 schema 段与实现。
5. 增加 **role 或层级**：role 名与 Depth 阶梯集中为单一常量定义，并更新 validator 与相关检查。
6. 新增**枚举值**：检查全部 `switch`（R21）。
7. 清理**死代码**前先全仓搜索：只命中声明与定义的即为死代码（`MappingTable::targets()` 与
   s2p 的 `Configuration::file` 即按此删除）。只供测试观察的状态不进入库的导出面：测试经友元类读取
   私有状态（样板为 `ResourceCacheProbe` 与 `src/lib/Support/ResourceCacheImpl.h`）。
8. 修改**分批与邻接语义**：后端**自行**按「原词序中的极大连续可转换段」分批，**不得假定调用方已
   切分**。依据：`pipe-chain` 的 `model` 步已按段切分，因此后端是否自行切分在链下**不可观察**，只有
   **直接使用契约**的宿主能触发差异；反例：pinyin 曾把过滤后的词合并为一张表，使被否决词两侧的
   字符互相邻接。
9. 新增**被引用的文件**（表、规则、模型）：先按 R8 的边界确认故障归属：**内容非法**由插件诊断
   （必须包含路径与位置），**缺失或无法读取**由 `ResourceCache` 的尺寸检查与散列步骤报告（已包含
   路径，插件侧不重复实现）；并检查该文件是否需要按 R2/R5 设置版本载体与世代常量。
10. 执行**验证通道**（§2 末段）：两棵树增量构建并检查告警，运行 `ctest`（含
    `test_ConvertedPackages`）；新增插件或变体时同时增加 `wolf_add_test`。骨架统一（§2 首段）不是
    待办项。

## 9. 格式与注释

**[规范性] R25** 全部 C++ 文件符合仓库根目录的 `.clang-format`（基于 LLVM、4 空格缩进、100 列、
`PointerAlignment: Right`、`SortIncludes: Never`）；CI 目前没有格式检查，由本地格式化与评审保证。

**[规范性] R26** 注释只写不变量与设计理由，不写历史沿革的副本；中英文按文件现状，不整篇改写。

## 10. 未决项

| # | 未决项 | 现状 |
| :-- | :-- | :-- |
| P1 | ~~`lua` 与 wolf linguist 的 `configuration` 既无 `formatVersion`，规范中也无豁免条款~~ | **已定案（2026-09-21，用户裁定）：写明豁免**，见 §3 的 R2 表与豁免说明；不设必填，理由是新增必填键会导致既有分发产物加载失败 |
| P2 | ~~`level` 的固定判定是否改为集合判定、manifest 是否改为生成~~ | **已定案（2026-09-21，用户裁定）：分批采纳**。manifest 生成先实施（先例见 R24 的 `stub`），`level` 集合判定在实际引入 L2/L3 时实施；该轮只定案，不改代码 |
| P3 | ~~R7 错误码与 R9/R10 的严格化会改变对外可观察的行为~~ | **已实施（2026-09-21）**：三处 `fileError` 的两个分支、chain/s2p 的缺列、多列与空列检查、chain 空表的 `holds no entries` 均已实现（`ChainTables.cpp`、`S2PTables.cpp`、`OnsetRules.cpp`）；原先「须先清点存量词典（空表、多列）」的前提已不成立（2026-09-22 审计复核：§4 中 R7/R9/R10 的 `[现状]` 段与代码一致） |
| P4 | R13 的收敛落点与帮助函数签名 | 待定 |
