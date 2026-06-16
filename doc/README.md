# NavCaster 文档入口

更新时间：2026-06-16
治理任务：NC-035 文档目录统一与文档治理专项
复核基线：`team-dev @ 72c7e3a`，并纳入 NC-029 至 NC-034 的文档记忆提交。

`doc` 是仓库内唯一文档入口。仓库根目录不再维护并列 `docs` 目录；原 `docs`
中的协议、历史资料和草稿素材已经迁入 `doc/references` 或 `doc/archive`。

## 先读什么

```text
current/project-memory.md
  当前项目地图、构建命令、启动链路和高风险注意点。

current/workflow.md
  当前代码端到端工作流程，优先作为 NTRIP、Core、HTTP/SSE、Web 线程模型参考。

current/backend-core.md
  Core/Auth/Relay/Cluster/Redis key 的当前实现事实和源码追溯入口。

current/backend-http.md
  HTTP API、SSE、token、Redis adapter、入口策略的当前实现事实。

current/web-console.md
  Web 管理台技术栈、路由、API types、SSE/polling 的当前事实。

current/redis-schema.md
  Redis key、账号语义、运行态和历史监控数据模型。

api/api-reference.md
  HTTP API 和 SSE 参考。改接口时必须和 src/http、web/src/api 交叉核对。

api/api-contract-sync.md
  Proto/API/Web 类型同步规则、契约检查命令、覆盖范围和当前允许差异。

deployment/http-ingress.md
  HTTP ingress、Force_Enable、fixed/sticky 管理入口和 smoke 口径。

design/http-multi-entry-productization.md
  HTTP 多入口产品化设计决策、round-robin 限制、后续任务和验证矩阵。

deployment/redis.md
  Redis 最低版本、关键命令依赖、兼容检查和 Docker/CI 版本口径。

qa/qa-gates.md
  构建、contract check、schema_smoke、e2e smoke 和文档治理 gate。
```

## 可信度分层

```text
当前实现事实
  源码、CMakePresets、deploy 脚本、web/package.json、current/*、api/*、
  deployment/*、qa/*、近期任务卡/QA/Review 记录。

目标方向
  roadmap/architecture-v2.md、roadmap/refactor-plan-v2.md、
  shared/references/NC-002-architecture-optimization-plan.md。
  这些文档描述长期方向，不代表当前源码已经完全实现该分层。

产品化设计决策
  design/*。这些文档给出当前可承诺产品边界、未选方案理由和后续拆分任务；
  实现事实仍需回到源码、current/*、deployment/* 和 qa/* 交叉核对。

历史计划和迭代流水
  roadmap/development-plan*.md、roadmap/iteration-progress.md。
  旧 checkbox 不能直接作为完成态判断，完成状态以源码和近期任务记录为准。

协议和历史参考
  references/protocols、references/historical、references/proto。
  协议资料可用于对接；历史资料不能直接当当前实现事实。

过期草稿和代码快照
  archive/historical、archive/code-snapshots。
  这些资料只保留追溯价值，不参与当前构建，不能直接复制进实现。
```

## 目录职责

| 目录 | 职责 | 注意事项 |
| --- | --- | --- |
| `current/` | 当前实现事实和岗位模块说明 | 必须能追溯到源码或近期任务记录。 |
| `api/` | HTTP API、SSE、Proto/API/Web 契约同步 | 接口变化必须同步 Web types 和契约检查。 |
| `design/` | 产品化设计决策和方案取舍 | 不等于源码已实现，必须写明当前承诺边界。 |
| `deployment/` | 构建、Redis、HTTP ingress、部署 smoke | 当前 C++ 默认使用 CMake + Ninja + 全处理器并行。 |
| `qa/` | QA gate、smoke/e2e 矩阵、验证命令 | 文档任务不新增运行态测试矩阵。 |
| `roadmap/` | 目标架构、未来方向、旧开发计划和迭代流水 | 不用旧计划 checkbox 判断当前完成状态。 |
| `references/protocols/` | NTRIP、PROXY 等外部协议资料 | 协议原文不等于当前实现覆盖范围。 |
| `references/historical/` | 早期需求、旧 Redis/Web 资料 | 只作历史参考。 |
| `references/proto/` | proto 生成历史备忘 | 当前 CMake 不自动生成 proto。 |
| `archive/` | 过期说明、旧架构、草稿代码、源码快照 | 非当前事实，不参与构建。 |

## 关键入口

| 主题 | 当前应读 |
| --- | --- |
| 构建默认口径 | `deployment/redis.md`、`qa/qa-gates.md`、根目录 `CMakePresets.json`、`deploy/scripts/build_ninja.*` |
| 部署和 HTTP ingress | `deployment/http-ingress.md` |
| HTTP 多入口产品化决策 | `design/http-multi-entry-productization.md` |
| Redis 版本与 key/schema | `deployment/redis.md`、`current/redis-schema.md`、`current/backend-core.md` |
| HTTP API 和 SSE | `api/api-reference.md`、`current/backend-http.md` |
| Relay / Cluster / Master lease | `current/backend-core.md`、`current/project-memory.md` |
| Web 管理台 | `current/web-console.md`、`api/api-contract-sync.md` |
| QA gate 和 e2e 矩阵 | `qa/qa-gates.md`、`current/qa-and-verification.md` |
| 目标架构方向 | `roadmap/architecture-v2.md`、`roadmap/refactor-plan-v2.md` |
| NTRIP/PROXY 协议 | `references/protocols/README.md` |
| 旧需求、旧 Redis、旧 Web 迁移记录 | `references/historical/README.md` |
| 草稿代码和源码快照 | `archive/README.md` |

## 当前待确认决策

```text
HTTP 多节点入口策略
  当前已收口为固定管理入口或稳定 sticky session 管理入口。
  普通 round-robin 无状态写入口池不属于当前支持能力。
  产品化取舍和后续任务见 design/http-multi-entry-productization.md。

STR:ACTIVE legacy 去留
  当前 /api/accounts/active 仍兼容 STR:ACTIVE fallback。
  后续是否完全迁移到 ACT:SESSION:* 需单独任务确认。

Auth Redis 与 Caster Redis
  当前仍可分开配置；是否长期保持双实例需后续架构决策。

长期存储
  Redis 当前继续承担配置、运行态和部分历史监控。
  长期账号主数据和历史数据是否迁出 Redis 尚未决策。

Web lint 基线
  Web build 近期通过；lint 仍有既有债务，不能假定为当前硬门槛。
```

## 维护规则

```text
修改 HTTP API 时，同步 api/api-reference.md、web/src/api/types.ts 和契约检查。
修改 Redis key/schema 时，同步 current/redis-schema.md、current/backend-core.md 和 src/core/context/redis_keys.*。
修改 proto 时，同步 proto/src 生成物、HTTP JSON、Web types、api/api-contract-sync.md，并运行 contract check。
修改部署或运行策略时，同步 deployment/*、qa/qa-gates.md、deploy 脚本说明和相关任务记录。
修改 Web 路由、API types、SSE/polling 行为时，同步 current/web-console.md。
历史、协议、草稿资料必须留在 references 或 archive，不得冒充 current。
```
