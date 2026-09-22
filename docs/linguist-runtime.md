# wolf 语言域运行时与宿主接入（L4 + L5）

本文钉**运行时装配层（L4）**与**宿主接入层（L5）**：pipeline extension 的挂载与取用、执行体
监督树、任务与并发模型、失败与降级语义、诊断通道，以及宿主的解析次序与呈现策略。

上位规范 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)；
声明形态与加载期裁决见 [linguist-domain-contract.md](linguist-domain-contract.md)；
逐调用 IO 词汇见 [linguist-inference-contract.md](linguist-inference-contract.md)。

**词汇分层**：**L4 为规范性**——宿主依赖其调用面与可观察语义；**L5 为建议级**——宿主可自定
策略，本文只给推荐做法与其依据。

---

# L4 —— 运行时装配层

## 1. 宿主初始化序列

每步一步，不得换序：

1. **链接 wolf**（编译期）。wolf 是库不是程序。链接即生效：库内静态注册对象在 `main` 之前
   把 `linguist` 类别连同 provider factory 挂入 synthrt 类别注册表
   （`src/lib/Linguist/LinguistContrib.cpp:94-95`）。
   **宿主不链接 wolf ⇒ 无 `linguist` 类别 ⇒ 含语言贡献的包整体被拒**
   （`synthrt/lib/Core/PackageLoader.cpp:1210-1212`）；
2. **构造 `SynthUnit`**。构造即收集注册表全部类别（`synthrt/lib/Core/SynthUnit.cpp:14-31`），
   先于任何包解析（spec 2.4:250）；
3. **配置路径**：
   - `setPackagePaths`——包搜索路径。**公共语言包路径须排在声库内置包路径之前**（依据见 §9.3）；
   - `setPluginPaths(category, paths)`——按类别配置。语言域需要两条：
     `linguist`（wolf 的组合 provider 插件）与 `inference`（G2P / S2P / Onset 解释器插件）。
     后者与 dsinfer 的推理插件共用同一类别与同一 IID，落位于同一批目录；
4. **加载包**：`openPackage(path, Load)`。收据为 `PackageHandle`——生命周期即运行时资源归属，
   句柄归零即包释放。`DataOnly` 只读清单、不碰运行时（§9.1 用它做缺依赖预检）。

## 2. Pipeline extension

### 2.1 挂载条件

wolf 为满足下列条件的 Singer spec 注册 pipeline extension：

> 歌手声明的 `languages` 映射非空，且其每个值所指的 import 目标类别为 `linguist`。

trait ID 为 **`org.openvpi.wolf.extension.LinguistPipeline`**，经
`ContribSpecExtensionTraits<SingerSpec, WolfPipelineExecutive>` 特化注册。

挂载发生在加载事务的 **Ready-3**（`PackageLoader.cpp:1004-1007` 的
`attachSpecExtensions`），此时全部 import binding 与执行工厂均已就绪。框架对事务内每个新
spec 平铺调用**全部已加载解释器**的 `createExtensions`（`PackageLoader.cpp:946-990`），因此
wolf 得以为「由 dsinfer 解释的歌手」挂载自己的扩展。

> 挂载判定**不看 role 名**。语言身份由 `languages` 映射承载，role 是自由命名的本地槽位
> （语言域契约 §10.2）。

### 2.2 取用

```cpp
auto *base = srt::ContribSpecExtension::findFromSpec<
                 wolf::Api::Linguist::L1::WolfPipelineExecutive>(singerSpec);
auto *ext  = base ? base->as<wolf::Api::Linguist::L1::WolfPipelineExtension>() : nullptr;
```

`findFromSpec` 返回基类指针（`synthrt/include/synthrt/Core/ContribSpecExtension.h:30-35`），
取用后下转。**无此 extension ⇒ 该歌手未声明任何语言导入，语言功能整体不可用**——这是正常
状态，不是错误（见 §8.1）。

extension 的语言集合在 Package Commit 后**不再变化**，可全程缓存，仅在包加载/卸载事件时失效。

### 2.3 方法面

```cpp
class WolfPipelineExtension : public srt::SingerPipelineExtension {
public:
    /// 该歌手声明的全部语言句柄，按 languages 映射的键。
    virtual const std::vector<std::string> &languages() const = 0;

    /// 歌手声明的缺省语言句柄；未声明时为空串。
    virtual const std::string &defaultLanguage() const = 0;

    /// 某语言句柄对应的 linguist 贡献定位符，用于呈现与诊断。
    virtual const srt::ContribLocator *locate(std::string_view language) const = 0;

    srt::Expected<std::unique_ptr<srt::SingerPipelineExecutive>>
        createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override;
};
```

## 3. 执行体监督树

```
SingerSpec ──extension──▶ WolfPipelineExecutive
                              └─ createChild(languages[tag]) ─▶ LinguistExecutive
                                     ├─ createChild("linguist/g2p")   ─▶ G2PExecutive    ┐
                                     ├─ createChild("linguist/s2p")   ─▶ S2PExecutive    ├ srt::InferenceExecutive
                                     └─ createChild("linguist/onset") ─▶ OnsetExecutive  ┘
```

全程经 `ContribExecutive::createChild(role, runtimeOptions)` 完成
（`synthrt/lib/Core/ContribExecutive.cpp:111-129`），**零框架改动**。每一跳的机制相同：
按 role 在**本执行体自身 spec 的 imports** 中取执行工厂，创建后 `adoptChild` 纳入监督树。

- 第一跳的 role 来自歌手 `languages` 映射的值，工厂由 `linguist` 类别提供；
- 第二跳的 role 是 `linguist/g2p` 等固定名，工厂由 `inference` 类别提供。

### 3.1 二元组注入

`LinguistExecutive` 创建三个子执行体时，把**自身 spec 的 `(language, scheme)`** 写入各自的
`RuntimeOptions`。变体一格必须取自目标 spec 的 `variant()`，不可硬编码——见链推理契约 §6.3。

```cpp
const auto &target = spec().findImport("linguist/g2p")->binding()->target();
Api::G2P::L1::G2PRuntimeOptions options(target.variant());
options.binding = { spec().language(), spec().scheme() };
auto child = createChild("linguist/g2p", options);
```

### 3.2 生命周期

- **所有权**：子执行体归父执行体所有。宿主可提前 `delete` 子执行体，它会先从父的子表中摘除
  （`ContribExecutive.cpp:27-35`）；
- **停止顺序**：`quit` **自顶向下**——先本节点 `quit()`，再递归子节点
  （`ContribExecutive.cpp:139-148`）；
- **等待顺序**：`wait` **自底向上**——先递归子节点 `wait()`，再本节点 `wait()`
  （`ContribExecutive.cpp:159-172`）。

因此各层实现的 `quit()` / `wait()` **只需处理本节点自身的在飞活动**，子节点的停等由监督树
负责，不得自行递归。

- 同一 role 允许创建多个子执行体（`adoptChild` 不作同 role 唯一性约束），这是并发模型的基础；
- 包释放时框架对该包全部执行体执行 quit/wait；执行体必须在其归属 Package 释放前销毁。

## 4. 任务与并发模型

### 4.1 一条链 = 一棵子树 = 一个在飞任务

`srt::InferenceExecutive` 是**单任务面**：`state()` / `stop()` / `waitForFinished()` 三个方法，
`quit()` / `wait()` 被 private-final（`synthrt/include/synthrt/SVS/InferenceExecutive.h:39-50`）。
G2P / S2P / Onset 执行体因此各自只承载一个在飞任务。

`LinguistExecutive` 与之**保持同构**：同一时刻只允许一次转换在飞。理由是它的三个子执行体本
就无法并发受理，多任务面只会是骗人的接口。

> **并发靠多开执行体，不靠多开任务。** 宿主要并发 k 路，就 `createLinguist` k 次，得到 k 棵
> 相互独立的子树。

### 4.2 `LinguistExecutive` 方法面

形制对位 `srt::InferenceExecutive`：

```cpp
class LinguistExecutive : public wolf::LinguistPipelineExecutive {
public:
    using AsyncCallback =
        std::function<void(srt::Expected<std::unique_ptr<LinguistConvertResult>>)>;

    virtual srt::Expected<std::unique_ptr<LinguistConvertResult>>
        start(const LinguistConvertInput &input) = 0;

    virtual srt::Expected<void>
        startAsync(std::shared_ptr<const LinguistConvertInput> input, AsyncCallback callback) = 0;

    virtual srt::ITask::State state() const noexcept = 0;
    virtual srt::Expected<void> stop() = 0;
    virtual srt::Expected<void> waitForFinished() = 0;

    /// 执行体级诊断（§6）。二者在本执行体生命周期内恒定。
    virtual const Api::Common::L1::LanguageScheme &binding() const noexcept = 0;
    virtual const srt::ContribLocator &g2pContribution() const noexcept = 0;
};
```

- **单一任务门**：只有一种转换任务，不设 G2P-only / S2P-only 等多入口。「只要发音不要音素」
  由 `depth` 截断下游段实现（§4.3）；
- **无 `initialize`**：本执行体在创建时已由 `LinguistRuntimeOptions` 完成全部绑定，Level 1
  没有第二份初始化参数可携。子执行体各自的 `initialize` 由 wolf provider 内部调用，不外露；
- `stop()` 请求合作式取消，实现须在**词边界**响应。三条口径：
  - 被取消的转换**不是失败**——`state()` 取 `Canceled`，已完成的词照常返回，调用方看到链走到
    哪一步而不是一个错误；
  - `stop()` 在无执行时是**空操作**，不得毒化下一批（否则停一个已结束的批次会静默吃掉下一个）；
  - 「词边界」对能自己死循环的实现不成立，此类实现须另备打断手段（脚本变体见变体文档 §7.4）；
  - **须向下传递**：一个阶段是整批一次调用，故只停在本层的请求要等那次调用自己返回——而对模型
    或脚本来说，那正是值得打断的等待。`LinguistExecutive::stop()` 因此同时停已创建的三个子
    执行体；
  - **时延实测约 50 µs**（最坏情形：S2P 为死循环脚本，被计数钩子打断；三次取样 0.050 / 0.050 /
    0.062 ms）。规范**不给数字**——该值的上界是钩子的指令预算而非计时器，随「一批指令能做多少
    活」浮动，与输入规模无关。此处只作实现侧的量级参考（Q2）；
- `waitForFinished()` 不改变执行体存活；
- **线程纪律**：worker 线程由实现自持，**回调在实现线程上触发**，宿主须自行 marshal 到 UI
  线程（沿 `srt::ITask` 既定约定，`synthrt/include/synthrt/Task/ITask.h:100-101`）。

### 4.3 输入模型：逐词分层锁定

编辑器的歌词、发音、音素三层均可在任意时刻被人手修改，因此**不**提供「只能整链重算」的入口。

```
LinguistConvertInput : TaskStartInput {
    words: [ LinguistWordInput ]
    depth: enum { Pronunciation, Phonemes, Onsets }   // 链在哪一段截断
}

LinguistWordInput {
    lyric:         string                              // 必填
    pronunciation: optional<string>                    // 给出即为人工锁定层
    locked:        optional<{ phonemes: array<string>,
                              onsets:   array<bool> }> // 给出即为人工锁定层
}
```

**锁定层用 optional 表达，不用「布尔 + 值」配对**：给出即锁定，不给出即未锁定。空串与空数组
因此是**有意义的锁定值**（锁定为空），不会与「未锁定」混淆。`phonemes` 与 `onsets` 必须同时
给出且等长，故合为一个 optional，两者不可能各自缺席。

| 锁定状态 | 执行体动作 |
| :-- | :-- |
| 全未锁 | 全链：G2P 段 → S2P 段 → Onset 段 |
| 仅给出 `pronunciation` | 跳过 G2P 段，由锁定发音直接走 S2P 段 → Onset 段 |
| 给出 `locked` | G2P / S2P 全跳过，透传，仅做形状校验 |

`depth` 是**单值枚举而非若干开关**：onset 标记与音素序列等长对齐，「要 onset 但不要音素」
不是合法状态。用一个截断点表达，非法组合在类型上即不可表示。

**输入不携带语言参数**——执行体在创建时已绑定单一 `(language, scheme)`（§3.1）。

**保留记号词的直通是宿主预过滤职责**：`SP` / `AP`、连音 `-`、拆音续音符 `+` 串等不进批、
不在转换结果中占位，由宿主自行合成。

时长偏移属推理 DSP / 时长模型输入域，**不在本文范围**——语言链只产 `phonemes` 与 onset 布尔位。

### 4.4 输出

```
LinguistConvertResult : TaskResult { words: [ LinguistWordOutput ] }

LinguistWordOutput {
    pronunciation: string           // 经 G2P 段或锁定透传
    candidates:    array<string>    // 首个即主发音；锁定层不回填
    mode:          enum             // convert / copy / skip
    error:         enum             // 值域见 §5.1
    phonemes:      array<string>?   // depth >= Phonemes 时授予
    onsets:        array<bool>?     // depth == Onsets 时授予
    hitStage:      enum?            // 逐词诊断，见 §6
}
```

**批量是主形状**：逐词失败不中断批，词序位保留，沿袭链推理契约的共现约束。

## 5. 失败与降级语义

### 5.1 三级失败

| 级别 | 通道 | 何时 |
| :-- | :-- | :-- |
| **词级** | `LinguistWordOutput::error`，值域 = G2P 六值（链推理契约 §3.4.4） | 单个词转换失败；批继续 |
| **批级** | `start` / `startAsync` 返回 `Expected` 错误 | 驱动不可用、模型会话失效、资源加载失败等整批不可继续 |
| **加载级** | Package `Load` 失败 | 声明、`exports`、`configuration`、`options`、imports 集合任一校验失败（spec 2.4:444） |

补充规则：

- 手改层形状非法（`phonemes` 与 `onsets` 不等长、含空元素）→ 该词 `error` = `InvalidInput`；
- **S2P / Onset 段无独立错误通道**（链推理契约既定）。「后段无产出」表现为该词 `phonemes`
  为空且**不带** `error`，与「未命中不是失败」一致。空产出的告警/失败判定归宿主策略；
  实现仍应使真正的异常路径走**批级** `Expected`，使宿主可区分「正常未命中」与「异常静默」。

### 5.2 L4 不新增加载期失败事由

`createPipeline` / `createLinguist` 只做诊断兜底：请求一个不在 `languages` 映射中的语言句柄
判 `InvalidArgument`。这些只产出可观察诊断，**不构成新的加载期失败事由**。

## 6. 诊断通道

诊断**不进词级 `error`**，且严格按「变化频率」分置——同一执行体内恒定的信息不逐词复制：

| 诊断 | 位置 | 含义 |
| :-- | :-- | :-- |
| `hitStage` | **逐词**（`LinguistWordOutput`） | `dict` \| `model` \| `rule` \| `fallback` \| `locked`。前四值取自 G2P 的可选输出 `hitSource`，模块未提供时为空；`locked` 为锁层透传标注 |
| `binding` | **执行体级**（`LinguistExecutive::binding()`） | 该执行体生效的 `(language, scheme)` |
| `g2pContribution` | **执行体级**（`LinguistExecutive::g2pContribution()`） | 其 G2P 子执行体的贡献定位符（含包 ID 与版本），宿主可据此查包元数据 |

后两项在执行体的整个生命周期内恒定——一个执行体绑定一个二元组（A16）、持有一个 G2P 子执行体，
逐词携带它们是纯复制。宿主需要时向执行体查询即可。

`hitStage` **可为空**：宿主不读不损功能；实现可降级为空——实现自由，但有损可诊断性。

---

# L5 —— 宿主接入层（建议级）

本层全部内容为**建议**。宿主可自定策略；本文给出推荐做法与其依据，便于多宿主行为一致。

## 7. 解析：（歌手, 语言）→ 链路

以包完成 `Load` 为前提（`DataOnly` 下 import binding 为空，
`synthrt/include/synthrt/Core/ContribSpec.h:99-102`）：

1. `PackageHandle::contribution("singer", <singerId>)` 取 `SingerSpec`；
2. 经 §2.2 取 extension。无 extension ⇒ 该歌手无语言功能，正常状态；
3. 目标语言句柄直接在 `extension->languages()` 中查找；**命中即得**，无需遍历 imports、
   无需解析任何 ID；
4. `extension->createPipeline(options)` 得 `WolfPipelineExecutive`；
5. `pipeline->createLinguist(languageTag, options)` 得 `LinguistExecutive`。

### 7.1 缺省语言

宿主「跟随歌手」语义取 `extension->defaultLanguage()`。该值由歌手声明的 `defaultLanguage`
字段提供，**必然**是 `languages` 的键（`SingerCategory` 在 Probe 保证）。

歌手未声明 `languages` 时该值为空串，宿主自行处置（通常等价于「该歌手无语言功能」）。

> `defaultLanguage` 无加载期与运行时语义，语言域自身从不读它——它只是作者写给宿主的意图声明。

### 7.2 缓存纪律

- 语言集合与缺省语言在包 Commit 后不变，故（歌手 → pipeline）与（歌手 → 语言集合）两级
  映射可全程缓存，仅在包加载/卸载事件时失效；
- 推荐每歌手惰性建一次 pipeline、每条语言惰性建一个 `LinguistExecutive`；需要并发时再按
  §4.1 多开。

## 8. 三个接入场景

### 8.1 歌手无语言导入

不是错误。extension 缺席即语言功能整体不可用，宿主照常加载与合成，仅语言相关 UI 置灰。

### 8.2 声库缺语言依赖（硬失败）

框架语义为硬失败：依赖缺失即整个声库 `Load` 失败（spec 2.4:157-159 强制依赖 + :444 失败即
整次失败），**且不得回退其他候选版本**（:396）。配套义务是**失败部分必须逐条点名可呈现**。

推荐接入形态：

1. 宿主用 `DataOnly` 先扫全部候选包（只解析清单，不碰运行时），按 spec 2.4《依赖项》的选择
   规则自算依赖闭包，缺集 = 闭包差集；
2. 呈现粒度为「**声库 × 语言**」。受影响语言清单由宿主用缺包 ID 反查声库的 `languages`
   映射自算——**映射键本身就是语言句柄，DataOnly 下即可读**，不需要打开缺失的那个包；
3. 用户仍要装入时，`Load` 失败。框架的点名承载力：
   - 缺包在依赖求解层即失败，错误文本携带**依赖包 ID 与发起方包 ID**
     （`PackageLoader.cpp:692-693` + `:748-750` 上下文链）；
   - import 级点名（失败 role / 目标 ref）**不携带**（`:782-784` 仅「module import target
     does not exist」）。

**粒度铁律**：硬失败的单位是**包**——声库声明的任一语言包缺失，该声库 `Load` 即整体失败。
「其他正常」仅指依赖齐全的其他声库，不是同一声库的其余语言。宿主应把该声库标为「待安装
依赖」、阻止装入并呈现逐语言缺口报告；补齐依赖后即恢复，无需重装声库。

「缺语言仅该语言灰显、同声库其余语言照常唱」需上位规范增补可选/软导入后方能承诺，Level 1
不提供。

### 8.3 包在但不可加载

依赖求解选中候选后、解释器期的失败（典型：变体资源 `formatVersion` 超出当前插件支持上限）
**不得回退其他候选版本**（spec 2.4:396），整链按 §8.2 的粒度铁律硬失败。

呈现粒度与缺包相同，文案区分两种引导：

- **升级编辑器**——包属正常发布、资源格式版本确需更新解释器能力时；
- **安装兼容版本**——包属 `compatVersion` 虚报（发布纪律失守的包缺陷）时，从资源仓安装真正
  满足目标版本的旧版并存（spec 2.4:402 允许多版本共存）。

正常发布纪律下本场景应已前移收敛为「缺依赖」呈现，本节是纪律失守时的兜底。

## 9. 其他宿主约定

### 9.1 语言模型加载时机

包 `Load`（Acquire）只做声明解析与轻资源装配；模型会话建议惰性到 `LinguistExecutive` 创建
或首次调用（实现自由；spec 2.4:382 允许缓存解析结果）。

宿主预热即在后台线程对已加载歌手的每个语言各建一次执行体，**不另设 API**。

### 9.2 资源缓存是架构必需件

这是 §4.1 并发模型的直接推论，不是可选优化：

> 并发 k 路 = k 棵子树 = k 组 G2P / S2P / Onset 执行体。若无跨执行体的资源共享，
> k 路并发就是 k 份词典与 k 组 ONNX session。

缓存挂在 provider 执行域（spec 2.4:442 允许 provider 级基础设施不归属于任一 Package；:586
插件常驻至 Runtime 销毁），Package 卸载只减少引用计数。缓存**对本文的全部可观察行为透明**：
命中只是「取得同一资源对象」，不改变任何 IO。设计见 `linguist-resource-cache.md`。

### 9.3 包搜索路径顺序

**公共语言包所在路径必须排在声库内置包路径之前。**

依据是 spec 2.4:406 的**路径优先**规则：「如果当前路径没有候选，则继续下一个路径；如果存在
候选，则选择其中版本最高的 Package，不再搜索后续路径。」因此**先出现的路径只要存在满足该
依赖边的候选就赢得选择**。

> 注意不要把这条与 :404 的遮蔽规则混为一谈：:404 的遮蔽粒度是**身份 = id + 规范化版本**，
> 不同版本的同 ID 包不会互相遮蔽。真正保证顺序的是 :406；而当先位路径**不存在**满足目标
> 版本的候选时，后位路径的副本仍会被选中。

用户自定义包根若提供，应排在编辑器内置包根之前；内置与更新的落位必须同一包根，或更新包根
在前。

---

## 10. 现行实现与目标语义的差距

本文 §3-§6 为**发布级目标语义**。截至当前分支，执行体树、三份推理契约的解释器、资源缓存、会话层与
取消传递均已实现并有用例覆盖（见 [Status](Status.md)）。仍与目标形态有差距的只剩一项：

| 面 | 现状 |
| :-- | :-- |
| 歌手侧 `languages` / `defaultLanguage` | 读自 synthrt `SingerCategory` 的类别追加字段（A11 已落地于 synthrt 分支 `onnxruntime-builds-uptake`，见 A26 补记）。形状与 role 存在性由 synthrt 在开包时校验；`readSingerLanguages()` 只把已解析的映射换成 wolf 的形状 |

取消的现行口径见决策台账 A73 及其补记：子执行体在入口读到未消费的停止请求即以 `Canceled` 返回，
linguist 执行体识别「并非本次转换发出的停止」并重试一次该阶段。
