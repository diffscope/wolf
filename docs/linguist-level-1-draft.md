# DiffSinger 语言契约 Level 1（修订版）

本文定义 wolf 注册到 synthrt 的 `linguist` 贡献类别、语言组合契约，以及 G2P / S2P / Onset / G2PModel / DictQuery 五份契约的 Level 1：

- WolfLinguist 与 G2P / S2P / Onset 是语言组合直接使用的契约；
- G2PModel（模型后端）与 DictQuery（词典查询）是辅助面契约——只经 imports 被编排变体或宿主工具消费，不占用语言组合的 role 名额（第四轮拓扑决策，见变体文档台账 D15-D18）。

Package、贡献类别、模块三元组、`imports`、ModuleReference、`dependencies` 等概念遵循 [DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)。

本文只钉契约面：类别与三元组的存在性、Exports / Import options / Variables、`level` 变更纪律、以及宿主可见的组合与裁决语义。

- `exports`、import `options` 与 Variables 属于 (`interface`, `level`) 契约；
- `configuration` 及其资源格式属于 `variant`——**wolf 收录变体**的键汇与收录清单由 wolf 变体文档维护（当前暂存于 [linguist-g2p-variants-wolf-draft.md](linguist-g2p-variants-wolf-draft.md)，迁入 wolf 仓库后以其发布物为准；第三方变体的键汇由第三方自行管辖与发布，不入该文档），外部系统不依赖其实现细节。

**发布状态**：Level 1 尚未对外发布；稳定前的原地修订不构成上位规范「契约发布后不得作不兼容修改」（spec 2.4:594）意义上的发布后修改（口径同 [Status](Status.md) 的预发布声明）。

**重排注记**：2026-08-30 版式重构——仅分段分点，语义零改动（方案与决策台账见 `docs/plans/linguist-docs-restructure.md`）。

**修订注记**：2026-09-05 消费方对齐修订——Singer 语言导入 role 后缀钉为语言句柄、缺省语言解析（D29）；§8.9 备案区批量清理（台账见变体文档 §8.12）。

| Interface | Level | Variants | Description |
| :-- | --: | :-- | :-- |
| `org.openvpi.wolf.linguist.WolfLinguist` | 1 | `wolf` | 语言组合：以固定 role 绑定 G2P / S2P / Onset 推理模块，导出链末端音素全集 |
| `org.openvpi.wolf.inference.G2P` | 1 | `pipe-chain` / `algo-pinyin`（详见变体文档） | 歌词 → 发音（宿主面主转换器） |
| `org.openvpi.wolf.inference.G2PModel` | 1 | `multig2p-onnx`（详见变体文档） | 词 → 发音与候选（模型后端；仅编排变体经 imports 消费，语言不可直引） |
| `org.openvpi.wolf.inference.DictQuery` | 1 | `dsdict`（详见变体文档） | 词典键 → 值查询（宿主工具/编排变体消费；不参与语言组合） |
| `org.openvpi.wolf.inference.S2P` | 1 | `dict` / `direct` / `mapping` / `lua`（详见变体文档） | 发音 → 音素序列 |
| `org.openvpi.wolf.inference.Onset` | 1 | `rule` / `lua`（详见变体文档） | 音素 → onset 位置标记 |

> 命名对照注记（信息性，非规范性）：外部生态的用词与本契约族呈「镜像」关系——
>
> - 外部所谓 g2p（g2p_en、OpenUtau `G2pPack`、MFA `mfa g2p`）多指「词 → 发音」的模型/词典后端，对应本契约族的 **G2PModel**；
> - 外部所谓 phonemizer（OpenUtau、Coqui、Piper，输入含音高时值的编排层）才对应本契约族的 **G2P** 宿主面；
> - 「发音词典作为一等公民」对应 **DictQuery**（先例：OpenUtau `dsdict.yaml`、MFA pronunciation dictionary，以及 lm/synthrt 旧栈以独立类别承载的 `dict.dsdict` 插件键）；
> - S2P / Onset 沿用本组织已发布库 phoneme-converter 的既定类名与变体切分。
>
> 六接口名经第五轮定案维持不变（台账 D19/D21，见变体文档 §8.7）。

## 前置说明

### 词汇（Exports / Import options / Configuration / Variables）

- Exports：模块公开的能力，由对应 (`interface`, `level`) 契约规定
- Import options：导入方引用模块时为该次导入指定的选项，由目标模块的 (`interface`, `level`) 契约规定
- Configuration：解释器加载模块所需的实现配置，由 `variant` 规定
- Variables：契约运行时的输入与输出，按调用单元的语义描述（对应 dsinfer 契约中的「Model variables」，本文按调用单元而非模型张量描述）

### 路径与 JSON 形态约定

- 本文表格中的 `path` 是 JSON 字符串：相对路径以当前 contribution 声明文件所在目录为基准，使用前已完成 `${vars}` 变量展开；展开由 Loader 在声明交付 Category 与解释器之前统一完成，Category 与解释器均不得对已展开字符串再次套用展开规则；
- `array<T>` 表示 JSON array，`map<K, V>` 表示成员名为 `K`、成员值为 `V` 的 JSON object；
- 所有 `exports` 和 import `options` 都必须是 JSON object。除下文明确要求的字段外，其余字段可以省略；
- 模块 ID 由所属 Package 在 `contributions` 条目中赋予，模块声明文件不携带 `$version` 与 `id`；`$version` 只写在 `desc.json`。

### 语言贡献 ID 形态约定

- 基本形为 `<iso-639-3>-<注音体系>` 全称（`cmn-pinyin`、`jpn-romaji`、`yue-jyutping`、`eng-arpabet`…）；同一语言的多种注音体系以多张 ID 并列（如 `cmn-pinyin` 与 `cmn-bopomofo`）；
- 允许追加一个自定义字段作为作者标志或同包多方案的区分（如 `cmn-pinyin-v2`）；自定义字段无语义、不被剥除，匹配一律以全 ID 恒等判定；
- ID 内以连字符分隔的部分称字段（iso／注音体系／可选自定义字段），各字段内部不得再含连字符；
- 第一个字段（iso-639-3 代码）称为该语言的**语言句柄**，跨贡献指称同一语言时使用；
- ID 是结构标识符，同 Package 同类别下不得重复。

### 组合规则

语言贡献按固定 role 导入推理模块：

- 必须以 `linguist/g2p` 与 `linguist/s2p` role 各导入恰好一个 G2P 与 S2P contribution，可以以 `linguist/onset` role 导入至多一个 Onset contribution；
- 违反基数即语言模块加载失败（由 provider 在加载事务的 Ready 校验中执行）；
- G2P 导出 `languages` 时，语言的贡献 ID 必须恒等命中其中一项，未命中即语言模块加载失败（由 provider 在加载事务的 Ready 校验中执行，显式/隐式双侧落点分工见《自动组合校验的分层落点》与下文 imports 节）；
- Singer 以 `linguist/` 前缀的 role 导入语言贡献，role 后缀必须为所导语言贡献的语言句柄（如 `linguist/cmn`，见《Singer 侧配套》）；role 在模块内唯一（框架既定）加后缀=句柄，使同一歌手导入的语言其语言句柄必然互不相同（唯一映射约束，效力归属见《Singer 侧配套》）；语言加载失败则依赖它的歌手加载随之失败；
- 导入方不得仅因遇到自身不认识的 role 而拒绝整个模块，但仍可严格要求本契约规定的 role 存在且只指向规定的目标契约。

### 自动组合校验的分层落点

语言链的组合一致性按加载事务分层执行。框架无「仅告警」通道——凡进加载事务的校验，失败即整次加载失败（spec 2.4:444；synthrt main 实现的 Acquire/Ready 全部钩子失败即中止，无旁路告警分支）；提示级检查一律归编辑器 lint 与宿主日志：

- L0 模块自验（Acquire）：provider 校验本模块声明、`exports` 与 `configuration`（如本类别 `phonemes` 的形状校验、各变体 `configuration` 键汇）；
- L1 单条 options 校验（Ready pass 1）：被引用目标的 provider 校验该条 `options`（如 G2P 的 `languageId` 与其 `exports.languages` 命中）——对应 synthrt main 的 `createImportOptions` 钩子（`PackageLoader.cpp:873-893`，同步失败即整次加载失败）；
- L2 图级组合校验（Ready pass 2）：importing module 的 provider 依 spec 2.4:426 验证有序 imports 集合的数量、顺序、目标 interface/level 与组合关系；框架执行点为对事务内每个新 spec 平铺调用全部已加载解释器贡献的 `ContribImportValidator`（`PackageLoader.cpp:924-944`，注释 «Ready pass 2 validates the complete prepared import graph» :999）。

  wolf linguist provider 的 validator 在此执行，覆盖面分四项：

  - 本类别 role 校验与基数（上界收敛项见 D11 注记）；
  - Singer 侧语言导入三元组等值、`linguist/` 前缀与 role 后缀=目标贡献语言句柄校验（现行实现仅前二层，狼仓 `WolfLinguistProvider.cpp:94-152`；后缀校验随迁移收敛）；
  - 语言 imports 未显式给出 `options.languageId` 时以贡献 ID 对目标 `exports.languages` 的命中校验（现行实现未执行，见前置说明注记）；role 后缀=句柄成立后，语言句柄唯一映射由 role 唯一性（框架既定）结构性成立（见《Singer 侧配套》）；
  - 跨模块相容性校验在 dsinfer 已有先例（vocoder 对 acoustic 的 `validateCompatibilityWith`：`lib/SVS/InferenceContrib.cpp:79-86`、`dsinfer/plugins/singerproviders/diffsinger/DiffSingerProvider.cpp:123-127`）；
- L3 管线创建期（运行时）：wolf 语言 pipeline extension 的 `createPipeline` / `createLinguist` 仅做诊断兜底（越界 role 判 `InvalidArgument` 为现行既定行为），只产出可观察诊断，不构成新的加载期失败事由；诊断的通道形态由《语言域运行时 API 与宿主接入规范》（linguist-runtime-api-draft.md §5）管辖，本文不钉。

凡不在加载事务内的比对一律为编辑器运行期或打包期 lint 的警告级职责，框架与 wolf provider 均不据此判加载失败。此类比对共三项：

- 语言 `phonemes` 与各 stage 模型音素表的交集裁决（见 WolfLinguist Exports）；
- S2P `phonemes` 与语言链末端 `phonemes` 的对齐比对（方向：S2P 产出集 ⊆ 语言 `phonemes`，超出即告警）；
- Onset 对链上实际音素的覆盖比对。

另有两条边界口径：

- G2P `symbols` 可为音节等非音素原子符号，与音素类导出集异类，Level 1 不为其规定与任何音素集的跨键比对（其省略告警仍沿 G2P Exports 条款）；
- Onset 覆盖比对按下界口径执行——未列入 `knownPhonemes` 不等于未被规则覆盖，通配 `"*"` 覆盖者不得告警（见 Onset Exports）。

### 变体治理

变体名属实现方命名空间：

- 本契约族收录变体由 wolf 维护——
  - G2P 宿主面保留 `pipe-` / `algo-` 前缀；
  - G2PModel 变体按「家族-后端」复合命名（首个 `multig2p-onnx`）；
  - DictQuery 变体按词典族命名（首个 `dsdict`）；
  - 全部裸变体名为 wolf 官方保留（`nn-` 前缀已退役，见变体文档 D15/D16）；
  - 第三方使用反向域名变体（如 `com.vendor.myengine`）；
- 选择纪律：框架按 (`interface`, `level`, `variant`) 三元组全匹配选择 provider，`configuration` 不参与选择；wolf 保证其收录变体名单一承载，三元组命不中即「找不到提供者」加载失败；
- 收录变体清单、各变体 `configuration` 键汇与资源格式见变体文档；变体可随时新增，新增不触碰本契约。

机器可读形式：各 (interface, level) 契约的 `exports` 与 `imports[].options` JSON Schema 随契约发布物落位于 wolf 文档侧；Schema 落位前以本文表格为准。

### 锚点口径

- 本文所引 synthrt 框架锚点（`PackageLoader.cpp`、`SynthUnit.h/.cpp`、`ContribCategory.h`、`ContribPluginFactory.cpp` 等）均取 synthrt origin/main 新框架（观察点 `a060af0`；2026-09-05 复核 main HEAD 未漂移），语言契约以该框架为落地承载基线；
- 本分支（`language-level-1`）工作区不含该代码树——两线自 merge-base `304b275` 分叉，本分支仅有旧一代 G2P 栈（`include/synthrt/G2P/**`、`plugins/G2P/**`；狼仓 `D:/projects/wolf` 锚点就地随文另注，2026-09-05 复核仍为 `cecedba`、工作区干净）；
- spec 2.4 文本 main、本分支与工作区三侧同 blob（本分支 `8341c26` 同步了 main `3249a7c` 的文档变更；3249a7c 的框架代码属 main 侧，未并入本分支；2026-09-05 复核 blob `3d0c6568` 三侧一致）。

> 实现注记（非规范性），按话题分五点：
>
> 1. **白名单与 vars**：wolf 现行实现对语言声明文件执行字段白名单校验，未含 `vars`，故模块级字符串变量暂请写在 `desc.json`；按上位规范 JSON profile「框架定义的 object 未知字段不得拒绝」，该白名单行为不符合上位规范，待 wolf 修复收敛后本注记移除。
> 2. **类型化 IO 未落地**：G2P / S2P / Onset / G2PModel / DictQuery 的类型化运行时 IO 在 wolf 实现中均尚未落地，实际可观察行为以 wolf 发布物为准；`languageId` 命中校验（见下文组合规则）在现行实现中亦尚未执行，随类型化 IO 一并落地。
> 3. **exports.phonemes 形状校验已执行**：语言贡献 `exports.phonemes` 的加载期形状校验已由现行实现强制执行——规则全文见《WolfLinguist Exports》尾句，此处不重复。
> 4. **语言 Level 1「无 import `options` 词汇」属目标语义**：
>    - 现行实现 `LinguistImportOptions` 为空类型，provider 的 `createImportOptions` 只校验 options 为 JSON object 及目标三元组、不校验键汇（狼仓 `WolfLinguistProvider.cpp:268-283`）；
>    - `options` 整体省略时框架交付空 object（synthrt `PackageLoader.cpp:1358-1362`，`JsonValue options = JsonObject{};`），显式给出非 object（如 `null`）才被现行 provider 判 InvalidFormat；
>    - 即现行实现下 linguist 导入的 `options` 可省略、无词汇可携（携键因不校验键汇而被静默忽略）；上位规范「`options` 为可选字段」（spec 2.4:631-632）的可省略性已在 synthrt main 收敛。
> 5. **更名对齐与 IID 嵌入**：
>    - 类别名、契约接口名、trait ID、插件 IID 与 role 前缀均已按 wolf `c22dac8` 的 Language→Linguist 更名对齐，并复验于 `cecedba` 未漂移（狼仓 `LinguistContrib.h:14`、`LinguistApiL1.h:16/101`、`LinguistProviderPlugin.h:13`）；
>    - 自 wolf `cecedba` 起插件 IID 经 `stdc_add_plugin_metadata` 嵌入动态库、`plugin.json` 为根级 `name`+`interpreters` 的无 envelope 用户 metadata（IID 字面量在狼仓 `src/plugins/linguistproviders/CMakeLists.txt:3`、嵌入调用在 `src/plugins/linguistproviders/wolf/CMakeLists.txt:15`、`plugin.json` 为该 IID 的用户 metadata），与上位规范第 3 节同步。

## `linguist` 贡献类别

`linguist` 是模块贡献类别，由 wolf 注册：

- wolf 是被宿主（编辑器、命令行工具等）直接链接的库：库内静态注册对象在程序启动期（`main` 之前）把该类别挂入 synthrt 的类别注册表（狼仓 `LinguistContrib.cpp:94-95` 的 `ContribCategoryRegistry::Add`；注册表为 `stdc::StaticRegistry` 实例，见 synthrt `ContribCategory.h:158`），链接即完成该类别的进程内注册；
- 每个 `SynthUnit` 构造时收集注册表中的全部类别（synthrt `SynthUnit.h:27-28`，构造实现 `SynthUnit.cpp:14-31`），先于任何 Package 解析（spec 2.4:250 同此要求）；
- 注册内容包含类别名、条目解析器与模块类别必备的 provider factory（插件 IID `org.openvpi.wolf.plugin.LinguistProvider`）；
- 上位规范要求模块类别注册的有序插件搜索路径（spec 2.4:250）在实现中不随注册携带，由宿主在单元上按类别经 `SynthUnit::setPluginPaths` 配置（synthrt `SynthUnit.h:66-67`）；
- 宿主未链接 wolf 时该类别未注册，含本类别贡献的 Package 将被加载器整体拒绝（报「contribution category is not registered」，synthrt `PackageLoader.cpp:1210-1212`）。

### 贡献条目

```json
"contributions": {
    "linguist": [
        { "id": "cmn-pinyin", "path": "./linguists/cmn-pinyin/linguist.json" }
    ]
}
```

条目恰好包含 `id` 与 `path` 两个键。`path` 指向语言声明文件，约定 `linguists/<id>/linguist.json`。`id` 的形态见前置说明的 ID 形态约定。引用语言模块与引用其他模块同文法：`other-pkg:linguist/jpn-romaji`、`:linguist/cmn-pinyin`（当前包）。

### 推荐目录结构

```
+ linguists
  + cmn-pinyin
    - linguist.json     // 语言组合声明
    - phonemes.json     // exports.phonemes 所指文件（内联数组时可无）
    + g2p
      - inference.json  // G2P 模块声明（词典等资源就近放置）
    + s2p
      - inference.json  // S2P 模块声明（词典等资源就近放置）
    + onset
      - inference.json  // Onset 模块声明
```

## `org.openvpi.wolf.linguist.WolfLinguist`

语言组合。三元组固定为 (`org.openvpi.wolf.linguist.WolfLinguist`, 1, `wolf`)。本类别不追加类别字段；声明文件使用公共字段，`name` 为多语言文本，缺省等价于 `{"_": <id>}`。

### Exports

`exports` 必须提供 `phonemes`。

|   name   |         type          |                           description                            |       example       |
| :------: | :-------------------: | :--------------------------------------------------------------: | :-----------------: |
| phonemes | path \| array&lt;string&gt; | 本语言默认封闭链（自带 G2P + S2P）末端可能产出的内容音素全集^[1]^ | `"./phonemes.json"` |

[1] 保留音素（如 SP、AP、EP）不进清单。写为路径时指向内容为 `array<string>` 的 JSON 文件，元素须为非空且不重复的字符串。

`phonemes` 是语言链末端与消费方的对齐基准：

- 宿主据此核验实际组装链的 S2P 产物，模型侧对照各 stage 音素表；
- 语言包作者拥有默认链全部模块，故该全集可静态给出；
- 声库逐项覆写链路成员导致清单与实际产物不符的，属内容缺陷；是否拒绝由消费方决定，Level 1 不做加载期强制校验。

与各 stage 模型音素表的交集裁决口径：

- 各 stage 的「模型音素表」以推理模块 `onnx` 变体 `configuration.phonemes` 所指的「音素名↔ID 表」文件为准；
- 编辑器的正确校验方向是 **`phonemes` ⊆（各 stage 模型音素表的交集）**（语言产出的每个音素都必须能被声库所用各 stage 编码）；
- 交集裁决属编辑器运行期职责，加载事务不执行；
- 未提供模型音素表的 stage 不参与交集；全部 stage 均未提供时，编辑器跳过核验并给出提示，不得判加载失败。

该键的加载期形状校验（必须存在、元素非空且不重复、写为路径时所指 JSON 必须为数组）由 wolf provider 强制执行。

### Import options

无。语言 Level 1 不定义 import `options` 词汇。

### imports（组合规则）

语言的 `imports` 是本契约族唯一规范性的组合层，Level 1 内集合恒定：

|      role       |              目标契约               |  数量  |                    用途                     |
| :-------------: | :---------------------------------: | :----: | :-----------------------------------------: |
|  `linguist/g2p`  |   `org.openvpi.wolf.inference.G2P`  | 恰好 1 |               歌词 → 发音                   |
|  `linguist/s2p`  |   `org.openvpi.wolf.inference.S2P`  | 恰好 1 |             发音 → 音素序列                 |
| `linguist/onset` | `org.openvpi.wolf.inference.Onset`  |  0..1  | 音素 → onset 标记；缺省时由宿主合成全 `false` |
|  其他   |                不限                 |  任意  | Level 1 未定义用途；wolf 不参与校验，留作开放扩展 |

```json
"imports": [
    { "role": "linguist/g2p", "ref": ":inference/g2p", "options": { "languageId": "cmn-pinyin" } },
    { "role": "linguist/s2p", "ref": ":inference/s2p" },
    { "role": "linguist/onset", "ref": ":inference/onset" }
]
```

- 语言不感知 G2P 的内部组合（是否需要模型后端、几个后端，由 G2P 通过自己的 `imports` 设置；后端契约 `G2PModel` 与查询契约 `DictQuery` 不占用上表 role），语言层的基数表因此恒定不变；
- 语言与支持集合的匹配：G2P 提供 `exports.languages` 时，语言须恒等命中其中一项（落点见《自动组合校验的分层落点》：语言显式给出 `options.languageId` 时归 L1，由目标 provider 在 Ready pass 1 的 `createImportOptions` 校验该条 options；未显式给出时判定值为本语言贡献 ID（目标 provider 无法自该条 options 得知导入方身份），归 L2，由语言自身 provider 在图级校验中执行）：
    - G2P 导出的条目必须包括该语言的贡献 ID 全名（如语言 `cmn-pinyin-v2` 要求 G2P 导出 `cmn-pinyin-v2`）；
    - G2P 的实现细节不强制进入导出面：内部为 v2 实现的 G2P 仍可只导出 `cmn-pinyin`，内部差异由模块自身配置映射吸收（见 G2P 契约的 Import options）；要对接带自定义字段的语言贡献时，则须导出同名全名 ID；
    - 语言侧显式 `options.languageId` 时按同一规则判定，判定值即该选项值——上条贡献 ID 全名要求仅适用于未显式给出时的默认判定分支；目标未导出 `languages` 时不得提供本选项——双方导入导出不匹配，提供即语言模块加载失败；
- 目标 level 校验：上表只约束目标契约与 role，不约束 level 数值；被引用目标的三元组须命中某 provider 插件为其声明的 (`interface`, `level`, `variant`) 条目，被引用模块的运行时变量按其自身声明的 level 执行，本表基数与匹配规则不随目标 level 变化；
- 解释器在加载阶段只校验上述恒定基数与契约三元组；模块之间的连线由各自解释器沿模块自己的 `imports` 完成；
- 引用其他 Package 的模块时，谁 import 谁在 `desc.json` 的 `dependencies` 中声明。

> 实现注记（非规范性），按话题分七点：
>
> 1. **前缀化 role 既定**：`linguist/g2p`、`linguist/s2p`、`linguist/onset` 前缀化 role 名与 Singer 侧 `linguist/*` role 前缀均为 wolf 官方既定（更名与复验沿革见前置说明的实现注记，现行锚点 `WolfLinguistProvider.cpp:69-152`）；
> 2. **role family 前缀已获上位规范正式支持**：spec 2.4:629-644 将 `role` 文法定为多段 `segment *("/" segment)`，并建议以一个或多个前导 segment 表示 role family（如 `singer/acoustic`、`linguist/g2p`）；
> 3. **实现相容**：wolf 为可运行事实代码，其前缀 role 与该文法相容；synthrt main 加载器亦已按该文法受理多段 role（`isValidImportRole` 逐段校验，synthrt `PackageLoader.cpp:38-56`），本文与之对齐；
> 4. **类别名裸名格**：类别名 `linguist` 为 OpenVPI 官方类别，与 `inference`/`singer` 同格使用裸名，不属上位规范「第三方类别反向域名」条款（spec 2.4:248）的适用范围；
> 5. **基数校验现行实现只执行下界**（台账 D11）：`linguist/g2p`、`linguist/s2p` 缺失与目标 interface 不符均已被拒（狼仓 `WolfLinguistProvider.cpp:92-152`，wolf `cecedba` 实测）；
> 6. **上界未执行**（台账 D11）：多余同类 role（第二个 g2p/s2p、第二个 onset）不被拒绝、组合结果未定义，随语言契约落地收敛；
> 7. **同类缺口**：歌手侧 role 后缀=句柄校验（唯一映射约束的承载，沿革见《Singer 侧配套》注记与台账 D9、§8.12 D29）属同类缺口；G2P `exports.languages` 命中校验未执行情况见前置说明注记。

### Configuration for `wolf`

Level 1 不定义任何 `configuration` 键；`wolf` 变体要求其为空对象或省略，出现任何键即加载失败。语言固有的词典、规则与模型资源一律归于所导入模块的 `configuration`。

> 实现注记（非规范性）——现行实现下 `configuration` 必须显式写为空对象：
>
> - 省略时框架交付缺省 JSON 值（Null，synthrt main `PackageLoader.cpp:1316-1319`；stdcorelib 的 `JsonValue` 缺省即 Null——stdcorelib 为 vcpkg 端口、头文件行号随版本漂移，不作硬锚）；
> - provider 以非 object 判 InvalidFormat 拒绝（狼仓 `WolfLinguistProvider.cpp:308-319`）；
> - 即上文的「或省略」现行不可行，随 wolf 修复收敛；收敛前声明请显式写 `"configuration": {}`。

### Variables

无。语言不承载逐单元推理 IO。

> 实现注记（非规范性）：
>
> - 本类别为每条语言导入提供 Executive Factory（wolf 实现逐 import binding 创建，产物为 `LinguistExecutive`；狼仓 `WolfLinguistProvider.cpp:285-288`、`LinguistApiL1.h:51`）；
> - Level 1 不定义其可观察运行时行为，歌手侧的聚合与生命周期见《Singer 侧配套》。

## `org.openvpi.wolf.inference.G2P`

歌词文本 → 发音（grapheme-to-phoneme）。接口按批量词列表受理，本文按单元语义描述。

### Exports

`symbols` 与 `languages` 均可省略。省略 `symbols` 时可加载，但宿主无法对 G2P 产出与其语言链末端音素做静态比对，应设置警告；省略 `languages` 时导入方不得提供 `languageId` 选项。

|   name    |         type          |                       description                       |                 example                  |
| :-------: | :-------------------: | :-----------------------------------------------------: | :--------------------------------------: |
|  symbols  | path \| array&lt;string&gt; |         本模块可能输出的原子符号全集^[1]^         |            `"./symbols.json"`            |
| languages | path \| array&lt;string&gt; |          本模块支持的语言贡献 ID 集合^[2]^          | `["cmn-pinyin", "yue-jyutping"]` |

[1] 输出串含保留定界符（空格）时按本文的空格定界约定拆分后计入，无空格时整串即一个符号（音节等发音单元）；形式同 `phonemes`。

[2] 一律为语言贡献 ID（形态规则见前置说明，可带自定义字段），按全 ID 恒等匹配；常规实现导出基本形（如 `cmn-pinyin`），对接特定方案时导出自定义字段全名（如 `eng-arpabet-plus`）。

契约暴露面只允许规范语言 ID，实现内部的子资源标识（如模型后端家族的 `eng/default`）不出模块边界：暴露 ID 到内部子资源的映射由模块在自身 `configuration` 内声明并处理（见变体文档）。

### Import options

|    name    |  type  |                description                 |     example     |
| :--------: | :----: | :----------------------------------------: | :-------------: |
| languageId | string | 指定目标模块支持的语言贡献 ID，须恒等命中其 `exports.languages` | `"eng-arpabet"` |

仅当目标模块导出 `languages` 时可提供本选项；未导出时提供即双方导入导出不匹配，加载失败。内部子资源标识（如后端模型家族内部的 langRef `eng/default`）不进入契约词，模块收到 `languageId` 后自行映射（见变体文档）。`languageId` 选项只在导入期起绑定/选定作用，不构成运行时通道；运行时的语言值走 Variables 的 `languageId`。

### Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### Variables

#### Level 1 能力边界

输入是有序的词列表。句级语境参与的多音字消歧 / 同形异读属后续 Level 演进（部分 G2P 变体已实现，但不做能力承诺）。分界判据：解释器在多轮调用间维护会话（滚动上下文、修正窗重发）属实现自由——只要逐单元调用形状与本节词汇表、定界约定不变，即不构成 Level 递增事由；一旦能力需要导入方或宿主可见（如声明「按句喂词可获益」）或改变输入单元的语义，即按上位规范《何时递增 Level》处理。任务与状态生命周期由运行时 API 规范管辖。

#### 输入合法性判定次序

（依序判定、先到先生效，均未命中者正常受理）

1. `lyric` 为空串或仅含空白字符 → 该词按 `mode=skip` 产出；
2. 其余含空白字符（词内或首尾空白）→ `pronunciation` 写入原词透传，并携带非空 `error` 显式上报（值如 `InvalidInput`；此时 `mode` 与 `candidates` 之值无定义）。

#### 词汇表

|   variable    |  I/O   |       type       |                   description                    |   activation condition    |
| :-----------: | :----: | :--------------: | :----------------------------------------------: | :-----------------------: |
|     lyric     | input  |      string      |            歌词单元（一个词/一个分片）            |             -             |
|  languageId   | input  |      string      |       语言贡献 ID（同 Import options 规则）       | `"languages" in exports`  |
| pronunciation | output |      string      |                  主发音^[1]^                    |             -             |
|  candidates   | output | array&lt;string&gt; |            候选发音（多音），首个即主发音            |             -             |
|     mode      | output |       enum       | 结果来源（`convert` 转换 / `copy` 原词保留 / `skip` 空词跳过） |             -             |
|     error     | output |       enum       |    失败类型，值域见下^[2]^    |             -             |
|   hitSource   | output |       enum       | 结果来源标注（`dict` 词典命中 / `model` 模型推理 / `rule` 规则推导 / `fallback` 兜底）^[3]^ |      `mode=convert`      |

[1] 空格为发音层保留定界符——含空格即视该串为音素序列，无空格即待转换的发音；此定界约定使 Pronunciation 层无需区分两种变量类型，输出与输入同形对齐；发音串不携带数值标注（如置信度）。

[2] 非空即失败，词序位保留且 `mode`、`candidates` 之值无定义；成功时必须取空值——词级成败一律以本变量为准，失败词的处置（如原词保留）由宿主决定。

[3] 可选诊断词，与 `error` 值域同属本契约词汇：模块无法区分来源或 `mode` 非 `convert` 时省略，宿主不得依赖其存在；变体只可援引上述取值、不得自定义新值。`rule` 涵盖规则算法类变体与编排链规则步的推导产出。`mode=copy`（打标类原样保留）不是转换来源，`hitSource` 恒省略；`fallback` 专指 `mode=convert` 的词经兜底步产出（含显式 `useOriginal` 的原词兜底——显式兜底≠`mode=copy`）。

#### error 值域

`error` 的值域是契约词，与 `mode` 同属 (`interface`, `level`) 词汇。Level 1 固定六个取值：

- `InvalidInput`（输入非法）；
- `ModelInferenceFailed`（后端推理失败）；
- `PhonemeGenerationFailed`（转换/兜底仍未产生发音）；
- `DriverUnavailable`（推理驱动不可用）；
- `NotInitialized`（模块未初始化）；
- `UnknownError`（其他未分类失败）。

取值与现行实现 `G2pErrorType` 逐值同名（实现注记，非规范性：所指为 synthrt `language-level-1` 分支旧栈 `include/synthrt/G2P/Base/LangCommon.h:61-69`；`mode` 三值 `convert`/`copy`/`skip` 同见 :29-31；wolf 推理契约头 `include/wolf/Api/Inferences/` 目前仅含接口与 level 常量，类型化 IO 尚未落地）。

变体只可援引上述取值、不得自定义新值；取值的增补属契约词汇表演进，按上位规范的 Level 判据处理。各取值的具体触发条件见变体文档。

#### 运行时路径

调用方以 iso 格式的语言句柄传入（如 `cmn`），宿主按歌手侧语言映射解析为语言贡献 ID 后送达 G2P。语言侧在 `imports` 中显式给出 `options.languageId` 时，该绑定值即本导入唯一选定语言，依序判定：运行时 `languageId` 未传入 → 回落绑定值、正常受理；传入且与绑定值恒等 → 正常受理；传入但与绑定值不一致（含入参无法解析的情形）→ 该词以 `error`=`InvalidInput` 失败产出。无绑定值时的回退（如后端的缺省语言机制）由变体文档规定。

#### 输出共现约束

（适用于 `error` 为空的产出；`error` 非空时以注 2 为准）

- `mode=skip` 时 `pronunciation` 与 `candidates` 均为空；
- `mode=copy` 时 `pronunciation` 为原词、不得携带 `candidates`；
- `mode=convert` 时 `pronunciation` 即 `candidates` 首个元素。

#### 关联契约

模型后端能力（张量模型驱动的「词 → 发音」）由 `org.openvpi.wolf.inference.G2PModel` 契约承载，词典键值查询由 `org.openvpi.wolf.inference.DictQuery` 契约承载；两者只被编排变体经 imports 消费（DictQuery 还可被宿主工具直接消费），语言的 `linguist/g2p` role 只认本契约——backend-only 由此钉死的目标契约自动成立（第四轮拓扑决策，见变体文档 D15-D18）。

## `org.openvpi.wolf.inference.G2PModel`

模型后端：待转换词批 → 发音与候选。只经 imports 被编排变体（当前为 pipe-chain）消费，不承担宿主面编排语义（`mode`/`copy`/`skip` 归编排变体）；语言不可直引。接口按批量词列表受理，本文按单元语义描述。

### Exports

`symbols` 与 `languages` 均可省略，语义、省略后果与暴露面边界（内部子资源标识如家族内 langRef `eng/default` 不出模块边界、映射由变体 `configuration` 承担）同 G2P 契约同名键。

|   name    |         type          |                       description                       |           example           |
| :-------: | :-------------------: | :-----------------------------------------------------: | :-------------------------: |
|  symbols  | path \| array&lt;string&gt; |         本模块可能输出的原子符号全集         |      `"./symbols.json"`      |
| languages | path \| array&lt;string&gt; |          本模块支持的语言贡献 ID 集合         | `["cmn-pinyin", "eng-arpabet"]` |

### Import options

|    name    |  type  |                       description                        |     example     |
| :--------: | :----: | :----------------------------------------------------: | :-------------: |
| languageId | string | 指定目标模块支持的语言贡献 ID，须恒等命中其 `exports.languages`；编排变体以此把后端绑定为单一语言 | `"eng-arpabet"` |

规则与后果同 G2P Import options（目标未导出 `languages` 时提供即双方导入导出不匹配，加载失败）。

### Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### Variables

#### Level 1 能力边界

输入是有序的词列表；无句级语境承诺（口径同 G2P）；**不携带编排语义**——空词、原词保留、词典候选合并等策略全部由编排变体负责，后端对坏输入一律显式失败。语言选定经绑定期 `options.languageId` 或变体缺省完成，属「单绑定单语言」。

#### 输入合法性判定

（依序判定、先到先生效，均未命中者正常受理）

1. `word` 为空串或仅含空白字符 → 该词以 `error`=`InvalidInput` 产出；
2. 其余含空白字符（词内或首尾空白）→ 同样以 `error`=`InvalidInput` 产出。

两种情形下 `pronunciation` 与 `candidates` 之值均无定义（后端无 skip/copy：坏输入显式失败，策略决定权归编排器）。

#### 词汇表

|   variable    |  I/O   |       type       |                       description                        |   activation condition    |
| :-----------: | :----: | :--------------: | :------------------------------------------------------: | :-----------------------: |
|     word      | input  |      string      |           待转换的词（编排变体已完成打标与清洗）            |             -             |
|  languageId   | input  |      string      |           语言贡献 ID（同 Import options 规则）           | `"languages" in exports`  |
| pronunciation | output |      string      |      主发音（空格定界约定同 G2P 契约注 1，输出输入同形）     |             -             |
|  candidates   | output | array&lt;string&gt; | 候选发音，首个即主发音（模型 topK 等真实候选经此变量上链） |             -             |
|     error     | output |       enum       |                  失败类型，值域见下                      |             -             |

#### error 值域

`error` 的值域是契约词。Level 1 固定七值：

- `InvalidInput`（输入词非法）；
- `LanguageUnsupported`（缺省级联映射查无——语言 ID 不受支持；运行时传入与绑定值不一致另按 `InvalidInput` 判，见《运行时路径》）；
- `ModelInferenceFailed`（后端推理失败）；
- `PhonemeGenerationFailed`（推理完成但未能产出可用发音）；
- `DriverUnavailable`（推理驱动不可用）；
- `NotInitialized`（模块未初始化）；
- `UnknownError`（其他未分类失败）。

后五值与 G2P 契约同名者语义同款。各取值的具体触发条件见变体文档；变体只可援引、不得自定义新值；取值增补属契约词汇表演进，按上位规范 Level 判据处理。

#### 运行时路径

与 G2P 契约同款绑定判定——运行时 `languageId` 未传入 → 回落绑定期 `options.languageId`；传入且与绑定值恒等 → 正常受理；传入但与绑定值不一致（含入参无法解析的情形）→ 该词以 `error`=`InvalidInput` 失败产出。绑定值亦无时的变体缺省级联（如 `default_language`/`languageMap`）与级联查无映射时判 `LanguageUnsupported` 的细则见变体文档。

#### 输出共现约束

（适用于 `error` 为空的产出；`error` 非空时以输入合法性判定为准）`candidates` 非空，且 `pronunciation` 即 `candidates` 首个元素。

## `org.openvpi.wolf.inference.DictQuery`

词典键 → 值批量查询。消费方为宿主工具（如多音字选择界面）与编排变体；不参与语言组合、不占用语言的 `linguist/*` 名额。接口按批量键受理，本文按单元语义描述。

### Exports

|     name      |         type         |                        description                        |   example   |
| :-----------: | :------------------: | :-------------------------------------------------------: | :---------: |
| dictionaries | array&lt;string&gt; | 可选。本模块收录的词典 ID 集合；省略时导入方不得提供 `dictId` 选项 | `["official"]` |

### Import options

|  name  |  type  |                         description                         |   example   |
| :----: | :----: | :--------------------------------------------------------: | :---------: |
| dictId | string | 绑定期选定缺省词典（运行时未传 `dictId` 时回落此值），须恒等命中其 `exports.dictionaries`；未导出 `dictionaries` 时提供即加载失败 | `"official"` |

### Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### Variables

| variable |  I/O   |       type       |                      description                       |                activation condition                 |
| :------: | :----: | :--------------: | :----------------------------------------------------: | :-------------------------------------------------: |
|   key    | input  |      string      |                     查询键（如词）                      |                          -                          |
|  dictId  | input  |      string      |               目标词典 ID（恒等匹配一项）                | 模块收录多于一部词典（已有绑定期缺省时可省，回落绑定值） |
|  values  | output | array&lt;string&gt; |        命中值列表（候选序按所收词典的声明合并序）         |                          -                          |
|  found   | output |     boolean      |                        是否命中                         |                          -                          |

Level 1 不定义错误通道（口径同 S2P/Onset）：空 `key` 与未命中均以 `found=false`、`values` 为空产出——未命中不是失败；`dictId` 查无收录项时同样以 `found=false` 产出。按语言过滤词典等需求属后续 Level 演进。本接口逐调用 IO 无跨键、跨调用语境。

## `org.openvpi.wolf.inference.S2P`

发音字符串 → 音素序列（symbol-to-phoneme）。输入的多形态已由 G2P 的空格定界约定消除；预音素化用途（宿主已有音素序列、仅需切分/映射的输入）应绕开 G2P、直调本接口。

### Exports

`phonemes` 可省略。省略时可加载，但宿主无法对本模块产出与语言链末端音素做静态比对，应设置警告（口径同 G2P Exports 的省略条款）。

|   name    |         type          |                   description                    |       example       |
| :-------: | :-------------------: | :----------------------------------------------: | :-----------------: |
| phonemes | path \| array&lt;string&gt; | 本模块可能产出的音素全集（**产出集**） | `"./phonemes.json"` |

形态规则同 WolfLinguist Exports `phonemes`（保留音素不进清单；写为路径时指向 `array<string>` JSON，元素非空且不重复）——与彼处不同，本键可省略（省略后果见上）。其余口径分两条：

- 各变体的推导口径见变体文档 §6：`dict` / `mapping` 可由 `configuration` 资源静态推导（`mapping` 的未列入项原样透传，导出为「目标列 ∪ 透传域」的上界集），`direct` 开放无界、`lua` 不可静态推导——这两类由作者在声明中显式补齐或省略。
- **本键只服务于宿主工具的静态比对与告警：一切比对结果均为警告级，不构成加载期失败事由**；比对方向为 S2P 产出集 ⊆ 语言链末端 `phonemes`（超出即告警）；加载事务不执行包含/交集裁决（分工口径同 WolfLinguist Exports 的 `phonemes` 对齐基准）。

### Import options

无。

### Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### Variables

|   variable    |  I/O   |       type       |                description                 | activation condition |
| :-----------: | :----: | :--------------: | :----------------------------------------: | :------------------: |
| pronunciation | input  |      string      |        发音字符串（G2P 的主发音）         |          -           |
|   phonemes    | output | array&lt;string&gt; | 音素序列（未命中时的产出语义见各变体文档） |          -           |

`pronunciation` 沿用发音层空格定界约定（见 G2P Variables 注[1]）：含空格即空格定界的待转换音素序列（逐段转换），无空格即单一待转换发音。Level 1 不定义 error/失败通道，未命中与失败的可观察语义由变体文档承接（见契约族变体文档 §6）。本接口逐调用 IO 无跨词、跨调用语境；会话内上下文（如联诵缓存）属变体实现自由，不进入 Level 1 词汇。

## `org.openvpi.wolf.inference.Onset`

音素序列 → onset 位置标记。

### Exports

`knownPhonemes` 可省略。省略时可加载，宿主无法进行覆盖比对，应设置警告（口径同 G2P `symbols` / S2P `phonemes` 的省略条款）。

|       name       |         type          |                     description                      |         example          |
| :--------------: | :-------------------: | :--------------------------------------------------: | :----------------------: |
| knownPhonemes | path \| array&lt;string&gt; | 本模块可识别并参与匹配的音素集（**输入识别集**） | `"./known-phonemes.json"` |

形态与形状规则同 S2P `phonemes`（本键同样可省略）。其余口径分三条：

- **注意方向**：本键是**识别集**，与 S2P `phonemes` 的**产出集**不同义——比对逻辑不得混用两键（链上的合理关系是「链上实际音素均被本模块规则覆盖」，而非 `knownPhonemes` 与 `phonemes` 之间的包含式）。
- 各变体推导口径见变体文档 §7：`rule` 变体取 `phonemeTypes` 中类型名被至少一条规则引用的键集，并并入 pattern 字面音素段；通配 `"*"` 段功能上覆盖任意输入，故派生集是覆盖面的**下界**而非全集（未列入音素仍可能被良定义处理，识别集低估不构成缺陷）；`lua` 不可静态推导，由作者声明补齐或省略。
- 输入中未被覆盖的位置输出 `false` 是本契约的目标合法语义（见 Variables），**一切比对结果均为警告级，不构成加载期失败事由**。

### Import options

无。

### Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### Variables

| variable |  I/O   |        type        |         description         | activation condition |
| :------: | :----: | :----------------: | :-------------------------: | :------------------: |
| phonemes | input  | array&lt;string&gt; |   音素序列（S2P 的输出）    |          -           |
|  onsets  | output | array&lt;boolean&gt; |  与输入等长的 onset 标记   |          -           |

Level 1 不定义错误通道；输入形状与合法性校验语义由变体文档承接（见契约族变体文档 §7）。本接口逐调用 IO 无跨词、跨调用语境；会话内上下文（如联诵缓存）属变体实现自由，不进入 Level 1 词汇。

## Singer 侧配套

歌手用 `imports` 声明支持的每种语言；语言导入条目的 `role` 以 `linguist/` 为前缀、后缀必须为所导语言贡献的**语言句柄**（如 `linguist/cmn` 指向句柄为 `cmn` 的语言贡献 `cmn-pinyin`）——role 在歌手声明内唯一（框架既定），后缀=句柄使 role 本身即「语言句柄 → 语言导入」的索引；语言 Level 1 无 import `options` 词汇：

```json
"imports": [
    { "role": "singer/acoustic", "ref": ":inference/acoustic" },
    { "role": "linguist/cmn", "ref": ":linguist/cmn-pinyin" },
    { "role": "linguist/jpn", "ref": "wolf/lang-jpn:linguist/jpn-romaji" }
]
```

其他规则：

- 语言句柄（定义见前置说明《语言贡献 ID 形态约定》）：上游 stage 的 `configuration.languages`（指向「语言句柄 → 模块内部语言 ID 对应表」JSON 文件的路径，对应表的键为语言句柄）与宿主展示一律使用语言句柄（`configuration.languages` 键归 dsinfer 侧声库 stage 变体管辖，非本契约族词汇）；
- **唯一映射约束**：同一声库中每个语言同时只支持一种注音体系——由「role 在模块内唯一（框架既定）+ 后缀必须为语言句柄」结构性成立，句柄互不相同是其等价推论；加载期校验（role 后缀=目标贡献 ID 首字段）属 Singer 契约义务（由歌手变体解释器或语言 provider 在加载事务的 Ready 校验中执行；现行实现尚未落该校验——狼仓 validator 仅校验语言导入目标三元组、role 前缀与 executiveFactory 存在性，`WolfLinguistProvider.cpp:127-150`；沿革见变体文档 §8.4 D9、§8.12 D29）；
- 支持语言清单经语言导入表达（目标设计；现行实现中歌手的 `languages` 仍居 `configuration`，随迁移收敛）；
- **缺省语言**：经 Singer 声明的 `defaultLanguage` 扩展键表达（iso 语言句柄；键的归属与采纳状态归 Singer 契约《贡献条目与类别追加字段》管辖，非本契约词汇）——宿主「跟随歌手」语义经 role `linguist/<defaultLanguage>` 命中语言导入解析；该键未给出或不命中时由宿主回退（Singer 契约建议取声明序第一个句柄）。现行实现中 `defaultLanguage` 仍居声库 `configuration`，随迁移收敛。

### 运行时聚合（语言 pipeline extension）

wolf 为含语言导入（目标类别为 `linguist` 且 role 以 `linguist/` 为前缀的 import）的 Singer spec 注册 pipeline extension，trait ID 为 `org.openvpi.wolf.extension.LinguistPipeline`：

- Package 加载 Commit 后，宿主按该 trait ID 自 Singer spec 直接取用（加载期的语言导入校验（结构文法、三元组与 provider 解析）已由 synthrt 加载事务统一完成，见上位规范《加载》）；
- extension 的角色集合即该歌手语言导入的 `role` 集合，Package 加载后不再变化；
- 运行时经 extension 创建聚合各语言导入的 pipeline，并按歌手本地 `role` 创建对应语言的执行体——`role` 不属于该歌手的语言导入集合即报错；父子生命周期由 synthrt 的 Executive 监督树承载；
- pipeline extension 是 synthrt 运行时模型的通用扩展点（其他贡献类别注册后亦可为 Singer 挂载新的 pipeline），其 C++ 级 API 形状（extension 取用、executive 方法面、任务异步纪律、诊断通道、缺依赖降级模型）由《语言域运行时 API 与宿主接入规范》（linguist-runtime-api-draft.md）定义，本文不钉。

> 实现注记（非规范性），分三点：
>
> - **挂载判定与注册**：extension 的挂载判定为双条件——import 目标类别为 `linguist` 且 role 带 `linguist/` 前缀（狼仓 `WolfLinguistProvider.cpp:249-266`）；trait ID 经 `ContribSpecExtensionTraits<SingerSpec, WolfPipelineExecutive>` 特化注册（狼仓 `LinguistApiL1.h:99-102`）；
> - **越界报错**：越界 `role` 在 `createLinguist` 报 `InvalidArgument`（狼仓 `WolfPipelineExecutive.cpp:22-26`）；
> - **现行空实现**：语言导入 binding 的 activate/close 现行为空实现、`LinguistExecutive` 尚未持有聚合的推理执行体（狼仓 `LinguistProvider.cpp:20-28`）——上文的运行时聚合属发布级目标语义，现行可观察行为以 wolf 发布物为准。

## 公共语言包

- 公共语言包由 **wolf 项目发布**（经 wolf 项目名下的 g2p 资源仓 release 发布，渠道、清单格式与编辑器更新义务见
  [linguist-g2p-package-distribution-draft.md](linguist-g2p-package-distribution-draft.md)），编辑器负责内置随发行；安装位置不限；
- 公共语言包由 wolf 统一发布维护、规范性最强；宿主（编辑器/工具）配置包搜索路径时，**公共语言包所在路径必须排在声库内置包路径之前**——这是对宿主部署的约定：按上位规范「路径优先、同身份首次出现即遮蔽」，先出现来源始终赢得候选选择；
- **版本纪律**（规范性；机制细节见发布设计文档）：
    - 声库在 `dependencies` 中只写**目标版本**（实际依赖的最低语义版本），
      不要求精确版本——能否兼容由公共包的 `[compatVersion, version]` 区间
      承诺判定（上位规范《版本》《依赖项》）；
    - 公共包维护 `compatVersion`：非破坏性内容更新只抬 `version`；触发
      上位规范《版本》兼容承诺清单所列公开表面变更的破坏性更新，必须把
      `compatVersion` 抬至破坏起点；
    - 公共包内部资源格式版本的抬升（变体文档规定「解释器按版本一并校验」
      的格式，如 chain `formatVersion`、multig2p `bundle_version`）视同破坏
      性更新——新格式须在依赖求解阶段即排除旧目标版本的命中，不依赖解释
      器期拒绝兜底；
    - 编辑器更新公共包采用**并存安装**：只添加新版本、不删除既有版本（上位规范允许多版本共存），被破坏性更新淘汰且仍被在役声库依赖的旧版本必须保留。
- 公共包是普通 Package，安装、解包安全、依赖求解全按上位规范，`desc.json` 必须显式声明 `runtimeLevel`（当前为 1）；
- **已发布包目与支持语言列表由 wolf 维护并随版本更新，以 wolf 文档为准，本文不列举**；
- 包 ID 一律为 `wolf/lang-<iso-639-3>`（如 `wolf/lang-cmn`），声库书写 `dependencies` 时按此命名；
- 一个公共包可包含同一语言多种注音体系的多张语言贡献（如 `cmn-pinyin` 与 `cmn-bopomofo` 同处 `wolf/lang-cmn`），歌手按唯一映射约束择一导入；
- 每个公共语言包是完整语言闭包：
    - `contributions` 同时含 `linguist` 与 `inference`（本语言的 G2P、默认 S2P、默认 Onset），其语言声明按需导入本包内模块与其他公共包模块；
    - G2P 如需模型后端，由 G2P 模块自己的 `imports` 引用共享后端包，语言声明不携带后端。
- **生态 ID 对照**（信息性），分三条：
    - lm / dspkg 侧流通的既有 g2p ID 为 `cmn-pinyin` / `jpn-romaji` / `yue-jyutping` / `eng-cmu`（lm `README.md:14-20` 命名表、生成点 dspkg `dspkg-core/src/converter/singer.rs:64-120`）；
    - 公共包贡献 ID 以 wolf 发布物为准：与生态既有 ID 重合者沿用；分歧者（如 `eng-cmu` 与本表示例 `eng-arpabet`）由 wolf 定夺并在发布注记显式说明，避免双名并存漂移（历史反例：lm 内部 README 示例 `cmn-pinyin` 与实际 moduleId `g2p-cmn-official` 并存漂移，`README.md:44-47` vs `res/G2pPackages/Phonetic-Suite-Cmn/package.json:9`）；
    - 本文示例 ID（含 `eng-arpabet`）仅示范形态约定、不构成公共包内容承诺；公共包若采用生态分歧名（如 `eng-cmu`），不触发本文示例改写，差异由 wolf 发布注记显式说明；
    - 公共包是否、何时收录对接第三方语言贡献自定义字段全名（如 `eng-arpabet-plus`）的 G2P 方案，同由 wolf 发布注记定夺，本文不预设。

声库的两种用法：

1. **整体引用**（推荐、绝大多数声库）：`dependencies` 声明依赖公共语言包，歌手直接 `imports` 公共包中的语言贡献（如 `wolf/lang-cmn` 中的 `linguist/cmn-pinyin`）。
2. **逐项覆写**（需要自定义的声库）：在本包写该语言的贡献，把公共模块（如公共 G2P）与本包私有模块（如自定义 S2P 词典）混装进自己的 `imports`；`dependencies` 按实际引用声明。声库内私有 G2P 即本包普通的 `inference` 贡献，不再有任何特殊机制。想替换公共 G2P 的资源或后端接线的声库，整份覆写该 G2P 模块的资源。
