# NavCaster 目标架构 V2

生成时间：2026-06-12

本文档定义后续完整迭代的目标架构。它不是一次性重写方案，而是渐进式重构的方向约束。

## 目标

- 明确实时连接、业务规则、数据存储、HTTP 管理和前端展示的边界。
- 让 Redis schema 成为可维护、可迁移、可测试的基础设施，而不是散落在各模块的字符串约定。
- 让 proto 成为结构定义来源，但避免 HTTP/Core/Auth 各自解释字段。
- 保证每一阶段都能编译、运行、回退，不做不可控的大重写。

## 当前架构判断

现有目录按构建库划分：

- `src/base`：基础协议和工具。
- `src/core`：Caster 核心、Redis、集群、状态、权限、relay 调度。
- `src/auth`：NTRIP 账号鉴权和在线记录。
- `src/http`：REST API、SSE、静态文件、审计、监控。
- `src/service`：进程入口、配置、NTRIP listener、session 编排。

这套划分能工作，但有几个边界问题：

- `caster_internal` 职责过多，包含 Redis、Pub/Sub、主节点、relay、访问控制、source table、历史、统计。
- `http_handler.cpp` 职责过多，包含路由、鉴权、CRUD、配置、监控、审计、Redis sync client、SSE 数据源。
- Redis key 与 value schema 分散在 `core/auth/http` 中。
- 账号主表、鉴权索引、在线会话表语义不统一。
- 部分结构用 proto JSON，部分直接 JSON，部分兼容历史字符串。

## 目标分层

目标上把系统拆成六层：

```text
┌─────────────────────────────────────────────────────────────┐
│ app                                                         │
│ 进程入口、配置加载、生命周期、event_base/线程组装              │
├─────────────────────────────────────────────────────────────┤
│ transport                                                   │
│ ntrip listener/session I/O, http routes/controllers, SSE     │
├─────────────────────────────────────────────────────────────┤
│ services                                                    │
│ AuthService, CasterService, RelayScheduler, ClusterService   │
├─────────────────────────────────────────────────────────────┤
│ domain                                                      │
│ Account, Mountpoint, ClientSession, RelayTask, Node, Policy  │
├─────────────────────────────────────────────────────────────┤
│ storage                                                     │
│ Redis repositories, key registry, schema migration, pub/sub  │
├─────────────────────────────────────────────────────────────┤
│ infra/base                                                  │
│ libevent, hiredis, logging, metrics, RTCM/NMEA/base64/network│
└─────────────────────────────────────────────────────────────┘
```

## 建议目录

物理目录不必第一步搬迁。目标目录可作为中长期形态：

```text
src/
  app/
    main.cpp
    app_config.*
    app_runtime.*

  transport/
    ntrip/
      ntrip_listener.*
      sessions/
        server_session.*
        client_session.*
        near_session.*
        source_session.*
        relay_pull_session.*
        relay_push_session.*
    http/
      http_server.*
      controllers/
      sse/

  services/
    auth_service.*
    caster_service.*
    source_table_service.*
    access_policy_service.*
    relay_scheduler.*
    cluster_service.*
    config_service.*

  domain/
    account.*
    access_policy.*
    mountpoint.*
    connection_state.*
    relay_task.*
    node_state.*

  storage/
    redis/
      redis_client.*
      redis_keys.*
      redis_repository.*
      account_repository.*
      caster_state_repository.*
      source_repository.*
      relay_repository.*
      history_repository.*
      pubsub_bus.*
      schema_migration.*

  infra/
    event_loop.*
    logger.*
    system_metrics.*

  base/
    decode_rtcm.*
    decode_nmea.*
    ntrip_msg.*
```

短期可以先在现有目录中新增局部组件，例如 `src/core/storage/redis_keys.h`、`src/http/controllers/*`，等边界稳定后再整体移动。

## 模块职责

### App

负责：

- 加载 YAML 和命令行参数。
- 组装 Caster Core、Auth、HTTP、NTRIP listener。
- 管理 event_base、HTTP 线程、停止信号。
- 记录版本、启动信息、运行配置快照。

不负责：

- 不直接操作 Redis。
- 不直接实现业务规则。

### Transport/NTRIP

负责：

- TCP 接入。
- NTRIP 1.0/2.0 请求解析。
- Basic Auth 提取。
- `Ntrip-GGA`/body 中 GGA 提取。
- session 生命周期和 bufferevent I/O。

不负责：

- 不拼 Redis key。
- 不判断账号是否有效。
- 不计算全局 relay 调度。

### Transport/HTTP

负责：

- 路由与参数解析。
- HTTP status 和错误响应。
- 调用 service 层。
- SSE client 管理。
- 静态文件服务。

不负责：

- 不直接访问 Redis hash。
- 不包含业务校验细节。
- 不保存账号密码明文。

### Services

负责表达业务规则：

- `AuthService`：账号校验、密码验证、连接数限制、匿名登录策略。
- `CasterService`：基站/用户注册、订阅、发布、踢下线。
- `SourceTableService`：source record、自动解码 source、别名、nearest source table 生成。
- `AccessPolicyService`：访问组、挂载点访问、可见性、nearest 权限。
- `RelayScheduler`：pull/push 任务调度、主节点分配、状态重启。
- `ClusterService`：节点状态、主节点选举、节点历史。
- `ConfigService`：运行配置查询、更新、广播。

### Domain

负责纯模型和规则，尽量不依赖 Redis/libevent/hiredis：

- 字段含义。
- 默认值。
- 状态枚举。
- 基础校验。
- 兼容旧字段的转换策略。

### Storage/Redis

负责：

- Redis key 定义。
- TTL 和生命周期。
- schema version。
- proto JSON 编解码。
- 兼容迁移。
- Pub/Sub channel 封装。
- repository 方法。

不负责：

- 不直接处理 HTTP response。
- 不直接处理 bufferevent。

## 线程与事件模型

当前源码实际使用两个 event loop：

- NTRIP/Core/Auth 主 event_base。
- HTTP/SSE 独立 event_base，运行在 HTTP thread。

目标上保留这个方向，但明确约束：

- 跨线程共享状态必须通过 Redis、消息队列或明确的线程安全对象。
- NTRIP session 容器仍由主 event loop 管理。
- HTTP 不直接触碰主 event loop 内部容器。
- 后续如引入后台任务线程，必须先定义边界和所有权。

## Redis 与数据库边界

继续使用 Redis 的场景：

- 连接实时状态和 TTL。
- Pub/Sub 数据分发。
- 主节点选举。
- 集群节点心跳。
- relay 任务协调。
- 管理台低频配置，短期可继续保留。

未来可引入关系型数据库的场景：

- 用户账号主数据。
- 密码哈希和安全审计。
- 复杂查询、分页、模糊搜索。
- 多管理员和角色权限。
- 长期历史和账务类数据。

短期不引入新数据库。第一阶段先统一 Redis schema 和账号模型。

## Proto 使用原则

- proto 定义业务消息和 Redis value schema。
- Redis 存储默认使用 proto JSON，而非二进制 protobuf，以保持可读和 Web 友好。
- 所有 proto JSON 写入必须经过 repository 或 schema helper。
- 禁止 HTTP controller 和 core 直接随意 `body.dump()` 写入关键表。
- 新增字段优先只追加，不复用旧 tag。
- 每个持久配置类 value 增加 `schema_version` 或由 repository 外层维护版本。

## 目标依赖方向

允许：

```text
app -> transport -> services -> storage
app -> services
services -> domain
services -> storage
storage -> domain/proto
transport -> domain DTO
infra/base 被各层使用
```

避免：

```text
http -> hiredis direct
ntrip session -> Redis key direct
domain -> Redis/libevent
storage -> HTTP response
auth -> core internal container
core -> http handler
```

## 渐进式重构阶段

### Phase 0：设计与基线

- 生成 `architecture-v2.md`。
- 生成 `redis-schema-v2.md`。
- 生成 `refactor-plan-v2.md`。
- 生成并持续更新 `iteration-progress.md`。

### Phase 1：Redis schema 和账号模型

- 确认账号主表、登录索引、在线会话表。
- 统一 `ACT:RECORD`、`ACT:ACTIVE`、`ACT:REC:*`、`ACT:UND:*`、`STR:ACTIVE` 语义。
- 增加账号同步逻辑。
- 引入密码哈希兼容策略。
- 建立 Redis key registry。

### Phase 2：Repository 层

- 抽出 Redis key 常量和 repository。
- HTTP CRUD 走 repository。
- Core/Auth 逐步改用 repository 或明确的 storage adapter。

### Phase 3：拆 HTTP handler

- 拆 controllers：Auth、Accounts、Sources、Relays、Nodes、Stats、Config、Logs。
- SSE 数据源通过 service/repository 获取。
- 保持 API 行为兼容。

### Phase 4：拆 Caster Core

- 将 access policy、source table、relay scheduler、cluster node、history 从 `caster_internal` 中逐步抽出。
- 保持 `CASTER::*` 对外 API 一段时间作为兼容门面。

### Phase 5：测试与迁移工具

- 增加 Redis schema smoke test。
- 增加账号登录端到端测试。
- 增加迁移/修复脚本。
- 更新部署和文档。

## 架构验收标准

- 新增业务功能时，不需要在 `http_handler.cpp` 和 `caster_internal.cpp` 同时堆大量逻辑。
- Redis key 只在 storage 层或 key registry 中定义。
- 账号从 Web 创建后，NTRIP 鉴权行为明确且可测试。
- 运行时状态、持久配置、历史日志、账号数据生命周期清晰。
- 文档、proto、前端类型和后端写入逻辑一致。
