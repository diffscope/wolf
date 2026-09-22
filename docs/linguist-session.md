# wolf 语言域会话（L6）

本文钉**宿主真正持有的那个对象**。它在层栈最上层，把执行体树、就绪态、并发与保留词收进语言域
内部，使宿主只需要回答自己的问题：哪个歌手、哪种语言、哪批词。

上位规范 [spec 2.4](ds-spec-2.4.md)；分层见 [linguist-architecture.md](linguist-architecture.md)；
决策见 [linguist-decisions.md](linguist-decisions.md)。本文属 **wolf 实现文档**，不是对外契约。

> **口径**：本文写**目标形态**。实现状态见 [Status.md](Status.md)；与现行代码的差异在 §8 逐条
> 列出，不散落在正文里。

## 1. 为什么需要这一层

wolf 当前止步于「provider + 执行体」：宿主拿到的是 `WolfPipelineExtension`，从它创建
`WolfPipelineExecutive`，再创建 `LinguistExecutive`，然后 `start()`。**这中间缺的东西，每个宿主
都得自己补一遍。**

ds-editor-lite 已经补过一遍了。它的 `VoicebankSession` 在 refactor 线上提供：

```
session.ensureLanguageReady(packageId, version, language)   // 惰性、幂等、内部缓存
session.convertG2p(identifier, language, inputs)            // 按语言分组批量
session.convertS2p(identifier, language, pronunciation)     // 逐词，返回 {phonemes, onsets}
```

以及调用点里的一批附属逻辑（`GetPronunciationTask.cpp` / `GetPhonemeNameTask.cpp`）：

- 按语言分组、**逐音符**的 `language`；
- `readyLanguages` / `failedS2pLanguages` 两张集合——**成功与失败都要缓存**，否则每个音符重试一次；
- SP / AP 特判：产出单个保留音素、`isOnset = true`；
- 连音、`+` 后缀、空词的过滤；
- 分阶段就绪：`sessionReady()` 只保证元数据，模型可 `deferLanguageModels`。

**这些不是宿主的业务判断，是语言域的实现细节。** 「失败缓存到什么时候」「并发开几个执行体」
「SP 算什么」——每家给一个答案，就有三种不一致的行为，而正确答案只有一个。

`srt::SynthUnit` 不提供这一层：它只有包、插件与 Runtime Service 的注册，没有扫描、没有就绪态、
没有路由缓存（`synthrt/include/synthrt/Core/SynthUnit.h:25-95`）。这一层要么 wolf 做，要么每个
宿主各做一遍。

## 2. 会话做的六件事

| # | 事 | 现在归谁 |
| :-: | :-- | :-- |
| 1 | 歌手 → 语言的**快照**，原子发布 | 宿主（lite 有 `VoicebankSnapshot`） |
| 2 | 三态**就绪查询**与主动预热 | 宿主（lite 的 `ensureLanguageReady`） |
| 3 | 成功与失败的**双向缓存** | 宿主（lite 的两张集合） |
| 4 | **执行体池**与包的持有：并发靠多开（A14），池把「多开」变成内务，并给出释放点 | 宿主 |
| 5 | **保留词与空词**的统一处置 | 宿主（各写一遍） |
| 6 | **取消令牌**：把 A51 铺好的向下传递给宿主一个把手 | 宿主 |

会话**不做**的事见 §10。

## 3. 接口形状

```cpp
namespace wolf {

    /// 一个 (歌手, 语言) 组合此刻的可用状态。查询无副作用。
    enum class Readiness {
        Ready,        ///< 已预热：下一次转换不会再加载任何资源
        Cold,         ///< 绑定已定、形状已算，仅未预热（A71）
        Unavailable,  ///< 此处不可用，reason 说明原因
    };

    struct LanguageStatus {
        Readiness readiness = Readiness::Unavailable;
        std::string reason;   ///< 仅 Unavailable 时非空

        /// 这个语言最深能走到哪一层。由 imports 集合直接读出，不需要转换一次才知道：
        /// 缺 linguist/s2p 即 Pronunciation，缺 linguist/onset 即 Phonemes（A70）。
        Api::Linguist::L1::Depth maxDepth = Api::Linguist::L1::Depth::Onsets;

        /// linguist 的 exports.phonemes 里，宿主声库覆盖得到的比例。
        /// 宿主未经 setSingerPhonemes() 给出音素表时为 Unknown；linguist 声明
        /// openSet 时清单本就不是全集，故只能是下界（Lower）而非精确值（A70）。
        enum class CoverageKind { Unknown, Exact, Lower };
        CoverageKind coverageKind = CoverageKind::Unknown;
        double coverage = 0.0;
        std::vector<std::string> missingPhonemes;   ///< 截断到前 N 条
    };

    /// 快照里的一条语言。
    struct LanguageEntry {
        std::string handle;                       ///< 语言句柄，[a-z]{3}
        Api::Common::L1::LanguageScheme binding;
        srt::ContribLocator linguist;             ///< 承载它的 linguist 贡献
    };

    struct SingerEntry {
        srt::ContribLocator locator;
        std::vector<LanguageEntry> languages;
        std::string defaultLanguage;              ///< 作者提示，不参与匹配
    };

    /// 不可变、可跨 refresh 持有。
    class LinguistCatalog {
    public:
        const std::vector<SingerEntry> &singers() const noexcept;
        const SingerEntry *find(const SingerRef &singer) const;
    };

    /// 宿主可从另一线程置位；会话在其有效期内把它转成对已租执行体的 stop()。
    class CancelToken {
    public:
        void cancel() noexcept;
        bool cancelled() const noexcept;
    };

    class LinguistSession {
    public:
        /// 借用而不拥有：宿主可能还挂着别的类别与服务。unit 须比 session 长寿。
        explicit LinguistSession(srt::SynthUnit &unit);
        ~LinguistSession();

        /// 从 unit 当前已提交的包重建目录，并原子发布。
        /// 旧快照的持有者继续看到旧值；就绪与失败缓存随之失效，池换代（§6）。
        void refresh();

        /// 放掉一个歌手的池条目与包句柄，不动其余。宿主卸载某个声库前调用。
        void release(const srt::ContribLocator &singer);

        std::shared_ptr<const LinguistCatalog> catalog() const;

        /// 无副作用查询。不创建执行体、不加载资源。
        LanguageStatus probe(const srt::ContribLocator &singer,
                             std::string_view language) const;

        /// 主动预热：创建并保留执行体，使随后的转换不再加载。
        /// 失败被缓存，直到下一次 refresh()。
        srt::Expected<void> warm(const srt::ContribLocator &singer, std::string_view language);

        /// 一次转换。深度、逐词锁定与逐词输出沿用 L4 的既有形状。
        srt::Expected<std::unique_ptr<Api::Linguist::L1::LinguistConvertResult>>
            convert(const srt::ContribLocator &singer, std::string_view language,
                    const Api::Linguist::L1::LinguistConvertInput &input,
                    const CancelToken &token = {});

        /// 告知一个声库能唱哪些音素，据此算出 LanguageStatus 的覆盖度（A70）。
        ///
        /// 由宿主给，因为 wolf 不认识声库格式。未给出时覆盖度为 Unknown，并且不影响
        /// readiness——「没告诉我」与「一个都覆盖不到」是两回事。
        void setSingerPhonemes(const SingerRef &singer, std::vector<std::string> phonemes);
    };

}
```

**歌手用 `srt::ContribLocator` 标识**，不另发明键：那是框架自己的身份（包 id + 版本 + 类别 +
贡献 id），拿得到 `SingerSpec` 的宿主直接取 `spec.locator()`，不必在两套身份之间翻译。

**转换的 IO 沿用 L4 的 `LinguistConvertInput` / `LinguistConvertResult`**，不另立一套。会话是
生命周期层，不是第二套数据模型——`depth` 截断与逐词锁定（A17）已经把 lite 的三个 API
（`convertG2p` / `convertS2p` / 逐词预置发音）压成一个。

> **已对代码核实**：预置了 `pronunciation` 的词标为 `copy` 但**不**带 `locked`，因而仍进入
> S2P 与 Onset 两趟（`LinguistExecutiveImpl.cpp` 的 pass two 只跳过 `locked` / 带 `error` /
> `skip` 三类）。lite 的 `convertS2p(id, lang, pronunciation)` 因此确实能映射为「一个词、预置
> 发音、`depth = Onsets`」，不是纸面推演。

**三处砍掉的冗余**（复核结论）：

| 原设计 | 处置 | 理由 |
| :-- | :-- | :-- |
| `LanguageStatus::binding` | 删 | 目录的 `LanguageEntry` 已带同一信息，`probe()` 只该回答就绪 |
| `refresh()` 返回 `Expected<void>` | 改 `void` | 包能加载即已过 Ready-2 校验，重扫无失败路径；**永不失败的 `Expected` 是噪声** |
| 池的观测 / 收缩 API | 不加 | `release(singer)` 本就为 §5 的释放点而必需，一件机制两用 |

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

**`Cold` 不是「已加载」的承诺，但它是「已定」的承诺**（A71，收紧了 A58 原先的写法）。A69 否掉
运行期能力解析之后，绑定在加载期已由 `imports[].ref` 完全确定，`maxDepth` 从 `imports` 集合直接
读出，覆盖度只是一次集合运算——这三样在初始化期全部有答案，且都不需要打开任何模型。所以
`Cold` 与 `Ready` 的差别只剩「资源是否已在内存里」。

仍然只能在 `warm()` 才暴露的是：驱动没装、模型打不开、词典读不出。这些是**安装环境与包内容**
的属性（A36 已有同款分野），清单里看不出来，任何设计都不可能在初始化期回答。`Cold → Unavailable`
这条边因此保留。

**为什么不干脆把预热也搬到初始化**：`Cold → Ready` 付的不是词典。词典类资源全部在 Acquire 期由
各变体的 `createConfiguration` 解析完毕，装包时已在内存里并经 `ResourceCache` 共享；预热真正付
的是每执行体一份、按 A32 / A39 / A59 无法共享的 cpp-pinyin 引擎、ONNX session 与 `lua_State`。
声明 12 种语言的声库若在初始化期全部预热，就是一次性付清这 12 份。A63 ③ 正是为这笔开销把建
执行体移出会话锁的。「初始化时彻底处理好」要的是**确定性**，不是**已加载**。

三条规矩：

1. **`probe()` 绝不改变状态**。宿主的 UI 每帧问一次也不该触发加载；
2. **失败与成功同样缓存**。lite 用 `failedS2pLanguages` 避免逐音符重试，那是必需品不是优化：
   一个 500 音符的片段，一次失败会变成 500 次；
3. **唯一的失效点是 `refresh()`**。没有超时、没有后台重试——「什么时候该重新看」是宿主知道
   的事（装了新包、切了搜索路径），会话不猜。

`Unavailable` 的 `reason` 直接取自失败点的诊断串，不重新措辞：加载器已经把话说清楚了
（「找不到提供者」「词典根已被占用」「模型打不开」），再包一层只会让人查两次。

## 5. 所有权与生命周期

池要成立，先得把三层所有权说清楚——它们各不相同，且都不是常规形状。

```
LinguistSession
 └─ 每歌手：srt::PackageHandle              持有：延长包的存活
    └─ std::unique_ptr<SingerPipelineExecutive>   拥有：createPipeline 交出所有权
       └─ 每语言：N × LinguistExecutive*          由 pipeline 拥有；delete 即脱离
```

1. **必须持有 `PackageHandle`。** pipeline 建自 `SingerSpec&`，而 spec 活在已加载的 Package 里；
   框架明写「执行体必须在其贡献所属 Package 释放之前销毁」
   （`ContribExecutive.h:31-33`）。池条目因此显式持 `spec.package()` 取得的句柄。

   > 这修正了 §10 原先的措辞。「不代管包的打开与关闭」是对的——**但不代管不等于不持有**。
   > 会话在池非空期间事实上延长了包的存活，所以必须给宿主显式的释放点：
   > `release(singer)` 与 `refresh()`。宿主要卸载一个声库，先调其中之一。

2. **pipeline 归会话所有**：`createPipeline` 返回 `unique_ptr`
   （`SingerPipelineExecutive.h:31-32`）。

3. **linguist 子执行体归 pipeline 所有，`delete` 即脱离**：`createChild` 文档写「返回的指针仍
   由本执行体拥有；直接 delete 会在销毁前把它从本执行体摘下」（`ContribExecutive.h:70-76`）。
   **池的驱逐路径必须 `delete`**，否则子执行体会在 pipeline 下无限累积——`createLinguist` 不做
   记忆化，每次调用都新建一个（`WolfPipelineExecutive.cpp:22-39`）。

## 6. 并发与执行体池

`srt::InferenceExecutive` 是单任务面，并发靠多开执行体（A14）。会话把「多开」变成内务：

- 池按 **(歌手 locator, 语言句柄)** 分键；
- `convert()` 取一个空闲租约，无空闲则新建；用毕归还；
- **不设默认上限**。并发度是宿主线程池的属性，会话跟着走，高水位即宿主的峰值并发。

**`refresh()` 不得销毁在飞的执行体。** 池按代（generation）管理：

- `refresh()` 换代，**立即丢弃空闲条目**，已租条目**在归还时**丢弃；
- 新的 `convert()` 只从新代取，因此不会拿到旧代资源；
- 由此 `refresh()` 与 `convert()` 可并发调用，而不是要求宿主自己排它。

> 这条是复核补上的。原稿写「池随 `refresh()` 清空」——若另一线程正在转换，清空即销毁运行中的
> 执行体。宿主的重扫与转换本来就会并发（装包与编辑同时进行），把排它推给宿主等于把一个必然
> 发生的竞态交出去。

**会话自身线程安全**：`probe` / `warm` / `convert` / `refresh` 可从任意线程调用。

**一个 unit 一个会话**是常见形态，不是限制：多个会话共用一个 unit 经论证安全并有用例覆盖（A78），
代价只是各自建池、各自建 pipeline。

**这样做的代价比看上去小**，因为最贵的东西已经共享了：

> ONNX 驱动按 `path` 与 `(size, hash)` 两级索引、引用计数持有 `SessionImage`（内含
> `Ort::Session`）——同一模型文件被 N 个 `InferenceSession` 打开，**权重只载一份**
> （`dsinfer/plugins/inferencedrivers/onnxdriver/Session/SessionSystem.h:20-46`、
> `Session/Session.cpp:523-660`）。

真正随执行体重复的只有两样，且都已知：cpp-pinyin 的词表（每实例约 1–2 MiB，A32）与
`lua_State`（A39）。两者都因上游的可变状态而无法共享，不是设计选择。

## 7. 保留词与空词

三种输入现在**每个宿主各判一遍**，收进会话：

| 输入 | 处置 | 依据 |
| :-- | :-- | :-- |
| 空串或仅含空白 | `mode = skip`，发音与候选为空 | **契约已规定**（链推理契约 §3.4.2 第 1 条） |
| 含空白的非空词 | 原词透传 + `error = InvalidInput` | **契约已规定**（同上第 2 条） |
| 保留标记（缺省 `SP` / `AP`） | `mode = copy`，发音即标记；音素层为单个该标记、`onset = true` | 生态惯例，非契约 |

前两条**是 G2P 契约的义务**，落在模块侧：三个 G2P 变体各自遵守，且共用一个判定组件写一次
（与 `Verifier` 同款理由——同一条规则的两份拷贝迟早分叉）。该组件即
`wolf::classifyLyric()`（`Support/InputRules.h`），三个变体在任何步骤之前先判一遍。

第三条**是宿主惯例**，落在会话侧，分两层：歌手自己声明的记号集合优先——`reservedPhonemes` 是
synthrt `singer` 类别的追加字段，写在声明根，类别校验形状、DiffSinger 校验器在加载期对每个模型
校验，会话在扫描目录时直接从 `SingerSpec` 读进 `SingerEntry::reservedMarkers`，宿主不必转交；
没有声明的歌手回落到会话级集合，缺省 `{SP, AP}`，用别的记法的宿主可以替换。域契约 §4 已规定保留
音素不进 `exports.phonemes`，两处口径一致。

**项目专属标记仍归宿主**——连音符、`+` 后缀、切分标记是 lite 的工程模型，不是语言域的概念。
宿主对这类词预置 `pronunciation` 或干脆不送进来。

两条边界，避免会话与模块互相打架：

- **保留词在派发前拦下**，不进执行体。因此会话须自己按 `depth` 补齐输出形状：`Pronunciation`
  只给发音；`Phonemes` 另给 `phonemes = [标记]`；`Onsets` 再给 `onsets = [true]`。否则 S2P 会
  拿到「SP」去查表；
- **预置发音优先于保留词判定**。宿主显式写了 `pronunciation`，就是显式意图，会话不再拿它跟
  保留词表比对。

## 8. 随本层落地的三处契约修正（均已实现）

### 8.1 C1 — `exports` 从「全集」改为「清单 + 开放位」

**问题**：链推理契约 §3.1 要求 `symbols` 是「可能输出的原子符号**全集**」，域契约 §4 同样要求
`phonemes` 是**全集**且**必填**。而十二条链的末步都是「查不到即输出原词」，产出因此无界——
`symbols` 一条都声明不了（A48/A50），`phonemes` 只能当建议值填（A55）。**一个必填字段实际是
建议值**，这个味道必须去掉。

**修正**：同一 object 上增一个开放位。

```json
"exports": {
    "phonemes": ["aa", "ae", "ah"],
    "openSet": true
}
```

| 键 | 含义 |
| :-- | :-- |
| 清单（`phonemes` / `symbols` / `knownPhonemes`） | 本模块**声明**的原子集合 |
| `openSet` | 缺省 `false`。为真即「产出可能落在清单外」 |

- `openSet` 为假时，清单即全集，语义与今天完全一致——**既有声明无需改动**；
- `openSet` 为真时，清单是**已知部分**，宿主据此知道静态比对不完整；
- **由打包 lint 自动推导**，不靠作者自觉：链带能产出任意文本的兜底步即为真。

**收益**：宿主第一次能在加载期说出有用的话——「这个语言可能产出你的声库不认识的音素」。
此前它只能沉默，因为字段要么不存在、要么在撒谎。

**落地时缩了一处范围**：Onset 的 `knownPhonemes` **不加**这个位。它的契约本来就写明是覆盖面的
下界（通配段覆盖任意输入），再加一个「可能超出」等于把同一句话说两遍——正是要避免的冗余。开放位
只加在自称是全集的三处。

### 8.2 C2 — 把 Onset 缺席的默认写进契约

无 Onset 模块时，逐位输出 `false`。实现已如此
（`LinguistExecutiveImpl.cpp:294-296`），契约没写。四仓零 Onset 资源，这条是**当前唯一实际
生效的路径**，不该只存在于代码里。现写在域契约 §5.0，并说明了为什么不能留给宿主：`onsets` 与
`phonemes` 等长是既有约束，长度已定死，剩下的只有取值一种自由度；把它留给宿主就是让两个宿主对
同一个语言包给出不同的切分。

### 8.3 C3 — 让 §3.4.2 的输入合法性判定真正生效

链推理契约 §3.4.2 规定了两条判定次序（空词 → `skip`；含空白 → `InvalidInput`）。三个 G2P
变体原先都没有实现：空词经 `verify` 落入 `copy` 产出空发音，含空白的词被当作普通词送进词典，
`mode = skip` 是一个定义了却无人产出的枚举值。

现已实现，判定抽在 `wolf::classifyLyric()` 一处，三个变体在任何步骤之前调用它：

- **链变体**在词上置 `settled` 位。这一位与「携带 `error`」有别——步骤失败的词仍要走后续
  `fallback`（变体文档已规定），而契约判定过的词在任何步骤之前就已定案；
- **拼音变体**把非 `Accept` 的词排除在字符连续段之外。引擎读邻词，被契约摘掉的词不是邻词，
  否则「银 · 空词 · 行」会仍按词组匹配读作 hang；
- **多语言变体**只把 `Accept` 的词编进批请求，其余按原位回填。

三条各有一个测试钉住（`test_PipeChain` / `test_PinyinBackend` / `test_MultiG2P`）。

## 9. 落地状态

**本文 §3–§8 描述的每一项都已实现。** 差距表随之作废：会话本体、就绪查询与双向缓存、执行体池
与按代换、保留标记、`classifyLyric()` 的两条判定、`openSet`、Onset 缺席默认，全部在位，落地
时的三处设计修正见 §12.1。

唯一的文档修正是资源缓存那条：原写「k 路并发 = k 组模型会话」，后半句对 ONNX 不成立（§6）。

## 10. 会话不做的事

以下每条都是**决定**，不是遗漏：

- **不提供异步入口**。`convert()` 同步。宿主已经有自己的线程池与任务模型（lite 在
  `IInferTask` 上跑），会话再包一层 future 只会多一个要对齐的取消与线程语义。要异步的宿主
  自己起线程调它；
- **不报进度**。lite 的两个任务都用 indeterminate 进度条，没有可报的分母；
- **不拥有 `SynthUnit`**，不代管包的打开与关闭——那是宿主对搜索路径与安装的判断。
  **但池会持有 `PackageHandle`**，见 §5：不代管 ≠ 不持有；
- **不做后台重试与超时**，唯一失效点是 `refresh()`（§4）；
- **不解释项目模型**：连音、切分、时值、音符 id 一概不知（§7）；
- **不另立数据模型**：转换 IO 沿用 L4 的形状（§3）；
- **不缓存逐词结果**——那是记忆化，与资源去重不是一回事
  （[linguist-resource-cache.md](linguist-resource-cache.md) §1）。

## 11. 与 lite 的对接映射

| lite 现在 | 迁移后 | 备注 |
| :-- | :-- | :-- |
| `ensureLanguageReady(pkg, ver, lang)` | `warm(singer, language)` | 语义一致，失败同样缓存 |
| `convertG2p(id, lang, inputs)` | `convert(..., Depth::Pronunciation)` | |
| `convertS2p(id, lang, pron)` | `convert(..., Depth::Onsets)` + 逐词预置 `pronunciation` | 一次调用拿 phonemes + onsets，与现状同形 |
| `resolveLanguageRoute(id, lang)` | `probe(singer, language)` | 无副作用 |
| `VoicebankSnapshot` 的语言部分 | `catalog()` | |
| `readyLanguages` / `failedS2pLanguages` | 删除 | §4 收口 |
| SP / AP 分支 | 删除 | §7 收口 |
| `G2pConvertRunner` / `G2pInputAdapter` | 删除 | 会话直吃词表 |
| `normalizePronunciationCandidates` | 删除 | 补偿的是旧栈 `DictStep` 把候选填成逐个音素的缺陷；refactor 已修，wolf 从未有过 |
| 逐音符调 `convertS2p` | 一次调用一批 | lite 的 S2P 按音符逐个调，G2P 却按语言成批；本层两侧同形 |
| 尾随 `+` 去除、纯 `+` 音符跳过、`-` 连音跳过、`keepPhonemesOnWordRoots` | **留在宿主** | 项目记法，不是语言域的概念（§10） |

**判据是 lite 侧净减代码**：上表七项删除若不成立，说明这一层没有真正收口。

### 11.1 映射已按真实资源验证

上表不是照着 lite 的代码抄的映射表，而是**跑通过的**：`test_HostFlow` 用一个真实资源之上的
歌手包，把这套流程按宿主的次序走了一遍——先取发音、用户改写、再取音素与 onset，中间夹着
保留标记与项目记法。所用资源全部为真：

| 件 | 来源 |
| :-- | :-- |
| cmn / yue 的 G2P | synthrt `origin/refactor` 的真实套件，经 `convert-g2p-packages.py` 迁到新格式 |
| 拼音 / 粤拼音节表 | 同上套件的 `assets/`（615 / 639 条） |
| 音节→音素词典、onset 规则 | **由 `scripts/make-voicebank-fixture.py` 生成**，见发布文档 §3.2 |

第三行是构造的，因为它必须是：cmn 与 yue 的音节词典是**歌手包的内容**，四个仓里没有任何一份。
这也正是这条流程最值得跑一遍的地方——语言包只出 G2P，歌手包补上 S2P 与 onset，两侧在一个进程里
合成一条链，此前没有任何测试走过。

## 12. 复核结论（本设计的自检）

按「能否稳定实现 / 拓展接口是否够 / 有无过度冗余」三条自检，改了六处、砍了三处、明确了三条。

**补上的（不补即会崩或会被误用）**

| # | 问题 | 处置 |
| :-: | :-- | :-- |
| 1 | 池持有的 pipeline 建自 `SingerSpec&`，而框架要求执行体先于其 Package 销毁——原稿只说「不代管包」，没说池事实上延长了包的存活 | §5：显式持 `PackageHandle`，并给 `release(singer)` 作释放点 |
| 2 | 原稿「池随 `refresh()` 清空」会销毁**在飞**的执行体；而重扫与转换必然并发（装包与编辑同时发生） | §6：池按代管理，空闲即弃、已租归还时弃 |
| 3 | 子执行体的所有权是「`delete` 即脱离」，不 delete 就在 pipeline 下无限累积 | §5 写明，并定为池的驱逐路径 |
| 4 | `Cold` 会被当成「能用」 | §4：原写明它只表示「有路由、没试过」。**A69 / A71 之后已收紧**为「绑定已定、形状已算，仅未预热」——不做晚绑定后，「有没有路由」在加载期就已经有答案，`Cold` 不再包含那份不确定性 |
| 5 | 保留词在派发前被拦下，会话须自己按 `depth` 补齐输出形状 | §7 两条边界 |
| 6 | 多会话共用一个 unit 的形态未定 | 原答「一 unit 一会话是预期形态」。**A78 已推翻**：那条限制既没强制也没检查，而会话之间除框架加锁的 `SynthUnit` 与两个自锁的进程级单例外无共享可变状态。现由 `test_LinguistSession_TwoSessionsShareOneUnit` 钉住多会话安全 |

**砍掉的（冗余）**：`LanguageStatus::binding`（目录已有）、`refresh()` 的 `Expected`（无失败
路径）、池的观测 / 收缩 API（`release()` 已兼任）。

**明确为决定而非遗漏**：不提供异步入口、不报进度、不缓存逐词结果（§10）。

**已对代码核实而非推演**的两条：`createLinguist` 不做记忆化（池可成立）、预置发音的词仍走
S2P 与 Onset（lite 的 `convertS2p` 可映射）。

## 12.1 落地时对设计的三处修正

设计文档写在实现之前，实现时有三处不得不改。都记在这里，因为它们各自代表设计当时看错了一件事。

### 1. 歌手的键必须带版本

原设计写「歌手用 `srt::ContribLocator` 标识，不另发明键：那是框架自己的身份（包 id + **版本** +
类别 + 贡献 id）」。**括号里那句是错的**——`ContribLocator` 明写「不含 Package 版本，也不做依赖
解析」。而同一个声库的两个版本同时加载是常态（lite 自己就有 `G2pVersionAmbiguous` 这条错误）。

于是键改为 `SingerRef { locator, version }`。版本留空表示「唯一加载的那个」，加载了不止一个时
**拒绝**并说明是哪一种失败——「有好几个」与「一个都没有」的修法不同，合成一句话等于让人查两次。
目录填的是具体版本，所以宿主把目录里的条目原样递回来永远不会歧义。

### 2. 目录取绑定不能靠解析 `locate()` 的结果

`WolfPipelineExtension::locate()` 返回的是**目标贡献自己的** locator，它属于语言包，从歌手包
`resolve()` 不到。于是给扩展加了一个 `binding(language)`：绑定在读清单时就定了，问它不必创建
任何东西，`probe()` 因此仍然无副作用。

### 3. 建执行体不能在会话锁内

原稿只说「会话自身线程安全」，没说锁的粒度。第一版把建执行体也放在锁内——那一步要载词典、开
模型，可能是秒级的；期间任何一个 `probe()`（宿主每帧都在问）都会卡住，正好是池要避免的事。

改为两段：锁内只做便宜的事（查目录、查缓存、取空闲条目、必要时建 pipeline），**建执行体在锁外**。
租约在放锁之前就已占住，所以并发的 `refresh()` 不会把脚下的 slot 抽掉。

> **已用 ThreadSanitizer 验证**：`test_LinguistSession` 的并发用例（8 线程同歌手同语言各转 60
> 轮、转换途中连做 200 次 `refresh()`、转换途中 2000 次 `probe()`）在 TSan 下干净。全套 17 个
> 测试里只有 `test_MultiG2P` 报 race，栈在 `libonnxruntime.so` 内部（主线程分配、ORT 工作线程
> 释放），wolf 的帧只是调用方——未插桩库的已知误报，不是本仓的问题。

## 13. 迁移路径

| 阶段 | 内容 | 阻塞 |
| :-: | :-- | :-- |
| 1 | wolf 落 L6 与 C1/C2/C3；更正资源缓存文档的模型会话前提 | 无 |
| 2 | lite 的 synthrt 端口切 main；`VoicebankSession` 语言部分委托本会话 | **B4**，lite 侧决策 |
| 3 | A11 落地（synthrt 分支 `onnxruntime-builds-uptake`），`SingerLanguages` 的 `configuration` 分支已拆；余下只是并入 synthrt main | 已完成，合并待上游 |
| 4 | cpp-pinyin 词典根改构造参数 → A31 的仲裁器退化为空操作 | 上游 |

阶段 1 完全在 wolf 内。2 / 3 / 4 各自等外部，且互不阻塞。
