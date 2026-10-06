# wolf 语言域会话（L6）

本文规定**宿主直接持有的会话对象**。会话位于层栈最上层，把执行体树、就绪状态、并发与保留词收进
语言域内部，宿主只需提供三项输入：歌手、语言与待转换的词。

上位规范为 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)；
决策见 [linguist-decisions.md](linguist-decisions.md)。本文属 **wolf 实现文档**，不是对外契约。

> **口径**：本文描述**目标形态**，该形态已全部实现（§9）。实现状态见 [Status.md](Status.md)；
> 实施阶段对原设计的修正集中列于 §12.1。

## 1. 动机

没有会话层时，wolf 只提供「provider + 执行体」：宿主取得 `WolfPipelineExtension`，由其创建
`WolfPipelineExecutive`，再创建 `LinguistExecutive`，然后调用 `start()`。**两者之间缺失的逻辑
需要每个宿主各自实现一遍。**

ds-editor-lite 已经实现过一遍。其 `VoicebankSession` 在 refactor 分支上提供：

```
session.ensureLanguageReady(packageId, version, language)   // 惰性、幂等、内部缓存
session.convertG2p(identifier, language, inputs)            // 按语言分组批量
session.convertS2p(identifier, language, pronunciation)     // 逐词，返回 {phonemes, onsets}
```

以及调用点中的一批附属逻辑（`GetPronunciationTask.cpp` / `GetPhonemeNameTask.cpp`）：

- 按语言分组、**逐音符**指定的 `language`；
- `readyLanguages` / `failedS2pLanguages` 两个集合：**成功与失败都需要缓存**，否则每个音符都会
  重试一次；
- SP / AP 特判：产出单个保留音素，`isOnset = true`；
- 连音、`+` 后缀、空词的过滤；
- 分阶段就绪：`sessionReady()` 只保证元数据就绪，模型可通过 `deferLanguageModels` 推迟加载。

**这些逻辑不是宿主的业务判断，而是语言域的实现细节。** 失败缓存的有效期、并发执行体的数量、SP
的语义，如果由每个宿主各自决定，就会产生多种不一致的行为，而正确的行为只有一种。

`srt::SynthUnit` 不提供这一层：它只负责包、插件与 Runtime Service 的注册，不负责扫描、就绪状态
与路由缓存（`synthrt/include/synthrt/Core/SynthUnit.h:25-95`）。这一层若不由 wolf 实现，就需要
每个宿主各自实现。

## 2. 会话职责

| # | 职责 | 原先的承担方 |
| :-: | :-- | :-- |
| 1 | 歌手 → 语言的**快照**，原子发布 | 宿主（lite 的 `VoicebankSnapshot`） |
| 2 | 三态**就绪查询**与主动预热 | 宿主（lite 的 `ensureLanguageReady`） |
| 3 | 成功与失败的**双向缓存** | 宿主（lite 的两个集合） |
| 4 | **执行体池**与包的持有：并发依靠多个执行体（A14），执行体池把多开变为内部事务，并提供释放点 | 宿主 |
| 5 | **保留词与空词**的统一处置 | 宿主（各自实现） |
| 6 | **取消令牌**：为 A51 建立的向下取消传递提供宿主侧的入口 | 宿主 |

会话**不承担**的职责见 §10。

## 3. 接口定义

以下为 `include/wolf/Session/LinguistSession.h` 的摘要，注释为中文概述，完整说明以头文件为准。

```cpp
namespace wolf {

    /// 标识会话中的一个歌手贡献。ContribLocator 不含包版本，因此版本单独携带。
    /// 版本为空表示「唯一已加载的版本」；加载了多个版本时视为歧义并拒绝（§12.1 第 1 条）。
    struct SingerRef {
        srt::ContribLocator locator;
        stdc::VersionNumber version;
    };

    /// 一个 (歌手, 语言) 组合当前的可用状态。查询不改变状态。
    enum class Readiness {
        Ready,        ///< 已预热：下一次转换不再加载任何资源
        Cold,         ///< 绑定已确定、深度与覆盖度已计算，尚未预热（A71）
        Unavailable,  ///< 不可用，reason 给出原因
    };

    struct LanguageStatus {
        Readiness readiness = Readiness::Unavailable;
        std::string reason;   ///< 仅 Unavailable 时非空

        enum class CoverageKind {
            Unknown,  ///< 宿主未给出音素表，或语言未声明音素清单；不等同于覆盖度为零
            Exact,    ///< 语言声明清单即全集，比例精确
            Lower,    ///< 语言声明 openSet，清单与比例均为下界
        };

        /// 该语言可达的最深层级，由组合的 imports 集合读出（A70）。
        /// 仅在 readiness 不为 Unavailable 时有意义；缺省取最浅层级。
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Pronunciation;

        /// 该语言的发音层是否是独立的层，由其 S2P 成员的 variant 读出（A81）。
        /// 与 maxDepth 同源：它描述组合**声明**出的形状，凡歌手声明了该语言就会被报出；
        /// 路由后变为不可用（如歌手音素表覆盖度为 0）时**不清位**。仅当歌手未声明该语言时为
        /// 缺省 false。读取该值前必须先看 readiness。
        bool hasSeparatePronunciationLayer = false;

        CoverageKind coverageKind = CoverageKind::Unknown;
        double coverage = 0.0;                      ///< 0 到 1，仅在 coverageKind 不为 Unknown 时有意义
        std::vector<std::string> missingPhonemes;   ///< 歌手无法演唱的声明音素，截断至前 16 条
    };

    /// 快照中的一条语言。
    struct LanguageEntry {
        std::string handle;                         ///< 语言句柄，[a-z]{3}
        Api::Common::L1::LanguageScheme binding;
        srt::ContribLocator linguist;               ///< 承载该语言的 linguist 贡献
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Onsets;  ///< 与 probe() 报告的值相同
        bool hasSeparatePronunciationLayer = false;  ///< 与 probe() 报告的值相同（A81）
        std::vector<std::string> phonemes;          ///< 组合声明可能产出的音素
        bool openSet = false;                       ///< 为真时 phonemes 不是全集
    };

    struct SingerEntry {
        SingerRef ref;                              ///< 目录中的版本总是具体版本
        std::vector<LanguageEntry> languages;
        std::string defaultLanguage;                ///< 作者提示，不参与匹配
        std::vector<std::string> reservedMarkers;   ///< 歌手声明的 reservedPhonemes；为空时使用会话级集合
    };

    /// 构建时刻的不可变视图，可跨 refresh() 持有。
    class LinguistCatalog {
    public:
        const std::vector<SingerEntry> &singers() const noexcept;
        const SingerEntry *find(const SingerRef &singer) const;
        /// 同一查找；返回空时 ambiguous 区分「加载了多个版本」与「不存在」。
        const SingerEntry *find(const SingerRef &singer, bool &ambiguous) const;
    };

    /// 宿主可从另一线程置位，使会话对已租出的执行体调用 stop()。
    /// 副本共享同一状态。
    class CancelToken {
    public:
        void cancel() noexcept;
        bool cancelled() const noexcept;
    };

    /// 全部成员可从任意线程调用。
    class LinguistSession {
    public:
        /// 借用而不拥有 unit：宿主通常在同一 unit 上还挂有其他类别与服务。unit 须比会话长寿。
        explicit LinguistSession(srt::SynthUnit &unit);
        /// 析构前全部 convert() 必须已返回。
        ~LinguistSession();

        /// 从 unit 已提交的包重建目录并原子发布。旧目录的持有者继续读到旧值；
        /// 就绪与失败缓存随之清空，执行体池换代（§6）。
        void refresh();

        /// 释放一个歌手的池条目与包句柄，不影响其他歌手。宿主卸载该声库前调用。
        /// 之后其语言读作 Cold；已缓存的失败保留。
        void release(const SingerRef &singer);

        std::shared_ptr<const LinguistCatalog> catalog() const;

        /// 无副作用查询：不创建执行体，不加载资源。
        LanguageStatus probe(const SingerRef &singer, std::string_view language) const;

        /// 主动预热：创建并保留执行体，使随后的转换不再加载。失败被缓存，直到下一次 refresh()。
        srt::Expected<void> warm(const SingerRef &singer, std::string_view language);

        /// 一次转换。深度、逐词锁定与逐词输出沿用 L4 的既有形状。
        /// 取消不是错误：结果包含取消生效前完成的词，调用方通过令牌区分取消与完成。
        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            convert(const SingerRef &singer, std::string_view language,
                    const Api::Linguist::L1::LinguistConvertInput &input,
                    const CancelToken &token = CancelToken());

        /// 告知会话一个歌手能演唱的音素，供 probe() 计算覆盖度（A70）。
        void setSingerPhonemes(const SingerRef &singer, std::vector<std::string> phonemes);

        /// 会话级保留标记集合，用于未声明自身集合的歌手；缺省为 SP 与 AP（§7）。
        std::vector<std::string> reservedMarkers() const;
        void setReservedMarkers(std::vector<std::string> markers);
    };

}
```

**歌手以 `SingerRef` 标识**，即 `srt::ContribLocator` 加包版本，不另行定义键。`ContribLocator`
是框架自身的身份（包 id + 类别 + 贡献 id），持有 `SingerSpec` 的宿主可直接取用，无需在两套身份
之间转换；由于 `ContribLocator` 不含包版本，版本单独携带（§12.1 第 1 条）。目录中的 `ref` 总是
带具体版本，宿主把目录条目原样传回时不会产生歧义。

**转换的输入输出沿用 L4 的 `LinguistConvertInput` / `LinguistConvertResult`**，不另立一套。会话
是生命周期层，不是第二套数据模型：`depth` 截断与逐词锁定（A17）已经把 lite 的三个 API
（`convertG2p` / `convertS2p` / 逐词预置发音）合并为一个。

> **已对代码核实**：预置了 `pronunciation` 的词标为 `copy` 但**不**带 `locked`，因而仍进入
> S2P 与 Onset 两趟处理（`LinguistExecutiveImpl.cpp` 的 pass two 只跳过带 `locked`、带 `error`
> 或 `mode = skip` 的词）。lite 的 `convertS2p(id, lang, pronunciation)` 因此可以映射为「一个词、
> 预置发音、`depth = Onsets`」。

**`setSingerPhonemes()` 的语义**：音素表由宿主提供，因为 wolf 不读取声库格式。未提供时覆盖度为
`Unknown`，这与覆盖度为零是不同的结论。会话不设覆盖度阈值，唯一的例外是完全不覆盖：语言声明的
清单为全集（`Exact`）且歌手一个音素都不覆盖时，`probe()` 报告 `Unavailable`，因为这不是降级的
路由，而是错误的路由。歌手按当前目录查找；目录中没有该歌手，或未指定版本而加载了多个版本时，
该调用被忽略，并在 wolf 日志类别下记录一条警告。`refresh()` 丢弃新目录中已不存在的歌手的音素表。

**原设计中删除的三处冗余**（复核结论）：

| 原设计 | 处置 | 理由 |
| :-- | :-- | :-- |
| `LanguageStatus::binding` | 删除 | 目录的 `LanguageEntry` 已携带同一信息，`probe()` 只报告就绪状态 |
| `refresh()` 返回 `Expected<void>` | 改为 `void` | 能加载的包已通过 Ready-2 校验，重新扫描已提交状态没有失败路径；**永不失败的 `Expected` 没有信息量** |
| 执行体池的观测 / 收缩 API | 不增加 | `release(singer)` 本身是 §5 所需的释放点，同一机制兼作两用 |

## 4. 状态机与缓存

```
                refresh()
                    │
                    ▼
              ┌──────────┐   probe()          ┌───────────┐
              │   Cold   │ ─────────────────► │  Cold     │（无副作用）
              └────┬─────┘                    └───────────┘
                   │ warm() / convert()
          ┌────────┴────────┐
          ▼                 ▼
    ┌──────────┐      ┌───────────────┐
    │  Ready   │      │  Unavailable  │  ← 原因缓存至下次 refresh()
    └──────────┘      └───────────────┘
```

**`Cold` 不保证资源能够加载，但保证不再有待定事项**（A71，收紧了 A58 原先的表述）。A69 否决
运行期能力解析之后，绑定在加载期已由 `imports[].ref` 完全确定，`maxDepth` 从 `imports` 集合直接
读出，`hasSeparatePronunciationLayer` 从其 S2P 成员的 variant 读出，覆盖度只是一次集合运算。这四项
在初始化期都已确定，且都不需要打开任何模型，因此 `Cold` 与 `Ready` 的区别只在于资源是否已在内存中。

只有 `warm()` 才能暴露的失败是：驱动未安装、模型无法打开、词典无法读取。这些是**安装环境与包
内容**的属性（A36 已有同样的划分），无法从清单中得知，任何设计都不能在初始化期确定。
`Cold → Unavailable` 这条边因此保留。

**不在初始化期预热的理由**：`Cold → Ready` 的开销不在词典。词典类资源全部在 Acquire 期由各变体
的 `createConfiguration` 解析完毕，装包时已在内存中并经 `ResourceCache` 共享；预热的实际开销是
每个执行体各一份、按 A32 / A39 / A59 无法共享的 cpp-pinyin 引擎、ONNX session 与 `lua_State`。
声明 12 种语言的声库若在初始化期全部预热，就要一次性付出 12 份开销。A63 ③ 正是为此把执行体创建
移出会话锁。初始化期需要的是**确定性**，而不是**已加载**。

三条规则：

1. **`probe()` 不改变状态**。宿主界面每帧查询一次也不得触发加载；
2. **失败与成功同样缓存**，但只有**整条路由级**的失败进缓存：**执行体创建失败**（模型打不开、
   词典读不到）与**执行体整批运行失败**（`convert()` 里 `start()` 返回错误，
   `LinguistSession.cpp:861-871`）都让该 (歌手, 语言) 变 `Unavailable` 直到下一次 `refresh()`；
   **逐词失败只是转换结果，不缓存**；**取消不算失败**，也不缓存（被取消的运行仍然返回结果，只把
   任务状态置 `Canceled`，`include/wolf/Support/ExecutiveTask.h:52-71`）。lite 用
   `failedS2pLanguages` 避免逐音符重试，这是必需的而非优化：
   对一个 500 音符的片段，一次失败会变成 500 次；
3. **失败缓存的唯一失效点是 `refresh()`**。没有超时，也没有后台重试；何时需要重新检查由宿主
   决定（安装了新包、更改了搜索路径），会话不作推测。`release()` 只清除成功缓存，保留已缓存的
   失败：释放资源不会修复一条错误的路由（`LinguistSession.cpp:697-699` 的实现注释即写明
   "releasing resources does not repair a failed route"，该函数不改动失败缓存；清空它的唯一一处
   是 `refresh()` 内的 `failed.clear()`，`:646`）。
   **宿主的重试入口因此不是某个「清除失败」接口，而是重新扫描声库、让会话重建目录**：先让包经
   加载事务 Commit（`refresh()` 只收录 `findLoadedPackage()` 已加载的包，`:417`），再调用
   `refresh()`；该函数本身就是文档化的目录重建点，同时丢弃就绪与失败缓存
   （`include/wolf/Session/LinguistSession.h:195-204`）。lite 的对应操作是**重扫声库**
   （`SynthrtEngine::refreshVoicebanks()`）：该函数每次扫描后都调用会话的 `refresh()`，与包集合
   是否变化无关（`SynthrtEngine.cpp:490-491`，经 `LanguageBridge.cpp:105-106` 透传），所以重扫
   一次即完成重试。

`Unavailable` 的 `reason` **以失败层自己的诊断为主体，但不只是它的文本**：宿主只拿得到字符串，
拿不到错误对象，因此命中失败缓存时由 `describeFailure()` 渲染**错误码的 kind 与整条 cause 链**
（`LinguistSession.cpp:236-252` 的辅助函数，`probe()` 在 `:744` 使用它），message 本身就是该 code
的罐头文本时省略 kind，以免读成 "file not found: file not found"。加载器的诊断已经明确（如
「找不到提供者」「词典根已被占用」「模型无法打开」），再包装一层只会增加排查成本。

一个已加载、声明了 `languages` 却没有挂载 linguist pipeline 的歌手不进入目录（与没有 linguist
贡献的歌手相同），但会话在扫描时为其每个语言记录原因。`probe()` 的 `reason` 以及 `warm()` /
`convert()` 的错误给出该原因（如「language zxx routes to role lang/model, whose target … is not
a linguist contribution」），而不是「no such singer」，同时在 wolf 日志类别下记录一条警告。成因
在 synthrt：加载事务只调用**已创建**的 provider 的导入校验器，若该事务不含任何 linguist 贡献，
wolf provider 从未被创建，歌手的语言映射无人校验，包照常加载。wolf 无法在这一侧拒绝该包，只能
报告原因。

失败缓存保存的是错误对象本身（`srt::Error`），而非其文本。`warm()` 与 `convert()` 重放缓存的
失败时，返回与首次失败相同的错误码与因果链：插件层以错误码区分「资源缺失」（`FileNotFound`）、
「资源不可读」（`FileNotOpen`）与「特性不受支持」（`FeatureNotSupported`），这一区分在会话层
不得丢失。

## 5. 所有权与生命周期

执行体池的成立以三层所有权为前提。这三层各不相同，且都不是常规形态。

```
LinguistSession
 └─ 每歌手：srt::PackageHandle              持有：延长包的存活
    └─ std::unique_ptr<SingerPipelineExecutive>   拥有：createPipeline 交出所有权
       └─ 每语言：N × std::unique_ptr<LinguistExecutive>   createOwnedLinguist 创建；析构即脱离
```

1. **必须持有 `PackageHandle`。** pipeline 建自 `SingerSpec&`，而 spec 位于已加载的 Package 中；
   框架明确规定「执行体必须在其贡献所属 Package 释放之前销毁」
   （`ContribExecutive.h:31-33`）。池条目因此显式持有由 `findLoadedPackage()` 取得的句柄。

   > 这一条修正了 §10 原先的表述。「不代管包的打开与关闭」是正确的，**但不代管不等于不持有**。
   > 会话在池非空期间实际延长了包的存活，因此必须为宿主提供显式的释放点：`release(singer)` 与
   > `refresh()`。宿主卸载声库前应先调用其中之一。

2. **pipeline 归会话所有**：`createPipeline` 返回 `unique_ptr`
   （`SingerPipelineExecutive.h:31-32`）。

3. **linguist 子执行体挂在 pipeline 下，`delete` 即脱离**：`createChild` 的文档规定「返回的指针
   仍由本执行体拥有；直接 delete 会在销毁前把它从本执行体摘下」（`ContribExecutive.h:70-76`）。
   **池的驱逐路径必须 `delete`**，否则子执行体会在 pipeline 下无限累积：`createLinguist` 不做
   记忆化，每次调用都新建一个（`WolfPipelineExecutive.cpp:22-39`）。公共头因此另外提供非虚的
   `createOwnedLinguist()`，返回 `std::unique_ptr<LinguistExecutive>`：指针析构即脱离并销毁，
   所有权体现在类型中。会话的池始终持有这种指针；这些指针仍须先于 pipeline 释放（slot 析构时先
   清空空闲执行体，再释放 pipeline）。

## 6. 并发与执行体池

`srt::InferenceExecutive` 是单任务面，并发依靠多个执行体（A14）。会话把多开变为内部事务：

- 池按 **(歌手 `SingerRef`, 语言句柄)** 分键；
- `convert()` 取一个空闲执行体，无空闲时新建，用毕归还；
- 转换失败、返回的词数与请求不符，或转换期间令牌已被取消时，该执行体不归还池中，而是销毁；
- **不设默认上限**。并发度是宿主线程池的属性，会话随之伸缩，池的高水位即宿主的峰值并发。

**`refresh()` 不得销毁正在使用的执行体。** 池按代（generation）管理：

- `refresh()` 换代，**立即丢弃空闲条目**，已租出的条目**在归还时**丢弃；
- 新的 `convert()` 只从新代取用，因此不会取得旧代的资源；
- 由此 `refresh()` 与 `convert()` 可以并发调用，无需宿主自行互斥。

> 这一条由复核补充。原稿写「池随 `refresh()` 清空」：若另一线程正在转换，清空会销毁运行中的
> 执行体。宿主的重新扫描与转换必然并发（装包与编辑同时进行），把互斥推给宿主等于把一个必然发生
> 的竞态交给宿主。

**会话自身线程安全**：全部成员可从任意线程调用。

**一个 unit 一个会话**是常见形态，而非限制：多个会话共用一个 unit 已经论证安全并有用例覆盖
（A78），代价只是各自建池、各自建 pipeline。

**该方案的开销小于表面估计**，因为开销最大的资源已经共享：

> ONNX 驱动按 `path` 与 `(size, hash)` 两级索引、以引用计数持有 `SessionImage`（内含
> `Ort::Session`）：同一模型文件被 N 个 `InferenceSession` 打开时，**权重只加载一份**
> （`dsinfer/plugins/inferencedrivers/onnxdriver/Session/SessionSystem.h:20-46`、
> `Session/Session.cpp:523-660`）。

随执行体重复的只有两项，且均已知：cpp-pinyin 的词表（每个实例约 1–2 MiB，A32）与
`lua_State`（A39）。两者都因上游的可变状态而无法共享，不是设计选择。

## 7. 保留词与空词

以下三类输入原先由每个宿主各自判定，现收进语言域：

| 输入 | 处置 | 依据 |
| :-- | :-- | :-- |
| 空串或仅含空白 | `mode = skip`，发音与候选为空 | **契约已规定**（链推理契约 §3.4.2 第 1 条） |
| 含空白的非空词 | 原词透传 + `error = InvalidInput` | **契约已规定**（同上第 2 条） |
| 保留标记（缺省 `SP` / `AP`） | `mode = copy`，发音与唯一候选均为该标记；音素层为单个该标记，`onset = true` | 生态惯例，非契约 |

前两条**是 G2P 契约的义务**，由模块侧承担：三个 G2P 变体各自遵守，并共用一个判定组件（与
`Verifier` 的理由相同：同一条规则的两份副本终将分叉）。该组件即 `wolf::classifyLyric()`
（`Support/InputRules.h`），三个变体在执行任何步骤之前先调用它。

第三条**是宿主惯例**，由会话侧承担，分两层：歌手自身声明的标记集合优先。`reservedPhonemes` 是
synthrt `singer` 类别的追加字段，写在声明根，类别校验其形状，DiffSinger 校验器在加载期对每个模型
校验；会话在扫描目录时直接从 `SingerSpec` 读入 `SingerEntry::reservedMarkers`，宿主无需转交。
未声明的歌手使用会话级集合，缺省为 `{SP, AP}`，采用其他记法的宿主可通过 `setReservedMarkers()`
替换。域契约 §4 已规定保留音素不进入 `exports.phonemes`，两处口径一致。

**项目专属标记仍归宿主**：连音符、`+` 后缀、切分标记属于 lite 的工程模型，不是语言域的概念。
宿主对这类词预置 `pronunciation`，或不将其送入会话。

会话与模块之间的两条边界：

- **保留词在派发前拦截**，不进入执行体。因此会话须自行按 `depth` 补齐输出形状：`Pronunciation`
  只给出发音与候选；`Phonemes` 另给出 `phonemes = [标记]`；`Onsets` 再给出 `onsets = [true]`。
  否则 S2P 会以「SP」查表；
- **宿主预置的内容优先于保留词判定**。宿主为词预置了 `pronunciation` 或锁定了音素层
  （`locked`）时，即表达了明确意图，会话不再将其与保留标记集合比对，也不以标记的形状覆盖该内容。

## 8. 随本层实施的契约修正

以下三处修正均已实现。

### 8.1 C1：`exports` 的清单与开放位

**问题**：链推理契约 §3.1 要求 `symbols` 是「可能输出的原子符号**全集**」，域契约 §4 同样要求
`phonemes` 是**全集**且**必填**。而十二条链的末步都是「查不到即输出原词」，产出因此无界：
`symbols` 无法声明任何条目（A48/A50），`phonemes` 只能作为建议值填写（A55）。**一个必填字段
实际上只是建议值**，这一矛盾必须消除。

**修正**：在同一 object 上增加一个开放位。

```json
"exports": {
    "phonemes": ["aa", "ae", "ah"],
    "openSet": true
}
```

| 键 | 含义 |
| :-- | :-- |
| 清单（`phonemes` / `symbols`） | 本模块**声明**的原子集合 |
| `openSet` | 缺省 `false`。为真表示产出可能落在清单之外 |

- `openSet` 为假时，清单即全集，语义与修正前完全一致，**既有声明无需改动**；
- `openSet` 为真时，清单是**已知部分**，宿主据此得知静态比对不完整；
- **由打包 lint 自动推导**，不依赖作者自觉：链中包含能产出任意文本的兜底步时即为真。

**收益**：宿主首次能在加载期给出有用的提示，例如「该语言可能产出声库无法识别的音素」。此前该
字段要么不存在，要么内容不实，宿主无法给出任何提示。

**实施时缩小的范围**：Onset 的 `knownPhonemes` **不增加**这个开放位。其契约已写明它是覆盖面的
下界（通配段覆盖任意输入），再增加「可能超出」的标记只是重复表达同一事实，正是需要避免的冗余。
开放位只加在自称全集的清单上。

### 8.2 C2：Onset 缺席时的缺省输出

无 Onset 模块时，逐位输出 `false`。实现早已如此（`LinguistExecutiveImpl.cpp` 的 pass three，
无 onset 成员时的分支），但契约未作规定。四个仓库中没有任何 Onset 资源，这条路径是**当前唯一
实际生效的路径**，不应只存在于代码中。现已写入域契约 §5.0，并说明不能留给宿主决定的理由：
`onsets` 与 `phonemes` 等长是既有约束，长度已经确定，剩下的只有取值这一项自由度；将其留给宿主
决定，会使两个宿主对同一个语言包给出不同的切分。

该降级说明的是**引擎能力**。随包发布的组合另有要求：每个声明语言必须达到 onset 层，且生效的是
歌手**实际绑定到的那个 linguist** 的 `imports`——声库因条目自带 `s2pFile`/`dict` 而自建 linguist
时，语言包那个带 `linguist/onset` 的 linguist 不会被绑定、其 onset 成员不生效；两方都无法提供规则
资源时必须把该语言登记为无卡拍层（已知限制）。口径、检测方式（`maxDepth`）与后果见域契约 §5.4。

### 8.3 C3：§3.4.2 输入合法性判定的实施

链推理契约 §3.4.2 规定了两条按序执行的判定（空词 → `skip`；含空白 → `InvalidInput`）。三个 G2P
变体原先都未实现：空词经 `verify` 落入 `copy` 并产出空发音，含空白的词被当作普通词送入词典，
`mode = skip` 是一个已定义却无人产出的枚举值。

现已实现，判定集中在 `wolf::classifyLyric()` 一处，三个变体在执行任何步骤之前调用它：

- **链变体**在词上设置 `settled` 标志。该标志与「携带 `error`」不同：步骤失败的词仍要经过后续
  `fallback`（变体文档已规定），而经契约判定的词在任何步骤之前即已定案；
- **拼音变体**把判定结果不为 `Accept` 的词排除在字符连续段之外。引擎读取相邻词，被契约排除的词
  不应视为相邻词，否则「银 · 空词 · 行」仍会按词组匹配读作 hang；
- **多语言变体**只把判定结果为 `Accept` 的词编入批请求，其余词在原位置回填。

三条各有一个测试覆盖（`test_PipeChain` / `test_PinyinBackend` / `test_MultiG2P`）。

## 9. 实施状态

**本文 §3–§8 描述的每一项均已实现。** 原差距表随之作废：会话本体、就绪查询与双向缓存、执行体池
与按代换代、保留标记、`classifyLyric()` 的两条判定、`openSet`、Onset 缺席时的缺省输出均已到位，
实施时的三处设计修正见 §12.1。

唯一的文档修正涉及资源缓存文档：原文「k 路并发 = k 组模型会话」的后半句对 ONNX 不成立（§6）。

## 10. 范围之外的职责

以下每一条都是**明确的决定**，而非遗漏：

- **不提供异步入口**。`convert()` 是同步调用。宿主已有自身的线程池与任务模型（lite 基于
  `IInferTask`），会话再包装一层 future 只会多出一套需要对齐的取消与线程语义。需要异步的宿主
  自行在线程中调用；
- **不报告进度**。lite 的两个任务都使用不确定进度条，没有可报告的分母；
- **不拥有 `SynthUnit`**，不代管包的打开与关闭，这属于宿主对搜索路径与安装的判断。
  **但执行体池会持有 `PackageHandle`**，见 §5：不代管不等于不持有；
- **不做后台重试与超时**，失败缓存的唯一失效点是 `refresh()`（§4）；
- **不解释项目模型**：不处理连音、切分、时值与音符 id（§7）；
- **不另立数据模型**：转换的输入输出沿用 L4 的形状（§3）；
- **不缓存逐词结果**：逐词缓存属于记忆化，与资源去重是不同的机制
  （[linguist-resource-cache.md](linguist-resource-cache.md) §1）。

## 11. 与 lite 的对接映射

| lite 现状 | 迁移后 | 备注 |
| :-- | :-- | :-- |
| `ensureLanguageReady(pkg, ver, lang)` | `warm(singer, language)` | 语义一致，失败同样缓存 |
| `convertG2p(id, lang, inputs)` | `convert(..., Depth::Pronunciation)` | |
| `convertS2p(id, lang, pron)` | `convert(..., Depth::Onsets)` + 逐词预置 `pronunciation` | 一次调用取得 phonemes 与 onsets，与现状同形 |
| `resolveLanguageRoute(id, lang)` | `probe(singer, language)` | 无副作用 |
| `VoicebankSnapshot` 的语言部分 | `catalog()` | |
| `readyLanguages` / `failedS2pLanguages` | 删除 | 由 §4 承担 |
| 重扫声库（`PackageManager::refreshInstalledPackages()` → `SynthrtEngine::refreshVoicebanks()`） | `refresh()` | 该函数每次扫描后都调用会话的 `refresh()`，失败缓存随之清空；这是宿主侧唯一的重试入口（§4） |
| SP / AP 分支 | 删除 | 由 §7 承担 |
| `G2pConvertRunner` / `G2pInputAdapter` | 删除 | 会话直接接受词表 |
| `normalizePronunciationCandidates` | 删除 | 该函数补偿的是旧栈 `DictStep` 把候选填成逐个音素的缺陷；refactor 分支已修复，wolf 中从未存在该缺陷 |
| 逐音符调用 `convertS2p` | 一次调用处理一批 | lite 的 S2P 按音符逐个调用，G2P 却按语言成批调用；本层两侧同形 |
| 尾随 `+` 去除、纯 `+` 音符跳过、`-` 连音跳过、`keepPhonemesOnWordRoots` | **保留在宿主** | 属于项目记法，不是语言域的概念（§10） |

**判据是 lite 侧代码净减少**：上表中的七项删除若不能成立，说明这一层没有真正承担相应职责。

### 11.1 基于真实资源的验证

上表的映射已实际运行验证：`test_HostFlow` 以真实资源之上的歌手包，按宿主的顺序执行整套流程，
即先取发音、由用户改写、再取音素与 onset，其间包含保留标记与项目记法。所用资源如下：

| 资源 | 来源 |
| :-- | :-- |
| cmn / yue 的 G2P | synthrt `refactor` 分支的真实套件（版本为 `convert-g2p-packages.py` 中固定的 `SOURCE_REF`），经该脚本转换为新格式 |
| 拼音 / 粤拼音节表 | 同一套件的 `assets/`（615 / 639 条） |
| 音节 → 音素词典、onset 规则 | **由 `scripts/make-voicebank-fixture.py` 生成**，见发布文档 §3.2 |

第三行是构造的资源，因为 cmn 与 yue 的音节词典属于**歌手包的内容**，四个仓库中都不存在。这也是
该流程最有验证价值之处：语言包只提供 G2P，歌手包补充 S2P 与 onset，两者在同一进程中组成一条链，
此前没有任何测试覆盖过这一组合。

## 12. 设计复核与落地修正

按「能否稳定实现 / 扩展接口是否足够 / 有无过度冗余」三项标准复核，落地时修正三处（§12.1）。以下只列
**现行口径**，逐条论证与演变过程见台账 A62（复核的结论）、A63（落地的三处修正）、A78（多会话）：

- **池持有包**：池必须持有 `PackageHandle`（执行体先于其 Package 销毁），释放点为 `release(singer)`；
  子执行体的所有权是「`delete` 即脱离」，这也是池的驱逐路径（§5）；
- **池按代管理**：`refresh()` 按代替换而不清空在飞条目；空闲条目立即丢弃，已租出的条目在归还时丢弃（§6）；
- **`Cold` 的含义**：绑定已确定、深度与覆盖度已计算，仅未预热——不再包含「路由是否存在」的不确定性
  （§4，A69 / A71）；
- **保留词拦截须按 `depth` 补齐输出形状**（§7）；一个 unit 可以共存多个会话（A78，§4）；
- **删除项**：`LanguageStatus::binding`、`refresh()` 的 `Expected`、池的观测与收缩 API（由 `release()` 兼任）；
- **明确为决定而非遗漏**：不提供异步入口、不报告进度、不缓存逐词结果（§10）。

**已对代码核实**的两条：`createLinguist` 不做记忆化（池因此可以成立）；预置发音的词仍经过 S2P 与 Onset
（lite 的 `convertS2p` 因此可以映射）。并发行为经 ThreadSanitizer 验证，`test_LinguistSession` 的并发
用例（8 线程各 60 轮转换，其间 200 次 `refresh()` 与 2000 次 `probe()`）无报告；仅有的告警位于未插桩
的第三方运行库内部，见 `Status.md` 的验证节。

### 12.1 落地时的三处修正（A63）

- **歌手键为 `SingerRef { locator, version }`**：`ContribLocator` 不含版本，而同一声库的两个版本并存
  是常见情形；版本留空表示「唯一已加载的版本」，加载了多个版本时拒绝（§4）；
- **绑定由 `WolfPipelineExtension::binding()` 直接提供**：`locate()` 返回的 locator 属于语言包，
  无法从歌手包解析；绑定在读取清单时即已确定，`probe()` 因此仍然无副作用（§4）；
- **执行体在会话锁外创建**：锁内只做低开销操作（查目录、查缓存、取空闲条目），租约在释放锁前已占用；
  销毁同理（`giveBack()` / `refresh()` / `release()` 在锁内摘下待销毁对象，锁外析构）。创建需加载词典、
  打开模型，在锁内执行会阻塞每帧都调用的 `probe()`（§5、§6）。

## 13. 迁移路径

| 阶段 | 内容 | 阻塞项 |
| :-: | :-- | :-- |
| 1 | wolf 实施 L6 与 C1/C2/C3；更正资源缓存文档中关于模型会话的前提 | 无 |
| 2 | lite 的 synthrt 端口切换到 main；`VoicebankSession` 的语言部分委托给本会话 | **B4**，由 lite 侧决策 |
| 3 | A11 实施（synthrt 分支 `onnxruntime-builds-uptake`），`SingerLanguages` 的 `configuration` 分支已拆除；剩余工作只是并入 synthrt main | 已完成，等待上游合并 |
| 4 | cpp-pinyin 把词典根改为构造参数 → A31 的仲裁器退化为空操作 | 上游 |

阶段 1 完全在 wolf 内完成。阶段 2 / 3 / 4 各自依赖外部，且互不阻塞。
