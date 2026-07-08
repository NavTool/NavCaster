# NavCaster v2 Architecture Contract

更新时间：2026-06-27
任务：NC-081 v2 Architecture API Data Contract
状态：v2 冻结契约
适用范围：NavCaster v2 新工程骨架、控制面、数据面、存储边界和命名规则。
可信度：架构冻结文档；后续实现任务 NC-082 至 NC-087 应按本文和同目录 v2 设计文档执行。

依据：

```text
F:\Projects\NavCaster\_team\tasks\active\NC-081-v2-architecture-api-data-contract.md
F:\Projects\NavCaster\shared\references\2026-06-26-control-plane-data-plane-architecture-plan.md
F:\Projects\NavCaster\shared\references\2026-06-27-large-refactor-implementation-blueprint.md
F:\Projects\NavCaster\shared\references\sub2api-structure-design-notes-2026-06-26.md
```

相关确认岗位：

```text
architect
docs-planner
backend-http
backend-core
frontend
qa
reviewer
```

## 1. 冻结结论

NavCaster v2 是全新架构契约，不是旧程序的兼容升级。

冻结结论：

```text
v2 不兼容旧架构。
v2 不兼容旧 HTTP API。
v2 不兼容旧 Redis key。
v2 不兼容旧 protobuf。
v2 不沿用旧 C++/HTTP/Web 命名规则。
v2 不沿用旧 NavCaster Web 风格。
v2 不要求旧程序平滑升级到新程序。
历史代码只作为业务行为、NTRIP 协议经验、Redis 运行态经验和性能经验参考。
```

v2 目标架构：

```text
navcaster-admin   Go   控制面后端
navcaster-agent   Go   本机运行时管理
navcaster-caster  C++  实时数据面
web               TS   管理前端
PostgreSQL             长期权威数据库
Redis                  运行态投影、缓存和消息总线
```

核心边界：

```text
AdminService 管全局意图和权威业务写入。
Agent 管本机资源和本机 Runtime 生命周期。
Caster Runtime 管 NTRIP 实时入口、Worker 和数据面。
Caster Worker 管单 worker 内 session、bufferevent、map 和本地 fan-out。
PostgreSQL 是 source of truth。
Redis 是 projection / cache / bus，不是长期权威数据库。
Web 只表达管理意图，不直接操作机器、Redis 或 Caster 内部容器。
```

## 2. 总体架构

```text
┌──────────────────────────────────────────────────────────────┐
│ Web                                                          │
│ 管理前端：Host、Runtime、Worker、账号、配置、计费、审计         │
└───────────────────────────────┬──────────────────────────────┘
                                │ HTTP/JSON + SSE
┌───────────────────────────────▼──────────────────────────────┐
│ navcaster-admin                                               │
│ 控制面：API、权限、PG、Redis 投影、desired state、审计          │
└───────────────┬───────────────────────────────┬──────────────┘
                │                               │
                │ SQL                           │ Redis commands
┌───────────────▼──────────────┐    ┌──────────▼───────────────┐
│ PostgreSQL                    │    │ Redis                    │
│ 权威业务数据、配置版本、审计     │    │ 运行态投影、TTL、Pub/Sub   │
└──────────────────────────────┘    └──────────┬───────────────┘
                                                │
                       desired projection / actual state / bus
                                                │
┌───────────────────────────────────────────────▼──────────────┐
│ navcaster-agent                                               │
│ 每台机器一个：注册、心跳、拉取 desired、守护本机 Runtime         │
└───────────────────────────────────────────────┬──────────────┘
                                                │ local process
┌───────────────────────────────────────────────▼──────────────┐
│ navcaster-caster                                              │
│ C++ Runtime：NTRIP acceptor、WorkerManager、health、metrics    │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │
│  │ Worker 0      │  │ Worker 1      │  │ Worker N      │        │
│  │ event_base    │  │ event_base    │  │ event_base    │        │
│  │ mount shard   │  │ mount shard   │  │ mount shard   │        │
│  └──────────────┘  └──────────────┘  └──────────────┘        │
└──────────────────────────────────────────────────────────────┘
```

第一版通信协议：

| 方向 | 协议 | 说明 |
| --- | --- | --- |
| Web -> AdminService | HTTP/JSON + SSE | Web API、管理操作、状态流。 |
| Agent -> AdminService | HTTP/JSON polling 或 long polling | 注册、心跳、desired state、actual state、事件和指标。 |
| Agent -> Caster Runtime | 本机进程管理 + local HTTP/JSON | 启停、健康检查、指标、可选 reload。 |
| Caster Runtime -> Redis | hiredis async / Redis protocol | 运行态 TTL、投影读取、Pub/Sub。 |
| Caster Runtime -> PostgreSQL | 禁止 | Caster 热路径不访问 PG。 |

第一版不引入 gRPC。只有当 Agent 数量、双向控制复杂度或强类型 SDK 需求显著增加时，才重新评估。

## 3. 程序职责边界

### 3.1 AdminService

二进制名称：

```text
navcaster-admin
```

职责：

```text
提供 v2 HTTP/JSON Web API。
管理 Web 登录、权限、角色和审计。
读写 PostgreSQL 权威业务数据。
生成 Redis 运行态投影和鉴权投影。
管理 Agent 注册、认证和心跳。
维护 Host registry。
维护 Runtime desired state。
维护 ConfigVersion、ConfigRelease、回滚记录和 checksum。
聚合 Agent / Runtime actual state。
保存操作审计和关键控制面事件。
提供人工确认式扩容和调度意图。
```

禁止：

```text
不直接 SSH 到机器。
不直接远程执行命令。
不直接启动、停止远端 Caster 进程。
不承载 NTRIP 数据流。
不参与 Caster 热路径。
不把 Redis 当长期权威数据库。
```

### 3.2 Agent

二进制名称：

```text
navcaster-agent
```

职责：

```text
每台机器一个 Agent。
注册 host 和 agent identity。
上报 heartbeat、CPU、内存、磁盘、网络和本机 runtime 状态。
拉取 desired state。
缓存 last-known desired state。
渲染 Caster Runtime 本地配置。
启动、停止、重启、守护本机 navcaster-caster。
AdminService 离线时继续维持本机 Runtime。
AdminService 恢复后重新 reconcile desired / actual。
```

禁止：

```text
不做全局调度。
不管理其他机器。
不直接修改 PostgreSQL 业务表。
不承载 NTRIP 数据面。
不绕过 AdminService 写全局业务状态。
```

### 3.3 Caster Runtime

二进制名称：

```text
navcaster-caster
```

职责：

```text
监听 NTRIP 入口。
解析 NTRIP 请求头和 Basic Auth。
按 mount ownership 选择 Worker。
管理 Worker 生命周期和 worker_count。
提供本机 health / metrics。
执行 source / client / near / relay 实时接入和分发。
维护本地 auth/config cache。
读取 Redis 投影。
写 Redis 运行态 TTL 和 Pub/Sub 数据。
聚合 Runtime / Worker metrics。
```

禁止：

```text
不提供完整 Web 管理后台。
不直接访问 PostgreSQL。
不做长期业务查询。
不保存长期权威配置。
不执行全局扩容调度。
```

### 3.4 Web

职责：

```text
提供 v2 管理前端。
展示 Host、Runtime、Worker、Mount、Session、Usage、Billing、Audit。
发起创建 Runtime、启动、停止、重启、drain、配置发布和回滚等操作意图。
展示 desired / actual state 差异和进度。
按角色组织 admin / customer / supplier 能力区。
```

禁止：

```text
不直接启动进程。
不直接写本机配置文件。
不直接访问 PostgreSQL。
不直接写 Redis。
不直接连接 Caster session 容器。
不复用旧 NavCaster Web 风格。
```

### 3.5 PostgreSQL

职责：

```text
长期权威业务数据。
账号、权限、密码材料和状态。
AccessAccount、MountPoint、StationRecord、Supplier、Usage、Billing。
Host、Agent、Runtime registry。
Runtime desired state。
ConfigVersion、ConfigRelease、发布和回滚记录。
操作审计和登录审计。
长期连接历史和聚合统计。
```

原则：

```text
PostgreSQL = source of truth
```

### 3.6 Redis

职责：

```text
运行态 projection。
短生命周期 cache。
TTL 在线状态。
Agent / Runtime 临时心跳。
NTRIP 鉴权快速索引。
Runtime actual snapshot。
Worker metrics snapshot。
Pub/Sub 或后续 Sharded Pub/Sub 数据总线。
控制面变更通知。
```

原则：

```text
Redis = projection / cache / bus
Redis 不是长期权威数据库。
Redis 中可由 PG 或 Runtime 重建的数据不得成为唯一事实。
```

## 4. 存储边界

数据写入原则：

```text
Web 写操作 -> AdminService
AdminService 权限校验和审计 -> PostgreSQL
AdminService / projection worker -> Redis 投影
Agent 拉取 desired state -> 本机配置和进程动作
Caster Runtime 读取 Redis 投影 / 本地 cache
Caster Runtime 写 Redis runtime state
AdminService 聚合 Redis actual state -> Web 展示
长期历史由 AdminService 或后台任务写 PostgreSQL
```

禁止出现：

```text
Caster 热路径查询 PostgreSQL。
Agent 直接修改业务表。
Web 直接写 Redis 运行态。
Redis 数据成为长期唯一事实。
AdminService 直接远程执行命令。
多个 Worker 直接共享 session map 或 bufferevent。
```

## 5. Desired / Actual State

v2 控制链路以 desired state 和 actual state 分离为基本契约。

```text
desired state
  管理员想要的目标状态。
  由 AdminService 写入 PostgreSQL。
  由 Agent 拉取、缓存、执行和回报。

actual state
  Agent / Runtime 实际观测到的状态。
  由 Agent 上报给 AdminService。
  Caster Runtime 同时写 Redis runtime projection。
  AdminService 聚合后供 Web 展示和审计。
```

关键规则：

```text
动作类 API 不直接表示远程命令已完成，只表示 intent accepted。
Web 必须展示 pending / applying / running / failed 等状态。
Agent 离线时 AdminService 不假设远端动作已执行。
AdminService 离线时 Agent 使用 last-known desired state 继续守护本机 Runtime。
AdminService 恢复后以版本号和 actual snapshot 重新 reconcile。
```

## 6. Caster 热路径规则

Caster 数据面热路径包括：

```text
accept socket
读取 NTRIP header
auth 快速判断
source / client / relay session I/O
RTCM/NMEA 解析采样
本地 fan-out
Redis publish / subscribe
在线状态 TTL 刷新
```

热路径规则：

```text
不得访问 PostgreSQL。
不得同步调用 AdminService。
不得依赖 Web 或 AdminService 存活。
鉴权和配置通过 Redis 投影或本地 cache 完成。
计费、审计和长期事实通过异步事件、Redis runtime state 或后台汇聚进入 PG。
```

## 7. Caster Runtime / Worker 所有权

Runtime 内部规则：

```text
RuntimeManager 管进程生命周期、配置、health 和 metrics 聚合。
Acceptor 只负责 accept、读 header、解析 ConnectInfo、选择 Worker 和 handoff。
WorkerManager 管 worker_count、mount ownership、draining 和 worker 生命周期。
CasterWorker 独占 event_base、session、bufferevent、mount map 和 Redis async context。
```

Worker 独占资源：

```text
session objects
bufferevent pointers
mount subscription map
client status map shard
server/source status map shard
stream counters shard
RTCM decoder shard
Redis async command context
Redis async subscribe context
```

跨 Worker 原则：

```text
跨 Worker 通信只使用 message queue、event_active、pipe、eventfd 或等价 mailbox。
不得直接修改其他 Worker 的 map。
不得跨 event_base 迁移正式 bufferevent。
不得共享同一个 hiredis async context。
```

## 8. 命名规则

v2 命名从新契约开始，不受旧 `CasterService`、旧 Redis key、旧 HTTP path 或旧 protobuf 约束。

### 8.1 二进制

```text
navcaster-admin
navcaster-agent
navcaster-caster
```

Windows 可发布 `.exe`，逻辑名称保持一致。

### 8.2 Go package

```text
app/admin/internal/api
app/admin/internal/auth
app/admin/internal/control
app/admin/internal/agent
app/admin/internal/runtime
app/admin/internal/storage/postgres
app/admin/internal/storage/redis
app/admin/internal/projection
app/admin/internal/audit

app/agent/internal/client
app/agent/internal/host
app/agent/internal/runtime
app/agent/internal/supervisor
app/agent/internal/state
app/agent/internal/service
```

### 8.3 C++ namespace

```text
navcaster::caster::runtime
navcaster::caster::transport
navcaster::caster::worker
navcaster::caster::session
navcaster::caster::domain
navcaster::caster::storage::redis
navcaster::caster::infra
```

### 8.4 HTTP route

```text
/api/v1/auth/*
/api/v1/me/*
/api/v1/supplier/*
/api/v1/admin/*
/api/v1/control/*
/api/v1/agents/*
/api/v1/runtime-local/*
```

`/api/v1/runtime-local/*` 只用于 Agent 到本机 Caster Runtime，不面向 Web 公网入口。

### 8.5 PostgreSQL table

PostgreSQL 表使用 lower_snake_case 和领域前缀：

```text
accounts
access_accounts
mount_points
mount_point_groups
hosts
agents
runtimes
runtime_desired_states
runtime_actual_snapshots
config_versions
config_releases
operation_audit_logs
usage_facts
```

### 8.6 Redis key

当前 Redis key/channel 不使用 `v2:` 前缀，也不复用旧 key 作为兼容义务：

```text
auth:access-account:<username>
config:runtime:<runtime_id>
config:version
runtime:actual:<runtime_id>
runtime:worker-stat:<runtime_id>
session:access-account:<access_account_id>
stream:mount:<mount>
control:config
agent:heartbeat:<agent_id>
sourcetable:runtime:<runtime_id>
sourcetable:index
sourcetable:changed
```

旧 `ACT:*`、`MPT:*`、`STR:*`、`PULL:*`、`PUSH:*`、`CASTER:*` 不作为 v2 兼容契约。

### 8.7 配置

配置字段使用 lower_snake_case：

```text
admin_listen_addr
postgres_dsn
redis_addr
agent_id
runtime_id
listen_port
worker_count
restart_policy
config_version
```

不沿用旧配置拼写或旧 YAML 层级作为 v2 约束。

## 9. 目标工程布局

建议布局：

```text
repo/
  app/
    admin/
      go.mod
      cmd/navcaster-admin/main.go
      internal/
      migrations/

    agent/
      go.mod
      cmd/navcaster-agent/main.go
      internal/

  caster/
    CMakeLists.txt
    app/
    runtime/
    transport/
    worker/
    session/
    domain/
    storage/redis/
    infra/

  web/
    src/

  api/
    openapi/
    idl/

  deploy/
    scripts/
    systemd/
    windows-service/
    docker/
```

说明：

```text
旧 src/、proto/、web/ 只作参考，不作为 v2 依赖边界。
api/idl 可选；若引入 protobuf，必须从 v2 package、message、字段编号重新设计。
第一版 Admin/Agent/Web 通信仍以 HTTP/JSON 为准。
```

## 10. 故障模型

### 10.1 AdminService 离线

影响：

```text
Web 管理页面不可用或只显示过期状态。
不能创建新的 Runtime、发布配置或下发新操作意图。
控制面状态聚合延迟。
```

不应影响：

```text
已有 source 连接。
已有 client 连接。
Caster 本地 fan-out 和 Redis Pub/Sub。
Agent 对本机 Runtime 的守护。
Caster Runtime 崩溃后的本机自动拉起。
```

### 10.2 Agent 离线

影响：

```text
Host 标记 agent_offline。
Web 禁止或 pending 该 Host 的新动作。
AdminService 不能确认该 Host 上的进程动作。
```

不应自动发生：

```text
AdminService 不直接杀死该机器上的 Runtime。
已有 Caster Runtime 不因 Agent 心跳超时被远程关闭。
```

### 10.3 Caster Runtime 崩溃

恢复规则：

```text
Agent 按 restart_policy 决定是否拉起。
重启后 Runtime 读取本地配置和 Redis 投影。
Redis TTL 状态自然过期或由启动补偿清理。
NTRIP 客户端通过重连恢复。
```

## 11. 阶段边界

后续任务可并行，但必须遵守本文契约：

| 阶段 | 主要交付 |
| --- | --- |
| NC-082 | 新工程骨架、目录、构建和二进制命名。 |
| NC-083 | PostgreSQL schema、Redis key registry、投影流程。 |
| NC-084 | AdminService HTTP/JSON API、desired state、Agent contract。 |
| NC-085 | Agent MVP、本地状态、进程管理、离线自治。 |
| NC-086 | Caster Runtime / Worker MVP、handoff、mount ownership。 |
| NC-087 | Web 控制面骨架和 Sub2API 风格参考实现。 |

## 12. 待实现验证项

本文是设计契约，不运行产品构建。后续实现必须建立以下验证：

```text
AdminService + PostgreSQL + Redis integration。
Agent register / heartbeat / desired polling。
AdminService 离线时 Agent 维持 Runtime。
Agent 离线时 AdminService 禁止误判动作完成。
Caster Runtime 启动、health、metrics。
Acceptor handoff fd / initial buffer 生命周期。
Worker 独占 session / bufferevent / map 检查。
Caster 热路径不访问 PostgreSQL 的代码审查和测试。
Web intent accepted -> pending -> actual state 展示流。
Redis projection 可从 PostgreSQL 重建。
```

## 13. 残余风险

```text
从零实现 v2 会牺牲旧程序兼容性，需要 QA 以新契约重新建矩阵。
PG -> Redis 投影存在一致性和补偿复杂度，需要 outbox / version / checksum 设计。
C++ 多 worker 若破坏所有权规则会产生难复现竞态，Reviewer 必须专项检查。
AdminService 第一版单节点可接受，但未来高可用需要幂等操作和 leader lock。
Redis Pub/Sub 仍可能成为多节点数据面瓶颈，需先完成指标观测再评估 Sharded Pub/Sub。
```
