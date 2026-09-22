# 插件内部规范

**状态：V1（待评审）**。本文管 `src/plugins/` 内部的写法与约定——骨架、配置与资源解析、错误与
诊断、契约身份、演进检查清单。层语义、契约词汇、变体键汇、发布面各有专文，本文不重复，只引用
（见 [Status.md](Status.md) 的文档表）。

约定：标注 **[规范性]** 的是要求，可被判违规；标注 **[现状]** 的是当前实现的事实描述，不构成
要求。冲突时的权威顺序为 `ds-spec-2.4.md` > `linguist-*.md`（层与变体面）> 本文 > 代码注释。
**定位方式**：对文档只给小节，对代码给文件与行号——小节是稳定锚点，文档一改行号即失效。

## 1. 权威锚点

| 面 | 权威 | 本文用到处 |
| :-- | :-- | :-- |
| 路径解析两级基准 | [linguist-variants.md](linguist-variants.md) §2.1 | R3 |
| 格式版本自验 | 同上 §2.2 | R1、R3、R5 |
| 拒绝未知键的适用范围 | 同上 §2.3 | R6 |
| S2P / Onset 的豁免 | 同上 §3.3 | R3 |
| `rule` 文件 schema | 同上 §3.3「`rule` 文件结构」 | R9、R3 |
| 兜底成功与 `error` | 同上 §3.3 兜底段落 | R17 |
| 三级失败与诊断通道 | [linguist-runtime.md](linguist-runtime.md)（`:249-262`、`:271`） | R16、R17 |
| 无原生版本字段资源的版本载体 | [linguist-resource-cache.md](linguist-resource-cache.md)（`:54`） | R2、R5 |
| role 的开放集 | [linguist-domain-contract.md](linguist-domain-contract.md) §5（`:237-241`） | R22 |
| 包身份三方比对与导入选项时机 | synthrt `lib/Core/PackageLoader.cpp` 的 `validatePayloadIdentity`（三方比对）与 `createImportOptions`（导入选项时机） | R23、R24 |
| 契约头常量 | `include/wolf/Api/**` 的 `API_INTERFACE` / `API_LEVEL` / `API_VARIANT` | R23 |

## 2. 骨架与构建 [现状]

九个插件同构：`Configuration : srt::ContribConfiguration`、`Executive : XxxApi::XxxExecutive`、
`Interpreter : srt::InferenceInterpreter`、`XxxPlugin final : srt::InferenceInterpreterPlugin`；
CMake 一律经 `wolf_add_plugin(... NO_EXPORT FEATURES cxx_std_17 ...)` + `stdc_add_plugin_metadata`
+ `install(FILES plugin.json)`。故"统一骨架"不是待办工作，本文只规定骨架之上易走偏的部分。

### 验证通道 [现状]

改完 `src/plugins/**`，至少过这两关，都在本仓内离线可跑：

| 关 | 怎么跑 | 本次体检（2026-09-21，HEAD `6c3fb5c`） |
| :-- | :-- | :-- |
| 编译器体检 | 两棵已配置的树增量构建：`cmake --build build/cmake`（Release）、`cmake --build build/cmake-debug`（Debug）；两者 `WOLF_BUILD_TESTS=ON` | 碰 mtime 强制重编 31 个插件源/头文件，两棵树各自重编受影响 TU（共 40 个编译动作、18 次链接），**exit 0、0 条告警** |
| 自动测试 | `ctest --test-dir build/cmake` | **17/17 通过**（7.00 s） |

测试按层放在 `src/tests/auto/`，各层的职责写在该目录 `CMakeLists.txt` 的注释里（Support 纯单元；
Linguist 类别与声明；Inference 链与变体解释器；Runtime 执行树与会话；EndToEnd 走查生成包）；
新增插件或变体时照 `add_auto_test(<层>/<文件> wolf)` 补一条，别让它只靠人工点。

校验"生成物是否自洽"也在这里：语言包由 `test_ConvertedPackages` 走查（改声明或生成脚本后必跑）。
与**宿主**的联动不属本仓：宿主能装载什么、列举出什么，由宿主仓自己的测试负责。

## 3. 版本与世代

**[规范性] R1 三态处置**：任何**由资源声明**的版本字段（`formatVersion`、`bundle_version`）一律
三态一致处置——**缺失或非正整数 ⇒ 加载失败**（`InvalidFormat`）；**超过实现上限 ⇒
`FeatureNotSupported`**，且诊断必须同时给出「声明值」「支持上限」「升级指引」。依据
§2.2。样板：`chain/main.cpp:838-844` 与 `pinyin/main.cpp:403-409`（两处逐字相同，
新写时照此文案）。（kind 世代常量不在此列——它没有"声明值"，见 R1a。）

**[规范性] R1a kind 世代常量不是版本字段**：形如 `s2p-dict-tsv@1` 的世代常量是**解析器的缓存
身份**，资源侧没有承载它的字段（表文件与声明里都没有）：它作为 `parserGeneration` 参与缓存键
（`include/wolf/Support/ResourceCache.h` 的 `acquire`，该处注释写明用途是"格式变更不再命中旧条目、
无须逐条失效"），**抬它本身就是失效机制**（R5）。因此对它**不存在"三态处置"的对象**——没有可
校验的"声明值"。拒绝不合法**形状**靠解析层严格性：未知键（R6）、同一世代内字段数双向严格
（R10）、文本规整（R11）。

**[规范性] R2 载体归属**：

| 资源族 | 版本载体 | 依据 |
| :-- | :-- | :-- |
| 变体 `configuration` 键汇 | `formatVersion` | §2.2 |
| 家族配置文件（经路径键定位的 JSON，如 `chain.json`） | `formatVersion`；S2P/Onset 的 rule 文件见 R3 | §2.2 |
| 资源 bundle（multig2p `bundle.json`） | 文件内的 `bundle_version`，上限是 `Bundle.h:60` 的 `SUPPORTED_VERSION` | §5.3 |
| 无原生版本字段的表（s2p dict/mapping TSV、chain 词典 TSV） | **kind 语法世代常量**（形如 `s2p-dict-tsv@1`） | [resource-cache](linguist-resource-cache.md) `:54` |
| Lua 脚本 | 无（脚本即代码） | — |
| `lua` 插件的 `configuration` | **豁免**（无版本载体） | 定案说明见下 |
| wolf linguist 的 `configuration` | **豁免**（无版本载体） | 定案说明见下 |

**豁免说明（`lua` 与 wolf linguist 的 `configuration`，已定案）**：这两族不设 `formatVersion`，判据与 R3
同源——**新增必填键不得拒载既有分发产物**。实测：存量实现里这两族的 `configuration` **一处都没有**
该字段（`lua/main.cpp`、`WolfLinguistProvider.cpp` 各 0 处），把它们设成必填等于当场拒载既有 lua 模块与
既有 linguist 配置。形状演进仍有两处把守：**键名与拼写**由 R6"拒绝未知键"守；**契约语义**由契约
`level` 的抬升守（R22，新增核心 role 必须递增 level）。脚本文件本身不是配置，见上表"Lua 脚本"行。


**[规范性] R3 家族配置文件的版本字段是否必填，按"存量"判**：`formatVersion` 只在**新增必填键
不会拒载既有分发产物**时才可以设为必填。判据是清点：公开分发的 **7 款声库包**各带 4 份 onset
`rule` 文件（共 **28 份**，其中 `assets/eng.json` 的文件名不含 `onset`，按名字搜索会漏四分之一），
顶层键一律只有 `phonemeTypes` 与 `rules`、**没有一份**带 `formatVersion`；而 rule 文件在 Acquire
期解析（`onset/main.cpp:164-178`），解析失败即**包级加载失败**。故该文件的 `formatVersion` 为
**可选**：缺省视作 1，声明则按 R1 校验；形状演进交给缓存代际常量（`RULE_KIND =
"onset-rule-json@1"`，`onset/main.cpp:28-30`），读入端以"拒绝未知键"守住拼写演进。实现见
`OnsetRules.h` 的 `RuleTable::FORMAT_VERSION` 与其注释。

**[规范性] R4 单一真相源**：同一版本/世代常量在一个插件内**只允许一处定义**，其余处写引用；
上限常量与其注释要写清"抬它意味着什么"。 [现状：multig2p 的 `@1` 世代在 `main.cpp:39-40`、
`bundle_version` 的上限在 `Bundle.h:60`，是**两类事实**且无交叉引用——这正是 R5 要立的原因]

**[规范性] R5 形状变更只走世代抬升**：`formatVersion` 只覆盖 `configuration` 与其经路径键引用的
家族配置文件；`configuration` 之外的表/资源由 kind 世代常量承载（R2）。两套世代**各自独立抬
升**，任何一方的**形状**变更必须抬升对应那一份。**不得靠放宽解析器实现形状变更**：同一世代内
字段数照 R10 严格，新增列属于新世代。§2.3 的"拒绝未知键"只守键名与拼写，不构成"形状永不新增
列"的承诺。

## 4. 配置与资源解析

**[规范性] R6 拒绝未知键**：`configuration`、`exports` 与家族配置文件一律拒绝未知键，文案形如
`unknown <对象> key: <key>`。依据 §2.3。 [现状：`OnsetRules.cpp` 的 rule 文件与规则对象已补]

**[规范性] R7 打开失败的错误码**：资源/表文件缺失 ⇒ `FileNotFound`；存在但**打不开或读取中断** ⇒
`FileNotOpen`（对照 `lib/Support/ManifestValues.cpp:20`）。判定按**路径是否存在**（`fs::exists` 带
`error_code`）而不是"开失败就假定缺失"，因为一个开失败同时覆盖两种故障，而它们要不同的处置：缺文件
是包装错了，读不出是宿主环境问题。改错误码是**对外可观测的行为变更**，按"破坏性微调"声明。
[现状：**三处已统一**——chain/s2p/onset 各有一个 `fileError(path, what)` 助手（缺 ⇒
`<what> not found: <路径>`；存在但打不开 ⇒ `failed to open the <what>: <路径>`），读取中断统一为
`failed to read the <what>: <路径>`。**就当前框架而言插件侧这两支是防御性的**：实测（第八轮 Case B、
第十轮 Case C）缺失与读不出的故障都在插件被调用前由框架作答 ⇒ 见 R8 的边界段]

**[规范性] R8 诊断必须带文件身份**：一律"**完整路径（经 `stdc::path::to_utf8`）＋位置**：原因"；
行式文件给行号，JSON 给解析器报的行列。只写文件名在多个包各有同名表时无法定位。 [现状：三处均已
带路径与位置——chain 行号、s2p 行号、onset 的 JSON 行列；本批补前 onset 完全无路径、chain 无行号]

**R8 的已知边界（三条实测，第八/十轮；归因于 2026-09-22 全链路审计第一批**更正**）**：
1. **文件缺失** ⇒ **计量门**先答（`failed to interpret module configuration: failed to size a
   resource`）——**包声明**的资源如此（第八轮 Case A 的对照组），**声库在自己角色配置里点名的资产**
   也如此（第十轮 Case C：删掉副本的 `assets/*_onset.json`）。**更正**：这条路上插件**已被调用**——
   `failed to interpret module configuration` 这半句是 synthrt 在插件 `createConfiguration` **之后**
   加的语境（`PackageLoader.cpp` 的 `withContext`），而 `failed to size a resource` 出自
   **wolf `src/lib/Support/ResourceCache.cpp` 的 `statFile`**：`acquireErased` 在任何解析之前先
   `statFile`，于是插件的 `fileError` 助手轮不到，看上去像"插件未被调用"；
2. **文件在但读不出**（如路径落在目录上、被占用）⇒ **哈希门**先答（`… failed to open a
   resource for hashing`，同文件的 `hashFile`）；两者**都不点名文件**，而错误码分别已是
   `FileNotFound` / `FileNotOpen`（码是对的，缺的是位置——**而位置就在这两处助手，它们手里有 `path`**）；
3. **文件在且能读、但内容非法** ⇒ 才轮到插件作答，此时诊断**带完整路径与位置**（行号 / JSON 行列）
   ——第八轮 Case A（词表多列）与 Case B（onset 规则坏 JSON）即此支。
⇒ 因此 R8 的"点名"承诺覆盖**内容类故障**；**路径类故障（缺失 / 读不出）的点名缺口在
`ResourceCache` 的 `statFile` / `hashFile`**——那是 **wolf 自己的库**（`src/lib/Support/`），不是
synthrt：原先"缺口在框架（synthrt）侧、按项目纪律不在下游补偿"的登记**据此更正为可修项**
（2026-09-22 用户裁定 D1：把 `path` 写进三条报文）。R7 的三处 `fileError` 仍是**防御性**的
（在现网已走过的路线上不可达——`acquireErased` 先于插件自解析做 stat——但语义仍须正确）。

**[规范性] R9 空表是失败，但**只限 TSV 表**，且按**使用者**判**：chain 词典为空 ⇒
`InvalidFormat`（样板 `ChainTables.cpp` 的 `holds no entries`）。**s2p 的 dict/mapping 不判空**——
这是**已定的差异，不是缺陷**：空表＝无映射，词级失败交链上 `fallback`；且现网语料里 s2p 的
`variant` 全为 `direct` 且 `configuration` 为空（不声明表），无样本可据以改判。要改需先有语料证据。
**不外溢到规则文件**：onset 的空 `rules` 是目标合法语义（"整条无匹配输出全 `false`"，§3.3），
实现对它故意不判非空（只对 `phonemeTypes` 判非空）。

**[规范性] R10 同一世代内字段数双向严格**：缺列与多列都拒绝。样板 `S2PTables.cpp:22-38`。
 [现状：**已对齐**——chain 与 s2p 都拒绝多列与缺列，且四处文案同款（`missing tab separator` /
`multiple tab separators` / `empty first column` / `empty second column`）；对齐前 chain 把首个
tab 之后全部归入发音，会经 `splitPronunciation` 产出含 tab 的音素名]

**[规范性] R11 文本规整**：只剥首行 BOM 与行尾 `\r`；不支持注释行；**不得**对键列做隐含 trim
（空白是数据的一部分，要拒绝就显式拒绝）。

**[规范性] R12 读完整性**：读取循环结束后检查 `file.bad()`（整读 `rdbuf()` 的写法同样要在读后检查）。
样板 `S2PTables.cpp:97-99`。 [现状：chain、s2p、onset **均已检查**]

**[规范性] R13 JSON 读取下沉到单一实现**：助手放库侧并带"上下文形参"，以保住各处既有诊断措辞；
下沉是公开面**增项**（向后兼容）。注意实现细节差异（如助记名、宽松实参）要在下沉前对齐，不要
按印象假设冲突。 **[决定：本目标内暂不实施]**——下沉的落点在库侧（`include/wolf` + `src/lib`），
超出"只改插件内部、不动整体框架"的授权范围；且需先对齐各处助手差异（P4 未定）。若要做，应作为
**独立批次**并单独征求同意；在那之前，插件间的同形助手**各自自包含**（本仓既有布局：无共享的
插件私有头）。

**[规范性] R14 大小写折叠按 Unicode 表**，或把声明面收窄到确有把握的区间；禁止用码位奇偶之类
的启发式冒充 Unicode 语义。 [现状：`ChainTables.cpp:41-43` 在 U+0138–U+0149 与 U+0178–U+017E
区间方向错误；另注意声库包的 `phonemeTypes` 里存在**仅大小写不同**的类型名（如 `n`/`N`、
`ap`/`AP`），故键比较**必须大小写敏感**]

**[规范性] R15 值作路径前先过闭集**：形如 `ref` 的短标识符在拼入路径前必须经白名单校验。样板
`PinyinEngines.cpp:145-147` + `:173`。 [现状：`pathFromManifest` 只做反斜杠归一，不净化 `..`
（`ManifestValues.cpp:74-83`）⇒ bundle 的 `files` 可越界]

## 5. 错误、诊断与日志

**[规范性] R16 失败按三级通道归属**：词级 → `LinguistWordOutput::error`；批级 → `start` /
`startAsync` 返回 `Expected`；加载级 → Package `Load` 失败（依据 `linguist-runtime.md:249-255`）。
**异常路径必须走批级 `Expected`**，不得退化为"空产出且无 error"（`:257-262`）；L4 组合层不新增
加载期失败事由（`:264-267`）。 [现状：lua 的**运行期**失败沿用加载级的 `InvalidFormat`
（`LuaSandbox.cpp:358`、`:376`、`:410`），与分级不吻合]

**[规范性] R17 词级诊断只走 `hitStage`**，不得写入词级 `error`；兜底成功时 `error` 必须为空且
`hitSource = fallback`（§3.3 兜底段落、`linguist-runtime.md` §6）。

**[规范性] R18 日志**用 `wolf::logCategory()`；历史沿革写在库侧一处，插件侧只写不变量。

## 6. 线程、取消与生命周期

**[规范性] R19 取消返回与输入等长的结果并逐词标注**（契约 `G2PApiL1.h:114`"失败词保留位置"）。
 [现状：pinyin `main.cpp:221-226` 返回空 words；multig2p `:180` 先 `resize` ⇒ 两者不一致]

**[规范性] R20 停止谓词统一为 `m_stopRequested.exchange(false)`**（正例 `s2p/main.cpp:135`、
`onset/main.cpp:46`）。

**[规范性] R21 禁止静默丢项**：对契约枚举的 `switch`，**穷尽列举**（依赖 `-Wswitch` 编译期兜底）
**或** 显式 `default` 报错，两者皆可；**唯独禁止 `default: break;`**——它会关闭 `-Wswitch`，把
现网赖以兜底的编译期检查变成静默。 [现状：**第十轮全量清点，四处全部合规**——chain `main.cpp:478`
的 `classifyLyric`（3/3 `LyricVerdict`）、chain `main.cpp:499` 的 `step.kind`（5/5 `StepKind`，
枚举本体在同文件 `:42-48`）、pinyin `main.cpp:249` 的 `verdicts[i]`（3/3，含 `Accept`：**逐输入索引
必然产出一个词**）、`LinguistExecutiveImpl.cpp:274` 的 `hitSource`（5/5，`Unspecified` 是其 case
**内**的空操作）；且 `src/plugins` 内**不存在任何 `default` 分支**，故本条的禁项不成立。原"pinyin
对 `LyricVerdict` 无 `default` 且会落到循环尾丢词"的描述**已随 R5 的分批重写失效，此处更正**]

## 7. 契约身份与分派

**[规范性] R22 role 的命名空间**：`linguist/` 前导段的 role 名属本契约族保留命名空间；Level 1
核心集恒为 `linguist/g2p`、`linguist/s2p`、`linguist/onset`；**新增核心 role 必须递增契约
`level`**，不得以 `configuration` 键或资源 `formatVersion` 承载（L1 的 linguist `configuration`
必须为空 object，`WolfLinguistProvider.cpp:424-435`）。**无 `linguist/` 前导段的 role 按
[domain-contract](linguist-domain-contract.md) §5「其他」放行，不判加载失败**（`:241`）——
"未识别 role 静默放行"（`WolfLinguistProvider.cpp:175-204` 三支无 else）**是承重行为，不是缺陷**，
不得改成拒绝。

**[规范性] R23 `level` 判定写成集合成员判定**：不得写成与单一常量的等值比较。 [现状：全仓 15 处
硬钉 L1；其中 `WolfLinguistProvider.cpp:47, 79, 102, 316, 383` 最致命——L2 组合被 L1 声库导入时，
synthrt 在加载期就取导入选项（`lib/Core/PackageLoader.cpp` 的 `createImportOptions`），而该处要求
三元组全等（同文件的 `validatePayloadIdentity`），于是直接"unsupported contract"加载失败，而不是降级]

**[规范性] R24 manifest 与代码的对应关系由构建或测试保证**：`plugin.json` 的 interpreters 条目
应由受支持档位集合**生成或比对**，不手抄（先例：`stub/CMakeLists.txt:45-48` 的
`configure_file(plugin.json.in)`）。插件返回的 payload 由框架按三元组比对（synthrt
`lib/Core/PackageLoader.cpp` 的 `validatePayloadIdentity`），故"返回恒等"是既有保障，"声明与代码一致"仍需本条。
 [现状：插件自写的 `plugin.json` 与 `create()` 接受的集合无自动比对]

## 8. 演进检查清单

1. 新增 **level/变体**：改 `plugin.json` 三元组、代码 `VARIANT`/接口常量、目录名、CMake
   `CATEGORY`、契约头常量；`create()` 用**一次组合判定**并让诊断含三分量（正例
   `chain/main.cpp:898-902`、`WolfLinguistProvider.cpp:45-51`；偏差样本 `lua/main.cpp:433-443`）。
2. 同插件多变体：**共用参数化 Configuration**（正例 `stub/StubExecutives.cpp:98-104`、
   `singerproviders/stub/main.cpp:32-37`；反例 `lua/main.cpp:38-56` 两份同形类）。
3. 改 **TSV 表语法**：抬 kind 世代常量（R5）。
4. 改 **家族配置文件格式**：按 R3 判必填与否，并同步 schema 段与实现。
5. 加 **role/层级**：role 名与 Depth 阶梯收成常量单点，并更新 validator 与闸口。
6. 新增 **枚举值**：检查所有 `switch`（R21）。
7. 清理**死代码**前先全仓 grep（样板：`MappingTable::targets()` 只有声明与定义命中）。
8. 改**分批/邻接语义**：后端**自己**按"原词序中的极大连续可转换段"分批，**不得假定调用方已切分**
   （依据：链 `pipe-chain` 的 `model` 步已按段切分 ⇒ 后端是否自切在链下**不可观测**，只有**直连契约**
   的宿主能触发；反例：pinyin 曾把过滤后的词拍平成一张表，使被否决词两侧的字符互成邻接）。
9. 新增**被引用的文件**（表、规则、模型）：先按 R8 边界确认故障归属——**内容非法**归插件诊断（必须
   带路径与位置），**缺失/读不出**归框架计量/哈希门（不点名文件，勿在插件侧重复实现"点名"以图补救），
   并检查该文件是否需要按 R2/R5 走版本载体与世代常量。
10. 跑**验证通道**（§2 末段）：两棵树增量构建看告警 + `ctest`（含 `test_ConvertedPackages`）；
   新增插件或变体时同时补 `add_auto_test`。骨架统一（§2 首段）不是待办，别把它当工作项。

## 9. 格式与注释

**[规范性] R25** 全部 C++ 文件符合仓根 `.clang-format`（LLVM 基、4 空格、100 列、
`PointerAlignment: Right`、`SortIncludes: Never`）；CI 暂无格式门禁，靠本地对齐与评审守。

**[规范性] R26** 注释只写不变量与"为什么"，不写历史沿革副本；中英按文件现状，不整篇改写。

## 10. 未决项

| # | 未决 | 现状 |
| :-- | :-- | :-- |
| P1 | ~~`lua` 与 wolf linguist 的 `configuration` 既无 `formatVersion`、规格亦无豁免条款~~ | **已定案（2026-09-21，用户裁定）：写明豁免**——见 §3 的 R2 表与"豁免说明"；不设必填，理由是新增必填键会拒载既有分发产物 |
| P2 | ~~`level` 硬钉是否改成集合判定、manifest 是否生成化~~ | **已定案（2026-09-21，用户裁定）：采纳分批**——manifest 生成化先做（先例见 R24 的 `stub` `configure_file`），`level` 集合判定等真要上 L2/L3 时再做；本轮只定案，不改代码 |
| P3 | ~~R7 错误码与 R9/R10 严格化会改变对外可观测行为~~ | **已实施（2026-09-21，`41d7fc1` / `1801798`）**：三处 `fileError` 的两支、chain/s2p 的缺列·多列·空列、chain 空表 `holds no entries` 均已在代码里（`ChainTables.cpp`、`S2PTables.cpp`、`OnsetRules.cpp`）；原"需先清点存量词典（空表、多列）再动手"的前提已不成立（2026-09-22 审计复核：§4 的 R7/R9/R10 `[现状]` 段与代码一致） |
| P4 | R13 的下沉落点与助手签名 | 待定 |
