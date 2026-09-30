# wolf 公共语言包发布与 vcpkg 端口

本文规定公共语言包的**形态、身份、版本模型与发布渠道**，以及 wolf 的 **vcpkg 端口拓扑**。

上位规范为 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)。
本文属 wolf 实现文档，不构成对外契约。

---

## 1. 发布物形态：解包即目录

**发布资产必须是解包后即为 Package root 的目录，不得是 `.dspk` 单文件。**

该约束来自实现：spec 2.4:59 把 Package 定义为 `.dspk` ZIP，但 **synthrt main 分支的加载器只接受
目录**：

```cpp
// synthrt/lib/Core/PackageLoader.cpp:1055-1061
const auto isDirectory = fs::is_directory(root, pathError);
if (!isDirectory) { return Error(Error::FileNotFound, "Package path is not a directory"); }
```

包搜索路径同样只枚举**子目录**（`:614`），且 main 分支不依赖任何解压库。由此：

- **release 资产**为每个包一个 zip，解包后是一个 Package root 目录；
- **包搜索路径**下每个子目录是一个 Package。目录名不参与识别（id 与 version 从 `desc.json`
  读取），但在同一搜索路径内必须唯一。当前归档解包后的目录名是包 ID 中 `/` 替换为 `-` 的形式
  （如 `wolf-lang-cmn`、`wolf-g2p-multi`），不含版本号；需要并存安装多个版本的宿主（§4.3）须为
  各版本选择不同的目录名；
- `.dspk` 单文件形态待 main 分支支持解压后再定。届时只更换资产打包方式，本文其余部分不变。

## 2. 包切分

在 `convert-g2p-packages.py` 中固定的 `SOURCE_REF`（synthrt refactor 线上的提交）下，
`resources/G2pPackages` 含 16 个套件，按新架构切分为三类。

### 2.1 语言包（12 个）→ `wolf/lang-<iso>`

| 旧套件 | 版本 | 旧 class | 新包 ID | `language` | G2P 变体 | 依赖 |
| :-- | :-- | :-- | :-- | :-- | :-- | :-- |
| Phonetic-Suite-Cmn | 1.0.1 | MandarinG2p | `wolf/lang-cmn` | `cmn` | `pipe-chain` | `wolf/g2p-pinyin` |
| Phonetic-Suite-Yue | 1.0.1 | CantoneseG2p | `wolf/lang-yue` | `yue` | `pipe-chain` | `wolf/g2p-pinyin` |
| Phonetic-Suite-Jpn | 0.0.1 | ChainG2p | `wolf/lang-jpn` | `jpn` | `pipe-chain` | — |
| Phonetic-Suite-Deu | 1.0.0 | ChainG2p | `wolf/lang-deu` | `deu` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Eng | 1.0.0 | ChainG2p | `wolf/lang-eng` | `eng` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Fil | 1.0.0 | ChainG2p | `wolf/lang-fil` | `fil` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Fra | 1.0.0 | ChainG2p | `wolf/lang-fra` | `fra` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Ita | 1.0.0 | ChainG2p | `wolf/lang-ita` | `ita` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Kor | 1.0.0 | ChainG2p | `wolf/lang-kor` | `kor` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Por | 1.0.0 | ChainG2p | `wolf/lang-por` | `por` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Rus | 1.0.0 | ChainG2p | `wolf/lang-rus` | `rus` | `pipe-chain` | `wolf/g2p-multi` |
| Phonetic-Suite-Spa | 1.0.0 | ChainG2p | `wolf/lang-spa` | `spa` | `pipe-chain` | `wolf/g2p-multi` |

语言包的目标形态是**完整语言闭包**：`contributions` 同时含 `linguist` 与 `inference`。

**当前进度**：`eng`、`por`、`kor`、`ita` 四种已闭包。这四种语言的 `scheme` 已确定（A49），且其
G2P 产出空格分隔的音素，因此 S2P 使用 `direct`；Onset 按契约可以省略，且没有可用的规则资源。
其余八种只有 `inference`：五种等待 P7 定名；`cmn`、`yue`、`jpn` 产出的是**音节**而非音素，其 S2P
需要一份音节到音素的词典，而**该词典属于歌手包的内容**（spec 2.3 的 `languages[].dict`），不由
语言包提供。因此这三种语言只含 `inference` 是最终形态，而非缺口（A66）。

> **`exports.phonemes` 的取值口径**：闭包语言的清单取自**该语言自身的词典**，即该语言使用的
> 音素集。模型产出的词，以及未查到而原样返回的词，都可能落在清单之外。域契约 §4.1 把该清单定为
> **对齐基准**，且 Level 1 不做加载期强制校验，A50 的取舍以此为前提。

#### `scheme` 取值

依据 refactor 线的词典内容逐个判定：

| `language` | `scheme` | 依据 |
| :-- | :-- | :-- |
| `cmn` | `pinyin` | cpp-pinyin `Pinyin` 引擎 |
| `yue` | `jyutping` | cpp-pinyin `Jyutping` 引擎 |
| `jpn` | `romaji` | `kana2romaji.txt`（`っ` → `cl`） |
| `eng` | `arpabet` | `ds_cmudict-07b.txt` 内容为 `aa l ow` 形式，即小写 ARPABET |
| `por` | `xsampa` | `E J L O R S X Z dZ tS` 均为 X-SAMPA 特征符，鼻化用 `~`（`a~ e~ i~ o~ u~ w~ j~`） |
| `kor` | `romaja` | 修正罗马字：`eo` `eu` 元音、`jj kk pp ss tt` 紧音，大小写区分初声与终声 |
| `ita` | `xsampa-geminate` | X-SAMPA 基础（`dZ tS E O J L S`）加叠写表示双辅音（`dZZ tSS JJ LL SS EE OO`） |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位） | **不符合 §2.1.1**，待 P7 定名；见下 |

**`eng` 取 `arpabet` 而非生态中已有的 `cmu`**：`cmu` 命名的是**词典来源**（CMUdict），而
`scheme` 按定义是**记法**。同一份 CMUdict 可以转写为其他记法，同一套 ARPABET 也可以来自其他词典，
因此 `arpabet` 与字段语义一致。生态中已有的 `eng-cmu` 写法在发布注记中显式说明对应关系，
**避免两个名称并存并逐渐分化**。

**其余五种仍取 `ds`**：逐符号清点后，这五套音素集既不对应任何标准记法，也没有社区通用名称。`deu`
是以 ARPABET 记法扩充德语音素（51 个符号中 38 个是 ARPABET 原符号，另加 `cc ex oe ohh ooh pf rr ts
ue yy`）；`fra` 与 `rus` 以叠写字母表示辅音（前者 `bb dd ff gg ll mm nn pp rr ss tt`，后者以同法
表示腭化，并有 `ja je jo ju`）；`spa` 一半是 X-SAMPA 浊擦音 `B D G`，一半是正字法二合字母
`ch ll rr gn`；`fil` 是与 ARPABET **无交集**的全大写自有集（`DX DY NY SY TS Q`）。

以 `ipa`、`sampa`、`xsampa` 命名会**造成误导**，因为这些音素集并非上述记法。`scheme` 由
`language` 定域（域契约 §2.1），因此 `(deu, ds)` 与 `(fra, ds)` 是不同的二元组，不会混淆；若将来
出现 IPA 版德语，`(deu, ipa)` 不会与之匹配，行为正确。

> **`ds` 不符合下节的命名规则**：该名称表示音素集的使用方，而非音素集本身。`por`、`kor`、`ita`
> 三种已按规则定名，其余五种保留 `ds` 作为 P7 未决期间的占位值。见 §2.1.1 末尾。

#### 2.1.1 `scheme` 命名规则

一种语言可以有**多套并列的音素集**（共享后端的 bundle 即带有三套额外音素集，§2.2）。命名规则须
容纳这一情况，且不得表达不存在的从属关系。

```
scheme := <base> ( "-" <qualifier> )*
```

**`<base>`（记法族名）按优先级分三档取值：**

| 档 | 取值来源 | 示例 |
| :-: | :-- | :-- |
| 1 | 该音素集**即某个既有标准记法**时，取其标准名 | `arpabet`、`ipa`、`xsampa`、`pinyin`、`jyutping`、`romaji` |
| 2 | 否则，该音素集**已有社区通用名称**时，取该名称 | `marzipan`、`millefeuille` |
| 3 | 两者皆无时，由收录方命名，且名称**必须描述该音素集本身** | —— |

第 3 档有一条硬约束：**不得使用来源、生态、版本或「默认」之类的空泛词**。`ds`、`official`、
`standard`、`v2` 均被此条排除，因为这些名称既不描述音素集的构成，也无法区分同一语言的两套音素集。
名称应取自该音素集的显著特征。

**`<qualifier>`（同族限定）**表示同一记法族内的扩展或分支，可以有多段：

- **仅在确属同族派生时使用**。`eng/plus` 是 ARPABET 加上 `ax dr dx tr` 四个符号，因此
  `arpabet-plus` 成立；
- **并列关系的两套音素集各自取 `<base>`**，不得写成 `x` 与 `x-y`。`deu/marzipan` 与
  `deu/default` 是并列的两套，把前者写成 `ds-marzipan` 会表达一条不存在的从属关系，本规则即为
  防止此类命名而设。

**唯一性只需在语言内成立**（二元组由 `language` 定域），因此 `(deu, marzipan)` 与
`(fra, millefeuille)` 各自命名，互不影响。

**扩展方式**：新记法归入第 1 档，新社区音素集归入第 2 档，同族新分支添加 `<qualifier>`。三者都
不需要改动既有取值，也不需要中心登记表，只需在收录时按上表判定一次。

> **待办**：`deu`、`fra`、`spa`、`rus`、`fil` 五种仍取 `ds`，按本规则应归入第 3 档并另行命名
> （P7）。`por`（第 1 档，X-SAMPA）、`kor`（第 1 档，修正罗马字）、`ita`（第 1 档加限定，
> X-SAMPA 加叠写）已确定。各套音素集的清点证据见 §2.2。

### 2.2 共享后端包（2 个）→ `wolf/g2p-multi`、`wolf/g2p-pinyin`

Phonetic-Suite-Multi（19 MB，含 ONNX 权重）是 9 个语言包共用的 seq2seq 模型后端。该套件**不是
语言**，不含 `linguist` 贡献，只提供一个 `inference` 模块，由各语言包在 `dependencies` 中声明依赖。

**后端模块的契约**：A2 取消了 `G2PModel` 契约，但共享一个大模型**必须**将其作为独立包中的模块
（spec 2.4《依赖项》的模块复用场景即针对此情况），而模块必须声明契约。结论如下：

> 后端模块声明 **`org.openvpi.wolf.inference.G2P`**，变体 `multig2p-onnx`，
> 由 `pipe-chain` 经自身 `imports` 中的私有 role 使用。

该模块也**可以**被某种语言直接用作 `linguist/g2p`。这不构成漏洞：模块声明了 G2P 契约，就必须
满足 G2P 契约的全部要求（`mode` 的 `copy`、`skip`、`error` 均须支持）。因此只含模型、不含编排的
语言链是一种合法配置。与原方案「另立 G2PModel 契约并依靠命名隔离防止直接引用」相比，该方案少一份
契约，也少一层机制。

#### bundle 携带的音素集

共享模型的 bundle 带有 **12 个内部语言引用**，其中三个是并列的其他音素集。各套的多字符音素清点
如下：

| bundle 引用 | 多字符音素 | 判定 | 二元组 |
| :-- | :-- | :-- | :-- |
| `eng/default` | `aa ae ah ao aw ay ch dh eh er ey hh ih iy jh ng ow oy sh th uh uw zh` | ARPABET | `(eng, arpabet)` |
| `eng/plus` | 同上 **+ `ax dr dx tr`** | ARPABET 的同族扩展 | `(eng, arpabet-plus)` |
| `deu/marzipan` | `ueh oeh ei au eu xh tsh dsh rh rx vf cl` | 已有通用名称，与 `deu/default` **并列** | `(deu, marzipan)` |
| `fra/millefeuille` | `ah eh ae ee oe ih oh oo ou uh en in on uy sh ng` | 同上 | `(fra, millefeuille)` |
| `por/default` | `a~ e~ i~ o~ u~ j~ w~ dZ tS E J L O R S X Z` | X-SAMPA | `(por, xsampa)` |
| `kor/default` | `NG ch eo eu jj kk pp ss tt` | 修正罗马字 | `(kor, romaja)` |
| `ita/default` | `dz dZZ EE JJ LL nf ng OO SS ts tSS` | X-SAMPA 加叠写 | `(ita, xsampa-geminate)` |
| 其余五个 `*/default` | 见下 | 无标准记法，无通用名称 | `(<lang>, ds)`，待 P7 |

其余五套的构成（供 P7 命名参考）：

| 引用 | 多字符音素 | 构成 |
| :-- | :-- | :-- |
| `deu/default` | `aa ae ah ao aw ax ay cc ch dh ee eh er ex hh ih iy jh ng oe ohh ooh oy pf rr sh th ts ue uh uw yy zh` | ARPABET 形式加德语补充 |
| `rus/default` | `bb dd ff gg kk ll mm nn pp rr ss tt vv zz ja je jo ju sch` | 叠写辅音加拉丁转写 |
| `fra/default` | `bb dd ff gg kk ll mm nn pp rr ss tt vv ww yy zz gn oe ou uy an in on un` | 叠写辅音 |
| `spa/default` | `ch gn ll rr` | 接近正字法 |
| `fil/default` | `dx dy hh ng ny sy th ts` | ARPABET 形式 |

**全部 12 个引用都列入 `languageMap`**：没有映射的音素集无法被任何声库选用。契约只要求映射的
`ref` 必须存在于 bundle 中，不要求反向成立，但没有理由隐藏已有的音素集。

#### 词典与模型的音素集差异

同一条 `pipe-chain` 中，`dict` 步与 `model` 步各自产出音素，**两者使用的音素集可以不同**。逐包
清点结果（比较词典发音列的记号与 bundle 中该语言 `*/default` 的符号集）如下：

| 语言 | 词典记号数 | 不在 bundle 中 | 受影响行 | 越界记号 |
| :-- | --: | --: | :-- | :-- |
| `fil` | 31 | **30** | 24752 / 24752（100%） | 整套大写集 `A B D DX DY E F G H HH …` |
| `ita` | 37 | 5 | 7037 / 9341（75%） | `a1 e1 i1 o1 u1`（重音标记） |
| `eng` | 42 | 3 | 59567 / 133804（44%） | `ax dx`（属 `eng/plus`）、`_r` |
| `deu` `fra` `kor` `por` `rus` `spa` | — | **0** | — | 一致 |

结论有三条：

1. **该差异并非 `fil` 独有**。`eng` 与 `ita` 的差异**早已存在，且已随 `lang-v0.1.0.0` 发布**，
   这两种语言的词典一直处于使用中（只有 `fil` 的词典因以空格分列而从未被读取，见 A34）。因此单独
   禁用 `fil` 的词典以求统一没有依据，那样会遗留两处更大的差异；
2. **`eng` 的词典使用比 `arpabet` 略多的符号**（`ax dx` 正是 `eng/plus` 的扩充符号），而 `_r`
   不在任一 bundle 音素集中。语言包绑定 `(eng, arpabet)` 还是 `(eng, arpabet-plus)`，属于 P7 的内容
   判断；
3. **链的符号集是各步产出的并集，再加上兜底步可能产出的内容**。链推理契约 §3.1 要求
   `exports.symbols` 是可能输出的原子符号的**全集**，因此：

   > **`exports.symbols` 与 `useOriginal` 原词兜底互斥**：原词兜底可以把任意歌词作为发音产出，
   > 符号集无界，`symbols` 只能省略（§3.1 允许省略，宿主应告警）。

   实测**全部 12 条链都使用 `useOriginal`**，因此当前没有一条链能声明 `symbols`。需要静态可比对的
   语言包必须先放弃原词兜底（改用固定的 `defaultPronunciation`，或不配置兜底步）。这是内容取舍，
   与是否编写清单无关。**`openSet` 使这种情况得以显式声明**（域契约 §4.0）：链可以保留兜底，同时
   声明清单不是全集。声库音素表仍须覆盖链实际产出的并集。

#### `wolf/g2p-pinyin`

该包含 cpp-pinyin 引擎的词典树（约 650 KiB），由 `wolf/lang-cmn` 与 `wolf/lang-yue` 两条
`pipe-chain` 共用。变体为 `algo-pinyin`，契约同样是 G2P。

与 `g2p-multi` 不同，该包**不以节省体积为目的**：两个引擎的词典子目录互不重叠，拆包不会去重任何
字节。拆包的理由是**一个进程只能有一个词典根**：引擎经进程全局状态解析词典，两个词典根会相互冲突
（变体文档 §6.1 的实测依据）。拆包使单一词典根成为结构上的事实，而不依赖使用约定。

**版本跟随引擎**：包内词典在打包时取自本仓所链接的 cpp-pinyin 端口（`share/cpp-pinyin/dict`）。
包版本的前三位取该端口版本（现为 `1.0.2`），第四位为打包修订号（§4.1.1，当前发布为 `1.0.2.4`）；
端口的 REF 记入 `converted.json`。词典与读取它的引擎是同一件工件，不应分别编号。

> 存量包中的两份词典**与上游 `res/dict` 字节相同**（唯一差异是 `mandarin/trans_word.txt` 少一行
> `吒:咤`）。该实测结果是将其判定为引擎载荷而非语言内容的直接依据。

### 2.3 直通包（1 个）→ `wolf/lang-zxx`

Phonetic-Suite-**Num**、**Punc**、**Unknown** 合并为**一个**语言包。

三者的 `config.json` 均为**纯直通、无资源**：各有一个 tagger 正则标记 `copy`，再接
`fallback: useOriginal`：

| 旧套件 | tagger 正则 | action |
| :-- | :-- | :-- |
| Num | `(\p{N})` | `copy` |
| Punc | `(\p{P})` | `copy` |
| Unknown | `([.]+)` | `copy` |

除正则外三者完全相同，且旧栈的 `tag` 字段（`number`、`punctuation`、`unknown`）**没有任何消费方**。
因此合并为单一贡献不丢失信息：契约面用 `mode=copy` 表示原样保留，宿主无需区分命中的类别。

**包与贡献**：

| 项 | 取值 |
| :-- | :-- |
| 包 ID | `wolf/lang-zxx` |
| 贡献 ID | `zxx-passthrough` |
| `language` | `zxx` |
| `scheme` | `passthrough` |
| G2P | `pipe-chain`：单个 `verify` 步，正则合并为 `\p{N}|\p{P}|[.]+` → `copy`；接 `fallback: useOriginal` |
| S2P | `direct`（按空格切分，无资源） |
| Onset | 省略（0..1，宿主合成全 `false`） |

**`zxx` 是 ISO 639-3 的正式代码，含义为「无语言内容」**（no linguistic content），与数字、标点和
孤立符号的语义一致。因此无需为其破坏 `language` 的 `[a-z]{3}` 规则，也无需使用私用码
（`qaa`-`qtz`）这类不透明写法。

**边界**：`SP`、`AP`、连音 `-`、拆音续音符 `+` 等**保留记号**仍由宿主预过滤（运行时文档 §4.3
不变）；歌词中的数字与标点由本包处理。

**本包是发布链的引导包**：本包不含词典与模型，不涉及许可问题，但完整覆盖「`desc.json`、语言声明、
两个推理模块、歌手映射」的全链路，因此在实施方案中排在最前（见
[linguist-implementation-plan.md](linguist-implementation-plan.md) M3.5）。本包的源文件位于
`packages/wolf-lang-zxx/`，由 `make-lang-release.py --extra` 纳入发布。

## 3. 结构迁移

旧格式（`package.json` 的 `packageId`、`modules`、`class`、`configuration`，模块声明外壳
`$version`、`level`、`mode`、`schema`、`configuration` 五个键）到 spec 2.4 的迁移是**结构重写，
而非键名替换**：

```
旧                                          新
Phonetic-Suite-Cmn/                         wolf-lang-cmn/
  package.json         packageId/modules      desc.json          id/version/compatVersion/
                                                                 runtimeLevel/contributions/
                                                                 dependencies
  modules/G2p-Cmn/config.json               linguists/cmn-pinyin/linguist.json   ← 新增，无旧对应物
                                            inferences/cmn-pinyin-g2p/inference.json
                                            inferences/cmn-pinyin-s2p/inference.json
                                            inferences/cmn-onset/inference.json
  modules/G2p-Cmn/dict/                     inferences/cmn-pinyin-g2p/dict/
  assets/                                   assets/
```

每个包必须**新写**的内容（旧格式中没有对应物）：

1. `desc.json` 的全部内容（含 `runtimeLevel: 1`、`compatVersion`、`dependencies`）；
2. `linguist.json`，即语言组合声明，含 `language`、`scheme`、`exports.phonemes` 与三条 role imports；
3. 各推理模块的 `exports`：G2P 的 `languages` 与 `symbols`、S2P 的 `languages` 与 `phonemes`、
   Onset 的 `knownPhonemes`；
4. **S2P 与 Onset 模块本身**：旧栈中二者是声库 manifest 的 `s2pMode`、`s2pFile`、`onsetFile`
   字段，而非模块。公共语言包要成为完整闭包，必须为每种语言产出默认的 S2P 与 Onset 声明。

第 4 条是迁移的主要工作量，因此必须在首次 release 之前完成。

**许可证边界**：旧草案称 `Phonetic-Suite-Eng` 的再分发许可未核实、不进入发行物，但其引用的出处
`resources/G2pPackages/README.md` **在 refactor 与 language-level-1 两个分支上均不存在**，属于从旧
草案继承的过时内容。实测该词典 `ds_cmudict-07b.txt` **不带任何许可证头**：上游 CMUdict 0.7b 的
BSD-2-Clause 声明在改编时被删除。

现行处置（由用户决定）：`wolf/lang-eng` 正常发布，**并在 release notes 中注明来源与许可**，补回被
删除的出处。发布清单仍是可机检的边界：需要排除某个包时通过 `make-lang-release.py --exclude` 指定，
排除的包 ID 记入 `manifest.json` 的 `excluded` 字段。

### 3.1 转换管线：产物不进入 git

**转换的中间产物与成品一律不进入 wolf 的 git 历史。** 语言资源包括 133k 行的词典与 19 MB 的模型，
入库会永久增加仓库体积，而 release 资产是更合适的载体。

| 位置 | 内容 |
| :-- | :-- |
| **wolf git** | 转换与发布脚本；`wolf/lang-zxx` 的**源文件**（`packages/wolf-lang-zxx/`，无资源，约 2 KB，属于源文件而非构建产物）；负面用例夹具（`src/tests/auto/packages/`，同样极小） |
| **gitignored 暂存目录** | 转换的全部输出，位于 `build/lang-packages/`，由现有 `.gitignore` 的 `build/` 规则覆盖 |
| **wolf release** | zip 资产与 `manifest.json` |
| **本仓 overlay 端口** | `scripts/vcpkg-ports/wolf-lang-packages/` 的 `assets.cmake` 与 `vcpkg.json`（生成物，随 release 同步） |

**溯源要求（必须执行）**：转换脚本必须**固定 synthrt refactor 线的提交**（`convert-g2p-packages.py`
中的 `SOURCE_REF`），脚本经 `git show` 与 `git ls-tree` 按该提交读取源资源，首次 release notes 记录
该提交。否则 refactor 分支日后被删除或回收后，源资源的唯一副本只存在于 release zip 中，转换无法
复现。

`wolf/g2p-pinyin` 的词典**不来自套件**，而来自本仓所链接的 cpp-pinyin 端口，因此转换需要第二个
来源：

```sh
python3 scripts/convert-g2p-packages.py \
    --synthrt ../synthrt \
    --out build/lang-packages \
    --cpp-pinyin-dict build/vcpkg_installed/<triplet>/share/cpp-pinyin/dict
```

两个来源的 REF 都写入 `converted.json`（`sourceRef`、`cppPinyinRef`）。

### 3.1.0 端口安装验证（A67）

A67 之前端口从未安装成功，且该问题未被发现：测试全程经 `WOLF_LANG_PACKAGES_SOURCE` 指向本地副本，
不经过端口；P8（release 未公开）又使安装失败看似符合预期。实际失败与 release 无关：
`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 中是硬错误，端口一经使用即失败。现行 portfile 改为
直接写出 copyright 文件，并收录已安装包中的许可证文件。

验证不需要公开 release：把归档预先放入 vcpkg 的 `downloads/` 目录，`vcpkg_download_distfile` 校验
SHA512 通过后跳过下载，portfile 其余部分照常执行。

```sh
cp build/lang-packages/dist/*.zip "$VCPKG_ROOT/downloads/"
vcpkg install "wolf-lang-packages[core,multi,pinyin,cmn,...,zxx]" \
    --overlay-ports=scripts/vcpkg-ports --overlay-ports=scripts/vcpkg/ports \
    --triplet x64-linux --x-install-root=<临时目录>
```

结果：15 个包安装到 `share/wolf/packages/`，wolf 的 17 个测试指向该目录树全部通过。**W3a 与 W5 的
退出判据由此首次达成**，其余只有 HTTP 下载一段依赖 release 公开。

清单同时增加了 `lang-packages` feature。实施计划中两处退出判据都写有
`vcpkg install --x-feature=lang-packages`，而此前该 feature 并不存在。该 feature 需显式启用，不影响
普通构建。

### 3.1.1 打包验证：`--verify`

**消费方实际获得的是归档**，因此转换正确不等于发布正确：打包过程中路径被改写、文件被 glob 遗漏、
文件名无法存入 zip，这些错误在打包之前的每一项检查中都无法发现。

`make-lang-release.py --verify <dir>` 把每个归档原样解包，与源目录逐文件比较（**按内容比较，不比较
stat**，因为重写可以保持大小与时间不变），任一不符即**拒绝打包**。解包后的目录树保留在原处，测试
因此运行在归档内容上，而非转换输出上。验证由脚本执行，而不依赖人工记忆。

归档成员的时间戳、`create_system` 与 `external_attr` 固定为同一取值，因此同一目录树在不同时区和
不同操作系统上打出的归档字节相同，SHA512 也相同。

> 打包前脚本还检查 `--out` 是否包含任何源目录。`--out` 与 `--converted` 指向同一目录时，脚本曾在
> 列出源目录后执行 `rmtree(out)`，把源目录一并删除。现行脚本在修改任何文件之前拒绝这种参数组合。
> 此外，脚本在打包前运行 `check-declarations.py`，检查失败即拒绝打包（`--skip-checks` 跳过该检查）。

### 3.2 歌手包形状的夹具：语言包之外的另一半

转换得到的 `wolf/lang-cmn` 与 `wolf/lang-yue` 只有 G2P，没有 S2P，也没有 onset 规则。这不是转换
遗漏：**音节到音素的词典属于歌手包的内容**（spec 2.3 的 `languages[].dict`、`onsetFile`），四个
仓库中都没有这类词典。因此整条链在仓库内无法走完，语言包只能产出到发音为止。

`scripts/make-voicebank-fixture.py` 补全另一半，产出一个歌手包形状的包（位于 gitignored 的
`build/voicebank-fixture/`）。测试不假定该包位于源码树中：`test_HostFlow` 只从 CMake 缓存变量
`WOLF_VOICEBANK_FIXTURE_SOURCE` 获取其位置，该变量为空时以 skip 状态退出，与
`WOLF_LANG_PACKAGES_SOURCE` 的处理方式相同。

| 贡献 | 内容 |
| :-- | :-- |
| `inference` × 4 | 每种语言一个 `dict` S2P（`opencpop-extension.txt` 形式）与一个 `rule` Onset |
| `linguist` × 2 | 每种语言一条组合：G2P 引用 `wolf/lang-cmn:inference/g2p`，S2P 与 onset 为夹具自身的模块 |
| `singer` × 1 | 声明两种语言，缺省为 cmn |

**该词典不是任何在售歌手包所用的词典**，也不以此自居。脚本按一条明确的切分规则，从真实资源自带的
音节表（`assets/ds-zh-pinyin-lite.txt` 615 条、`assets/jyutping_dict.txt` 639 条）推导出词典：按
最长匹配取声母，其余部分为韵母；声母占据整个音节的情况（`m`、`ng`）不拆分。由此 G2P 可能产出的
每一个音节都有词条，这正是测试所需；具体音素集由各歌手包自行决定。

依赖版本取自语言包自身的 `compatVersion`，不写死：若写死版本，打包修订号一经变动即无法解析。

## 4. 版本兼容模型（规范性）

### 4.1 区间在提供方，目标点在依赖方

不为依赖方新增任何区间文法，上位规范的现有模型已经足够：

| 侧 | 写法 | 依据 |
| :-- | :-- | :-- |
| **依赖方（声库）** | `dependencies[].version` 写**目标版本**（实际依赖的最低语义版本） | spec 2.4:161 明确该值「不表示必须加载该精确版本」 |
| **提供方（公共包）** | 以 `[compatVersion, version]` 区间作出承诺；缺省 `compatVersion = version`，即单点区间 | spec 2.4:183-187、:149 |

求解由框架既定规则完成，无需改动：候选条件为依赖目标落在区间内（:398）；同一路径取最高版本候选
（:406）；选中后失败**不回退**到其他候选（:396）；多个版本可以同时加载（:402）。

「声库设置 1.0.0.0 ≤ x ≤ 2.0.0.0」的需求等价于「声库固定目标 `1.0.0.0`，公共包声明
`compatVersion=1.0.0.0, version=2.0.0.0`」。只有**破坏性更新**（抬升 `compatVersion`）才使旧目标
失配，此时按 §4.3 并存安装以保留旧版本。

> **依赖方不设兼容上限文法的理由**：兼容性从哪个版本起被破坏，只有资源包作者掌握，上位规范把
> 兼容承诺的主体责任规定给 Package 作者（:189-198）。声库作者预设上限是对未来的猜测，设错即造成
> 假阳性拒绝。

### 4.1.1 第四位版本号：打包修订号

转换得到的包版本形如 `<源版本三位>.<打包修订号>`：前三位表示**源资源**的版本，第四位表示**本管线
对该资源的打包次序**。当前修订号为 4（`convert-g2p-packages.py` 中的 `PACKAGING_REVISION`）；
`wolf/lang-zxx` 是创作包而非转换产物，版本为 `1.0.0.0`。

**管线对同一输入产出不同结果时必须抬升该位。** 否则两个内容不同的包使用同一版本，按目标点求解的
消费方得到哪一个取决于下载到的是哪一个。

由此得出 `compatVersion` 的取值，**两类包一致**：

| 包 | `compatVersion` | 理由 |
| :-- | :-- | :-- |
| 后端（`g2p-multi`、`g2p-pinyin`） | 修订号取 **0** | 修订之间变化的是打包，而非依赖方绑定的内容：模块 ref 与契约不变，因此按修订 0 构建的消费方仍由修订 3 满足 |
| 语言包 | 修订号取 **0** | 同上。spec 2.4 §兼容性只在六项公开表面被破坏时才要求抬升 `compatVersion`，而打包修订不破坏其中任何一项（A68） |

**依赖方指向区间下界**（即目标包的 `compatVersion`），这是最宽松且始终能被满足的目标点。

> **语言包原先取 `version`，该做法是错误的**（A54 的语言包分支，已由 A68 推翻）。当时的理由是
> 「语言包无人依赖」，但 A66 把 `cmn`、`yue`、`jpn` 的 linguist 与 S2P 划归歌手包，歌手包因此必然
> 在 `dependencies` 中指向语言包，该前提在同一批工作中即已不成立。此外，是否有依赖方并不是
> `compatVersion` 的决定因素：`compatVersion` 是包对**自身公开表面**的承诺。
>
> 实证：把 `wolf-lang-cmn` 的 `version` 与 `compatVersion` 一起抬升到 `1.0.1.3` 后，`test_HostFlow`
> 的六个用例全部报告 `no installed Package satisfies dependency wolf/lang-cmn`。
>
> **该规则现由 lint 强制执行**：目标包在同一次检查的包集合内时，`check-declarations.py` 检查每个
> `dependencies[].version` 是否等于目标包声明的 `compatVersion`，不等即报告**错误**（而非警告），
> 发布脚本据此拒绝打包。A69 已否决运行期能力解析，该检查是这一约束的唯一机制，因此不能是软提示。
>
> 回归测试必须使用「**旧夹具与新包**」的组合：夹具与包一起重新生成时，该失效不会出现。

> 该结论有实测依据：依赖写成后端当前版本的字面量后，第一次抬升修订号即导致九个语言包同时解析
> 失败，加载器报告「no installed Package satisfies dependency」。区间机制正是为此而设。

改变声明形态的修订仍属破坏性更新，须按 §4.2 手工抬升下界。

### 4.2 wolf 的发布规则

1. **非破坏性内容更新**（词典增补、模型替换、资源纠错）：只抬升 `version`，`compatVersion` 不变；
2. **破坏性更新**（触发上位规范《版本》兼容承诺清单所列公开表面的变更）：`compatVersion` 抬升至
   破坏起点；
3. **资源格式版本抬升视同破坏性更新**：变体的 `formatVersion` 抬升时，`compatVersion` 必须同步
   抬升。数据格式不兼容旧插件即意味着包级不兼容，须在依赖求解阶段排除旧目标版本命中新包的情况，
   不依赖解释器阶段的拒绝；
4. **`scheme` 变更即破坏性更新**：`scheme` 是 G2P、S2P 与语言组合之间的匹配键（语言域契约 §2.1），
   改动会使既有链路失配，必须抬升 `compatVersion`；
5. **发布注记义务**：破坏性变更须逐条列出受影响的公开表面；各语言的 `scheme` 取值在首次发布注记
   中列明。

### 4.3 宿主的更新义务

1. **并存安装**：更新即向包根**添加**新版本目录，不删除、不覆盖。被破坏性更新淘汰但仍被在役声库
   依赖的旧版本必须保留（spec 2.4:402）。同一身份（id 与规范化版本）的重复目录属于安装缺陷
   （:404，存在歧义即失败）。当前归档的目录名不含版本号（§1），并存安装时由宿主选择目录名；
2. **更新预检**（lint 级）：升级前按 `DataOnly` 扫描已安装声库的依赖目标，提示受影响的声库清单，
   粒度沿用运行时文档 §8.2 的「声库 × 语言」；
3. **失败呈现**：包存在但 `Load` 失败时不得回退到其他候选（:396），提示文案须区分「升级编辑器」
   与「安装兼容版本」（运行时文档 §8.3）。

### 4.4 包搜索路径顺序

**公共语言包路径必须排在声库内置包路径之前**，依据与常见误读见运行时文档 §9.3。

## 5. 发布渠道：wolf 仓库 Release

- **宿主**：wolf 仓库的 GitHub Releases（不另设资源仓库）；
- **tag 命名空间隔离**：资源发布使用 `lang-v<bundleVersion>`，与代码发布的 `v<x.y.z>` 分开，避免
  两条发布节奏共用同一命名空间；
- **一次 release 即一次全量快照**：含 15 个资产（12 个语言包、1 个直通包、2 个后端包）与一份
  `manifest.json`。各包的 `version` 在各自 `desc.json` 中独立演进，bundleVersion 只标记快照；
- **资产命名**：包 ID 中的 `/` 替换为 `-`，再加 `-<version>.zip`，即 `wolf-lang-<iso>-<version>.zip`、
  `wolf-g2p-multi-<version>.zip`、`wolf-g2p-pinyin-<version>.zip`。解包后为 Package root 目录
  `wolf-lang-<iso>/`、`wolf-g2p-<name>/`（§1）；
- **`manifest.json`**：由 `make-lang-release.py` 写出，供编辑器插件管理器等下载方使用。字段为
  `bundleVersion`、`packages`，以及仅在使用 `--exclude` 时出现的 `excluded`；`packages` 的每个条目
  含 `id`、`file`、`sha512`、`version`、`compatVersion`、`directory`、`size`（字节）与 `breaking`
  （脚本当前一律写 `false`）：

```json
{ "bundleVersion": "0.1.2.0",
  "packages": [
    { "id": "wolf/lang-cmn", "file": "wolf-lang-cmn-1.0.1.4.zip", "sha512": "…",
      "version": "1.0.1.4", "compatVersion": "1.0.1.0", "directory": "wolf-lang-cmn",
      "size": …, "breaking": false } ] }
```

- 上位规范不提供来源认证（spec 2.4:371）：SHA512 是**完整性**校验而非真实性证明，真实性由 release
  渠道（仓库写权限）保证。

### 5.1 当前发布

当前发布为 `lang-v0.1.2.0`（`assets.cmake` 中的 `WOLF_LANG_PACKAGES_BUNDLE_VERSION`），含 15 个归档：

| 包 | 版本 | 归档 |
| :-- | :-- | :-- |
| `wolf/g2p-multi` | 1.0.0.4 | `wolf-g2p-multi-1.0.0.4.zip` |
| `wolf/g2p-pinyin` | 1.0.2.4 | `wolf-g2p-pinyin-1.0.2.4.zip` |
| `wolf/lang-cmn`、`wolf/lang-yue` | 1.0.1.4 | `wolf-lang-cmn-1.0.1.4.zip`、`wolf-lang-yue-1.0.1.4.zip` |
| `wolf/lang-deu`、`eng`、`fil`、`fra`、`ita`、`kor`、`por`、`rus`、`spa` | 1.0.0.4 | `wolf-lang-<iso>-1.0.0.4.zip` |
| `wolf/lang-jpn` | 0.0.1.4 | `wolf-lang-jpn-0.0.1.4.zip` |
| `wolf/lang-zxx` | 1.0.0.0 | `wolf-lang-zxx-1.0.0.0.zip` |

套件列表（`WOLF_LANG_PACKAGES_SUITES`）为
`multi;pinyin;cmn;deu;eng;fil;fra;ita;jpn;kor;por;rus;spa;yue;zxx`。

## 6. vcpkg 端口拓扑

wolf 沿用 lite 的既有接线方式：两个仓库经 `.gitmodules` 共用 **`stdware/vcpkg-overlay`** 子模块
（wolf 中位于 `scripts/vcpkg`），`vcpkg-configuration.overlay-ports` 指向 `../vcpkg/ports`。

wolf 在此之上**增加一层本仓 overlay**（A29），即 `scripts/vcpkg-ports/`。清单
`scripts/vcpkg-manifest/vcpkg.json` 中的 `overlay-ports` 为有序的两项，本仓 overlay 优先：

```json
"overlay-ports": [ "../vcpkg-ports", "../vcpkg/ports" ]
```

清单定义三个 feature：`onnx`（启用 `synthrt-main[onnx]`）、`lang-packages`（依赖
`wolf-lang-packages`）与 `tests`（依赖 `boost-test`）。

| 端口 | 类型 | 位置 | 说明 |
| :-- | :-- | :-- | :-- |
| **`synthrt-main`** | 源码构建 | **wolf 本仓** | 新框架（`ContribCategory`、`PackageLoader`、`SingerCategory`）；不带补丁，固定在 synthrt `onnxruntime-builds-uptake` 分支的提交上 |
| **`wolf-lang-packages`** | 纯数据 | **wolf 本仓** | 从 wolf release 下载语言包，安装到包搜索目录 |

**作用域：wolf 本仓 overlay 只承载 wolf 自身使用的端口**（A29）。理由见 A29：`synthrt-main` 只服务
wolf；`wolf-lang-packages` 的 `assets.cmake` 每次 release 重新生成，端口与 release 同仓才能在一个
提交中同步更新。

**lite 所需的端口由 lite 仓库自己的 vcpkg 配置提供**，本方案不产出供 lite 使用的 `wolf` 端口。

> **子模块须先检出**：wolf 固定了 `stdware/vcpkg-overlay` 的一个修订，接线前须执行
> `git submodule update --init`。该修订**必须与所依赖的 synthrt 一致**：两个修订的
> `stdcorelib-plugin` 版本号同为 `0.1.0.0#1`，却拉取不同的上游 REF，其公开函数从
> `stdc_add_plugin_metadata` 改名为 `stdc_add_plugin_manifest`；修订不一致会导致 wolf 配置失败。

### 6.1 `synthrt-main` 端口

共享 overlay 中**已有**一个 `synthrt` 端口，但该端口固定在 **refactor 线**（`HEAD_REF
localization/passthrough-keys`，与 `convert-g2p-packages.py` 中固定的 `SOURCE_REF` 是同一提交；旧栈：
`srt-g2p`、`srt-s2p`、`plugins/G2P`），lite 使用该端口。wolf 需要的是 **main 线**（新框架）。两条线
产出的包集不同，不能共用端口。

因此 wolf 本仓 overlay 提供 `synthrt-main`，与共享 overlay 的 `synthrt` **并存，互不遮蔽**。

**不复用 `synthrt` 这一名称**：本仓 overlay 排在前面，同名端口会**静默遮蔽**共享 overlay 中的
端口；这种遮蔽在清单中不可见，对比 wolf 与 lite「都依赖 synthrt」时会产生误解。

端口要点：

- `vcpkg_from_git(URL https://github.com/diffscope/synthrt.git REF <提交> HEAD_REF
  onnxruntime-builds-uptake)`，按提交获取，无需归档哈希；`version-string` 为
  `onnxruntime-builds-uptake`；
- 依赖：`qmsetup`、`stduuid`、`stdcorelib`、`stdcorelib-plugin`、`blake3`、`sparsepp`、`bit7z`；
- **缺省 `SYNTHRT_BUILD_DSINFER=OFF`**：dsinfer 的命令行工具硬依赖 `onnxdriver`，而后者在没有
  onnxruntime 时不构建（`dsinfer/plugins/inferencedrivers/onnxdriver/CMakeLists.txt:1-4` 直接
  return），`add_dependencies` 因找不到目标而在配置期失败。wolf 语言域需要的 `inference` 与 `singer`
  两个类别都在 synthrt 本体中，不在 dsinfer 中；
- **可选 feature `onnx`**：为 `multig2p-onnx` 变体启用 dsinfer 与 ONNX 驱动，见 §6.1.1；
- A11 已合入 synthrt 的 `onnxruntime-builds-uptake` 分支，因此端口不带补丁，直接固定在该分支的
  提交上（A26 补记）。

#### 6.1.1 feature `onnx`：ORT 载荷由端口提供

`onnxruntime-builds` 端口**已在共享 overlay 中**（wolf 的 `scripts/vcpkg` 子模块，与 lite 为同一
仓库），无需另建。其缺省 flavor 符合需要：Windows 使用 DirectML NuGet 包，其他平台使用 GitHub 的
CPU 包；`cuda12` 是可选 feature，**不启用**。

两条 synthrt 线的 ORT 接线曾经不同，现已一致：

| 线 | ORT 定位方式 |
| :-- | :-- |
| `refactor`（共享 overlay 的 `synthrt` 端口） | `find_package(onnxruntime-builds)` |
| `main`（本端口使用） | **同上**。原先取 `${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime/default/include`，端口为此手工放置头文件；上游 `onnxruntime-builds-uptake` 分支改为 `find_package` 后，该步骤已删除（A80） |

要点：

- **只需头文件**。驱动用 `stdc::SharedLibrary` 在运行期 `dlopen` 并手动解析 `OrtGetApiBase`
  （配合 `ORT_API_MANUAL_INIT`），链接期不引入 ORT 库。运行期由**宿主**把一个目录作为
  `DriverInitArgs::runtimePath` 传给驱动；**驱动不在插件所在目录中查找 ORT**，因此 synthrt 也不向
  该目录部署任何文件；
- `DSINFER_ENABLE_CUDA=OFF`（上游缺省为 ON）；`DSINFER_ENABLE_DIRECTML` 只在 Windows 上为 ON，其他
  平台传 OFF。实测产出的 `libonnxdriver.so` 不 NEEDED 任何 CUDA 库；
- dsinfer 随 synthrt 安装自己的 CMake 包。端口对 synthrt 与 dsinfer 各执行一次 config fixup 并保留
  父目录，消费方 `find_package(dsinfer CONFIG)` 即得到 `dsinfer::dsinfer`。驱动不是链接目标，运行期
  按插件搜索路径加载。

不构建 multig2p 时无需启用本 feature，也就无需下载 ORT 载荷和编译 dsinfer。

### 6.2 wolf 自身的构建

wolf 不为自身提供端口，而是直接把 `synthrt-main` 列为清单依赖，因此无需单独构建并安装 synthrt。

供 lite 使用的 `wolf` 端口**不在本方案范围内**：其使用方是 lite，按 A29 的作用域原则应由 lite 仓库
自行提供。

### 6.3 `wolf-lang-packages` 端口（纯数据）

该端口沿用同一 overlay 中 `ffmpeg-builds` 的既有结构（`assets.cmake`、`portfile.cmake`、
`<port>-config.cmake.in`、`usage` 与生成脚本），**不引入新模式**：

- `vcpkg.json` 的 `version-string` 等于 bundleVersion（当前为 `0.1.2.0`）；不编译源码，
  `supports` 为 `!uwp`，portfile 启用 `VCPKG_POLICY_EMPTY_PACKAGE`；
- **feature 与套件一一对应**：`cmn`、`deu`、`eng`、`fil`、`fra`、`ita`、`jpn`、`kor`、`por`、`rus`、
  `spa`、`yue`、`zxx`，另加两个后端 `multi` 与 `pinyin`。依赖后端的语言 feature 在 `vcpkg.json` 中
  声明对 `wolf-lang-packages[multi]` 或 `[pinyin]` 的自依赖（`cmn` 与 `yue` 依赖 `pinyin`，其余九个
  `pipe-chain` 语言依赖 `multi`，`jpn` 与 `zxx` 无依赖），写法与 `ffmpeg-builds` 的 feature 互依相同。
  `features` 块由发布脚本按所打的包生成：每个套件一个 feature，依赖取自该包 `desc.json` 的
  `dependencies`；已有 feature 的描述保留（两个后端的描述写有体积），新 feature 使用生成的描述。
  因此 `features` 与 `assets.cmake` 的 `WOLF_LANG_PACKAGES_SUITES` 始终一致；
- **`default-features` 为 `cmn`、`jpn`、`yue`、`zxx`**（经依赖附带 `pinyin`），不含依赖 `multi` 的九种
  语言。该列表由人工维护，发布脚本只从中删除已不存在的 feature；
- `assets.cmake`（每个套件的归档文件名、SHA512 与解包目录，属于**生成物**）由发布脚本在写出
  `manifest.json` 的同一次运行中，依据同一组包记录生成；
- portfile 由 bundleVersion 推导 release tag `lang-v<bundleVersion>`，对每个选中的 feature 以
  `vcpkg_download_distfile` 从该 tag 下载归档并校验 SHA512，解包后复制到
  `${CURRENT_PACKAGES_DIR}/share/wolf/packages/<pkgdir>/`。子路径 `share/wolf/packages` 只在 portfile
  的 `WOLF_LANG_PACKAGES_SUBDIR` 中写一次，config 模板经 `configure_file` 使用同一值；
- portfile 直接写出 copyright 文件，内容为已安装套件列表，以及已安装包中所有 `License.txt`、
  `LICENSE`、`COPYING` 文件的全文；
- **config 包**在 `find_package(wolf-lang-packages CONFIG)` 之后提供：
  - `WOLF_LANG_PACKAGES_DIR`：**包搜索根目录**，可直接传给 `SynthUnit::setPackagePaths`；
  - `WOLF_LANG_PACKAGES_INSTALLED`：本次实际安装的套件列表（由所选 feature 决定）；
  - `WOLF_LANG_PACKAGES_BUNDLE_VERSION`：端口对应的 bundleVersion；
  - `WOLF_LANG_PACKAGES_FOUND`：恒为 `TRUE`。

  纯数据目录不含头文件与库，不在 `find_path` 的搜索范围内；vcpkg 工具链的 installed-dir 变量名在
  不同版本间也有 `VCPKG_INSTALLED_DIR` 与 `_VCPKG_INSTALLED_DIR` 之分。config 包因此以自身文件位置
  推导目录，消费方无需直接调用 `find_path`（与 `ffmpeg-builds` 的 config 采用相同做法）。

### 6.4 消费矩阵

| 消费方 | 接线 | 用途 |
| :-- | :-- | :-- |
| **wolf 自身** | 清单 feature `lang-packages` 引入 `wolf-lang-packages`（使用其缺省 feature） | 构建期取得真实包数据供测试使用 |
| **ds-editor-lite** | **由 lite 仓库自行提供所需端口**（A29 作用域原则），本方案不涉及 | 部署脚本把 `share/wolf/packages` 复制到运行期包根 |
| **synthrt** | 不接入，main 线不含 G2P 资源 | — |

### 6.5 wolf 的构建与测试接线

1. **清单 feature**：`scripts/vcpkg-manifest/vcpkg.json` 的 feature `lang-packages` 依赖
   `wolf-lang-packages`，不指定 feature，因此安装端口的缺省 feature（`cmn`、`jpn`、`yue`、`zxx`，
   以及经依赖附带的 `pinyin`），不含 `multi` 及依赖 `multi` 的语言。常规构建不启用该 feature 时
   **不下载任何包数据**；
2. **CMake 取用**：根 `CMakeLists.txt` 定义缓存变量 `WOLF_LANG_PACKAGES_SOURCE`（缺省为空）。该变量
   为空时执行 `find_package(wolf-lang-packages CONFIG QUIET)`，找到时以 `WOLF_LANG_PACKAGES_DIR` 作为
   其值；两者皆无时**配置不失败**，只输出提示，依赖数据的测试将跳过；
3. **测试装配**：`test_ConvertedPackages` 与 `test_HostFlow` 无条件注册，`test_MultiG2P` 仅在找到
   dsinfer 时注册；三者的 `SKIP_RETURN_CODE` 均为 77。有数据时经测试属性 `ENVIRONMENT` 传入
   `WOLF_LANG_PACKAGES_SOURCE` 与 `WOLF_VOICEBANK_FIXTURE_SOURCE`；数据缺失时测试以 77 退出，ctest
   记为跳过，因此未安装该 feature 的常规构建结果不受影响；
4. **替代来源**：设置缓存变量 `WOLF_LANG_PACKAGES_SOURCE` 指向本机解包副本时，不查找 config 包，
   离线环境无需下载即可运行测试；
5. **CI**：`.github/workflows/ci.yml` 在 Linux（`x64-linux`）与 Windows（`x64-windows`）上以
   `--x-feature=onnx --x-feature=tests` 安装依赖，不启用 `lang-packages`，也不设置
   `WOLF_LANG_PACKAGES_SOURCE`，因此依赖数据的测试在 CI 中跳过。CI 另外运行 lint 单元测试、对
   `packages/wolf-lang-zxx` 运行 `check-declarations.py`，并以 `.github/consumer` 检查安装后的
   CMake 包。

> **边界**：端口只保证**包数据**就绪。真实包加载测试的另一项前提是 G2P、S2P、Onset 解释器。wolf
> 现随附六个解释器插件（`chain`、`pinyin`、`s2p`、`onset`，以及条件构建的 `multig2p` 与 `lua`），
> 端到端用例（`test_ConvertedPackages`、`test_HostFlow`）以此为前提。

### 6.6 发布与端口同步

端口位于本仓（A29），因此同步是**单仓库内的一步**：

1. 运行发布脚本 `make-lang-release.py`（`--converted`、`--extra packages/wolf-lang-zxx`、`--out`、
   `--bundle-version`、`--verify`、`--port scripts/vcpkg-ports/wolf-lang-packages`）：按 §2 的切分
   打包，计算 SHA512，写出 `manifest.json`，并**就地更新**
   `scripts/vcpkg-ports/wolf-lang-packages/assets.cmake` 及该端口 `vcpkg.json` 的 `version-string`
   与 `features`；
2. 提交，打 tag `lang-v<bundleVersion>`，在 GitHub release 上传资产；
3. 执行一次干净的 `vcpkg install`，验证下载校验与安装布局。

指定 `--port` 时，脚本在改写 `assets.cmake` 前执行两项检查：

- bundleVersion 未变而归档内容、归档集合有变化时，拒绝改写。release tag 由 bundleVersion 推导，
  沿用旧版本号会使端口指向不存在的 release；
- bundleVersion 已变，但某个归档在上一次发布已用过的文件名下内容改变时，拒绝改写。vcpkg 按文件名
  缓存下载，已下载旧归档的消费方会遇到哈希不匹配，因此必须抬升该包的版本。

`assets.cmake`、`version-string` 与 `features` 是**生成物**，不手工编辑（与 `ffmpeg-builds` 的
`update-assets.ps1` 约定相同）。这些生成物虽然入库，但只是几十行文件名与 SHA512 文本，与 A28 所禁止
的「133k 行词典、19 MB 模型」性质不同；同仓存放使端口与对应的 release 由同一个提交固定。

### 6.7 端口的下载前提：release 必须无凭证可达

`vcpkg_download_distfile` 以不带凭证的 curl 下载。私有仓库的 release 资产无法以这种方式获取：
GitHub 对未认证请求返回 404 而非 401，表现为资产不存在（已实测）。

**端口不处理任何凭证**。凭证不应进入被提交的文件，也不应进入每个消费方的构建环境；面向所有消费方
的纯数据端口不应持有凭证。

wolf 仓库自 2026-09-28 起公开，release 资产可匿名下载，端口可以直接使用。无网络环境或需要测试未
发布数据时，使用**消费侧替代来源**：CMake 缓存变量 `WOLF_LANG_PACKAGES_SOURCE` 指向一份解包后的
本地副本，不经过端口，也不需要网络。wolf 顶层仅在该变量未设置时回退到
`find_package(wolf-lang-packages CONFIG)`。

## 7. 运行期部署面

前六节描述**资源**的就位方式，本节描述**二进制**的就位方式。两者相互独立，任一缺失都会使包无法
加载，且两种情况的现象相同。

### 7.1 wolf 的安装内容

`cmake --install` 安装七个插件（各带 `plugin.json`）与一个库：

```
lib/libsynthrt-wolf.so                                  # 注册 linguist 类别
lib/plugins/wolf/linguistproviders/wolf/                # WolfLinguistProvider
lib/plugins/wolf/inferenceinterpreters/{chain,onset,s2p}/       # 无额外依赖
lib/plugins/wolf/inferenceinterpreters/pinyin/          # 需要 libcpp-pinyin
lib/plugins/wolf/inferenceinterpreters/lua/             # 需要 libluajit
lib/plugins/wolf/inferenceinterpreters/multig2p/        # 需要 dsinfer 与 ONNX 驱动
```

仅用于测试的桩插件（`inferenceinterpreters/stub`、`singerproviders/stub`）标记为 `NO_INSTALL`，
**不进入安装树**。

### 7.2 各插件的运行期依赖

以下依据 `readelf -d` 实测（Linux；其他平台结构相同）：

| 插件 | wolf 之外的依赖 | 缺失后果 |
| :-- | :-- | :-- |
| `chain` `onset` `s2p` | 无 | —— |
| `pinyin` | `libcpp-pinyin` | 插件无法加载，`algo-pinyin` 无提供者，`wolf/g2p-pinyin` 在 Probe 阶段失败，**cmn 与 yue 均不可用** |
| `lua` | `libluajit-5.1` | `lua` 变体无提供者（当前无生产资源使用该变体，实际无影响） |
| `multig2p` | `libsynthrt-dsinfer` | `multig2p-onnx` 无提供者，**九个 `pipe-chain` 语言包均不可用** |
| `libsynthrt-wolf` | `libre2`、`libblake3` | wolf 本身无法加载 |

**依赖缺失与资源缺失的现象无法区分**，两者都表现为包加载失败。诊断上的区别在于：资源缺失报告找不到
Package，提供者缺失报告 `FeatureNotSupported`，即找不到提供者（spec 2.4:576、:590）。

> **链接 wolf 的保证方式：显式调用而非链接器选项**。链接 wolf 的作用在于 main 之前执行的静态注册
> （README 的核心论点），而 ELF 链接器在 `--as-needed` 下会丢弃这种没有符号引用的依赖（MSVC 链接器
> 同样会丢弃每个未被引用的 import library）。因此 `src/lib/CMakeLists.txt` 不向使用者的链接行添加
> 链接器选项，而是导出 `wolf::linkLinguistCategory()`（`include/wolf/Linguist/LinguistContrib.h:25`）。
> 只需要该类别、不引用任何 wolf 符号的宿主在构造第一个 `SynthUnit` 之前调用该函数一次。函数体为空，
> 其唯一作用是形成一处符号引用（见 README 中说明 wolf 须链接而非加载的一节）。**仓库中没有
> `INTERFACE "LINKER:--no-as-needed"` 这类接线**，链接期丢失依赖时按本条排查。

### 7.3 ONNX 驱动的三段式部署

`multig2p-onnx` 是唯一需要三个组件分别就位的变体：

| 组件 | 来源 | 责任方 |
| :-- | :-- | :-- |
| `libsynthrt-dsinfer` | `synthrt-main[onnx]` | 链接期，随 wolf 插件解析 |
| 驱动插件 `libonnxdriver` | `synthrt-main[onnx]`，安装到 `lib/plugins/dsinfer/inferencedrivers/onnx/` | **宿主**把 `inferencedrivers` 目录传给 `ds::InferenceDriverFactory::setPluginPaths` |
| ONNX Runtime 动态库 | `onnxruntime-builds`，安装到 `share/onnxruntime-builds/runtime/default/` | **宿主**把该目录作为 `DriverInitArgs::runtimePath` 传入 |

第三个组件在运行期经 `dlopen` 加载（`ORT_API_MANUAL_INIT`），**构建期不链接**，因此构建成功不代表
运行期可用。三个组件齐备后宿主才注册 Runtime Service，模块才能取得驱动（A36）。

任一组件缺失时的表现一致：包正常加载，每个词报告 `DriverUnavailable`，由链上的兜底步处理。
**该行为是设计行为而非故障**，归属划分见 A36。

### 7.4 cpp-pinyin 词典不随二进制部署

cpp-pinyin 端口的 `usage` 建议把 `dict/` 复制到可执行文件所在目录。**该建议不适用于 wolf**：词典是
`wolf/g2p-pinyin` 包的内容，由 `configuration.dictRoot` 相对模块声明目录定位（变体文档 §6.2），
运行期不读取 `share/cpp-pinyin/dict`。

端口提供的词典只在**打包期**由 `scripts/convert-g2p-packages.py` 读取一次，用于生成包（A30）。按
上游 usage 另外复制一份到 bin 目录不会出错，但没有作用。

### 7.5 条件构建的可观察后果

`multig2p` 与 `lua` 按依赖是否存在条件构建（A40）。**最小构建**（无 dsinfer、无 LuaJIT）与**完整
构建**只有两处差别：安装树少两个插件目录；测试少两个（15 个对 17 个）。

**15 个包在两种构建下都能加载通过**，因为最小构建中的测试桩会补上缺失的三元组。但这只在测试构建中
成立：桩插件不进入安装树，**发行的最小构建加载 `wolf/g2p-multi` 会失败**。

## 8. 开放问题

| # | 项 |
| :-- | :-- |
| P3 | 每种语言的默认 S2P 与 Onset 声明须新写（§3 第 4 条），是迁移的主要工作量 |
| P4 | lite 使用 wolf 语言包时的端口重复问题（自建、引用本仓 overlay 或提升到共享 overlay）。**已解决**：lite 在其仓库中保留端口副本，发布新版本时整体复制 |
| P5 | `.dspk` 单文件形态，待 main 分支支持解压后再定 |
| P6 | 旧栈声库 manifest 的 `g2pPackageVersion`、`g2pPackages` 字段在迁移时的映射 |
| P7 | `deu`、`fra`、`spa`、`rus`、`fil` 五种的 `scheme` 待定：`ds` 不符合 §2.1.1，须按第 3 档命名（§2.2 已给出各套音素清点）。`por`、`kor`、`ita` 已确定 |
| P8 | release 公开。**已解决**：`lang-v0.1.2.0` 已于 2026-09-28 发布，端口可匿名安装（§6.7） |
