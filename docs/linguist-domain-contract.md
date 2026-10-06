# wolf 语言域契约（L1 + L2）

本文规定**语言域身份层（L1）**与**语言组合契约层（L2）**：`linguist` 贡献类别、语言身份、
`WolfLinguist` Level 1 契约，以及歌手侧的语言映射。

上位规范为 [spec 2.4](ds-spec-2.4.md)，分层与跨层不变量见
[linguist-architecture.md](linguist-architecture.md)，冲突时按索引所定的可信源顺序裁决。
链推理契约（G2P / S2P / Onset）见 [linguist-inference-contract.md](linguist-inference-contract.md)；
运行时与宿主接入见 [linguist-runtime.md](linguist-runtime.md)；`configuration` 键汇与资源格式
见 [linguist-variants.md](linguist-variants.md)。

**发布状态**：Level 1 尚未对外发布。稳定前的原地修订不构成 spec 2.4:594
「契约发布后不得作不兼容修改」意义上的发布后修改。

## 0. 词汇与通用约定

### 0.1 四类内容

| 词 | 含义 | 规定方 |
| :-- | :-- | :-- |
| Exports | 模块公开的能力或身份 | (`interface`, `level`) 契约 |
| Import options | 导入方为该次导入指定的选项 | **目标模块**的 (`interface`, `level`) 契约 |
| Configuration | 解释器加载模块所需的实现配置 | `variant` |
| Variables | 契约运行时的输入与输出 | (`interface`, `level`) 契约 |

### 0.2 路径与 JSON 形态

- 表格中的 `path` 是 JSON 字符串。相对路径以**当前声明文件所在目录**为基准；使用前
  `${vars}` 已由 Loader 完成展开，类别与解释器**不得**对已展开字符串再次套用展开规则
  （spec 2.4:88）；
- `array<T>` 表示 JSON array，`map<K, V>` 表示成员名为 `K`、成员值为 `V` 的 JSON object；
- 所有 `exports` 与 import `options` 必须是 JSON object；
- 模块 ID 由所属 Package 在 `contributions` 条目中赋予，模块声明文件不携带 `$version` 与 `id`。

### 0.3 `vars` 字段

模块声明可按 spec 2.4:82-106 使用 `vars`。框架在把声明交给类别与解释器**之前**已完成展开
并从声明对象中剥离该字段（`synthrt/lib/Core/PackageLoader.cpp:406`），因此语言域的字段
白名单不会遇到该字段，本层不对其作出规定。

---

# L1 —— 语言域身份层

本层与具体契约无关：`linguist` 类别下无论承载哪个 `interface`，本层规则一律适用。
本层的全部产物在 **DataOnly** 模式下可用，宿主不加载运行时即可列出已安装的语言。

## 1. `linguist` 贡献类别

`linguist` 是**模块贡献类别**，由 wolf 注册：

- wolf 是由宿主直接链接的共享库。库加载时，库内静态注册对象把该类别加入 synthrt 的类别
  注册表（`src/lib/Linguist/LinguistContrib.cpp:172-173`；注册表为 `stdc::StaticRegistry`，
  见 `synthrt/include/synthrt/Core/ContribCategory.h:158`），因此**链接即完成进程内注册**。
  宿主若不引用 wolf 的任何其他符号，丢弃未引用库的链接器（`--as-needed` 下的 ELF 链接器、
  处理导入库的 MSVC 链接器）会删去该依赖；此类宿主须在构造第一个 `SynthUnit` 之前调用一次
  `wolf::linkLinguistCategory()`（`include/wolf/Linguist/LinguistContrib.h`）；
- 每个 `SynthUnit` 构造时收集注册表中的全部类别（`synthrt/lib/Core/SynthUnit.cpp:14-31`），
  先于任何 Package 解析（spec 2.4:250 同此要求）；
- 注册内容包含类别名、条目解析器与模块类别必备的 provider factory，插件 IID 为
  `org.openvpi.wolf.plugin.LinguistProvider`（`LinguistProviderPlugin::IID`）；
- 上位规范要求的**有序插件搜索路径**（spec 2.4:250）不随注册携带，由宿主按类别经
  `SynthUnit::setPluginPaths` 配置（`synthrt/include/synthrt/Core/SynthUnit.h:66-67`）；
- 宿主未链接 wolf 时该类别未注册，含本类别贡献的 Package 被加载器整体拒绝
  （`PackageLoader.cpp:1210-1212`）。

类别名 `linguist` 为 OpenVPI 官方类别，与 `inference` / `singer` 同样使用裸名，不属于
spec 2.4:248「第三方类别推荐反向域名」的适用范围。

### 1.1 贡献条目

```json
"contributions": {
    "linguist": [
        { "id": "cmn-pinyin", "path": "./linguists/cmn-pinyin/linguist.json" }
    ]
}
```

条目由 `id` 与 `path` 两个键构成。条目 schema 归类别所有（spec 2.4:230「其余字段由该类别
自行规定」、:250「注册该类别的条目解析器或 schema」）。wolf 对条目中的未知键记一条警告并
忽略该键，不判加载失败，理由是 spec 2.4 的 JSON 约定规定未知字段不得使文档无效，后续版本
新增的条目字段因此仍可在当前版本下加载。声明根上既不属于框架公共字段、也不属于 §2 类别
追加字段的键按同一规则处理。

`path` 指向语言声明文件，约定为 `linguists/<id>/linguist.json`。引用语言模块与引用其他模块
的文法相同：`other-pkg:linguist/jpn-romaji`、`:linguist/cmn-pinyin`（当前包）。

### 1.2 推荐目录结构

```
+ linguists
  + cmn-pinyin
    - linguist.json      // 语言组合声明
    - phonemes.json      // exports.phonemes 所指文件（内联数组时可无）
+ inferences
  + cmn-pinyin-g2p
    - inference.json     // G2P 模块声明（词典等资源就近放置）
  + cmn-pinyin-s2p
    - inference.json
  + cmn-onset
    - inference.json
```

推理模块声明**放在 `inferences/` 下**，与 spec 2.4:775-800 的推荐布局一致；语言目录只放
语言组合声明及其直属资源。同包含多个语言时，以模块 ID 前缀区分推理模块目录。

> 本布局是推荐，不是规范。相对路径的基准恒为**声明文件自身所在目录**，与目录的组织方式无关。

## 2. 语言身份

语言身份由声明根的两个**类别追加字段**承载（spec 2.4:529-535 的第二层：由贡献类别规定、
对该类别下所有模块生效、由类别自行解析）。同层先例为 `singer` 的
`avatar` / `background` / `demoAudio`（`synthrt/lib/SVS/SingerContrib.cpp` 中的
`SingerCategory::createSpec`）。

| 字段 | 类型 | 必选 | 说明 |
| :-- | :-- | :-- | :-- |
| `language` | string | 是 | **语言句柄**：ISO 639-3 代码，形如 `[a-z]{3}`（含 `qaa`-`qtz` 私用区） |
| `scheme` | string | 是 | **注音体系**：`[a-z0-9]+(-[a-z0-9]+)*` |

### 2.1 `scheme` 的语义

`scheme` 标识**发音层的记法体系**，是本契约族的互换契约：

> 在**同一 `language` 下**，两个模块声明同一 `scheme`，即表示其 `pronunciation` 串可互相消费。

**`scheme` 由 `language` 定域**：匹配键恒为二元组 `(language, scheme)`，而不是单独的 `scheme`。
因此 `(cmn, pinyin)` 与 `(deu, pinyin)` 是两个互不相干的二元组，同名不构成任何互换约定；
同一个 `scheme` 名也可以在不同语言下分别表示各自语言的记法，不要求全局唯一。

`scheme` 是 G2P / S2P 与语言组合之间唯一的匹配键（见 §5.3），不是装饰性字段。官方体系名由
wolf 维护清单（治理方式同变体名，见 §9）；第三方使用反向域名或厂商前缀，以避免同名导致的
语义分歧。

### 2.2 贡献 ID 语法

```
id = <language> "-" <scheme> [ "-" <qualifier> ]
```

- `qualifier` **无语义**，不参与任何匹配，其作用是使同一二元组的多个贡献（变种、
  不同作者、精简版）在同一包内并存；
- ID 的字符集由框架约束：贡献 ID 必须是合法 segment `[A-Za-z0-9_-]+`
  （`synthrt/lib/Core/ContribLocator.cpp:43-53`），区分大小写。

**该规则是书写惯例，不是加载期条件。** 加载器与本契约族的任何匹配都不读取 ID；不符合本形式的
ID 只在**打包期 lint** 中产生警告，不影响加载。lint 的判据是前缀比对而非解析：`id == language +
"-" + scheme`，或 `id` 以 `language + "-" + scheme + "-"` 开头且余部非空。由此：

- **`scheme` 内部的连字符不产生歧义**：`cmn-pinyin-lite` 在
  (`scheme=pinyin`, `qualifier=lite`) 与 (`scheme=pinyin-lite`, 无 qualifier) 两种声明下
  都合法且各自自洽，因为任何一方都不从 ID 反推语义；
- **ID 是冗余副本，声明字段是真值**。本契约族的匹配、绑定与路由均不读取 ID。

> 不作加载期强制校验的理由：ID 的前缀不携带额外信息（真值在 `language` / `scheme` 两个字段中），
> 强制校验只会取消 spec 2.4:506-508 明确赋予 Package 的命名自由，即「同一份模块目录被两个
> Package 收录时两边可以各自命名」。

### 2.3 加载期校验（Probe）

由 `LinguistCategory::createSpec` 执行，任一项失败即整次加载失败：

1. 条目引用的声明文件存在（条目中的未知键只产生警告，见 §1.1）；
2. `language` 存在、为 string、匹配 `[a-z]{3}`；
3. `scheme` 存在、为 string、匹配 `[a-z0-9]+(-[a-z0-9]+)*`。

**ID 形态不在此校验**（§2.2），属于打包期 lint 项。

本层**不**校验 `exports`、`configuration` 与 `imports` 的内容，这些内容由 L2 与解释器校验。

### 2.4 DataOnly 身份面

`LinguistSpec` 提供 `language()`、`scheme()` 与 `locator().contributionId()`。三者均只依赖
Probe 期的解析结果，**在 `DataOnly` 模式下同样可用**（`DataOnly` 在 typed manifest 构造完成后
即停止，不涉及 provider 与运行时）。宿主的语言清单、缺依赖预检与安装引导据此实现，不需要
加载任何插件。

---

# L2 —— 语言组合契约

## 3. `org.openvpi.wolf.linguist.WolfLinguist`

语言组合。三元组固定为 (`org.openvpi.wolf.linguist.WolfLinguist`, 1, `wolf`)。

声明文件使用公共字段与 §2 的两个类别追加字段；`name` 为多语言文本，缺省等价于 `{"_": <id>}`。

| Interface | Level | Variant | 说明 |
| :-- | --: | :-- | :-- |
| `org.openvpi.wolf.linguist.WolfLinguist` | 1 | `wolf` | 以固定 role 绑定 G2P / S2P / Onset，导出链末端音素全集 |

## 4. Exports

`exports` 必须提供 `phonemes`，可另给 `openSet`。

| name | type | 必填 | 说明 | 示例 |
| :-: | :-: | :-: | :-- | :-- |
| `phonemes` | path \| array&lt;string&gt; | 是 | 本语言默认封闭链（自带 G2P + S2P）末端可能产出的**内容音素清单**。`openSet` 为假时即全集 | `"./phonemes.json"` |
| `openSet` | boolean | 否 | 缺省 `false`。为真表示产出可能落在清单之外 | `true` |

- 保留音素（如 `SP` / `AP` / `EP`）不列入清单；
- 写为路径时指向内容为 `array<string>` 的 JSON 文件；元素须为非空且互不重复的字符串。

**加载期形状校验**（Acquire，由 wolf provider 执行，失败即整次加载失败）：`exports` 必须是
object 且不含上表以外的键；`phonemes` 必须存在；写为路径时所指 JSON 必须为数组；元素非空且
不重复。`openSet` 若出现须为布尔值，**类型不符的值不按 `false` 处理**，因为 `false` 表示清单
为全集，误判为全集的后果比误判为非全集更严重。机器可读 schema 为
`docs/schemas/linguist-1-exports.schema.json`。

### 4.0 `openSet`：非全集清单的声明

`phonemes` 为必填项，而**十二条既有链的末步都是「查不到即输出原词」**，此时产出包含用户输入的
任意文本，任何清单都不可能是全集。引入本字段之前，此类语言只能留空清单（不提供任何信息）或
声明一个与事实不符的集合，两者均不符合「必填」的含义。

| `openSet` | 清单的含义 | 宿主可据此执行的操作 |
| :-- | :-- | :-- |
| `false`（缺省） | **全集**。语义与引入本字段之前完全一致，既有声明无需改动 | 静态比对具有权威性：清单外的音素即缺陷 |
| `true` | **已知部分** | 静态比对不完整；可提示「本语言可能产出声库无法识别的音素」 |

**本字段的取值由链结构决定，不依赖作者判断**：G2P 链含 `useOriginal` 为真（缺省即为真）的
`fallback` 步时应为真。`scripts/convert-g2p-packages.py` 生成声明时据此写入该字段；
`scripts/check-declarations.py` 在链可能输出原词而未声明 `openSet` 时，以及声明了 `openSet`
而链不可能产出表外结果时，各报一条警告。

### 4.1 对齐基准

`phonemes` 是语言链末端与消费方的对齐基准：宿主据此核验实际组装链的 S2P 产物，模型侧据此
对照各 stage 音素表。语言包作者拥有默认链的全部模块，因此该全集可以静态给出。声库逐项覆写
链路成员导致清单与实际产物不符的，属内容缺陷；是否拒绝由消费方决定，**Level 1 不做加载期
强制校验**。

与各 stage 模型音素表的交集裁决规则：

- 各 stage 的「模型音素表」以推理模块 `onnx` 变体 `configuration.phonemes` 所指的
  「音素名 ↔ ID 表」文件为准；该 `onnx` 变体是规范 2.4 为 Inference 契约定义的变体
  （`docs/ds-spec-2.4.md:562`），该字段由声库侧的模型模块提供，不属本仓；
- 校验方向为 **`phonemes` ⊆（各 stage 模型音素表的交集）**，即语言产出的每个音素都
  必须能被声库所用的各 stage 编码；
- 该裁决**属于编辑器运行期职责，加载事务不执行**；
- 未提供模型音素表的 stage 不参与交集；全部 stage 均未提供时，编辑器跳过核验并给出提示，
  不得判加载失败。

## 5. imports（组合规则）

语言的 `imports` 是本契约族唯一规范性的组合层。Level 1 的 role 集合固定如下：

| role | 目标契约 | 数量 | 用途 |
| :-: | :-- | :-: | :-- |
| `linguist/g2p` | `org.openvpi.wolf.inference.G2P` | 恰好 1 | 歌词 → 发音 |
| `linguist/s2p` | `org.openvpi.wolf.inference.S2P` | 0..1 | 发音 → 音素序列；**缺席时该语言最深只到 `Depth::Pronunciation`**，见 §5.0.1 |
| `linguist/onset` | `org.openvpi.wolf.inference.Onset` | 0..1 | 音素 → onset 标记；**缺席时逐位输出 `false`**，见 §5.0 |
| 其他 | 不限 | 任意 | Level 1 未定义用途。wolf 不绑定此类 import，加载器记一条警告后忽略；打包 lint 判错（§12） |

```json
"imports": [
    { "role": "linguist/g2p",   "ref": ":inference/cmn-pinyin-g2p" },
    { "role": "linguist/s2p",   "ref": ":inference/cmn-pinyin-s2p" },
    { "role": "linguist/onset", "ref": ":inference/cmn-onset" }
]
```

### 5.0 Onset 缺席时的规定产出

synthrt、wolf、otter 与 ds-editor-lite 四个仓库中均没有非空的 Onset `rule` 资源，因此 Onset
缺席不是边缘情形，而是当前唯一实际生效的路径。规定如下：

**无 `linguist/onset` 绑定时，`onsets` 与 `phonemes` 等长，逐位为 `false`。**

该取值既非「未定义」，也不交由宿主决定。`onsets` 与 `phonemes` 等长是既有约束，长度已经确定，
剩余的自由度只有取值；若交由宿主决定，两个宿主会对同一语言包给出不同的切分。实现
（`src/plugins/linguistproviders/wolf/LinguistExecutiveImpl.cpp`）始终按此行为执行，本节将其
写入契约。

### 5.0.1 S2P 缺席时的深度上限

`linguist/g2p` 是唯一必选的 role。`linguist/s2p` 缺席时：

**该语言的最深深度是 `Depth::Pronunciation`。宿主请求更深的 `depth` 时，转换在发音层结束，
不报错。**

该形态是合法的语言形态而非容错：在 A66 的组合路径中，语言包提供 G2P，歌手包提供 S2P 与
onset，缺少歌手包部分时的正确行为是停在发音层。深度上限在初始化期即可从 `imports` 集合读出，
经 `WolfPipelineExtension::maxDepth` 与 `LanguageStatus::maxDepth` 提供给宿主（A70、A71），
**无需先执行一次转换**。

放宽下界属于 `variant` 的权限，不是规范改动：spec 2.4 §`imports` 把「哪些 role 必须存在」交给
导入方自己的 `variant`，并写明导入方「**仍可**」严格要求，即允许而非必须。对既有声明而言这是
纯增量修改（原先失败的声明变为可加载，可加载声明的行为不变），因此不触发 `compatVersion` 抬升。

### 5.0.2 发音层是否独立于音素层

两层形状由 S2P 成员决定，且**在初始化期即可读出**（A81）：经
`WolfPipelineExtension::hasSeparatePronunciationLayer` 与
`LanguageStatus::hasSeparatePronunciationLayer` 提供给宿主，与 `maxDepth` 同源、同样不创建任何对象。

**有独立发音层 ⇔ S2P 成员转换符号**：

| 组合形态 | 发音层与音素层 | 取值 |
| :-- | :-- | :-- |
| S2P 成员为 `direct` | 符号相同（只按保留分隔符切分） | `false` |
| S2P 成员为 `dict` / `mapping` / `lua` | 改写字素（逐行查表／逐音素替换／脚本） | `true` |
| 无 S2P 成员（§5.0.1 形态） | 发音即最深可达层 | `false` |

**音素层始终存在**：最深可达层的符号即音素层。宿主据此判断 `pronunciation` 是独立的发音层还是
音素层的内容本身。该判定与 `maxDepth` **正交**：可达 `Onsets` 的组合两种取值都有——字典形态的 cmn
为 `true`，`direct` 形态的语言为 `false`。

判据只读 **variant**：S2P 成员的 TSV 表与脚本在 Acquire 期读取，查询期不创建对象。因此「表内容
其实是恒等」这类内容级事实不由运行期判定：`dict` / `mapping` 的表逐行只做等同或切分时，
`check-declarations.py` 报 warning 由作者定夺（§12）；`mapping` 表可读但无任何可用行时同理——没有
条目可套用，其产出就是 `direct` 的产出；`lua` 的输出无法静态判定，一律按 `true` 处理——宁可多呈现
一层发音，不把发音误判成音素。**`dict` 空表不在此列**：它未命中键时产出空序列，是另一种缺陷。

**该布尔值的粒度是整个组合，不到音节**。现实里存在**混合表**：本仓声库夹具实测，
`wolf-voicebank-zh/inferences/s2p-cmn/opencpop-extension.txt` 的 615 个有效行中有 13 行符号恒等，
`s2p-yue/jyutping-extension.txt` 的 639 行中有 25 行恒等。这类表整体仍在改写符号（绝大多数行确实
在改写），因此整语言按变体名报 `true`。消费方**不得**把 `true` 读成「每个音节的发音层都独立」；
逐音节的判断只能看该音节自身的产出。

`direct` 的恒等是**符号级**而非字符串级：连续空格被折叠成一个分隔、首尾空格丢弃，所以
`"a  b"` 与 `"a b"` 产出同一组符号。不要拿原始字符串比较来判定恒等。

内容级例外只产生 **warning**，不阻断发布：`make-lang-release.py` 只在脚本以非零退出码结束时中止
（`scripts/make-lang-release.py:191-197`），而 `check-declarations.py` 只在存在 error 时返回 1。
因此「改用 `direct`」是**容错性建议**，是否照做由作者判断（§12）。

本节是**纯增量查询**（既有声明的加载与转换行为不变，只多一条可读属性），不触发 `compatVersion`
抬升。

### 5.1 基数的两侧分工

- **上界由框架结构性保证**：`role` 在模块内唯一由框架强制（spec 2.4:634；
  `PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport` 以 role 为键 `try_emplace`）。
  三个固定 role 名各自至多出现一次，**wolf 不重复校验上界**；
- **下界由 wolf 校验**：缺少 `linguist/g2p` 即加载失败（Ready-2）。缺少 `linguist/s2p` 或
  `linguist/onset` 不导致失败，分别按 §5.0.1 与 §5.0 规定的形态降级。

### 5.2 目标契约与 level

上表只约束目标**契约**与 role，不约束 `level` 数值：

- 被引用目标的三元组须命中某个 provider 插件为其声明的 (`interface`, `level`, `variant`) 条目，
  否则 Probe 阶段即报「找不到提供者」（`PackageLoader.cpp:769-774`）；
- 被引用模块的运行时变量按其自身声明的 level 执行，本表的基数与匹配规则不随目标 level 变化。

### 5.3 二元组命中（本层的核心裁决）

> 语言自身的 `(language, scheme)` 必须命中其 `linguist/g2p` 与 `linguist/s2p` 目标声明的
> `exports.languages`，未命中即加载失败。**只判定已声明的 role**：`linguist/s2p` 缺席时
> 不存在第二侧，不构成未命中。

- 命中判定为**二元组整体相等**：`language` 与 `scheme` 两个字段均相等；
- 目标**省略** `exports.languages` 时跳过该侧判定（可加载，宿主应告警，见链推理契约的
  Exports 省略条款）；
- 该规则取代了「按贡献 ID 相等匹配」的旧规则，带来三项结果：
  1. G2P / S2P 作者**无需知道任何 linguist 贡献的 ID**；
  2. 第三方语言贡献（ID 带 `qualifier`）只要 `scheme` 相同即自动对接官方 G2P；
  3. 「拼音 G2P 接粤拼 S2P 词典」这类错配在**加载期即失败**，而不是在运行时表现为大面积未命中。

语言不感知 G2P 的内部组合（是否需要模型后端、需要几个后端由 G2P 通过自身的 `imports` 设置），
因此语言层的 role 表固定不变。

引用其他 Package 的模块时，导入方在 `desc.json` 的 `dependencies` 中声明对目标包的依赖。

## 6. Import options

**无。** 语言 Level 1 不定义 import `options` 词汇。

导入方省略 `options` 时框架交付空 object（`PackageLoader.cpp:1358`）；显式给出非 object
（如 `null`）或含任何键的 object 时，provider 返回 `InvalidFormat`。机器可读 schema 为
`docs/schemas/linguist-1-import-options.schema.json`。

运行时的语言选定不经 import options：组合 provider 在创建子执行体时经 `RuntimeOptions`
注入 `(language, scheme)`（见 [linguist-runtime.md](linguist-runtime.md) §3.1）。清单因此无需
重复语言声明中已有的信息，两侧也不存在不一致的可能。

## 7. Configuration

Level 1 不定义任何 `configuration` 键。`wolf` 变体要求 **`configuration` 必须显式写为空对象
`{}`**，出现任何键即加载失败。

> 省略该字段时框架交付缺省 JSON 值 Null（`PackageLoader.cpp:1316-1319`），provider 以非
> object 为由返回 `InvalidFormat`。本条与 spec 2.4:522「`configuration` 为可选公共字段」的关系：
> 契约有权要求其所辖模块显式提供某个可选公共字段，这属于契约层的加严，不违反框架规定。

语言固有的词典、规则与模型资源一律归入所导入模块的 `configuration`。

## 8. Variables

**无。** 语言不承载逐单元推理 IO。语言的运行时角色是**装配**：为每条语言导入提供执行工厂，
产物为聚合了 G2P / S2P / Onset 子执行体的 `LinguistExecutive`，其可观察行为由 L4 规定。

## 9. 变体治理

本层只有一个变体：三元组固定为 (`WolfLinguist`, 1, `wolf`)；变体可随时新增，新增变体不涉及本
契约的修改（spec 2.4:769）。其余规则（框架按三元组全匹配选择 provider、`configuration` 不参与
选择、裸变体名由 wolf 官方保留、第三方用反向域名变体）见
[linguist-variants.md](linguist-variants.md) §1.1、§1.2。

---

## 10. 歌手侧：语言映射

歌手用两个**类别追加字段**声明所支持的语言及缺省语言。二者由 `SingerCategory`
（synthrt）解析，与 `avatar` / `background` / `demoAudio` 同层。

```json
{ "languages": { "cmn": "lang/mandarin", "jpn": "lang/japanese" },
  "defaultLanguage": "cmn",
  "imports": [
      { "role": "singer/acoustic", "ref": ":inference/acoustic" },
      { "role": "lang/mandarin",   "ref": ":linguist/cmn-pinyin" },
      { "role": "lang/japanese",   "ref": "wolf/lang-jpn:linguist/jpn-romaji" } ] }
```

| 字段 | 类型 | 必选 | 说明 |
| :-- | :-- | :-- | :-- |
| `languages` | map&lt;string, string&gt; | 否 | **语言句柄 → 本声明内的 import role** |
| `defaultLanguage` | string | `languages` 非空时**必选** | 缺省语言句柄，必须是 `languages` 的键 |

### 10.1 映射值取 role 的设计理由

ImportBinding 只由 `imports` 数组产生（`PackageLoader.cpp:894-917`）；数组之外的
ModuleReference 不产生任何绑定，运行时因此无法取得执行工厂。映射值只能指向**已有的 import**，
这由框架结构决定，而非风格选择。

### 10.2 歌手 role 自由命名

歌手侧的 `role` 是纯粹的本地槽位名，符合 spec 2.4:648「`role` 是导入方为该条目指定的
本地 slot」。语言身份由 `languages` 映射承载，**不由 role 后缀编码**。

`linguist/` 前缀仅为书写建议，无规范效力。

### 10.3 唯一映射约束

> 同一声库中每个语言同时只支持一种注音体系。

该约束**结构性成立，无需校验代码**：`languages` 是 JSON object，
`JsonObject = std::map<std::string, Value, std::less<>>`（stdcorelib `support/json.h:68`），
每个语言句柄至多对应一条映射，因而至多对应一个语言导入与一种 `scheme`。

> 附注：源 JSON 中的重复键被 map 静默折叠，而不是按 spec 2.4:72「同一层不得出现重复 key，
> 违反时整份声明无效」报错。这是 stdcorelib 的符合性缺口；本约束所依赖的不变量（每个键至多
> 一条映射）不受影响。

未被 `languages` 引用的 linguist import 合法但不参与语言域，由编辑期 lint 提示，不判加载失败。

### 10.4 `defaultLanguage`

- **无加载期与运行时语义**，仅作为宿主策略的输入，语言域自身不读取该字段；
- 必须显式给出而不能取「第一个」键的原因：`languages` 是 object，成员按键排序，**声明顺序
  不保留**（stdcorelib `support/json.h:68`），object 中不存在「第一个」成员；
- 宿主的「跟随歌手」语义据此解析（见 [linguist-runtime.md](linguist-runtime.md) §7.1）。

### 10.5 校验分工

| 校验 | 执行者 | 阶段 |
| :-- | :-- | :-- |
| `languages` 是 object、值全为 string；每个键是非空合法 segment | `SingerCategory` | Probe |
| 每个值命中本声明 `imports` 中的某个 role（`ContribCreateContext::imports()`，`ContribCategory.h:60`） | `SingerCategory` | Probe |
| `defaultLanguage` 为 string 且是 `languages` 的键；`languages` 非空时必填 | `SingerCategory` | Probe |
| 键形如 `[a-z]{3}`。不单独校验：目标声明的 `language` 已在 Probe 通过该形状校验，末行的相等判定蕴含本项 | **wolf validator** | Ready-2 |
| 值指向的 import，其目标类别为 `linguist`，且目标三元组为 (`WolfLinguist`, 1, `wolf`) | **wolf validator** | Ready-2 |
| 目标声明的 `language` 等于映射键 | **wolf validator** | Ready-2 |

**synthrt 只处理「一个语言标签映射到一个 import role」，不涉及 linguist 的语义。**
形状与 role 存在性由框架校验，语言域语义由 wolf 校验。两个字段因此对任何语言体系通用，
wolf 的领域知识不进入框架。

---

## 11. 校验的分层落点

框架**没有「仅告警」通道**：凡进入加载事务的校验，失败即整次加载失败（spec 2.4:444；
`PackageLoader.cpp` 的 Acquire/Ready 钩子失败即中止，不存在旁路告警分支）。提示级检查一律
由编辑器 lint 与宿主日志承担。

本文各节的落点**不另立整表**。整表——列为「相 | 执行者 | 校验内容 | 层 | 框架执行点」，含 L1 / L2
各行与 `synthrt/lib/Core/PackageLoader.cpp` 行号——见
[linguist-architecture.md](linguist-architecture.md) §6「校验矩阵」。本文各节的对应关系为：
§2.3 → Probe / `LinguistCategory::createSpec`；§10.5 前三行 → Probe / `SingerCategory::createSpec`；
§4、§7 → Acquire / 组合 provider；§6 → Ready-1；§5.1 下界、§5.2、§5.3、§10.5 后三行 →
Ready-2 / wolf validator；Ready-3 的挂载由 L4 规定。

### 11.1 Ready-2 的已知盲区

`ContribImportValidator` 只来自**已加载的解释器**（`ContribPluginFactory.cpp:128-131`），而
解释器只在事务内有贡献选中其三元组时才会加载。因此，**依赖闭包内不含任何 linguist 贡献时，
wolf 的歌手侧校验不执行。**

后果与对策：对于声明了 `languages` 却未 import 任何 linguist 的歌手包，`SingerCategory` 仍会
检出映射值「role 不存在」（该校验在框架侧恒定执行），但「role 指向的不是 linguist」这类判定
缺失。因此，**必须恒成立的结构性规则一律放在 Probe（L1），Ready-2 只承载需要跨模块信息的
判定**。本文的分工按此原则划定。

## 12. 归编辑期 lint 的比对

以下各项均为警告级，框架与 wolf provider 均不据此判加载失败：

- 语言 `phonemes` 与各 stage 模型音素表的交集裁决（§4.1）；
- S2P `phonemes` 产出集 ⊆ 语言链末端 `phonemes`（超出即告警）；
- Onset 对链上实际音素的覆盖比对，按**下界规则**执行：未列入 `knownPhonemes` 不等于未被
  规则覆盖，被通配 `"*"` 覆盖的音素不得告警；
- G2P `symbols` 可为音节等非音素原子符号，与音素类导出集异类，Level 1 **不**为其规定与任何
  音素集的跨键比对；
- 未被 `languages` 引用的 linguist import（§10.3）；
- 贡献 ID 不符合 `<language>-<scheme>[-<qualifier>]` 书写惯例（§2.2）；
- S2P `dict` / `mapping` 表的内容级恒等（§5.0.2）：表的每一可用行都保持其键的符号时，组合会被
  上报为持有独立发音层，与表中实际内容矛盾；`mapping` 表可读但没有任何可用行时同理（其产出与
  `direct` 逐符号相同）。

**执行方**：后三项只依赖声明本身，由 `scripts/check-declarations.py` 执行，并由
`make-lang-release.py` 在打包前调用，因为打包是修正不合规声明成本最低的最后环节，也是声明
成为他人下载物的起点。前四项需要声库或模型的音素表，属于编辑器运行期职责，打包期无法执行。

该脚本对加载器同样拒绝的缺陷判错，对上述 lint 项判警告（加载器不据此失败），两者的严格度
因此与运行时一致。判错范围与加载器对齐：schema 不符；身份字段与包 id、版本号的文法（与
synthrt 加载器使用同一文法：1–4 段十进制、无前导零、段非空；id 为 `/` 分隔的 `[A-Za-z0-9_-]+` 段，
语言与 scheme 的正则直接读自 `docs/schemas/language-scheme.schema.json`）；缺少 `linguist/g2p`
以及**未知 role**（加载器忽略未知 role，组合的实际深度因而浅于声明所示，并记一条警告）；每个
role 的目标接口须与该 role 对应；G2P / S2P 成员声明了 `exports.languages` 时须包含本 linguist
的二元组（§5.3）；歌手的语言须路由到同语言的 linguist 贡献（§10）。需要读取 import 目标的检查
仅在目标包位于命令行给出的包集合内时执行，否则跳过，不作推测。声明无法读取或缺少字段时，
脚本报告所在包并继续执行。未知接口的声明判警告，因为 lint 没有该接口的 schema，无法检查。

有两条规则 lint 比加载器严格：依赖版本须等于目标包的兼容下界（见发布文档）；歌手语言路由到
非 linguist 模块时，加载器仅在同一事务中恰好建有 linguist provider 时拒绝，否则放行（见会话
文档的相应说明）。

对照关系由测试固定：`src/tests/auto/loader-verdicts.json` 记录每个测试包在加载器下的结论，
`test_LinguistLoad` 逐个加载并核对该记录，`scripts/test_check_declarations.py` 读取同一记录，
要求 lint 必须拒绝加载器拒绝的包，例外仅限记录中注明理由（lint 在声明层面无法检测的原因）的
条目。
