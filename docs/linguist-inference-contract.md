# wolf 链推理契约（L3）

本文钉语言链上三份推理契约的 Level 1：**G2P**（歌词 → 发音）、**S2P**（发音 → 音素序列）、
**Onset**（音素 → onset 标记）。

上位规范 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)；
组合规则与语言身份见 [linguist-domain-contract.md](linguist-domain-contract.md)；
执行体树、任务与并发归 [linguist-runtime.md](linguist-runtime.md)；
`configuration` 键汇与资源格式归 [linguist-variants.md](linguist-variants.md)。

**发布状态**：Level 1 尚未对外发布（口径同语言域契约）。

| Interface | Level | 变体举例 | 说明 |
| :-- | --: | :-- | :-- |
| `org.openvpi.wolf.inference.G2P` | 1 | `pipe-chain` / `algo-pinyin` | 歌词 → 发音 |
| `org.openvpi.wolf.inference.S2P` | 1 | `dict` / `direct` / `mapping` / `lua` | 发音 → 音素序列 |
| `org.openvpi.wolf.inference.Onset` | 1 | `rule` / `lua` | 音素 → onset 位置标记 |

> **命名对照（信息性）**：外部生态的用词与本契约族呈镜像关系——外部所谓 g2p
> （g2p_en、OpenUtau `G2pPack`、MFA `mfa g2p`）多指「词 → 发音」的模型/词典**后端**，在本
> 契约族中是 `pipe-chain` 变体的内部事务；外部所谓 phonemizer（OpenUtau、Coqui、Piper）才
> 对应本契约族的 **G2P** 宿主面。

---

## 1. 类别归属与插件形态

三份契约的模块是 **synthrt 内置 `inference` 类别**的贡献，不是 wolf 自有类别。因此：

- 解释器必须派生自 `srt::InferenceInterpreter`
  （`synthrt/include/synthrt/SVS/InferenceInterpreter.h`）；
- 插件必须嵌入 synthrt 的推理插件 IID **`org.openvpi.synthrt.plugin.InferenceInterpreter`**
  （`synthrt/include/synthrt/SVS/InferenceInterpreterPlugin.h:11`），并落位于宿主为
  `inference` 类别配置的插件搜索路径；
- 执行体必须派生自 `srt::InferenceExecutive`；
- **wolf 只发布契约头文件**（`include/wolf/Api/Inferences/**`），不注册新模块类别、不新增
  宿主部署面、不拥有这些插件的 IID。

由此得到两条结构性后果：

1. `srt::InferenceExecutive` 是**单任务面**（`state()` / `stop()` / `waitForFinished()`，
   `quit()` / `wait()` 被 private-final），故**一个执行体只承载一个在飞任务**；并发靠多开
   执行体，不靠一个执行体开多个任务。详见运行时文档；
2. `srt::InferenceSpec::validateCompatibilityWith`
   （`synthrt/lib/SVS/InferenceContrib.cpp:79-86`）可用于跨模块相容性判定，先例为 dsinfer
   的 vocoder ↔ acoustic（`dsinfer/plugins/singerproviders/diffsinger/DiffSingerProvider.cpp:123-127`）。
   Level 1 **不**使用该钩子——链内相容性已由语言域契约 §5.3 的二元组命中在 Ready-2 覆盖。

---

## 2. 通用约定

### 2.1 `exports.languages`：共享形状

G2P 与 S2P 使用**同一个键、同一个形状**，语义互为对偶：

```json
"exports": { "languages": [ { "language": "cmn", "scheme": "pinyin" },
                            { "language": "yue", "scheme": "jyutping" } ] }
```

| | G2P | S2P |
| :-- | :-- | :-- |
| 语义 | 本模块**能产出**这些二元组的发音 | 本模块**能消费**这些二元组的发音 |

- 二元组的取值规则见语言域契约 §2（`language` 形如 `[a-z]{3}`，`scheme` 形如
  `[a-z0-9]+(-[a-z0-9]+)*`）；
- 数组元素必须互不重复；
- **可省略**。省略即放弃静态匹配保障：模块可加载，但语言域契约 §5.3 的命中判定对该侧跳过，
  宿主**应**告警；
- 命中判定为二元组整体恒等，**从不读贡献 ID**。

Onset 不涉及注音体系，不声明本键。

### 2.2 发音层的空格定界约定

空格是发音层的**保留定界符**：

> 含空格即视该串为空格定界的音素序列；无空格即单一待转换的发音单元。

该约定使 Pronunciation 层无需区分两种变量类型，G2P 的输出与 S2P 的输入同形对齐。发音串
不携带数值标注（如置信度）。

### 2.3 逐单元语义

三份契约的接口均**按批量受理**（一次调用处理一个有序序列），本文按单元语义描述。逐词失败
不中断批，词序位保留。

### 2.4 契约暴露面

契约暴露面只允许规范的 `(language, scheme)` 二元组。实现内部的子资源标识（如模型后端家族
内部的 langRef `eng/default`）**不出模块边界**：暴露层到内部子资源的映射由模块在自身
`configuration` 内声明并处理（见变体文档）。

### 2.5 机器可读 Schema

按 spec 2.4:596-602，各 (interface, level) 的 `exports` 与 `imports[].options` JSON Schema
随契约发布物落位于 wolf 文档侧。Schema 落位前以本文表格为准。

---

## 3. `org.openvpi.wolf.inference.G2P`

歌词文本 → 发音（grapheme-to-phoneme）。**宿主面主转换器**：直接占用语言的 `linguist/g2p`
名额，对外实现完整的 G2P Variables。

### 3.1 Exports

| name | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `languages` | array&lt;{language, scheme}&gt; | 否 | 本模块能产出的二元组集合（§2.1） |
| `symbols` | path \| array&lt;string&gt; | 否 | 本模块声明可能输出的**原子符号清单**；`openSet` 为假时即全集 |
| `openSet` | boolean | 否 | 缺省 `false`。为真即产出可能落在 `symbols` 之外（域契约 §4.0） |

`symbols` 的形态规则同语言 `phonemes`（写为路径时指向 `array<string>` JSON，元素非空且不
重复）。输出串含保留定界符（空格）时按 §2.2 拆分后计入；无空格时整串即一个符号（音节等
发音单元）。

省略 `symbols` 时可加载，但宿主无法对 G2P 产出与语言链末端音素做静态比对，**应告警**。
`symbols` 可为音节等非音素原子符号，与音素类导出集异类；Level 1 不为其规定与任何音素集的
跨键比对（见语言域契约 §12）。

### 3.2 Import options

**无。** Level 1 不定义 import `options` 词汇。

语言选定不经 import options，而由组合 provider 在创建执行体时经 `G2PRuntimeOptions` 注入
`(language, scheme)`——语言声明已经说明自己是哪个二元组，清单里不必重写一遍。

### 3.3 Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### 3.4 Variables

#### 3.4.1 能力边界

输入是**有序的词列表**。句级语境参与的多音字消歧 / 同形异读属后续 Level 演进（部分变体已
实现，但不作能力承诺）。

分界判据：解释器在多轮调用间维护会话（滚动上下文、修正窗重发）属**实现自由**——只要逐单元
调用形状与本节词汇表、定界约定不变，即不构成 Level 递增事由。一旦能力需要导入方或宿主可见
（如声明「按句喂词可获益」）或改变输入单元的语义，即按 spec 2.4:744-753 处理。

**执行体已绑定单一二元组**：一个 G2P 执行体由其创建时的 `G2PRuntimeOptions` 绑定到一个
`(language, scheme)`，其生命周期内不改变。因此 Level 1 的运行时 IO **不携带语言参数**，也
不存在运行时语言与绑定值不一致的判定分支。宿主需要多语言即多开执行体。

#### 3.4.2 输入合法性判定次序

依序判定、先到先生效，均未命中者正常受理：

1. `lyric` 为空串或仅含空白字符 → 该词按 `mode=skip` 产出；
2. 其余含空白字符（词内或首尾空白）→ `pronunciation` 写入原词透传，并携带非空 `error`
   显式上报（值为 `InvalidInput`；此时 `mode` 与 `candidates` 之值无定义）。

#### 3.4.3 词汇表

| variable | I/O | type | 说明 |
| :-: | :-: | :-: | :-- |
| `lyric` | input | string | 歌词单元（一个词/一个分片） |
| `pronunciation` | output | string | 主发音 |
| `candidates` | output | array&lt;string&gt; | 候选发音（多音），首个即主发音 |
| `mode` | output | enum | 结果来源：`convert` 转换 / `copy` 原词保留 / `skip` 空词跳过 |
| `error` | output | enum | 失败类型，值域见 §3.4.4 |
| `hitSource` | output | enum | 可选诊断：`dict` / `model` / `rule` / `fallback` |

- `error` **非空即失败**，词序位保留且 `mode`、`candidates` 之值无定义；成功时必须取空值。
  词级成败一律以本变量为准；失败词的处置（如原词保留）由宿主决定；
- `hitSource` 为可选诊断词：模块无法区分来源或 `mode` 非 `convert` 时省略，宿主不得依赖其
  存在；变体只可援引上述取值、不得自定义新值。`rule` 涵盖规则算法类变体与编排链规则步的
  推导产出。`mode=copy`（打标类原样保留）不是转换来源，`hitSource` 恒省略；`fallback` 专指
  `mode=convert` 的词经兜底步产出（含显式 `useOriginal` 的原词兜底——显式兜底 ≠ `mode=copy`）。

#### 3.4.4 `error` 值域

值域是契约词，与 `mode` 同属 (`interface`, `level`) 词汇。Level 1 固定六个取值：

| 值 | 含义 |
| :-- | :-- |
| `InvalidInput` | 输入非法 |
| `ModelInferenceFailed` | 后端推理失败 |
| `PhonemeGenerationFailed` | 转换/兜底仍未产生发音 |
| `DriverUnavailable` | 推理驱动不可用 |
| `NotInitialized` | 模块未初始化 |
| `UnknownError` | 其他未分类失败 |

变体只可援引上述取值、**不得自定义新值**；取值的增补属契约词汇表演进，按 spec 2.4:750-753
的 Level 判据处理。各取值的具体触发条件见变体文档。

#### 3.4.5 输出共现约束

适用于 `error` 为空的产出；`error` 非空时以 §3.4.3 首条为准。

| `mode` | 约束 |
| :-- | :-- |
| `skip` | `pronunciation` 与 `candidates` 均为空 |
| `copy` | `pronunciation` 为原词，**不得**携带 `candidates` |
| `convert` | `pronunciation` 即 `candidates` 的首个元素 |

---

## 4. `org.openvpi.wolf.inference.S2P`

发音字符串 → 音素序列（symbol-to-phoneme）。输入的多形态已由 §2.2 的空格定界约定消除。

> **预音素化用途**（宿主已有音素序列、仅需切分/映射）应绕开 G2P、直调本接口。

### 4.1 Exports

| name | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `languages` | array&lt;{language, scheme}&gt; | 否 | 本模块**能消费**的二元组集合（§2.1） |
| `phonemes` | path \| array&lt;string&gt; | 否 | 本模块声明可能产出的音素清单（**产出集**）；`openSet` 为假时即全集 |
| `openSet` | boolean | 否 | 缺省 `false`。为真即产出可能落在 `phonemes` 之外（域契约 §4.0） |

`phonemes` 的形态规则同语言 `phonemes`（保留音素不进清单；写为路径时指向 `array<string>`
JSON，元素非空且不重复）——与彼处不同，**本键可省略**。

- 省略 `phonemes` 时可加载，但宿主无法对本模块产出与语言链末端音素做静态比对，**应告警**；
- 各变体的推导口径见变体文档：`dict` / `mapping` 可由 `configuration` 资源静态推导，
  `direct` 开放无界、`lua` 不可静态推导——后两类由作者在声明中显式补齐或省略；
- **本键只服务于静态比对与告警**：比对方向为 S2P 产出集 ⊆ 语言链末端 `phonemes`（超出即
  告警），**一切比对结果均为警告级，不构成加载期失败事由**。

### 4.2 Import options

**无。**

### 4.3 Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### 4.4 Variables

| variable | I/O | type | 说明 |
| :-: | :-: | :-: | :-- |
| `pronunciation` | input | string | 发音字符串（G2P 的主发音） |
| `phonemes` | output | array&lt;string&gt; | 音素序列 |

`pronunciation` 沿用 §2.2 的空格定界约定：含空格即空格定界的待转换音素序列（逐段转换），
无空格即单一待转换发音。

#### 失败语义

Level 1 **不定义逐单元 error 通道**。可观察语义分两级：

- **逐单元未命中**：产出该单元的空音素序列。**未命中不是失败**；各变体的未命中口径见
  变体文档；
- **整批硬失败**（驱动不可用、资源失效、脚本崩溃等）：以执行体调用的 `Expected` 错误返回，
  不伪装成逐单元空产出。

实现应使真正的异常路径走整批失败，使宿主可区分「正常未命中」与「异常静默」。

本接口逐调用 IO 无跨词、跨调用语境；会话内上下文（如联诵缓存）属变体实现自由，不进入
Level 1 词汇。

---

## 5. `org.openvpi.wolf.inference.Onset`

音素序列 → onset 位置标记。

### 5.1 Exports

| name | type | 必选 | 说明 |
| :-: | :-: | :-: | :-- |
| `knownPhonemes` | path \| array&lt;string&gt; | 否 | 本模块可识别并参与匹配的音素集（**输入识别集**） |

形态与形状规则同 S2P `phonemes`（本键同样可省略；省略时宿主无法进行覆盖比对，**应告警**）。

**注意方向**：本键是**识别集**，与 S2P `phonemes` 的**产出集**不同义——比对逻辑不得混用两键。
链上的合理关系是「链上实际音素均被本模块规则覆盖」，而不是两键之间的包含式。

推导与比对口径：

- 各变体推导口径见变体文档。`rule` 变体取 `phonemeTypes` 中**类型名被至少一条规则引用**的
  键集，并并入 pattern 字面音素段；
- **Onset 不设 `openSet`**：`knownPhonemes` 从一开始就规定为下界，加一个「可能超出」的位是重复
  声明同一件事。开放位只加在**自称是全集**的三处（域契约 `phonemes`、G2P `symbols`、
  S2P `phonemes`）；
- 通配 `"*"` 段功能上覆盖任意输入，故派生集是覆盖面的**下界**而非全集。未列入音素仍可能被
  良定义处理，**识别集低估不构成缺陷**；
- 输入中未被覆盖的位置输出 `false` 是本契约的**目标合法语义**，不是错误；
- **一切比对结果均为警告级**，不构成加载期失败事由。

### 5.2 Import options

**无。**

### 5.3 Configuration

由 `variant` 全权规定；各变体键汇与资源格式见变体文档，其中路径以本模块声明文件目录为基。

### 5.4 Variables

| variable | I/O | type | 说明 |
| :-: | :-: | :-: | :-- |
| `phonemes` | input | array&lt;string&gt; | 音素序列（S2P 的输出） |
| `onsets` | output | array&lt;boolean&gt; | 与输入**等长**的 onset 标记 |

#### 失败语义

口径同 S2P §4.4：Level 1 不定义逐单元 error 通道；未覆盖位置输出 `false`；整批硬失败以
`Expected` 错误返回。输入形状与合法性校验语义由变体文档承接。

本接口逐调用 IO 无跨词、跨调用语境。

---

## 6. C++ 契约面

wolf 发布的契约头位于 `include/wolf/Api/Inferences/<Contract>/1/`，另有
`include/wolf/Api/Inferences/Common/1/CommonApiL1.h` 承载共享类型。形制对位 dsinfer 的
`DurationApiL1.h`。

### 6.1 共享类型

```cpp
namespace wolf::Api::Common::L1 {
    /// 语言句柄与注音体系构成的二元组，本契约族唯一的语言匹配键。
    struct LanguageScheme {
        std::string language;   // ISO 639-3, [a-z]{3}
        std::string scheme;     // [a-z0-9]+(-[a-z0-9]+)*
    };
    bool operator==(const LanguageScheme &a, const LanguageScheme &b) noexcept;
}
```

### 6.2 每份契约的 payload 体系

以 G2P 为例，S2P / Onset 同构：

| 类 | 基类 | 承载 |
| :-- | :-- | :-- |
| `G2PExports` | `srt::ContribExports` | §3.1 |
| `G2PImportOptions` | `srt::ContribImportOptions` | 空（§3.2） |
| `G2PRuntimeOptions` | `srt::InferenceRuntimeOptions` | 绑定 `(language, scheme)` |
| `G2PInitArgs` | `srt::InferenceInitArgs` | 变体无关的初始化参数 |
| `G2PStartInput` | `srt::TaskStartInput` | 一批 `lyric` |
| `G2PResult` | `srt::TaskResult` | 一批逐词输出（§3.4.3） |
| `G2PExecutive` | `srt::InferenceExecutive` | `initialize` / `start` / `startAsync` |

### 6.3 `RuntimeOptions` 必须携带目标变体（硬约束）

`srt::ContribSpecPayload` 的三元组在创建执行体时被逐格比对：

```cpp
// synthrt/lib/SVS/InferenceContrib.cpp:25-30
if (runtimeOptions.interface() != target.interface() ||
    runtimeOptions.variant()   != target.variant()   ||   // ← 变体也比
    runtimeOptions.level()     != target.level()) { … }
```

G2P / S2P / Onset 都是**多变体契约**，因此其 `RuntimeOptions` 类
**不得**像 dsinfer 单变体契约那样硬编码 `API_VARIANT`：

```cpp
class G2PRuntimeOptions : public srt::InferenceRuntimeOptions {
public:
    // 变体由调用方按目标 spec 的 variant() 传入，不可硬编码。
    explicit G2PRuntimeOptions(std::string variant)
        : InferenceRuntimeOptions(API_INTERFACE, std::move(variant), API_LEVEL) {}

    Common::L1::LanguageScheme binding;   // 该执行体绑定的二元组
};
```

调用方（组合 provider）从 import 的目标 spec 读 `variant()` 构造该对象。这是框架事实，
不是风格选择。

### 6.4 与运行时的边界

本文只钉**每次调用的 IO 形状与词汇**。任务的创建、并发、取消、停等、诊断通道与降级模型
全部归 [linguist-runtime.md](linguist-runtime.md)。

---

## 7. 变体治理

- 变体名属实现方命名空间。本契约族收录变体由 wolf 维护：G2P 宿主面保留 `pipe-` / `algo-`
  类前缀；全部裸变体名为 wolf 官方保留；第三方按 spec 2.4:562 使用反向域名变体
  （如 `com.vendor.myengine`）；
- 框架按 (`interface`, `level`, `variant`) 三元组全匹配选择 provider，`configuration`
  **不参与**选择（spec 2.4:576、:590）。选择按「目录顺序 + 目录内文件名顺序」形成的全序扫描，
  首个全匹配者当选，三元组命不中即「找不到提供者」加载失败
  （`PackageLoader.cpp:769-774`），**不存在运行期再分派**；
- 「同一变体名只许一个 provider 插件承载」是 wolf 对收录变体的**收录纪律**，使公共包的
  provider 选择结果确定；并非框架约束；
- 变体可随时新增，新增不触碰本契约（spec 2.4:769）。

### 7.1 何时新增变体、何时递增 Level、何时另立 interface

按 spec 2.4:744-753 的判据：

| 情形 | 处置 |
| :-- | :-- |
| 只有解释器关心 | 写入 `configuration`，不动 Level，必要时升资源 `formatVersion` |
| 导入方需要，且现有词汇表说得圆 | 用现有词汇表表达，不动 Level |
| 导入方需要，但现有词汇表缺词 | **递增 Level**，为该契约补充词汇 |
| 输入输出已根本不同 | **另起一份 `interface`** |

模型后端（词 → 发音的张量模型）与词典查询在 Level 1 **不单独立契约**：它们是 `pipe-chain`
变体的内部事务，经该变体自己的 `imports` 与 `configuration` 表达。需要跨实现互操作时按上表
末两行处理。
