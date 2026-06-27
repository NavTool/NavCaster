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
| `v2-caster-runtime-worker-design.md` | Caster Runtime 多 Worker、Acceptor handoff、session/bufferevent/map 所有权设计。 |
| `v2-web-layout-reference.md` | v2 Web 控制面布局、Sub2API 只读参考边界和操作意图 UI 规范。 |
