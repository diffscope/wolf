# wolf 语言域推理资源缓存设计（hash 内容寻址 · 草稿）

> **状态**：本文是 wolf 实现侧的**实现优化设计文档**，不是契约、不是变体规范，
> 与 [linguist-level-1-draft.md](linguist-level-1-draft.md)（语言契约 Level 1）、
> [linguist-g2p-variants-wolf-draft.md](linguist-g2p-variants-wolf-draft.md)（变体参考）
> 分离维护（2026-08-29 用户决策：保持两份契约文档纯净，本设计独立成文；台账 D23）。
> **透明性底线**：缓存不改变任何 exports / configuration / 运行时 IO 的可观察行为；
> 内容、键汇、命中率均不进入声明面，包作者无感知、无新义务。
> 上位规范锚点：spec 2.4（[ds-spec-2.4.md](ds-spec-2.4.md)）。

## 1. 问题陈述：重复加载是现行事实

同一资源文件被反复读盘解析，在现役两条栈上均可实测：

**synthrt 旧栈（`language-level-1` 分支）**
- ds-dict：每个任务实例 `initialize()` 逐词典独立打开并整份解析 TSV
  （`plugins/G2P/ds-dict/internal/V1/TaskImpl.cpp:49-104`）；
- multig2p：每个任务实例独立加载 `bundle.json` + `vocabulary.json` 并打开
  encoder / decoder_step_init / decoder_step 三组 ONNX session
  （`plugins/G2P/multig2p/internal/TaskImplBase.cpp:47-63,87+`）；
- chain dict 步：每个模块实例自持 `PhonemeDict` 各载一份
  （`plugins/G2P/chain/internal/Steps/DictStep.cpp:83-99`）。

**language-manager（lm，现役插件宿主）**
- ChainG2p DictStep 同上，每实例各载一份（`plugins/G2ps/ChainG2p/internal/Steps/DictStep.cpp:40`）；
- lm 的声库 Context 机制（`ContextKey{context, version}` + FQID，
  `core/include/LangCore/Support/ContextUtils.h:17-54`）放大了资源复制面：
  不同声库 context 可指向同一物理词典。

**重复形态三类**
1. 同文件、同插件、多实例：逐实例重复解析（最常见）；
2. 同文件、异插件：ds-dict 宽松解析与 S2P `dict` 严格解析各读一遍
   （`s2pFile` 缺省回落声库 manifest `dict`，同一 TSV 两族各解析一次；
   解析产物**不同族**，见变体文档 D12/§4b 互引）；
3. 异路径、同内容：公共语言包的逐项覆写=整份资源复制；路径键去不掉，内容键可去。

## 2. 生态先例（去重已有两处实现得好）

| 先例 | 机制 | 锚点 |
| :-- | :-- | :-- |
| lm DsDict | 进程级静态表 `canonical path → weak_ptr<Dictionary>`，互斥锁；命中复用、weak 失效后重载 | `plugins/Dicts/DsDict/internal/V1/TaskImpl.cpp:13-14`（声明）、`:147-163`（复用）、`:234-242`（注册） |
| OpenUtau G2pPack | 每语言进程级单例（首调加载 + lock）+ 词级推理结果 `PredCache` | `OpenUtau.Core/G2p/ArpabetG2p.cs:22-48`、`Api/G2pPack.cs:71-78` |
| lm cpp-pinyin 接线 | 进程全局词典路径（全局单资源） | `plugins/G2ps/Common/PinyinG2pTaskImplBase.cpp:26-50` |

注：cpp-pinyin 的「进程全局路径」形态与声库 Context 隔离存在张力，且其 v1.0.2 已删除
`setDictionaryPath` 旧 API（lm 仍用旧版，台账 D19 警注）；新契约拓扑下词典归各模块实例
自持——正回到本文面向的重复加载缺口。

## 3. 设计

### 3.1 缓存对象与归属

- **缓存对象 = 解析产物**：词典表、音素集、`vocabulary.json` 结构、Onset rule 集等
  加载期一次性构造的只读内存结构；不缓存原始字节（无消费方）；
- **解析产物必须基路径纯净**（硬约束）：不得捕获来源目录或解析期绝对路径；
  资源内相对引用保持相对形态、基路径由消费方在用点自带（现行证据：multig2p
  `plugins/G2P/multig2p/BundleLoader.cpp:115-122` 仅存相对文件名，`:212-221` 用点才
  与 bundle 目录拼接）。本约束是内容寻址共享的正确性前提——若产物 eager-resolve 了
  来源路径，两个包共享同内容条目时会静默串到先载者目录；违反本约束的变体解析产物
  不得进入内容寻址缓存；
- **归属 provider 执行域**：spec 2.4:442 允许 provider 级基础设施不归属于任一 Package，
  spec 2.4:586 插件常驻至 Runtime 销毁、不随 Package 卸载——缓存表挂在 provider 侧
  （wolf 未来的推理 provider 插件），Package 卸载只减少引用计数；
- **释放**：引用计数耗尽即析构（沿 lm `weak_ptr` 先例；配合 §3.3 记忆表清理）；
- **线程安全**：缓存表读写锁（共享读、独占写）；解析-插入在锁内原子完成
  （或 double-checked）防止并发重复解析同一资源。

**明确不缓存**：
- ONNX session / 推理驱动句柄——线程亲和与会话状态属 driver 面，共享风险高，
  是否缓存由驱动层另行设计，本文不管；
- 逐词查询/推理结果（OU `PredCache` 属该层，另一份设计的事）；
- Lua 脚本字节码之类的执行体（安全边界遵循沙箱约定，不进本缓存）。

### 3.2 键设计（用户定案：内容寻址 hash + 路径快路径）

```
cacheKey = { contentHash, resourceKind, parserFingerprint }
```

- **contentHash**：文件全字节的强散列（候选 SHA-256 或 BLAKE3，实现阶段选型，
  本文只要求：确定性、抗碰撞足够、流式可算、不引额外依赖前三思）；
- **resourceKind**：变体内部的资源类型名（如 `dsdict-tsv`、`s2p-dict-tsv`、
  `s2p-mapping-tsv`、`onset-rule-json`、`multig2p-vocabulary`……）。
  **同一物理文件被宽松/严格两族解析是两个 kind，绝不共享**
  （对齐变体文档 D12：两侧语义不同族、勿跨侧复用）；
- **parserFingerprint**：解析器 schema 指纹 = variant 名 + 资源格式世代
  （`formatVersion` / `bundle_version` 等原生版本字段；无原生版本字段的资源族——
  裸 TSV 等——以按 kind 声明的语法世代常量填充，如 `dsdict-tsv@1`，由缓存组件随
  资源族语法演进手工递增）；格式演进后旧条目自然不再命中，无需显式失效。
  （实现注记：进程内缓存 × 插件随 Runtime 常驻不可换（spec 2.4:584-586）⇒ 解析器实现
  修订段永不与旧修订同表相遇，不入键；未来跨进程持久化再议——届时须先解决取值来源：
  现行插件元数据无版本字段，`plugin.json` 仅根级 `name` + `interpreters`。）

**路径快路径**：provider 域另持记忆表
`canonicalPath → (size, mtime, contentHash)`：

- 同一路径再加载时先查该表，(size, mtime) 吻合即免重读盘、直接取 hash 查缓存；
- 失配或缺席才读盘并流式算 hash（读盘本来就是解析的必要 IO，hash 叠加其上，
  miss 场景的额外成本≈0）；canonical 化沿用既有路径解析设施（变量展开已完成之后
  的规范化路径，见变体文档 §1.2 公约 1）；
- **信任边界**（与 spec 2.4:382 同源的豁免口径）：「同身份 Package 内容在使用期间
  稳定」是包作者/部署方的承诺——(size, mtime) 吻合即免重读，意味着「同尺寸且同
  时间戳的原地改写」（粗粒度 mtime、保留时间戳的落盘工具所致）将被视为未变化并
  沿用既有条目；该场景违反上述承诺，缓存不为其检测负责。

命中即取得既有解析产物强引用；未命中则解析⇒插入⇒登记记忆表。

### 3.3 失效与内存边界

- 内容寻址天然无失效问题：**被检测到的变内容 = 变键**（快路径信任边界见 §3.2），
  旧键条目随引用计数归零析构；
- 记忆表条目在 (size, mtime) 失配时惰性过期；进程内数量与文件引用数同阶；
- Level 1 实现不设容量上限（解析产物为文本表结构，体量可控）；若未来引入音频/
  大矩阵类资源，再评 LRU + 上限（实现注记，二期）。

### 3.4 与契约族的关系（透明性论证）

- spec 2.4:382 明示「实现可以缓存解析结果」——本设计属实现自由，框架零改动；
- 注入点在各推理 provider 的 **Acquire** 资源装配内部（spec 2.4:425：Acquire 取得
  尚未激活的运行时资源）——缓存命中只是「取得同一资源对象」而非「重复取得」；
- 覆写语义兼容：逐项覆写=不同包路径下的资源复制；同内容则内容键命中共享、
  不同内容则键不同各载各的——两种情况的可观察行为与无缓存时逐字节一致；
- Ready/Commit 事务纪律不受影响：缓存表不含 Package 归属状态，Commit 前不可见的
  模块对象照常事务私有（spec 2.4:429-436）；
- **rollback 撤销等价性**：条目引用计数随持有它的模块对象析构归零——synthrt main
  的 rollback 为析构驱动（未 Commit 实例不经 quit/wait：`PackageHandle.cpp:17-20`
  未 loaded 即早退、直接成员析构；框架无 provider teardown 钩子，payload 以
  `unique_ptr` 持有，`ContribInterpreter.h:18-65`、`ContribSpec_p.h`）；对象所有权即
  spec 2.4:446 完成日志的等效记录，析构序即其反序撤销（spec 2.4:456）；缓存表持
  弱引用，不延缓条目死亡。他人已持引用的条目在本事务 rollback 后留存属合法
  （计数只减本事务那一份，spec 2.4:460）。

## 4. 落点与分期（实施注记，非规范性）

- **P1**：wolf 侧提供共享组件（候选 `wolf::ResourceCache`，头+实现各一），
  含单测：并发双实例对同一资源的读盘次数断言 = 1；hash 碰撞注入测试；
  记忆表 (size,mtime) 失配重读测试；
- **P2**：接入 wolf 未来推理 provider（G2PModel `multig2p-onnx` 的 vocabulary/bundle 文本件、
  DictQuery `dsdict` 的 TSV、S2P `dict`/`mapping` TSV、Onset `rule` JSON）；
  旧栈对应点位见 §1，迁移时逐点替换为缓存路径；
- **P3**：可观察性——命中/未命中计数走 provider 日志（诊断通道，不进错误通道）。

**验收基线（等价性）**：加载同一语言包两次 / 两个语言共享同一后端包的场景下，
同一路径的资源读盘与解析次数由 N 降为 1；异路径同内容时解析次数降为 1
（读盘次数不少于路径数为物理下限——不知内容前无法免读）；全部既有单测与
对照运行输出逐字节一致。

## 5. 残留待定（实现阶段定稿）

1. hash 算法选型（SHA-256 vs BLAKE3；不引新依赖前提下评估现成实现面）；
2. 容量上限/淘汰策略是否从 P1 就带（当前口径：解析产物不设上限）；
3. ONNX session / 大模型张量的缓存归属（driver 面单独立项）；
4. G2PModel 变体是否延伸音素集导出（如 multig2p `vocabulary.json` symbols 派生）——
   属契约候选演进（台账 §8.7 残面⑥），与本文无耦合。
