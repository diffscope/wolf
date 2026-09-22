# wolf 语言域变体参考（正交面）

本文是**变体面**：各 `variant` 的 `configuration` 键汇与资源格式。它与分层主栈正交——
契约层（[linguist-domain-contract.md](linguist-domain-contract.md)、
[linguist-inference-contract.md](linguist-inference-contract.md)）只规定
「`configuration` 由 `variant` 全权规定」，具体内容全在本文。

本文属 **wolf 实现文档**，不是对外契约。上位规范 [spec 2.4](ds-spec-2.4.md)；
分层见 [linguist-architecture.md](linguist-architecture.md)；决策见
[linguist-decisions.md](linguist-decisions.md)。

> **口径**：本文写**目标形态**。各变体的键汇已逐条对照 synthrt `origin/refactor` 的实现核过
> （移植来源见 §8），实现与目标的差异集中在各节的「移植注记」。原变体草案中涉及
> G2PModel / DictQuery 契约、`options.languageId`、按贡献 ID 匹配语言的部分**一律作废**
> （A2 / A3 / A16），该草案已删除。

## 1. 变体治理

### 1.1 命名

- 变体名属实现方命名空间，编码为单个 `segment`（`[A-Za-z0-9_-]+`，
  `synthrt/lib/Core/ContribLocator.cpp:43-53`）；
- wolf 收录变体的命名惯例：
  - G2P 宿主面保留 `pipe-` / `algo-` 类前缀（编排类 / 规则算法类）；
  - S2P、Onset 沿用既有库类名切分；
- **全部裸变体名为 wolf 官方保留**；第三方按 spec 2.4:562 使用反向域名变体
  （如 `com.vendor.myengine`）。

### 1.2 变体名即分派键

- 框架按 (`interface`, `level`, `variant`) 三元组**全匹配**选择 provider，`configuration`
  内容**不参与**选择（spec 2.4:576、:590）；
- 选择按「目录顺序 + 目录内文件名顺序」形成的全序扫描，**首个全匹配者当选**，后续相同三元组
  不参与（实现见 `synthrt/lib/Core/ContribPluginFactory.cpp:74-83`）；
- 三元组命不中即「找不到提供者」加载失败（`PackageLoader.cpp:769-774`，Probe 阶段报
  `FeatureNotSupported`）。**不存在运行期再分派**；
- 「同一变体名只许一个 provider 插件承载」是 wolf 对收录变体的**收录纪律**（使公共包的
  provider 选择结果确定），并非框架约束——框架侧只规定首个全匹配者当选。

### 1.3 演进判据

按 spec 2.4:744-753：

| 情形 | 处置 |
| :-- | :-- |
| 只有解释器关心 | 写入 `configuration`；不动 `level`，必要时升资源 `formatVersion` |
| 导入方需要，现有词汇表说得圆 | 用现有词汇表表达，不动 `level` |
| 导入方需要，现有词汇表缺词 | 递增 `level` |
| 输入输出已根本不同 | 另起 `interface` |

**模型后端与词典查询在 Level 1 不单独立契约**（A2）：它们是 `pipe-chain` 变体的内部事务，
经该变体自己的 `imports` 与 `configuration` 表达。

## 2. `configuration` 公约

`configuration` 由 `variant` 全权规定（spec 2.4:604、:620），契约不设统一外层形态。本族约定
三条：

### 2.1 路径解析

- `configuration` 中的路径以**模块声明文件所在目录**为基（`${vars}` 已由 Loader 展开）；
- 解释器**不得**对已展开字符串再次套用展开规则（spec 2.4:88）；
- 经路径键定位的家族配置文件，其**内部**路径再以该文件自身所在目录为基。

> 书写时务必对齐这两级基准。原变体草案的 pipe-chain 示例即在此处自相矛盾——声明写在
> `inferences/g2p/inference.json` 却引用 `./g2p/chain.json`，实际会解析到
> `inferences/g2p/g2p/chain.json`。

### 2.2 格式版本自验

- 配置（内联键汇或家族配置文件）顶层可携 `formatVersion`（单调递增正整数）；
- 解释器声明自身支持上限，支持 `[1, 上限]` 全区间；读入超过上限即**加载失败、不得回退**，
  诊断须携带「声明版本 > 支持上限」与升级指引；**对采纳本条的变体**，缺键、非正整数、文件不可
  解析同按加载失败处理（未采纳本条者见 §3.3 对 S2P 与 Onset 的豁免）；
- `level` 管契约能力，`formatVersion` 管变体内部格式演进，两层互不替代；
- 打包/编辑器等静态工具**不加载插件即可读** `formatVersion` 预检；
- **发布侧联动**：资源 `formatVersion` 抬升视同所在包的破坏性更新，包级 `compatVersion` 必须
  同步抬升——须在依赖求解阶段就排除旧目标版本命中新包，不依赖解释器期拒绝兜底。

### 2.3 拒绝未知键

各变体可约定「遇未知键即加载失败」，以防拼写错误静默生效。该约定只作用于**变体定义的
object**，属 spec 2.4:74 末句「贡献类别、interface 与 variant 定义的 object 按各自 schema
处理」的豁免口径，与框架定义 object 的「未知字段不得拒绝」不冲突。

## 3. 收录变体清单

### 3.1 G2P（`org.openvpi.wolf.inference.G2P`）

| variant | 大类 | 外部依赖 | 要点 |
| :-- | :-- | :-- | :-- |
| `pipe-chain` | 编排 | 经 `imports` 引用的模型后端模块 | 打标校验 → 词典/规整（可复步交错） → 模型 → 兜底；步序由 `chain.json` 定。**唯一经 `imports` 暴露依赖的变体** |
| `algo-pinyin` | 规则算法后端 | 无 | cpp-pinyin 引擎（普通话 / 粤语：查表 + 规则转换）；发布为独立包 `wolf/g2p-pinyin`，被 cmn / yue 两条 `pipe-chain` 共用（§6） |
| `multig2p-onnx` | 模型后端 | dsinfer 的 ONNX 驱动（由宿主注册为 Runtime Service） | seq2seq 模型；发布为独立包 `wolf/g2p-multi`，被 9 个语言的 `pipe-chain` 共用（§5） |

两个后端变体（`multig2p-onnx`、`algo-pinyin`）都必须声明 `exports.languages`，且与各自的
`languageMap` 逐项对账（§5.2、§6.2）。编排类的 `pipe-chain` 按链推理契约 §2.1 声明。

### 3.2 S2P（`org.openvpi.wolf.inference.S2P`）

| variant | `configuration` | 说明 |
| :-- | :-- | :-- |
| `dict` | `file`（必选，path）：TSV，每行 `发音\t音素1 音素2 …` | 词典整体查表；未命中产出空音素序列 |
| `direct` | 无 | 按 ASCII 空格把发音拆成音素列表（空段丢弃；制表符不作分隔符） |
| `mapping` | `file`（必选，path）：TSV，每行 `原音素\t目标音素` | 逐音素替换；未列入表的音素原样透传 |
| `lua` | `file`（必选，path）：Lua 脚本（LuaJIT），须定义全局函数 `s2p(发音) → list<string>` | 脚本转换，沙箱见 §7 |

`exports.phonemes`（产出集）推导口径：`dict` 取 TSV 目标列的音素并集；`mapping` 取
「目标列 ∪ 透传域」的上界集；`direct` 开放无界、`lua` 不可静态推导——后两类由作者显式补齐
或省略。

`exports.languages`（可消费的二元组）由作者按变体实际资源声明。`direct` 与 `lua` 对任何体系
都成立，故**通用模块应省略本键**而不是编造声明。

> **装在语言包内的除外**：一个随 `wolf/lang-eng` 发布的 `direct` 模块就是为这门语言服务的，
> 声明 `[{eng, arpabet}]` 是真话而非编造，且能让二元组匹配在**加载期**跑起来——省略只会让宿主
> 每装一个语言包告警一次（域契约 §5.3）。`wolf/lang-zxx` 与四个已闭包的语言均按此办。

### 3.3 Onset（`org.openvpi.wolf.inference.Onset`）

| variant | `configuration` | 说明 |
| :-- | :-- | :-- |
| `rule` | `file`（必选，path）：JSON 规则定义 | 模式匹配标记 |
| `lua` | `file`（必选，path）：Lua 脚本，须定义全局函数 `markonset(音素表) → 等长布尔表` | 脚本标记，沙箱见 §7 |

`rule` 文件结构：

```json
{
    "phonemeTypes": { "b": "consonant", "a": "vowel" },
    "rules": [ { "pattern": ["vowel"], "onsets": [0] } ]
}
```

- `phonemeTypes`：非空 object，音素 → 自定义类型名（类型名不得为保留词 `*`）；
- `rules`：`pattern` 为音素或类型（含通配 `"*"`）组成的序列，`onsets` 给出匹配序列中处于
  onset 位置的下标；匹配按**最长、最特异者优先**；同一字符串同为字面音素与登记类型名时按
  字面音素段处理（字面较类型特异）；
- 未登记于 `phonemeTypes` 的音素不匹配任何类型段，仅可匹配字面音素段与通配 `"*"`；
- 输入中未被任何规则覆盖的位置输出 `false`（整条无匹配时输出全 `false`）——这是**目标合法
  语义**，不是错误。

`exports.knownPhonemes`（识别集）推导口径：`rule` 变体取 `phonemeTypes` 中**类型名被至少一条
规则引用**的键集，并并入 pattern 字面音素段。因 `"*"` 通配段功能上覆盖任意输入，派生集是
覆盖面的**下界**；识别集低估不构成缺陷。`lua` 不可静态推导。

> **B2 已解决**：旧栈 `rule` 变体的字面音素段永不匹配——插入时 pattern 各段被一律标为通配
> （`lib/S2P/RuleOnsetMarker.cpp:296-299`），精确匹配分支与特异性计数恒退化为死代码，尽管结构
> 本就备有 `exactChildren` 字段。**wolf 的实现按本节目标语义重写，未搬入该缺陷**，并有专门
> 用例覆盖「字面段胜过其类型段」。
>
> 「修复即改变现网行为」的顾虑在**仓内**不成立：四仓中没有任何带实质内容的生产 rule JSON。
> 但**分发的声库包不受此豁免**——清点公开分发的 7 款声库包，各带 4 份规则文件（共 **28 份**，
> 其中 `assets/eng.json` 的文件名不含 `onset`，按名字搜索会漏掉四分之一），顶层键一律只有
> `phonemeTypes` 与 `rules`、**没有一份**带 `formatVersion`。规则文件在 Acquire 期解析
> （`onset/main.cpp:164-178`），解析失败即**包级加载失败**，故"新增必填顶层键"的改动会拒载现网
> 声库包。本段不足以作为"改这里零风险"的依据。

S2P 与 Onset 的 `configuration` **不设 `formatVersion`**：键汇极简，以 §2.3 的「拒绝未知键」
守住演进。二者的**家族配置文件**（`configuration.file` 指向的 rule JSON）同样**不设必填**
`formatVersion`：形状演进由缓存代际常量承担（`onset-rule-json@1`，`onset/main.cpp:28-30`），
读入端以「拒绝未知键」守住拼写演进——该文件在 Acquire 期解析，必填会拒载已分发的声库包（见
上文 B2 之后的清点）。若文件自行声明 `formatVersion`，按 §2.2 校验：缺省视作 1、非正整数与
超过上限按 §2.2 处理。

## 4. `pipe-chain`（编排类 G2P）

线性管道：把一个词依次交给若干 step，先产出发音者胜。当前是编排类的唯一成员；DAG 编排、
条件路由等未来风格同属此类并共用 `pipe-` 前缀。

### 4.1 `configuration`

```json
"configuration": {
    "formatVersion": 1,
    "steps": [
        { "step": "verify",   "params": { "entries": [ { "type": "regex", "value": ["([A-Za-z'\\-]+)"], "mode": "convert" } ] } },
        { "step": "dict",     "params": { "file": "./cmudict.txt" } },
        { "step": "format",   "params": { "operations": ["lowercase"] } },
        { "step": "dict",     "params": { "file": "./cmudict.txt" } },
        { "step": "model",    "params": { "role": "backend", "batchSize": 20 } },
        { "step": "fallback", "params": { "useOriginal": true } }
    ]
}
```

| key | type | 必选 | 说明 |
| :-- | :-- | :-: | :-- |
| `formatVersion` | int | 是 | 单调递增正整数，§2.2 公约 |
| `steps` | array&lt;object&gt; | 是 | 有序步骤表，**上限 50 步** |

**steps 内联，不引外部 chain 文件。** `configuration` 本就是 variant 全权规定的块，`formatVersion`
可直接写在它顶层（§2.2），再套一层文件只多一级路径基准而不多任何表达力——原草案的 chain.json
方案正是在这一层写错了路径基准（见 §2.1 的告诫）。声库要覆写链路，覆写整个 `inference.json`
即可（语言域契约《逐项覆写》）。

每个 step 条目：

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `step` | string | 是 | 步类型，值域见 §4.2；未知类型即加载失败 |
| `enabled` | bool | 否 | 缺省 `true`；为 `false` 时该步整条不入列 |
| `params` | object | 否 | 该步的参数；省略等价于空对象（各步取自身缺省） |

同一类型可重复出现（现网夹具即「词典 → 规整 → 词典」复步）。

### 4.2 step 词汇

| step | params | 作用 |
| :-: | :-- | :-- |
| `verify` | `entries: [{type, value, mode}]` | 逐词打标，决定 `mode` |
| `dict` | `file`（path，必选） | 词典查表 |
| `format` | `operations` / `stripTrailingSpace` / `addSpaceBetweenPhones` | 文本规整 |
| `model` | `role`（必选）、`batchSize` | 消费一个被 `role` 点名的后端模块 |
| `fallback` | `useOriginal`、`defaultPronunciation` | 兜底策略 |

#### `verify`

`params.entries` 是条目数组（键名不与步名重复）。每个条目：

| key | type | 说明 |
| :-: | :-: | :-- |
| `type` | enum | `regex` \| `array` \| `dict`，**三型必须全部实现；未知型即加载失败** |
| `value` | array&lt;string&gt; | `regex`：正则（多条合并为 `\|` 择一，整串 full match）；`array`：字面词表；`dict`：TSV 路径（相对模块声明目录），取每行首列为词 |
| `mode` | enum | 命中时该词的 `mode`：`convert` \| `copy` |

未被任何条目命中的词取 `copy`。

> **条目形状与 `algo-pinyin` 统一**：两个变体做的是同一件事（逐词分类为 convert/copy），迁移后
> 共用同一个 `Verifier` 组件，因此**共用同一套条目形状** `{type, value, mode}`。旧栈 chain 侧的
> `action` 与 pinyin 侧的 `mode` 是同义异名，统一到后者（该组件的既有形状，改动量为零）；步
> 类型名由 `tagAndValidate` 改为 `verify`。容器名按各自语境取：chain 是步参数 `entries`，
> algo-pinyin 是 `configuration.verify`。

#### `dict`

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `file` | path | 是 | TSV 词典，每行 `词\t发音`；重复词按声明序合并为候选组 |

命中时 `pronunciation` 取首个候选，`candidates` 取整个候选组，`hitSource` = `dict`。

词典格式**严格**：分隔符只认制表符，行内不得缺列，空行忽略。另有两条读入约定：

- 首行 UTF-8 BOM 剥离；
- 词尾 `(` + 纯数字 + `)` 的 CMU 式变体后缀就地剥离，`word(2)` 与 `word` 归并为同一候选组
  （按文件序）——不剥离的话那些键永无人查。

> **发布侧的对应义务**：不合式的存量词典由转换管线归一化，**不靠读入端放宽**。现网清点出两处：
> `fil_dict.txt` 用空格分列（旧栈只认制表符且静默跳过不合式行，故其 24752 条**一条也没被读到
> 过**）、`kor_dict.txt` 与 `ds_cmudict-07b.txt` 开头有 `;;;` 引文行。均在
> `scripts/convert-g2p-packages.py` 内改写为规范 TSV。

#### `format`

| key | type | 缺省 | 说明 |
| :-: | :-: | :-: | :-- |
| `operations` | array&lt;string&gt; | `[]` | 清洗操作序列；Level 1 值域为 `lowercase`（UTF-8 逐码点小写映射），未知操作即加载失败 |
| `stripTrailingSpace` | bool | `false` | 去掉产出发音的尾随空格 |
| `addSpaceBetweenPhones` | bool | `false` | 在音素间补空格 |

`operations` **不嵌在 `cleaner` 对象里**——旧栈的 `cleaner` 只有 `operations` 一个成员，那层
嵌套不携带信息（§2.3 的同一把尺子）。

清洗只作用于**尚未产出发音的词的歌词文本**，结果写入清洗后词；其后的产出步一律取清洗后词。
规整面改写 `pronunciation` 时**必须同步改写 `candidates` 各元素**——契约的共现约束在链内全程
成立。

#### `model`

| key | type | 必选 | 缺省 | 说明 |
| :-: | :-: | :-: | :-: | :-- |
| `role` | string | 是 | — | 点名本模块 `imports` 中的一个条目，其目标契约须为 `org.openvpi.wolf.inference.G2P` |
| `batchSize` | int | 否 | 实现定 | 分批调用后端的批大小 |

- **按极大连续段分批**：本步只作用于 `mode=convert`、未丢弃、尚无发音的词，但送入后端时按
  **原词序中的极大连续段**切分，不把过滤后的词拍平成一张表。后端可跨相邻词取上下文
  （`algo-pinyin` 的短语表即是，§6.5），被别的词隔开的两个词不是邻居；
- `batchSize` 缺省为**不再切分**（一段一次调用）。显式给出时在段内再切，因此对上下文敏感的
  后端不应设置它——逐词模型（`multig2p-onnx`）不受影响；
- 步内**不写 ref 引用串**，只写 role；被点名模块经框架的 import binding 解析；
- 点名的 `role` 不存在、多个 model 步点名同一 `role`、或所指条目的目标契约不是 G2P 的，均即
  本模块加载失败。未被任何 model 步点名的 G2P 导入不注入、不视为错误；
- 绑定只认 `role`，不依赖声明顺序；
- **不写语言参数**。后端执行体由本模块创建，其 `(language, scheme)` 由本模块自身的绑定注入
  （A16 / 链推理契约 §6.3）。旧栈的 `langRef`（内部子资源标识，下划线词法如 `eng_plus`）
  不出模块边界。

后端逐词返回的 `error` 按下表收敛为本链输出的 `error`，不叠加新枚举值：五值
（`ModelInferenceFailed` / `PhonemeGenerationFailed` / `DriverUnavailable` / `NotInitialized` /
`UnknownError`）同名透传；`InvalidInput` 同名透传。后端调用整体失败标 `ModelInferenceFailed`，
驱动不可用标 `DriverUnavailable`。携带 `error` 的词仍按后续 `fallback` 步处理。

#### `fallback`

| key | type | 缺省 | 说明 |
| :-: | :-: | :-: | :-- |
| `useOriginal` | bool | `true` | 以原词作兜底发音 |
| `defaultPronunciation` | string | `""` | `useOriginal` 为假时的固定兜底发音 |

只处理**未丢弃、`mode=convert`、且发音仍为空**的词。

- **兜底成功产出时 `error` 必须为空**，`hitSource` = `fallback`；
- 只有兜底也未产出（`useOriginal=false` 且 `defaultPronunciation` 为空）时，才以
  `error=PhonemeGenerationFailed`、发音与候选为空产出，**不得回填原词**；
- 链未配置 `fallback` 步时同上。

> 旧栈在此处有缺陷：`FallbackStep::handle` 无条件置
> `errorType = PhonemeGenerationFailed`，即使 `useOriginal` 已经产出了发音。那与契约的
> 「`error` 非空即失败、`mode`/`candidates` 无定义」直接冲突，移植时按本节修正。

### 4.3 单一来源守卫

`dict` / `model` / `fallback` 三个产出发音的步一律只作用于 **`mode=convert`、未丢弃、且尚无
发音**的词。由此：

> 逐词候选只取**单一最终来源**——词典命中即词典值组（声明合并序保留），后端产出即后端候选
> 序列，兜底产出即兜底单值。

Level 1 **不做跨源合并、不去重、不设数量上限**。该性质由上述守卫成立，**与步序无关**，因此
同链可含多个同型步。跨源合成与截断若将来需要，属 `formatVersion` 演进，`level` 不变。

### 4.4 移植注记

旧栈（`plugins/G2P/chain/**`）与本节目标形态的差异，移植时须一并收敛：

| 项 | 旧栈 | 目标 |
| :-- | :-- | :-- |
| 配置入口 | 直读 `configuration.steps`，无 `formatVersion` | 同左，**增** `formatVersion` 必选 |
| 打标步 | `tagAndValidate`，键 `tagger` / `action` | `verify`，键 `verify` / `mode` |
| 打标三型 | 只编译 `regex`，`array`/`dict` **静默丢弃**（`TagAndValidateStep::compileEntries`） | 三型全实现，未知型加载失败（复用 `Inferutil::Verifier`，该组件已具三型且未知型即报错） |
| `enabled` 双轨 | 步项级与 `params.enabled` 两处独立 | **只保留步项级**；`params.enabled` 删除 |
| `format` | `cleaner.operations` 嵌套；`normalizeTones` 解析后**从不使用**（死参数） | 扁平 `operations`；删 `normalizeTones` |
| `model` 绑定 | `id` 点名旧框架任务 + `langRef` 直递内部语言引用 | `role` 点名 import + 二元组经 RuntimeOptions 注入 |
| `model` 冗余键 | 夹具携带不被读取的 `file` | 无 |
| `model` 分批 | 按 `batchSize` 切分过滤后的**全表**，相邻性丢失 | 按原词序的极大连续段切分（§4.2 `model`） |
| `dict` 读入 | 只认制表符，不合式行**静默跳过**（`PhonemeDict.cpp:188-196`） | 同样只认制表符，但不合式即**加载失败**；不合式存量在转换期归一化 |
| 目标契约校验 | 无 | `createImportValidators` 在 Commit 前校验每个 `role` 已绑定且目标为 G2P |
| `fallback` | 无条件置 `PhonemeGenerationFailed`；夹具携带不被读取的 `markFailed` | 仅未产出时置该 error；无 `markFailed` |
| 步数上限 | 50（硬编码） | 保留 50 |

## 5. `multig2p-onnx`（模型后端 G2P）

seq2seq ONNX 模型后端：词 → 发音与候选。发布为独立包 `wolf/g2p-multi`，被 9 个语言包的
`pipe-chain` G2P 经 `imports` 共用。

**它声明的是 `G2P` 契约，不是某种「后端专用契约」**（A20）：共享一个大模型必须让它成为独立包
中的模块，模块必须有契约，而声明了 G2P 就必须满足 G2P（`mode` / `copy` / `skip` / `error`
一应俱全）。因此它既可被 `pipe-chain` 的 `model` 步消费，也可被某个语言直接用作
`linguist/g2p`——「纯模型、无编排」是一种合法的语言链配置。

### 5.1 资源布局

```
+ inferences/multig2p
  - inference.json      // 模块声明
  - bundle.json         // 资源清单与格式版本
  - vocabulary.json     // 词表
  - encoder_int8.onnx
  - decoder_step_init_int8.onnx
  - decoder_step_int8.onnx
```

**`bundle.json` 必须与模块声明文件同目录，不设指向它的配置键。** 一个 bundle 对应一份声明，
路径可由声明位置确定——写一个 `bundle` 键只会多一种表达同一件事的方式（§2.3 的同一把尺子）。

### 5.2 `configuration`

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `languageMap` | array&lt;object&gt; | 是 | 契约二元组 → bundle 内部语言引用的映射，见下 |
| `maxLen` | int | 否 | 解码长度上限，缺省 48 |
| `beamSize` | int | 否 | beam 宽度。**本实现只支持 1**，见 §5.5 |
| `topK` | int | 否 | 候选数上限，经 `candidates` 上链。**本实现只支持 1**，见 §5.5 |
| `lengthPenalty` | number | 否 | 长度惩罚。**本实现只支持 0**，见 §5.5 |

```json
"configuration": {
    "languageMap": [
        { "language": "eng", "scheme": "arpabet", "ref": "eng/default" },
        { "language": "deu", "scheme": "ipa",     "ref": "deu/default" }
    ],
    "topK": 5
}
```

**`languageMap` 是暴露面到内部子资源的唯一通道**（链推理契约 §2.4）。bundle 内部的语言引用
形如 `eng/default` / `eng/plus`（含斜杠的家族内标识），**不出模块边界**；契约面只有
`(language, scheme)`。

`exports.languages` 必须与 `languageMap` 的二元组集合**逐项一致**，由 provider 在 Acquire
校验——两者一个是契约面、一个是实现面，跨面不可互相推导，因此必须各自声明并对账。契约允许
省略 `exports.languages`（链推理契约 §2.1），但**本变体不允许**：省略即无从对账。

**无 `default_language`**：执行体在创建时已绑定单一二元组（A16），不存在「缺省语言」。绑定值
不在 `languageMap` 中即加载失败。

### 5.3 `bundle.json`

| key | 说明 |
| :-- | :-- |
| `bundle_version` | **本变体的资源格式版本**，按 §2.2 公约处理（解释器声明支持上限，超限拒绝加载） |
| `files` | 逻辑名 → ONNX 文件名的映射，路径以 `bundle.json` 自身目录为基 |
| `languages` | bundle 收录的内部语言引用清单，`languageMap` 的 `ref` 必须命中其中一项 |

其余顶层键（`schema_version`、`model_version`、`min_runtime_version`、`vocab_hash`、
`opset_version`、`export_flags`、`generated_at`）是**发布侧内部元数据，契约不解释**。

### 5.4 驱动归属与降级

**驱动是宿主的，不是模块的。** 宿主创建 `ds::InferenceDriverFactory`、找到 `onnx` 后端、以
`DriverInitArgs`（execution provider 与 ORT 路径）初始化，再经 `SynthUnit::addRuntimeService`
注册；模块按后端名查 Runtime Service，**自己绝不加载驱动**。

这条归属线也决定了两种失败的分野：

| 情形 | 归属 | 处置 |
| :-- | :-- | :-- |
| 进程内没有 ONNX 驱动 | **安装环境**的属性 | 包正常加载，逐词报 `DriverUnavailable`，交由链上 `fallback` 兜底 |
| 驱动在，模型打不开 | **包**的属性 | 执行体创建失败，加载失败 |
| 单次推理失败 | 运行期 | 该批全部词报 `ModelInferenceFailed`，仍走 `fallback` |

前者若判为加载失败，一台没装驱动的机器上整个语言包都会不可用——而契约本就备了逐词的
`DriverUnavailable` 通道，正是为此。

### 5.5 解码范围：贪心，不含 beam search

**本实现只做贪心解码**（一步一个 argmax token）。`beamSize`、`topK` 取值大于 1，或
`lengthPenalty` 非 0，一律**加载失败并指名不支持**，不静默降级。

两条理由：

1. **唯一存在的资源不需要它**——`wolf/g2p-multi` 的配置是 `beamSize: 1, topK: 1,
   lengthPenalty: 0.0`；
2. **接受键却忽略它会谎报模块能力**。旧栈那 366 行 beam search 无任何现网资源触及，也就无对照
   输出可验；照搬一份验不了的实现，比一句「不支持」更危险。

真正的候选（`topK > 1`）必须靠 beam search 才能产出，故 `topK` 与 `beamSize` 同受此限。将来补上
beam search 属 `formatVersion` 不变的实现增强，`level` 不动。

### 5.6 移植注记

| 项 | 旧栈（`plugins/G2P/multig2p/**`） | 目标 |
| :-- | :-- | :-- |
| 契约归属 | 独立的 `g2p.model.Multig2pInference` 插件键 | `G2P` 契约、变体 `multig2p-onnx`（A20） |
| bundle 定位 | 取模块目录自身（`TaskImplBase.cpp:48`） | 同左，明确为规范（§5.1） |
| `languageMap` | **未实现**；`languageId` 字串直送查表，归一化函数 `normalizeLanguageId` 全仓零调用（死代码） | 必选，且与 `exports.languages` 对账 |
| 未映射语言 | **静默回退默认语言**（`TaskImpl.cpp:111-119` 只记录缺失下标，产出循环从不检查） | 绑定期即失败；无「缺省语言」概念 |
| 语言引用词法 | 下划线形态如 `eng_plus` 与斜杠形态 `eng/plus` 混用 | 内部形态由 bundle 决定，不出边界；契约面只有二元组 |
| `bundle_version` | 仅校验非空，无版本比较（`BundleLoader.cpp:133-142`），且为字符串 `"1.0"` | 正整数，按 §2.2 做 `[1, N]` 上限校验；存量的 `"1.0"` 由转换管线归一化为 `1`（A34 的同一把尺子） |
| 保留符号下标 | 硬编码 `unk=0 / pad=1 / bos=2 / eos=3`（`BundleLoader.h`） | 按名字（`<unk>` / `<pad>` / `<bos>` / `<eos>`）在词表中查；缺任一即加载失败 |
| 词表查询 | `lookup` 线性扫描全表，逐字符调用 | 哈希表 |
| 驱动获取 | 自 G2P Manager 的 driver 类别取单例 | 宿主注册的 Runtime Service（§5.4） |
| 解码 | 贪心 + beam search 两条路径 | 仅贪心；beam 相关键取值受限并指名不支持（§5.5） |
| 发音串 | 逐音素追加空格，**留尾随空格** | 空格分隔、无尾随空格 |
| 驱动缺失 | 初始化期仅告警并回退 copy，运行期逐词 `ModelInferenceFailed` | 按契约报 `DriverUnavailable` |
| `configuration` | 夹具是训练配置全量 dump，运行时只读其中 `inference.*` 五个键 | 只保留 §5.2 的五个键，拒绝未知键 |
| `topK` 候选 | 真实候选未上链（被弃） | 经 `candidates` 上链（链推理契约 §3.4.3） |

## 6. `algo-pinyin`（规则算法后端 G2P）

基于 cpp-pinyin 的查表 + 规则转换，无外部推理依赖。**发布为独立包 `wolf/g2p-pinyin`**，被
`wolf/lang-cmn` 与 `wolf/lang-yue` 两条 `pipe-chain` 经 `imports` 共用。

### 6.1 为什么是后端而不是语言包内的模块

cpp-pinyin 的词典根是**进程全局态**，且**只在引擎构造期被读一次**：

| 事实 | 锚点（cpp-pinyin `3924631`） |
| :-- | :-- |
| `dictionaryPath()` / `setDictionaryPath()` 读写一个文件作用域的全局对象，无同步 | `src/G2pglobal.cpp:13-21` |
| 全局量的唯一读取点是 `ChineseG2pPrivate::init()`，由 `ChineseG2p` 构造函数调用 | `src/ChineseG2p.cpp:80`（构造函数在 `:110-113`） |
| 构造完成后实例自持四张词表，不再触碰全局 | `src/ChineseG2p_p.h:22-25` |
| 两个引擎是**同一根下的两个子目录**：`Pinyin = ChineseG2p("mandarin")`、`Jyutping = ChineseG2p("cantonese")` | `include/cpp-pinyin/Pinyin.h:13`、`Jyutping.h:13` |

旧栈两个语言包各带一份词典根，于是必然冲突：后构造者覆盖全局，先构造者的 `initialized()`
静默变假，整条链退化。**根因不是「全局态」，是「一个进程里有两个根」。**

而这两份词典**逐文件比对与 cpp-pinyin 自带的 `res/dict` 字节相同**（11 个文件中 10 个完全
一致，`mandarin/trans_word.txt` 少一行 `吒:咤`，即包比上游旧一版）。它是**引擎载荷，从来不是
语言内容**——出现在语言包里本身就是旧栈的分层错误。

因此收敛为一个后端包：**一个进程一个根，两个引擎各取子目录**，与 `multig2p-onnx` 同形
（A20 的第二个佐证）。副产品是 cmn / yue 与其余 11 个语言结构一致：每个语言都是
「`pipe-chain` 覆在共享后端上」。

> 原文档「cpp-pinyin 新版已移除 `setDictionaryPath` 旧 API」一句**与实际源码不符**，已按上表
> 更正。

### 6.2 `configuration`

```json
"configuration": {
    "formatVersion": 1,
    "dictRoot": "./dict",
    "languageMap": [
        { "language": "cmn", "scheme": "pinyin",   "ref": "mandarin"  },
        { "language": "yue", "scheme": "jyutping", "ref": "cantonese" }
    ]
}
```

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `formatVersion` | int | 是 | 单调递增正整数，§2.2 公约 |
| `dictRoot` | path | 是 | 引擎词典**根**（相对模块声明目录），其下按 `ref` 分子目录；必须存在 |
| `languageMap` | array&lt;object&gt; | 是 | 契约二元组 → 引擎内部引用，非空、二元组不得重复 |

- `ref` 值域是**编译期闭集**（`mandarin` / `cantonese`），未收录者即加载失败——比 multig2p
  的 `bundle.json.languages` 更强，因为引擎类本就编译在插件里；
- `exports.languages` **必选**，与 `languageMap` 的二元组集合逐项对账（同 A24）；
- 按 §2.3 拒绝未知键。原 `dictPath` 键**改名为 `dictRoot`**：语义从「本语言的词典目录」变为
  「引擎词典根」，同名异义比新名危险，故改名；旧格式包无 `formatVersion`，按 §2.2 响亮失败。

### 6.3 不设 `verify` 键

打标归 `pipe-chain` 的 `verify` step（A22 已使两者条目形状完全相同，迁移是零翻译搬运）。后端
因此彻底语言无关。

作为直接的 `linguist/g2p` 使用时本变体不打标，全部词按 convert 处理——对纯汉字输入无差别，
混排输入则由 §6.5 的兜底口径收敛。

### 6.4 进程级词典根仲裁（B1 的收口）

拆包把「必然两个根」变成「正常情况下一个根」，但仍有三个口子：第三方另发一个 pinyin 后端包；
声库逐项覆写后端模块指向别处；以及 `set` 与构造之间的**发布次序**。

故 provider 域另持一个进程级仲裁器（形制对位 `ResourceCache`，归属见资源缓存文档 §3）：

- 一把互斥锁**罩住 `setDictionaryPath` 的发布点**（每次 claim 代际只发布一次），引擎构造在锁外
  进行——构造必须先持有一枚 live claim，而根只在最后一枚 claim 归还后才被清除与重新发布，故
  「发布」之后不可能再插入第二次发布；同一声库的双语言因而能并行预热（机制说明见
  `PinyinEngines.h` 中 `createEngine` 的注释）；
- 记录首个成功登记的规范化根。同根放行；**异根即加载失败**，诊断同时点名两个根；
- 登记发生在 **Acquire**（读 `configuration` 时），因为「两个根冲突」是关于包的事实，与绑定
  无关，理应在承载它们的那次加载里失败；
- 构造后校验 `initialized()`，为假即加载失败（旧栈只在 `start` 期报运行时错误）；
- 保留旧栈的词典目录预检（存在 / 是目录 / 非空）——其注释记着一次真实事故：路径不存在时
  `Pinyin::Pinyin()` 会无限挂起，把模块永久卡在 Loading。

**效果是把静默失灵换成确定性的加载期失败**，符合三级失败模型。上游若把词典根改成构造参数，
仲裁器退化为空操作；在那之前不能只靠收录纪律。

### 6.5 引擎行为与契约的对齐

| 项 | cpp-pinyin | 契约面 |
| :-- | :-- | :-- |
| 声调风格 | 两个引擎旧栈均取 `NORMAL`（无调），无按语言的差异 | 无配置键——信息可从绑定推导（A17 尺子一） |
| 未收录字 | `Error::Default` 原样保留该字，同时置 `PinyinRes.error` | **产出空发音 + `PhonemeGenerationFailed`**，交由链上 `fallback` 决定 |
| 输入粒度 | `hanziToPinyin(vector<string>)` 只取每元素**首个码点**（`zhPosition` 读 `input[i][0]`），多字词静默截断 | 词拆为码点后整批送入，按词回拼（空格分隔）；**未搬入截断缺陷** |
| 候选 | 逐字候选 | 单字词取引擎候选；多字词候选为发音自身（跨字候选是笛卡尔积，本契约无从表达） |
| 部分失败 | 逐字独立 | **一词整体成败**：任一码点未产出即整词失败，不为失败码点编造读音 |

> **相邻性是语义的一部分**：引擎跨相邻元素查 `phrases_dict`，「银行」与「银 · 行」读音不同。
> 因此 `pipe-chain` 的 `model` 步按**原词序中的极大连续可转换段**分批送入，而不是把过滤后的
> 词拍平成一张表（§4.2 `model`）。

### 6.6 并发：每个后端执行体一个引擎实例

`ChineseG2p` 的全部转换方法是 `const`，却经 `d_ptr` 改写实例暂存（`ChineseG2p_p.h:55-56`、
`:67-73`，被 `ChineseG2p.cpp:181` 等六处调用）。**单个实例不能承接并发转换**——旧栈
`PinyinG2pTaskImplBase::start` 只取 `shared_lock`，实为放行并发写。记作 **B1-b**。

因此每个后端执行体各持一个实例：无锁真并行，内存 ×k。**本变体的解析产物不进
`ResourceCache`**，理由即 B1-b——缓存的契约是「只读解析产物」（资源缓存文档 §1），塞进一个
可变对象会破坏该不变式。上游若把暂存改为局部变量，词表方可进缓存、暂存留执行体。

### 6.7 移植注记

| 项 | 旧栈 | 目标 |
| :-- | :-- | :-- |
| 分发形态 | 两款硬编码插件，各随一份词典根随语言包发布 | 单变体、单后端包 `wolf/g2p-pinyin`，词典一份（§6.1） |
| 变体切分 | 两个 plugin key | 引擎按 `languageMap` 由绑定二元组选（§6.2） |
| 配置键 | `dictPath`（本语言词典目录）、无 `formatVersion` | `dictRoot` + `formatVersion` + `languageMap` |
| `exports.languages` | 旧包格式无对应物 | 必选，且与 `languageMap` 对账 |
| 打标 | 引擎内 `configuration.verify` | 移入链的 `verify` step（条目形状不变） |
| 词典根冲突 | 无察觉，后者覆盖前者，先载者静默失灵 | Acquire 期仲裁，异根即加载失败并点名两根（§6.4） |
| 引擎初始化失败 | 仅 `start` 期报运行时错误 | 构造后即校验，加载失败 |
| 兜底 | 引擎内保留原字并置 error（自相矛盾） | 空发音 + error，链上 `fallback` 产出（§6.5） |
| 多字词 | 只看首个码点，静默截断 | 拆码点整批转换后回拼 |
| 拒绝未知键 | 未实现 | 按 §2.3 实现 |

## 7. `lua`（脚本类 S2P / Onset）

S2P 与 Onset 各有一个 `lua` 变体，由同一个插件承载——两者共用同一套沙箱，实现放一处比放两处
可靠。

### 7.1 `configuration` 与入口

| 契约 | 入口全局函数 | 签名 |
| :-- | :-- | :-- |
| S2P | `s2p` | `(发音: string) → list<string>` |
| Onset | `markonset` | `(音素: list<string>) → list<boolean>`，**与输入等长** |

`configuration` 只有 `file` 一个键（必选，相对模块声明目录），按 §2.3 拒绝未知键。

**加载期即校验**：脚本在 Acquire 期编译并运行一次，检查入口全局函数存在。编译失败、或脚本
未定义该函数，**即包加载失败**——不是等到第一次转换时才发现。

Onset 返回表长度与输入不符即报错：契约是一个音素配一个标记，短表会让词尾静默失标，长表则带着
无处安放的标记。

### 7.2 沙箱

**语言包是数据，数据不该能打开文件。** 沙箱在 `luaL_openlibs` 之后移除全部对外通道：

| 移除项 | 原因 |
| :-- | :-- |
| `io`、`os` | 文件系统与操作系统 |
| `debug` | 可绕过其余一切限制 |
| `package`、`require`、`module`、`dofile`、`loadfile` | 加载更多代码 |
| `load`、`loadstring` | 同上（**旧栈遗留未移除**，本实现补上） |
| `collectgarbage` | 让脚本干预宿主的内存行为 |
| `jit` | `jit.on()` 会重新开启 JIT，而 §7.4 的中断在 JIT 打开时失效——**脚本不得拆掉自己的停止开关** |

有专门用例断言这些名字在脚本里全部为 `nil`。

### 7.3 `utf8` 库

解释器是 LuaJIT，语言级别为 Lua 5.1，**不带 `utf8` 库**。非 ASCII 记法的脚本因此连遍历自己的
输入都做不到，故沙箱补上一个。

**补的是 Lua 5.3 `utf8` 的完整形状**——`char` / `codepoint` / `codes` / `len` / `offset` /
`charpattern`，不是形似的子集。取了标准名字就该给标准语义，否则脚本作者照手册写 `utf8.offset`
会撞上一个不存在的函数。

### 7.4 取消：计数钩子，且必须关掉 JIT

运行时文档要求 `stop()` **在词边界响应**。但脚本可以自己死循环，词边界因此根本不是边界，故除
逐词检查外还向解释器装 count hook，在指令批次之间放弃当前调用。

> **实测结论：LuaJIT 的 count hook 在 JIT 编译出的 trace 内不触发。** 一个跑得够久、值得被编译
> 的循环，恰好就是再也停不下来的那个——裸 `while true do end` 装了钩子仍永久挂起。因此沙箱
> **关闭 JIT 引擎**（`luaJIT_setmode(..., LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`）。
>
> 两个易错处，都是实测撞出来的：
>
> 1. **必须在 `luaL_openlibs` 之后关**——开库会连带打开 `jit` 库并把编译器重新打开；
> 2. **必须同时移除 `jit` 全局**（§7.2），否则脚本一句 `jit.on()` 就把中断废掉。
>
> 代价可忽略：这些脚本每词只做几次字符串操作，解释执行的开销远不及「能停下一个失控的包」。
>
> **旧栈装了同一个钩子却从未关过 JIT**（全树零处 `luaJIT_setmode`），故其取消机制对它唯一
> 存在的目标场景无效。

被中断的调用报为**取消而非失败**：`state()` 取 `Canceled`，已完成的词照常返回。`stop()` 在
无执行时是空操作，不毒化下一批。

### 7.5 并发与缓存

`lua_State` 不可共享，故**每个执行体各编译一份**，源文本由模块在 Acquire 期读入一次后共用。

**脚本体不进 `ResourceCache`**：被共享的会是执行上下文而非只读解析产物，与资源缓存文档 §1 的
不变式冲突（同 A32 的尺子）。

### 7.6 现状

四仓中**没有任何生产 Lua 脚本资源**。本变体属补齐变体表，不解锁任何存量。

## 8. 移植来源与口径

> **本节及各节「移植注记」中的代码锚点一律取 synthrt `origin/refactor`（HEAD `814bf81`），
> 不在 `main` 树内。** 其余文档的 synthrt 锚点取 `main`，两者不可混引。

wolf 已随附全部七个推理解释器插件：`s2p`、`onset`、`chain`、`pinyin`，以及按依赖条件构建的
`multig2p`（需 dsinfer）与 `lua`（需 LuaJIT，承载两个脚本变体）。实现从 **synthrt
`origin/refactor`**（HEAD `814bf81`）移植：

| 变体 | 移植来源 |
| :-- | :-- |
| `pipe-chain` | `plugins/G2P/chain/**`（步实现在 `internal/Steps/`：TagAndValidate / Dict / Model / Format / Fallback） |
| `algo-pinyin` | `plugins/G2P/mandarin/**`、`plugins/G2P/cantonese/**`、`lib/G2P/Support/Common/PinyinG2pTaskImplBase.*`（两款硬编码引擎，收敛为单变体的共享后端包） |
| `multig2p-onnx` | `plugins/G2P/multig2p/**` |
| S2P `dict`/`direct`/`mapping`/`lua` | `lib/S2P/**`（现为 istream 库类，无模块外壳） |
| Onset `rule`/`lua` | `lib/S2P/RuleOnsetMarker.*`、`LuaOnsetMarker.*` |

**口径：旧栈是「待移植的参考实现」，不是「待规范化的现状」。** 本文写目标键汇；旧实现与目标
的差异只在必要处以移植注记标出，不作逐条现状盘点——那些属事实记录，归
[linguist-decisions.md](linguist-decisions.md) 的 D 系列保留区（D3 / D4 / D5 / D6 / D7 /
D12 / D13）。

移植时须按 synthrt main 的新框架重构，而非照搬：

- 分派键从旧的 TaskPlugin `key()` 字符串换成 (`interface`, `level`, `variant`) 三元组；
- 任务面从旧的 G2pTask 换成 `srt::InferenceExecutive` + `srt::ITask`（单任务面，见 A14）；
- 资源装配点移到 Acquire，并接入 `wolf::ResourceCache`（[linguist-resource-cache.md](linguist-resource-cache.md)）；
- 内部语言引用（旧栈的 `langRef`、下划线词法 `eng_plus`）不出模块边界，换成契约的
  `(language, scheme)` 二元组（链推理契约 §2.4）。
