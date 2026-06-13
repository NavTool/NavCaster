# NavCaster 文档入口

更新时间：2026-06-13

基线：

```text
team-dev @ 03f15e3bfb6d731a6827915c8a1b7f4fd8f5fa91
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

api-reference.md
  HTTP API 和 SSE 文档。改接口时必须和 src\http、web\src\api 交叉核对。
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
HTTP 多节点入口策略：master-only + 反向代理，还是固定管理节点 Force_Enable=true。
Redis 最低生产版本：是否强制 Redis 8.6.3+，是否兼容无 HSETEX/HEXPIRE 环境。
STR:ACTIVE legacy 去留：是否有外部依赖，何时迁移到 ACT:SESSION:* 或新 session API。
Auth Redis 与 Caster Redis：是否长期保持双实例。
CI/QA 准入门槛：schema_smoke、Web lint/build、HTTP smoke 是否作为合并硬门槛。
长期存储：历史和账号主数据是否继续放 Redis，还是进入数据库评估。
```

## 维护规则

```text
修改 HTTP API 时，同步 api-reference.md 和 web\src\api\types.ts，或在任务记录中说明不变更原因。
修改 Redis key/schema 时，以 redis-schema-v2.md 和 src\core\context\redis_keys.* 为主线。
修改 proto 时，同步 proto\src 生成物、HTTP JSON、Web types 和相关文档。
修改部署或运行策略时，同步 deploy、cmake 配置模板和 QA smoke 说明。
不使用 refactor-plan-v2.md 的旧 checkbox 直接判断当前完成状态；以源码和 iteration-progress.md 为准。
```
