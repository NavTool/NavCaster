# NavCaster 重构路线 V2

生成时间：2026-06-12

本文档是架构迭代的阶段计划。实际进展记录在 `doc/iteration-progress.md`。

## 总体策略

- 先设计，后改代码。
- 先收敛 schema，再拆模块。
- 先建立兼容层，再迁移调用方。
- 每个阶段都保持主分支可构建、可运行。
- 不在同一阶段同时做目录大搬迁和业务行为修改。

## Phase 0：设计基线

目标：建立后续协作共同记忆。

任务：

- [x] 生成 `doc/project-memory.md`
- [x] 生成 `doc/architecture-v2.md`
- [x] 生成 `doc/redis-schema-v2.md`
- [x] 生成 `doc/refactor-plan-v2.md`
- [x] 生成 `doc/iteration-progress.md`

验收：

- 后续迭代都能引用这些文档。
- 明确第一阶段优先处理 Redis schema 和账号模型。

## Phase 1：Redis Schema 与账号模型

目标：修复账号主表、鉴权索引、在线会话表语义不一致的问题。

任务：

- [ ] 增加 Redis key registry，例如 `src/*/redis_keys.*` 或 `src/storage/redis/redis_keys.*`
- [ ] 定义账号主表 `ACT:RECORD` 的写入规范。
- [ ] 定义 `ACT:ACTIVE` 作为登录索引的生成规则。
- [ ] 明确 `STR:ACTIVE` 的 legacy 状态，设计替代方案。
- [ ] HTTP 创建/更新/删除账号时同步 `ACT:ACTIVE`。
- [ ] Auth 鉴权读取路径兼容新旧字段。
- [ ] 引入密码哈希字段，兼容旧明文密码。
- [ ] 为账号 CRUD 和 NTRIP 鉴权增加 smoke test 或最小验证脚本。

风险：

- 改账号会影响 NTRIP server/client/source 登录。
- 当前默认匿名登录开启，实名账号路径可能被长期掩盖，测试时要关闭匿名登录验证。

建议顺序：

1. 只加 key registry 和 helper，不改行为。
2. 增加账号同步函数。
3. HTTP 写路径调用同步函数。
4. Auth 读取兼容 hash 字段。
5. 增加测试。

## Phase 2：Redis Repository 层

目标：让 Redis 读写从 HTTP/Core/Auth 中收敛出来。

任务：

- [ ] 抽 `AccountRepository`。
- [ ] 抽 `SourceRepository`。
- [ ] 抽 `RelayRepository`。
- [ ] 抽 `AccessPolicyRepository`。
- [ ] 抽 `RuntimeStateRepository`。
- [ ] 统一 proto JSON 序列化和校验。
- [ ] 逐步替换 `http_handler.cpp` 中直接 Redis 调用。

验收：

- 新增 Redis key 不直接写在 handler 或 session 中。
- HTTP CRUD 逻辑基本只做参数解析和 service 调用。

## Phase 3：拆 HTTP Handler

目标：降低 `src/http/http_handler.cpp` 复杂度。

任务：

- [ ] 拆 `AuthController`。
- [ ] 拆 `AccountsController`。
- [ ] 拆 `SourcesController`。
- [ ] 拆 `AccessController`。
- [ ] 拆 `RelayController`。
- [ ] 拆 `NodesController`。
- [ ] 拆 `StatsController`。
- [ ] 拆 `ConfigController`。
- [ ] 拆 `LogsController`。
- [ ] SSE channel 数据源改为调用 repository/service。

验收：

- `http_handler` 只注册路由和组合 controller。
- 单个 controller 文件职责清楚。
- API 行为保持兼容。

## Phase 4：拆 Caster Core

目标：降低 `caster_internal` 的职责密度。

任务：

- [ ] 抽 `AccessPolicyService`。
- [ ] 抽 `SourceTableService`。
- [ ] 抽 `ClusterService`。
- [ ] 抽 `RelayScheduler`。
- [ ] 抽 `HistoryService`。
- [ ] 抽 `RuntimeStateService`。
- [ ] 保留 `CASTER::*` 作为兼容 facade。

验收：

- `caster_internal` 不再直接承担所有业务。
- NTRIP session 调用的外部 API 保持稳定。

## Phase 5：目录迁移

目标：让物理目录匹配目标架构。

前置条件：

- Repository 和 service 边界稳定。
- HTTP handler 和 core 已拆小。
- 测试能覆盖主要路径。

任务：

- [ ] 新建 `src/app`、`src/transport`、`src/services`、`src/domain`、`src/storage`、`src/infra`。
- [ ] 分批移动文件。
- [ ] 更新 CMake。
- [ ] 更新 include path。
- [ ] 更新文档链接。

验收：

- 构建通过。
- Smoke test 通过。
- 文档路径更新。

## Phase 6：测试、迁移和可观测性

目标：让后续迭代更稳。

任务：

- [ ] Redis schema smoke test。
- [ ] 账号登录/禁用/过期/连接数限制测试。
- [ ] relay start/stop/update 测试。
- [ ] SSE channel 基础测试。
- [ ] Redis migration/repair 工具。
- [ ] 日志和审计字段脱敏检查。

## 暂不做

- 暂不整体替换 Redis。
- 暂不立即引入 PostgreSQL/SQLite。
- 暂不一次性搬完整目录。
- 暂不重写 NTRIP session 模型。

## 决策记录

- Redis 继续承担实时状态、TTL、Pub/Sub、主节点选举。
- proto 继续作为字段定义方向，但默认 Redis value 使用 proto JSON。
- 用户管理短期继续 Redis，先修 schema 和密码安全；未来如需要复杂查询和强审计，再引入关系型数据库。
- HTTP 和 NTRIP 层后续不直接拼 Redis key。
