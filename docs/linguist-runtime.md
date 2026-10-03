# wolf 语言域运行时与宿主接入（L4 + L5）

本文规定**运行时装配层（L4）**与**宿主接入层（L5）**：pipeline extension 的挂载与取用、执行体
监督树、任务与并发模型、失败与降级语义、诊断通道，以及宿主的解析次序与呈现策略。

上位规范为 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)；
声明形态与加载期裁决见 [linguist-domain-contract.md](linguist-domain-contract.md)；
逐调用 IO 词汇见 [linguist-inference-contract.md](linguist-inference-contract.md)。

**词汇分层**：**L4 为规范性内容**，宿主依赖其调用面与可观察语义；**L5 为建议级内容**，宿主可
自定策略，本文只给出推荐做法及其依据。

---

# L4 —— 运行时装配层

## 1. 宿主初始化序列

各步骤须按下列顺序执行：

1. **链接 wolf**（编译期）。wolf 是库而非独立程序，CMake 包 `wolf` 提供目标 `wolf::wolf`
   （共享库，输出名 `synthrt-wolf`）。链接即生效：库加载时，库内静态注册对象把 `linguist`
   类别连同 provider factory 加入 synthrt 类别注册表
   （`src/lib/Linguist/LinguistContrib.cpp:172-173` 的静态注册对象）。宿主若不引用 wolf 的其他符号，须在
   构造第一个 `SynthUnit` 之前调用一次 `wolf::linkLinguistCategory()`，以防链接器丢弃该依赖
   （语言域契约 §1）。
   **宿主未链接 wolf ⇒ 无 `linguist` 类别 ⇒ 含语言贡献的包整体被拒**
   （`synthrt/lib/Core/PackageLoader.cpp:1210-1212`）；
2. **构造 `SynthUnit`**。构造时收集注册表中的全部类别（`synthrt/lib/Core/SynthUnit.cpp:14-31`），
   先于任何包解析（spec 2.4:250）；
3. **配置路径**：
   - `setPackagePaths`：包搜索路径。**公共语言包路径须排在声库内置包路径之前**（依据见 §9.3）；
   - `setPluginPaths(category, paths)`：按类别配置插件搜索路径。语言域需要两个类别：
     `linguist`（wolf 的组合 provider 插件）与 `inference`（G2P / S2P / Onset 解释器插件）。
     后者与 dsinfer 的推理插件共用同一类别与同一 IID，宿主把 wolf 的插件目录与 dsinfer 等库的
     插件目录一并列入 `inference` 类别的路径序列；
4. **加载包**：`openPackage(path, Load)`，返回 `PackageHandle`。句柄的生命周期即运行时资源
   的归属期，最后一个句柄释放时包随之释放。`DataOnly` 模式只读取清单、不涉及运行时（§8.2 用它
   做缺依赖预检）。

### 1.1 插件安装布局

wolf 的插件安装在 `lib/plugins/wolf/<category>/<name>/`，构建树采用相同布局
（`src/CMakeLists.txt` 的 `WOLF_INSTALL_PLUGINS_DIR` / `WOLF_BUILD_PLUGINS_DIR`）。该布局与
dsinfer 的 `lib/plugins/dsinfer/...` 一致，使基于 synthrt 的各库插件位于同一根目录下。
本节规定的是**运行期查找路径**（路径形态与宿主传给 `setPluginPaths` 的映射）；安装树里装了哪些
插件、各自缺什么依赖的后果属于**分发物清单**，见
[linguist-distribution.md](linguist-distribution.md) §7.1。
`<category>` 取以下两值：

| 目录 | 插件 | 传给 `setPluginPaths` 的类别 |
| :-- | :-- | :-- |
| `linguistproviders` | wolf 组合 provider（IID `org.openvpi.wolf.plugin.LinguistProvider`） | `linguist` |
| `inferenceinterpreters` | G2P / S2P / Onset 解释器（IID `org.openvpi.synthrt.plugin.InferenceInterpreter`） | `inference` |

`find_package(wolf CONFIG)` 之后，`wolfConfig.cmake`（`src/wolfConfig.cmake.in`）定义以下变量：

- `WOLF_PLUGINS_DIR`：release 树的插件根目录，即安装规则的插件目标目录，因此库目录不为 `lib`
  的安装树同样适用。宿主把 `${WOLF_PLUGINS_DIR}/linguistproviders` 与
  `${WOLF_PLUGINS_DIR}/inferenceinterpreters` 分别传给上表对应类别的 `setPluginPaths`。插件在
  运行时按名称加载，因此该值是路径而非链接目标；
- `WOLF_PLUGINS_DIR_DEBUG`：仅在安装前缀下存在 `debug/` 前缀树（如 vcpkg 树的 Debug 构建）时
  定义，指向该树的插件根目录。Debug 宿主须使用该目录，因为在 Windows 上，以不同运行时库构建的
  插件与宿主无法互相加载。只读取 `WOLF_PLUGINS_DIR` 的宿主不受该变量影响。

`.github/consumer` 在 CI 中检查安装包导出 `WOLF_PLUGINS_DIR`，且其下存在上述两个类别目录。

## 2. Pipeline extension

### 2.1 挂载条件

wolf 为满足下列条件的 Singer spec 注册 pipeline extension：

> 歌手声明的 `languages` 映射中至少有一项所指的 import 目标为 linguist 贡献，且目标三元组为
> (`org.openvpi.wolf.linguist.WolfLinguist`, 1, `wolf`)。

extension 只挂载满足该条件的映射项。Ready-2 的 wolf validator 已拒绝指向其他目标的映射项
（语言域契约 §10.5），因此到达 Commit 的加载挂载整张映射；该过滤仍然保留，因为框架不保证
validator 先于 extension 创建执行。

trait ID 为 **`org.openvpi.wolf.extension.LinguistPipeline`**，经
`ContribSpecExtensionTraits<SingerSpec, WolfPipelineExecutive>` 特化注册。

挂载发生在加载事务的 **Ready-3**（`PackageLoader.cpp:1004-1007` 的
`attachSpecExtensions`），此时全部 import binding 与执行工厂均已就绪。框架对事务内每个新
spec 依次调用**全部已加载解释器**的 `createExtensions`（`PackageLoader.cpp:946-990`），因此
wolf 可以为由 dsinfer 解释的歌手挂载自身的扩展。

> 挂载判定**不依据 role 名**。语言身份由 `languages` 映射承载，role 是自由命名的本地槽位
> （语言域契约 §10.2）。

### 2.2 取用

```cpp
auto *ext = wolf::Api::Linguist::L1::WolfPipelineExtension::from(singerSpec);
```

`from()` 是公共头中的内联静态函数，等价于以下两步写法：

```cpp
auto *base = srt::ContribSpecExtension::findFromSpec<
                 wolf::Api::Linguist::L1::WolfPipelineExecutive>(singerSpec);
auto *ext  = base ? base->as<wolf::Api::Linguist::L1::WolfPipelineExtension>() : nullptr;
```

`findFromSpec` 返回基类指针（`synthrt/include/synthrt/Core/ContribSpecExtension.h:34-39`），
取得后下转。**无此 extension 表示该歌手未声明任何指向 linguist 贡献的语言，语言功能整体不可用**；
这是正常状态，不是错误（见 §8.1）。

extension 的语言集合在 Package Commit 后**不再变化**，可全程缓存，仅在包加载或卸载事件时失效。

### 2.3 方法面

```cpp
class WolfPipelineExtension : public srt::SingerPipelineExtension {
public:
    static WolfPipelineExtension *from(const srt::SingerSpec &singer);

    /// 该歌手声明的全部语言句柄，按 languages 映射的键序排列。
    virtual const std::vector<std::string> &languages() const = 0;

    /// 歌手声明的缺省语言句柄；未声明时为空串。
    virtual const std::string &defaultLanguage() const = 0;

    /// 语言句柄对应的 linguist 贡献定位符，用于呈现与诊断；未声明的句柄返回 nullptr。
    virtual const srt::ContribLocator *locate(std::string_view language) const = 0;

    /// 语言句柄对应的 linguist 所绑定的二元组；未声明的句柄返回 nullptr。
    virtual const Common::L1::LanguageScheme *binding(std::string_view language) const = 0;

    /// 语言句柄对应的组合可达的最深层，由其 imports 集合读出（语言域契约 §5.0.1）。
    virtual Depth maxDepth(std::string_view language) const = 0;

    /// 语言句柄对应的组合所声明的音素清单与 openSet；未声明的句柄返回 nullptr。
    virtual const LinguistExports *exports(std::string_view language) const = 0;
};
```

`createPipeline(const srt::SingerPipelineRuntimeOptions &)` 继承自
`srt::SingerPipelineExtension`，由 wolf provider 实现，返回 `WolfPipelineExecutive`（§7）。
`binding()`、`maxDepth()` 与 `exports()` 均在构造 extension 时读出，调用时不创建任何执行体。
`exports()` 返回的指针指向 linguist spec 的 exports（可能属于另一个包），在宿主持有歌手包的
`PackageHandle` 期间有效；需要跨越该期间保留时，调用方应复制其中的值。

## 3. 执行体监督树

```
SingerSpec ──extension──▶ WolfPipelineExecutive
                              └─ createChild(languages[tag]) ─▶ LinguistExecutive
                                     ├─ createChild("linguist/g2p")   ─▶ G2PExecutive    ┐
                                     ├─ createChild("linguist/s2p")   ─▶ S2PExecutive    ├ srt::InferenceExecutive
                                     └─ createChild("linguist/onset") ─▶ OnsetExecutive  ┘
```

整棵树经 `ContribExecutive::createChild(role, runtimeOptions)` 构建
（`synthrt/lib/Core/ContribExecutive.cpp:111-129`），**不需要改动框架**。每一层的机制相同：
按 role 在**本执行体自身 spec 的 imports** 中取执行工厂，创建子执行体后以 `adoptChild` 纳入
监督树。

- 第一层的 role 来自歌手 `languages` 映射的值，工厂由 `linguist` 类别提供；
- 第二层的 role 是 `linguist/g2p` 等固定名，工厂由 `inference` 类别提供。子执行体在首次需要
  时创建；S2P 与 Onset 在对应 role 缺席时不创建。

### 3.1 二元组注入

`LinguistExecutive` 创建 G2P 与 S2P 子执行体时，把**自身 spec 的 `(language, scheme)`** 写入
各自 `RuntimeOptions` 的 `binding`；Onset 的 `RuntimeOptions` 不携带二元组。变体必须取自目标
spec 的 `variant()`，不得硬编码，见链推理契约 §6.3。

```cpp
const auto &target = spec().findImport("linguist/g2p")->binding()->target();
Api::G2P::L1::G2PRuntimeOptions options(target.variant());
options.binding = { spec().language(), spec().scheme() };
auto child = createChild("linguist/g2p", options);
```

### 3.2 生命周期

- **所有权**：子执行体归父执行体所有。宿主可提前 `delete` 子执行体，子执行体在析构时先从父
  执行体的子表中移除（`ContribExecutive.cpp:27-35`）；
- **停止顺序**：`quit` **自顶向下**，先调用本节点的 `quit()`，再递归子节点
  （`ContribExecutive.cpp:139-148`）；
- **等待顺序**：`wait` **自底向上**，先递归子节点的 `wait()`，再调用本节点的 `wait()`
  （`ContribExecutive.cpp:159-172`）。

因此各层实现的 `quit()` / `wait()` **只处理本节点自身的在飞活动**，子节点的停止与等待由
监督树负责，实现不得自行递归。

- 同一 role 允许创建多个子执行体（`adoptChild` 不要求同 role 唯一），这是并发模型的基础；
- 包释放时框架对该包的全部执行体执行 quit/wait；执行体必须在其所属 Package 释放前销毁。

## 4. 任务与并发模型

### 4.1 单任务模型：一条链对应一棵子树与一个在飞任务

`srt::InferenceExecutive` 是**单任务面**：提供 `state()` / `stop()` / `waitForFinished()` 三个方法，
`quit()` / `wait()` 为 private final（`synthrt/include/synthrt/SVS/InferenceExecutive.h:39-50`）。
G2P / S2P / Onset 执行体因此各自只承载一个在飞任务。

`LinguistExecutive` 与之**保持同构**：同一时刻只允许一次转换在飞。理由是其三个子执行体本身
无法并发受理任务，多任务接口无法兑现其语义。

> **并发通过创建多个执行体实现，而非启动多个任务。** 宿主需要 k 路并发时，调用 k 次
> `createLinguist`（或 `createOwnedLinguist`），得到 k 棵相互独立的子树。

### 4.2 `LinguistExecutive` 方法面

形制与 `srt::InferenceExecutive` 对应：

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

- **单一任务入口**：只有一种转换任务，不设 G2P-only / S2P-only 等多个入口。「只需发音、不需
  音素」由 `depth` 截断下游段实现（§4.3）；
- **无 `initialize`**：本执行体在创建时已由 `LinguistRuntimeOptions` 完成全部绑定，Level 1
  没有第二组初始化参数。子执行体各自的 `initialize` 由 wolf provider 内部调用，不对外暴露；
- `waitForFinished()` 不改变执行体的存活状态；
- **线程规则**：worker 线程由实现自行持有，**回调在实现线程上触发**，宿主须自行将其转发到 UI
  线程（沿用 `srt::ITask` 的既定约定，`synthrt/include/synthrt/Task/ITask.h:95-101`）。

#### 4.2.1 停止语义

`stop()` 请求合作式取消，实现须在**词边界或阶段边界**响应。具体规则如下：

- **被取消的转换不是失败**：`state()` 取 `Canceled`，`start()` 与异步回调返回已完成的词（保留
  序位），未处理的词以未转换的形状返回。调用方依据 `state()` 而非错误区分停止与完成。该规则
  写在公共头 `LinguistExecutive` 的类注释中，G2P / S2P / Onset 执行体与
  `LinguistSession::convert()`（取消令牌）同样遵守。该语义与构建在 synthrt 上的分析执行体
  （otter 的 F0 / Note / Align 执行体在停止后由 `start()` 返回 `Cancelled` 错误）**有意不同**：
  一批歌词由彼此独立的词构成，其余词未转换时已转换的词仍然正确，编辑器应显示而非丢弃这些词；
  中途停止的分析结果不是更短的正确结果，因此分析执行体返回错误。两个库各自保留自己的语义；
- **请求的作用对象**：`stop()` 作用于当前在飞的转换；无转换在飞时，作用于下一次转换。一次
  停止请求由一次转换消费后清除，不延续到之后的转换（A73）；
- **无法在词边界响应的实现**（如可能陷入死循环的脚本）须另备中断手段（脚本变体见变体文档 §7.4）；
- **向下传递**：每个阶段对整批只调用一次，仅停止本层的请求须等待该调用返回，而对模型或脚本
  而言，该等待正是需要中断的部分。因此 `LinguistExecutive::stop()` 同时停止已创建的三个子
  执行体；
- **时延实测约 50 µs**（最坏情形：S2P 为死循环脚本，由计数钩子中断；三次取样分别为 0.050 /
  0.050 / 0.062 ms）。规范**不规定具体数值**，因为该值的上界取决于钩子的指令预算而非计时器，
  随单批指令的执行量变化，与输入规模无关。此处仅作为实现侧的量级参考（Q2）。

### 4.3 输入模型：逐词分层锁定

编辑器的歌词、发音、音素三层均可随时被用户手动修改，因此接口**不**限定为只能整链重算。

```
LinguistConvertInput : TaskStartInput {
    words: [ LinguistWordInput ]
    depth: enum { Pronunciation, Phonemes, Onsets }   // 链的截断位置，缺省 Onsets
}

LinguistWordInput {
    lyric:         string                              // 必填
    pronunciation: optional<string>                    // 给出即为人工锁定层
    locked:        optional<{ phonemes: array<string>,
                              onsets:   array<bool> }> // 给出即为人工锁定层
}
```

**锁定层以 optional 表达，而不是以「布尔值 + 值」配对表达**：给出即锁定，未给出即未锁定。
空串与空数组因此是**有意义的锁定值**（锁定为空），不会与「未锁定」混淆。`phonemes` 与
`onsets` 必须同时给出且等长，因此合并为一个 optional（C++ 类型 `LockedPhonemes`），不存在
二者之一单独缺席的状态。

| 锁定状态 | 执行体动作 |
| :-- | :-- |
| 全未锁定 | 全链：G2P 段 → S2P 段 → Onset 段 |
| 仅给出 `pronunciation` | 跳过 G2P 段，由锁定发音直接进入 S2P 段 → Onset 段 |
| 给出 `locked` | G2P / S2P / Onset 全部跳过，原样透传，仅做形状校验 |

`depth` 是**单值枚举而非若干开关**：onset 标记与音素序列等长对齐，「需要 onset 但不需要音素」
不是合法状态。以一个截断点表达，可使非法组合在类型上无法表示。

**输入不携带语言参数**：执行体在创建时已绑定单一 `(language, scheme)`（§3.1）。

**保留记号词的直通由宿主预过滤负责**：`SP` / `AP`、连音 `-`、拆音续音符 `+` 串等不进入批次，
不在转换结果中占位，由宿主自行合成。

时长偏移属于推理 DSP 与时长模型的输入域，**不在本文范围内**；语言链只产出 `phonemes` 与
onset 布尔位。

### 4.4 输出

```
LinguistConvertResult : TaskResult { words: [ LinguistWordOutput ] }

LinguistWordOutput {
    pronunciation: string           // 经 G2P 段产出或由锁定层透传
    candidates:    array<string>    // 首个元素即主发音；锁定层不回填
    mode:          enum             // convert / copy / skip；锁定层透传时为 copy
    error:         enum             // 值域见 §5.1；None 表示成功
    phonemes:      array<string>    // depth >= Phonemes 时授予，否则为空
    onsets:        array<bool>      // depth == Onsets 时授予，否则为空
    hitStage:      enum             // 逐词诊断，见 §6；无诊断时为 Unspecified
}
```

**批量是主形状**：单个词失败不中断整批，词的序位保留，沿用链推理契约的共现约束。

## 5. 失败与降级语义

### 5.1 三级失败

| 级别 | 通道 | 适用情形 |
| :-- | :-- | :-- |
| **词级** | `LinguistWordOutput::error`，值域为 G2P 的六个取值（链推理契约 §3.4.4） | 单个词转换失败；整批继续 |
| **批级** | `start` / `startAsync` 返回 `Expected` 错误 | 驱动不可用、模型会话失效、资源加载失败、阶段返回的条目数与输入数不符等导致整批无法继续的情形 |
| **加载级** | Package `Load` 失败 | 声明、`exports`、`configuration`、`options`、imports 集合任一校验失败（spec 2.4:444） |

补充规则：

- 锁定层形状非法（`phonemes` 与 `onsets` 不等长）→ 该词 `error` 为 `InvalidInput`；
- **S2P / Onset 段无独立错误通道**（链推理契约的既定规则）。「后段无产出」表现为该词 `phonemes`
  为空且**不带** `error`，与「未命中不是失败」一致。空产出的告警或失败判定由宿主策略决定；
  实现仍应使真正的异常路径走**批级** `Expected`，以便宿主区分「正常未命中」与「异常静默」。

### 5.2 L4 与加载期失败事由

`createPipeline` 对契约标识不符的 runtime options 返回 `InvalidArgument`；`createLinguist`
对不在 `languages` 映射中的语言句柄返回 `InvalidArgument`。这些错误只作为可观察诊断，
**不构成新的加载期失败事由**。

## 6. 诊断通道

诊断**不进入词级 `error`**，并严格按变化频率分置，同一执行体内恒定的信息不逐词复制：

| 诊断 | 位置 | 含义 |
| :-- | :-- | :-- |
| `hitStage` | **逐词**（`LinguistWordOutput`） | `dict` \| `model` \| `rule` \| `fallback` \| `locked`。前四个取值来自 G2P 的可选输出 `hitSource`，模块未提供时为 `Unspecified`；`locked` 标注由锁定层透传的词（给出 `pronunciation` 或 `locked` 的词） |
| `binding` | **执行体级**（`LinguistExecutive::binding()`） | 该执行体生效的 `(language, scheme)` |
| `g2pContribution` | **执行体级**（`LinguistExecutive::g2pContribution()`） | 其 G2P 子执行体的贡献定位符（含包 ID 与版本），宿主可据此查询包元数据 |

后两项在执行体的整个生命周期内恒定：一个执行体绑定一个二元组（A16）并持有一个 G2P 子执行体，
逐词携带这两项只是重复复制。宿主需要时向执行体查询。

`hitStage` **可为空**：宿主不读取该字段不影响功能；实现可以不提供该字段，这属于实现自由，但会
降低可诊断性。

---

# L5 —— 宿主接入层（建议级）

本层全部内容为**建议**。宿主可自定策略；本文给出推荐做法及其依据，以便多个宿主行为一致。

## 7. 解析：（歌手, 语言）→ 链路

前提是包已完成 `Load`（`DataOnly` 模式下 import binding 为空，
`synthrt/include/synthrt/Core/ContribSpec.h:99-102`）：

1. 由 `PackageHandle::contribution("singer", <singerId>)` 取得 `SingerSpec`；
2. 按 §2.2 取得 extension。无 extension 表示该歌手无语言功能，属于正常状态；
3. 在 `extension->languages()` 中直接查找目标语言句柄；**命中即可**，无需遍历 imports，
   也无需解析任何 ID；
4. 由 `extension->createPipeline(options)`（`options` 为 `WolfPipelineRuntimeOptions`）取得
   `WolfPipelineExecutive`；
5. 由 `pipeline->createOwnedLinguist(languageTag, options)`（`options` 为
   `LinguistRuntimeOptions`）取得持有所有权的 `std::unique_ptr<LinguistExecutive>`。指针析构时
   执行体从 pipeline 移除并销毁；该指针须在 pipeline 销毁之前释放。虚函数 `createLinguist`
   仍然保留，返回 pipeline 名下的裸指针，调用方须自行 `delete`，否则执行体在 pipeline 下累积。

### 7.1 缺省语言

宿主的「跟随歌手」语义取 `extension->defaultLanguage()`。该值来自歌手声明的 `defaultLanguage`
字段，**必然**是 `languages` 的键（由 `SingerCategory` 在 Probe 阶段保证）。

歌手未声明 `languages` 时不挂载 extension（§2.1），宿主通常按「该歌手无语言功能」处置。

> `defaultLanguage` 无加载期与运行时语义，语言域自身不读取该字段；该字段仅记录作者向宿主表达
> 的意图。

### 7.2 缓存规则

- 语言集合与缺省语言在包 Commit 后不变，因此（歌手 → pipeline）与（歌手 → 语言集合）两级
  映射可全程缓存，仅在包加载或卸载事件时失效；
- 推荐为每个歌手惰性创建一次 pipeline，为每个语言惰性创建一个 `LinguistExecutive`；需要并发
  时再按 §4.1 创建多个执行体。

## 8. 三种接入场景

### 8.1 歌手无语言导入

不是错误。extension 缺席表示语言功能整体不可用，宿主照常加载与合成，仅将语言相关 UI 置灰。

### 8.2 声库缺少语言依赖（硬失败）

框架语义为硬失败：依赖缺失即整个声库 `Load` 失败（spec 2.4:157-159 规定依赖为强制依赖，
:444 规定任一失败即整次失败），**且不得回退到其他候选版本**（:396）。相应的义务是**失败部分
必须能够逐条点名呈现**。

推荐接入形态：

1. 宿主以 `DataOnly` 模式预先扫描全部候选包（只解析清单，不涉及运行时），按 spec 2.4《依赖项》
   的选择规则自行计算依赖闭包，缺失集合为闭包的差集；
2. 呈现粒度为「**声库 × 语言**」。受影响的语言清单由宿主用缺失包 ID 反查声库的 `languages`
   映射得出：**映射键本身即为语言句柄，在 DataOnly 模式下即可读取**，不需要打开缺失的包；
3. 用户仍要装入时，`Load` 失败。框架的点名能力如下：
   - 缺失包在依赖求解层即失败，错误文本携带**依赖包 ID 与发起方包 ID**
     （`PackageLoader.cpp:692-693` 与 `:748-750` 的上下文链）；
   - import 级点名（失败的 role 与目标 ref）**不携带**（`:782-784` 仅报告「module import target
     does not exist」）。

**粒度规则**：硬失败的单位是**包**。声库声明的任一语言包缺失，该声库的 `Load` 即整体失败。
「其他正常」仅指依赖齐全的其他声库，不包括同一声库的其余语言。宿主应把该声库标为「待安装
依赖」，阻止装入并呈现逐语言的缺口报告；补齐依赖后即可恢复，无需重新安装声库。

「缺失的语言单独置灰、同一声库的其余语言照常可用」需要上位规范增补可选导入或软导入后才能
支持，Level 1 不提供。

### 8.3 包已安装但不可加载

依赖求解选中候选之后、在解释器阶段发生的失败（典型情形：变体资源的 `formatVersion` 超出当前
插件支持的上限）**不得回退到其他候选版本**（spec 2.4:396），整条链按 §8.2 的粒度规则硬失败。

呈现粒度与缺失包相同，文案区分两种引导：

- **升级编辑器**：包属于正常发布，资源格式版本确实需要更新解释器能力；
- **安装兼容版本**：包的 `compatVersion` 声明不实（违反发布规则的包缺陷），应从资源仓库安装
  真正满足目标版本的旧版并与之并存（spec 2.4:402 允许多版本共存）。

在正常的发布规则下，本场景应在前一阶段即表现为「缺依赖」；本节是发布规则失效时的兜底处理。

## 9. 其他宿主约定

### 9.1 语言模型加载时机

包 `Load`（Acquire）只做声明解析与轻量资源装配；模型会话建议推迟到 `LinguistExecutive` 创建
或首次调用时加载（属于实现自由；spec 2.4:382 允许缓存解析结果）。

宿主预热即在后台线程中为已加载歌手的每个语言各创建一次执行体，**不另设 API**。

### 9.2 资源缓存

k 路并发对应 k 棵子树，即 k 组 G2P / S2P / Onset 执行体（§4.1），因此执行体之间必须共享解析
产物；本层只要求该共享**对本文规定的全部可观察行为透明**（命中仅表示取得同一资源对象，不改变
任何 IO）。缓存为何是架构必需件见 [linguist-architecture.md](linguist-architecture.md) §1.2 的 C2；
缓存的对象、键、归属、生命周期与验收基线见
[linguist-resource-cache.md](linguist-resource-cache.md)；「k 路并发即 k 组 ONNX session」的更正
（对 ONNX 不成立）见该文档 A59 条。

### 9.3 包搜索路径顺序

**公共语言包所在路径必须排在声库内置包路径之前。**

依据是 spec 2.4:406 的**路径优先**规则：「如果当前路径没有候选，则继续下一个路径；如果存在
候选，则选择其中版本最高的 Package，不再搜索后续路径。」因此，**只要靠前的路径中存在满足该
依赖边的候选，该路径即赢得选择**。

> 该规则不应与 :404 的遮蔽规则混淆：:404 的遮蔽粒度是**身份（id + 规范化版本）**，
> 不同版本的同 ID 包不会互相遮蔽。保证顺序的是 :406；当靠前的路径中**不存在**满足目标版本的
> 候选时，靠后路径中的副本仍会被选中。

若提供用户自定义包根，应排在编辑器内置包根之前；内置包与更新包必须位于同一包根，或者更新包
根排在前面。

---

## 10. 现行实现与目标语义的差距

本文 §3-§6 为**发布级目标语义**。在当前分支上，执行体树、三份推理契约的解释器、资源缓存、
会话层与取消传递均已实现并有用例覆盖（见 [Status](Status.md)）。与目标形态仍有差距的只有一项：

| 方面 | 现状 |
| :-- | :-- |
| 歌手侧 `languages` / `defaultLanguage` | 读自 synthrt `SingerCategory` 的类别追加字段（A11 已在 synthrt 的 `onnxruntime-builds-uptake` 分支实现，见 A26 补记）。形状与 role 存在性由 synthrt 在打开包时校验；`readSingerLanguages()` 只把已解析的映射转换为 wolf 的形状 |

取消的现行规则见决策台账 A73 及其补记：子执行体在入口读到未消费的停止请求时以 `Canceled`
返回；linguist 执行体识别出并非本次转换发出的停止请求时，重试一次该阶段。
