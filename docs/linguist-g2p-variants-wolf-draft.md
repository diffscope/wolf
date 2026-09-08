# wolf 语言域收录变体参考（G2P / G2PModel / DictQuery / S2P / Onset）

> **状态**：本文承载《DiffSinger 语言契约 Level 1（修订版）》迁出的全部实现族细节，
> 属 **wolf 实现文档**而非对外契约；待迁入 wolf 仓库 docs/ 后删除本仓暂存。
> 上位契约：`linguist` 类别、`org.openvpi.wolf.*` 契约面、`level` 纪律见语言契约 Level 1；
> 上位规范：spec 2.4（`docs/ds-spec-2.4.md`），冲突时以 spec 为准。
> 本文修订以 step 词汇、配置键汇、资源布局为主，一般不触碰对外契约。触契约的变更
> 仅两轮四项：第四轮（2026-08-29）推理侧由单一 G2P 接口扩为三接口（G2P 宿主面 /
> G2PModel 后端面 / DictQuery 查询面，原 `nn-onnx` 与拟议 `algo-dsdict` 两枚 G2P 变体
> 移出，台账 D15-D18），第五轮新增《自动组合校验的分层落点》节与 S2P / Onset 可选
> exports 词汇（D22）；`lstm-onnx` 待收录备案（D20）与命名定案（D21）不触契约
> （台账 §8.7 D19-D23）。
> **重排注记**：2026-08-30 版式重构——仅分段分点，语义零改动（方案与决策台账见
> `docs/plans/linguist-docs-restructure.md`）。
> **修订注记**：2026-09-05 消费方对齐修订——新增 §8.12（四平面复核零漂移 +
> 台账 D29-D30 + §8.9 备案区批量关闭），契约侧落点见语言契约修订注记。

## 1. 总则

### 1.1 变体治理

- 变体名属实现方命名空间：
  - 本契约族收录变体由 wolf 维护：G2P 宿主面保留 `pipe-`/`algo-` 类前缀；G2PModel
    后端面变体按「家族-后端」复合命名（首个为 `multig2p-onnx`，家族直名保留，台账
    D16）；DictQuery 查询面变体按词典族命名（首个为 `dsdict`）；全部裸变体名为
    wolf 官方保留（`nn-` 前缀随 D15 退役，不再出现于任何接口下）；
  - 第三方引擎/家族按 spec 2.4《关于 `interface` 与 `variant`》使用反向域名变体（如 `com.vendor.myengine`）。
- variant 编码成单个 `segment`（结构标识符文法允许连字符；`pipe-`/`algo-` 前缀与
  「家族-后端」复合等命名惯例见上条），**变体名即分派键**：
  - 框架按 (`interface`, `level`, `variant`) 三元组全匹配选择 provider，
    `configuration` 内容不参与 provider 选择（spec 2.4《模块 provider 发现》）；
    选择按「目录顺序 + 目录内文件名顺序」形成的全序扫描进行，首个三元组全匹配
    的 provider 当选、后续出现的相同三元组不参与选择（spec 2.4:576；实现见
    synthrt `ContribPluginFactory.cpp:74-83`），三元组命不中即「找不到提供者」
    加载失败（synthrt `PackageLoader.cpp:769-774`，Probe 阶段报
    `FeatureNotSupported`），不存在运行期再分派；
  - 「同一变体名只许一个 provider 插件承载」是 wolf 对本契约族收录变体的收录
    纪律（使公共包的 provider 选择结果确定），并非框架约束——框架侧只规定
    首个全匹配者当选；
  - 模块 ID 区分同一引擎下的具体产品打包（如 `multig2p`）。
- 推理会话能力经独立驱动注入：
  - 变体名中的字样只声明该家族绑定哪类驱动；
  - 会话创建本身不属变体职责。
- spec 2.4 允许变体随时新增；新增不触碰契约文档。
- `error` 值域是契约词（G2P Variables 六值、G2PModel Variables 七值，见契约各章；
  DictQuery 无错误通道），变体只可援引、不得自定义新值。

> 实现注记（非规范性），分三点：
>
> - **承载宿主**：收录变体的现行承载宿主为 synthrt `language-level-1` 分支旧栈——TaskPlugin 键机制（`include/synthrt/G2P/Task/TaskPlugin.h:17-32`，分派键为插件 `key()` 字符串而非 (interface, level, variant) 三元组）；以 spec 2.4 provider 形态落地属迁移事项，新旧 key ↔ variant 对照见 D10；
> - **provider 缺位**：wolf 仓目前不随附任何推理/查询 provider（G2P/S2P/Onset/G2PModel/DictQuery 皆缺，`src/plugins/` 仅 linguistproviders 一家）；
> - 本文锚点口径与复核基线见 §8.4 对照面。

### 1.2 configuration 公约

`configuration` 由 `variant` 全权规定（spec 2.4《三个语法块的归属》），契约不设统一外层形态；
本族变体直携自身键汇。约定两条：

1. **路径解析**：
   - `configuration` 中的路径以模块声明文件目录为基（已完成 `${vars}` 展开）；
   - `${vars}` 展开由 Loader 在声明交付类别与解释器前统一完成，解释器不得对已展开
     字符串再次套用展开规则；
   - 经路径键定位的家族配置文件，其内部路径再以该文件自身目录为基。
2. **格式版本自验**：
   - 配置（内联键汇或家族配置文件）顶层可携 `formatVersion`（单调递增正整数）；
   - 解释器声明自身支持上限；读入超过上限即加载失败、不得回退，诊断携带
     「声明版本 > 支持上限」与升级指引；
   - `level`（模块三元组格）管契约能力，`formatVersion` 管变体内部格式演进，
     两层互不替代；
   - 各变体该键必/选见下文。

本族各变体的「拒绝未知键」约定（§4/§4b/§6/§7）只作用于变体定义的 object，属
spec 2.4:74 末句「贡献类别、interface 与 variant 定义的 object 按各自 schema
处理」的豁免口径，与框架定义的 object「未知字段不得拒绝」不冲突。

## 2. 契约面总览与变体清单

本契约族推理侧登记三份接口（2026-08-29 第四轮决策，台账 D15）：

- `org.openvpi.wolf.inference.G2P` Level 1——**宿主面**主转换器契约：直接占用语言的
  `linguist/g2p` 名额，对外实现完整 G2P Variables（见语言契约）；
- `org.openvpi.wolf.inference.G2PModel` Level 1——**后端面**模型契约：只经 imports
  被编排变体（当前为 pipe-chain）消费；语言 `linguist/g2p` 的目标契约被钉死为
  G2P（wolf validator 等值校验，狼仓 `WolfLinguistProvider.cpp:81-83,99-115`），
  本契约模块挂不进语言组合——backend-only 以此成立，框架无需任何可见性机制
  （第四轮调研确认 synthrt main 无 hidden/internal 类机制，命名隔离是唯一承载路径）；
- `org.openvpi.wolf.inference.DictQuery` Level 1——**查询面**词典契约：键→值批量查询，
  消费方为宿主工具（如多音字选择 UI）与编排变体，不参与语言组合。
- 命名定案（第五轮 D21）：六接口名维持不变；与外部惯例为镜像关系（外部 g2p≈本族
  G2PModel、外部 phonemizer≈本族 G2P 宿主面、发音词典一等公民≈DictQuery），对照注记见
  语言契约接口表下；`WolfLinguist` → `Linguist` 的低成本皮肤级改名列为独立候选立项，
  不属本轮（wolf 仓字面量仅 2 处：`LinguistApiL1.h:16`、`plugin.json:5`）。

同一 (`interface`, `level`) 下，宿主面变体之间的差别只剩**实现大类与外部依赖剖面**：

- **规则算法**（`algo-` 类，无外部推理依赖）：资源即规则与词典；
- **编排**（`pipe-` 类，依赖其他推理模块）：组合式 G2P，依赖经 `imports` 显式暴露；
  - 当前唯一成员 `pipe-chain`（线性管道）；DAG 编排、条件路由、流式处理等
    未来风格同属此类并共用此前缀。

宿主面（G2P）收录变体：

| variant | 大类 | 外部依赖 | 实现要点 | 详见 |
| :--: | :--: | :--: | :-- | :--: |
| pipe-chain | 编排 | imports 的 G2PModel 模块 | 打标校验 → 词典/规整（可复步交错，序由 chain.json 定） → 模型 → 兜底；宿主面唯一经 `imports` 暴露依赖的变体 | §3 |
| algo-pinyin | 规则算法 | 无 | cpp-pinyin 引擎（普通话/粤语：查表 + 规则转换） | §4 |

后端面（G2PModel）收录变体（变体名 = 「家族-后端」复合，台账 D16）：

| variant | 家族 | 外部依赖 | 实现要点 | 详见 |
| :--: | :--: | :--: | :-- | :--: |
| multig2p-onnx | multig2p | ONNX 驱动 | seq2seq bundle 模型后端；topK 真实候选经 `candidates` 上链（现状被弃，见 §5 注记⑥） | §5 |

- 待收录候选（第五轮 D20）：`lstm-onnx`（家族 lstm，ONNX 后端）——生态在产佐证：lm
  `LstmG2p`（插件键 `g2p.model.LstmG2pInference`，seq2seq 双 session + `phonemes.json`
  音素名↔ID 表，资源形态见 `res/G2pPackages/Phonetic-Suite-Eng/modules/LstmG2p-Eng/`）；
  收录动作由 wolf 决定，本文先行登记候选，性质为备案而非收录。

查询面（DictQuery）收录变体：

| variant | configuration | 说明 | 详见 |
| :--: | :-- | :-- | :--: |
| dsdict | `dictionaries` 键汇 | ds 词典 TSV 族（宽松解析、多值组）键值查询服务；现状形态即目标形态（D18） | §4b |

演进判定（变体粒度 = 分派粒度）：

- **同族新成员**：
  - G2P：新宿主面算法引擎（如未来的西语规则引擎）登记 `algo-*` 同级新变体名，
    不挤占现有变体；
  - G2PModel：新模型家族登记复合名新变体（如 `lstm-onnx`）；同家族更换推理后端
    登记尾段替换名（如未来的 `multig2p-<backend>`）；
  - DictQuery：新词典格式族登记同级新变体名；
- **新后端能力面**（张量级中间产物、跨调用会话态等现有契约无法表达的能力）→
  按 spec 2.4:750-753 判据评估另立接口，评估记录入台账；
- 轻量、附属于组合链的规则逻辑走 pipe-chain 的 `rules`/`lua` step（资源格式演进），
  不构成新变体。
- 非典型变体口径（组织级）：S2P 的 `direct`/`mapping`/`lua` 与 Onset 的 `lua` 为已实现
  的库类、尚无生产接线（四仓无生产 mapping TSV / rule JSON / `.lua` 资源，见 §6/§7
  注记与台账 D19 事实链）——收录地位与典型变体同等，本文按集中表收，不设独立叙述段。

> 实现注记（非规范性），分两点：
>
> - **夹具格式陈旧**：资源夹具 `resources/G2pPackages/**` 全系旧版 G2pPackage 格式（包级 `packageId`/`modules`/`class` 键，模块声明壳 `$version/level/mode/schema/configuration` 五键，无 `exports` 面、无任何格式版本自验），迁 spec 2.4 贡献模块形态是结构重写而非改键名，迁移时须为每包补写 `exports.languages`/`exports.symbols`；
> - **Eng 不入发行物**：`Phonetic-Suite-Eng` 因许可证未核实不入发行物（`resources/G2pPackages/README.md`）。

## 3. pipe-chain（编排类）

- 模块声明的 `configuration` 只有一个键：`chain`（path，**必选**），指向链配置文件
  `chain.json`；
- `chain.json` 必填顶层 `formatVersion`（§1.2 公约 2）；
- 模块声明的 `imports` 引用本链所需的模型后端，按 spec 2.4 显式暴露：

```json
// 公共包 wolf/lang-eng 内（示例）
// inferences/g2p/inference.json
{
    "interface": "org.openvpi.wolf.inference.G2P",
    "level": 1,
    "variant": "pipe-chain",
    "exports": {
        "symbols": "./g2p/symbols.json",
        "languages": ["eng-arpabet"]
    },
    "configuration": { "chain": "./g2p/chain.json" },
    "imports": [
        {
            "role": "backend",
            "ref": "wolf/g2p-multi:inference/multig2p",
            "options": { "languageId": "eng-arpabet" }
        }
    ]
}
```

资源目录内部约定：

```
+ g2p
  - chain.json     // 步骤排布：{ "formatVersion": 1, "steps": [ { "step": <类型>, "params": <对象> } ] }
  - symbols.json   // exports.symbols 所指文件
  - *.txt          // 各步引用的词典等资源
```

Level 1 的 step 类型集合：

| step | params | description |
| :--: | :--: | :--: |
| tagAndValidate | `tagger: [{type, value, action}]`（注 1） | 分片打标 |
| dict | `{enabled, file}` | 词典查表 |
| model | `{role, enabled, batchSize}`（注 2） | 消费一个被 `role` 点名的后端 |
| format | `{cleaner: {operations: [...]}}` 等（注 3） | 文本规整 |
| fallback | `{useOriginal, defaultPronunciation}`（注 4） | 兜底策略 |

- 注 1：`type ∈ regex/array/dict`，`action ∈ convert/copy`；
- 注 2：步内不写 ref 引用串；`role` 必填，点名本模块 `imports` 中一个目标契约为
  `org.openvpi.wolf.inference.G2PModel` 的条目（第四轮起模型后端不复用宿主面
  G2P 契约，台账 D15）；
- 注 3：操作如 `lowercase`；
- 注 4：命中兜底的词 `error` 置 `PhonemeGenerationFailed`。

step 词汇与 `formatVersion` 是链家族的内部事务，与 `interface` 的 level 分层管理：

- 新增**纯内部** step（如 `rules`/`lua`，把西语式规则算法做成几十条规则而非整词
  词典；或不涉及外部推理能力变化的 `dict` 增强）→ 只升资源 `formatVersion`，
  模块声明的 `level` 不变；旧解释器遇到更高 `formatVersion` 一律拒绝加载；
- 新增依赖后端契约**更高 Level 能力**的 step（如未来调用 G2PModel Level 2 推理契约的
  model 步变体）→ 使用该 step 的 pipe-chain 模块自身必须声明更高 `level`；
  后端面各变体共享同一 IO（契约的 G2PModel Variables），跨 level 只有整体切换，不存在混用。

**执行细则**：

- 解释器支持 `[1, 自身上限]` 区间全部 `formatVersion`；高于上限按 §1.2 公约 2
  诊断处理，缺键、非正整数、文件不可解析同按加载失败处理；
- 版本只增不减——旧配置遇新插件无缝，反向错误路径明确；
- 打包/编辑器等静态工具不加载插件即可读 `formatVersion` 预检；
- 发布侧联动（规范性，详见发布设计文档
  [linguist-g2p-package-distribution-draft.md](linguist-g2p-package-distribution-draft.md)）：
  chain 资源 `formatVersion` 抬升视同所在包的破坏性更新，包级 `compatVersion`
  必须同步抬升——「数据格式不兼容旧解释器」与「契约词汇不兼容旧宿主」同为
  包级不兼容，须在依赖求解阶段排除旧目标版本命中新包，不依赖解释器期拒绝兜底。

**绑定规则**：

- 每个启用的 model 步以 `params.role` 点名本模块 `imports` 中的一个条目，被点名模块
  连同其 `options.languageId` 一并注入该步；
- 点名的 `role` 不存在、多个 model 步点名同一 `role`、或所指条目的目标契约不是
  G2PModel 的，均即本模块加载失败；未被任何 model 步点名的 G2PModel 导入不注入、
  不视为错误；
- 绑定只认 `role`，不依赖声明顺序。
- model 步点名的 `role` 是本变体的内部槽位，不属于跨契约的开放扩展 role
  family：按 spec 2.4:629-644 的 role 文法，单段裸名（如上文示例的 `backend`）
  与多段 family 名同样合法，family 前缀仅为开放扩展 role 的命名建议、非有效性
  条件；本契约族不为变体内部槽位族化命名。

> 实现注记（非规范性），按话题分三点：
>
> 1. **配置入口差异**：现行实现（语言契约落地前的旧 g2p 框架）从模块 `configuration` 顶层直读 `steps` 数组，不存在 `chain` → chain.json 文件层引用，也不读取 `formatVersion`（`plugins/G2P/chain/internal/Core/G2pPipeline.cpp:40-128`，全文 `formatVersion` 零命中）。
> 2. **model 步键汇差异**：model 步键汇现为 `{enabled, id, batchSize, langRef}`，以 `id` 点名旧框架任务、以 `langRef` 直递内部语言引用（`internal/Steps/ModelStep.cpp:20-57,186-188`），并非上表目标设计的 `role` 点名 + `options.languageId` 注入。两项差异的收敛见 §8.4 D4/D5。
> 3. **其余实测事实**（随收敛一并处理）：
>    - tagger 的 `type` 现行仅 `regex` 生效，`array`/`dict` 及任何未知 type 静默丢弃、无报错（`TagAndValidateStep.cpp:74-89`）；条目未知键同样被忽略（夹具 `…/ChainG2p-Eng/config.json:17` 的 `tag` 键即无消费方）——注 1 三型为目标语义，收敛时复用 `lib/G2P/Support/Inferutil` 的三型实现补齐（`src/Verifier.cpp:104-116`；§4 verify 三型已全实现且未知型加载期拒绝，两处受理面不可横向套用）；
>    - `enabled` 双轨：步项级（整步不入列，`G2pPipeline.cpp:76-84`）与 dict/model `params.enabled`（步内停用，`DictStep.cpp:16-25`）两处独立；
>    - 链 `steps` 上限 50（`G2pPipeline.cpp:49-54`）；
>    - format 步 `normalizeTones` 有解析无使用点（`FormatStep.cpp:13-14`，死参数）；
>    - 夹具 model 步另携不消费的 `file` 键（`ChainG2p-Eng/config.json:54`）。

**覆写语义**：声库按语言契约《公共语言包》的逐项覆写规则覆写链配置文件（整份资源）时，
`formatVersion` 语义不变：

- 仍由承载本三元组的 provider 按其支持上限校验；
- 覆写不得抬升或重置版本序列。

**model 步行为**：

- 仅处理打标为 `convert`、未命中词典、尚无发音的词（清洗后词优先）；
- 按 `batchSize` 分批调用后端；
- 后端调用失败标 `ModelInferenceFailed`，不可用标 `DriverUnavailable`；
- 后端（被引 G2PModel 模块）逐词返回的 `error` 按下表收敛为链输出的 `error`、
  不叠加本链枚举值：`ModelInferenceFailed`/`PhonemeGenerationFailed`/
  `DriverUnavailable`/`NotInitialized`/`UnknownError` 五值同名透传；
  `InvalidInput` 同为同名透传，`LanguageUnsupported` 收敛为 `InvalidInput`（词法/语言纠纷归一
  报为输入非法，保持链对外六值封闭）；
- 携带 `error` 的词仍按本链 `fallback` 步的既有规则处理。
- `candidates` 规整（第五轮定案，收编第四轮残面①）：逐词候选只取**单一最终来源**——
  词典命中即词典值组（声明合并序保留）、后端产出即后端 topK 序列、兜底产出即兜底
  单值；链的顺序过滤结构保证同一词至多命中一个来源，故 Level 1 不做跨源合并、不去重、
  不设数量上限（跨源合成与截断若未来需要，属链资源 `formatVersion` 演进，`level` 不变）。
  守卫明文化：dict / model / fallback 三个产出发音的步一律只作用于 `mode=convert`、
  未丢弃且尚无发音的词，model 步对失败词保持空发音交付后续步——单一来源性由此守卫
  成立、与步序无关（同链可含多个同型步，现网夹具即「词典→规整→词典」复步，
  `ChainG2p-Eng/config.json:23-46`）；链未配置 fallback 或 fallback 未产出时，仍未获得
  发音的 convert 词以 `error=PhonemeGenerationFailed`、发音与候选为空产出，不得回填
  原词。format 步的清洗面只作用于未产出词的歌词文本、产出步一律取清洗后词；其规整面
  改写 `pronunciation` 时必须同步改写 `candidates` 各元素——契约共现约束在链内全程
  不变，「单一来源」指来源归属、不冻结后续步对串值的就地规整。

## 4. algo-pinyin（规则算法类）

- 本变体由唯一 provider 插件承载；新算法引擎登记同级新变体名（§2）；
- `configuration` 直接携带下列键汇（与现行实现直读 configuration 对齐）；
- 本变体无配置文件，`formatVersion` 不适用；
- 键汇演进以**拒绝未知键**守住——加载时遇未知键即失败，防拼写错误静默生效：

| name | type | description | example |
| :--: | :--: | :--: | :--: |
| scheme | enum | 引擎内转换方案（注 1） | `"mandarin"` |
| dictPath | path（相对声明文件目录） | 引擎词典目录 | `"./dict"` |
| verify | list&lt;object&gt; | 前置校验（注 2） | — |

- 注 1：cpp-pinyin 收录 `"mandarin"` \| `"cantonese"`，取值随引擎支持面扩充；
- 注 2：对象形 `{type: "regex"\|"array"\|"dict", value: [...], mode: "copy"\|"convert"}`，
  三键均必填；`dict` 型 value 为相对声明文件目录的路径。

- `exports.languages` 为本引擎支持的语言贡献 ID 集合（mandarin → `["cmn-pinyin"]`、
  cantonese → `["yue-jyutping"]`）；
- 同一语言家族贡献多个语种时，按「每语种一个模块实例、各自独立的资源配置」贡献：
  现行 cpp-pinyin 引擎词典路径为进程全局态，单实例多语种暂不受支持
  （实现侧已知约束，不影响契约）。

> 实现注记（非规范性）：现行实现为两款硬编码引擎插件、实际键汇与上表 `scheme` 键的目标形态有差（台账 D7 单载全部锚点）；「拒绝未知键」约定现行未实现（旧栈解析全为宽容读取，未知键静默忽略、全栈同状）；`exports.languages` 在旧版包格式中无对应物（资源夹具零痕迹），语言导出面纯目标。

## 4b. DictQuery 变体 dsdict（词典查询族）

词典键→值批量查询服务：同一注音体系下的多份 ds 词典按声明序合并加载，同键合并为
多值组（基础读音在前、追加读音作为后续 `values` 元素）。**本契约不参与语言组合**、
不占用语言的 `linguist/*` 名额；消费方为宿主工具（如多音字选择 UI）与编排变体。
「词典型 G2P」需求（歌词直接查表得发音）继续由 pipe-chain 的内化 `dict` step
（§3）承担，不经本契约。

- 接口：`org.openvpi.wolf.inference.DictQuery` Level 1，变体名 `dsdict`；
- 本变体由唯一 provider 插件承载（§2 收录纪律），无外部推理依赖；
- `exports.dictionaries`：可选，本模块收录的词典 ID 集合（见契约《DictQuery
  Exports》）；
- `configuration` 直携下列键汇，键汇演进守「**拒绝未知键**」（与 §4 同一约定）；

| name | type | description | example |
| :--: | :--: | :--: | :--: |
| dictionaries | map&lt;string, path \| {path}&gt; | **必选**。词典 ID → 词典文件路径（简写字符串或 `{path}` 对象），按声明序合并；同键后到的读音并入既有词条的值组 | `{ "official": "./dict/official.txt" }` |

- 词典为「键 → 一或多个读音（空格定界音素序列）」的文本条目格式（TSV）；解析守宽
  松侧惯例：`#` 注释与空行豁免、坏行逐条告警跳过、`word(n)` 后缀剥离并入同键值组；
- 未命中产出按契约 DictQuery Variables：`found=false`、`values` 为空——Level 1 无
  错误通道，未命中不是失败；
- 与 §6 S2P `dict` 解析**不同族**：本侧宽松、彼侧严格（整份拒载、不剥后缀、精确
  查表）；同一份文件勿跨两侧复用——现行接线 `s2pFile` 缺省回落声库 manifest 的
  `dict` 字段，同一文件可被两种解析器各解析一次而语义分裂（此为设计选择，不统一；
  细则锚点见 D12，§6 侧互引）。

> 实现注记（非规范性，台账 D12/D18），分四点：
>
> 1. **现状形态即本节目标形态**（D18：原 D10/D12 的「G2P 化为 algo-dsdict」路线撤销）：现行 `plugins/G2P/ds-dict` 即按 `dictId` 路由的词典查询任务（DictTask IO：`DictInputV1{dictId, keys, defaultValue, flags}` → `DictResV1{values[], found, foundCount}`，`include/synthrt/G2P/Task/DictTask.h:17-49`）。
> 2. **迁移只剩契约壳与接线**：
>    - 键汇读取 `plugins/G2P/ds-dict/internal/V1/TaskImpl.cpp:49-104`（`dictionaries` 缺失不报错、单词典失败仅告警 :60-63,96-100）；
>    - TSV 宽松侧 :196-233；
>    - 多值组来源为 `word(n)` 后缀剥离合并（`PhonemeDict.h:173-179`）与声明序合并（`plugins/G2P/chain/internal/Steps/DictStep.cpp:83-99`）；
> 3. **形状差与取舍**：
>    - 现行 `DictResV1.values` 为逐键单值串（多发音不拆数组），与本节「多值数组」的形状差收敛时以本节为准；
>    - `defaultValue`/`flags` 两键不迁契约（逐调用默认发音属调用方事务）；
> 4. **边界**：chain dict step 继续内化词典文件（§3）；不经 imports 消费本契约的接线属未来演进（届时只升链资源 formatVersion、Level 不变）；「拒绝未知键」约定现行未实现（同 §4 注记）。


## 5. G2PModel 变体 multig2p-onnx（家族 multig2p）

接口 `org.openvpi.wolf.inference.G2PModel` Level 1 的首个收录变体（第四轮由原 G2P
契约 `nn-onnx` 变体迁入，台账 D15/D16）。backend-only 定位：只经 imports 被
pipe-chain 等编排变体消费，不占用语言的 `linguist/g2p` 名额（目标契约钉死为 G2P，
直引即被 wolf validator 等值校验拒绝）。变体名即分派键：新模型家族登记同级
「家族-后端」复合名变体（§2），本节只描述 multig2p 家族的配套约定。
`configuration` 直接携带下列键（不设独立运行时配置文件）：

| name | type | description | example |
| :--: | :--: | :--: | :--: |
| bundle | path | **必选**。模型 bundle 根目录；`bundle.json` 与 `vocabulary.json` 与该目录邻接 | `"./model"` |
| inference | object | 推理参数键集，全部可选（注 1） | `{ "default_beam_size": 4 }` |
| languageMap | map&lt;string, string&gt; | 可选。语言贡献 ID → 内部 langRef 映射（注 2），缺省恒等（注 3） | — |

- 注 1：键集 `{default_max_len, default_beam_size, default_top_k, length_penalty, default_language}`。
  `default_language` 为缺省语言 ID（**暴露层 ID**，语义同 `languageId`，经
  `languageMap` 映射，不是内部 langRef）；数值键缺省属实现自由（类比 spec 2.4
  《何时递增 Level》采样步数之例）；`default_language` 缺省时按下文 langRef
  生效顺序判定。
- 注 2：如 `"eng-arpabet"` → `"eng/default"`、`"eng-arpabet-plus"` → `"eng/plus"`。
  后者是导出面携带自定义段 ID 的用例，用于对接同名方案的语言贡献（契约的 imports 组合规则）；
  常规实现只导出基本形 ID。
- 注 3：缺省恒等仅当内部 langRef 本身即合法语言贡献 ID 形态（纯 `segment`，不含路径段）
  时成立；langRef 形如 `eng/default` 含 `/` 时必须显式给出映射，否则内部子资源标识将
  泄出模块边界（契约的 G2PModel Exports 暴露面边界）。

`exports.languages` 取映射表的键集合（即暴露面）。`languages` 列表与 `bundle.json`
内部 `languages`（内部 langRef 全集）无须逐项相同——差集正是内部映射所吸收的差异。

bundle 目录内约定文件：

- `bundle.json`：**必填**。
  - 必填键：`bundle_version`、`languages`（合法 langRef 全集，如 `"eng/default"`、
    `"deu/marzipan"`）；
  - `files` 映射逻辑名 → 模型文件名，运行时打开 `encoder`、`decoder_step_init`、
    `decoder_step` 三个逻辑名（`decoder` 为导出侧保留，运行时忽略）。
- `vocabulary.json`：**必填**。
  - `symbols`（按 `{lang}/{variant}/{symbol}` 前缀组织，此处 `variant` 指 langRef 的
    资源变体段，如 `eng/default` 的 `default`，非模块三元组的 `variant`），及特殊符号表
    `global_symbols`（可选，缺失时 unk/pad/bos/eos 按约定索引 0..3 缺省）；
  - 不单独携带版本：与 bundle 同一发布单元、同一包身份，格式演进随
    `bundle_version`（解释器按 `bundle_version` 一并校验）。

内部 langRef 生效顺序：

1. 绑定期 `options.languageId`（若提供）：本导入唯一选定语言——运行时 `languageId` 未传入时回落该值，传入且不一致时按契约以 `InvalidInput` 失败产出（G2PModel Variables 值域，下同）；
2. 调用方 `languageId` 输入（经 `languageMap` 映射）——仅在无绑定期选定时生效；
3. `inference.default_language`（暴露层 ID，同样经 `languageMap` 映射）。

边界处理：

- 两级皆缺省时 → 加载期失败并诊断（模块缺省语言由 `default_language` 唯一承担，
  不设内建默认）；
- `languageId` 经映射查无内部项的 → 该词以 `error`=`LanguageUnsupported` 产出
  （G2PModel 值域新增值，取代现行实现的静默回退路径——不得静默改用其他语言）；
- `default_language` 经映射查无内部项的 → 属包声明与 bundle 不一致的包级错误，
  加载期失败并诊断。
- `DriverUnavailable` 触发条件限「初始化期未获得推理驱动、模块降级运行」一种；
  运行期会话/解码失败一律 `ModelInferenceFailed`；驱动对象运行中途失效属整次调用
  失败（框架错误通道），不进入逐词 `error` 值域（契约《G2PModel Variables》钉取值
  集合，触发条件归本文）。

> 实现注记（非规范性，台账 D6），分四点：
>
> 1. **键未实现与静默回退**：`bundle`/`languageMap` 键均未实现——bundle 目录取模块目录自身、`default_language` 直作内部 langRef；未映射 `languageId` 现行**静默回退默认语言**，与本节边界处理「按词报错、不得静默换语言」直接冲突，收敛以本节为准；
> 2. **驱动与版本行为**：驱动缺失现行仅警告并逐词产出 `ModelInferenceFailed`（非契约 `DriverUnavailable`）；`bundle_version` 现行仅校验非空、无版本比较（「一并校验」为目标规则）。实现锚点与元数据键清单单载于台账 D6（含 2026-08-30 增补④⑤）；
> 3. **契约面迁移**（2026-08-29）：由原 G2P/`nn-onnx` 迁为 G2PModel/`multig2p-onnx`（D15/D16），本节「以本节为准」表述均改指新契约值域；
> 4. **topK 实测红利**：另实测 topK 候选在解码期按 normScore 去重排序产出（`internal/V1/TaskImpl.cpp:769-787`）却在产出循环丢弃——`G2pRes.candidates` 恒为空 vector（:921），契约保真 `candidates` 后该红利可上链至 pipe-chain 逐词候选。

## 6. S2P 变体

| variant | configuration | description |
| :--: | :-- | :-- |
| dict | `file`（**必选**，path）：TSV，每行 `发音\t音素1 音素2 …` | 词典整体查表；未命中返回空音素序列 |
| direct | 无 | 按 ASCII 空格把发音拆成音素列表（注 1） |
| mapping | `file`（**必选**，path）：TSV，每行 `原音素\t目标音素` | 逐音素替换；未列入表的音素按原样透传 |
| lua | `file`（**必选**，path）：Lua 脚本（LuaJIT 执行环境），须定义全局函数 `s2p(发音) → list<string>` | 脚本转换 |

- 注 1：连续及首尾空格产生的空段丢弃（不产出空音素）；制表符不视为分隔符。
  - 实现分歧记录（第五轮）：上游独立库 phoneme-converter 的 `DirectS2P` **保留**空段
    （`tests/S2PTests.cpp:23-24`），synthrt 旧栈同名类丢弃空段并容忍尾随空格
    （`lib/S2P/DirectS2P.cpp:22-37`）；本变体语义以 synthrt 现行实现为准，上游对齐列为
    迁移事项（台账 D19 附记）。
  - 对抗注记（第六轮收尾）：空表 mapping 在实现上与 direct 等价（`MappingS2P.cpp:84`
    首句复用 `DirectS2P::convert` 切分）；二者仍不合体——`file` 的必/选约束与严格解析
    失败面（`MappingS2P.cpp:40-72`）不同，合并属语义变更，故仅做文档组织归并（§2
    非典型变体口径），不变体级合并。
- 新增（第五轮 D22）：`exports.phonemes`（**产出集**）推导口径——`dict` 变体取 TSV
  目标列的音素并集；`mapping` 变体取「目标列 ∪ 透传域」的上界集（逐音素替换、未列入者
  原样透传，phoneme-converter `src/MappingS2P.cpp:72-83`）；`direct` 开放无界、
  `lua` 不可静态推导，此两类由模块声明显式补齐或省略（省略即宿主警告、可加载，
  契约《S2P Exports》）。一切比对归警告级 lint（wolf 打包期 lint 与宿主工具，
  对账分工见台账 D22），不进加载事务（契约《自动组合校验的分层落点》）。

> 实现注记（非规范性），分四点：
>
> 1. **现行形态与接线**：现行 S2P/Onset 实现为 istream 库类、无 `configuration.file` 模块外壳；生产接线为声库 manifest `s2pMode/s2pFile/onsetFile` → `LanguageRoute`（值域现行仅 `"dict"|"direct"`，`include/synthrt/G2P/LanguageRoute.h:29`）→ `LanguageResource`（`lib/S2P/LanguageResource.cpp:29-84`）；
> 2. **mode 回落与文件共用**：**非 `"dict"` 的 mode 值现行静默回退为 direct（`lib/G2P/LanguageServiceLang.cpp:912-921`），迁移收敛时以「未知 mode 即加载失败」为准**；另 `s2pFile` 缺省回落声库 manifest 的 `dict` 字段（`LanguageServiceLang.cpp:866-868`），故同一词典文件可被 ds 家族（§4b）与本地 `dict` 变体各解析一次——两侧语义不同族（§4b 互引），勿跨侧复用；
> 3. **mapping/lua 现状**：mapping/lua 库类已实现但无生产消费方（仅单测），收录属目标形态；
> 4. **Lua 沙箱**：`io/os/debug/package/require/module/dofile/loadfile` 置 nil，指令计数钩子协作取消（`lib/S2P/internal/LuaExecutionEnvironment.cpp:372-383,429-435`）。

## 7. Onset 变体

| variant | configuration | description |
| :--: | :-- | :-- |
| rule | `file`（**必选**，path）：JSON 规则定义 | 模式匹配标记 |
| lua | `file`（**必选**，path）：Lua 脚本，须导出 `markonset`，返回与输入等长的布尔表 | 脚本标记 |

rule 文件结构：

```json
{
    "phonemeTypes": { "b": "consonant", "a": "vowel" },
    "rules": [ { "pattern": ["vowel"], "onsets": [0] } ]
}
```

- `phonemeTypes`：非空 object，音素 → 自定义类型名（类型名不得为保留词 `*`）。
- `rules`：`pattern` 为音素或类型（含通配 `"*"`）组成的序列，`onsets` 给出匹配
  序列中处于 onset 位置的下标；匹配按最长、最特异者优先；同一字符串同为字面音素
  与登记类型名时按字面音素段处理（字面较类型特异）。
- 未登记于 `phonemeTypes` 的音素不匹配任何类型段，仅可匹配字面音素段与通配
  `"*"`；输入中未被任何规则覆盖的位置，`onsets` 对应位置输出 `false`
  （整条无匹配时输出全 `false`）。
- 新增（第五轮 D22）：`exports.knownPhonemes`（**输入识别集**）推导口径——`rule`
  变体取 `phonemeTypes` 键集中**其类型名出现于至少一条规则**的键（类型名未被任何
  规则引用的登记键不参与匹配、不得并入；判定锚点 phoneme-converter
  `src/RuleOnsetMarker.cpp:306-319,134-148`：查询侧仅按登记类型名配通配位），并并入
  pattern 字面音素段；因 `"*"` 通配段功能上覆盖任意输入，派生集为覆盖面的**下界**，
  低估不构成缺陷（上条「未覆盖输出 `false`」本就是目标合法语义）；反向亦须防高估——
  D13 修复期字面段恒不命中（死代码），此期间并入字面段所得的识别集含不可匹配成员，
  lint 告警文案应注明推导基线、不得把派生集当精确覆盖证明；`lua` 不可静态推导，由
  声明显式补齐或省略。一切比对归警告级 lint（wolf 打包期 lint 与宿主工具，对账分工
  见台账 D22），不进加载事务（契约《自动组合校验的分层落点》）。
- 资源实况（第五轮调研）：磁盘上四仓（synthrt / language-manager / phoneme-converter / wolf）
  均无带实质内容的生产 rule JSON，最完整样例为 phoneme-converter
  `tests/OnsetMarkerTests.cpp:19-96` 的内联英语规则；上游 README 的通配示例携带空
  `phonemeTypes`（`README.md:166-174`），与实现非空校验（`src/RuleOnsetMarker.cpp:253-255`；
  synthrt 侧同根 `lib/S2P/RuleOnsetMarker.cpp:262-263`）直接矛盾——文档漂移实例，
  上游修复或示例更正归属 D13 同族迁移事项；且 D13 字面段死代码缺陷在上游同样存在
  （`src/RuleOnsetMarker.cpp:287-289`），修复时两仓同步。

S2P 与 Onset 变体的 `configuration` 不设 `formatVersion`：键汇极简，以「遇未知键
即加载失败」守住演进——声明中出现本表未列出的键即加载失败，防拼写错误静默生效
（与 §4 algo-pinyin 同一约定）。

> 实现注记（非规范性，台账 D13）：本节目标语义如上；现行实现有已知缺陷——字面音素段永不匹配（pattern 各段被一律按通配入树，精确匹配路径成死代码；实现锚点与存量资源盘点任务单载于 D13）。收敛方向为修实现，本节目标语义不变；语言契约 Level 1 尚未发布（契约《发布状态》声明），本注记不构成修复时点承诺。「拒绝未知键」约定现行未实现（同 §4 注记）。

## 8. 复核注记（2026-08-28 起，非规范性）

本节是 synthrt 侧对照 `ds-spec-2.4.md`、语言契约 Level 1 修订版与 synthrt
`language-level-1` 分支现行实现（G2P 插件栈不在 main，锚点均取本分支）的复核记录，
属决策台账而非规范性内容。台账 D 编号永久不复用、不因清理重排；防线记功保
「面名+结论+记功锚点」（引用另文档一律节名+锚词锚定，不用行号），攻防论证过程
不再全载——修订史经 git 历史回溯。

### 8.1 断言复核结果

| 断言 | 结论 | 依据 |
| :-- | :-- | :-- |
| 其他 G2P 按现有规则分变体（pipe-/algo-/nn- 三类） | 成立 | spec 2.4:545-562（规则/模型差异属同一契约下的 variant）；本文 §2 |
| chain（pipe-chain）可经 imports 调用 multig2p 作后端 | 成立 | 本文 §2 表、§3（model 步 role 点名 + `options.languageId`） |
| multig2p 的输入输出是 backend 的状态 → 可单独 interface | 不成立（第四轮推翻，见 D15） | spec 2.4:746-753（契约 IO 与其他 G2P 同形，「输入输出根本不同」判据不满足）；语言契约 G2P Variables 共享同一 IO；现状 langRef 泄漏恰是待收敛项，见 D3 |
| 「只需要分两种 interface」 | 口径需调准（第四轮再调：G2P 域内现为 3 个 interface，见 D15） | 语言域原为 4 个 interface：1 组合（WolfLinguist）+ 3 推理（G2P/S2P/Onset）；D15 起 G2P 域内为 G2P/G2PModel/DictQuery 三份 |

### 8.2 决策台账

- **D1（2026-08-28，历史决议）**：multig2p 不另起 interface，维持
  `org.openvpi.wolf.inference.G2P` Level 1 下 `nn-onnx` 变体收录（曾议独立 backend
  interface，经出示连锁改动后由用户收回；依据 spec 2.4:753/:558/:769）。**2026-08-29
  被 D15 推翻：用户定向改为三契约拓扑，直引缺口由公共语言包的 pipe-chain 包装吸收；
  本条仅存为谱系。**
- **D2（2026-08-28，历史决议）**：独立 backend interface 方案存档（重启判据
  spec 2.4:750-753：契约无法表达的后端能力、且导入方需要看懂）。**2026-08-29 D15
  作废本存档条件：独立 backend interface 以 G2PModel 契约落地，重启理由为消费方分层
  而非词汇不可表达；原对比稿（§8.3）已撤表，其否定结论「语言不能直引模型后端」经
  D15 转为三契约拓扑的既定取舍。**
- **D3（2026-08-28，迁移项）**：synthrt `language-level-1` 分支现状将 bundle 内部
  langRef 泄出模块边界，与语言契约《G2P Exports》暴露面规则（「契约暴露面只允许
  规范语言 ID、内部子资源标识不出模块边界」段）冲突：
  - `include/synthrt/G2P/Task/G2pTask.h:17`：`G2pInputV1::languageId` 注释明示收
    lang_ref（如 `"deu/default"`）；
  - `plugins/G2P/chain/internal/Steps/ModelStep.cpp:46-51,186-188`：chain model 步
    解析并透传 `langRef` 至被引 G2P 任务；
  - `plugins/G2P/multig2p/LangIdMap.cpp:37-43`（2026-08-30 勘误）：`normalizeLanguageId`
    存在但**全仓零调用、属死代码**——归一化实际由上游（chain `langRef` / 宿主传参）
    约定俗成承担，运行时 `languageId` 字串直送查表（`internal/V1/TaskImpl.cpp:111,`
    `839-844`）；且旧栈词法为下划线（`eng_plus`），与契约语言贡献 ID 的连字符形态
    （`eng-arpabet-plus`）不同族，迁移时须转换。
  处置：随契约落地实施一并收敛为 §5 `configuration.languageMap` 模式（暴露层语言
  贡献 ID 进模块、langRef 不出边界），本轮不改代码。

### 8.3 备查：单一 G2P 契约 vs 独立 backend interface（对比表已撤）

> D1/D2 对比表原存档于此；2026-08-29 D15 落地后撤表（修订史经 git 历史回溯）。
> 表中「语言不能直接导入 multig2p」一行的否定结论已由 D15 转为三契约拓扑的
> **既定取舍**——语言不可直引模型后端，由公共语言包的 pipe-chain 包装吸收。

### 8.4 第二轮复核（2026-08-29）

对照面与复核结论：

- wolf `cecedba`；
- synthrt origin/main（`38e60e1`，第三轮时推进至 `a060af0`）；
- 本分支 HEAD；
- spec 2.4——main/本分支/工作区三侧同 blob（main `3249a7c` 的 spec 文档文本经本分支 `8341c26` 同步；`3249a7c` 所涉框架代码属 main 侧新框架、`synthrt/lib/Core/**` 路径系，**未并入本分支**，两线自 merge-base `304b275` 起全树分叉）；
- 复核结论（三路攻防代理，主代理逐条亲核）：事实断言 12/12 成立、spec 锚点 17 处 0 偏移 0 冲突、演进攻击 4 面中 2 面攻破 2 面防线。

**锚点快照（复验未漂移；括注为微调处）**：

- 狼仓（`cecedba`）：
    - API 头：`LinguistApiL1.h:16-18/99-102`、`LinguistProviderPlugin.h:13`、`LinguistContrib.h:14`；
    - 类别注册：`LinguistContrib.cpp:15-24/26-37/94-95`；
    - provider：`WolfLinguistProvider.cpp:69-152/249-266/268-283/285-288/308-319`、`WolfPipelineExecutive.cpp:22-26`；
    - IID：字面量 `src/plugins/linguistproviders/CMakeLists.txt:3`、嵌入调用 `src/plugins/linguistproviders/wolf/CMakeLists.txt:15`；
- synthrt main：`ContribPluginFactory.cpp:20-83`、`PackageLoader.cpp:38-56/769-774`（原引 769-775 收紧）`/1210-1212/1316-1319/1358-1362`、`ContribCategory.h:158`、`SynthUnit.h:27-28,66-67`（原引 65-68 收紧）/`SynthUnit.cpp:14-31`；stdcorelib `JsonValue` 缺省即 Null（该依赖为 vcpkg 端口，头文件行号随版本 pin 漂移，不作硬锚）；
- 本分支 G2P 旧栈：`include/synthrt/G2P/Base/LangCommon.h:29-31/61-69/73-82`、`include/synthrt/G2P/Support/PhonemeDict.h:160-187`、`plugins/G2P/chain/**`、`plugins/G2P/multig2p/**`、`plugins/G2P/ds-dict/**`、`plugins/G2P/{mandarin,cantonese}/**`。

**本轮落地补丁（已并入正文）**：语言契约《G2P Variables·运行时路径》改依序判定、《phonemes 对齐基准》交集分母句（未提供模型音素表的 stage 不参与、全无则跳过不判失败）、变量展开禁令扩至 Category、新增发布状态声明、S2P/Onset「Level 1 不定义错误通道」口径；本文 provider 发现锚点收紧、「envelope」措辞改「外层形态」、§3/§4/§5 补实现注记、新增 §4b（后经 D18 改为 DictQuery 章节）。

**防线记功（面名+记功锚点，攻击均失败）**：

- `algo-dsdict` 收录不破「变体名即分派键 / 收录变体单一承载」治理：语言契约《变体治理》段 + 本文 §1.1「变体名即分派键」条、§2 三大类划分与「同族新成员登记新变体名」条；与 pipe-chain `dict` step（§3）职责重叠仅属实现级。（该收录后经 D18 撤销为 DictQuery/`dsdict`，防线对收录治理本身仍有效。）
- `formatVersion` 公约在双 provider 共存同一变体名下的结果确定性：本文 §1.1 全序扫描首匹配当选条、§1.2 公约 2（自验上限、超限拒绝带诊断）、§3 执行细则「版本只增不减」与《覆写语义》「不抬版本序列」条。

**决策台账新增 D4-D10**：

- **D4（2026-08-29，迁移项）**：pipe-chain 的 `chain.json` 文件层与 `formatVersion` 在现行实现中未落地：`plugins/G2P/chain/internal/V1/TaskImpl.cpp:17-26` 经 `G2pPipeline::configure` 直读模块 `configuration` 顶层 `steps`（`internal/Core/G2pPipeline.cpp:40-128`，全文 `formatVersion` 零命中）。目标设计（§3）不变，随语言契约落地收敛。
- **D5（2026-08-29，迁移项，D3 同族）**：chain model 步绑定现为旧框架任务 ID + `langRef`：`internal/Steps/ModelStep.cpp:20-57` 键汇 `{enabled, id, batchSize, langRef}`、:186-188 将 `langRef` 写入批量输入 `languageId`。目标设计为 `params.role` 点名 `imports` 条目 + `options.languageId` 注入（§3 绑定规则）；依赖 synthrt 的 imports/options 机制接入 G2P 后收敛。
- **D6（2026-08-29，高危迁移项）**：multig2p 与 §5 边界的实现差：
    - ①无 `bundle` 键、bundle 目录取模块目录自身（`plugins/G2P/multig2p/internal/TaskImplBase.cpp:48`）；
    - ②`languageMap` 未实现（D3 归一化潜规则延续）；
    - ③**未映射 `languageId` 静默回退默认语言**：`internal/V1/TaskImpl.cpp:111-119` 仅记录 `missingLangIndices`、:893-923 产出循环从不检查该集合——与 §5「查无映射按词报错、不得静默换语言」直接冲突，收敛时以 §5 为准；另驱动缺失仅警告并回退 copy（`TaskImplBase.cpp:79-85`），与契约 `DriverUnavailable` 语义的观测差异并记于本条；
    - 2026-08-30 增补：
        - ④驱动缺失的现行产出链为初始化期警告（`TaskImplBase.cpp:79-85`）叠加运行期逐词 `ModelInferenceFailed`（:149-161、`internal/V1/TaskImpl.cpp:827-830`），触发条件判定锚点见 §5 边界处理尾条；
        - ⑤`bundle_version` 现行仅校验非空、无版本比较（`BundleLoader.cpp:133-142`），bundle.json 顶层其余元数据键 `schema_version`/`model_version`/`min_runtime_version`/`vocab_hash`/`generated_at`/`opset_version`/`export_flags` 为发布侧内部元数据，契约不解释（本条单载，§5 注记仅索引）；
        - 基线锚点：bundle 目录取模块目录自身 `TaskImplBase.cpp:48`、`default_language` 直作内部 langRef :42-44、静默回退 `internal/V1/TaskImpl.cpp:111-119` + :893-923。
- **D7（2026-08-29）**：algo-pinyin 的 `scheme` 键为收敛目标；现行两款插件硬编码引擎（`g2p.template.MandarinG2pInference` / `g2p.template.CantoneseG2pInference`，`plugins/G2P/{mandarin,cantonese}/main.cpp:9-10`），键汇 `dictPath`+`verify`（verify 选词字段 `mode`，区别于 chain tagger 的 `action`）。
- **D8（2026-08-29，口径判定）**：契约「`mode=skip` 时 `pronunciation` 与 `candidates` 均为空」与现行 chain 兜底冲突：
    - 事实面（冲突源头共两处）：
        - `plugins/G2P/chain/internal/V1/TaskImpl.cpp:63-69` 对任何 mode 强制 `pronunciation` 非空回填原词（原稿文件名 `ChainG2pTaskImpl.cpp` 有误，2026-08-30 正编）；
        - 另 `include/synthrt/G2P/Base/LangCommon.h:110-114` 的 `G2pRes` 构造亦做空发音回填；
    - 判定：**契约为准**（skip 即空词产出为空，词位保留与兜底归宿主决定），代码为迁移对象；
    - `G2pRes` 的契约外字段 `g2pContext/g2pContextVersion/g2pSource`（`include/synthrt/G2P/Base/LangCommon.h:73-82`，D14 注释显式保留）作 D3 同族迁移项并记。
- **D9（2026-08-29）**：语言契约《Singer 侧配套》唯一映射约束（同歌手内语言句柄互不相同）无加载期实现：狼仓 validator 仅校验语言导入目标三元组与 `linguist/` role 前缀（狼仓 `WolfLinguistProvider.cpp:127-149`）。归属歌手契约义务（歌手变体解释器在加载事务 Ready 校验中执行），随歌手侧语言迁移收敛。
- **D10（2026-08-29，收录 + 迁移项）**：`algo-dsdict` 收录为规则算法类 G2P 变体（§4b）。传统插件 key ↔ 目标三元组变体名对照：
    - `dict.dsdict`→`algo-dsdict`；
    - `g2p.chain.ChainG2pInference`→`pipe-chain`；
    - `g2p.template.MandarinG2pInference`/`g2p.template.CantoneseG2pInference`→`algo-pinyin`（引擎随该变体统一容纳）；
    - `g2p.model.Multig2pInference`→`nn-onnx`；
    - **（2026-08-29 经 D15/D18 修订：`dict.dsdict`→DictQuery/`dsdict`（§4b，撤销 algo-dsdict）；`g2p.model.Multig2pInference`→G2PModel/`multig2p-onnx`（§5，撤销 nn-onnx）；chain 与 pinyin 两对照不变。）**

### 8.5 第三轮复核（2026-08-30）

对照面：wolf `cecedba`、synthrt origin/main `a060af0`、本分支 HEAD（`a9365a4`）与 spec 2.4（三侧同 blob）。过程：三路只读调研 + 三路攻防（契约一致性 / 变体治理与收录 / S2P·Onset 迁移），承重断言主代理逐条亲核源码后采纳。结果：**6 处攻破全部采纳并入正文、3 条精度补丁采纳、8 条防线记功**；规范性条款未动，D1-D10 无推翻（D3/D8 仅勘误与正编）。

**防线记功（面名+记功锚点，攻击均失败）**：

- 「拒绝未知键」与 spec 2.4:74 框架对象规则无冲突：:74 末句字面豁免 + spec :604/:616/:620 + 契约《本文只钉契约面》段 + 本文 §1.2 四层互锁；
- 实现级事实（50 步上限、双轨 enabled、死参数）入注记不稀释规范：「实现注记（非规范性）」隔离惯例 + D4/D6 先例；
- pipe-chain tagger「三型目标、regex 现行」不构成契约破洞：error 六值管词级运行时、configuration 受理面归 variant（契约《G2P Configuration》「由 variant 全权规定」句 + spec :616/:620-622 + D6 先例）；fallback `markFailed` 键汇落差经亲验不成立（`FallbackStep.cpp:10-16` 仅 `useOriginal`/`defaultPronunciation`，与 §3 表一致）；
- algo-dsdict 变体收录正当性不坠：「共享同一 IO」（契约 G2P Variables 封闭词汇 + 本文 §3）与 spec 2.4:750-753 双重封死；措辞定稿「另立接口」（§4b 注记、D12）；
- Onset `rule`「字面或类型」目标语义表达力自洽：任意字面段可由单例类型编码入 `phonemeTypes`；撞名歧义以「字面较类型特异性优先」收拢（与实现 `findBestPath` 计数方向一致）；
- mapping/lua 收录为变体正当性不坠：库类实现已存在（`lib/S2P/MappingS2P.cpp`/`LuaS2P.cpp`/`LuaOnsetMarker.cpp`），「无生产消费方」为接线缺口非能力缺口；变体新增本属开放（spec 2.4:769）；
- 契约 `DriverUnavailable` 触发条件委托落点合法：「取值集合在契约、触发条件归变体文档」（契约《G2P Variables》error 段「各取值的具体触发条件见变体文档」句）+ 预发布声明（契约《发布状态》）——语义洞补登在本文 §5 即属正位。

**决策台账新增 D11-D14**：

- **D11（2026-08-30，迁移项）**：linguist 基数校验只执行下界——`linguist/g2p`、`linguist/s2p` 缺失或目标 interface 不符已被拒（狼仓 `WolfLinguistProvider.cpp:92-152`，presence 恰为基数下界）；上界未执行——重复同类 role（第二个 g2p/s2p、第二个 onset）不被拒绝、组合结果未定义，随语言契约落地收敛。契约《组合规则》已定违例后果句；注意该演进属就地加严——同一违规包在修复由拒由不拒，可观察行为不同，契约《发布状态》声明允许原地修订，收敛排期时应在发布注记中列明。
- **D12（2026-08-30，高危迁移项，修订 D10 表意）**：
    - **定性翻转**：algo-dsdict 以目标变体形态收录（D10 不变），但现行 `plugins/G2P/ds-dict` 不是 G2P——是按 `dictId` 路由的通用词典查询服务（`include/synthrt/G2P/Task/DictTask.h:17-49` IO），无 lyric/mode/candidates/error 语义；§4b 的 G2P 语义为纯目标，多发音候选与声明序合并机制实为 chain DictStep 的 PhonemeDict（`include/synthrt/G2P/Support/PhonemeDict.h:160-187`）；
    - **迁移方向**：ds-dict G2P 化（改收契约 G2P Variables IO，立项语义即 §4b）；通用查询需求不构成对本变体 IO 的扩展压力——若未来需跨模块契约化，按 spec 2.4:750-753 判据另立接口（其下再登记变体），否则归宿主工具侧；
    - **实现锚点**：
        - 查询服务 IO `include/synthrt/G2P/Task/DictTask.h:17-49`（`DictInputV1{dictId, keys, defaultValue, flags}` → `DictResV1{values, found, foundCount}`）；
        - 配置读取 `plugins/G2P/ds-dict/internal/V1/TaskImpl.cpp:49-104`（`dictionaries` 缺失不报错、单词典失败仅告警 :60-63,96-100）；
        - TSV 宽松侧 `TaskImpl.cpp:196-233`（`#` 注释/空行豁免、坏行逐条告警跳过）+ `word(n)` 后缀剥离合并 `include/synthrt/G2P/Support/PhonemeDict.h:173-179` + 声明序与候选组 `plugins/G2P/chain/internal/Steps/DictStep.cpp:83-99`；
        - TSV 严格侧 `lib/S2P/DictionaryS2P.cpp:43-82`（无 tab/多 tab/空字段/重复发音即整份拒载）+ :92-98（不剥后缀、精确查表）；
        - 共享文件现实 `lib/G2P/LanguageServiceLang.cpp:866-868`（`s2pFile` 缺省回落 manifest `dict` 字段）；
    - **（2026-08-29 D18 转向：上文「ds-dict G2P 化」路线整体作废——以原生 DictTask 形态立 DictQuery 契约收录为 `dsdict` 变体（§4b）；「通用查询需求另立接口」的门径本案正式取用；algo-dsdict 不复存在，§4b 现为 DictQuery 章节；现行实现与本节的形状差（逐键单值 vs 多值数组）收敛时以本节为准。）**
- **D13（2026-08-30，实现 bug + 现网风险）**：Onset `rule` 变体字面音素段死代码——
    - **机理**：`lib/S2P/RuleOnsetMarker.cpp:296-298` 构造树时一律按通配入树，`:125-133` 精确匹配分支与 `findBestPath` 特异性计数（`:100-103`）恒退化；结构本有 `exactChildren` 字段（`:160`），判为缺陷非设计。目标语义不变（§7），收敛方向为修实现；
    - **附带盘点任务**（收敛排期前必须执行）：清点存量 onset rule JSON 资源是否使用字面段——若已使用，现行实现对它们静默输出全 `false`「整条无匹配」结果，修复属改变现网行为的破坏性变更，届时按结果重启「修复顺序 / 兼容期」议题。
- **D14（2026-08-30，迁移项，正编 D8 前向引用）**：`G2pRes` 的契约外字段 `g2pContext`/`g2pContextVersion`/`g2pSource`（`include/synthrt/G2P/Base/LangCommon.h:73-82`，D8 当时注释明示保留）归属诊断为宿主工具侧的观测通道，不属契约 G2P Variables IO；随契约落地收编为工具属性或废弃（D8 所引「D14」即本条）。

**下轮攻击者须知（备案）**：

- 已封闭面＝契约一致性（锚点口径、基数后果句）、变体治理收录（§4b 逃逸口措辞、fixtures 定性）、S2P/Onset 迁移（撞名歧义、接线静默回退、TSV 不同族互引）；
- 有价值残面＝
    - D11 修复前的「可观察行为未定义」是否需在《运行时聚合》补一句兜底（评价口径：契约《Singer 侧配套·运行时聚合》「extension 的角色集合即语言导入 role 集合」句与 D11 上界未拒组合后的聚合语义洞是否伤及引用方，阈值偏高）；
    - D13 存量资源盘点结果（未出）可能触发「修复 = 破坏性变更」重议；
    - multig2p 上游 bundle 格式与作者的口头确认（标为待确认）；
    - `docs/modules/g2p.md:17` 的 `lstm` → `multig2p` 事实性文档 drift（超出本族范围，发包前顺手修订即可）。

### 8.6 第四轮复核（2026-08-29，定向修订轮）

性质：**非攻防轮**——由用户新设计倾向（「部分 G2P 可适当修改 interface、只当 backend 用，不强制统一 interface」）驱动的定向修订。

- 过程：三路只读调研（本分支旧栈五插件盘点／狼仓承载面／main 框架机制面），承重断言主代理逐条亲核源码通过：
    - wolf validator 等值校验点（`WolfLinguistProvider.cpp:268-283` 等）；
    - multig2p 静默回退与产出循环（`TaskImpl.cpp:111-119,893-923`）；
    - topK 候选算而弃（:769-787→:921）；
    - main 三元组全等先到者赢（`ContribPluginFactory.cpp:64-66,74-83`）；
    - ImportBinding 三态（`ContribImportBinding.h:14-21,62-66`）；
    - DictTask IO（`DictTask.h:17-49`）；
    - 旧栈 TaskPlugin 分派键机制（`TaskPlugin.h:17-32`、`LangCommon.h:21-35`、`PackageManager.cpp:916,924,1051,1063`）；
    - chain model 步 `id`/`langRef`（`ModelStep.cpp:20-52,182-188`）；
- 结论：**本轮未开攻防演练**；§8.4/§8.5 防线继续生效，新拓扑下「变体名即分派键/单一承载/拒绝未知键豁免」复核后仍成立（分派治理按接口各自生效）。

**调研事实链（支撑 D15-D18）**：

- **spec 承载面**：`variant = dotted-id`（spec 2.4:318），「家族-后端」复合命名无障碍；「何时递增 Level」表（:744-757）意在抑制 Level 增长（:757 自述），对同所有者命名空间新增 interface 是枚举性建议而非禁令；代价是每份 (interface, level) 须发布契约规范与 exports/options JSON Schema（:594-606）；
- **框架承载面**（origin/main，`a060af0` 口径沿用 §8.5）：
    - provider 选择为三元组全等匹配、先到者赢；**无任何 hidden/internal/backend-only 可见性机制**——「backend-only」只能靠命名隔离，目标契约被钉死后非该契约模块自然装不进；
    - 一个插件可声明多个三元组并按三元组分派 `create`（`ContribInterpreterPlugin.h:20-21`），同一后端插件未来同出宿主面+后端面无障碍（现行 6 插件均只声明一枚三元组，先例空位但代码路径完整）；
    - ImportBinding 三态 Prepared/Active/Closed（亲核锚点见上段）；
    - dsinfer 先例「一种能力一份 interface（Acoustic…Vocoder）+ onnx 变体」与本案「按消费方切面」切分轴正交，不构成直接论据，仅佐证 interface 粒度可伸缩；
- **wolf 承载面**（`cecedba`）：
    - 仅注册 `linguist` 一个类别（`LinguistContrib.cpp:94-95`）；G2P/S2P/Onset 契约头仅常量、类型化 IO 未落地（狼仓 `docs/Status.md:30` 列为待办）；
    - WolfImportValidator 对 `linguist/g2p|s2p|onset` 目标 interface 严格等值校验（`WolfLinguistProvider.cpp:81-83,99-115`）——**backend-only 执行点现成，wolf 零改动**；非三件套 role 静默放行（:98-120 无 else 分支）；
    - 全仓无 G2P/S2P/Onset 解释器插件（`src/plugins/` 仅 linguistproviders 一家）；
- **旧栈适配度**（本分支五插件卡）：mandarin/cantonese 与 chain 天然契合统一 G2P IO（G2pRes 词汇表即以 pinyin 插件为蓝本，真实产出 candidates）；multig2p 四处削足适履——静默回退、candidates 恒空、DriverUnavailable 伪装、无每请求解码参数——但形态属 G2P；ds-dict 是 DictTask K-V 查询、chain 完全不消费它（dict 步用内嵌 PhonemeDict），硬塞统一 IO 属「接口装错品类」。

**决策台账新增 D15-D18**：

- **D15（2026-08-29，推翻 D1、作废 D2 存档条件）**：推理侧由单一 G2P 接口改为三接口拓扑——
    - **拓扑**：`G2P` 宿主面（pipe-chain / algo-*）、`G2PModel` 后端面（仅 imports 消费）、`DictQuery` 查询面（宿主工具/编排变体消费）；
    - **理由**：用户定向修订——backend 与宿主面消费方不同，应各按其本来语义裁剪契约，而非强制统一；判据面以 spec 2.4:746「导入方需要看懂它吗」为纲（pipe-chain 不需要也不想被迫提供 mode/copy/skip 编排词汇），不援引 :753 的「输入输出根本不同」；
    - **已知代价明示**：语言不能再直引模型后端（公共语言包以 pipe-chain 包装吸收，§8.3 对比表该行否定结论转为既定取舍；语言契约 imports 表钉死 linguist/g2p 目标为 G2P 使 backend-only 自动成立、zero wolf 改动）；两份新契约须随发布物落 JSON Schema（spec 2.4:594-602）。
- **D16（2026-08-29，用户指定命名）**：G2PModel 变体采「家族-后端」复合名，首个为 `multig2p-onnx`；`nn-` 前缀退役（原前缀语义已被接口本身「模型后端」定位吸收）。新家族登记 `家族-后端` 复合名，同家族换推理后端以尾段区分（§2 演进判定）。
- **D17（2026-08-29，G2PModel Variables 定稿）**：
    - **IO 面**：输入 `word` + 条件 `languageId`；输出 `pronunciation` + `candidates` + `error`；**不携带 mode 三态编排语义**；
    - **`error` 值域七值**：新增 `InvalidWord`（输入词非法：空串、纯/含空白）、`LanguageUnsupported`（语言 ID 不受支持或映射查无——取代现行静默回退），`ModelInferenceFailed`/`PhonemeGenerationFailed`/`DriverUnavailable`/`NotInitialized`/`UnknownError` 五值与宿主面同名同语义；
    - **保留 `candidates`**：承接 multig2p 实测被弃的 topK 红利（TaskImpl.cpp:769-787→:921）；运行时路径与 G2P 同款绑定判定；共现约束为 error 空则 `pronunciation`=`candidates` 首元素；
    - **pipe-chain 侧映射**：pipe-chain model 步对七值的收敛映射表定稿于 §3（`InvalidWord`/`LanguageUnsupported`→`InvalidLyric`，余者同名透传，保持链对外六值封闭）。
- **D18（2026-08-29，修订 D10/D12）**：ds-dict 不 G2P 化——以原生 DictTask 形态立 `org.openvpi.wolf.inference.DictQuery` Level 1 契约（spec 2.4:753 判据字面成立），首个变体 `dsdict`（§4b）；原 `algo-dsdict` 收录整体撤销；`defaultValue`/`flags` 不入契约；词典型 G2P 需求继续由 pipe-chain 内化 dict step 承担；DictQuery 消费方为宿主工具（多音字选择 UI 等）与未来编排变体。

**下轮攻击者须知（第 4 轮后）**：

- 已封闭面＝§8.4/§8.5 全部 + 本轮调研事实链各锚点（勿再打「backend-only 需框架机制」——命名隔离路径已核实）；
- 有价值残面＝
    - ①pipe-chain 逐词 `candidates` 的规整策略未钉（后端 topK 真实候选与词典候选的组合/去重/上限）；
    - ②DictQuery 的宿主工具消费面需求未经编辑器侧确认；
    - ③D11 基数上界、D13 存量盘点、multig2p bundle 口头确认等旧残面照旧；
    - ④首批公共语言包（wolf/lang-*）在三契约拓扑下的模块清单（每语言 pipe-chain 包装 + G2PModel imports 接线）待落地验证；
    - ⑤G2PModel 与 DictQuery 的 JSON Schema 发布物未起草（spec 2.4:596-599 硬要求，属后续成文任务）。

### 8.7 第五轮复核（2026-08-29，调研深化轮）

性质：**非攻防轮**——用户驱动项=「继续完善两份草稿 + 部分 g2p 可适当修改 interface 名的再讨论 + 借助 wolf pipeline 的自动组合校验设计 + g2p 资源 hash 快速缓存（另起独立文档）」。

- 过程：两路深挖调研（接口命名与分类 / S2P·Onset 导出音素集可行性）+ 一轮三路调研（wolf 深读 / main 框架 / 六仓 g2p 生态），承重断言主代理逐条亲核源码通过（对照面：wolf `cecedba`、synthrt origin/main `a060af0`、spec 2.4 三侧同 blob——**与第四轮基线零漂移**）；
- 亲核清单：
    - `WolfLinguistProvider.cpp:75-152/249-266/268-283/308-319`；
    - `PackageLoader.cpp:861-1010`（Ready 三段与 validator 平铺）；
    - `InferenceContrib.cpp:79-86`+`DiffSingerProvider.cpp:117-127`（相容性先例）；
    - lm `DsDict internal/V1/TaskImpl.cpp:13,147-163`（静态去重表）；
    - lm `README.md:14-20/44-47`+`Phonetic-Suite-Cmn/package.json:9`（ID 漂移）；
    - phoneme-converter `README.md:166-174` vs `src/RuleOnsetMarker.cpp:253-255`（空 phonemeTypes 示例不可运行）；
    - `src/MappingS2P.cpp:72-83`（透传）；
    - OpenUtau `G2pDictionary.cs:76-84`+`DiffSingerBasePhonemizer.cs:225-253`（dsdict 类型注记驱动 onset 等价逻辑 + 渲染期硬校验）；
- 结论：**本轮未开攻防演练**；§8.4-8.6 防线继续生效。

**调研事实链（支撑 D19-D23）**：

- **lm 插件五品类 → 本契约族映射**（台账 D19）：
    - `g2p.template.{Mandarin,Cantonese}G2pInference` → G2P/`algo-pinyin`；
    - `g2p.chain.ChainG2pInference` → G2P/`pipe-chain`；
    - `g2p.model.LstmG2pInference` → G2PModel/`lstm-onnx` 候选（D20）；
    - `dict.dsdict` → DictQuery/`dsdict`；
    - `onnx` driver → 契约族外 driver 面；
    - 编排语义烙在旧 IO 外壳（`mode`/`candidates` 字段）恰是五接口拆分的存量证据；**DsDict 在 lm 即独立于 G2P 类别存在**——「不姓 G2P」的先例在生态内早已成立（D18 方向再佐证）。
- **命名惯例镜像**（台账 D21）：外部 g2p（g2p_en / G2pPack / `mfa g2p`）= 词→发音后端，对应 G2PModel；外部 phonemizer（OpenUtau / Coqui / Piper）= 编排面，对应 G2P；词典一等公民对应 DictQuery。S2P/Onset 无外部通用专名，phoneme-converter 公开类即本组织先例（逐类同构 §6/§7）。改名成本实测：G2PModel/DictQuery 代码面零锚点；G2P/S2P/Onset 字面量各 1 处（狼仓 L1 头）；WolfLinguist 字面量 2 处。
- **S2P/Onset 可推导性**（台账 D22）：
    - **实现面**：phoneme-converter 与 synthrt 旧栈同源 PIMPL、无 getter（最小增量=按值 set getter，成本低）；dict ✅ / mapping ✅（带透传上界）/ rule ✅（`phonemeTypes` 键 ∪ 字面段，`*` 使派生集为下界）/ direct·lua ❌；
    - **资源实况**：TSV 样例齐（kana2romaji.txt、ds_cmudict-07b.txt 133,805 行）；**生产 mapping TSV 与 rule JSON、任何 .lua 文件四仓皆无**；
    - **生态先例**：OU dsdict.yaml `symbols:[{symbol,type}]` 的类型注记直接驱动 onset 等价计算且校验发生在渲染期；dspkg/dsinfer 官方方向=音素表属各 stage 资源、语言侧只持 dict 指针——无「S2P 自报产出集」先例，本族按声明式补齐。
- **缓存证据链**（台账 D23，详见独立文档）：lm DsDict canonical-path→`weak_ptr<Dictionary>` 去重、OU G2pPack 进程单例+PredCache 为正例；lm ChainG2p DictStep 每实例各载、旧栈 ds-dict/multig2p 每实例重复解析 bundle/词典为反例。

**本轮落地补丁（已并入正文）**：语言契约《命名对照注记》、《自动组合校验的分层落点》节（L0-L3 分层、警告级边界）、S2P Exports 可选 `phonemes`（产出集）与 Onset Exports 可选 `knownPhonemes`（识别集）、公共语言包生态 ID 对照 bullet；本文 §2 命名定案（D21）与 `lstm-onnx` 候选（D20）、§3 model 步 `candidates` 规整（收编残面①）、§6 产出集推导口径与 DirectS2P 空段分歧记录、§7 识别集推导口径与资源实况。

**防线记功增补（面名+记功锚点，攻击均失败）**：

- 「S2P/Onset 导出音素集应强校验」不成立：三枚可证伪反例——①共享词典回落（s2pFile 缺省回落 manifest dict，同一 133k 行 TSV 喂两侧，包含式必假阳性）②`*` 通配使派生识别集系统性低估（`tests/OnsetMarkerTests.cpp:131-146`）③有意不标 onset 是 §7 目标合法语义；叠加契约《WolfLinguist Exports》`phonemes` 对齐基准的分工（交集裁决属编辑器）与 OU 先例（同类校验在渲染期执行）——词汇按「可选+声明式+警告级」落地；
- 「接口名应向外部惯例翻转」不成立：三段纵深同构、命名镜像但治理无冲突，改名=同义改写纯成本+台账回写——以对照注记吸收；
- 「mapping 变体可导出闭包产出集」不成立：透传语义使产出无上界（`MappingS2P.cpp:72-83` 亲验）——口径钉「目标列 ∪ 透传域」上界集；
- 「direct/lua 不可推导 ⇒ 不应加导出词」不成立：声明补齐+省略警告口径照搬契约《G2P Exports》`symbols` 省略条款先例；
- 「wolf validator linguist 分支只校 interface 是缺陷」不成立：契约《imports（组合规则）》「目标 level 校验」条只约束目标契约与 role、《WolfLinguist》契约段钉三元组固定——两分支差异为有意且合规，复核封闭。

**决策台账新增 D19-D23**：

- **D19（2026-08-29，生态调研入档）**：
    - lm 五插件品类与本契约族五接口/变体的逐件映射（见上事实链）；phoneme-converter = S2P/Onset 参照实现；
    - 警注两条生态漂移——cpp-pinyin 磁盘版 v1.0.2 已删 `setDictionaryPath` 旧 API（lm 仍调用旧版，迁移须经 vcpkg 版本核对）、cpp-kana 已被 lm 表驱动方案架空（Jpn 走 ChainG2p+kana2romaji.txt）；
    - lm `moduleId` 与 README g2pId 双轨漂移列为公共包 ID 治理反例（语言契约《公共语言包》节对照 bullet）。
- **D20（2026-08-29，候选登记，非收录）**：`lstm-onnx` 升格为 G2PModel 待收录候选变体（家族 lstm；生态在产佐证 lm `LstmG2p`）；§2 表保持现行收录清单不变，候选行显式标为备案。
- **D21（2026-08-29，命名定案）**：六接口名维持不变（G2P/G2PModel/DictQuery/S2P/Onset/WolfLinguist）；依据=外部惯例镜像注记+phoneme-converter 本组织先例+改名纯成本测算（狼仓字面量面实测）；`WolfLinguist`→`Linguist` 列为可选低优先独立立项（须另界定 IID `...plugin.LinguistProvider` 与 trait `...extension.LinguistPipeline` 两个相邻名字空间的联动边界），不属本轮。
- **D22（2026-08-29，契约词汇新增）**：
    - **新增词汇**：S2P Exports 增可选 `phonemes`、Onset Exports 增可选 `knownPhonemes`（路径或内联数组、形状校验同 linguist `phonemes`）；声明式落地——作者显式列出；
    - **对账归属**：可推导变体（dict/mapping/rule）「导出声明 vs `configuration` 资源推导集」的对账归 wolf 侧打包期 lint 与宿主工具职责（契约《自动组合校验的分层落点》的 lint 落位）、不一致仅告警、告警文案须并列声明与资源两侧路径；
    - **加载事务边界**：加载事务内 provider 不执行该项比对（无仅告警通道：spec 2.4:444、wolf provider 现状仅 Error 返回），provider 侧落地若未来需要，通道与执行点另行立项（第六轮攻破 3 修正）；一切消费比对警告级、不进加载事务（Ready 失败面不变：仍仅基数/languages 命中/三元组等值三件套）；
    - **宣告**：预发布声明（语言契约 §发布状态）覆盖本次原地修订；JSON Schema 发布物并入既有任务（残面⑤）。
- **D23（2026-08-29，独立成文决议）**：g2p 推理资源 hash 快速缓存设计不进入本族两份文档，独立落盘 `docs/linguist-g2p-resource-cache-draft.md`；键形态=内容寻址 hash+（大小，mtime）路径快路径（用户拍板）；缓存粒度=解析产物，不碰 ONNX session；归属 provider 执行域（spec 2.4:442/:586 授权）；先例 lm DsDict / OU G2pPack。

**下轮攻击者须知（第 5 轮后）**：

- 已封闭面＝§8.4-8.6 全部 + 本轮防线五条（导出强校验、命名翻转、mapping 闭包、不可推导阻却、validator 双分支差异）；
- 有价值残面＝
    - ①残面② DictQuery 宿主工具消费面待编辑器确认（照旧）；
    - ②残面④ 公共包模块清单在三契约拓扑下落地验证（照旧）；
    - ③残面⑤ 全部五接口+S2P/Onset 新词汇的 JSON Schema 发布物（照旧并扩）；
    - ④phoneme-converter/synthrt 两处 D13 同根缺陷的跨仓修复顺序与存量资源盘点（照旧，现增上游同步义务）；
    - ⑤缓存文档的实现级参数（hash 算法选型、容量上限/淘汰策略分期）待实现阶段定稿；
    - ⑥`knownPhonemes`/`phonemes` 是否向 G2PModel 变体延伸（如 multig2p `vocabulary.json` symbols 段派生导出）——Level 1 未定，列后续演进候选。

### 8.8 第六轮复核（2026-08-29，攻防演练轮）

性质：**攻防轮**（用户发令「开启攻防演练，目标是深化设计」）。三路攻击面并行：面 A 契约一致性、面 B 变体治理与推导口径、面 C 缓存设计与组合校验落地性。

承重断言主代理逐条亲核源码：

- `ChainG2p-Eng/config.json:23-46` 双 dict 步夹 format 实录；
- `DictStep.cpp:71-81`/`ModelStep.cpp:130-136`/`FallbackStep.cpp:29-41`/`FormatStep.cpp:101-121` 守卫与改写面；
- spec 2.4 警告零命中、狼仓 provider 零日志调用；
- `RuleOnsetMarker.cpp:306-319/134-148` 类型名匹配机制；
- `PackageHandle.cpp:17-20` 析构驱动 rollback；
- `BundleLoader.cpp:115-122/212-221` 相对名+用点拼接。

结果：**9 处攻破全部采纳并入正文、1 处攻防对冲裁决、防线记功 12 条；规范性结构零调整（9 处攻破全部措辞级），无新增 Level 事由**；台账本轮无新增 D 项（全部补丁为既有决策的精确化）。

**本轮落地补丁（已并入正文）**：

- 语言契约 5 处：警告级比对清单纠偏（G2P `symbols` 异类不作跨键比对；Onset 覆盖比对钉下界口径「未列入≠未覆盖、通配不告警」）；L3 补通道形态交叉引用（运行时 API 规范管辖）；L2 增列隐式 `options.languageId` 时语言侧贡献 ID 命中校验；《组合规则》命中校验钉显式→L1（目标 provider `createImportOptions`）/隐式→L2（语言自身 provider）双侧分工（目标 provider 从 options 看不到导入方身份为决定性论据）；生态 ID 补「示例 ID 非内容承诺」句；
- 本文 5 处：
    - §3 `candidates` 守卫明文化（产出步只作用于尚无发音的 convert 词、与步序无关、同型复步合法）、无 fallback 链的 `PhonemeGenerationFailed` 空产出口径（承 D8）、format 清洗/规整双责与 `candidates` 同步改写义务；
    - §2 概述箭头改「词典/规整可复步交错」；
    - D22 对账改归 wolf 打包期 lint 与宿主工具（provider 无仅告警通道：spec 2.4:444 + 狼仓现状；编辑器 lint 撞契约《本文只钉契约面》段变体封装律，wolf 自持变体细节故自带 lint 合法）、§6/§7 尾句同步；
    - §7 识别集收窄（并入仅限类型名出现于至少一条规则的键 + D13 修复期高估警示）；
    - 头部触契约枚举修正（D20/D21 不触契约降为内述）；
- 缓存文档 6 处：§3.1「解析产物基路径纯净」硬约束；§3.2 parserFingerprint 删「实现修订段」（进程内缓存 × 插件常驻不可换 ⇒ 永不相遇；插件元数据无版本字段无取值来源）并钉裸 TSV 语法世代常量（如 `dsdict-tsv@1`）；路径快路径信任边界（spec 2.4:382 同源豁免）；§3.3「被检测到的变内容=变键」；§3.4 rollback 等价性论证；§4 验收基线精确化（同路径 N→1；异路径同内容解析 N→1、读盘数为物理下限）。

**攻防对冲裁决记录**：面 C 攻破 (g) 主张钉死 L3 诊断通道形式；面 A 防线 (e) 证明诊断形态已合法外移运行时 API 规范（语言契约《运行时聚合》「其 C++ 级 API 形状由运行时 API 规范定义，本文不钉」），且唯一具契约后果的义务（不构成加载失败+越界 role 判 `InvalidArgument`）已双钉。裁决：采纳 A 防线，落一句交叉引用了事，不钉通道形式。

**防线记功（面名+记功锚点，攻击均失败）**：

- 组合校验 L1/L2 读目标类型化 exports 的时序成立：
    - Acquire 全事务循环先于 Ready 各 pass（main `PackageLoader.cpp:814-859` → `:861-1007`）；
    - validator 经 `binding()->target().exports()` 可达目标 exports（`ContribSpec.h:60-65/158-161`、`ContribImportBinding.h:32`）；
    - spec 2.4:426 直接授权；
    - wolf 现行 validator 已在用（`WolfLinguistProvider.cpp:66/81/137`）；
- 缓存主问题合法：Acquire 期向 provider 静态表插入不算违规持久副作用（析构驱动 rollback + 引用计数只减本事务份）；共享解析产物不违反停止隔离（纯数据无执行活动，spec 2.4:440-442 管活动归属）；Loader 事务串行（spec 2.4:422）+跨 Runtime 双检锁收敛；
- 133k 行词典 hash 成本与复读消解论证成立：同路径快路径命中免重读；异路径同内容第二次读盘是物理下限非弱点；
- candidates 四角守住：dict/model `enabled=false` 直通与守卫一致、fallback 单值经契约注 2「error 非空时 mode/candidates 无定义」合法化且与实现一致（`FallbackStep.cpp:29-41`）——攻破项仅要求守卫明文化；
- `lstm-onnx` 备案不暗示 bundle 布局自由度不足：configuration 归 variant 全权 + §5 家族自收窄 + lm 双 session/四文件布局实测；
- S2P/Onset 导出键的双源漂移属族群既定咨询型口径（先例：语言 phonemes「不符属内容缺陷」+ G2P `symbols` 省略警告），「要求声明与资源逐字相等」反与下界语义矛盾；
- 生态 ID 对照 bullet 与 ID 形态约定无互伤：包目屏蔽条款+形态铁律（不建注音体系词汇表）+历史反例双锚守稳；
- L3 诊断义务无悬空：形式外移运行时 API 规范、契约后果双钉、消费者为宿主——攻击成功但被对冲裁决吸收（见上）。

**下轮攻击者须知（第 6 轮后）**：已封闭面＝§8.4-8.7 全部 + 本轮防线八条 + 对冲裁决；有价值残面＝①语言契约「唯一映射约束效力归属 Singer 契约义务」与 linguist 侧「可兜底」的双执行口径统一性（面 A 接力）；②「应设置警告」类条款的告警主体与 lint 归属的跨节统一性（面 A 接力）；③wolf 打包期 lint 工具是否单独立项（D22 修正条款留下的立项口）；④§3 概述箭头补丁后与现网各 chain.json 真链序的全量一致性复核（面 B 接力）；⑤TSV 指纹世代常量的命名注册面（`dsdict-tsv@1` 等初值清单）随 P1 实现起草。

### 8.9 第七轮复核（2026-08-30，攻防演练轮）

性质：**攻防轮**（用户发令三子代理攻防校验，四命题：覆盖当前需要、拓展新语种空间、精确无冗余、分段分行清晰可读）。三路攻击面并行：面 A 覆盖度（对 rt-api 宿主需求逐项裁决）、面 B 扩展性（新语种、同语种新注音、自定义字段、第三方变体、新后端、新词典族、链上第四段共八场景判定表）、面 C 精确性·冗余·可读性（含 60 条行号引用真实性全量核验）。

- 过程：面 A 首任代理因通读耗尽上下文无声终止，按「素材直注+最小读取」模板降级重开交付；承重断言主代理逐条亲核（语言契约 :87-96/:218-228/:302-313/:385/:394/:408-425/:444/:456-461、rt-api :101/:114/:258、本文 :120-135/:348-361、spec :315-320/:536-547/:742-765）；
- 结果：**6 处攻破全部采纳（措辞收紧 5 处、词汇增补 1 处）、扩展性零战果（八场景中七个纯新增或新增变体落地，两个撞墙场景均属明示 Level 递增预留边界）、行号真实性 60 条核验零失效（一致 58、瑕疵级漂移 2）**；规范性结构零调整。

**本轮落地补丁（已并入正文）**：

- 语言契约 6 处：显式 `languageId` 判定值钉死（辖域真空）；《分层落点》与 S2P Exports 两处联动钉 S2P 对齐比对方向「S2P 产出集 ⊆ 语言 `phonemes`，超出即告警」；G2PModel `LanguageUnsupported` 值义收紧为级联查无；DictQuery `dictId` 钉回落模型（文本内部证据：运行时查无条款反证运行时可传）；S2P Variables 补发音层定界约定引用（锁发音双载输入语义）；G2P Variables 增可选输出 `hitSource`（见 D24）；
- rt-api 1 处：§5 `hitStage` 来源闭环（`dict|model|fallback` 取自 `hitSource`，模块未提供时为空）。

**决策台账新增 D24**：

- **D24（2026-08-30，契约词汇增补）**：G2P Variables 增可选输出 `hitSource`（enum：`dict`/`model`/`fallback`；注[3]：模块无法区分来源或 `mode` 非 `convert` 时省略、宿主不得依赖其存在、变体只可援引不得自定义）。动因：rt-api §5 `hitStage` 的 `dict` 值原无模块侧来源通道——G2P 输出面仅四变量、DictQuery 不经 linguist 链，「纯数据可为空集」把无源合法化；先例 D22（S2P/Onset 可选导出词同款操作）；预发布声明（语言契约《发布状态》）覆盖本次原地修订。

**防线记功（面名+记功锚点，攻击均失败）**：

- 覆盖度：锁层分层铁闸（契约《G2P Variables》「任务与状态生命周期由运行时 API 规范管辖」句）；错误词表与语言路由稳定（判定次序、六值契约词、运行时路径三件套，rt-api 引用全部可解析）；SP 直通宿主预过滤钉死（rt-api §3.1，含 lite 双侧实测）；缺语言硬失败承载面（契约《组合规则》、《公共语言包》闭包遮蔽包 ID）；D-R1 备案上限=「已备案」；
- 扩展性：ID 形态四条款（多注音多 ID 并列、自定义字段不剥除、全 ID 恒等、句柄=首字段）；变体治理（同族新成员登记新变体名、家族-后端尾段替换、词典族同级命名，本文 §2）；`options.languageId` per-import 绑定（spec 2.4:646）；backend-only 钉死（`linguist/g2p` 目标契约等值校验）；error 值域封闭加 `UnknownError` 兜底；升 Level 三层共存（spec 三元组不向下兼容、同插件多三元组声明、按声明 Level 读取）；
- 精确性：三键术语（语言 `phonemes`、S2P `phonemes`、`knownPhonemes`）全程无混用；`languageId` 命中校验规则六处表述零漂移；60 条行号锚点零失效、锚点口径声明逐项实测吻合。

**备案区（瑕疵级，未改正文，择机批量处理）**：
<!-- 2026-09-05 §8.12 第十轮：以下 17 项已全部批量处理，处置清单见 §8.12。 -->

- 措辞与一致性 6：五契约 Configuration 两派措辞（与本文 §1.2 重复）；Onset 省略告警缺「口径同」回链；语言契约 `knownPhonemes` 推导口径转述丢「被规则引用」限定；语言契约 :525「分两点」实三点（计数错）；「形状校验同注 1」存在性陷阱（S2P/Onset 本键可省略）；「兜底」与 `mode=copy` 撞词；
- 冗余 3：语言契约实现注记 3 与正文 :196 纯重复；省略告警三处展开（Onset 应引用式）；Configuration 重复（同措辞项）；
- 可读性 4：《组合规则》与《imports（组合规则）》节名撞车；连续注记块（语言契约 :230/:237）；:222 嵌套括注；G2P/G2PModel Variables 节 10 语义块无子标题；
- 行号漂移 2：语言契约 :513 validator 校验项枚举不全（还校 executiveFactory 存在）；:543 dspkg 路径简写（实为 `dspkg-core/src/converter/singer.rs`）；
- 治理残面 2：语言契约 :13「各变体」未点明是否涵盖第三方变体；公共 G2P 是否、何时收录第三方 v2 全名 ID 策略空白。

**下轮攻击者须知（第 7 轮后）**：

- 已封闭面=§8.4-8.8 全部、本轮六处补丁与防线（勿再攻锁层、SP 直通、缺语言灰显、DictQuery 批量、变体治理、ID 形态、三元组分派、全 ID 恒等、options 绑定、backend-only、升 Level 共存）；
- 有价值残面=①「发布时刻」——预发布修订自由已耗四笔（D15-D18 拓扑重拆、D22 原地加词、D11 就地加严、第六轮措辞修订），发布后第一类必走 Level 递增的演进（error 增值、role 表加行）的新旧包共存实演；②本文对 S2P/Onset 留白的承接真实性（语言契约 :456-461/:491-496 所指承接条款是否存在）；③G2P `candidates` 与 DictQuery `values` 的汇流、合并序规则两文档均未钉；④本轮备案区瑕疵批量清理轮；⑤B 轮两治理残面。

### 8.10 第八轮复核（2026-08-30，命名复核轮）

性质：**命名复核轮**（用户发问「应否修改变量名或 interface 名」，裁定：`WolfLinguist` 以 wolf 实际代码为准且本轮不改、`languages` 值域保留语言贡献 ID 全名、剩余两项统一并调研拓展枚举选项）。主代理单轮调研+三文档连锁修订，无子代理。

- 调研事实，分五点：
    - ① 旧栈 `g2pSource` 值域实为资源语境 `official`/`voicebank`
      （`LangCommon.h` G2pRes 注释、单测 `tst_g2p_route.cpp:263-264`，
      lite 仅日志消费 `GetPronunciationTask.cpp:261`）；
    - ② 旧栈 step 全集五类（`G2pStep.cpp:37`），`rules`/`lua` 为本文 §3 预留；
    - ③ algo-pinyin（§4 引擎词典+内部规则）的规则推导产出在
      `dict`/`model`/`fallback` 枚举中无值可标——`rule` 为现存缺口非未来预留；
    - ④ 狼仓无任何 `InvalidLyric`/`InvalidWord`/`hitSource` 字面量
      （error 值字符串实现未落），值名改动狼仓零接触；
    - ⑤ 单一来源性守卫（§3 执行细则）为 `hitSource` 变体侧语义基础。

**决策台账新增 D25**：

- **D25（2026-08-30，错误值统一与来源枚举拓展）**：
  - G2P `InvalidLyric` 与 G2PModel `InvalidWord` 统一为 **`InvalidInput`**。依据：三处使用场景（G2P 歌词非法、G2PModel 清洗后词非法、rt-api 手改层 phonemes/onsets 形状非法）的共同语义即「调用方输入非法」，中性名全覆盖，连带消解第七轮 C2 残面（「Lyric 之名对手改音素层错误的命名误导」）。连锁：语言契约八处（判定次序注释、六值表、G2P 运行时路径、G2PModel 判定次序×2、七值表、值义注释、运行时路径）、本文 §3 收敛规则与 §5 绑定判定、rt-api §3.3。本文 §8.6 D15 历史台账原文不动（历史纪律），D15 所记 `InvalidWord` 自本条起以 `InvalidInput` 为准；旧栈 enum 值名不受影响（C++ 内部标识符，非契约序列化面）；「链对外六值封闭」不变（`InvalidInput` 同名透传、`LanguageUnsupported` 收敛为 `InvalidInput`）。
  - `hitSource`/`hitStage` 枚举补 **`rule`**（规则推导：algo-pinyin 现存规则产出与未来 rules/lua step），并钉 `mode=copy` 与 `fallback` 边界（copy=打标类原样保留，不算转换来源；fallback 专指 convert 词经兜底步产出，含显式 `useOriginal`——显式兜底≠copy）。
  - rt-api §5「对位 v2 `g2pSource`」叙述修正为「诊断角色继承」：v2 值域为资源语境 `official`/`voicebank`，该语境现由 `g2pContribution` 承载；`hitStage` 为词级命中方式，新粒度。

**D21 补充实测（按用户裁定本轮不改名，留发布前终审）**：狼仓 interface 字面量仅 2 处（`LinguistApiL1.h:16` `API_INTERFACE`、`plugin.json:5`），实现类名 26 处不属契约面；API 目录名（`include/wolf/Api/Linguists/Linguist/1/`）与 trait ID（`LinguistPipeline`）均已不含 Wolf 前缀——接口字符串为仓内孤例。

**裁定不改清单**：

- `WolfLinguist`（用户指令，D21 补充如上）；
- `languages` 值域保留语言贡献 ID 全名（用户裁定：防歧义、预留同语种多注音体系扩展）；
- `G2P`/`S2P`/`Onset`/`G2PModel`/`DictQuery` 接口名（D19/D21 先例、phoneme-converter 已发布连续性）；
- 类别名 `linguist`（c22dac8 有意决策）；
- `lyric`/`word` 有意分层（编排清洗前后）；
- `pronunciation` 双载（定界约定为设计核心）；
- 三键 `phonemes`/`knownPhonemes` 分工（第七轮记功）；
- `hitSource` 命名（与宿主侧 `hitStage` 异名有区分度）。

**下轮攻击者须知（第 8 轮后）**：已封闭面=§8.4-8.9 全部、本轮值名统一与 `rule` 枚举（勿再攻错误值名一致性、来源枚举完备性、copy/fallback 边界）；有价值残面不变（发布时刻、S2P/Onset 留白承接真实性、candidates/values 汇流序、§8.9 备案区瑕疵清理、B 轮两治理残面）。

### 8.11 第九轮复核（2026-08-31，发布与版本兼容设计轮）

性质：**设计轮**（用户发问「G2pPackages 发布问题 + chainG2P 碰到比自己更新的资源包 + 声库固定 1.0.0.0 资源包版本的兼容设计」；四项方向经选项式提问拍板：残余版式最小清理 / 新建独立资源仓 / 沿用 spec 2.4 版本模型 / git 内资源目录渐进退役）。主代理单轮调研+设计成文+三文档增补，无子代理（用户明示）。

- 新立文档：[linguist-g2p-package-distribution-draft.md](linguist-g2p-package-distribution-draft.md)——发布渠道（资源仓 release+tag）、manifest 清单、vcpkg port（仿 synthrt `scripts/vcpkg-overlay/ports/ffmpeg-builds` 模式）、wolf 发布纪律、编辑器更新义务与插件管理器预留；本节只记契约面增补与决策；
- 版式补漏：§8.10 调研事实与「裁定不改清单」两条超限行（415/341 字）按第七/八轮口径补拆点，零语义删改。

**决策台账新增 D26-D28**：

- **D26（2026-08-31，发布渠道定案）**：公共语言包分发渠道为 wolf 项目名下**新建独立资源仓**的 GitHub release（tag=资源集 bundle 版本）；
  - vcpkg 自定义 port 与编辑器插件管理器（未来）同源消费同一资产与 SHA512 清单，synthrt git 内 `resources/G2pPackages` 渐进退役（I1 降级旧格式夹具、I2 DSPK 化迁移时转最小测试样例）；
  - 备选「wolf 仓 release」被否：资源 tag 与代码 tag 混用命名空间、发布节奏互相绑架；
  - 连锁：level-1《公共语言包》「随 wolf release 出新版本」措辞改为「经资源仓 release 发布」（发布主体仍为 wolf 项目）。
- **D27（2026-08-31，版本兼容模型定案）**：**沿用 spec 2.4 现成模型，不为依赖方新增区间文法**；
  - 声库 `dependencies[].version` 只写目标点（实际依赖的最低语义版本），兼容区间 `[compatVersion, version]` 由公共包维护；「声库设置 1.0.0.0≤xxx≤2.0.0.0」的诉求由「声库 pin 1.0.0.0 + 公共包声明 compatVersion=1.0.0.0」等价实现（spec 2.4:161 目标点语义 + :396/:406 同路径最高版本候选 + :402 多版本共存）；
  - 依据：兼容区间信息只在资源包作者手里（spec 2.4:189-198 兼容承诺主体为 Package 作者），声库侧预设上限是对未来的猜测；新增文法需动 spec、框架求解器与已发布包缺省语义；
  - 连锁：level-1《公共语言包》增补「版本纪律」小节（规范性：目标点写法 / compatVersion 维护 / 资源格式版本抬升视同破坏 / 编辑器并存安装）；发布纪律四条与编辑器更新义务成文于发布设计文档 §3。
- **D28（2026-08-31，formatVersion 发布联动与不可加载场景）**：chain `formatVersion` 抬升视同所在包的破坏性更新（`compatVersion` 同步抬升），使「chainG2P 碰到比自己更新的资源包」前移到依赖求解阶段、收敛为缺依赖呈现；
  - 连锁：本文 §3 执行细则增补联动条款；rt-api §4.1 增补「包在但不可加载」场景（呈现与缺包同粒度、文案区分升级编辑器/安装兼容版本，rt-api 台账 D-R5）；
  - 兜底链：发布纪律失守（compatVersion 虚报）时依赖求解选中新包→解释器期 formatVersion 拒绝（§1.2 公约 2 诊断）→spec 2.4:396 选中不回退→整链硬失败，属包缺陷。

**下轮攻击者须知（第 9 轮后）**：已封闭面=§8.4-8.10 全部、本轮发布渠道与版本模型（勿再攻「依赖方区间文法缺失」——D27 既定取舍；「compatVersion 纪律可绕过」——发布纪律+诊断兜底双防线，spec :396 选中不回退）；有价值残面=发布实施分期（资源仓建立、port 落地、DSPK 化迁移）、main 目录 Package Loader 布局核对、编辑器预检 lint 增强（发布设计文档 §9 备案区）。

### 8.12 第十轮复核（2026-09-05，消费方对齐轮）

性质：**复核+设计轮**（用户指令：结合 wolf 实际代码，以 spec 2.4 与 synthrt main
为严格规范、ds-editor-lite 为最终调用方，修订语言契约与 g2p 相关文档设计；
缺省语言方案与修订范围经选项式提问拍板）。主代理单轮四平面亲核+连锁修订，无子代理。

**四平面复核结论（锚点主代理逐一亲核，零漂移零失效）**：

- **wolf `cecedba`（工作区干净）**：语言契约/本文全部 wolf 锚点命中——validator
  各分支（`WolfLinguistProvider.cpp:75-153`：linguist 导入目标 interface 等值
  `:81-84`（无 level/variant 校验，§8.7 防线既定）、g2p/s2p 必选 onset 可选
  `:121-125`、singer 侧前缀+全三元组+executiveFactory `:127-150`）、
  `createImportOptions :268-283`、空 `configuration :308-320`、
  `exports.phonemes` 形状校验 `:201-234/:290-306`、IID 字面量
  （`LinguistProviderPlugin.h:13`、`src/plugins/linguistproviders/CMakeLists.txt:3`）
  与嵌入调用（wolf 子目录 `CMakeLists.txt:15`）、extension 挂载 `:249-266`、
  wolf `CMakeLists.txt:64`；G2P/S2P/Onset/G2PModel/DictQuery 在 wolf 零代码
  （API 头仅 interface+level 常量，全仓无 `languageId` 字面量）——现行实现
  注记全部如实；
- **synthrt origin/main**：观察点 `a060af0` 即 HEAD、未漂移；抽查锚点全部命中——
  `PackageLoader.cpp:38-56/692-693/748-750/769-774/782-784/873-893`（钩子调用
  在 `:875`）`/924-944/994/999/1210-1212/1316-1319/1358-1362`、
  `ContribCategory.h:158`、`SynthUnit.h:27-28/66-67`+`SynthUnit.cpp:14-31`、
  `ContribPluginFactory.cpp:64-66/74-83`、`InferenceContrib.cpp:79-86`、
  `DiffSingerProvider.cpp:123-127`、`ContribSpec.h:60-65/101/158-161`、
  `ContribImportBinding.h:14-21/32/62-66`、`ContribExecutive.h:66-99`、
  `PackageHandle.cpp:17-20`、`ContribSpecExtension.h`（findFromSpec 模板）、
  `PackageHandle.h`（contributions/contribution/resolve）、`ITask.h`
  （startAsync/stop/waitForFinished）、`DurationApiL1.h:125-144`、
  `DiffSingerApiL1.h:45-47`；
- **spec 2.4**：main/本分支/工作区三侧同 blob（`3d0c6568`）；抽查
  `:74/:149/:157-161/:183/:189-198/:250/:318/:371/:382/:392-406/:422-460/:545-562/:576/:584-586/:594-606/:616-622/:629-646/:744-769`
  零失效；
- **lite `076ee315`**：rt-api 引用锚点全部实测存在（`Note.h:39-55`、
  `G2pService.cpp:95-110/137-149`、`GetPhonemeNameTask.cpp:137-151`、
  `InferEngine.cpp:219-221`、`G2pInfoWidget`、`PianoRollContextMenuController`）。

**决策台账新增 D29-D30**：

- **D29（2026-09-05，缺省语言与 role 命名定案，用户拍板）**：
    - **动因**：编辑器「跟随歌手」语义（lite
      `PianoRollContextMenuController.cpp:127-140` 音符语言菜单 Follow singer；
      `SingerInfo.cpp:147` 缺省 g2pId 取 defaultLanguage）依赖歌手级缺省语言；
      语言契约原「歌手的语言信息全部经语言贡献表达」句下，语言贡献表达不了
      「缺省」——目标设计存在缺口（编辑器迁移必撞）。
    - **定案**：Singer 语言导入 role 后缀必须为所导语言贡献的语言句柄
      （`linguist/<句柄>`，原「本地自取唯一名+建议句柄」升格为规范性）；歌手
      缺省语言 = 声明顶层扩展键 `defaultLanguage`（ds-singer 契约《贡献条目与
      类别追加字段》既有键，iso 句柄）经 role `linguist/<值>` 命中，未给出或
      不命中时宿主回退（ds-singer 契约建议声明序第一个句柄）。
    - **连带效应**：唯一映射约束（D9）由「role 在模块内唯一（框架既定）+
      后缀=句柄」结构性成立，句柄互不相同成为等价推论；wolf validator 的新增
      校验点为 role 后缀=目标贡献 ID 首字段（目标语义，现行未执行，随迁移
      收敛；就地加严沿语言契约《发布状态》预发布声明）。
    - **落点**：语言契约《组合规则》《自动组合校验的分层落点》《Singer 侧
      配套》三处；ds-singer 契约四处（defaultLanguage 行、imports role 表、
      唯一映射约束、《Configuration for `openvpi`》语言信息句）；rt-api §2.1
      步骤 5 + §2.2 缓存 + 台账 D-R6。
    - **备选存档**：留歌手 `configuration`（需 ds-singer configuration 补键，
      与「`configuration` 不携带任何语言列表」句冲突）；新增 WolfLinguist
      import options 词汇（需推翻「语言 Level 1 无 import options 词汇」既定
      原则 + wolf provider `createImportOptions` 同步实现）——均被否。
- **D30（2026-09-05，DictQuery 宿主工具面经编辑器侧确认，关闭残面②）**：
    - **候选切换 UI 实测存在**：lite `PianoRollContextMenuController.cpp:122-126`
      （`appendPronunciationCandidateActions`，G2P 候选列于音符右键菜单顶部
      一键切换）、`Note.h:45-46`（`pronCandidates` 持久化）、
      `InferControllerHelper.cpp:238` 回写——rt-api §4.2 的多音字人工选择
      消费面成立；
    - **候选唯一来源=G2P 输出**（`GetPronunciationTask.cpp:251-254`），编辑器
      无词典直查 UI——DictQuery 面定位确认为「锁音素词按键补查」（rt-api
      §4.2 既定）与未来人工查词工具，G2P `candidates` 仍是主通道；
    - **消费方红利**：旧栈 dict 步候选为音素 token 拆分，lite 被迫双处实现
      `normalizePronunciationCandidates` 规整（`G2pService.cpp:29-52`、
      `GetPronunciationTask.cpp:37-55`）——契约 candidates 语义（首个即主
      发音、dict 命中=词典值组，本文 §3 candidates 规整）落地后该 workaround
      失效为历史；
    - **语言句柄实测**：lite 语言清单 cmn/yue/jpn/kor/eng（iso-639-3 句柄）+
      工具类 num/punc（宿主预过滤域，不进 linguist 链）——证实语言句柄设计
      与宿主预过滤分工（rt-api §3.1）；
    - **懒加载/预热习惯**（`SynthrtEngine.h` deferLanguageModels、
      `InferEngine.cpp:219-221`）与 rt-api §2.2/§4.3 对齐确认；
    - **残面③裁决**（G2P `candidates` 与 DictQuery `values` 的汇流序）：
      宿主策略不钉——编辑器 UI 只呈现 G2P candidates，DictQuery 补查为
      锁音素词候选来源；汇流序若有真实需求随宿主实现浮现。

**§8.9 备案区批量处理（17 项，本轮全部关闭）**：

- **措辞与一致性 6 + 冗余 3**：五契约 Configuration 措辞统一为「由 `variant`
  全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录
  为基」；Onset 省略告警补「口径同」回链；语言契约 `knownPhonemes` 转述补
  「类型名被至少一条规则引用」限定；Singer 侧注记「分两点」正为「分三点」；
  S2P/Onset「形状校验同注 1」存在性陷阱改写（明示本键可省略、与
  WolfLinguist `phonemes` 的存在性差异）；「兜底由宿主决定」改「失败词的
  处置（如原词保留）由宿主决定」（消解与 fallback 步术语撞车）；实现注记 3
  改引用式（消解与《WolfLinguist Exports》尾句重复）；
- **可读性 4**：语言契约 imports 连续注记块合并为七点单块；嵌套括注降一层；
  G2P/G2PModel Variables 语义块加 `####` 子标题（能力边界/输入合法性判定/
  词汇表/error 值域/运行时路径/输出共现约束/关联契约——《G2P Variables·
  运行时路径》自此有实指落点）；**《组合规则》/《imports（组合规则）》节名
  撞车按版式红线裁决不改**（节名为 30+ 处跨文档互引锚点，改动即漂移——记
  裁决定案，不修改）；
- **行号漂移 2**：Singer 侧 validator 校验枚举补 executiveFactory 存在性
  （亲核狼仓 `:145-148`）；dspkg 路径补全 `dspkg-core/src/converter/singer.rs:64-120`
  （亲核 `D:\projects\dspkg`）；
- **治理残面 2**：语言契约「各变体」澄清为「wolf 收录变体」（第三方变体键汇
  由第三方自管）；公共 G2P 第三方 v2 全名 ID 策略补「由 wolf 发布注记定夺」句。

**顺手项**：`docs/modules/g2p.md` `lstm`→`multig2p` 事实修正（三处；本分支
`plugins/G2P/` 目录实测无 lstm、只有 multig2p——§8.5 备案的文档 drift 本轮关闭）。

**下轮攻击者须知（第 10 轮后）**：已封闭面=§8.4-8.11 全部 + 本轮四平面复核与
D29/D30（勿再攻「缺省语言无承载」「唯一映射需独立校验」「DictQuery 消费面未
确认」「备案区瑕疵」）；有价值残面=①发布时刻（§8.9 残面①照旧，预发布修订
自由已耗六笔）；②本文对 S2P/Onset 留白的承接真实性（照旧）；③G2P candidates
与 DictQuery values 汇流序已裁决为宿主策略（关闭）；④§8.9 备案区已清空
（关闭）；⑤治理残面两项已随本轮澄清/补句关闭；⑥wolf 打包期 lint 是否单独立项
（D22 修正条款留下的立项口，照旧）；⑦D13 存量资源盘点、multig2p bundle
口头确认、残面⑤ JSON Schema 发布物（照旧）。
