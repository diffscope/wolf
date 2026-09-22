# wolf 公共语言包发布与 vcpkg 端口（正交面）

本文钉公共语言包的**形态、身份、版本模型、发布渠道**，以及 wolf 的 **vcpkg 端口拓扑**。

上位规范 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)。
本文属 wolf 实现文档，不是对外契约。

---

## 1. 发布物形态：解包即目录

**发布资产必须是解包后即为 Package root 的目录，不能是 `.dspk` 单文件。**

这不是偏好，是实现事实：spec 2.4:59 把 Package 定义为 `.dspk` ZIP，但 **synthrt main 的加载器
只受理目录**——

```cpp
// synthrt/lib/Core/PackageLoader.cpp:1055-1061
const auto isDirectory = fs::is_directory(root, pathError);
if (!isDirectory) { return Error(Error::FileNotFound, "Package path is not a directory"); }
```

包搜索路径同样只枚举**子目录**（`:614`），且 main 已不依赖任何解压库。因此：

- **release 资产** = 每语言一个 zip，解包后是一个 Package root 目录；
- **包搜索路径**下每个子目录是一个 Package。目录名不参与识别（id 与 version 从 `desc.json`
  读），但需在同一搜索路径内唯一——约定 `wolf-lang-<iso>-<version>`；
- `.dspk` 单文件形态待 main 支持解压后再议，届时只换资产打包方式，本文其余部分不变。

## 2. 包切分

`resources/G2pPackages` 现有 16 个套件（synthrt `origin/refactor`），按新架构切成三类：

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

每个语言包的目标形态是**完整语言闭包**：`contributions` 同时含 `linguist` 与 `inference`。

**当前进度**：`eng` / `por` / `kor` / `ita` 四种已闭包——它们的 `scheme` 已定（A49），且其 G2P
本就产出空格分隔的音素，故 S2P 用 `direct` 即可，Onset 契约上允许省略且无规则资源可用。余下八种
仍只有 `inference`：五种等 P7 定名；`cmn` / `yue` / `jpn` 产出的是**音节**而非音素，其 S2P 需要
一份音节→音素词典，而**那份词典是歌手包的内容**（spec 2.3 的 `languages[].dict`），语言包本就
不该出——所以这三种「只有 `inference`」是终局形态，不是缺口（A66）。

> **`exports.phonemes` 的口径**：闭包语言的清单取自**该语言自己的词典**，即这门语言工作在哪套
> 音素上。模型产出的词、以及「查不到原样返回」的词都可能落在清单之外——域契约 §4.1 把它定为
> **对齐基准**且 Level 1 不做加载期强制校验，正是这条让 A50 的取舍可以成立。

#### `scheme` 取值

对着 `origin/refactor` 的词典内容逐个判定：

| `language` | `scheme` | 依据 |
| :-- | :-- | :-- |
| `cmn` | `pinyin` | cpp-pinyin `Pinyin` 引擎 |
| `yue` | `jyutping` | cpp-pinyin `Jyutping` 引擎 |
| `jpn` | `romaji` | `kana2romaji.txt`（`っ` → `cl`） |
| `eng` | `arpabet` | `ds_cmudict-07b.txt` 内容为 `aa l ow` 形态，即小写 ARPABET |
| `por` | `xsampa` | `E J L O R S X Z dZ tS` 逐个是 X-SAMPA 特征符，鼻化用 `~`（`a~ e~ i~ o~ u~ w~ j~`） |
| `kor` | `romaja` | 修正罗马字：`eo` `eu` 元音、`jj kk pp ss tt` 紧音，大小写分初声/终声 |
| `ita` | `xsampa-geminate` | X-SAMPA 底（`dZ tS E O J L S`）+ 叠写表双辅音（`dZZ tSS JJ LL SS EE OO`） |
| `deu` `fra` `spa` `rus` `fil` | `ds`（占位） | **不符合 §2.1.1**，待 P7 造名；见下 |

**`eng` 用 `arpabet` 而非生态既有的 `cmu`**：`cmu` 命名的是**词典来源**（CMUdict），而 `scheme`
按定义是**记法**——同一份 CMUdict 可以转写成别的记法，同一套 ARPABET 也可以来自别的词典。取
`arpabet` 才与字段语义对齐。生态中既有的 `eng-cmu` 写法在发布注记中显式说明对应关系，
**避免双名并存漂移**。

**余下五种仍取 `ds`**——逐符号清点后，它们既不对应任何标准记法，也没有社区公名：`deu` 是
ARPABET 记法扩德语音（51 个符号里 38 个是 ARPABET 原符，另加 `cc ex oe ohh ooh pf rr ts ue
yy`）、`fra` 与 `rus` 以叠写辅音区分辅元音（前者 `bb dd ff gg ll mm nn pp rr ss tt`，后者同法
表腭化并有 `ja je jo ju`）、`spa` 一半是 X-SAMPA 浊擦音 `B D G` 一半是正字法二合字母
`ch ll rr gn`、`fil` 是与 ARPABET **零交集**的全大写自有集（`DX DY NY SY TS Q`）。

用 `ipa` / `sampa` / `xsampa` 命名会**误导**——它们不是那些记法。因 `scheme` 由 `language`
定域（域契约 §2.1），`(deu, ds)` 与 `(fra, ds)` 是不同二元组、互不混淆；将来若有人出 IPA 版
德语，`(deu, ipa)` 会正确地不匹配。

> **`ds` 不符合下节的命名规则**（它说的是「谁在用」而不是「是什么」）。`por` / `kor` / `ita`
> 三种已按规则定名，余下五种保留 `ds` 作 P7 未决期间的占位值。见 §2.1.1 末尾。

#### 2.1.1 `scheme` 命名规则

一个语言可以有**几套并列的音素集**（共享后端的 bundle 就带着三套额外的，§2.2）。规则须能容纳
这一点而不制造假的从属关系。

```
scheme := <base> ( "-" <qualifier> )*
```

**`<base>`（记法族名）按优先级三档取值：**

| 档 | 取值来源 | 例 |
| :-: | :-- | :-- |
| 1 | 该音素集**就是某个既有标准记法** → 取其标准名 | `arpabet`、`ipa`、`xsampa`、`pinyin`、`jyutping`、`romaji` |
| 2 | 否则该集**已有社区公名** → 取该公名 | `marzipan`、`millefeuille` |
| 3 | 两者皆无 → 由收录方造名，且**必须描述该集本身** | —— |

第 3 档有一条硬约束：**不得用来源、生态、版本或「默认」之类的空词**。`ds`、`official`、
`standard`、`v2` 都被此条排除——它们不告诉任何人这套音素长什么样，也无法把同语言的两套集区分
开。造名应抓该集的显著特征。

**`<qualifier>`（同族限定）**表同一记法族内的扩展或分支，可多段：

- **仅当确为同族派生时才用**。`eng/plus` 是 ARPABET 加了 `ax dr dx tr` 四个符号，故
  `arpabet-plus` 成立；
- **兄弟关系的两套集各自取 `<base>`**，不得写成 `x` 与 `x-y`。`deu/marzipan` 与 `deu/default`
  是并列的两套，把前者写成 `ds-marzipan` 会宣告一条不存在的从属关系——这正是本规则要防的事。

**唯一性只需在语言内成立**（二元组由 `language` 定域），故 `(deu, marzipan)` 与
`(fra, millefeuille)` 各自取自己的名字，互不干涉。

**扩展性**：新记法进第 1 档，新社区集进第 2 档，同族新分支加 `<qualifier>`。三者都不需要改动
既有取值，也不需要一张中心登记表——只需在收录时按上表判一次。

> **待办**：`deu` / `fra` / `spa` / `rus` / `fil` 五种仍取 `ds`，按本规则应落在第 3 档，须造名
> （P7）。`por`（第 1 档，X-SAMPA）、`kor`（第 1 档，修正罗马字）、`ita`（第 1 档 + 限定，
> X-SAMPA 加叠写）已定。逐套证据见 §2.2 的音素集清点。

### 2.2 共享后端包（2 个）→ `wolf/g2p-multi`、`wolf/g2p-pinyin`

Phonetic-Suite-Multi（19 MB，含 ONNX 权重）是 9 个语言包共用的 seq2seq 模型后端。它**不是语言**，
不含 `linguist` 贡献，只提供一个 `inference` 模块，由各语言包在 `dependencies` 中声明依赖。

**它声明什么契约？** A2 取消了 `G2PModel` 契约，但共享一个大模型**必须**让它成为独立包中的
模块（spec 2.4《依赖项》的模块复用场景正是为此），而模块必须有契约。结论：

> 后端模块声明 **`org.openvpi.wolf.inference.G2P`**，变体 `multig2p-onnx`，
> 由 `pipe-chain` 经自身 `imports` 的私有 role 消费。

它同时也**可以**被某个语言直接用作 `linguist/g2p`——那不是漏洞：它既然声明了 G2P 契约，就必须
满足 G2P 契约（`mode` / `copy` / `skip` / `error` 一应俱全）。「纯模型、无编排」的语言链因此是
一种合法配置。这比原方案的「另立 G2PModel 契约 + 靠命名隔离防直引」少一份契约、少一层机制。

#### bundle 携带的音素集

共享模型的 bundle 带 **12 个内部语言引用**，其中三个是并列的另类音素集。逐套清点其多字符音素：

| bundle 引用 | 多字符音素 | 判定 | 二元组 |
| :-- | :-- | :-- | :-- |
| `eng/default` | `aa ae ah ao aw ay ch dh eh er ey hh ih iy jh ng ow oy sh th uh uw zh` | 就是 ARPABET | `(eng, arpabet)` |
| `eng/plus` | 同上 **+ `ax dr dx tr`** | ARPABET 的同族扩展 | `(eng, arpabet-plus)` |
| `deu/marzipan` | `ueh oeh ei au eu xh tsh dsh rh rx vf cl` | 自有公名，与 `deu/default` **并列** | `(deu, marzipan)` |
| `fra/millefeuille` | `ah eh ae ee oe ih oh oo ou uh en in on uy sh ng` | 同上 | `(fra, millefeuille)` |
| `por/default` | `a~ e~ i~ o~ u~ j~ w~ dZ tS E J L O R S X Z` | X-SAMPA | `(por, xsampa)` |
| `kor/default` | `NG ch eo eu jj kk pp ss tt` | 修正罗马字 | `(kor, romaja)` |
| `ita/default` | `dz dZZ EE JJ LL nf ng OO SS ts tSS` | X-SAMPA + 叠写 | `(ita, xsampa-geminate)` |
| 其余五个 `*/default` | 见下 | 无标准记法、无公名 | `(<lang>, ds)`，待 P7 |

其余五套的形态（供 P7 造名参考）：

| 引用 | 多字符音素 | 形态 |
| :-- | :-- | :-- |
| `deu/default` | `aa ae ah ao aw ax ay cc ch dh ee eh er ex hh ih iy jh ng oe ohh ooh oy pf rr sh th ts ue uh uw yy zh` | ARPABET 形状 + 德语补充 |
| `rus/default` | `bb dd ff gg kk ll mm nn pp rr ss tt vv zz ja je jo ju sch` | 叠写辅音 + 拉丁转写 |
| `fra/default` | `bb dd ff gg kk ll mm nn pp rr ss tt vv ww yy zz gn oe ou uy an in on un` | 叠写辅音 |
| `spa/default` | `ch gn ll rr` | 近正字法 |
| `fil/default` | `dx dy hh ng ny sy th ts` | ARPABET 形状 |

**全部 12 个都进 `languageMap`**：一套无人映射的音素集就是任何声库都点不到的音素集。契约只
要求「映射的 `ref` 必须存在于 bundle」，不要求反向，但没有理由把已有的集藏起来。

#### 词典与模型的音素集并不一定一致

一条 `pipe-chain` 里 `dict` 步与 `model` 步各自产出音素，而**两者用的音素集可以不同**。逐包
清点（比对词典发音列的记号与 bundle 中该语言 `*/default` 的符号集）：

| 语言 | 词典记号数 | 不在 bundle 中 | 受影响行 | 越界记号 |
| :-- | --: | --: | :-- | :-- |
| `fil` | 31 | **30** | 24752 / 24752（100%） | 整套大写集 `A B D DX DY E F G H HH …` |
| `ita` | 37 | 5 | 7037 / 9341（75%） | `a1 e1 i1 o1 u1`（重音标记） |
| `eng` | 42 | 3 | 59567 / 133804（44%） | `ax dx`（属 `eng/plus`）、`_r` |
| `deu` `fra` `kor` `por` `rus` `spa` | — | **0** | — | 一致 |

三条结论：

1. **这不是 fil 独有**。`eng` 与 `ita` 的错配**早已存在且已随 `lang-v0.1.0.0` 发布**——它们的
   词典本就是活的（只有 fil 的词典因空格分列而从未被读到，见 A34）。因此单独关掉 fil 的词典
   来「保持统一」没有依据：那会留下两个更大的错配不管；
2. **`eng` 的词典说的是比 `arpabet` 略富的方言**（`ax dx` 正是 `eng/plus` 的扩充符号），
   而 `_r` 两套 bundle 集都没有。语言包该绑 `(eng, arpabet)` 还是 `(eng, arpabet-plus)`，
   属 P7 的内容判断；
3. **链的符号集是各步产出的并集，再加兜底步能产出的东西**。而链推理契约 §3.1 要求
   `exports.symbols` 是「可能输出的原子符号**全集**」，于是：

   > **`exports.symbols` 与 `useOriginal` 原词兜底互斥**——原词兜底能把任意歌词当发音产出，
   > 符号集无界，`symbols` 只能省略（§3.1 允许省略，宿主应告警）。

   实测**全部 12 条链都用 `useOriginal`**，故当前一个都不能声明 `symbols`。想要静态可比对的
   语言包必须先放弃原词兜底（改用固定 `defaultPronunciation` 或不配兜底步）。这是内容取舍，
   不是写不写清单的问题。**`openSet` 已让这条不再是沉默**（域契约 §4.0）：链可以照旧兜底，
   同时说出「清单不是全集」。声库音素表仍须覆盖链实际产出的并集。

#### `wolf/g2p-pinyin`

cpp-pinyin 引擎与其词典树（约 650 KiB），被 `wolf/lang-cmn` 与 `wolf/lang-yue` 两条
`pipe-chain` 共用。变体 `algo-pinyin`，契约同样是 G2P。

与 `g2p-multi` 不同，它**不是为了省体积**——两个引擎的词典子目录互不重叠，拆包不去重任何字节。
它存在的理由是**一个进程只能有一个词典根**：引擎经进程全局态解析词典，两份根即两败俱伤
（变体文档 §6.1 的实测锚点）。拆包让「一个根」成为结构事实而非纪律要求。

**版本随引擎走**：包内词典在打包时取自本仓所链接的 cpp-pinyin 端口
（`share/cpp-pinyin/dict`），版本号取该端口版本（现为 `1.0.2.0`），端口 REF 记入
`converted.json`。词典与读它的引擎是一件东西，不该各自编号。

> 存量包里那两份词典**与上游 `res/dict` 字节相同**（唯一差异是 `mandarin/trans_word.txt` 少一行
> `吒:咤`）。这条实测是把它判为引擎载荷、而非语言内容的直接依据。

### 2.3 直通包（1 个）→ `wolf/lang-zxx`

Phonetic-Suite-**Num** / **Punc** / **Unknown** 合并为**一个**语言包。

三者的 `config.json` 实为**纯直通、零资源**——各一个 tagger 正则打 `copy` 标，再接
`fallback: useOriginal`：

| 旧套件 | tagger 正则 | action |
| :-- | :-- | :-- |
| Num | `(\p{N})` | `copy` |
| Punc | `(\p{P})` | `copy` |
| Unknown | `([.]+)` | `copy` |

除正则外三者完全相同，且旧栈的 `tag` 字段（`number`/`punctuation`/`unknown`）**无任何消费方**。
因此合并为单一贡献不丢信息：契约面用 `mode=copy` 表达「原样保留」，宿主不需要知道命中的是
哪一类。

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

**`zxx` 是 ISO 639-3 的正式代码，含义为「无语言内容」**（no linguistic content）——数字、标点、
游离符号正是这个语义。因此不必为它们破坏 `language` 的 `[a-z]{3}` 规则，也不必动用私用码
（`qaa`-`qtz`）那种不透明写法。

**边界**：`SP` / `AP`、连音 `-`、拆音续音符 `+` 等**保留记号**仍归宿主预过滤（运行时文档
§4.3 不变）；歌词中的数字与标点走本包。

**本包是发布链的引导包**：零词典、零模型、零许可问题，却完整覆盖「`desc.json` + 语言声明 +
两个推理模块 + 歌手映射」全链路，因此在实施方案中被排在最前（见
[linguist-implementation-plan.md](linguist-implementation-plan.md) M3.5）。

## 3. 结构迁移

旧格式（`package.json` 的 `packageId`/`modules`/`class`/`configuration`，模块声明壳
`$version`/`level`/`mode`/`schema`/`configuration` 五键）到 spec 2.4 是**结构重写而非改键名**：

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

逐包必须**新写**的内容（旧格式里零对应物）：

1. `desc.json` 全部（含 `runtimeLevel: 1`、`compatVersion`、`dependencies`）；
2. `linguist.json`——语言组合声明，含 `language` / `scheme` / `exports.phonemes` 与三条 role imports；
3. 各推理模块的 `exports`——G2P 的 `languages` / `symbols`、S2P 的 `languages` / `phonemes`、
   Onset 的 `knownPhonemes`；
4. **S2P 与 Onset 模块本身**——旧栈里它们是声库 manifest 的 `s2pMode`/`s2pFile`/`onsetFile`
   字段，不是模块。公共语言包要成为完整闭包，必须为每种语言产出默认 S2P 与 Onset 声明。

第 4 条是迁移的主要工作量，也是它必须先于首次 release 完成的原因。

**许可证边界**：旧草案称 `Phonetic-Suite-Eng` 的再分发许可未核实、不入发行物，其引用的出处
`resources/G2pPackages/README.md` **在 refactor 与 language-level-1 两个分支上都不存在**，属
从旧草案继承的漂移。实测该词典 `ds_cmudict-07b.txt` **不带任何许可证头**——上游 CMUdict 0.7b
的 BSD-2-Clause 声明在改编时被剥除。

现行处置（用户拍板）：`wolf/lang-eng` 正常发布，**并在 release notes 中标明来源与许可**，补回
被剥除的出处。发布清单仍是可机检的边界：需要排除某个包时经 `make-lang-release.py --exclude`
指定，不靠文档里的一句话。

### 3.1 转换管线：产物不进 git

**转换的中间产物与成品一律不进 wolf 的 git 历史。** 语言资源是 133k 行的词典与 19 MB 的模型，
入库即永久增重，而它们本就有更合适的载体（release 资产）。

| 落点 | 内容 |
| :-- | :-- |
| **wolf git** | 转换脚本；`wolf/lang-zxx` 的**创作源**（零资源，约 2 KB——它是 source 而非 build output）；负面用例夹具（同样极小） |
| **gitignored 暂存目录** | 转换的全部输出。落 `build/lang-packages/`，已被现有 `.gitignore` 的 `build/` 覆盖 |
| **wolf release** | zip 资产 + `manifest.json` |
| **overlay 端口仓** | `assets.cmake`（生成物，随 release 同步） |

**溯源纪律（必须执行）**：转换脚本必须**固定 synthrt `origin/refactor` 的 commit SHA**，首次
release notes 记录该 SHA。否则 refactor 分支来日删除或被 GC 后，源资源的唯一副本就只剩在
release zip 内，无法复现转换。

`wolf/g2p-pinyin` 的词典**不来自套件**，而来自本仓所链接的 cpp-pinyin 端口，故转换需第二个来源：

```sh
python3 scripts/convert-g2p-packages.py \
    --synthrt ../synthrt \
    --out build/lang-packages \
    --cpp-pinyin-dict build/vcpkg_installed/<triplet>/share/cpp-pinyin/dict
```

两个来源的 REF 都写入 `converted.json`（`sourceRef` / `cppPinyinRef`）。

### 3.1.0 端口已实测（A67）

端口在此之前**从未安装成功过**，而没人知道——测试全程经 `WOLF_LANG_PACKAGES_SOURCE` 指向本地
副本绕开它，`P8`（release 未公开）又给了「装不上是正常的」这个现成解释。实际的失败与 release
无关：`vcpkg_install_copyright(FILE_LIST "")` 在 vcpkg 里是硬错误，端口一被使用就失败。

验法不需要公开 release：把归档预置进 vcpkg 的 `downloads/`，`vcpkg_download_distfile` 校验
SHA512 通过即跳过取件，portfile 其余部分照常执行。

```sh
cp build/lang-packages/dist/*.zip "$VCPKG_ROOT/downloads/"
vcpkg install "wolf-lang-packages[core,multi,pinyin,cmn,...,zxx]" \
    --overlay-ports=scripts/vcpkg-ports --overlay-ports=scripts/vcpkg/ports \
    --triplet x64-linux --x-install-root=<临时目录>
```

结果：15 个包装进 `share/wolf/packages/`，wolf 的 17 个测试指向该树全部通过。**W3a / W5 的退出
判据至此首次达成**，只剩 HTTP 取件一段真正等 release 公开。

清单侧同时补上了 `lang-packages` feature——实施计划两处退出判据都写着
`vcpkg install --x-feature=lang-packages`，而这个 feature 一直不存在。它是 opt-in 的，不牵连
普通构建。

### 3.1.1 打包即验证：`--verify`

**归档才是消费方真正拿到的东西**，所以「转换正确」与「发布正确」不是同一句话：路径在打包途中被
改写、文件被 glob 漏掉、名字过不了 zip，这些在打包之前的每一道检查里都是通过的。

`make-lang-release.py --verify <dir>` 把每个归档原样解开，与源目录逐文件比对（**按内容比，不看
stat**——重写可以保住大小与时间），任一不符即**拒绝打包**。解开的树留在原地，测试就跑在归档上
而不是跑在转换输出上。至此「验证过了」是脚本做过的事，不是谁记得做过的事。

> 同一个改动里还堵上了一个已经发生过的事故：`--out` 与 `--converted` 指同一目录时，脚本先列出
> 源、再 `rmtree(out)`，把刚列出的源一起删了。现在它在动任何东西之前先拒绝。

### 3.2 歌手包形状的夹具：补上语言包不出的那一半

转换出来的 `wolf/lang-cmn` 与 `wolf/lang-yue` 只有 G2P，没有 S2P，也没有 onset 规则。这不是转换
漏了：**音节到音素的词典是歌手包的内容**（spec 2.3 的 `languages[].dict`、`onsetFile`），四个仓
里一份都没有。于是整条链在仓内无法走完——语言包出到发音就断了。

`scripts/make-voicebank-fixture.py` 补上另一半，产出一个歌手包形状的包
（gitignored 的 `build/voicebank-fixture/`；测试不假定它在源码树里，`test_HostFlow` 只从
CMake 缓存变量 `WOLF_VOICEBANK_FIXTURE_SOURCE` 得知它在哪，没有就以 skip 退出，与
`WOLF_LANG_PACKAGES_SOURCE` 同一口径）：

| 贡献 | 内容 |
| :-- | :-- |
| `inference` × 4 | 每语言一个 `dict` S2P（`opencpop-extension.txt` 形状）与一个 `rule` Onset |
| `linguist` × 2 | 每语言一条组合：G2P 引自 `wolf/lang-cmn:inference/g2p`，S2P 与 onset 是自己的 |
| `singer` × 1 | 声明两种语言，缺省 cmn |

**它不是任何在售歌手包所用的词典**，也不冒充是。词典由脚本按一条写明的切分规则，从真实资源自带
的音节表（`assets/ds-zh-pinyin-lite.txt` 615 条、`assets/jyutping_dict.txt` 639 条）推出来：按最长
匹配取声母，其余为韵母，声母吃满整个音节的（`m`、`ng`）不拆。这样每一个 G2P 能产出的音节都有词条
——测试要的正是这个，具体音素集是各家歌手包自己的事。

依赖版本取自语言包自己的 `compatVersion`，不写死：打包修订号一动，写死的版本当天就解析不到。

## 4. 版本兼容模型（规范性）

### 4.1 区间在提供方，目标点在依赖方

不为依赖方新增任何区间文法——上位规范的现成模型已经够用：

| 侧 | 写什么 | 依据 |
| :-- | :-- | :-- |
| **依赖方（声库）** | `dependencies[].version` 写**目标版本**（实际依赖的最低语义版本） | spec 2.4:161 明示该值「不表示必须加载该精确版本」 |
| **提供方（公共包）** | `[compatVersion, version]` 区间承诺；缺省 `compatVersion = version` 即单点区间 | spec 2.4:183-187、:149 |

求解为框架既定、零改动：候选条件 = 依赖目标落在区间内（:398）；同一路径取最高版本候选
（:406）；选中后失败**不回退**其他候选（:396）；多版本可同时加载（:402）。

「声库设置 1.0.0.0 ≤ x ≤ 2.0.0.0」的诉求等价实现为「声库 pin 目标 `1.0.0.0` + 公共包声明
`compatVersion=1.0.0.0, version=2.0.0.0`」。只有**破坏性更新**（`compatVersion` 抬升）才使旧
目标失配，此时按 §4.3 并存安装保留旧版。

> **为什么不给依赖方加兼容上限文法**：「从哪版起破坏」的信息只掌握在资源包作者手里，上位规范
> 把兼容承诺的主体责任钉在 Package 作者（:189-198）。声库作者预设上限是对未来的猜测，写错即
> 假阳性拒绝。

### 4.1.1 第四位版本号是打包修订号

转换出的包版本形如 `<源版本三位>.<打包修订号>`：前三位说的是**源资源**，第四位说的是**本管线
对它的第几次打包**。

**管线对同一输入产出不同结果时必须抬这一位。** 否则两个内容不同的包会顶着同一个版本，而按目标
点求解的消费方拿到哪一个取决于它下载到了哪一个。

由此推出 `compatVersion` 的口径，**两类包一致**：

| 包 | `compatVersion` | 理由 |
| :-- | :-- | :-- |
| 后端（`g2p-multi`、`g2p-pinyin`） | 修订号取 **0** | 修订之间变的是打包，不是被依赖方绑定的东西——模块 ref 与契约没动，故按修订 0 构建的消费方仍被修订 3 服务 |
| 语言包 | 修订号取 **0** | 同上。spec 2.4 §兼容性只在六项公开表面被破坏时才要求抬 `compatVersion`，而打包修订一项都没破（A68） |

**依赖方指向区间下沿**（即目标包的 `compatVersion`），这是最宽松且始终被满足的目标点。

> **语言包原先取 `version`，那是错的**（A54 的语言包分支，已由 A68 推翻）。当时的理由是「语言包
> 无人依赖」，但 A66 把 `cmn` / `yue` / `jpn` 的 linguist 与 S2P 判归歌手包，歌手包因此必然在
> `dependencies` 里指向语言包——这个前提在写下它的同一批工作里就已经不成立。而且「有没有人依赖
> 我」根本不是 `compatVersion` 的输入：它是对**自身公开表面**的承诺。
>
> 实证：把 `wolf-lang-cmn` 的 `version` 与 `compatVersion` 一起抬到 `1.0.1.3`，`test_HostFlow`
> 六个用例全部报 `no installed Package satisfies dependency wolf/lang-cmn`。
>
> **这条现在由 lint 强制**：`check-declarations.py` 检查每个 `dependencies[].version` 是否等于
> 目标包声明的 `compatVersion`，不等即**错误**（不是警告），发布脚本据此拒绝打包。它是这条线上
> 唯一的机制——A69 已否掉运行期能力解析——所以不能是软提示。
>
> 回归测试必须用「**旧夹具 + 新包**」的组合：夹具与包一起重新生成时，这个失效不会出现。

> 这条不是纸面推演：把依赖写成后端的当前版本字面量后，第一次抬修订号就让九个语言包同时解析
> 失败——加载器当场报「no installed Package satisfies dependency」。区间机制本就是为此存在的。

改变了声明形态的修订仍属破坏性更新，须按 §4.2 手工抬高下沿。

### 4.2 wolf 的发布纪律

1. **非破坏性内容更新**（词典增补、模型替换、资源纠错）：只抬 `version`，`compatVersion` 不动；
2. **破坏性更新**（触发上位规范《版本》兼容承诺清单所列公开表面变更）：`compatVersion` 抬至
   破坏起点；
3. **资源格式版本抬升视同破坏性更新**：变体的 `formatVersion` 抬升时 `compatVersion` 必须同步
   抬升——数据格式不兼容旧插件 = 包级不兼容，须在依赖求解阶段就排除旧目标版本命中新包，不依赖
   解释器期拒绝兜底；
4. **`scheme` 变更即破坏性更新**：`scheme` 是 G2P/S2P 与语言组合的匹配键（语言域契约 §2.1），
   改动它会使既有链路失配，必须抬 `compatVersion`；
5. **发布注记义务**：破坏性变更逐条列出受影响的公开表面；各语言 `scheme` 取值在首次发布注记
   中明列。

### 4.3 宿主的更新义务

1. **并存安装**：更新 = 向包根**添加**新版本目录，不删除、不覆盖。被破坏性更新淘汰但仍被在役
   声库依赖的旧版本必须保留（spec 2.4:402）。同身份（id + 规范化版本）重复目录属安装缺陷
   （:404 歧义即失败）；
2. **更新预检**（lint 级）：升级前按 `DataOnly` 扫描已装声库的依赖目标，提示受影响声库清单，
   粒度沿运行时文档 §8.2 的「声库 × 语言」；
3. **失败呈现**：包在但 `Load` 失败不得回退其他候选（:396），文案区分「升级编辑器」与「安装
   兼容版本」（运行时文档 §8.3）。

### 4.4 包搜索路径顺序

**公共语言包路径必须排在声库内置包路径之前**，依据与常见误读见运行时文档 §9.3。

## 5. 发布渠道：wolf 仓库 Release

- **宿主** = wolf 仓库的 GitHub Releases（不另设资源仓）；
- **tag 命名空间隔离**：资源发布用 `lang-v<bundleVersion>`，与代码发布的 `v<x.y.z>` 分开，
  避免两条节奏抢同一命名空间；
- **一次 release = 一次全量快照**：含 15 个资产（12 语言 + 1 直通 + 2 后端）与一份 `manifest.json`。
  各包自身的 `version` 在各自 `desc.json` 中独立演进，bundleVersion 只标记快照；
- **资产命名** `wolf-lang-<iso>-<version>.zip` / `wolf-g2p-multi-<version>.zip`，解包即 Package
  root 目录（§1）；
- **`manifest.json`**（编辑器插件管理器与 port 生成器共同消费）：

```json
{ "bundleVersion": "1.0.0.0",
  "packages": [
    { "id": "wolf/lang-cmn", "file": "wolf-lang-cmn-1.0.1.0.zip", "sha512": "…",
      "version": "1.0.1.0", "compatVersion": "1.0.0.0", "breaking": false,
      "language": "cmn", "scheme": "pinyin" } ] }
```

- 上位规范不提供来源认证（spec 2.4:371）：SHA512 是**完整性**校验而非真实性证明，真实性由
  release 渠道（仓库写权限）承担。

## 6. vcpkg 端口拓扑

参照 lite 的既有接线：两仓经 `.gitmodules` 共用 **`stdware/vcpkg-overlay`** 子模块，
`vcpkg-configuration.overlay-ports` 指向 `../vcpkg/ports`。

wolf 在此之上**再加一层本仓 overlay**（A29）——`scripts/vcpkg-ports/`，`overlay-ports` 写成
有序两项：

```json
"overlay-ports": [ "../vcpkg-ports", "../vcpkg/ports" ]
```

| 端口 | 类型 | 落点 | 说明 |
| :-- | :-- | :-- | :-- |
| **`synthrt-main`** | 源码构建 | **wolf 本仓** | 新框架（`ContribCategory` / `PackageLoader` / `SingerCategory`）；后续携带 A11 的 patch |
| **`wolf-lang-packages`** | 纯数据 | **wolf 本仓** | 从 wolf release 下载语言包，装进包搜索目录 |

**作用域：wolf 仓的 overlay 只承载 wolf 自己消费的端口**（A29）。理由见 A29：`synthrt-main`
是只服务 wolf 的端口且要带补丁，`wolf-lang-packages` 的 `assets.cmake` 每次 release 重新生成、
同仓才能在一个提交里更新。

**lite 所需的端口由 lite 仓自己的 vcpkg 提供**，本方案不产出「供 lite 消费的 `wolf` 端口」。

> **子模块须先检出**：wolf pin 的是 `stdware/vcpkg-overlay`，接线前需
> `git submodule update --init`。注意该 pin **必须与所依赖的 synthrt 一致**——两个修订的
> `stdcorelib-plugin` 版本号同为 `0.1.0.0#1` 却拉不同上游 REF，公开函数从
> `stdc_add_plugin_metadata` 改名为 `stdc_add_plugin_manifest`，pin 不一致会导致 wolf 配置失败。

### 6.1 `synthrt-main` 端口

共享 overlay 里**已有**一个 `synthrt` 端口，但它 pin 的是
`REF 814bf81` / `HEAD_REF localization/passthrough-keys`——即 **refactor 线**（旧栈：
`srt-g2p` / `srt-s2p` / `plugins/G2P`），lite 正在消费它。wolf 需要的是 **main 线**（新框架）。
两条线的产出包集不同，不能共用。

因此 wolf 本仓 overlay 提供 `synthrt-main`，与共享 overlay 的 `synthrt` **并存且互不可见**。

**不复用 `synthrt` 这个名字**：本仓 overlay 排在前面，同名端口会**静默遮蔽**共享 overlay 的
那个；遮蔽在 manifest 上看不出来，任何人对比 wolf 与 lite「都依赖 synthrt」时都会被误导。

端口要点：

- `vcpkg_from_github(REPO diffscope/synthrt REF <main 的 commit SHA>)`；
- 依赖：`qmsetup`、`stduuid`、`stdcorelib`、`stdcorelib-plugin`、`blake3`、`sparsepp`、`bit7z`；
- **`SYNTHRT_BUILD_DSINFER=OFF`（缺省）**：dsinfer 的 cli 硬依赖 `onnxdriver`，而后者在没有
  onnxruntime 时不构建（`dsinfer/plugins/inferencedrivers/onnxdriver/CMakeLists.txt:1-4` 直接
  return），于是 `add_dependencies` 找不到目标、配置期即失败。wolf 的语言域需要的 `inference`
  与 `singer` 两个类别都在 synthrt 本体，不在 dsinfer；
- **feature `onnx`（可选）**：为 `multig2p-onnx` 变体打开 dsinfer 与 ONNX 驱动，见 §6.1.1；
- A11 已在 synthrt 分支 `onnxruntime-builds-uptake` 落地，端口不带补丁，直接钉在该分支的提交上
  （A26 补记）。

#### 6.1.1 feature `onnx`：ORT 载荷由端口就位

`onnxruntime-builds` 端口**已在共享 overlay 内**（wolf 的 `scripts/vcpkg` 子模块，与 lite 同一
个仓），无需另建。其缺省 flavor 正是所需：Windows 取 DirectML NuGet，其余平台取 GitHub 的 CPU
包；`cuda12` 是可选 feature，**不启用**。

两条 synthrt 线的 ORT 接线曾经不同，现已一致：

| 线 | ORT 定位方式 |
| :-- | :-- |
| `refactor`（`814bf81`） | `find_package(onnxruntime-builds)` |
| `main`（本端口消费） | **同上**。原先取 `${SYNTHRT_SOURCE_DIR}/third-party/onnxruntime/default/include`，端口为此手工铺头文件；上游分支 `onnxruntime-builds-uptake` 改掉之后，那段搬运已删（A80） |

要点：

- **只需头文件**。驱动用 `stdc::SharedLibrary` 在运行期 `dlopen` 并手动 resolve
  `OrtGetApiBase`（配合 `ORT_API_MANUAL_INIT`），链接期不引 ORT 库；运行期由**宿主**把一个目录
  作为 `DriverInitArgs::runtimePath` 传给驱动——**驱动自己从不去插件旁边找**，所以 synthrt 也
  不再往那里部署任何东西；
- `DSINFER_ENABLE_CUDA=OFF`（上游缺省为 ON）、`DSINFER_ENABLE_DIRECTML=ON`（非 Windows 上该宏
  本就不生效）。实测产出的 `libonnxdriver.so` 不 NEEDED 任何 CUDA 库；
- dsinfer 随 synthrt 安装自己的 CMake 包。端口对 synthrt 与 dsinfer 各做一次 config fixup 并保留
  父目录，消费方 `find_package(dsinfer CONFIG)` 即得 `dsinfer::dsinfer`。驱动不是链接目标，按插件
  搜索路径在运行期取用。

不做 multig2p 的构建不必启用本 feature，也就不必拖 ORT 载荷与 dsinfer 编译。


### 6.2 wolf 自身的构建

wolf 不为自己出端口，而是直接把 `synthrt-main` 列为 manifest 依赖——这已消除 README 那句
「synthrt is not a vcpkg dependency here. Build and install it separately」。

供 lite 消费的 `wolf` 端口**不在本方案范围**：它的消费方是 lite，按 A29 的作用域原则应由 lite
仓自己提供。

### 6.3 `wolf-lang-packages` 端口（纯数据）

复刻同 overlay 中 `ffmpeg-builds` 的既有范式（`assets.cmake` + `portfile.cmake` +
`<port>-config.cmake.in` + `usage` + 生成脚本），**不发明新模式**：

- `vcpkg.json` 的 `version-string` = bundleVersion；零源码编译，`supports` 全平台；
- **features 与语言一一对应**：`cmn` / `deu` / `eng` / `fil` / `fra` / `ita` / `jpn` / `kor` /
  `por` / `rus` / `spa` / `yue` / `zxx`，另加两个后端 `multi` 与 `pinyin`。依赖后端的语言 feature
  在 `vcpkg.json` 中声明对 `wolf-lang-packages[multi]` / `[pinyin]` 的自依赖（`cmn` 与 `yue` 依
  `pinyin`，其余九个 `pipe-chain` 语言依 `multi`），与 ffmpeg-builds 的 feature 互依写法同款；
- **default-features 不含 `eng`**（许可未核实，§3）；
- `assets.cmake`（文件名 + SHA512，**生成物**）由发布脚本从 `manifest.json` 直接产出；
- portfile 按 release tag URL `vcpkg_download_distfile` 下载校验后解包安装到
  `${CURRENT_PACKAGES_DIR}/share/wolf/packages/<pkgdir>/`；
- **config 包**提供 `find_package(wolf-lang-packages CONFIG)` 后可用的：
  - `WOLF_LANG_PACKAGES_DIR`——**包搜索根目录**，可直接喂给 `SynthUnit::setPackagePaths`；
  - `WOLF_LANG_PACKAGES_INSTALLED`——本次实际安装的语言清单（由 feature 决定）。

  纯数据目录不在 `find_path` 的默认搜索面，且 vcpkg 工具链的 installed-dir 变量名跨版本有
  `VCPKG_INSTALLED_DIR` / `_VCPKG_INSTALLED_DIR` 之差——两者都由 config 包内部吸收，消费方
  不做裸 `find_path`（ffmpeg-builds config 先例同款手法）。

### 6.4 消费矩阵

| 消费方 | 接线 | 用途 |
| :-- | :-- | :-- |
| **wolf 自身** | manifest feature `lang-packages` 门控依赖 `wolf-lang-packages`（按测试需要选 features） | 构建期取得真实包数据供测试 |
| **ds-editor-lite** | **由 lite 仓自行提供所需端口**（A29 作用域原则），本方案不涉及 | 部署脚本把 `share/wolf/packages` 拷进运行期包根 |
| **synthrt** | 不接——main 线不含 G2P 资源 | — |

### 6.5 wolf 的构建测试接线

1. **manifest 门控**：`scripts/vcpkg-manifest/vcpkg.json` 增 feature `lang-packages`，其
   dependencies 携 feature 选择（默认取 `cmn` + `multi`：一条 `algo-` 链 + 一条带后端的
   `pipe-chain` 链，覆盖两种拓扑）。常规构建不带该 feature 时**不下载任何包数据**；
2. **CMake 取用**：`find_package(wolf-lang-packages CONFIG)`；未启用 feature 时变量为空，
   **不 fail 配置**；
3. **测试装配**：包相关测试按条件注册——有数据则经 `ENVIRONMENT` 传目录，无数据则测试体内检测
   缺失即 `SKIP`，保证未装 feature 的常规 CI 绿灯不劣化；
4. **逃生口**：CMake 缓存变量 `WOLF_LANG_PACKAGES_SOURCE` 覆盖 config 包结果（指向本机
   checkout），离线环境零下载可跑。

> **边界**：端口只解决**包数据**就绪。真实包加载测试的另一半前提是 G2P / S2P / Onset 解释器，
> 现已随附七个解释器插件（`chain` / `pinyin` / `s2p` / `onset`，以及条件构建的 `multig2p` 与
> `lua`），端到端用例（`test_ConvertedPackages` / `test_HostFlow`）以此为前提。

### 6.6 发布 → 端口同步

端口在本仓（A29），因此同步是**单仓一步**：

1. 跑发布脚本：按 §2 切分打包 → SHA512 → 写 `manifest.json`，并**就地更新**
   `scripts/vcpkg-ports/wolf-lang-packages/assets.cmake` 与其 `vcpkg.json` 的 `version-string`；
2. 提交 → tag `lang-v<bundleVersion>` → GitHub release 上传资产；
3. 跑一次干净 `vcpkg install` 验证下载校验与安装布局。

`assets.cmake` 与 `version-string` 是**生成物**，不手工编辑（同 ffmpeg-builds 的
`update-assets.ps1` 约定）。它们虽是生成物却入库——几十行文件名与 SHA512 的文本，与 A28 所禁的
「133k 行词典、19 MB 模型」不是一回事，而同仓才能让端口与其对应的 release 由同一个提交固定。

### 6.7 端口的下载前提：release 必须无凭证可达

`vcpkg_download_distfile` 用无凭证的 curl。**私有仓的 release 资产因此取不到**——GitHub 对未认证
请求返回 404 而非 401，看上去像资产不存在（实测）。

**端口不承载任何凭证**，这是有意的：凭证既不该进入被提交的文件，也不该进入每个消费方的构建
环境；而一个「谁都能装」的纯数据端口，本就没有持有凭证的道理。

release 尚未公开期间，改用**消费侧逃生口**：CMake 缓存变量 `WOLF_LANG_PACKAGES_SOURCE` 指向
一份解包好的本地副本，绕开端口，零网络零凭证。wolf 顶层在未设置该变量时才回落到
`find_package(wolf-lang-packages CONFIG)`。

因此 release 公开是端口真正可用的前提，列为发布侧的一项前置条件。

## 7. 运行期部署面

前六节讲的是**资源**怎么到位。本节讲**二进制**怎么到位——两者是两条独立的线，任一条断了，
包都加载不了，而症状看起来一模一样。

### 7.1 wolf 自身安装出什么

`cmake --install` 落七个插件（各带 `plugin.json`）加一个库：

```
lib/libsynthrt-wolf.so                                  # 注册 linguist 类别
lib/wolf/plugins/linguistproviders/wolf/                # WolfLinguistProvider
lib/wolf/plugins/inferenceinterpreters/{chain,onset,s2p}/       # 无额外依赖
lib/wolf/plugins/inferenceinterpreters/pinyin/          # 需 libcpp-pinyin
lib/wolf/plugins/inferenceinterpreters/lua/             # 需 libluajit
lib/wolf/plugins/inferenceinterpreters/multig2p/        # 需 dsinfer + ONNX 驱动
```

仅测试构建的桩（`inferenceinterpreters/stub`、`singerproviders/stub`）标了 `NO_INSTALL`，
**不进安装树**。

### 7.2 各插件的运行期依赖

实测 `readelf -d`（Linux；其他平台同构）：

| 插件 | 除 wolf 自身外还需要 | 缺失后果 |
| :-- | :-- | :-- |
| `chain` `onset` `s2p` | 无 | —— |
| `pinyin` | `libcpp-pinyin` | 插件加载不了 ⇒ `algo-pinyin` 无提供者 ⇒ `wolf/g2p-pinyin` 在 Probe 阶段失败 ⇒ **cmn 与 yue 一起不可用** |
| `lua` | `libluajit-5.1` | `lua` 变体无提供者（当前无生产资源使用，实际影响为零） |
| `multig2p` | `libsynthrt-dsinfer` | `multig2p-onnx` 无提供者 ⇒ **九个 `pipe-chain` 语言包一起不可用** |
| `libsynthrt-wolf` | `libre2`、`libblake3` | wolf 本身起不来 |

**依赖缺失与资源缺失在现象上不可分**——两者都表现为「包加载失败」。诊断上的区别是：资源缺失
报「找不到 Package」，提供者缺失报 `FeatureNotSupported`「找不到提供者」（spec 2.4:576、:590）。

> **wolf 不靠链接器选项，靠一次显式调用**：链接 wolf 的意义在于那次 main 之前的静态注册（README
> 的中心论断），而 ELF 链接器默认会把这种「没有符号引用」的依赖丢掉（MSVC 链接器同样会丢掉每个
> import library）。`src/lib/CMakeLists.txt` 因此不向使用者的链接行推链接器选项，而是导出
> `wolf::linkLinguistCategory()`（`include/wolf/Linguist/LinguistContrib.h:25`）：只想要类别、
> 不引用任何 wolf 符号的宿主在构造第一个 `SynthUnit` 之前调用它一次即可，函数体为空，被命名就是
> 它的全部职责（README「Why wolf must be linked, not loaded」）。**仓库里没有
> `INTERFACE "LINKER:--no-as-needed"` 这行接线**，链接期丢了符号时按这条排查。

### 7.3 ONNX 驱动的三段式部署

`multig2p-onnx` 是唯一需要三样东西各自到位的变体：

| 件 | 来处 | 谁负责 |
| :-- | :-- | :-- |
| `libsynthrt-dsinfer` | `synthrt-main[onnx]` | 链接期，随 wolf 插件解析 |
| 驱动插件 `libonnxdriver` | `synthrt-main[onnx]` 装到 `lib/plugins/dsinfer/inferencedrivers/onnx/` | **宿主**把 `inferencedrivers` 目录喂给 `ds::InferenceDriverFactory::setPluginPaths` |
| ONNX Runtime 动态库 | `onnxruntime-builds` 装到 `share/onnxruntime-builds/runtime/default/` | **宿主**把该目录作为 `DriverInitArgs::runtimePath` 传入 |

第三件是运行期 `dlopen`（`ORT_API_MANUAL_INIT`），**构建期不链接**，所以构建成功不代表运行期
能用。三件齐了宿主才注册 Runtime Service，模块才拿得到驱动（A36）。

任一件缺失的表现是统一的：包正常加载，逐词报 `DriverUnavailable`，由链上的兜底步接住。
**这是设计，不是故障**——见 A36 的归属分野。

### 7.4 cpp-pinyin 的词典不随二进制部署

cpp-pinyin 端口的 `usage` 写着「把 `dict/` 拷到可执行文件旁边」。**那条建议不适用于 wolf**：
词典是 `wolf/g2p-pinyin` 包的内容，由 `configuration.dictRoot` 相对模块声明目录定位
（变体文档 §6.2），运行期不读 `share/cpp-pinyin/dict`。

端口那份词典只在**打包期**被 `scripts/convert-g2p-packages.py` 读一次，用来生成包（A30）。
照 upstream 的 usage 多拷一份到 bin 目录不会出错，但也毫无作用。

### 7.5 条件构建的可观察后果

`multig2p` 与 `lua` 按依赖存在与否条件构建（A40）。一次**最小构建**（无 dsinfer、无 LuaJIT）
与**完整构建**的差别只有两处：安装树少两个插件目录；测试少两个（15 对 17）。

**15 个包在两种构建下都加载通过**——因为最小构建里测试桩会补上缺失的三元组。但那只在测试
构建里成立：**发行的最小构建加载 `wolf/g2p-multi` 会失败**，桩不参与安装。

## 8. 开放问题

| # | 项 |
| :-- | :-- |
| P3 | 每种语言的默认 S2P 与 Onset 声明须新写（§3 第 4 条），是迁移的主要工作量 |
| P4 | lite 消费 wolf 语言包时的端口重复问题（自建 / 引用本仓 overlay / 提升到共享 overlay）——归 lite 侧决策 |
| P5 | `.dspk` 单文件形态——待 main 支持解压后再议 |
| P6 | 旧栈声库 manifest 的 `g2pPackageVersion` / `g2pPackages` 字段在迁移时的映射 |
| P7 | `deu` / `fra` / `spa` / `rus` / `fil` 五种的 `scheme` 待定：`ds` 不符合 §2.1.1，须按第 3 档造名（§2.2 已给逐套音素清点）。`por` / `kor` / `ita` 已定 |
| P8 | release 何时公开——端口的下载路径在此之前不可用（§6.7），当前经 `WOLF_LANG_PACKAGES_SOURCE` 绕开 |
