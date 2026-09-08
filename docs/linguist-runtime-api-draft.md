# 语言域运行时 API 与宿主接入规范（Level 1 · 草案）

> **状态**：本文钉语言契约族的 **C++ 级运行时可观察语义**与**宿主接入面**——
> 语言契约 Level 1（`linguist-level-1-draft.md`）钉类别/三元组/exports/import
> options/Variables 与组合裁决；本文钉其声明「不钉、归运行时 API 规范」的全部
> 字面：pipeline extension 取用、executive 方法面与任务异步纪律、错误与降级
> 语义、诊断通道。变体 `configuration` 与资源格式归变体文档
> （`linguist-g2p-variants-wolf-draft.md`）；资源加载去重回 wolf 侧缓存设计
> （`linguist-g2p-resource-cache-draft.md` 缓存对本文完全透明）。上位规范：
> spec 2.4（`ds-spec-2.4.md`），冲突时以 spec 为准。
> 参考宿主：ds-editor-lite（v3 facade 形态：引擎持有的 Runtime + 每声库
> Session + 模块层薄 facade）。
>
> **重排注记**：2026-08-30 版式重构——仅分段分点，语义零改动
> （方案与决策台账见 `docs/plans/linguist-docs-restructure.md`）。
>
> **修订注记**：2026-09-05 消费方对齐修订——§2.1 增缺省语言解析步（D29，
> 台账见变体文档 §8.12）。
>
> 锚点口径：本文 synthrt 框架锚点取 origin/main 新框架（观察点 `a060af0`，
> 树内前缀 `synthrt/`；2026-09-05 复核 main HEAD 未漂移），wolf 锚点取
> `cecedba`（随文另注宿主编译期链接关系；2026-09-05 复核未漂移、工作区干净），
> lite 消费点锚点取 lite 工作区 `src/**` 实测（同名 B1b/B1c 迁移注记处）。

**本文词汇分层**：「宿主可见的调用面」= 规范性要求（宿主升版本依赖它）；
「实现注记」= 非规范性现状对照；「待定」= 台账未决项（§7）。

## 1. 宿主初始化序列

宿主进程接入语言域的完整序列（每步一步、不得换序）：

1. **链接 wolf**（编译期）：wolf 是库不是程序（wolf `CMakeLists.txt:64`
   `find_package(synthrt CONFIG REQUIRED)`）。链接即生效：wolf 内静态注册
   对象在 `main` 之前把 `linguist` 类别连同 provider factory（插件 IID
   `org.openvpi.wolf.plugin.LinguistProvider`）挂入 synthrt 类别注册表
   （狼仓 `LinguistContrib.cpp:94-95`；注册表为 `stdc::StaticRegistry`，
   main `ContribCategory.h:158`）。**宿主不链接 wolf ⇒ 无 linguist 类别
   ⇒ 含语言贡献的包整体被拒**（main `PackageLoader.cpp:1210-1212`）。
2. **构造 `SynthUnit`**（main `SynthUnit.h:27-28`）：构造即收集注册表全部
   类别（`SynthUnit.cpp:14-31`），先于任何包解析（spec 2.4:250）。
3. **配置路径**：`setPackagePaths`（包搜索路径；**公共语言包路径须排在声库
   内置包路径之前**，依据见 level-1《公共语言包》）、按类别
   `setPluginPaths`（含 `linguist` 类别的 wolf provider 插件目录；该目录为
   宿主部署面，wolf 随宿主发行物落位）。
4. **加载包**：`openPackage(path, Load)`（main `SynthUnit.h`；`DataOnly`
   只读清单不碰运行时）。收据为 `PackageHandle`——生命周期即运行时资源
   归属，句柄归零即包释放（正常卸载语义 spec 2.4:458/:460；失败回滚按
   :446/:456 析构反序撤销）。

> 实现注记（非规范性，lite 现状对照）：v2 线 lite `SynthrtEngine::initialize`
> （`SynthrtEngine.cpp:298-468`）的等价物切点——插件根目录与 ONNX 驱动的
> `initializeRuntime/initializeG2pOnnxDriver` 保留宿主侧职责形态不变；
> v2 的 `g2p::Manager::addPluginPath`/`VoicebankSession.refresh` 由本节 1-4
> 步取代；`deferLanguageModels`/预热等价物见 §4.3。

## 2. 解析面：（歌手, 语言）→ 链路

### 2.1 解析算法

给定歌手贡献与 iso 语言句柄（`cmn`、`jpn`…），解析到该歌手的语言链路。
本节诸面以包完成 `Load` 为前提（`DataOnly` 下 import binding 为空，
main `ContribSpec.h:101` 注记）：

1. `PackageHandle::contribution("singer", <singerId>)` 取 `SingerSpec`
   （main `PackageHandle.h` contributions/contribution/resolve 三面）；
2. 自 spec 取 extension：
   `ContribSpecExtension::findFromSpec<WolfPipelineExecutive>(spec)`
   （返回基类指针，取用后经契约 `as<WolfPipelineExtension>()` 下转），
   trait ID `org.openvpi.wolf.extension.LinguistPipeline`（挂载判定见
   level-1《Singer 侧配套·运行时聚合》）；**无此 extension ⇒ 该歌手未
   声明任何语言导入，语言功能整体不可用**（非错误，见 §4.1）；
3. 遍历 `spec.imports()`（保序）：取 `role` 以 `linguist/` 为前缀、且其
   binding 目标 linguist 贡献 ID 的**语言句柄**（ID 首字段，见 level-1
   《语言贡献 ID 形态约定》）恒等者——其 `role` 即所求。至多一条命中
   （唯一映射约束，level-1《Singer 侧配套》）；无命中 ⇒ 该语言不可用。
   role 后缀=句柄（level-1《Singer 侧配套》，D29）成立后，亦可直接按
   role 后缀命中；按目标贡献句柄匹配为稳健算法，不依赖 provider 侧
   后缀校验落地；
4. `extension->createPipeline(options)` 得 `WolfPipelineExecutive`，
   `createLinguist(role, {})` 得该语言的 `LinguistExecutive`（越界 role
   报 `InvalidArgument`，狼仓 `WolfPipelineExecutive.cpp:22-26`）；
5. **缺省语言解析**（宿主「跟随歌手」语义，D29）：读 Singer 声明的
   `defaultLanguage` 扩展键（iso 语言句柄，见 ds-singer 契约《贡献条目
   与类别追加字段》），经 role `linguist/<值>` 直接命中语言导入；该键
   未给出或不命中时由宿主回退（ds-singer 契约建议声明序第一个句柄）。
   解析结果仅用于宿主决定 G2P `languageId` 的缺省取值，语言契约不另设
   缺省语言词汇。

### 2.2 宿主侧缓存纪律

- role 集合在包 Commit 后不变（level-1《运行时聚合》），故（歌手 →
  pipeline executive）与（歌手+语言句柄 → role）两级映射可全程缓存，
  仅在包加载/卸载事件时失效；（歌手 → 缺省 role）解析结果同理可缓存
  ——其输入 `defaultLanguage` 与 role 集合同为 Commit 后不变量；
- 推荐宿主为每歌手惰性建一次 pipeline、每条语言惰性建一个
  `LinguistExecutive`——对齐 lite 现状 `ensureLanguageReady` 的
  「每语言就绪一次」调用习惯（`GetPhonemeNameTask.cpp:137-151`）。
- executive 生命周期：归 pipeline 所有、父子由 Executive 监督树托管
  （main `ContribExecutive.h:68-77` adoptChild / `createChild(role,
  runtimeOptions)`），宿主可提前 delete 子 executive（DiffSinger 先例
  `DiffSingerApiL1.h:45-47` 同此约定）。

## 3. LinguistExecutive 方法面

### 3.1 输入模型：逐词分层锁定（人工修改友好）

编辑器的歌词、发音、音素三层均可在任意时刻被人手修改（lite 实测：
`Note.h:39-55` 的 pronunciation/phonemeNameSeq 双轨 Original/Edited）。
因此**不**提供「只能整链重算」的入口；统一为一个任务、逐词标注已锁层：

```
LinguistWordInput {
    lyric: string                 // 必填（保留记号词的直通属宿主预过滤，不进批，见下文）
    pronunciationLocked: bool     // 为真时 pronunciation 视为人工锁定层
    pronunciation: string         // locked 时必填（空格定界音素串或待转换发音）
    phonemesLocked: bool          // 为真时 phonemes/onsets 均视为人工锁定层
    phonemes: array<string>       // locked 时必填
    onsets: array<bool>           // locked 时必填（与 phonemes 等长）
    languageId: string            // 条件必填，同契约 G2P Variables《运行时路径》
}
```

接续规则（逐词独立判定）：

| 锁定状态 | 执行体动作 |
| :-- | :-- |
| 全未锁 | 全链：G2P 段 → S2P 段 → Onset 段 |
| 仅锁发音 | 跳过 G2P 段，由锁定发音直接走 S2P 段 → Onset 段 |
| 锁音素 | G2P/S2P 全跳过，透传 phonemes/onsets（仅做形状校验，
| | 非法按该词 `error` 失败产出，值域见 §3.3）；即使 G2P
| | 段被跳过，`candidates` 仍可经 DictQuery 面按词补查
| | （宿主工具需要候选时，见 §4.2） |

「只需发音不要音素」（填词面板现状 `G2pService::convert`）是同一任务经
options 关闭下游段的特例（§3.2 options 表），不占单独入口。

入批词表由宿主自由裁剪：保留记号词（`SP`/`AP`、连音 `-`、拆音续音符
`+` 串等）的直通是**宿主预过滤职责**，此类词不进批、不在转换结果中占位
（lite 现状两侧同此：`GetPronunciationTask.cpp:156-161`、
`GetPhonemeNameTask.cpp:119-129`；宿主自合成 SP/AP 音素与空结果）。

时长偏移（lite `phonemeOffsetSeq` 手改面）属推理 DSP/时长模型输入域，
**不在本文范围**——语言链只产 phonemes 与 onset 布尔位。

### 3.2 任务与调用形状（复用 main ITask 面；多实例并发 task 为本规范新增形状）

所有可执行工作以 **Task 对象**呈现，沿用 main 的 `ITask` 接口面
（`synthrt/Task/ITask.h`）：`TaskStartInput`/`TaskResult` 类型化载荷、
`start()` 同步执行、`startAsync(input, callback)` 异步（默认实现在自建
worker 线程上执行 start 并于其上触发回调）、`stop()` 取消、
`waitForFinished()` 等待、五态 State——单 task 对象同时只允许一个异步
执行（`ITask::startAsync` 已有运行保护）。

> 实现注记（非规范性）：main/dsinfer 的既定先例是**一执行体一任务**
> （任务面直接长在 executive 上，`DurationApiL1.h:126-144` 等）；本节
> 「executive 量产多个相互独立、可并发入队的 task」相对先例是新增形状。
> ITask 不在执行体监督树内（监督树只收 ContribExecutive，main
> `ContribExecutive.h:66-99`），executive 的 `quit()`/`wait()` 聚合其
> 名下 task 的停等属 wolf provider 实现义务（现行 quit/wait 为空实现）。
> 另 wolf 现状差距：`LinguistExecutive` 未聚合推理执行体、三 inference
> 契约类型化 IO 未落地（G2P/S2P/Onset API 头仅 interface+level 常量，
> 狼仓 `include/wolf/Api/Inferences/`）、现行 ExecutiveFactory 直接实例化
> API 类（狼仓 `WolfLinguistProvider.cpp:47-50`）——落地时须改落 provider
> 具体实现类并聚合子执行体；§3 方法面属发布级目标语义。

```
LinguistExecutive（抽象，provider 实现）:
    Expected<LinguistConvertTask *> createConvertTask()   // 唯一任务门类
    —— per-singer 语言执行体持有 G2P/S2P/Onset 子执行体的绑定引用；
       宿主按需创建任意多个 convert task（相互独立、可并发入队）。

LinguistConvertInput  : TaskStartInput { words[ LinguistWordInput ],
                                         options{ needPhonemes=?, needOnsets=? } }
LinguistConvertOutput : TaskResult     { words[ LinguistWordOutput ] }
LinguistWordOutput {
    pronunciation: string            // 通过了 G2P 段或锁定透传
    candidates: array<string>        // 首个即主发音；锁定层不回填
    mode: enum(convert/copy/skip)
    error: enum                      // 值域与触发次序见 §3.3
    phonemes: array<string>?         // options.needPhonemes 授予
    onsets: array<bool>?             // options.needOnsets 授予
    source: Diagnostics              // §5 诊断面（每个词的来源标注）
}
```

- **批量是主形状**：宿主按音符分片组批（G2P 段现状按语言分组，
  `G2pService.cpp:95-110`；S2P 段现状为逐音符 + 就绪集）；逐词失败不
  中断批（词级 `error` 非空即败，词序位保留，沿袭契约共现约束）；
- `stop()` 语义：请求当前批合作式取消（实现须在词边界响应）；
  `waitForFinished()` 不改变 task 存活；executive 生命周期沿用监督树，
  task 的停等由 executive 的 quit/wait 聚合（见上实现注记）；
- 线程纪律：task 的 worker 线程由实现自持；**callback 在实现线程上触发**，
  宿主须自行 marshal 到 UI 线程（main ITask 既定约定，宿主现状
  `InferTask` 模式已同构）。

### 3.3 失败语义

- 词级 `error` 值域 = 契约 G2P 六值（宿主面输出）；后段统一汇报通道：
  S2P/Onset 段无独立错误通道（level-1 既定），「后段无产出」表现为该词
  `phonemes` 为空且不带 `error`——与 level-1「未命中不是失败」口径一致。
  空产出的告警/失败判定归宿主策略（lite 现状即将空 phonemes 判失败并
  告警，`GetPhonemeNameTask.cpp:176-181`）；实现仍应使真正的异常路径走
  词级 `error`，使宿主可区分「正常未命中」与「异常静默」；
- 手改层形状非法（phonemes 与 onsets 不等长、空元素）→ `InvalidInput`；
- 语言路由失败（运行时 `languageId` 与绑定不一致等）→ 按契约《G2P
  Variables·运行时路径》判定次序产出；
- 执行体级失败（驱动不可用、模型会话失效）：整批以 Expected error 返回，
  宿主按 lite 现状「该语言整组保原词」策略降级（`G2pService.cpp:137-149`
  同款落点，仅信源从 error string 换成结构化 Expected）。

## 4. 宿主接入三场景对位

### 4.1 声库缺语言（硬失败，含失败部分枚举）

**定案（用户拍板）：加载期硬失败，不静默降级**；配套义务=失败部分必须
逐条点名可呈现。宿主接入形态：

1. 宿主用 `DataOnly` 先扫全部候选包（不碰运行时，仅清单解析面
   `manifestDeclaration()`/`dependencies()`），按 spec 2.4《依赖项》的选择
   规则自算依赖闭包，缺集 = 闭包差集；
2. 用户界面呈现粒度为「声库 × 语言」：缺 `wolf/lang-cmn` ⇒ 提示「声库 X
   的普通话链需要安装 wolf/lang-cmn」并给安装入口（受影响语言清单由宿主
   用缺包 ID 反查声库 imports 自算）；
3. 若用户仍选择强行装入缺依赖声库：`Load` 失败（框架硬语义，spec
   2.4:157-159 强制依赖 + :444 失败即整次失败）。

   点名承载力现况（main `a060af0` 亲验）：

   - 缺包在依赖求解层即失败且错误文本携带**依赖包 ID**与发起方包 ID
     （`PackageLoader.cpp:692-693`「no installed Package satisfies
     dependency <id>」+:748-749 上下文链）——「缺哪个语言包」已可点名；
   - import 级（失败 role/目标 ref）点名不携带（:782-784 仅
     「module import target does not exist」）；
   - 宿主用缺包 ID 反查声库 imports 清单即可自算受影响语言，
     增强列台账 D-R3。

**粒度铁律（对抗审定案）**：硬失败的单位是**包**——声库声明的任一语言
包缺失，该声库 `Load` 即整体失败。

- 「其他正常」仅指依赖齐全的其他声库，不是同一声库的其余语言；
- 宿主按缺集把该声库标为「待安装依赖」、阻止装入并呈现逐语言缺口报告；
  补齐依赖后即恢复，无需重装声库；
- 「缺语言仅该语言灰显、同声库其余语言照常唱」需 spec 2.4 增补可选/软导入
  后方能承诺（台账 D-R1 备案，本轮不提供）。

**包在但不可加载（2026-08-31 增补，D-R5）**：依赖求解选中候选后、解释器
期的失败（典型：pipe-chain 资源 `formatVersion` 超出当前插件支持上限）
不得回退其他候选版本（spec 2.4:396），整链按上文粒度铁律硬失败。宿主
呈现口径与缺包同粒度（声库 × 语言），文案区分两种引导：

- **升级编辑器**——包属正常发布、资源格式版本确需更新解释器能力时；
- **安装兼容版本**——包属 `compatVersion` 虚报（发布纪律失守的包缺陷）
  时，从资源仓安装真正满足目标版本的旧版并存（spec 2.4:402 多版本共存）。

发布侧联动（公共包资源格式版本抬升视同破坏性更新）见 level-1《公共语言
包·版本纪律》与变体文档 §3 执行细则——正常发布纪律下，本场景应已前移
收敛为「缺依赖」呈现，本节是纪律失守时的呈现兜底。

### 4.2 多音字人工选择（宿主工具面）

`candidates` 覆盖自动转换；人工查词另走 DictQuery 契约（level-1 已立）：
宿主工具（多音字选择 UI）直接创建 DictQuery 模块执行体批量按键查询
（`found/values`），不经 linguist 链路——alignment with「不占用语言
role 名额」。锁音素词按键补查候选亦走此面。

### 4.3 语言模型加载时机

- 包 `Load`（Acquire）只做声明解析与轻资源装配；模型会话建议惰性到
  `LinguistExecutive` 创建或 task 首调（实现自由；spec 2.4:382 允许
  缓存解析结果，资源去重归缓存文档）；
- 宿主预热等价物（lite 现状 `warmUpLanguageModels`）：宿主后台线程对
  已加载歌手的 languages 各建一次 executive 即可，不另设 API。

## 5. 诊断通道

承 level-1 台账 D14（`g2pContext/g2pSource` 契约外字段的归宿）：诊断经
每词 `source` 载荷回传，不进词级 error。枚举（Level 1）：

- `g2pContribution`：产出该词发音的 G2P 模块贡献 ID——
  - 对位 v2 的 `g2pId` 日志信源（`GetPronunciationTask.cpp:264-265`）；
  - v2 的 `g2pContext`/`g2pContextVersion` 折叠进贡献 ID 的规范形态
    （包 ID@版本），宿主可经贡献查包元数据；
  - 对位 lite 设置页按（g2pId, context, version）三元组定位的显示需求
    （`G2pInfoWidget.cpp:70-111`）；
- `hitStage`：`dict|model|rule|fallback|locked`（锁层透传标注 `locked`；
  `dict|model|rule|fallback` 取自 G2P Variables 可选输出 `hitSource`，模块未提供时为空）——
  诊断角色继承 v2 `g2pSource`（其值域为资源语境 `official`/`voicebank`，该语境现由本节 `g2pContribution` 承载；`hitStage` 为词级命中方式，新粒度）；
- `languageBinding`：实际生效的语言贡献 ID（绑定期/回落后对账用）。

**必须是纯数据、可为空集**：宿主不读不损功能；实现可降级为空（实现自由
但有损可诊断性——宿主诊断日志依赖，lite 已在用）。

## 6. 与既有文档的分工与迁移

- level-1《Singer 侧配套·运行时聚合》节保留规范性存在性条款（trait ID、
  挂载条件、roles 集合冻结、越界报错）；「C++ 级 API 形状由运行时 API
  规范定义，本文不钉」的指向对象即本文；
- 变体文档：本文不涉及任何 `configuration` 键汇；变体实现须按本文 §3
  方法面落地各自的 executive 实现；
- 缓存文档：缓存对运行时调用面完全透明（其透明性底线即「不改变本文任何
  可观察行为」）。

## 7. 决策台账（本轮）

- **D-R1（降级语义，用户拍板）**：缺依赖硬失败 + 失败部分逐条点名；
  宿主经 DataOnly 预算缺集实现「声库 × 语言」级缺口呈现与安装引导。
  - **已知边界（第七轮对抗审定案）**：硬失败粒度为包——缺任一语言依赖 ⇒
    该声库整体拒装，「同声库内其余语言照常」在硬失败模型下不存在；
  - 软降级（框架 import 绑定失败降级为 Unavailable、spec 增补可选/软导入）
    列为备案演进项，需时单独立项。
- **D-R2（方法面定案）**：单一 convert 任务门 + 逐词分层锁定——
  - 替代了「G2P-only / full-chain / S2P-only 多入口」草案，依据 lite 实测
    双轨存模（`Note.h:39-55`）与「任意阶段手改」指令；
  - 锁音素词的候选补查经 DictQuery 面外置，不塞进 convert。
- **D-R3（部分坐实，增强项）**：缺依赖点名的分级现况——
  - 包级已成立：错误文本携带依赖包 ID 与发起方包 ID
    （`PackageLoader.cpp:692-693/:748-749`）；
  - import 级点名（失败 role/目标 ref）不携带（:782-784）；
  - 宿主可按缺包 ID 反查声库 imports 自算受影响语言、不构成阻塞；
    向 main 提「import 解析失败文本携带 role+locator」增强列为独立小项。
- **D-R4（待定）**：`stop()` 在词边界响应的时延上界不量化为规范性数字
  （实现注记级建议：百毫秒内），待 wolf 实现落地后按实测回写。
- **D-R5（2026-08-31，包在但不可加载）**：§4.1 增补「依赖求解选中后、
  解释器期失败（如 chain `formatVersion` 超限）」场景——不回退（spec
  2.4:396）、与缺包同粒度呈现、文案区分升级编辑器/安装兼容版本；与
  D-R1 缺语言场景共用「声库 × 语言」呈现面。发布侧联动见变体文档
  §8.11 D28 与发布设计文档 §4。
- **D-R6（2026-09-05，缺省语言解析）**：§2.1 步骤 5 增宿主「跟随歌手」
  解析——决策本体（role 后缀=语言句柄 + `defaultLanguage` 经 role 命中）
  见变体文档 §8.12 D29；本文只钉宿主解析次序与 §2.2 缓存纪律。动因：
  编辑器「跟随歌手」语义（`PianoRollContextMenuController.cpp:127-140`
  语言菜单）依赖歌手级缺省语言，原目标设计未定义其去向。
