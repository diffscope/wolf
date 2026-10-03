# wolf 语言域变体参考（正交面）

本文是**变体面**，记录各 `variant` 的 `configuration` 键集与资源格式。变体面与分层主栈正交：
契约层（[linguist-domain-contract.md](linguist-domain-contract.md)、
[linguist-inference-contract.md](linguist-inference-contract.md)）只规定
「`configuration` 由 `variant` 全权规定」，具体内容由本文规定。

本文属 **wolf 实现文档**，不是对外契约。上位规范为 [spec 2.4](ds-spec-2.4.md)；
分层见 [linguist-architecture.md](linguist-architecture.md)；决策见
[linguist-decisions.md](linguist-decisions.md)。

> **口径**：本文描述**目标形态**。各变体的键集已逐条与 synthrt `refactor` 分支的实现核对
> （移植来源与移植差异表见 §8），实现与目标的差异集中在 §8.1。原变体草案中涉及
> G2PModel / DictQuery 契约、`options.languageId`、按贡献 ID 匹配语言的部分**一律作废**
> （A2 / A3 / A16），该草案已删除。

## 1. 变体治理

### 1.1 命名

- 变体名属实现方命名空间，编码为单个 `segment`（`[A-Za-z0-9_-]+`，
  `synthrt/lib/Core/ContribLocator.cpp:43-53`）；
- wolf 收录变体的命名惯例：
  - G2P 宿主面保留 `pipe-` / `algo-` 类前缀（分别表示编排类与规则算法类）；
  - S2P、Onset 沿用既有库类名的切分；
- **全部裸变体名由 wolf 官方保留**；第三方按 spec 2.4:562 使用反向域名变体
  （如 `com.vendor.myengine`）。

### 1.2 变体名作为分派键

- 框架按 (`interface`, `level`, `variant`) 三元组**全匹配**选择 provider，`configuration`
  的内容**不参与**选择（spec 2.4:576、:590）；
- 选择按「目录顺序 + 目录内文件名顺序」形成的全序扫描，**首个全匹配者当选**，后续相同三元组
  不参与（实现见 `synthrt/lib/Core/ContribPluginFactory.cpp:74-83`）；
- 三元组未匹配任何 provider 时，加载以「找不到提供者」失败（`PackageLoader.cpp:769-774`，Probe
  阶段报 `FeatureNotSupported`）。**不存在运行期再分派**；
- 「同一变体名只允许一个 provider 插件承载」是 wolf 对收录变体的**收录纪律**（使公共包的
  provider 选择结果确定），并非框架约束；框架只规定首个全匹配者当选。

### 1.3 演进判据

按 spec 2.4:744-753：

| 情形 | 处置 |
| :-- | :-- |
| 只与解释器有关 | 写入 `configuration`；不改 `level`，必要时提升资源 `formatVersion` |
| 导入方需要，现有词汇表足以表达 | 用现有词汇表表达，不改 `level` |
| 导入方需要，现有词汇表缺少对应词项 | 递增 `level` |
| 输入输出已根本不同 | 另立 `interface` |

**模型后端与词典查询在 Level 1 不单独设立契约**（A2）：二者是 `pipe-chain` 变体的内部事务，
经该变体自身的 `imports` 与 `configuration` 表达。

## 2. `configuration` 公约

`configuration` 由 `variant` 全权规定（spec 2.4:604、:620），契约不设统一的外层形态。本族约定
以下三条。

### 2.1 路径解析

- `configuration` 中的路径以**模块声明文件所在目录**为基准（`${vars}` 已由 Loader 展开）；
- 解释器**不得**对已展开的字符串再次套用展开规则（spec 2.4:88）；
- 经路径键定位的家族配置文件，其**内部**路径以该文件自身所在目录为基准。

> 书写时必须区分这两级基准。原变体草案的 pipe-chain 示例在此处自相矛盾：声明位于
> `inferences/g2p/inference.json`，却引用 `./g2p/chain.json`，该路径实际解析为
> `inferences/g2p/g2p/chain.json`。

### 2.2 格式版本自验

- 配置（内联键集或家族配置文件）顶层可携带 `formatVersion`（单调递增的正整数）；
- 解释器声明自身支持的上限，并支持 `[1, 上限]` 全区间；读入的版本超过上限时**加载失败，不得
  回退**，诊断须包含「声明版本 > 支持上限」与升级指引；**对采纳本条的变体**，缺键、非正整数、
  文件不可解析同样按加载失败处理（未采纳本条的变体见 §3.3 对 S2P 与 Onset 的豁免）；
- `level` 管理契约能力，`formatVersion` 管理变体内部格式的演进，两层互不替代；
- 打包工具、编辑器等静态工具**无需加载插件即可读取** `formatVersion` 进行预检；
- **发布侧联动**：资源 `formatVersion` 的提升视同所在包的破坏性更新，包级 `compatVersion` 必须
  同步提升，发布规则与理由见发布文档 §4.2 第 3 条。

### 2.3 未知键的拒绝

各变体可约定「遇未知键即加载失败」，以防止拼写错误静默生效。该约定只作用于**变体定义的
object**，属于 spec 2.4:74 末句「贡献类别、interface 与 variant 定义的 object 按各自 schema
处理」所规定的豁免，与框架定义 object 的「未知字段不得拒绝」不冲突。

## 3. 收录变体清单

### 3.1 G2P（`org.openvpi.wolf.inference.G2P`）

| variant | 大类 | 外部依赖 | 要点 |
| :-- | :-- | :-- | :-- |
| `pipe-chain` | 编排 | 经 `imports` 引用的模型后端模块 | 打标 → 词典 / 规整（可重复、交错） → 模型 → 兜底；步序由内联的 `steps` 规定（§4.1）。**唯一经 `imports` 暴露依赖的变体** |
| `algo-pinyin` | 规则算法后端 | 无 | cpp-pinyin 引擎（普通话 / 粤语：查表 + 规则转换）；发布为独立包 `wolf/g2p-pinyin`，由 cmn / yue 两条 `pipe-chain` 共用（§6） |
| `multig2p-onnx` | 模型后端 | dsinfer 的 ONNX 驱动（由宿主注册为 Runtime Service） | seq2seq 模型；发布为独立包 `wolf/g2p-multi`，由 9 个语言的 `pipe-chain` 共用（§5） |

两个后端变体（`multig2p-onnx`、`algo-pinyin`）都必须声明 `exports.languages`，并与各自的
`languageMap` 逐项对账（§5.2、§6.2）。编排类的 `pipe-chain` 按链推理契约 §2.1 声明。

### 3.2 S2P（`org.openvpi.wolf.inference.S2P`）

| variant | `configuration` | 说明 |
| :-- | :-- | :-- |
| `dict` | `file`（必选，path）：TSV，每行 `发音\t音素1 音素2 …` | 按整个发音查表；未命中时产出空音素序列。重复的发音、音素列为空均为加载失败 |
| `direct` | 无必选键（可出现 `file`，但不读取） | 按 ASCII 空格把发音拆分为音素列表（空段丢弃；制表符不作分隔符） |
| `mapping` | `file`（必选，path）：TSV，每行 `原音素\t目标音素` | 逐音素替换；表中未列出的音素原样透传。重复的原音素为加载失败 |
| `lua` | `file`（必选，path）：Lua 脚本（LuaJIT），须定义全局函数 `s2p(发音) → list<string>` | 脚本转换，沙箱见 §7 |

`dict` 与 `mapping` 的 TSV 每行必须恰好包含一个制表符且两列均非空；空行忽略，首行的 UTF-8 BOM
与行尾 CR 被剥离。三个变体均拒绝 `file` 以外的键。

`exports.phonemes`（产出集）的推导口径：`dict` 取 TSV 音素列的并集；`mapping` 取
「目标列 ∪ 透传域」的上界集；`direct` 开放无界，`lua` 不可静态推导，这两类由作者显式补齐
或省略。

`exports.languages`（可消费的二元组）由作者按变体的实际资源声明。`direct` 与 `lua` 对任何体系
都适用，因此**通用模块应省略该键**，不应编造声明。

> **语言包内的模块除外**：随 `wolf/lang-eng` 发布的 `direct` 模块专为该语言服务，声明
> `[{eng, arpabet}]` 符合事实而非编造，并使二元组匹配在**加载期**生效；省略该键会导致宿主每装
> 一个语言包告警一次（域契约 §5.3）。`wolf/lang-zxx` 与四个已闭包的语言均按此声明。

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

- 顶层只允许 `formatVersion`、`phonemeTypes`、`rules` 三个键，每条规则只允许 `pattern` 与
  `onsets` 两个键；
- `phonemeTypes`：非空 object，音素 → 自定义类型名（类型名为非空字符串，且不得为保留词 `*`）；
- `rules`：`pattern` 为音素或类型（含通配 `"*"`）组成的非空序列，`onsets` 给出匹配序列中处于
  onset 位置的下标，下标必须落在 `pattern` 范围内；
- 匹配自左向右扫描：在每个位置选出**最长**的可匹配规则，长度相同时依次比较字面段数、类型段数
  与首个字面段的位置（字面段越多、类型段越多、首个字面段越靠前者优先），命中后跳过该规则覆盖的
  区间；同一字符串既是字面音素又是登记类型名时按字面音素段处理（字面较类型特异）；
- 未登记于 `phonemeTypes` 的音素不匹配任何类型段，只能匹配字面音素段与通配 `"*"`；
- 输入中未被任何规则覆盖的位置输出 `false`（整条无匹配时输出全 `false`）。这是**合法的目标
  语义**，不是错误。

`exports.knownPhonemes`（识别集）的推导口径：`rule` 变体取 `phonemeTypes` 中**类型名被至少一条
规则引用**的键集，并入 pattern 中的字面音素段。`"*"` 通配段在功能上覆盖任意输入，因此派生集是
覆盖面的**下界**；识别集偏小不构成缺陷。`lua` 不可静态推导。

> **B2 已解决**：旧栈 `rule` 变体的字面音素段永不匹配。插入时 pattern 各段一律被标为通配
> （`lib/S2P/RuleOnsetMarker.cpp:296-299`），精确匹配分支与特异性计数因此恒为死代码，尽管结构
> 中已备有 `exactChildren` 字段。**wolf 的实现按本节的目标语义重写，未继承该缺陷**，并有专门
> 用例覆盖「字面段优先于其类型段」。
>
> 「修复即改变现网行为」的顾虑在**仓内**不成立：四个仓库中没有任何带实质内容的生产 rule JSON。
> 但**已分发的声库包不在此列**：公开分发的 7 款声库包各带 4 份规则文件（共 **28 份**，其中
> `assets/eng.json` 的文件名不含 `onset`，按文件名搜索会遗漏四分之一），顶层键一律只有
> `phonemeTypes` 与 `rules`，**没有一份**带 `formatVersion`。规则文件在 Acquire 期解析
> （`onset/main.cpp` 的 `createConfiguration`），解析失败即**包级加载失败**，因此「新增必填顶层键」
> 的改动会拒载现网声库包。本段不能作为「此处改动零风险」的依据。

S2P 与 Onset 的 `configuration` **不设 `formatVersion`**：键集极简，以 §2.3 的「拒绝未知键」
约束演进。二者的**家族配置文件**（`configuration.file` 指向的 rule JSON）同样**不设必填的**
`formatVersion`：形状演进由缓存代际常量承担（`onset/main.cpp` 中的 `RULE_KIND`，值为
`onset-rule-json@1`），读入端以「拒绝未知键」约束拼写演进。该文件在 Acquire 期解析，将该键设为
必填会拒载已分发的声库包（见上文 B2 之后的清点）。文件自行声明 `formatVersion` 时按 §2.2 校验：
非正整数与超过上限按 §2.2 处理，缺省视作 1。

## 4. `pipe-chain`（编排类 G2P）

线性管道：把一个词依次交给若干 step，最先产出发音的步胜出。`pipe-chain` 当前是编排类的唯一成员；
DAG 编排、条件路由等未来形态同属此类，并共用 `pipe-` 前缀。

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
| `steps` | array&lt;object&gt; | 是 | 有序步骤表，非空，**上限 50 步** |

**steps 内联，不引用外部 chain 文件。** `configuration` 本身就是由 variant 全权规定的块，
`formatVersion` 可直接写在其顶层（§2.2）；再套一层文件只会增加一级路径基准，不增加任何表达力。
原草案的 chain.json 方案正是在这一层写错了路径基准（见 §2.1 的说明）。声库需要覆写链路时，覆写
整个 `inference.json`（语言域契约《逐项覆写》）。

每个 step 条目：

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `step` | string | 是 | 步类型，值域见 §4.2；未知类型即加载失败 |
| `enabled` | bool | 否 | 缺省 `true`；为 `false` 时该步不进入管道，但步类型仍受校验 |
| `params` | object | 否 | 该步的参数；省略等价于空对象（各步取自身缺省值） |

同一类型可重复出现（现网夹具即「词典 → 规整 → 词典」的重复步）。步序受两条约束，违反即加载
失败：

- `verify` 步必须位于所有产出发音的步（`dict`、`model`、`fallback`）之前。打标决定产出步的作用
  范围，位于其后的 `verify` 会把已有发音的词改判为 `copy`，从而丢弃已产出的结果；
- 两个 `model` 步不得点名同一 `role`。

各步与 step 条目均拒绝未知键。

### 4.2 step 词汇

| step | params | 作用 |
| :-: | :-- | :-- |
| `verify` | `entries: [{type, value, mode}]` | 逐词打标，决定 `mode` |
| `dict` | `file`（path，必选） | 词典查表 |
| `format` | `operations` / `stripTrailingSpace` / `addSpaceBetweenPhones` | 文本规整 |
| `model` | `role`（必选）、`batchSize` | 调用由 `role` 点名的后端模块 |
| `fallback` | `useOriginal`、`defaultPronunciation` | 兜底策略 |

#### `verify`

`params.entries` 是条目数组（键名不与步名重复）。每个条目：

| key | type | 说明 |
| :-: | :-: | :-- |
| `type` | enum | `regex` \| `array` \| `dict`，**三种类型均已实现；未知类型即加载失败** |
| `value` | array&lt;string&gt; | 非空，元素为非空字符串。`regex`：RE2 正则（多条各自包入 `(?:…)` 后合并为 `\|` 择一，整串 full match；每条的内联标志只作用于本条）；`array`：字面词表；`dict`：TSV 路径（相对模块声明目录），取每行首列为词 |
| `mode` | enum | 命中时该词的 `mode`：`convert` \| `copy` |

未被任何条目命中的词取 `copy`；多个条目命中同一词时，取声明序中最后一个命中条目的 `mode`。
经输入合法性判定定案的词（见会话文档 §8.3）不参与打标。

> **条目形状与 `algo-pinyin` 统一**：两个变体执行同一项工作（逐词分类为 convert / copy），迁移后
> 共用同一个 `Verifier` 组件，因此**共用同一套条目形状** `{type, value, mode}`。旧栈 chain 侧的
> `action` 与 pinyin 侧的 `mode` 同义异名，统一为后者（即该组件的既有形状，改动量为零）；步类型名
> 由 `tagAndValidate` 改为 `verify`。容器名按各自语境选取：chain 为步参数 `entries`，algo-pinyin
> 为 `configuration.verify`（algo-pinyin 现已不设该键，见 §6.3）。

#### `dict`

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `file` | path | 是 | TSV 词典，每行 `词\t发音`；重复词按声明序合并为候选组 |

命中时 `pronunciation` 取首个候选，`candidates` 取整个候选组，`hitSource` = `dict`。

词典格式**严格**：每行必须恰好包含一个制表符，两列均不得为空；空行忽略；不含任何条目的词典为
加载失败。另有以下读入约定：

- 首行的 UTF-8 BOM 与每行行尾的 CR 被剥离；
- 词尾由 `(` + 纯数字 + `)` 构成的 CMU 式变体后缀被剥离，`word(2)` 与 `word` 归并为同一候选组
  （按文件序）。不剥离时这些键永远不会被查到。

> **发布侧的对应义务**：不合规范的存量词典由转换管线归一化，**不依靠读入端放宽**。现网清点出
> 两处：`fil_dict.txt` 以空格分列（旧栈只接受制表符且静默跳过不合规范的行，因此其 24752 条
> **从未被读取**）；`kor_dict.txt` 与 `ds_cmudict-07b.txt` 开头有 `;;;` 注释行。两者均在
> `scripts/convert-g2p-packages.py` 中改写为规范 TSV。

#### `format`

| key | type | 缺省 | 说明 |
| :-: | :-: | :-: | :-- |
| `operations` | array&lt;string&gt; | `[]` | 清洗操作序列；Level 1 的值域只有 `lowercase`，未知操作即加载失败 |
| `stripTrailingSpace` | bool | `false` | 去除产出发音的尾随空白 |
| `addSpaceBetweenPhones` | bool | `false` | 在音素之间补空格 |

`lowercase` 逐码点做小写映射，覆盖 ASCII、Latin-1 补充、拉丁文扩展 A 与基本西里尔字母，其余
码点保持不变。

`operations` **不嵌套在 `cleaner` 对象中**：旧栈的 `cleaner` 只有 `operations` 一个成员，该层
嵌套不携带任何信息（与 §2.3 同一标准）。

`lowercase` 只作用于**可产出的词**（`mode=convert`、未经契约判定且尚无发音）的歌词文本，结果
写入清洗后词；其后的产出步一律使用清洗后词。`stripTrailingSpace` 与 `addSpaceBetweenPhones`
作用于所有已有发音的词。改写 `pronunciation` 时**同步改写 `candidates` 的各元素**，使契约的
共现约束在链内始终成立。

#### `model`

| key | type | 必选 | 缺省 | 说明 |
| :-: | :-: | :-: | :-: | :-- |
| `role` | string | 是 | — | 点名本模块 `imports` 中的一个条目，其目标契约须为 `org.openvpi.wolf.inference.G2P` |
| `batchSize` | int | 否 | 不切分 | 正整数，分批调用后端时的批大小 |

- **按极大连续段分批**：本步只作用于可产出的词，但送入后端时按**原词序中的极大连续段**切分，
  不把过滤后的词拼接为一张表。后端可跨相邻词取上下文（`algo-pinyin` 的短语表即是如此，§6.5），
  被其他词隔开的两个词不相邻；
- `batchSize` 缺省时**不再切分**（每段调用一次）。显式给出时在段内继续切分，因此对上下文敏感的
  后端不应设置该键；逐词模型（`multig2p-onnx`）不受影响；
- 步内**不写 ref 引用串**，只写 role；被点名的模块经框架的 import binding 解析；
- 点名的 `role` 不存在、未绑定，或所指条目的目标契约不是 G2P 时，本模块加载失败。未被任何 model
  步点名的 G2P 导入不注入执行体，也不视为错误；
- 绑定只依据 `role`，不依赖声明顺序；
- **不写语言参数**。后端执行体由本模块创建，其 `(language, scheme)` 由本模块自身的绑定注入
  （A16 / 链推理契约 §6.3）。旧栈的 `langRef`（内部子资源标识，下划线词法如 `eng_plus`）
  不越过模块边界。

后端逐词返回的 `error` 以同名透传为本链输出的 `error`，本链不增加新的枚举值。驱动不可用时由后端
逐词报告 `DriverUnavailable`，同样按原名透传。后端执行体无法创建、后端调用整体失败或返回的词数与
请求不符时，该段全部词标记为 `ModelInferenceFailed`。携带 `error` 的词仍由后续 `fallback` 步
处理。

#### `fallback`

| key | type | 缺省 | 说明 |
| :-: | :-: | :-: | :-- |
| `useOriginal` | bool | `true` | 以原词作为兜底发音 |
| `defaultPronunciation` | string | `""` | `useOriginal` 为假时的固定兜底发音 |

只处理**未经契约判定、`mode=convert` 且发音仍为空**的词，包括此前步骤已标记 `error` 的词。

- **兜底产出发音时 `error` 必须为空**，`hitSource` = `fallback`，`candidates` 为该兜底值；
- 只有兜底也未产出（`useOriginal=false` 且 `defaultPronunciation` 为空）时，才以
  `error=PhonemeGenerationFailed`、空发音与空候选产出，**不得回填原词**；
- 链未配置 `fallback` 步时，未产出发音且未携带其他 `error` 的词同样以
  `error=PhonemeGenerationFailed` 产出。

> 旧栈在此处有缺陷：`FallbackStep::handle` 无条件置
> `errorType = PhonemeGenerationFailed`，即使 `useOriginal` 已经产出了发音。这与契约规定的
> 「`error` 非空即失败、`mode`/`candidates` 无定义」直接冲突，移植时按本节修正。

### 4.3 单一来源守卫

`dict` / `model` / `fallback` 三个产出发音的步一律只作用于 **`mode=convert`、未经契约判定且
尚无发音**的词。由此得到以下性质：

> 逐词候选只取自**单一最终来源**：词典命中时为词典候选组（保留声明合并序），后端产出时为后端
> 候选序列，兜底产出时为兜底单值。

Level 1 **不做跨源合并、不去重、不设数量上限**。该性质由上述守卫保证，**与步序无关**，因此
同一条链可包含多个同类型的步。跨源合成与截断若将来需要，属于 `formatVersion` 演进，`level`
不变。

### 4.4 移植注记

原有差异表已并入 §8.1 的 `pipe-chain` 各行。

## 5. `multig2p-onnx`（模型后端 G2P）

seq2seq ONNX 模型后端：词 → 发音与候选。发布为独立包 `wolf/g2p-multi`，由 9 个语言包的
`pipe-chain` G2P 经 `imports` 共用。

**该变体声明的是 `G2P` 契约，而非某种「后端专用契约」**（A20）：共享一个大模型要求它成为独立包
中的模块，模块必须有契约，而声明了 G2P 就必须满足 G2P 的全部义务（`mode` / `copy` / `skip` /
`error`）。因此该变体既可由 `pipe-chain` 的 `model` 步调用，也可由某个语言直接用作
`linguist/g2p`；「纯模型、无编排」是合法的语言链配置。

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

**`bundle.json` 与 `vocabulary.json` 必须与模块声明文件位于同一目录，不设指向它们的配置键。**
一个 bundle 对应一份声明，路径可由声明位置确定；增设 `bundle` 键只会引入表达同一事实的第二种
方式（与 §2.3 同一标准）。

### 5.2 `configuration`

| key | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `languageMap` | array&lt;object&gt; | 是 | 契约二元组 → bundle 内部语言引用的映射，见下文 |
| `maxLen` | int | 否 | 解码长度上限，正整数，缺省 48 |
| `beamSize` | int | 否 | beam 宽度。**本实现只支持 1**，见 §5.5 |
| `topK` | int | 否 | 候选数上限，经 `candidates` 上传。**本实现只支持 1**，见 §5.5 |
| `lengthPenalty` | number | 否 | 长度惩罚。**本实现只支持 0**，见 §5.5 |

```json
"configuration": {
    "languageMap": [
        { "language": "eng", "scheme": "arpabet", "ref": "eng/default" },
        { "language": "deu", "scheme": "ipa",     "ref": "deu/default" }
    ],
    "maxLen": 48
}
```

**`languageMap` 是暴露面到内部子资源的唯一通道**（链推理契约 §2.4）。bundle 内部的语言引用
形如 `eng/default` / `eng/plus`（含斜杠的家族内标识），**不越过模块边界**；契约面只有
`(language, scheme)`。`languageMap` 必须非空，条目只允许 `language`、`scheme`、`ref` 三个键，
二元组不得重复，`ref` 必须命中 `bundle.json` 的 `languages` 中的一项。

`exports.languages` 必须与 `languageMap` 的二元组集合**逐项一致**，由 provider 在 Acquire
期校验。前者属契约面，后者属实现面，二者不能相互推导，因此必须各自声明并对账。契约允许省略
`exports.languages`（链推理契约 §2.1），但**本变体不允许**：省略后无从对账。

**无 `default_language`**：执行体在创建时已绑定单一二元组（A16），不存在「缺省语言」。绑定值
不在 `languageMap` 中时，执行体创建以 `FeatureNotSupported` 失败。

### 5.3 `bundle.json`

| key | 说明 |
| :-- | :-- |
| `bundle_version` | **本变体的资源格式版本**，正整数，按 §2.2 公约处理（解释器声明支持上限，超限拒绝加载） |
| `files` | 逻辑名 → ONNX 文件名的映射，路径以 `bundle.json` 自身所在目录为基准 |
| `languages` | bundle 收录的内部语言引用清单，非空；`languageMap` 的 `ref` 必须命中其中一项 |

其余顶层键（`schema_version`、`model_version`、`min_runtime_version`、`vocab_hash`、
`opset_version`、`export_flags`、`generated_at`）是**发布侧的内部元数据，契约不作解释**。

`vocabulary.json` 的 `symbols` 为非空字符串数组，不得重复；保留符号 `<unk>`、`<pad>`、`<bos>`、
`<eos>` 按名称查找，缺少任一即加载失败。

### 5.4 驱动归属与降级

**驱动归宿主所有，不归模块所有。** 宿主创建 `ds::InferenceDriverFactory`，找到 `onnx` 后端，以
`DriverInitArgs`（execution provider 与 ORT 路径）初始化，再经 `SynthUnit::addRuntimeService`
注册；模块按后端名查询 Runtime Service，**不自行加载驱动**。

该归属划分同时决定了两类失败的区别：

| 情形 | 归属 | 处置 |
| :-- | :-- | :-- |
| 进程内没有 ONNX 驱动 | **安装环境**的属性 | 包正常加载，逐词报告 `DriverUnavailable`，由链上的 `fallback` 兜底 |
| 驱动存在，但模型无法打开 | **包**的属性 | 执行体创建失败 |
| 单次推理失败 | 运行期 | 该批全部词报告 `ModelInferenceFailed`，仍由 `fallback` 处理 |

若将第一种情形判为加载失败，未安装驱动的机器上整个语言包都将不可用；契约专门设置的逐词
`DriverUnavailable` 通道正是为此情形而设。

### 5.5 解码范围

**本实现只做贪心解码**（每步取一个 argmax token）。`beamSize`、`topK` 取值大于 1，或
`lengthPenalty` 非 0 时，一律以 `FeatureNotSupported` **加载失败并指明不支持**，不静默降级。
因此每个词只产出一个候选，即发音本身。

理由有两条：

1. **现有唯一的资源不需要 beam search**：`wolf/g2p-multi` 的配置为 `beamSize: 1, topK: 1,
   lengthPenalty: 0.0`；
2. **接受某个键却忽略它，会错误地报告模块能力**。旧栈中 366 行的 beam search 实现未被任何现网
   资源使用，因而没有可供比对的输出；照搬一份无法验证的实现，比明确报告「不支持」风险更大。

真正的多候选（`topK > 1`）只能由 beam search 产出，因此 `topK` 与 `beamSize` 受同一限制。将来
补充 beam search 属于 `formatVersion` 不变的实现增强，`level` 不变。

### 5.6 移植注记

原有差异表已并入 §8.1 的 `multig2p-onnx` 各行。

## 6. `algo-pinyin`（规则算法后端 G2P）

基于 cpp-pinyin 的查表 + 规则转换，无外部推理依赖。**发布为独立包 `wolf/g2p-pinyin`**，由
`wolf/lang-cmn` 与 `wolf/lang-yue` 两条 `pipe-chain` 经 `imports` 共用。

### 6.1 设计理由：独立后端包

cpp-pinyin 的词典根是**进程全局状态**，且**只在引擎构造期读取一次**：

| 事实 | 锚点（cpp-pinyin `3924631`） |
| :-- | :-- |
| `dictionaryPath()` / `setDictionaryPath()` 读写一个文件作用域的全局对象，无同步 | `src/G2pglobal.cpp:13-21` |
| 全局量的唯一读取点是 `ChineseG2pPrivate::init()`，由 `ChineseG2p` 构造函数调用 | `src/ChineseG2p.cpp:80`（构造函数在 `:110-113`） |
| 构造完成后实例自持四张词表，不再访问全局量 | `src/ChineseG2p_p.h:22-25` |
| 两个引擎对应**同一根下的两个子目录**：`Pinyin = ChineseG2p("mandarin")`、`Jyutping = ChineseG2p("cantonese")` | `include/cpp-pinyin/Pinyin.h:13`、`Jyutping.h:13` |

旧栈的两个语言包各带一份词典根，因此必然冲突：后构造者覆盖全局量，先构造者的 `initialized()`
静默变为假，整条链退化。**根因不是全局状态本身，而是一个进程中存在两个词典根。**

逐文件比对表明，这两份词典与 cpp-pinyin 自带的 `res/dict` **字节相同**（11 个文件中 10 个完全
一致，`mandarin/trans_word.txt` 少一行 `吒:咤`，即包内版本比上游旧一版）。该词典是**引擎载荷，
不是语言内容**；将其放入语言包本身就是旧栈的分层错误。

因此收敛为一个后端包：**一个进程一个词典根，两个引擎各取一个子目录**，与 `multig2p-onnx` 同形
（A20 的第二个佐证）。由此 cmn / yue 与使用 `wolf/g2p-multi` 的 9 个语言结构一致：均为覆在共享
后端之上的 `pipe-chain`。

> 原文档中「cpp-pinyin 新版已移除 `setDictionaryPath` 旧 API」一句**与实际源码不符**，已按上表
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
| `dictRoot` | path | 是 | 引擎词典**根**（相对模块声明目录），其下按 `ref` 分子目录；目录必须存在，否则以 `FileNotFound` 加载失败 |
| `languageMap` | array&lt;object&gt; | 是 | 契约二元组 → 引擎内部引用，非空，二元组不得重复 |

- `ref` 的值域是**编译期闭集**（`mandarin` / `cantonese`），未收录的值以 `FeatureNotSupported`
  加载失败。该约束强于 multig2p 的 `bundle.json.languages`，因为引擎类编译在插件内；
- `exports.languages` **必选**，与 `languageMap` 的二元组集合逐项对账（同 A24）；
- 按 §2.3 拒绝未知键。原 `dictPath` 键**更名为 `dictRoot`**：语义从「本语言的词典目录」变为
  「引擎词典根」，同名异义比新名更容易误用，因此更名；旧格式包无 `formatVersion`，按 §2.2 明确
  报错失败。

### 6.3 打标职责的归属

本变体不设 `verify` 键。打标由 `pipe-chain` 的 `verify` step 承担（A22 已使两者的条目形状完全
相同，迁移只需原样搬运），后端因此完全与语言无关。

直接用作 `linguist/g2p` 时，本变体不打标，全部词按 convert 处理。纯汉字输入不受影响，混排输入由
§6.5 的兜底口径收敛。

### 6.4 进程级词典根仲裁（B1）

拆包把「必然存在两个根」变为「正常情况下只有一个根」，但仍有三种情形可能引入第二个根：第三方另行
发布一个 pinyin 后端包；声库逐项覆写后端模块并指向别处；以及 `setDictionaryPath` 与引擎构造之间
的**发布次序**。

因此 provider 域另持一个进程级仲裁器 `PinyinEngineRegistry`（形制与 `ResourceCache` 对应，归属
见资源缓存文档 §3）：

- 登记发生在 **Acquire**（读取 `configuration` 时），因为「两个根冲突」是关于包的事实，与绑定
  无关，应在承载它们的那次加载中失败；
- 登记返回一枚 claim，由模块的配置对象持有。同根登记递增 claim 计数；**异根登记以
  `FeatureNotSupported` 加载失败**，诊断同时指明两个根。最后一枚 claim 随配置对象释放后，仲裁器
  清除已记录的根，因此加载失败或包卸载会归还该根，不会在进程生命期内永久占用；
- 一把互斥锁**只保护 `setDictionaryPath` 的发布点**（每个 claim 代际只发布一次），引擎构造在锁外
  进行。构造必须先持有一枚有效 claim，而根只在最后一枚 claim 归还后才被清除与重新发布，因此发布
  之后不可能插入第二次发布；同一声库的两个语言因而可以并行预热（机制说明见 `PinyinEngines.h` 中
  `createEngine` 的注释）；
- 创建执行体时保留旧栈的词典目录预检（`dictRoot/<ref>` 存在、是目录且非空），否则以
  `FileNotFound` 失败。旧栈的注释记录了一次真实事故：路径不存在时 `Pinyin::Pinyin()` 会无限挂起，
  使模块永久停留在 Loading 状态；
- 构造后校验 `initialized()`，为假即执行体创建失败（旧栈只在 `start` 期报告运行时错误）。

**其效果是把静默失效转换为确定性的加载失败**，符合三级失败模型。上游若把词典根改为构造参数，
仲裁器即退化为空操作；在此之前不能只依靠收录纪律。

### 6.5 引擎行为与契约的对齐

| 项 | cpp-pinyin | 契约面 |
| :-- | :-- | :-- |
| 声调风格 | 旧栈两个引擎均取 `NORMAL`（无调），无按语言的差异 | 无配置键，该信息可从绑定推导（A17 标准一） |
| 未收录字 | `Error::Default` 原样保留该字，同时置 `PinyinRes.error` | **产出空发音 + `PhonemeGenerationFailed`**，由链上的 `fallback` 决定 |
| 输入粒度 | `hanziToPinyin(vector<string>)` 只取每个元素的**首个码点**（`zhPosition` 读 `input[i][0]`），多字词被静默截断 | 词拆分为码点后整段送入，再按词拼回（空格分隔）；**未继承截断缺陷** |
| 候选 | 逐字候选 | 候选以发音为首项。单字词在其后附加引擎的其余候选；多字词的候选只有发音本身（跨字候选是笛卡尔积，本契约无法表达） |
| 部分失败 | 逐字独立 | **按词整体判定成败**：任一码点未产出即整词失败，不为失败码点编造读音 |

> **相邻关系是语义的一部分**：引擎跨相邻元素查询 `phrases_dict`，「银行」与「银 · 行」的读音不同。
> 因此 `pipe-chain` 的 `model` 步按**原词序中的极大连续可转换段**分批送入，不把过滤后的词拼接为
> 一张表（§4.2 `model`）。本变体内部同样按契约判定后的连续可转换段调用引擎；引擎返回的读音数与
> 输入码点数不符时，该批以 `InvalidFormat` 失败。

### 6.6 并发模型

`ChineseG2p` 的全部转换方法声明为 `const`，却经 `d_ptr` 改写实例内的暂存区（`ChineseG2p_p.h:55-56`、
`:67-73`，被 `ChineseG2p.cpp:181` 等六处调用）。**单个实例不能承接并发转换**；旧栈
`PinyinG2pTaskImplBase::start` 只取 `shared_lock`，实际放行了并发写。该问题记作 **B1-b**。

因此每个后端执行体各持一个引擎实例：无锁并行，内存占用为 k 倍。**本变体的解析产物不进入
`ResourceCache`**，理由即 B1-b：缓存的契约是「只读解析产物」（资源缓存文档 §1），放入可变对象
会破坏该不变式。上游若把暂存区改为局部变量，词表即可进入缓存，暂存区留在执行体内。

### 6.7 移植注记

原有差异表已并入 §8.1 的 `algo-pinyin` 各行。

## 7. `lua`（脚本类 S2P / Onset）

S2P 与 Onset 各有一个 `lua` 变体，由同一个插件承载。两者共用同一套沙箱，集中实现比分散在两处
更可靠。

### 7.1 `configuration` 与入口

| 契约 | 入口全局函数 | 签名 |
| :-- | :-- | :-- |
| S2P | `s2p` | `(发音: string) → list<string>` |
| Onset | `markonset` | `(音素: list<string>) → list<boolean>`，**与输入等长** |

`configuration` 只有 `file` 一个键（必选，相对模块声明目录），按 §2.3 拒绝未知键。脚本文件为空
时加载失败。

**加载期即校验**：脚本在 Acquire 期编译并运行一次，检查入口全局函数是否存在。编译失败或脚本
未定义该函数时，**包加载失败**，不推迟到首次转换。

Onset 返回表的长度与输入不符时报错：契约规定每个音素对应一个标记，表过短会使词尾静默缺失标记，
表过长则包含无对应音素的标记。

### 7.2 沙箱

**语言包是数据，数据不应具备打开文件的能力。** 沙箱在 `luaL_openlibs` 之后移除全部对外通道：

| 移除项 | 原因 |
| :-- | :-- |
| `io`、`os` | 文件系统与操作系统 |
| `debug` | 可绕过其余全部限制 |
| `package`、`require`、`module`、`dofile`、`loadfile` | 加载更多代码 |
| `load`、`loadstring` | 同上（**旧栈未移除**，本实现补充） |
| `collectgarbage` | 使脚本能够干预宿主的内存行为 |
| `jit` | `jit.on()` 会重新开启 JIT，而 §7.4 的中断在 JIT 开启时失效；**脚本不得解除自身的中断机制** |

有专门用例断言这些名称在脚本中全部为 `nil`。

### 7.3 `utf8` 库

解释器是 LuaJIT，语言级别为 Lua 5.1，**不带 `utf8` 库**。使用非 ASCII 记法的脚本因此无法遍历
自身的输入，沙箱为此补充了该库。

**补充的是 Lua 5.3 `utf8` 的完整接口**：`char` / `codepoint` / `codes` / `len` / `offset` /
`charpattern`，而非外形相似的子集。采用标准名称就应提供标准语义，否则脚本作者按手册调用
`utf8.offset` 时会遇到不存在的函数。

### 7.4 取消机制：计数钩子与 JIT 关闭

运行时文档要求 `stop()` **在词边界响应**。但脚本可能自身陷入死循环，此时词边界不再出现，因此
除逐词检查外，还向解释器安装 count hook，在指令批次之间中止当前调用。

> **实测结论：LuaJIT 的 count hook 在 JIT 编译出的 trace 内不触发。** 运行时间足够长、值得编译
> 的循环，恰恰是无法停止的循环：裸 `while true do end` 在安装钩子后仍永久挂起。因此沙箱
> **关闭 JIT 引擎**（`luaJIT_setmode(..., LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)`）。
>
> 以下两处容易出错，均由实测发现：
>
> 1. **必须在 `luaL_openlibs` 之后关闭**：打开标准库会同时打开 `jit` 库并重新启用编译器；
> 2. **必须同时移除 `jit` 全局量**（§7.2），否则脚本调用 `jit.on()` 即可使中断失效。
>
> 代价可忽略：这些脚本每个词只做少量字符串操作，解释执行的开销远小于「能够停止失控的包」带来的
> 收益。
>
> **旧栈安装了同样的钩子，却从未关闭 JIT**（全树没有任何 `luaJIT_setmode` 调用），因此其取消
> 机制对唯一的目标场景无效。

被中断的调用报告为**取消而非失败**：`state()` 取 `Canceled`，已完成的词照常返回。`stop()` 在
无执行时是空操作，不影响下一批。

### 7.5 并发与缓存

`lua_State` 不可共享，因此**每个执行体各编译一份**；源文本由模块在 Acquire 期读入一次后共用。

**脚本不进入 `ResourceCache`**：被共享的将是执行上下文而非只读解析产物，与资源缓存文档 §1 的
不变式冲突（与 A32 同一标准）。

### 7.6 现状

四个仓库中**没有任何生产 Lua 脚本资源**。本变体用于补齐变体表，不对应任何存量资源。

## 8. 移植来源与口径

> **本节「移植来源」表与 §8.1 移植差异表中的代码锚点一律取自 synthrt `refactor` 分支上
> `scripts/convert-g2p-packages.py` 中固定的 `SOURCE_REF` 所指版本，不在 `main` 树内。** 其余
> 文档的 synthrt 锚点取自 `main` 分支，两者不可混用。

wolf 随附六个推理解释器插件：`s2p`、`onset`、`chain`、`pinyin`，以及按依赖条件构建的
`multig2p`（需要 dsinfer）与 `lua`（需要 LuaJIT，承载两个脚本变体）。另有仅随测试构建、不安装的
`stub` 插件。实现从 **synthrt `refactor` 分支**（同上 `SOURCE_REF`）移植：

| 变体 | 移植来源 |
| :-- | :-- |
| `pipe-chain` | `plugins/G2P/chain/**`（步实现在 `internal/Steps/`：TagAndValidate / Dict / Model / Format / Fallback） |
| `algo-pinyin` | `plugins/G2P/mandarin/**`、`plugins/G2P/cantonese/**`、`lib/G2P/Support/Common/PinyinG2pTaskImplBase.*`（两个硬编码引擎，收敛为单一变体的共享后端包） |
| `multig2p-onnx` | `plugins/G2P/multig2p/**` |
| S2P `dict`/`direct`/`mapping`/`lua` | `lib/S2P/**`（原为 istream 库类，无模块外壳） |
| Onset `rule`/`lua` | `lib/S2P/RuleOnsetMarker.*`、`LuaOnsetMarker.*` |

**口径：旧栈是「待移植的参考实现」，不是「待规范化的现状」。** 本文描述目标键集；旧实现与目标
的差异集中在 §8.1，不作逐条现状盘点。现状记录归入
[linguist-decisions.md](linguist-decisions.md) 的 D 系列保留区（D3 / D4 / D5 / D6 / D7 /
D12 / D13）。

移植时须按 synthrt main 的新框架重构，而非照搬：

- 分派键从旧的 TaskPlugin `key()` 字符串改为 (`interface`, `level`, `variant`) 三元组；
- 任务面从旧的 G2pTask 改为 `srt::InferenceExecutive` + `srt::ITask`（单任务面，见 A14）；
- 资源装配点移至 Acquire，并接入 `wolf::ResourceCache`（[linguist-resource-cache.md](linguist-resource-cache.md)）；
- 内部语言引用（旧栈的 `langRef`、下划线词法 `eng_plus`）不越过模块边界，改用契约的
  `(language, scheme)` 二元组（链推理契约 §2.4）。

### 8.1 旧栈 → 目标的移植差异

「旧栈」列的相对锚点分别位于 `plugins/G2P/chain/**`（`pipe-chain`）、`plugins/G2P/multig2p/**`
（`multig2p-onnx`）、`plugins/G2P/mandarin/**` 与 `plugins/G2P/cantonese/**`（`algo-pinyin`），
即上表「移植来源」所列目录。

| 变体 | 项 | 旧栈 | 目标 |
| :-- | :-- | :-- | :-- |
| `pipe-chain` | 配置入口 | 直读 `configuration.steps`，无 `formatVersion` | 同左，**增加**必选的 `formatVersion` |
| `pipe-chain` | 打标步 | `tagAndValidate`，键 `tagger` / `action` | `verify`，键 `entries` / `mode` |
| `pipe-chain` | 打标三型 | 只编译 `regex`，`array`/`dict` **静默丢弃**（`TagAndValidateStep::compileEntries`） | 三型全部实现，未知类型加载失败（复用 `Verifier`，该组件已具备三型且对未知类型报错） |
| `pipe-chain` | `enabled` 双轨 | 步项级与 `params.enabled` 两处相互独立 | **只保留步项级**；删除 `params.enabled` |
| `pipe-chain` | `format` | `cleaner.operations` 嵌套；`normalizeTones` 解析后**从未使用**（死参数） | 扁平的 `operations`；删除 `normalizeTones` |
| `pipe-chain` | `model` 绑定 | `id` 点名旧框架任务 + `langRef` 直接传递内部语言引用 | `role` 点名 import + 二元组经 RuntimeOptions 注入 |
| `pipe-chain` | `model` 冗余键 | 夹具携带不被读取的 `file` | 无 |
| `pipe-chain` | `model` 分批 | 按 `batchSize` 切分过滤后的**全表**，相邻关系丢失 | 按原词序的极大连续段切分（§4.2 `model`） |
| `pipe-chain` | `dict` 读入 | 只接受制表符，不合规范的行**静默跳过**（`PhonemeDict.cpp:188-196`） | 同样只接受制表符，但不合规范即**加载失败**；不合规范的存量在转换期归一化 |
| `pipe-chain` | 目标契约校验 | 无 | `createImportValidators` 在 Commit 前校验每个 `role` 已绑定且目标为 G2P |
| `pipe-chain` | `fallback` | 无条件置 `PhonemeGenerationFailed`；夹具携带不被读取的 `markFailed` | 仅在未产出时置该 error；无 `markFailed` |
| `pipe-chain` | 步数上限 | 50（硬编码） | 保留 50 |
| `multig2p-onnx` | 契约归属 | 独立的 `g2p.model.Multig2pInference` 插件键 | `G2P` 契约、变体 `multig2p-onnx`（A20） |
| `multig2p-onnx` | bundle 定位 | 取模块目录自身（`TaskImplBase.cpp:48`） | 同左，并明确为规范（§5.1） |
| `multig2p-onnx` | `languageMap` | **未实现**；`languageId` 字符串直接用于查表，归一化函数 `normalizeLanguageId` 全仓零调用（死代码） | 必选，且与 `exports.languages` 对账 |
| `multig2p-onnx` | 未映射语言 | **静默回退到默认语言**（`TaskImpl.cpp:111-119` 只记录缺失下标，产出循环从不检查） | 绑定期即失败；无「缺省语言」概念 |
| `multig2p-onnx` | 语言引用词法 | 下划线形态（如 `eng_plus`）与斜杠形态 `eng/plus` 混用 | 内部形态由 bundle 决定，不越过边界；契约面只有二元组 |
| `multig2p-onnx` | `bundle_version` | 仅校验非空，无版本比较（`BundleLoader.cpp:133-142`），且为字符串 `"1.0"` | 正整数，按 §2.2 做 `[1, N]` 上限校验；存量的 `"1.0"` 由转换管线归一化为 `1`（与 A34 同一标准） |
| `multig2p-onnx` | 保留符号下标 | 硬编码 `unk=0 / pad=1 / bos=2 / eos=3`（`BundleLoader.h`） | 按名称（`<unk>` / `<pad>` / `<bos>` / `<eos>`）在词表中查找；缺少任一即加载失败 |
| `multig2p-onnx` | 词表查询 | `lookup` 线性扫描全表，逐字符调用 | 哈希表 |
| `multig2p-onnx` | 驱动获取 | 从 G2P Manager 的 driver 类别取单例 | 宿主注册的 Runtime Service（§5.4） |
| `multig2p-onnx` | 解码 | 贪心 + beam search 两条路径 | 仅贪心；beam 相关键的取值受限并指明不支持（§5.5） |
| `multig2p-onnx` | 发音串 | 逐音素追加空格，**保留尾随空格** | 空格分隔、无尾随空格 |
| `multig2p-onnx` | 驱动缺失 | 初始化期仅告警并回退为 copy，运行期逐词 `ModelInferenceFailed` | 按契约报告 `DriverUnavailable` |
| `multig2p-onnx` | `configuration` | 夹具是训练配置的完整转储，运行时只读取其中 `inference.*` 的五个键 | 只保留 §5.2 的五个键，拒绝未知键 |
| `multig2p-onnx` | `topK` 候选 | 真实候选未上传（被丢弃） | 目标为经 `candidates` 上传（链推理契约 §3.4.3）；本实现仅支持单候选（§5.5） |
| `algo-pinyin` | 分发形态 | 两个硬编码插件，各带一份词典根随语言包发布 | 单一变体、单一后端包 `wolf/g2p-pinyin`，词典只有一份（§6.1） |
| `algo-pinyin` | 变体切分 | 两个 plugin key | 按 `languageMap` 由绑定二元组选择引擎（§6.2） |
| `algo-pinyin` | 配置键 | `dictPath`（本语言词典目录），无 `formatVersion` | `dictRoot` + `formatVersion` + `languageMap` |
| `algo-pinyin` | `exports.languages` | 旧包格式无对应项 | 必选，且与 `languageMap` 对账 |
| `algo-pinyin` | 打标 | 引擎内的 `configuration.verify` | 移入链的 `verify` step（条目形状不变） |
| `algo-pinyin` | 词典根冲突 | 无法察觉，后者覆盖前者，先加载者静默失效 | Acquire 期仲裁，异根即加载失败并指明两个根（§6.4） |
| `algo-pinyin` | 引擎初始化失败 | 仅在 `start` 期报告运行时错误 | 构造后立即校验，执行体创建失败 |
| `algo-pinyin` | 兜底 | 引擎内保留原字并置 error（自相矛盾） | 空发音 + error，由链上的 `fallback` 产出（§6.5） |
| `algo-pinyin` | 多字词 | 只读取首个码点，静默截断 | 拆分为码点整段转换后拼回 |
| `algo-pinyin` | 拒绝未知键 | 未实现 | 按 §2.3 实现 |

表中只列差异，不作为旧栈的现状盘点（现状记录见 D 系列）；S2P 与 Onset 各变体无对应的移植注记。
