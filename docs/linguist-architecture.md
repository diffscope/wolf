# wolf 语言域架构（索引）

本文是 wolf 语言域设计的**总架构与索引**：规定分层、决策与跨层不变量；各层的完整规则、
词汇表与错误语义在分层文档中展开。

## 本文地位与可信源顺序

冲突时按下列顺序裁决，**上位者恒胜**：

1. **[DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)**——上位规范；
2. **synthrt / dsinfer 实际代码**——框架能力的唯一事实来源。规范未描述而代码提供的能力，
   使用前必须在本文显式登记（见《依赖的框架超集》）；
3. **本文**——分层与跨层决策；
4. **分层文档**——层内规则全集。

原 `linguist-*-draft.md` 五份草案已在迁移完成后删除，其全部实质内容已并入本文档集（去向见
《文档地图》的迁移映射），论证过程可经 git 历史查阅。**这五份草案不再是有效的引用目标。**

## 锚点约定

| 树 | 基准 | 引用写法 |
| :-- | :-- | :-- |
| spec 2.4 | 本仓 `docs/ds-spec-2.4.md` | `spec 2.4:<行号>` |
| synthrt / dsinfer | synthrt 的 main 分支 | `synthrt/lib/Core/PackageLoader.cpp:38-56` |
| wolf | wolf 的 `linguistic-level-1-v2` 分支 | `src/lib/Linguist/LinguistContrib.cpp:172-173`（类别注册） |
| stdcorelib | vcpkg 端口，随版本变化 | 只记录文件与结论，不作固定锚点 |

synthrt 锚点一律带树内前缀（`synthrt/` 或 `dsinfer/`），两棵树位于同一仓库。代码行号为撰写时的
观察值，随分支演进可能漂移，以文件与符号为准。

---

## 1. 框架硬约束

下列事实**由代码确认**，不属于设计选择；改变这些事实需要修改框架。

### 1.1 可依赖的不变量

| # | 事实 | 锚点 |
| :-- | :-- | :-- |
| F1 | `inference` 是 synthrt 内置类别，插件 IID `org.openvpi.synthrt.plugin.InferenceInterpreter`，解释器基类 `srt::InferenceInterpreter` | `synthrt/lib/SVS/InferenceContrib.cpp:111,147`、`InferenceInterpreterPlugin.h:11` |
| F2 | `role` 在模块内唯一由框架强制 | spec 2.4:634；`synthrt/lib/Core/PackageLoader.cpp:1365-1369`、`ContribCategory_p.h::addImport` |
| F3 | `ContribExecutive::createChild(role, runtimeOptions)` 在**自身 spec 的 imports** 上按 role 取执行工厂并 adopt；`adoptChild` 不限制同 role 多子 | `synthrt/lib/Core/ContribExecutive.cpp:71-129` |
| F4 | 类别的 `createSpec` 在 **Probe** 执行，早于 provider 选择；`ContribCreateContext` 已可见 `imports()` | `PackageLoader.cpp:1371-1377`、`ContribCategory.h:60` |
| F5 | `vars` 由框架展开后**从声明对象中剥离**，类别与解释器都看不到它 | `PackageLoader.cpp:406` |
| F6 | ImportBinding 只从 `imports` 数组产生；数组之外的 ModuleReference 不产生任何绑定 | `PackageLoader.cpp:894-917` |
| F7 | `JsonObject = std::map<std::string, Value, std::less<>>`——成员按键排序，**声明顺序丢失** | stdcorelib `support/json.h:68` |
| F8 | `ContribImportValidator` 由**已加载的全部解释器**平铺跑遍事务内每个 spec | `PackageLoader.cpp:924-944` |

### 1.2 强加的设计约束

| # | 约束 | 后果 |
| :-- | :-- | :-- |
| C1 | `RuntimeOptions` 三元组必须等于**目标 spec** 三元组 | `G2PRuntimeOptions` 必须由构造参数接收目标变体；不得沿用 dsinfer 单变体契约的硬编码写法（`synthrt/lib/SVS/InferenceContrib.cpp:25-30`） |
| C2 | `srt::InferenceExecutive` 是单任务面（`state()/stop()/waitForFinished()`，`quit()/wait()` private-final） | 并发通过创建多个执行体实现，而非在一个执行体上运行多个任务；**资源缓存因此是架构必需件而非优化**（`InferenceExecutive.h:39-50`；缓存的键、归属与生命周期见 [linguist-resource-cache.md](linguist-resource-cache.md)） |
| C3 | validator 仅来自**已加载**解释器 | 必须始终成立的规则不能只放在 Ready-2；依赖闭包内无 linguist 贡献时，wolf 的校验不会执行（`ContribPluginFactory.cpp:128-131`） |

### 1.3 依赖的框架超集

下列能力**框架提供而 spec 2.4 未描述**，本架构显式依赖它们：

- **跨类别 imports 校验**：spec 2.4:426 只授权「importing module 的 provider」校验 imports 集合，
  而框架的 `ContribImportValidator` 允许任何已加载解释器校验任何 spec（F8）。wolf 据此校验
  歌手声明。spec 未禁止额外校验（:444 授权 imports 集合校验失败即整次失败），故不构成违规，
  但受 C3 的盲区约束。

---

## 2. 层栈

```
L6  会话层          就绪态 / 预热 / 负缓存 / 执行体池 / 保留词      wolf 独占
────────────────────────────────────────────────────────────────
L5  宿主接入层      解析次序 / 缺省语言 / 缺依赖呈现 / 诊断        宿主可自定
────────────────────────────────────────────────────────────────
L4  运行时装配层    extension 挂载 / 执行体树 / 二元组注入 / 停等   wolf 独占
────────────────────────────────────────────────────────────────
L3  链推理契约层    G2P / S2P / Onset 的 IO 词汇与 exports         变体可换实现
────────────────────────────────────────────────────────────────
L2  语言组合契约层  role 基数 / 二元组命中 / phonemes / 歌手侧映射  变体可换实现
────────────────────────────────────────────────────────────────
L1  语言域身份层    linguist 类别 / (language, scheme) / ID 语法    wolf 独占
────────────────────────────────────────────────────────────────
L0  框架不变量      role 唯一 / 三元组 / 事务四相 / 监督树 / vars    synthrt，只可依赖
```

> **L6 位于 L5 之上**：L6 是宿主**持有**的对象，不是宿主**实现**的层。L5 仍是宿主自定的策略面
> （解析次序、呈现）；每个宿主都需要实现且只有一个正确答案的部分由 L6 收归语言域内部。详见
> [linguist-session.md](linguist-session.md)。

以下两个正交平面不属于主栈，各自独立演进：

- **变体面**：`configuration` 键汇与资源格式，含 `formatVersion` 自验；
- **发布面**：包身份、`[compatVersion, version]` 区间、分发渠道与更新纪律。

### 分层边界

| 层 | 拥有 | **不得**触碰 |
| :-- | :-- | :-- |
| L1 | 类别注册、条目 schema、`language`/`scheme` 形状、ID 语法、DataOnly 身份面 | `exports.phonemes` 语义、任何 `configuration`、任何推理契约 |
| L2 | role 存在性与目标契约、二元组命中、`exports.phonemes`、`configuration` 空、歌手侧语言映射语义 | 基数上界（归 L0）、任务并发形状（归 L4） |
| L3 | 逐单元 IO 词汇、`error` 值域、共现约束、`exports` 对齐面 | 任务并发形状（归 L4）、`configuration`（归变体面） |
| L4 | extension 挂载、执行体树、运行时选项注入、停等聚合 | 任何加载期失败事由（只产诊断） |
| L5 | 解析次序、策略、呈现 | 任何规范性要求（全层为建议级） |
| L6 | 歌手→语言快照、就绪态与负缓存、执行体池、保留词与空词处理、取消句柄 | `SynthUnit` 的所有权、项目模型（连音 / 切分 / 时值）、第二套转换数据模型、逐词结果记忆化 |

---

## 3. 决策台账

全部 A1–A80 的编号、现行结论与论证过程见 [linguist-decisions.md](linguist-decisions.md)。**本文不维护
决策副本**：本节的表只列跨层高频引用的五条结论，便于在阅读上文各节时就近对照；取舍与依据一律以台账
为准，含作废与取代关系（台账中以删除线标注）。

| # | 跨层高频结论 |
| :-- | :-- |
| A3 | 语言身份 = 声明根上的 `(language, scheme)` 二元组；**贡献 ID 从不被解析** |
| A14 | 并发通过**创建多个执行体**实现，一条链对应一棵子树与一个在飞任务（C2 的直接推论） |
| A16 | 运行时 IO **不携带语言参数**；二元组在执行体创建时经 `RuntimeOptions` 绑定，生命周期内不变 |
| A69 | **不实现包级能力晚绑定**：绑定由 `imports[].ref` 在加载期完全确定，安装后即可用是本层必须保持的性质 |
| A70 | 降级只保留链级（`linguist/s2p` 可缺席）与音素级（覆盖度只报告、不设门限）；例外是 `coverage == 0 && !openSet` 直接判为不可用 |

---

## 4. 符合性与改动清单

**结论：符合 spec 2.4。** synthrt 使用「个别必需字段」额度中的 2 项，其余为 wolf 侧的增补。

```
synthrt  synthrt/lib/SVS/SingerContrib.cpp:33-36   白名单 +2（languages, defaultLanguage）
         synthrt/lib/SVS/SingerContrib.cpp         createSpec 解析 + 三条结构校验
         synthrt/include/synthrt/SVS/SingerContrib.h   SingerSpec +2 成员 +2 访问器

wolf     src/lib/Linguist/LinguistContrib.cpp:56-62    白名单 +2（language, scheme）
         src/lib/Linguist/LinguistContrib.{h,cpp}      LinguistSpec +2 访问器、language/scheme 形状校验
         src/plugins/.../WolfLinguistProvider.cpp:95-160   validator 改读 languages 映射、增二元组命中
         src/plugins/.../WolfLinguistProvider.cpp:377-404  createExtensions 改挂载条件
         新文件                                        include/wolf/Api/Inferences/**（含 Common/1）
                                                       LinguistExecutive 具体实现类
                                                       wolf::ResourceCache
dsinfer  0 处
```

`WolfLinguistProvider::createExports` 为纯增量（现有逻辑不动）；`createConfiguration` 不改（A9）。

### 两处已知张力（记录在案）

1. **歌手侧校验依赖框架超集**（见 §1.3），并受 C3 盲区约束。
2. **spec 与实现对 Package 形态的规定不一致**：spec 2.4:59 定义 Package 为 `.dspk` ZIP，
   而 synthrt main 的加载器**只接受目录**，拒绝非目录路径（`PackageLoader.cpp:1059-1061`），
   搜索路径只枚举子目录（`:614`），main 也不再依赖任何解压库。发布物形态据此定为
   「解包即目录」（见 [linguist-distribution.md](linguist-distribution.md)）。

---

## 5. 声明形态总览

```json
// linguists/cmn-pinyin/linguist.json                                       ← L1 + L2
{ "interface": "org.openvpi.wolf.linguist.WolfLinguist", "level": 1, "variant": "wolf",
  "language": "cmn", "scheme": "pinyin",
  "exports": { "phonemes": "./phonemes.json" },
  "configuration": {},
  "imports": [ { "role": "linguist/g2p",   "ref": ":inference/g2p" },
               { "role": "linguist/s2p",   "ref": ":inference/s2p" },
               { "role": "linguist/onset", "ref": ":inference/onset" } ] }
```

```json
// inferences/g2p/inference.json                                            ← L3
{ "interface": "org.openvpi.wolf.inference.G2P", "level": 1, "variant": "algo-pinyin",
  "exports": { "languages": [ { "language": "cmn", "scheme": "pinyin" },
                              { "language": "yue", "scheme": "jyutping" } ],
               "symbols": "./symbols.json" },
  "configuration": { "scheme": "mandarin", "dictPath": "./dict" } }
```

```json
// singers/singer1/singer.json                                              ← L2 歌手侧
{ "interface": "org.openvpi.dsinfer.singer.DiffSinger", "level": 1, "variant": "openvpi",
  "languages": { "cmn": "lang/mandarin", "jpn": "lang/japanese" },
  "defaultLanguage": "cmn",
  "configuration": { "dict": "./dict.txt" },
  "imports": [ { "role": "singer/acoustic", "ref": ":inference/acoustic" },
               { "role": "lang/mandarin",   "ref": ":linguist/cmn-pinyin" },
               { "role": "lang/japanese",   "ref": "wolf/lang-jpn:linguist/jpn-romaji" } ] }
```

由 ISO 代码到实际 linguist 贡献 ID 的解析路径：
`"cmn"` → role `lang/mandarin` → 该 import 的 `locator().contributionId()` = `cmn-pinyin`，
全程在 DataOnly 模式下可读。

---

## 6. 校验矩阵

任何校验失败均使整次加载失败（spec 2.4:444）。框架没有告警通道，提示级检查全部由编辑期 lint 执行。

本表是校验落点的**唯一权威表**：分层文档只写各自小节的判定规则，不再另立整表。L1 / L2 各行的
契约归属（哪条规则写在哪个小节）见 [linguist-domain-contract.md](linguist-domain-contract.md) §11，
Ready-2 的盲区见该文 §11.1。

| 相 | 执行者 | 校验内容 | 层 | 框架执行点 |
| :-- | :-- | :-- | :-- | :-- |
| Probe | 框架 | `role` 在模块内唯一 ⇒ linguist 三个固定 role 的**基数上界结构性成立** | L0 | `synthrt/lib/Core/PackageLoader.cpp:1365-1369` |
| Probe | 框架 | `languages` 映射键唯一 ⇒ **每语言至多一个语言导入** | L0 | 同上 |
| Probe | `LinguistCategory::createSpec` | 条目 schema；`language` 形如 `[a-z]{3}`；`scheme` 形状 | L1 | `synthrt/lib/Core/PackageLoader.cpp:1377` |
| Probe | `SingerCategory::createSpec` | `languages` 形状；每个值命中本声明某条 import role；`defaultLanguage` 是其键 | L1 | 同上 |
| Acquire | 组合 provider | `exports.phonemes` 形状；`configuration` 为空 object | L2 | `synthrt/lib/Core/PackageLoader.cpp:833/845` |
| Acquire | 三推理 provider | 各自 `exports`（含 `languages` 形状）与 `configuration` | L3 / 变体面 | 同上（每个模块各经同一对钩子） |
| Ready-1 | 被引目标 provider | 单条 import `options` | L2 / L3 | `synthrt/lib/Core/PackageLoader.cpp:873-893` |
| Ready-2 | wolf validator | linguist：`linguist/g2p` `linguist/s2p` 存在、目标 interface 相符、**自身二元组命中两者的 `exports.languages`**；singer：映射值指向的 import 目标类别为 `linguist`、**目标 `language` == 映射键** | L2 | `synthrt/lib/Core/PackageLoader.cpp:924-944` |
| Ready-3 | 组合 provider | pipeline extension 挂载 | L4 | `synthrt/lib/Core/PackageLoader.cpp:1004-1007` |
| 运行时 | 执行体 | 未映射的语言标签、IO 形状 | L4 | 不适用（不在加载事务内） |
| 打包 / 编辑期 | lint | `phonemes` ⊆ 各 stage 音素表交集；S2P 产出集 ⊆ 语言 `phonemes`；Onset 覆盖；未被映射引用的 linguist import；贡献 ID 书写惯例 | — | 不适用（由 lint 脚本执行） |

---

## 7. 执行体树

树形图与逐层机制见 [linguist-runtime.md](linguist-runtime.md) §3「执行体监督树」；本节只保留
跨层不变量：

- **一条链对应一棵子树与一个在飞任务**（C2）。宿主的 k 路并发即调用 `createLinguist` k 次；
- 跨子树的资源共享由 `wolf::ResourceCache` 承担（C2 的直接推论；键、归属与生命周期见
  [linguist-resource-cache.md](linguist-resource-cache.md)）。

---

## 8. 文档地图

| 文档 | 覆盖 | 状态 |
| :-- | :-- | :-- |
| 本文 | 索引、层栈、跨层不变量、决策结论 | 现行 |
| [linguist-domain-contract.md](linguist-domain-contract.md) | **L1 + L2**：类别、身份、组合、歌手侧映射、裁决 | 现行 |
| [linguist-inference-contract.md](linguist-inference-contract.md) | **L3**：G2P / S2P / Onset 的 IO 词汇与 exports | 现行 |
| [linguist-runtime.md](linguist-runtime.md) | **L4 + L5**：执行体树、任务、诊断、降级、宿主接入 | 现行 |
| [linguist-variants.md](linguist-variants.md) | 变体面：`configuration` 键汇与资源格式 | 现行 |
| [linguist-distribution.md](linguist-distribution.md) | 发布面：包身份、版本兼容模型、更新纪律 | 现行 |
| [linguist-resource-cache.md](linguist-resource-cache.md) | 资源缓存（**架构必需件**，非优化） | 现行 |
| [plugin-internals.md](plugin-internals.md) | **插件内部**：骨架、版本与世代、解析与诊断纪律、契约身份、演进检查清单 | 现行（V1 待评审） |
| [linguist-decisions.md](linguist-decisions.md) | 决策台账（全部 A / D 编号与论证） | 现行 |
| [linguist-implementation-plan.md](linguist-implementation-plan.md) | 实施顺序、里程碑与验收判据 | 现行 |

### 旧 draft 迁移映射（存档）

五份草案已删除，去向均已落实：`linguist-level-1-draft.md` → [linguist-domain-contract.md](linguist-domain-contract.md)
（§前置说明 / `linguist` 类别 / WolfLinguist / Singer 侧配套）与 [linguist-inference-contract.md](linguist-inference-contract.md)
（G2P / S2P / Onset 三章），其中 G2PModel / DictQuery 两章作废（A2）、§公共语言包 → [linguist-distribution.md](linguist-distribution.md)；
`linguist-runtime-api-draft.md`（§3.2 多任务形状按 C2 重写）→ [linguist-runtime.md](linguist-runtime.md)；
`linguist-g2p-variants-wolf-draft.md` §1–§7 → [linguist-variants.md](linguist-variants.md)、§8 → 决策台账；
`linguist-g2p-package-distribution-draft.md` → [linguist-distribution.md](linguist-distribution.md)；
`linguist-g2p-resource-cache-draft.md` → [linguist-resource-cache.md](linguist-resource-cache.md)。
最终版本见删除前的提交。
