# wolf 仓库清理与收敛方案

本文按 **otter 已确立的同一标准**（生成物与二进制不入 git、数据经 release 分发、文档与代码一致、提交内聚、
本地锚点可回溯）对 wolf 本仓做清理与收敛，并记录执行顺序、验收判据与决策台账。

范围：仅 wolf 本仓；`war`/`wolf-midi` 等其它仓不在范围内。全程**仅本地**操作，**禁止 push**（含
`--force` 与任何变体）。本文只记录已由实测确认的事实；推断与未证实项逐条标注。

---

## 1. 需人工确认的关键点

| 编号 | 关键点 | 结论 | 来源 |
| :-- | :-- | :-- | :-- |
| D2 | 两处既有红灯怎么处理 | **先定根因再修到绿**（已执行，见 §3.1） | 用户决策（R10） |
| D3 | CI/Release 三个缺口做到哪一步 | **按复核修正后的最小形态**：G1 最小化、G2 并入 G3、G3 三条离线校验 | 用户决策（R10/R11） |
| D4 | 2.5 GB 未跟踪构建缓存 | **只删可重建的构建缓存**，保留 `build/lang-packages/{dist,unpacked}` 数据资产 | 用户决策（R10） |
| D5 | 历史压缩边界 | **只压本地未推送部分**（公开主线不动） | 用户决策（R11） |
| D6 | 方案落盘位置 | **`docs/plans/wolf-cleanup.md`**（本文） | 用户决策（R11） |
| D8 | `.tmp/` 未被 `.gitignore` 覆盖 | 建议补一行忽略；低风险、可回退 | 待确认（R12） |

## 2. 现状盘点（结论先行）

1. **「二进制包/产物不得进 git 追踪」已经达标**。跟踪文件 308 个、合计 1.39 MiB，最大者是文档
   （`docs/linguist-decisions.md`，113.5 KiB）；唯一入库的"包"是 `packages/wolf-lang-zxx` 的 4 个声明文件
   （2171 B，无资源）。133 个 `.json` 与 52 个 `.txt` 逐个裁定**无一为生成产物**；19 MB 模型与 133k 行词典
   既不在工作树也不在对象库（历史一并成立）。最小必要集合 1,420 KB / 308 文件。
2. **「测试可以从 release 下载包」的机制已经存在并跑通**，形态与 otter 同构：
   release 资产 → 纯数据 vcpkg 端口（`scripts/vcpkg-ports/wolf-lang-packages/portfile.cmake`，下载后校验
   SHA512）→ CMake config 包 → `CMakeLists.txt` 的 `find_package(wolf-lang-packages CONFIG QUIET)` →
   `src/tests/auto/CMakeLists.txt` 经 ctest `ENVIRONMENT` 注入 `WOLF_LANG_PACKAGES_SOURCE`。
   本地 `dist/` 的 15 个归档与 `assets.cmake` 的 pin **15/15 SHA512 一致**（两轮独立复算）。
   17 个 ctest 用例中只有 3 个依赖下载数据，也只有它们设 `SKIP_RETURN_CODE 77`。
   （**2026-10-03 订正**：该属性现在设在 `src/tests/auto/CMakeLists.txt` 那份 ITEMS 清单所列的**全部测试**上，
   其中有 7 个读的 `WOLF_TEST_FIXTURES_SOURCE` 是一次性夹具，CI 已加 "Generate the test fixture packages"
   步骤并把该变量传进 Configure；下面"3 个端到端用例在 CI 中恒被跳过"现在只对其中两个成立——
   `test_ConvertedPackages` 在 Linux 端真实运行，原因是该 leg 启用了 `lang-packages`（它的跳过门只看
   `WOLF_LANG_PACKAGES_SOURCE`，来自语言包端口的 config 包，与夹具步骤无关）；`test_MultiG2P`
   与 `test_HostFlow` 仍跳过（§3.2、§6）。）
3. **缺的是闭环，不是机制**：CI 两个 leg 都不启用 `lang-packages`（`.github/workflows/ci.yml:23,27`），
   因此 3 个端到端用例在 CI 中恒被跳过（`docs/Status.md:112-113` 自认），pin 的 URL 与 SHA512 从未被自动化
   触碰；发布动作（打 tag + 上传资产）无自动化；pin ↔ manifest ↔ 文档表之间无机检。
   （**时效注 2026-10-03**：G1 已落地——Linux 端现启用 `lang-packages`（`.github/workflows/ci.yml:26`），
   `docs/Status.md` 的 CI 段已同步改写；上面"两个 leg 都不启用"与所引 `:23,27` 是方案制定时的现状，见 §3.2 与 §7。）
4. **原以为的两处红灯，实际只有一处**（详见 §3.1）：一处是上游 `synthrt` 的**拒绝文案漂移**（wolf 无缺陷），
   已在批 1 修好并使门禁转绿；另一处是**本文档体系自己的门禁脚本 bug**（`if (...)` 块内 `%ERRORLEVEL%`
   解析期展开），修好后 HostFlow 真实退出码为 0。
5. **体积问题在磁盘、不在 git**：工作树 2,659 MB ≈ 2,540 MB 可重建构建缓存 + 约 96 MB 数据/验证资产 +
   1.4 MB 真源码。
6. **历史约束**：现有 20 个提交中散碎的前 17 笔**全部是 `cecedba` 的祖先，而 `cecedba` 已在
   `origin/main` 上**，压缩它们等于改写公开主线；本地 `linguistic-level-1-v2` 与对应远端分支已双向分叉
   （领先 3、落后 4）。文档内不含任何 wolf 提交 SHA，下游 pin 不构成约束。

## 3. 编号决策台账

### 3.1 D2：两处既有红灯 → 先定根因再修到绿（已执行）

- **红灯 1（真）= 上游措辞漂移**。用例 `test_LinguistIdentity_MalformedSingerLanguageMapDoesNotOpen`
  用子串校验拒绝理由（`src/tests/auto/Linguist/test_LinguistIdentity.cpp:177-181`），期望
  `does not exist` / `not one of`；上游现行文案为 `refers to a nonexistent import role` /
  `is not a key of languages`（`synthrt/lib/SVS/SingerContrib.cpp:55-57,69-71`）。校验逻辑同构、仅文案变。
  2026-10-01 那次 PASS 是**陈旧 applocal DLL 造成的假绿**（该副本仍是旧文案），不是"当时正常"。
  修法：批 1 把两条期望改成现行文案（`e6efae1`），并对另外三类夹具保持可转红（形状非法仍由
  `:185` 的"不应打开"守住）。
- **红灯 2（假）= 本文档体系自己的门禁脚本 bug**。`.tmp/verify-baseline-20261003/run-baseline*.bat`
  把调用放进 `if exist (...)` 括号块，块内 `%ERRORLEVEL%` 在解析期即展开，取到了**上一次 ctest 的 8**。
  铁证：同次运行 ctest 把 HostFlow 记为 `Skipped`，而 `Skipped` 只在进程退出码 77 时成立。
  修法：`setlocal enabledelayedexpansion` + `!ERRORLEVEL!`（批 1 的门禁脚本 `run-batch1.bat` 已修）。
  同一 bug 反方向会把"用例真失败"报成 0（**假绿**），故必须修而不是绕过。
- **遗留未证实**：Boost.Test 默认开启 CRT 退出期检查（`framework.ipp:1199` → `debug.ipp:1003-1020`），
  故每次运行都会打印泄漏转储；**转储归属未证实**（无文件/行号，需 `_CRTDBG_MAP_ALLOC` 重建才能定位），
  且它**不改写退出码**。本方案不为它新增行为。

### 3.2 D3：CI/Release 三个缺口 → 最小形态

- **G1（CI 启用下载特性）**：给**一个** leg 加 `--x-feature=lang-packages`（默认特性只拉 5 个归档、
  约 0.31 MiB）。**收益须如实写明**：只有 `test_ConvertedPackages` 会真正跑起来；`test_MultiG2P` 仍需
  14 MiB 的 `wolf-g2p-multi`（不在默认特性内，manifest 是裸依赖、CLI 无法补特性），`test_HostFlow` 仍需
  本地生成的声库夹具，故这两者在 CI 中**仍跳过**。G1 的真实价值是让 15 个 URL/SHA512 被真实触碰。
  风险：分支构建与"已发布 release 的存在性"耦合（未发布的版本会让 CI 直接 404 红）。
- **G2（发布自动化）→ 降级并入 G3**：完整发布流需要两个外仓 pin（`--synthrt`、`--cpp-pinyin-dict`），
  不算最小改动，且其独有收益（tag 与 assets 同源）已被 G1+G3 覆盖大半。改为在 G3 的校验脚本里保留
  "release 存在性/一致性"检查，发布本身仍由人执行。
- **G3（防漂移机检）**：新增离线脚本，做三条可离线验证的检查——① `assets.cmake` 的 bundle 版本 ↔ 端口
  `vcpkg.json` 的版本；② `assets.cmake` 的 SUITES 集合 ↔ manifest 的 features 集合；③ default-features
  ⊆ features。**明确不做** `docs/linguist-distribution.md` §5.1 的 markdown 字面解析（该表是合并+通配表，
  6 个数据行，字面比对不可行），也**不在 manifest 缺失时报绿**（它在 gitignored 的 `build/` 下，
  CI 中恒不存在 → 写成"存在才比"，否则是假绿）。

### 3.3 D4：磁盘清理范围（待执行）

删除可重建的构建缓存（`cmake-build-debug/`、`cmake-build-verify/`、`cmake-build-verify2/`、`build/` 下的
中间产物），**保留** `build/lang-packages/dist`（15 个归档、发布资产）与 `build/lang-packages/unpacked`
（解包树、测试数据源）以及 `build/voicebank-fixture`（脚本可重建，但重生成需解包树）。删前逐个查引用。

### 3.4 D5：历史压缩（待执行）

只压**本地未推送部分**：`cecedba` 之前的历史保持不动（它已在公开 `main` 上），把本地领先的提交
（`6972872`、`e6efae1` 及后续批次的新提交）按主题折成内聚的少数提交；压缩前逐条建 `backup/*` 锚点并把
旧 SHA 记入本文件附录。不 push，不改 `main`。

### 3.5 D7：文档失真修正（待执行）

已实测的失真项（`docs/` 内，行号以快照 `6972872` 为准，实施前须自行复核）：

| 编号 | 位置 | 复核结论 | 处置 |
| :-- | :-- | :-- | :-- |
| D7-a | `docs/linguist-implementation-plan.md:265-266` | 属里程碑退出判据，`3` 与 `1.0.1.2` 是当时值（现行打包修订号为 4） | 补"当时值"标记并指向现行出处（已改） |
| D7-b | `docs/Status.md:106` | 行内版本号与用例实测不符：真用例是消费者钉在修订 2、由安装的 `1.0.0.3` 供给 | 按 `test_LinguistLoad.cpp:168-184` 改写（已改） |
| D7-c | `docs/linguist-implementation-plan.md:195` | 复核成立：移植来源表在 `linguist-variants.md` §8（`:656`），文中写 §7 | 节号改为 §8（已改） |
| D7-d | `docs/linguist-implementation-plan.md:104-107` | **复核推翻**：B2 结论确在 `linguist-variants.md` §3.3（`:154` 起"B2 已解决"） | 不改 |
| D7-e | `README.md:221` | 复核成立：示例写 3.16，而同文件 `:109` 与 `.github/consumer` 要求 3.19 | 改为 3.19（已改） |
| D7-f | `docs/linguist-decisions.md:830` | **复核推翻**：原文已写"两种构建**当时**均经验证"，属已标注的快照 | 不改 |

D7-d 与 D7-f 由本仓实测推翻（评估报告误报），此处如实记录；六条中实际改动 4 条。

## 4. 分批执行计划

| 批次 | 内容 | 改动面 | 验收 |
| :-- | :-- | :-- | :-- |
| 批 1 ✅ | 红灯定性 + 期望文案跟上游 + 门禁脚本自修 | `test_LinguistIdentity.cpp`；本地 `.tmp` 脚本 | 门禁 `CONFIGURE/BUILD/CTEST/HOSTFLOW-EXIT=0`、17/17、`VERDICT=PASS`；提交 `e6efae1` |
| 批 2 ✅ | 本方案落盘 | `docs/plans/wolf-cleanup.md` | 文档与实测事实逐条对齐 |
| 批 3 ✅ | G3 校验脚本 + G1 的 CI 一行 | `scripts/check-release-assets.py`、`.github/workflows/ci.yml` | 脚本本地跑通（含"manifest 缺失不报绿"的负例）+ 门禁 |
| 批 4 ✅ | 文档失真（实际 4 条，D7-d/D7-f 经复核推翻）+ `.gitignore` 补 `.tmp/` | `docs/**`、`README.md`、`.gitignore` | 门禁 + 逐条 grep 复核 |
| 批 5 ✅ | 磁盘清理（实测可删 1269 MB） | 无（仅本地目录） | 删后门禁从零重建仍绿（缓存可重建） |
| 批 6 ✅ | 历史压缩（只压本地未推送部分）+ `backup/*` 锚点 | 本地 git 历史 | 树哈希相同（`dd58cd2e…`）+ 逐文件差异 0 + 锚点可回退 |

## 5. 验收标准

1. 每批结束跑同一门禁：configure / build / ctest 退出码全 0，且 `VERDICT=PASS`；ctest 保持 **17/17**。
2. 除有意改动外**树内容不变**：批 6 前后做树级对比（相对提交的内容 diff 必须为空）。
3. 全程本地：**无任何 push**、无 `--force`；远程分支与 tag 保持原样。
4. 文档：本文与 `docs/**` 的每条事实性断言可回溯到代码或实测；不可回溯者标"未证实"。

## 6. 风险与未证实项

- **远端 release 的真实可下载性与字节一致性未验证**（禁联网）：15/15 哈希是"本地 dist vs 本地 pin"，
  不能替代远端证明。CI 启用 G1 后仍需一次真实下载才能背书——而那只能在 push 之后发生。
- **G1 的收益有限**：见 §3.2，它不会让 3 个端到端用例全部在 CI 中跑起来。
- **泄漏转储归属未证实**：见 §3.1；本方案不把它升格为硬失败（无法区分测试支撑代码与被测代码）。
- **上游文案若再变**，红灯 1 的用例会再红——这是设计意图（用例绑定"现行拒绝理由"），不是脆弱点。
- `docs/linguist-distribution.md` §5.1 是人工维护的合并表，G3 不比对它；表仍可能漂移，靠 G1 的真实下载兜底。

## 7. 执行结果

| 批次 | 结果 | 证据 |
| :-- | :-- | :-- |
| 批 1 | ✅ 提交 `e6efae1`（后并入压缩提交） | 门禁 `CONFIGURE/BUILD/CTEST/HOSTFLOW-EXIT=0`、ctest 17/17、`VERDICT=PASS` |
| 批 2 | ✅ `docs/plans/wolf-cleanup.md`（本文） | 132 行，编码无 BOM、无 CR |
| 批 3 | ✅ `scripts/check-release-assets.py` + 其单元测试 + `.github/workflows/ci.yml` 两处 | 真实仓库 35 项检查全过（含 15 个归档 SHA512 **现场重算**）；四个负例分别转红；manifest 缺失时 0 退出但显式报 `unverified`；CI 的 unittest 发现步骤整跑 exit 0 |
| 批 4 | ✅ 4 条文档失真修正 + `.gitignore` 忽略 `.tmp/` | 六条候选中 D7-d/D7-f 经复核**推翻**（评估误报），实际改 4 条；改后工作区完全干净 |
| 批 5 | ✅ 删除 1269 MB 可重建缓存（`cmake-build-verify2/-verify/-debug`、`build/agent-tests`、`build/cmake`） | 删后门禁**从零重建**仍 17/17 全绿（证明可重建）；保留 `dist`/`unpacked`/`voicebank-fixture`/`vcpkg_installed`；工作树 2659 MB → 约 936 MB |
| 批 6 | ✅ 本地未推送的 7 笔压成 3 笔（`53fb4b5` 代码 / `71f9cdf` 测试与 CI / `92232a9` 文档） | 压缩前后**树哈希相同**（`dd58cd2e…`）、逐文件差异 0、模式位一致；锚点 `backup/pre-local-squash-20261003`、`backup/pre-cleanup-6972872` |

> **2026-10-03 二次压缩（本计划执行完之后）**：本地未推送部分又按主题压成 6 笔，本节与 §4 引用的批次
> 提交 SHA（`e6efae1`、`53fb4b5`、`71f9cdf`、`92232a9` 等）**已不在分支上**，仍可由锚点
> `backup/pre-squash-20261003-wolf` 检出；当前增量以 `git log cecedba..HEAD` 为准，按「提交主题 + 文件
> 路径」定位而不是按 SHA。二次压缩同样以树哈希守卫（`fc42451d…` 前后一致、逐文件差异 0）。

压缩过程中被守卫抓到的实际事故：第一次重做分组时 `git add` 丢掉了 3 个 Python 脚本的**可执行位**
（`100755 → 100644`），树哈希守卫当场发现并按旧 tip 恢复——这正是"压缩后必须比树哈希"的价值。

未压缩部分（`cecedba` 及之前 17 笔）保持原样：它们是公开 `main` 的祖先，压缩等于改写公开历史。

