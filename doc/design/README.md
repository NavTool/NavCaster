# Design 文档说明

本目录保存已经进入产品化讨论的架构决策和设计方案。它不同于 `roadmap/`
中的长期方向，也不同于 `current/` 中的源码事实说明。

| 文件 | 内容 |
| --- | --- |
| `http-multi-entry-productization.md` | HTTP 多入口管理 API、token、SSE、Web 和部署策略的产品化决策。 |
| `account-billing-web-redesign.md` | 账户、接入账号、计费、供应商、站点历史和 Web 三角色重构设计。 |
| `v2-architecture.md` | v2 全新控制面 / 数据面总体架构、职责边界、命名规则和非兼容结论。 |
| `v2-control-plane-agent-runtime-contract.md` | AdminService、Agent、Runtime 的 desired / actual state、离线自治和执行语义契约。 |
| `v2-api-data-contract.md` | v2 HTTP/JSON API、PostgreSQL source of truth、Redis projection/cache/bus 数据契约。 |
| `v2-identity-access-account-contract.md` | v2 Web 用户账号 Account、Caster 接入账号 AccessAccount、PG name_locks、Web session、Redis auth projection、API、Web IA 和 QA gate 契约。 |
| `v2-caster-runtime-worker-design.md` | Caster Runtime 多 Worker、Acceptor handoff、session/bufferevent/map 所有权设计。 |
| `v2-web-layout-reference.md` | v2 Web 控制面布局、Sub2API 只读参考边界和操作意图 UI 规范。 |
| `v2-phase2-scope-and-contract-gate.md` | v2 Phase 2 真实控制面闭环范围、PG/Redis 边界、运行态事件/指标、集成顺序和非兼容 gate。 |
| `v2-phase3-production-contract-and-delivery-gate.md` | v2 Phase 3 生产化闭环加固范围、四类交付门槛、Admin/Agent/Caster/Web 集成边界、PG/Redis 一致性、控制指令生命周期、Runtime events/actual/metrics、Caster worker/pubsub 容量口径和 NC-101 到 NC-107 交付 gate。 |
| `v2-app-layout-contract.md` | v2 app layout 物理入口冻结契约：`app/admin`、`app/agent`、`app/caster`、`app/web` 是唯一默认生产态入口，`.archive/v1/src` 和 `.archive/v1/web` 只作历史参考，并定义 NC-109 到 NC-114 的依赖、合并顺序和审查口径。 |
