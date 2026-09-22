# wolf 语言域契约（L1 + L2）

本文钉**语言域身份层（L1）**与**语言组合契约层（L2）**：`linguist` 贡献类别、语言身份、
`WolfLinguist` Level 1 契约，以及歌手侧的语言映射。

上位规范 [spec 2.4](ds-spec-2.4.md)，分层与跨层不变量见
[linguist-architecture.md](linguist-architecture.md)，冲突时按索引所定的可信源顺序裁决。
链推理契约（G2P / S2P / Onset）见 [linguist-inference-contract.md](linguist-inference-contract.md)；
运行时与宿主接入见 [linguist-runtime.md](linguist-runtime.md)；`configuration` 键汇与资源格式
归 [linguist-variants.md](linguist-variants.md)。

**发布状态**：Level 1 尚未对外发布。稳定前的原地修订不构成 spec 2.4:594
「契约发布后不得作不兼容修改」意义上的发布后修改。

## 0. 词汇与通用约定

### 0.1 四类内容

| 词 | 含义 | 由谁规定 |
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

### 0.3 关于 `vars`

模块声明可按 spec 2.4:82-106 使用 `vars`。框架在把声明交给类别与解释器**之前**已完成展开
并从声明对象中剥离该字段（`synthrt/lib/Core/PackageLoader.cpp:406`），因此语言域的任何
字段白名单都不会遇到它，本层无需为其作出规定。

---

# L1 —— 语言域身份层

本层与具体契约无关：`linguist` 类别下无论承载哪个 `interface`，本层规则一律适用。
本层的全部产物在 **DataOnly** 模式下即可用——宿主在不加载运行时的前提下即可列出
已安装的语言。

## 1. `linguist` 贡献类别

`linguist` 是**模块贡献类别**，由 wolf 注册：

- wolf 是被宿主直接链接的库。库内静态注册对象在 `main` 之前把该类别挂入 synthrt 的类别
  注册表（`src/lib/Linguist/LinguistContrib.cpp:94-95`；注册表为 `stdc::StaticRegistry`，
  见 `synthrt/include/synthrt/Core/ContribCategory.h:158`），**链接即完成进程内注册**；
- 每个 `SynthUnit` 构造时收集注册表中的全部类别（`synthrt/lib/Core/SynthUnit.cpp:14-31`），
  先于任何 Package 解析（spec 2.4:250 同此要求）；
- 注册内容包含类别名、条目解析器与模块类别必备的 provider factory，插件 IID 为
  `org.openvpi.wolf.plugin.LinguistProvider`；
- 上位规范要求的**有序插件搜索路径**（spec 2.4:250）不随注册携带，由宿主按类别经
  `SynthUnit::setPluginPaths` 配置（`synthrt/include/synthrt/Core/SynthUnit.h:66-67`）；
- 宿主未链接 wolf 时该类别未注册，含本类别贡献的 Package 被加载器整体拒绝
  （`PackageLoader.cpp:1210-1212`）。

类别名 `linguist` 为 OpenVPI 官方类别，与 `inference` / `singer` 同格使用裸名，不属
spec 2.4:248「第三方类别推荐反向域名」的适用范围。

### 1.1 贡献条目

```json
"contributions": {
    "linguist": [
        { "id": "cmn-pinyin", "path": "./linguists/cmn-pinyin/linguist.json" }
    ]
}
```

条目恰好包含 `id` 与 `path` 两个键。条目 schema 归类别所有（spec 2.4:230「其余字段由该类别
自行规定」、:250「注册该类别的条目解析器或 schema」），故拒绝未知键合法。

`path` 指向语言声明文件，约定 `linguists/<id>/linguist.json`。引用语言模块与引用其他模块
同文法：`other-pkg:linguist/jpn-romaji`、`:linguist/cmn-pinyin`（当前包）。

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
语言组合声明与其直属资源。同包多语言时以模块 ID 前缀区分推理模块目录。

> 本布局是推荐，不是规范。相对路径的基准恒为**声明文件自身所在目录**，与目录如何组织无关。

## 2. 语言身份

语言身份由声明根的两个**类别追加字段**承载（spec 2.4:529-535 的第二层：由贡献类别规定、
对该类别下所有模块生效、由类别自己解析）。同层先例为 `singer` 的
`avatar` / `background` / `demoAudio`（`synthrt/lib/SVS/SingerContrib.cpp:33-36`）。

| 字段 | 类型 | 必选 | 说明 |
| :-- | :-- | :-- | :-- |
| `language` | string | 是 | **语言句柄**：ISO 639-3 代码，形如 `[a-z]{3}`（含 `qaa`-`qtz` 私用区） |
| `scheme` | string | 是 | **注音体系**：`[a-z0-9]+(-[a-z0-9]+)*` |

### 2.1 `scheme` 的语义

`scheme` 标识**发音层的记法体系**，是本契约族的互换契约：

> 在**同一 `language` 下**，两个模块声明同一 `scheme`，即承诺其 `pronunciation` 串可互相消费。

**`scheme` 由 `language` 定域**——匹配键恒为二元组 `(language, scheme)`，不是 `scheme` 单格。
因此 `(cmn, pinyin)` 与 `(deu, pinyin)` 是两个互不相干的二元组，同名不构成任何互换承诺；反过来，
同一个 `scheme` 名可以在不同语言下各自表示该语言的记法，不必全局唯一。

它是 G2P / S2P 与语言组合之间唯一的匹配键（见 §5.3），因此不是装饰性字段。官方体系名由
wolf 维护清单（治理同变体名，见 §7）；第三方使用反向域名或厂商前缀，避免撞名后语义分歧。

### 2.2 贡献 ID 语法

```
id = <language> "-" <scheme> [ "-" <qualifier> ]
```

- `qualifier` **无语义**，不参与任何匹配。其存在意义是让同一二元组的多张贡献（变种、
  不同作者、精简版）在同包内并存；
- ID 的字符集由框架兜底：贡献 ID 必须是合法 segment `[A-Za-z0-9_-]+`
  （`synthrt/lib/Core/ContribLocator.cpp:43-53`），区分大小写。

**该规则是书写惯例，不是加载期条件。** 加载器与本契约族的任何匹配都不读 ID；不合本形式的
ID 只在**打包期 lint** 告警，不影响加载。lint 的判据是前缀比对而非解析：`id == language +
"-" + scheme`，或 `id` 以 `language + "-" + scheme + "-"` 开头且余部非空。由此：

- **`scheme` 内部允许连字符不产生歧义**——`cmn-pinyin-lite` 在
  (`scheme=pinyin`, `qualifier=lite`) 与 (`scheme=pinyin-lite`, 无 qualifier) 两种声明下
  都合法且各自自洽，因为没有任何一方从 ID 反推语义；
- **ID 是冗余副本，声明字段是真值**。本契约族的任何匹配、绑定、路由都不读 ID。

> 之所以不作加载期硬校验：ID 的前缀不携带任何信息量（真值在 `language` / `scheme` 两字段里），
> 硬校验只会取消 spec 2.4:506-508 明确给 Package 的命名自由——「同一份模块目录被两个 Package
> 收录时两边可以各自命名」。

### 2.3 加载期校验（Probe）

由 `LinguistCategory::createSpec` 执行，失败即整次加载失败：

1. 条目仅含 `id` 与 `path`；
2. `language` 存在、为 string、匹配 `[a-z]{3}`；
3. `scheme` 存在、为 string、匹配 `[a-z0-9]+(-[a-z0-9]+)*`。

**ID 形态不在此校验**（§2.2）——它是打包期 lint 项。

本层**不**校验 `exports`、`configuration` 与 `imports` 的内容——那是 L2 与解释器的职责。

### 2.4 DataOnly 身份面

`LinguistSpec` 暴露 `language()`、`scheme()` 与 `locator().contributionId()`。三者均只依赖
Probe 期解析结果，**在 `DataOnly` 模式下同样可用**（`DataOnly` 只停在 typed manifest 构造
之后，不触碰 provider 与运行时）。宿主的语言清单、缺依赖预检与安装引导据此实现，不需要
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
| `openSet` | boolean | 否 | 缺省 `false`。为真即「产出可能落在清单外」 | `true` |

- 保留音素（如 `SP` / `AP` / `EP`）不进清单；
- 写为路径时指向内容为 `array<string>` 的 JSON 文件；元素须为非空且互不重复的字符串。

**加载期形状校验**（Acquire，由 wolf provider 执行，失败即整次加载失败）：`phonemes` 必须存在；
写为路径时所指 JSON 必须为数组；元素非空且不重复。`openSet` 若出现须为布尔——**拼错的值不按
`false` 处理**，因为两者中 `false` 是更危险的那个答案。

### 4.0 `openSet`：清单不是承诺时说出来

`phonemes` 必填，而**十二条既有链的末步都是「查不到即输出原词」**——此后产出即用户输入的任意
文本，任何清单都不可能是全集。在此位之前，这些语言只有两条路：留空（等于什么都没说），或写一个
不真的集合。二者都不是「必填」这个词该有的意思。

| `openSet` | 清单的含义 | 宿主可据此做什么 |
| :-- | :-- | :-- |
| `false`（缺省） | **全集**。语义与本位存在之前完全一致，既有声明无需改动 | 静态比对是权威的：清单外即缺陷 |
| `true` | **已知部分** | 静态比对不完整；可以说出「本语言可能产出你的声库不认识的音素」 |

**由打包 lint 自动推导，不靠作者自觉**：链带 `useOriginal` 的 `fallback` 步即为真
（`scripts/check-declarations.py`）。作者记得写不是机制。

### 4.1 对齐基准

`phonemes` 是语言链末端与消费方的对齐基准：宿主据此核验实际组装链的 S2P 产物，模型侧对照
各 stage 音素表。语言包作者拥有默认链全部模块，故该全集可静态给出。声库逐项覆写链路成员
导致清单与实际产物不符的，属内容缺陷；是否拒绝由消费方决定，**Level 1 不做加载期强制校验**。

与各 stage 模型音素表的交集裁决口径：

- 各 stage 的「模型音素表」以推理模块 `onnx` 变体 `configuration.phonemes` 所指的
  「音素名 ↔ ID 表」文件为准；
- 正确的校验方向是 **`phonemes` ⊆（各 stage 模型音素表的交集）**——语言产出的每个音素都
  必须能被声库所用各 stage 编码；
- 该裁决**属编辑器运行期职责，加载事务不执行**；
- 未提供模型音素表的 stage 不参与交集；全部 stage 均未提供时，编辑器跳过核验并给出提示，
  不得判加载失败。

## 5. imports（组合规则）

语言的 `imports` 是本契约族唯一规范性的组合层。Level 1 内集合恒定：

| role | 目标契约 | 数量 | 用途 |
| :-: | :-- | :-: | :-- |
| `linguist/g2p` | `org.openvpi.wolf.inference.G2P` | 恰好 1 | 歌词 → 发音 |
| `linguist/s2p` | `org.openvpi.wolf.inference.S2P` | 0..1 | 发音 → 音素序列；**缺席时该语言最深只到 `Depth::Pronunciation`**，见 §5.0.1 |
| `linguist/onset` | `org.openvpi.wolf.inference.Onset` | 0..1 | 音素 → onset 标记；**缺省时逐位输出 `false`**，见 §5.0 |
| 其他 | 不限 | 任意 | Level 1 未定义用途；wolf 不参与校验，留作开放扩展 |

```json
"imports": [
    { "role": "linguist/g2p",   "ref": ":inference/cmn-pinyin-g2p" },
    { "role": "linguist/s2p",   "ref": ":inference/cmn-pinyin-s2p" },
    { "role": "linguist/onset", "ref": ":inference/cmn-onset" }
]
```

### 5.0 Onset 缺席时的规定产出

**四个仓里没有任何一份有内容的 rule 资源**，所以 Onset 的「缺席」不是边角情形，是当前唯一实际
生效的路径。规定如下：

**无 `linguist/onset` 绑定时，`onsets` 与 `phonemes` 等长，逐位为 `false`。**

不是「未定义」，也不是「由宿主自行决定」：`onsets` 与 `phonemes` 必须等长是既有约束，长度已经
定死，剩下的只有取值一种自由度；把它留给宿主，就是让两个宿主对同一个语言包给出不同的切分。
实现一直如此（`LinguistExecutiveImpl.cpp`），此前只是没有写进契约——**只活在代码里的规定不是
规定**。

### 5.0.1 S2P 缺席时的深度上限

`linguist/g2p` 是唯一必选的 role。`linguist/s2p` 缺席时：

**该语言的最深深度是 `Depth::Pronunciation`。宿主请求更深的 `depth` 时，转换返回到发音层为止，
不报错。**

这不是容错，是一种合法的语言形态：A66 的组合路径里，语言包出 G2P、歌手包出 S2P 与 onset，
缺了歌手包那一半时正确的行为就是退到发音层。深度上限在初始化期即可从 `imports` 集合读出，由
`LanguageStatus::maxDepth` 交给宿主（A70、A71），**不需要试着转换一次才知道**。

放宽下界是 `variant` 的权限而非规范改动：spec 2.4 §`imports` 把「哪些 role 必须存在」交给导入
方自己的 `variant`，并写明导入方「**仍可**」严格要求——是可以，不是必须。对既有声明这是纯加法
（原先失败的声明变为可加载，能加载的行为逐字不变），故不触发 `compatVersion` 抬升。

### 5.1 基数的两侧分工

- **上界由框架结构性保证**：`role` 在模块内唯一是框架强制的（spec 2.4:634；
  `PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport` 以 role 为键 `try_emplace`）。
  三个固定 role 名各自至多出现一次，**wolf 不需要、也不应重复校验上界**；
- **下界由 wolf 校验**：`linguist/g2p` 缺失即加载失败（Ready-2）。`linguist/s2p` 与
  `linguist/onset` 缺失不失败，各自按 §5.0.1 与 §5.0 规定的形态降级。

### 5.2 目标契约与 level

上表只约束目标**契约**与 role，不约束 `level` 数值：

- 被引用目标的三元组须命中某 provider 插件为其声明的 (`interface`, `level`, `variant`) 条目，
  否则 Probe 阶段即报「找不到提供者」（`PackageLoader.cpp:769-774`）；
- 被引用模块的运行时变量按其自身声明的 level 执行，本表基数与匹配规则不随目标 level 变化。

### 5.3 二元组命中（本层的核心裁决）

> 语言自身的 `(language, scheme)` 必须命中其 `linguist/g2p` 与 `linguist/s2p` 目标声明的
> `exports.languages`；未命中即加载失败。**声明了哪个 role 就判哪个**——`linguist/s2p` 缺席
> 时没有第二侧可判，不构成未命中。

- 命中判定为**二元组整体恒等**：`language` 与 `scheme` 两格全等；
- 目标**省略** `exports.languages` 时跳过该侧判定（可加载，宿主应告警——见链推理契约的
  Exports 省略条款）；
- 该规则替代了「按贡献 ID 恒等匹配」的旧口径。后果有三：
  1. G2P / S2P 作者**不必知道任何 linguist 贡献的 ID**；
  2. 第三方语言贡献（ID 带 `qualifier`）只要 `scheme` 相同即自动对接官方 G2P；
  3. 「拼音 G2P 接粤拼 S2P 词典」这类错配在**加载期硬失败**，而非留给运行时大面积未命中。

语言不感知 G2P 的内部组合（是否需要模型后端、几个后端，由 G2P 通过自己的 `imports` 设置），
因此语言层的 role 表恒定不变。

引用其他 Package 的模块时，谁 import 谁在 `desc.json` 的 `dependencies` 中声明。

## 6. Import options

**无。** 语言 Level 1 不定义 import `options` 词汇。

导入方省略 `options` 时框架交付空 object（`PackageLoader.cpp:1358`）；显式给出非 object
（如 `null`）被 provider 判 `InvalidFormat`。

运行时的语言选定不经 import options：组合 provider 在创建子执行体时经 `RuntimeOptions`
注入 `(language, scheme)`（见 [linguist-runtime.md](linguist-runtime.md)）。这样清单里不必
重写一遍语言声明已经说过的信息，也就不存在两侧不一致的可能。

## 7. Configuration

Level 1 不定义任何 `configuration` 键。`wolf` 变体要求 **`configuration` 必须显式写为空对象
`{}`**，出现任何键即加载失败。

> 省略该字段时框架交付缺省 JSON 值 Null（`PackageLoader.cpp:1316-1319`），provider 以非
> object 判 `InvalidFormat`。本条与 spec 2.4:522「`configuration` 为可选公共字段」的关系是：
> 契约有权要求其所辖模块显式提供某个公共可选字段，这是契约层的加严，不是对框架的违反。

语言固有的词典、规则与模型资源一律归于所导入模块的 `configuration`。

## 8. Variables

**无。** 语言不承载逐单元推理 IO。语言的运行时角色是**装配**：为每条语言导入提供执行工厂，
产物为聚合了 G2P / S2P / Onset 子执行体的 `LinguistExecutive`。其可观察行为归 L4。

## 9. 变体治理

- 三元组固定为 (`WolfLinguist`, 1, `wolf`)，Level 1 只有 `wolf` 一个变体；
- 框架按 (`interface`, `level`, `variant`) 三元组全匹配选择 provider，`configuration`
  不参与选择（spec 2.4:576、:590）。三元组命不中即「找不到提供者」加载失败；
- 全部裸变体名为 wolf 官方保留；第三方使用反向域名变体（如 `com.vendor.myengine`）；
- 变体可随时新增，新增不触碰本契约（spec 2.4:769）。

---

## 10. 歌手侧：语言映射

歌手用两个**类别追加字段**声明它支持哪些语言、以及哪一个是缺省。二者由 `SingerCategory`
解析（synthrt 侧），与 `avatar` / `background` / `demoAudio` 同层。

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
| `defaultLanguage` | string | `languages` 非空时**是** | 缺省语言句柄，必须是 `languages` 的键 |

### 10.1 为什么值是 role 而不是 ModuleReference

ImportBinding 只从 `imports` 数组产生（`PackageLoader.cpp:894-917`）；数组之外的
ModuleReference 不产生任何绑定，运行时因此拿不到执行工厂。映射只能指向**已有的 import**，
这不是风格选择而是框架事实。

### 10.2 歌手 role 自由命名

歌手侧的 `role` 恢复为纯粹的本地槽位名，符合 spec 2.4:648「`role` 是导入方为该条目指定的
本地 slot」。语言身份由 `languages` 映射承载，**不再由 role 后缀编码**。

`linguist/` 前缀降为书写建议，无任何规范效力。

### 10.3 唯一映射约束

> 同一声库中每个语言同时只支持一种注音体系。

该约束**结构性成立，无需任何校验代码**：`languages` 是 JSON object，
`JsonObject = std::map<std::string, Value, std::less<>>`（stdcorelib `support/json.h:68`），
每个语言句柄至多对应一条映射，因而至多一个语言导入、至多一种 `scheme`。

> 附注：源 JSON 中的重复键会被 map 静默折叠，而非按 spec 2.4:72「同一层不得出现重复 key，
> 违反时整份声明无效」报错。这是 stdcorelib 的符合性缺口；本约束依赖的不变量（每键至多一条）
> 不受影响。

未被 `languages` 引用的 linguist import 合法但不参与语言域，归编辑期 lint 提示，不判加载失败。

### 10.4 `defaultLanguage`

- **无加载期与运行时语义**，纯宿主策略输入。语言域自身从不读它；
- 之所以必须显式给出而不能取「第一个」：`languages` 是 object，成员按键排序、**声明顺序丢失**
  （stdcorelib `support/json.h:68`），object 里不存在「第一个」；
- 宿主「跟随歌手」语义据此解析（见 [linguist-runtime.md](linguist-runtime.md) §宿主策略）。

### 10.5 校验分工

| 校验 | 执行者 | 相 |
| :-- | :-- | :-- |
| `languages` 是 object、值全为 string；每个键是非空合法 segment | `SingerCategory` | Probe |
| 每个值命中本声明 `imports` 中某条 role（`ContribCreateContext::imports()`，`ContribCategory.h:60`） | `SingerCategory` | Probe |
| `defaultLanguage` 为 string 且是 `languages` 的键；`languages` 非空时必填 | `SingerCategory` | Probe |
| 键形如 `[a-z]{3}` | **wolf validator** | Ready-2 |
| 值指向的 import，其目标类别为 `linguist` | **wolf validator** | Ready-2 |
| 目标声明的 `language` == 映射键 | **wolf validator** | Ready-2 |

**synthrt 只知道「一个语言标签映射到一个 import role」，完全不知道 linguist 是什么。**
形状与 role 存在性归框架，语言域语义归 wolf。这使这两个字段对任何语言体系都通用，而不是把
wolf 的领域知识塞进框架。

---

## 11. 校验的分层落点

框架**无「仅告警」通道**：凡进加载事务的校验，失败即整次加载失败（spec 2.4:444；
`PackageLoader.cpp` 的 Acquire/Ready 全部钩子失败即中止，无旁路告警分支）。提示级检查一律
归编辑器 lint 与宿主日志。

| 相 | 执行者 | 本文所辖内容 | 框架执行点 |
| :-- | :-- | :-- | :-- |
| Probe | 框架 | role 唯一（⇒ §5.1 上界）、`languages` 键唯一（⇒ §10.3） | `PackageLoader.cpp:1365-1369` |
| Probe | `LinguistCategory::createSpec` | §2.3 全部 | `PackageLoader.cpp:1377` |
| Probe | `SingerCategory::createSpec` | §10.5 前三行 | 同上 |
| Acquire | 组合 provider | §4 形状校验、§7 `configuration` 空 | `PackageLoader.cpp:833/845` |
| Ready-1 | 被引目标 provider | 单条 import `options`（本层为空词汇） | `PackageLoader.cpp:873-893` |
| Ready-2 | wolf validator | §5.1 下界、§5.2 目标 interface、§5.3 二元组命中、§10.5 后三行 | `PackageLoader.cpp:924-944` |
| Ready-3 | 组合 provider | pipeline extension 挂载（归 L4） | `PackageLoader.cpp:1004-1007` |

### 11.1 Ready-2 的已知盲区

`ContribImportValidator` 只来自**已加载的解释器**（`ContribPluginFactory.cpp:128-131`），而
解释器只在事务内有贡献选中其三元组时才加载。因此：**依赖闭包内不含任何 linguist 贡献时，
wolf 的歌手侧校验根本不会执行。**

后果与对策：一个声明了 `languages` 却不 import 任何 linguist 的歌手包，其 `languages` 映射值
仍会被 `SingerCategory` 验出「role 不存在」（因为那是框架侧、恒定执行的），但「role 指向的
不是 linguist」这类判定会缺席。故**凡必须永远成立的结构性规则一律放 Probe（L1），
Ready-2 只承载需要跨模块信息的判定**。本文的分工即按此原则划定。

## 12. 归编辑期 lint 的比对

以下一律为警告级，框架与 wolf provider 均不据此判加载失败：

- 语言 `phonemes` 与各 stage 模型音素表的交集裁决（§4.1）；
- S2P `phonemes` 产出集 ⊆ 语言链末端 `phonemes`（超出即告警）；
- Onset 对链上实际音素的覆盖比对，按**下界口径**执行——未列入 `knownPhonemes` 不等于未被
  规则覆盖，通配 `"*"` 覆盖者不得告警；
- G2P `symbols` 可为音节等非音素原子符号，与音素类导出集异类，Level 1 **不**为其规定与任何
  音素集的跨键比对；
- 未被 `languages` 引用的 linguist import（§10.3）；
- 贡献 ID 不合 `<language>-<scheme>[-<qualifier>]` 书写惯例（§2.2）。

**执行方**：后两项只看声明本身，由 `scripts/check-declarations.py` 执行，并被
`make-lang-release.py` 在打包前调用——归档是「不合式声明还便宜」的最后一刻，也是它变成别人
下载物的第一刻。前四项需要声库或模型的音素表，属编辑器运行期职责，打包期看不见。

该脚本**只对 schema 不符判错**（加载器同样拒绝），对上述 lint 项判警告（加载器不据此失败），
两者的严格度因此与运行时一致。
