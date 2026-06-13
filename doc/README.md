# NavCaster 文档入口

更新时间：2026-06-13

本轮复核基线：

```text
NC-005 基于 team-dev @ 8e725e1
```

## 阅读顺序

```text
project-memory.md
  当前项目地图、构建命令、启动链路和高风险注意点。

workflow.md
  当前代码端到端工作流程，优先作为运行模型和 HTTP/SSE 线程模型参考。

iteration-progress.md
  当前迭代历史流水和最近验证记录。完成态优先级高于旧计划文档 checkbox。

architecture-v2.md
  长期目标架构方向，不代表当前源码已经完全实现该分层。

redis-schema-v2.md
  Redis schema 和账号语义的当前目标文档。

redis-deployment.md
  Redis 最低版本、关键命令依赖、部署校验脚本和 Docker/CI 版本口径。

api-reference.md
  HTTP API 和 SSE 文档。改接口时必须和 src\http、web\src\api 交叉核对。

api-contract-sync.md
  Proto/API/Web 类型同步规则、契约检查命令、覆盖范围和当前允许差异。

http-deployment.md
  HTTP API 多节点入口、Force_Enable 默认策略、反向代理/failover 和 smoke 口径。
```

## 可信度分层

```text
当前事实
  源码、CMake、deploy 脚本、web/package.json、project-memory.md、workflow.md、
  iteration-progress.md，以及 shared\references\NC-001-current-architecture.md。

目标方向
  architecture-v2.md、redis-schema-v2.md、refactor-plan-v2.md、
  shared\references\NC-002-architecture-optimization-plan.md。

历史计划或流水
  development-plan.md、development-plan-v2.md、development-plan-v3.md、
  iteration-progress.md 中较早的工作记录。

过期或低可信参考
  data-flow-and-architecture.md 的单 event_base 描述、
  doc\session 下的源码快照、docs 下的早期架构/前端迁移草稿。
```

## 当前待确认决策

NC-002 后续迭代需要总控持续跟踪这些项目级决策：

```text
HTTP 多节点入口策略：已由 http-deployment.md 收口为默认 master-only + 反向代理；
  Force_Enable=true 仅用于固定管理节点或本地 smoke，不能作为普通多节点负载均衡池。
Redis 最低生产版本：已由 redis-deployment.md 收口为 Redis Open Source 8.4.0+；
  Docker/CI 打包验证版本保持 8.6.3，不兼容环境不提供降级路径。
STR:ACTIVE legacy 去留：是否有外部依赖，何时迁移到 ACT:SESSION:* 或新 session API。
Auth Redis 与 Caster Redis：是否长期保持双实例。
CI/QA 准入门槛：schema_smoke、Web lint/build、HTTP smoke 是否作为合并硬门槛。
长期存储：历史和账号主数据是否继续放 Redis，还是进入数据库评估。
```

## 维护规则

```text
修改 HTTP API 时，同步 api-reference.md 和 web\src\api\types.ts，或在任务记录中说明不变更原因。
修改 Redis key/schema 时，以 redis-schema-v2.md 和 src\core\context\redis_keys.* 为主线。
修改 proto 时，同步 proto\src 生成物、HTTP JSON、Web types 和相关文档，并运行契约检查。
修改部署或运行策略时，同步 deploy、cmake 配置模板和 QA smoke 说明。
不使用 refactor-plan-v2.md 的旧 checkbox 直接判断当前完成状态；以源码和 iteration-progress.md 为准。
```
