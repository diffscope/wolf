# Wolf 项目状态

## 当前定位

wolf 正在实现建立在 synthrt spec 2.4 之上的开放语言域。仓库负责语言 contribution 类别、Wolf Level 1 API、语言 Provider 插件以及歌手语言 Pipeline，不把具体语言能力写入 synthrt 或 dsinfer。

## 已完成

- 已建立 `linguist` contribution 类别以及 `LinguistSpec`、`LinguistProvider` 和 `LinguistProviderPlugin` 基础接口。
- `WolfLinguistProvider` 负责解释 Wolf Linguist Level 1 声明，并提供 import validator、Spec Extension 和 Executive Factory。
- G2P、S2P 与 Onset 使用独立的 inference contract，并分别通过`linguist/g2p`、`linguist/s2p`和`linguist/onset` role 进行绑定。
- Singer Spec 可获得 `WolfPipelineExtension`，并由它创建 `WolfPipelineExecutive`。
- `WolfPipelineExecutive` 可按 singer 本地 `role` 创建对应的 `LinguistExecutive`，创建结果由 Pipeline 维护父子生命周期。
- 运行时类型已经统一使用 `Executive` 和 `ExecutiveFactory` 命名，不再使用 Exec Instance 或 EI 概念。
- 当前库、插件和自动测试可在安装后的 synthrt 上完成构建，现有 2 个自动测试全部通过。

## 关于 Linguist Roles 的存储

当前 `WolfPipelineExtension` 与 `WolfPipelineExecutive` 都保存一份经过筛选的`linguist/*` roles。

- Extension 中的 roles 暂时不能删除。它们是 singer 全部 imports 的 Linguist 域投影，也是 `linguistRoles()` 和创建 Pipeline 时所需的稳定数据。
- Executive 中的 roles 从纯数据角度看可以重新从 Singer Spec 推导，因此并非绝对不可替代。但删除它需要让 Executive 回指 Extension，或者每次调用时重新遍历并判断 imports。前者增加生命周期耦合，后者重复 Provider 已经完成的筛选。
- roles 在 Package Load 后不再变化，数量通常很少。当前复制一份的成本可忽略，而且让 Executive 的可用角色集合在构造时固定。

因此现阶段保留两份 roles，不把删除它列为待办。将来若 synthrt 提供类型化且经过类别筛选的 import 视图，或者明确提供 Executive 到其 Extension 的安全引用，再合并存储更合适。

## 接下来

1. 按 spec 2.4 补齐语言声明解析、路径处理和错误语义测试。
2. 完善 G2P、S2P 与 Onset 的类型化输入输出协议，并固定 Level 1 可观察错误集合。
3. 增加包含多个 linguist role、可选 Onset 和未知开放 role 的 Singer 加载测试。
4. 用真实语言包验证 Provider discovery、Package Load 事务、Executive 创建与父子销毁流程。
