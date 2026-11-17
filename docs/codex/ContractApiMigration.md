# 契约 API 迁移

契约头文件以 Contract 对象统一提供 interface 和 level。exports、import options 和 runtime options 只持有契约，构造时不需要 variant。configuration 持有完整三元组，由具体实现解释。契约类型去掉命名空间中已经表达的前缀。任务输入与结果继续使用契约的 interface 和 level 作为类型与版本。

公开变体配置与契约头文件平铺。wolf 的既有公开 LinguistConfiguration 迁移到 LinguistWolfApiL1.h 的 Wolf::Configuration，使用 Wolf::Triplet。其余 wolf 和 otter 实现的配置仍在插件内部。单一变体插件在内部头文件提供 Triplet，契约头文件不依赖具体变体。

S2p 的符号保持能力通过 Exports::preservesSymbols 提供。实现保证始终保留输入的发音单元时设为 true，未知或条件性行为保持 false。direct 实现声明 true，其他内置实现保持 false。该能力由解释器生成，不是新增的清单字段。wolflinguist 读取该能力，不按变体名称维护白名单。

ContribSpecPayload 只提供 contract() 多态接口。各类 payload 保存自己的身份，configuration 从其完整三元组返回契约引用，不重复保存契约字符串。configuration 校验三项，其余 payload 校验 interface 和 level。

2026-11-17 验证：使用 synthrt 的 build/contract-identity-install，构建退出码为 0。40 项 CTest 通过。S2pInterpreter 的 5 个用例、38 条断言通过。