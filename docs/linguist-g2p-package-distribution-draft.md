# G2p 公共语言包发布与版本兼容设计（资源仓 · vcpkg port · 编辑器更新 · 草案）

> **状态**：本文是 wolf 实现侧的**发布与运维设计文档**（与
> [linguist-g2p-resource-cache-draft.md](linguist-g2p-resource-cache-draft.md)
> 同层的「非契约实现文档」）：钉 G2p 公共语言包的分发渠道、资产清单、
> 版本兼容纪律与编辑器更新义务。契约面的对应增补已并入
> [linguist-level-1-draft.md](linguist-level-1-draft.md)《公共语言包·版本纪律》、
> [linguist-g2p-variants-wolf-draft.md](linguist-g2p-variants-wolf-draft.md) §3 执行细则、
> [linguist-runtime-api-draft.md](linguist-runtime-api-draft.md) §4.1
> （决策台账见变体文档 §8.11 D26-D28）。
> 上位规范：spec 2.4（[ds-spec-2.4.md](ds-spec-2.4.md)），冲突时以 spec 为准。
> 锚点口径：spec 2.4 行号取本仓文档；synthrt 构建锚点取本分支 HEAD；
> lite 消费点锚点取 lite 工作区 `src/**` 实测；wolf 仓取 `cecedba`
> （2026-09-05 复核未漂移、工作区干净）。
>
> **修订注记**：2026-09-06 第二轮——新增决策 D-P5~P8（port 定居共用
> overlay 子模块、wolf 构建测试自动配置、每套件独立 zip 与 features 对应、
> 发布脚本三件套），新增 §6b；§2/§5/§6/§8/§10 相应增补。
>
> **适用范围注记**：版本兼容模型（§3）作用于 spec 2.4 Package 世界，自
> DSPK 化迁移（§8 I2）起生效；过渡期（旧格式 G2pPackage 资产）发布链路
> 只解决「资产在哪、怎么下载、怎么校验」，兼容语义暂无消费方。

## 决策台账（2026-08-31 首轮选项式提问拍板 D-P1~P4；2026-09-06 第二轮 D-P5~P8）

- **D-P1（发布仓库）**：**新建独立资源仓**（wolf 项目名下，建议名
  `wolf-g2p-packages`，实施时定）。理由：资源版本节奏与代码发布解耦；
  许可证隔离（待核实套件不进官方流）；发布 CI 只管资产。备选「wolf 仓
  release」被否：资源 tag 与代码 tag 混用命名空间、发布节奏互相绑架；
- **D-P2（版本模型）**：**沿用 spec 2.4 现成模型**，不为依赖方新增区间
  文法（§3.1）。备选「spec 增依赖方 min/max 区间文法」被否：需改 spec、
  框架求解器与已发布包缺省语义，且兼容区间信息本就只在资源包作者手里；
- **D-P3（git 内资源目录）**：**渐进退役**——先建发布链路，synthrt git 内
  `resources/G2pPackages` 降级为旧格式夹具，DSPK 化迁移时转最小测试样例
  （§8）；备选「立即移除」「永久双轨」被否；
- **D-P4（首轮版式）**：三份契约文档维持上轮完成的版式重构，仅补拆第八轮
  新增的两条超限行（记录于变体文档 §8.11，与本文无直接关系）；
- **D-P5（port 定居点，2026-09-06 用户指定）**：port `wolf-g2p-packages`
  定义落在 **vcpkg-overlay 仓库**（`stdware/vcpkg-overlay`——lite 与 wolf
  经 `.gitmodules` 共用的同一子模块，lite 已 pin 含 synthrt 适配提交
  `f2aff64`，实质为本项目维护面）；用户指令字面落点
  `ds-editor-lite/scripts/vcpkg/ports/wolf-g2p-packages/` 即该子模块内，
  两个消费仓经既有 `vcpkg-configuration.overlay-ports: "../vcpkg/ports"`
  接线**零改动**获得 port。备选「lite 仓 sibling overlay 目录
  （`scripts/vcpkg-local/ports`）」被否：与共用子模块模式冲突、wolf 侧
  需重复定义 port；备选「提交进 synthrt 仓内 overlay」同否（synthrt 不经
  子模块消费该 overlay，见 §6 消费矩阵第三行）；
- **D-P6（wolf 构建测试自动配置，2026-09-06 用户要求）**：wolf 经 manifest
  feature `g2p-tests` 门控依赖 `wolf-g2p-packages`（默认全量缺 eng，按需
  裁剪），CMake `find_package(wolf-g2p-packages CONFIG)` 取资源根目录
  `WOLF_G2P_PACKAGES_DIR` 送达测试（详见 §6b）；未启用 feature 时包相关
  测试注册为 skipped，不阻塞常规构建；
- **D-P7（每套件独立 zip 与 features 一一对应）**：16 套件逐个压缩、逐个
  feature（§6 features 表）；夹具实测总量 32M（Multi 19M 含模型、Jpn 仅
  13K、其余 0.1~4.3M）——全量缺省的自动安装成本低，无需再按权重拆档；
- **D-P8（发布脚本三件套）**：资源仓单脚本 `scripts/make-release.ps1`
  一次产出 zip 资产 + `manifest.json`（编辑器/插件管理器消费）+
  `assets.cmake`（port 消费，update-assets.ps1 同款格式）；port 侧
  `assets.cmake` 与 `vcpkg.json` version-string 为**生成物**，随每次
  release 同步并提交 overlay（流程钉于 §6.3）。

## 1. 问题陈述

1. **资源在 git 里、发行物边界无机制**：`resources/G2pPackages/**` 16 套
   包全量 checkin（含 133k 行 cmudict、ONNX 权重等大文件，规模见变体文档
   §8.7 调研事实），`CMakeLists.txt:251-253` 全量 `install(DIRECTORY)` 进
   `share/synthrt/G2pPackages`；`Phonetic-Suite-Eng` 的「不入发行物」只写在
   `resources/G2pPackages/README.md:3-8`，install 规则并未排除（dsinfer-cli
   staging 倒是排除了，`tools/dsinfer-cli/CMakeLists.txt:100-107`）——README
   口径与构建规则存在漂移，发行物边界没有可机检的载体；
2. **旧格式、版本语义未接线**：全系为旧版 G2pPackage 格式（变体文档 §2
   实现注记：迁 spec 2.4 是结构重写）；旧栈声库 manifest 的
   `g2pPackageVersion`（如 `1.0.0.0`，`domains/ds-bank/lib/PackageParser.cpp:120-122`）
   被解析后仅透传——`lib/G2P/LanguageServiceLang.cpp:100-101` 赋值进路由
   数据后无任何匹配逻辑消费，lite 侧也只透传至包管理信息面
   （`lite PackageManager.cpp:344-346`）；
3. **声库固定版本的兼容隐患**：若声库按精确版本理解 `dependencies[].version`
   （或发布方缺省 `compatVersion=version` 形成单点区间），编辑器把资源包从
   1.x 更新到 2.x 后，所有 pin 旧版本的在役声库将同时失配——即「更新资源包、
   老声库全灭」；
4. **chainG2P 碰到比自己更新的资源包**：chain 资源格式版本
   （`formatVersion`）的拒绝机制已有（变体文档 §1.2 公约 2、§3），但「资源
   包新于插件」时宿主如何呈现、如何与依赖求解联动，未成文。

## 2. 目标架构

```
资源仓 release（tag = 资源集 bundle 版本）
  ├─ wolf-lang-<iso>-<version>.zip × N   // 每套件一个资产（D-P7）
  └─ manifest.json                        // 机器可读清单（file + sha512 + 版本四元组）
        ├── vcpkg port wolf-g2p-packages  // port 定义落 vcpkg-overlay 仓（D-P5），
        │                                 //   assets.cmake 为 release 生成物（D-P8）
        │        ├─ ds-editor-lite  // 既有 overlay 子模块零改动接线；
        │        │                  //   部署期拷贝进运行期包根
        │        ├─ wolf            // 既有 overlay 子模块零改动接线；
        │        │                  //   构建测试经 feature g2p-tests 自动配置（§6b）
        │        └─ synthrt（可选）  // 构建测试经 SYNTHRT_G2P_PACKAGES_SOURCE（§8），
        │                           //   overlay 接线为可选项
        ├── 编辑器发行物内置               // 部署期拷贝进运行期包根
        └── 编辑器插件管理器（未来）        // 运行期检查更新 + 并存安装
```

- **发布物**：DSPK 化的 `wolf/lang-*` 公共语言包（包结构与契约见 level-1
  《公共语言包》）；过渡期资产为现行旧格式套件 zip（解包即目录），清单
  `format` 字段区分（§5）——发布链路先于格式迁移落地，消费方目录结构零改动；
- **单一事实源**：各消费链共享同一 release 资产与同一 SHA512 清单，
  校验口径不随消费方分叉。

## 3. 版本兼容模型（规范性）

### 3.1 区间在提供方，目标点在依赖方

- **依赖方（声库）**：`dependencies[].version` 写**目标版本**——实际依赖的
  最低语义版本。上位规范明示该值「不表示必须加载该精确版本」（spec
  2.4:161）。例：`{ "id": "wolf/lang-cmn", "version": "1.0.0.0" }`；
- **提供方（公共语言包）**：`[compatVersion, version]` 区间承诺（spec
  2.4:183-187）；缺省 `compatVersion = version` 即单点区间（:149）——
  发布纪律（§3.2）的意义正在于对抗这个缺省；
- **求解（框架既定，零改动）**：候选条件=依赖目标落在区间内（:392/:398）；
  同一路径取最高版本候选（:406）；选中后失败**不回退**其他候选（:396）；
  多版本可同时加载（:402）；同身份首次出现即遮蔽后续来源（:404）；
- **区间直觉的映射**：「声库设置 `1.0.0.0<=xxx<=2.0.0.0`」的诉求等价实现为
  「声库 pin 目标 `1.0.0.0` + 公共包声明 `compatVersion=1.0.0.0,
  version=2.0.0.0`」——编辑器把 1.x 更新到 2.0（区间覆盖 `1.0.0.0`）时，
  pin `1.0.0.0` 的声库命中 2.0.0.0（同路径最高版本候选），照常工作；只有
  破坏性更新（compatVersion 抬升）才使旧目标失配，此时按 §3.3 并存安装
  保留旧版，声库继续解析到 1.x；
- **为什么不为依赖方新增区间文法（D-P2）**：「从哪版起破坏」的信息只掌握
  在资源包作者手里——上位规范把兼容承诺的主体责任钉在 Package 作者
  （:189-198 兼容承诺清单）；声库作者预设兼容上限是对未来的猜测，写错即
  假阳性拒绝。若未来确有「声库明知不兼容更高版本」的真实需求，按 spec
  演进流程另立项（§9 备案）。

### 3.2 wolf 发布纪律（对公共语言包的发布义务）

1. **非破坏性内容更新**（词典增补、模型替换、资源纠错）：只抬 `version`，
   `compatVersion` 不动；
2. **破坏性更新**（触发上位规范《版本》兼容承诺清单所列公开表面变更：
   贡献增删、模块 ID、`interface`/`level`、exports 语义、options 写法、
   运行时 IO 等）：`compatVersion` 抬至破坏起点；
3. **资源格式版本抬升视同破坏性更新**：变体文档规定「解释器按版本一并
   校验」的资源格式（chain `formatVersion`、multig2p `bundle_version`）
   抬升时，`compatVersion` 必须同步抬升——数据格式不兼容旧插件=包级
   不兼容声明（联动条款见变体文档 §3 执行细则）；
4. **发布注记义务**：每个 release 的 manifest 携带逐包变更性质标注
   （`breaking` 布尔，§5），破坏性变更在 release notes 逐条列出受影响的
   公开表面。

### 3.3 编辑器更新义务（宿主侧）

1. **并存安装**：更新=向包根**添加**新版本目录，不删除、不覆盖既有版本；
   被破坏性更新淘汰、仍被在役声库依赖的旧版本必须保留（spec 2.4:402
   多版本共存）；同身份（id+规范化版本）重复目录属安装缺陷（:404 歧义
   即失败）；
2. **更新预检**（lint 级，非加载语义）：升级前按 `DataOnly` 扫描已装声库的
   依赖目标，计算升级后仍可命中的候选集，提示受影响声库清单（呈现粒度
   沿运行时 API 规范 §4.1：声库 × 语言）；
3. **失败呈现**：包在但 `Load` 失败（解释器期拒绝，如 `formatVersion` 超限）
   不得回退其他候选（:396），按缺依赖同粒度呈现，文案区分「升级编辑器」
   vs「安装兼容版本」（运行时 API 规范 §4.1 增补条款）。

## 4. chainG2P × 更新资源包：formatVersion 的端到端收敛

场景：pipe-chain provider 插件随宿主分发、公共语言包可独立更新——资源包
用到当前插件代码没有的 step。机制链三段：

1. **变体层（已有）**：chain.json `formatVersion` 公约——解释器声明支持
   上限 `[1, N]`，超上限拒绝加载、诊断携带「声明版本>支持上限」与升级
   指引（变体文档 §1.2 公约 2、§3 执行细则）；新增 step 类型必须抬
   `formatVersion`（§3 step 词汇段）；
2. **发布层（本轮新增，纪律 3）**：`formatVersion` 抬升 ⇒ 包级
   `compatVersion` 同步抬升——旧宿主中声库 pin 的目标版本不落在新包
   区间，新包在**依赖求解阶段**即被排除，呈现为缺依赖，收敛进运行时
   API 规范 §4.1 既有的「声库 × 语言」安装引导（装兼容旧版或升级宿主）；
3. **兜底（已有）**：发布纪律失守（虚报 compatVersion）时，依赖求解选中
   新包、解释器期 `formatVersion` 拒绝、:396 不回退 ⇒ 整链硬失败 + 诊断
   文案兜底——属包缺陷，靠纪律 4 的发布注记在发布面暴露。

静态预检：宿主不加载插件即可读 chain.json `formatVersion`（变体文档 §3
执行细则既有口径）；「插件经 metadata 上报自身 formatVersion 上限」列为
编辑器增强备案（§9），不进加载语义。

## 5. 发布物与 manifest 清单

- **tag**：`v<bundleVersion>`（资源集整体版本，四段式），一次 release =
  全部套件快照；套件自身版本仍在各自清单里独立演进；
- **资产**：每套件一个 zip（`wolf-lang-<iso>-<version>.zip`，D-P7）+
  `manifest.json`；过渡期 zip 内为现行旧格式目录（`package.json`/`modules/`
  …），DSPK 化后为 `.dspk` 单文件——`format` 字段区分
  `"g2ppackage-dir"` / `"dspk"`；
  - 夹具体积实测（2026-09-06，synthrt `resources/G2pPackages`）：总量
    32M——Multi 19M（含模型）、Rus 4.3M、Eng 3.3M（许可未核实，不入
    发布）、Kor 2.2M、其余 0.002~1M；全量缺省的下载与安装成本可接受，
    不再按体积拆档（D-P7）；
- **manifest.json**（示意）：

```json
{
    "bundleVersion": "1.0.0.0",
    "packages": [
        {
            "id": "wolf/lang-cmn",
            "file": "wolf-lang-cmn-1.2.0.zip",
            "sha512": "…",
            "version": "1.2.0.0",
            "compatVersion": "1.0.0.0",
            "format": "g2ppackage-dir",
            "breaking": false
        }
    ]
}
```

- **过渡期版本语义**：旧格式套件的 `version` 取自其 package.json（现状
  如 Cmn `1.0.1`、Jpn `0.0.1`）；`compatVersion` 过渡期恒等于 `version`
  ——旧栈无区间消费方（§1 第 2 条），DSPK 化起 §3 才生效；
- **Eng 排除机制化**：`Phonetic-Suite-Eng` 因再分发许可未核实
  （`resources/G2pPackages/README.md:3-8`）不入 manifest、不入资产——
  发行物边界从 README 一句话变为清单可机检；synthrt install 规则的对应
  修复见 §8 I1；
- 上位规范不提供来源认证（spec 2.4:371），SHA512 清单是完整性校验而非
  真实性证明——真实性由 release 渠道（仓库写权限）承担。

## 6. vcpkg port（仿 ffmpeg-builds；定居 vcpkg-overlay 仓）

- **端口名** `wolf-g2p-packages`；**定居点 = vcpkg-overlay 仓库**
  （`stdware/vcpkg-overlay`，D-P5）——lite 与 wolf 的 `.gitmodules` 共用
  同一子模块（实测：lite pin `f2aff64`、wolf pin `04b3297`），两仓
  `scripts/vcpkg-manifest/vcpkg.json` 的 `vcpkg-configuration.overlay-ports`
  已指向 `../vcpkg/ports`，port 合入该仓并各自 bump 子模块指针即对两仓
  同时生效，**消费仓零接线改动**；
- **模式复刻** `ffmpeg-builds`（同仓 `ports/ffmpeg-builds/`，portfile +
  assets.cmake + config 模板 + usage 四件套）：
  - `vcpkg.json` `version-string` = bundleVersion；纯数据端口、零源码编译，
    `supports` 全平台；
  - 资产清单（文件名+SHA512）由资源仓发布脚本生成 `assets.cmake`
    （D-P8，格式对齐 ffmpeg-builds 的 update-assets.ps1 产物）；portfile
    按 release tag URL `vcpkg_download_distfile` 下载校验后解包安装；
  - **features 与套件一一对应**（D-P7，16 套件实测清单）：
    `cmn`/`deu`/`eng`/`fil`/`fra`/`ita`/`jpn`/`kor`/`multi`/`num`/`por`/
    `punc`/`rus`/`spa`/`unknown`/`yue`；default-features 全量不含 `eng`
    （许可未核实）；
- **安装位置**：`${CURRENT_PACKAGES_DIR}/share/synthrt/G2pPackages/<套件>/`
  ——与 synthrt 现行 install 布局（`CMakeLists.txt:251-253`）一致，消费方
  目录结构零改动；
- **config 包**（port 附带，仿 ffmpeg-builds config 模板）：安装时生成
  `wolf-g2p-packages-config.cmake`，`find_package(wolf-g2p-packages CONFIG)`
  后提供
  - `WOLF_G2P_PACKAGES_DIR`——资源根目录
    （`<prefix>/share/synthrt/G2pPackages`）；
  - `WOLF_G2P_PACKAGES_SUITES`——本次实际安装的套件名清单（feature 决定）；
  - `WOLF_G2P_PACKAGE_<suite>_DIR`——逐套件目录变量。
    解决「纯数据目录不在 find_path 默认搜索面」问题，消费方不做裸
    `find_path`（vcpkg 工具链的 installed-dir 变量名跨版本有
    `VCPKG_INSTALLED_DIR`/`_VCPKG_INSTALLED_DIR` 之差，config 包内部
    吸收，ffmpeg-builds config 先例同款手法）；
- **消费矩阵**：

| 消费方 | 接线 | 消费面 |
| :-- | :-- | :-- |
| ds-editor-lite | 既有 overlay 子模块零改动（D-P5） | 部署脚本拷贝 `share/synthrt/G2pPackages` → 运行期 `pluginRootDir/srt-g2p/G2pPackages`（`InferEngine.cpp:219-220` 实测） |
| wolf | 既有 overlay 子模块零改动 + manifest feature `g2p-tests`（D-P6） | 构建测试自动配置（§6b） |
| synthrt（可选） | 仓内 `scripts/vcpkg-overlay/` 为树内目录、非该子模块；接线列为可选项 | 构建/测试经 `SYNTHRT_G2P_PACKAGES_SOURCE`（§8 I1），默认 git 夹具，port 仅 CI 便利 |

### 6.1 port 目录构成

```
ports/wolf-g2p-packages/
  - vcpkg.json                  // name/version-string=/<bundle 版本>；features 表；default-features
  - portfile.cmake              // 按 feature 选套件 → vcpkg_download_distfile(assets.cmake)
                                //   → 逐 zip 校验解包至 share/synthrt/G2pPackages/<套件>/
  - assets.cmake                // 生成物：每套件 FILE/SHA512/本地名 + bundle 版本（D-P8）
  - wolf-g2p-packages-config.cmake.in
  - usage                       // find_package 用法与变量说明
```

### 6.2 lite 消费接线（零改动确认）

- 子模块更新（bump 到含 port 的 overlay 提交）后，`vcpkg install` 即装得
  `share/synthrt/G2pPackages`；部署脚本拷贝至运行期位置
  `pluginRootDir/srt-g2p/G2pPackages`（lite `InferEngine.cpp:219-220`
  实测）——运行期目录结构与现行完全一致，无需改部署代码；
- `scripts/vcpkg-manifest/vcpkg.json` 的 dependencies 增
  `{"name": "wolf-g2p-packages", "features": [...]}`（默认全量缺 eng，
  可按发行策略裁剪）。

### 6.3 release → port 同步流程（D-P8，规范性）

1. 资源仓跑 `scripts/make-release.ps1`（或 CI）：逐套件压缩 → SHA512 →
   写 `manifest.json` + `assets.cmake` → 提交 → tag `v<bundleVersion>` →
   GitHub release 上传 zip + manifest.json；
2. 把生成的 `assets.cmake` 拷入 overlay 仓 `ports/wolf-g2p-packages/`，
   同步改 `vcpkg.json` `version-string` 为新 bundleVersion；提交并推送
   overlay 仓；
3. lite 与 wolf 各自 bump `scripts/vcpkg` 子模块指针（两个提交）；
   synthrt 的 `SYNTHRT_G2P_PACKAGES_SOURCE` 流程不经 port，无需动作；
4. 本地验证：两仓各跑一次干净 `vcpkg install`（或 upgrade），确认下载
   校验与安装布局。

### 6.4 synthrt 自身构建/测试接线（§8 I1 引用，形态不变）

新增 CMake 缓存变量 `SYNTHRT_G2P_PACKAGES_SOURCE`（默认 git 夹具目录），
`install(DIRECTORY)`、dsinfer-cli staging（`tools/dsinfer-cli/CMakeLists.txt:100-122`）
与 packaging 测试（`tests/packaging/CMakeLists.txt:44` 的
`G2P_PACKAGES_SOURCE_DIR`）统一改读该变量；CI 可选指向 port 安装树。

## 6b. wolf 构建测试自动配置（port 消费第二链，D-P6）

wolf 仓的编译与测试要求**开箱即得** G2p 包数据——不依赖手工放置目录或
网络脚本。接线四步：

1. **manifest 门控**（`wolf/scripts/vcpkg-manifest/vcpkg.json`）：`features`
   增 `g2p-tests`，其 dependencies 携 feature 选择：
   ```json
   "g2p-tests": {
       "description": "Fetch G2p language packages for linguist tests",
       "dependencies": [
           { "name": "wolf-g2p-packages", "features": ["cmn", "multi"] }
       ]
   }
   ```
   默认测试面取 `cmn`（语言组合主链）+ `multi`（19M，多语言与模型后端
   覆盖）；其余套件按测试需要增删 feature 清单即可，port 侧零改动。
   常规构建（不带 `--x-feature=g2p-tests`）不下载任何包数据；
2. **CMake 取用**：wolf 顶层（或 tests CMake）
   `find_package(wolf-g2p-packages CONFIG)` → `WOLF_G2P_PACKAGES_DIR` /
   `WOLF_G2P_PACKAGE_<suite>_DIR`（§6 config 包变量）；找不到（未启用
   feature）时变量为空，**不 fail 配置**；
3. **测试装配**：包相关测试（目标：真实 linguist 声明加载、
   `exports.phonemes` 形状校验、DataOnly 依赖求解等——wolf `docs/Status.md`
   既列的待补测试）按条件注册：
   - 有数据：test 属性 `ENVIRONMENT "WOLF_G2P_PACKAGES_DIR=${WOLF_G2P_PACKAGES_DIR}"`
     （或 compile definition），断言用相对变量不写死绝对路径；
   - 无数据：`add_test` 仍注册、测试体内检测目录缺失即 `SKIP` Return——
     保证「未装 feature 的常规 CI 绿灯不劣化」；
   - 逃生口：CMake 缓存变量 `WOLF_G2P_PACKAGES_SOURCE` 覆盖 config 包
     结果（指向本机 checkout / synthrt git 夹具），离线环境零下载可跑；
4. **边界声明**：port 只自动配置**包数据**；wolf 的语言 provider 插件
   （G2P/S2P/Onset interpreter）尚待实现（变体文档 §8.12 复核），真实包
   加载测试的另一半前提是 provider 落地——本节只解决数据侧就绪，两者
   就绪后测试即可写。声明解析类测试（DataOnly，不碰运行时）在 provider
   缺位时即可先行。

## 7. 编辑器插件管理器预留（契约面，不含 UI）

- 本节只钉与发布链路的对接契约，UI/交互属编辑器侧设计，不在本文范围；
- **更新检查**：读资源仓最新 release 的 `manifest.json`；下载资产后按
  `sha512` 校验；
- **安装**：解包至编辑器管理的包根（即「内置随发行」所用包根，运行期由
  宿主 `setPackagePaths` 注入，运行时 API 规范 §1 步骤 3 的宿主部署面）；
  更新=向该包根添加新版本目录（§3.3 并存安装）；用户自定义包根若提供，
  排序在编辑器包根之前（上位规范 :404/:406 路径优先语义下，后位路径的
  新版本在先位路径已有候选时不会被选中——内置与更新的落位必须同一包根
  或更新包根在前，此点列为编辑器实施核对项）；
- **公共语言包路径排序**沿 level-1《公共语言包》既有约定：公共语言包
  路径排在声库内置包路径之前；
- **目录布局**：包根内 id/version 子目录的组织以 main 框架目录 Package
  Loader 实测为准——**实施核对项**（本分支无 main 新框架代码，两线自分叉
  起互不可见，对照面见变体文档 §8.4）；
- 预检 lint（§3.3 义务 2）与安装引导（运行时 API 规范 §4.1）同源消费
  manifest 与 DataOnly 清单。

## 8. synthrt 接线与 git 目录渐进退役

**现状锚点**（问题陈述 §1 的构建侧事实）：16 套全量 checkin；install 全量
（未排除 Eng，与 README 漂移）；staging 排除 Eng；packaging 测试以源目录
GLOB 为准。

**分期**：

- **I1（发布链路建立，本轮设计后的首个实施轮）**：
  - 建资源仓（用户动作，仓库名见 D-P1）、落 `make-release.ps1`
    （D-P8）、首次 release（过渡期旧格式资产 + manifest）、
    `wolf-g2p-packages` port 合入 vcpkg-overlay 仓并推送（D-P5）；
  - lite：`scripts/vcpkg-manifest/vcpkg.json` 增依赖（§6.2）、bump
    overlay 子模块；
  - wolf：manifest 增 `g2p-tests` feature、CMake `find_package` 与测试
    条件装配（§6b 四步）、bump overlay 子模块；
  - synthrt 加 `SYNTHRT_G2P_PACKAGES_SOURCE` 变量并修复 install 的 Eng
    排除（`install(DIRECTORY … PATTERN "Phonetic-Suite-Eng" EXCLUDE)`，
    对齐 README 口径）；
  - git 目录 README 头部标注「旧格式夹具，发布链路就绪后退役」；
- **I2（DSPK 化迁移，另行立项）**：资产切换 `.dspk` 格式（结构重写按
  变体文档 §2 实现注记为每包补 exports 面）；§3 版本语义自本期生效；
  git 内 16 套旧格式目录退役为最小测试样例（或测试内生成），大文件全部
  出库、历史不再增重；
- **I3（编辑器插件管理器，下游立项）**：§7 契约面的消费实现。

**git 历史注记**：release 资产不经 git 对象库（GitHub release 存储），tag
只标记 bundle 版本——「不污染 git 历史」由渠道本身保证；I2 后 git 历史中
的大文件成为存量包袱但不增量，是否用 filter-repo 清理历史属破坏性操作，
默认不做、留用户决策。

## 9. 核对项与开放问题

**实施核对项**（实施轮逐项落定，本文不预设结论）：

- 包根内 id/version 目录布局——对照 main 框架目录 Package Loader；
- 编辑器包根与用户自定义包根的排序语义实测（:404/:406 遮蔽与最高版本
  交互）；
- port `version-string` 与 bundleVersion 的映射细节（vcpkg 版本比较文法）；
- lite 部署脚本从 vcpkg 安装树取资源的接线点。

**开放问题（备案，不阻塞 I1）**：

- 依赖方上限文法（声库自设兼容上限）——D-P2 既定取舍，重启判据：出现
  「资源包作者虚报 compatVersion 且声库侧需要防线」的真实案例；
- 插件 metadata 携带 `formatVersion` 支持上限——编辑器静态预检增强，
  不进加载语义；
- 旧栈声库 manifest `g2pPackageVersion`/`g2pPackages`（`PackageParser.cpp:115-148`）
  在 DSPK 化时的迁移映射——语义收敛为 `dependencies[].version` 目标点与
  声库私有包路径，逐字段对照表随 I2 起草；
- git 历史大文件的 filter-repo 清理（§8 注记）。

## 10. 当前快照与下一步

- **当前快照（2026-09-06 第二轮）**：D-P1~P8 八项决策齐备；port 定居
  vcpkg-overlay 共用子模块（lite/wolf 零接线改动）、wolf 测试自动配置
  形态（feature 门控 + config 包 + skip 兜底）与发布脚本三件套已钉；
  实施核对项新增 overlay 子模块 bump 与两仓干净安装验证；契约面增补
  已并入三份文档（level-1《公共语言包》、变体文档 §3、rt-api §4.1），
  契约台账 D26-D28 见变体文档 §8.11，本轮无契约面变更；
- **下一步**：I1 实施轮——建仓、发布脚本与首次 release、port 合入
  overlay、lite/wolf 消费接线与 synthrt 变量（每步独立提交可回滚，port
  需真实 tag 后方可联调）；
- **Next step (EN)**: I1 implementation round — create the resource repo,
  land `make-release.ps1`, first release, merge the `wolf-g2p-packages`
  port into the shared vcpkg-overlay submodule, wire lite/wolf consumers
  and the `SYNTHRT_G2P_PACKAGES_SOURCE` variable in synthrt.
