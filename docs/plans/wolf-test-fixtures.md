# wolf 测试夹具去 package 化方案（测试不携带 package）

本文按 `docs/plans/wolf-cleanup.md` 已确立的同一标准（**生成物不入 git**、数据经生成器复现、文档与代码一致）处理
`src/tests/auto/packages/**`：把 41 个测试夹具包、141 个文件从"入库的 package 目录树"改成"由脚本生成的构建产物"。
范围：仅 wolf 本仓；全程**仅本地**操作，**禁止 push**。

## 需人工确认的关键点（决策台账见文末）

1. **D-WTF1 生成器形态**：Python 脚本（推荐，与 `scripts/make-voicebank-fixture.py` 同族）／测试内 C++ 生成／单一数据文件 + 展开器。
2. **D-WTF2 生成时机**：构建期自定义目标（推荐）／仅手动生成（门禁可能静默缺数据）。
3. **D-WTF3 `loader-verdicts.json`**：保留跟踪并增加覆盖校验（推荐）／改由生成器一并产出。
4. **D-WTF4 生成目录**：`${CMAKE_BINARY_DIR}/test-fixtures`（推荐）／源码树内 gitignored 目录。
5. **D-WTF5 otter 是否同批**：不同批，第三阶段单独一轮（推荐）／本轮一并做。

## 1. 现状（2026-10-03 实测）

- 夹具根目录由 `src/tests/auto/CMakeLists.txt:122` 的 `_wolf_fixture_dir`（= `${CMAKE_CURRENT_SOURCE_DIR}/packages`）
  经 `WOLF_TEST_FIXTURE_DIR` 传给 **7 个测试**：`Linguist/test_LinguistIdentity`、`Linguist/test_LinguistLoad`、
  `Inference/test_PipeChain`、`Inference/test_PinyinBackend`、`Inference/test_MultiG2P`（条件编译）、
  `Runtime/test_LinguistRuntime`、`Runtime/test_LinguistSession`（宏定义处 `:137`、`:153`、`:165`、`:209`、`:221`）。
- 规模：41 个包目录、141 个跟踪文件、合计 43.9 KB，最大单文件 1.1 KB；扩展名 `.json ×107`、`.txt ×33`、`.lua ×1`。
- 内容形态：spec 2.4 清单（`desc.json` 含 `id`/`version`/`runtimeLevel`/`contributions`/`dependencies`），
  包间有真实依赖图（如 `lang-cmn/desc.json` 依赖 `wolf/test-pinyin-engine`），并含反例包（`bad-scheme`、`no-language`、
  `unknown-field`、`singer-lang-mismatch` 等）。
- `src/tests/auto/loader-verdicts.json`（3,439 B，跟踪）自述"记录 loader 在每个夹具包上的实际判定，
  `packages/` 下每个目录恰好列出一次"，由 `test_LinguistLoad` 逐包核对；`test_LinguistLoad.cpp:242` 会遍历整个夹具目录。
  该文件按判定分三组（`refused` 13 + `refusedByTheLoaderOnly` 8 + `accepted` 20 = 41，与夹具目录**当前完全同步**）。
- **该目录有第二个消费者**：`scripts/test_check_declarations.py:24` 硬编码 `FIXTURES = <repo>/src/tests/auto/packages`，
  并在 `:80-84` 的 `test_every_test_package_has_a_recorded_verdict` 里遍历它核对 verdicts。夹具改为生成后，
  这个脚本必须改为指向生成目录（或按需自行生成），否则它会**静默 lint 0 个包**而测试仍然"通过"。
- 现状可达性：夹具目录与唯一发布包 `packages/wolf-lang-zxx` 无同名项，二者是两回事。
- **本仓已有"测试自己造包"的先例**：`Inference/test_LuaVariants.cpp:32-47`、`Inference/test_OnsetRules.cpp:24-39`、
  `Inference/test_PipeChain.cpp:69`、`Inference/test_MultiG2P.cpp:99-101` 都在运行期用 `ofstream`/`fs::copy` 现造包。
- **生成器先例**：`scripts/make-voicebank-fixture.py`（266 行）+ `CMakeLists.txt:185` 的 `WOLF_VOICEBANK_FIXTURE_SOURCE`
  缓存变量 + 缺数据时测试以 77 退出；文档在 `README.md:200`、`docs/Status.md:81`、
  `docs/linguist-distribution.md:372-374`。
- 脚本目录惯例：`scripts/declarations.py` 供多个脚本共享常量，`scripts/test_check_declarations.py` 与
  `scripts/test_check_release_assets.py` 说明脚本自带单测；CMake 目前**从不调用 Python**。
- `.gitignore` 已含 `build/`、`cmake-build*`，构建目录里的生成物天然不入库。

## 2. 目标与判据

1. 仓库内不再存在任何 package 目录树：`git ls-files 'src/tests/auto/packages/*'` 为空。
2. 全部现有测试行为不变：生成结果与现有 141 个文件**逐字节相同**；门禁通过/跳过状态与基线一致
   （基线为 17 个 `wolf_add_test` 目标的当前环境结果，其中 `LuaVariants`/`MultiG2P` 受条件编译影响），
   且**夹具相关的 7 个测试不得新增 skip**。
3. 夹具内容仍可读、可评审、可逐包说明用途（不允许退化成不可读的二进制或黑箱压缩）。
4. 生成过程确定：同输入重复运行产物一致；生成器自带 `--check`（生成到临时目录并与目标比对，未改动时应 0 差异）。

## 3. 方案

### 3.1 生成器 `scripts/make-test-fixtures.py`

- 结构照抄 `make-voicebank-fixture.py`：`argparse` + `main()` + `if __name__`，输出目录用 `--output` 指定；
  复用 `scripts/declarations.py` 里的 role 常量，避免第二份真相。
- 数据以 Python 字面量表表达：每个夹具包一条记录（目录名 → 相对路径 → 文件内容），按用途分组并逐组写注释
  （清单/版本反例、语言图、歌手图、拼音引擎、未知字段等）。文本一律 **LF**、UTF-8、无 BOM，保证跨机一致。
- 提供 `--check`：生成到临时目录后与目标目录递归比对，差异即非零退出（服务于 `tool-metric-selfcheck` 的
  "未改动代码上 save→check 必须 0 差异"）。
- 提供 `--list` 打印包名清单，供人工核对与文档引用。
- 伴随 `scripts/test_make_test_fixtures.py`：断言确定性（两次生成一致）、包名集合与 `--list` 一致、
  `--check` 在未改动时为 0、以及每个包都存在 `desc.json`。

### 3.2 CMake 接线

- **实现偏离（2026-10-03 落档）**：最终**没有**采用"构建期生成"。`CMakeLists.txt:188-191` 明写
  `scripts/make-test-fixtures.py` generates them and **the build does not**，理由是"仓库只带测试代码、不带包产物"；
  全仓 tracked 的 CMake 里既没有 `Python3` 也没有 `add_custom_target`。实际接线是：缓存变量
  `WOLF_TEST_FIXTURES_SOURCE` 指向**外部生成目录**，测试经测试属性 `ENVIRONMENT` 读到它，缺数据时以 **77 退出**
  （ctest 记为跳过，不静默通过）。下面三条原始设想保留作历史记录，**不要照它实现**：
- ~~`CMakeLists.txt` 顶层新增缓存变量 `WOLF_TEST_FIXTURES_SOURCE`（默认空）：为空时由构建生成，非空时直接使用该目录。~~
- ~~生成用一个自定义目标（如 `wolf_test_fixtures`）产出到 `${CMAKE_BINARY_DIR}/test-fixtures`：在 `WOLF_BUILD_TESTS=ON` 时必需，`find_package(Python3 COMPONENTS Interpreter REQUIRED)` 找不到解释器时**配置期报错**。~~
- ~~`src/tests/auto/CMakeLists.txt`：`_wolf_fixture_dir` 改为生成目录，并对 `_wolf_tests` 里每个测试加该目标的依赖。~~
- `loader-verdicts.json` 保持跟踪（它记录的是 loader 的观察结果，不宜由生成器自动盖章）；
  新增校验：按 JSON 解析比较键集合与 `packages` 目录集合（注意该文件里还有 `accepted`/`refused`/`refusedByTheLoaderOnly`
  这类分组，比较必须解析结构而不是正则扫字符串），要求二者不多不少。校验已写入 `scripts/test_make_test_fixtures.py`。
- `scripts/test_check_declarations.py` 的 `FIXTURES` 改为：优先取环境变量指定的生成目录，未指定时导入生成器
  生成到临时目录；并断言待检包数大于 0（防"0 例静默通过"）。

### 3.3 文档同步

- `README.md`（`WOLF_VOICEBANK_FIXTURE_SOURCE` 邻近段落）：写明测试夹具由 `scripts/make-test-fixtures.py` 生成、位于构建目录、不需要入 git。
- `docs/Status.md`：脚本表新增该脚本一行，并订正测试数据来源的描述。
- `docs/linguist-distribution.md`：在已有的夹具说明段旁补一句测试夹具的来源。

## 4. 实施顺序（等价重构，分阶段可回滚）

> **已完成（2026-10-03，7 笔提交 `59a7fdf`…`a845ea2`，工作区干净）**：步骤 1–7 全部执行完毕。
> （2026-10-03 二次压缩后：这里的 SHA 与 `de708ca` 已不在分支上；当前增量按「提交主题 + 文件路径」定位。
> **订正**：文中提到的锚点 `backup/pre-squash-20261003-wolf` 在本机不存在，现行锚点是三次压缩后的
> `backup/pre-squash-20261003b-wolf`。）
> 生成器由 **git 对象库**字节一次性转录而成（41 包 / 141 文件，表 52,047 B）；生成结果与删除前提交
> `de708ca` 记录的字节**逐字节一致**（`tracked 141 / generated 141 / mismatches 0 / extras 0`）；
> `--check` 在未改动代码上 0 差异；`scripts/test_make_test_fixtures.py` 8 项用例通过（含 verdicts 覆盖与
> 纯 LF 两条守护）。门禁在本地 setdir synthrt 上 **17/17 全绿（0 失败 0 跳过）**，夹具用例直接对生成目录
> 运行；数据缺失时退出码 77 并打印 SKIP。
> 门禁抓到并修掉两处真实缺陷：① 生成器文档串仍写着旧宏名 `WOLF_TEST_FIXTURE_DIR`；
> ② `test_LinguistLoad` 用"夹具目录的父目录"定位 verdicts 记录，改生成目录后失效（已改为显式
> `WOLF_TEST_VERDICTS_FILE`）。

**门禁离线复现配方**（本机 `build/vcpkg_installed` 里的 synthrt 比 wolf 自己的 pin 还旧，且与 overlay 的同名
`synthrt`（refactor 线）混装、会遮蔽新头文件，故不能直接跑原生门禁）：

```
1) 由本地 synthrt 构建目录导出一个安装前缀：cmake --install <synthrt build> --prefix <A>
   （产出 <A>/lib/cmake/{dsinfer,synthrt}/ 与 <A>/bin/synthrt*.dll）
2) 复制影子安装根：robocopy build\vcpkg_installed <B> /E
3) 剔除影子根里陈旧的 synthrt/dsinfer 部件：<B>\x64-windows\{include\synthrt,include\dsinfer,share\synthrt,share\dsinfer}
4) 把 <A>\bin\synthrt.dll 与 synthrt-dsinfer.dll 覆盖进 <B>\x64-windows\bin\
5) 配置：cmake -S . -B build/cmake-shadow -G Ninja -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=<B> -DVCPKG_MANIFEST_MODE=OFF
     -Ddsinfer_DIR=<A>/lib/cmake/dsinfer -Dsynthrt_DIR=<A>/lib/cmake/synthrt -DWOLF_BUILD_TESTS=ON
     -DWOLF_TEST_FIXTURES_SOURCE=<abs>/build/test-fixtures
     -DWOLF_LANG_PACKAGES_SOURCE=<abs>/build/lang-packages/unpacked
     -DWOLF_VOICEBANK_FIXTURE_SOURCE=<abs>/build/voicebank-fixture
6) python scripts/make-test-fixtures.py --output build/test-fixtures → cmake --build build/cmake-shadow → ctest
   预期 17/17 全绿、0 跳过。
```

长期解法见下节风险：刷新 vcpkg 的 `synthrt-main` port，或把 wolf 的开发流程固定为 setdir（本地装有 synthrt 的
前缀）。

1. **基线快照**：对现有 141 个文件生成哈希清单（`git ls-files -s` + sha256），落 `.tmp/`；记录当前门禁结果（17/17）。
2. **转写**：用一次性本地导入脚本把现有 141 个文件转成生成器的字面量表（导入脚本不提交，放 `.tmp/`）。
3. **等价性验证（删文件之前）**：生成到临时目录，与现有 `src/tests/auto/packages` 递归比对；
   要求文件集合、相对路径、字节内容、行尾全部一致（`git diff --no-index` 0 输出）。
4. **接线**：加生成目标与依赖、改 `_wolf_fixture_dir`、加覆盖校验，保持夹具目录仍在源码树（此时双重来源并存，先证明可切换）。
5. **删除**：`git rm -r src/tests/auto/packages`，`loader-verdicts.json` 保留。
6. **门禁**：从零重建 + 全量 ctest，要求 17/17 全绿且**无 skip**；抽查 `test_LinguistLoad`（遍历全部包的那个）。
7. **文档与提交**：按上表同步文档；提交按主题拆分（生成器与单测／CMake 接线／删除夹具／文档）。
   本仓提交信息用英文散文、无 AI 署名；不提交 `.tmp/` 与一次性导入脚本。

## 5. 风险与对策

| 风险 | 对策 |
| :-- | :-- |
| 转写过程中内容被悄悄改动 | 第 3 步先于删除做逐字节比对；哈希清单留档；比对不通过即停止 |
| ~~生成的目录被测试写坏（同一次运行内）~~ | 不适用：生成物既不在构建树内、也不由构建重生成（见 §3.2 实现偏离）；测试对夹具只读（现有测试写入的都是各自临时目录） |
| 行尾或编码漂移导致 Windows 上比对失败 | 生成器显式写 LF/UTF-8；比对用字节级 |
| 缺 Python 的解释器环境 | 生成器是独立脚本，缺 Python 时无法生成；**构建不会报错**，测试因缺数据以 77 跳过（见 §3.2 实现偏离）——把 `WOLF_TEST_FIXTURES_SOURCE` 指向别处的预生成目录即可 |
| verdicts 与生成集合漂移 | 新增集合相等校验，纳入门禁 |
| 夹具数量继续增长 | 生成器成为唯一入口，新增夹具=改一处表并跑 `--check` |

## 6. 不做的事

- 不改夹具内容与语义（等价重构），不顺带重命名包或重排清单。
- 不动 `packages/wolf-lang-zxx`（发布包，非测试夹具）。
- 不在本轮处理 otter（第三阶段单独进行，标准与本方案一致）。
- 不做远端操作；不 push。

## 7. 决策台账

| 编号 | 决策 | 推荐 | 取舍 |
| :-- | :-- | :-- | :-- |
| D-WTF1 | 生成器形态 | Python 脚本（与 `make-voicebank-fixture.py` 同族，数据可读、可单测） | 测试内 C++ 生成：无新依赖但 44 KB 内容变 C++ 字面量、评审差；单一数据文件+展开器：文件多一层间接 |
| D-WTF2 | 生成时机 | 构建期自定义目标 + 配置期缺解释器报错 | 仅手动生成：依赖人工步骤，门禁可能静默缺数据 |
| D-WTF3 | verdicts 表 | 保留跟踪 + 集合覆盖校验 | 生成器一并产出：等于自动盖章"观察结果"，失去人工复核价值 |
| D-WTF4 | 生成目录 | `${CMAKE_BINARY_DIR}/test-fixtures`（已 gitignored） | 源码树内 gitignored 目录：贴近现状但污染工作树、易被误提交 |
| D-WTF5 | otter 批次 | 第三阶段单独一轮 | 同批：改动面过大、单轮不可验 |
