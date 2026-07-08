# NavCaster v2 API and Data Contract

更新时间：2026-06-27
任务：NC-081 v2 Architecture API Data Contract
状态：v2 冻结契约
适用范围：HTTP/JSON API、PostgreSQL 权威表域、Redis 投影/cache/bus、命名边界和数据同步。
可信度：架构冻结文档；后续 schema、AdminService、Agent、Web 和 Caster 实现任务必须按本文展开。

## 1. 契约结论

v2 数据契约冻结为：

```text
HTTP/JSON 是第一版 Web/Admin/Agent API 契约。
PostgreSQL 是 source of truth。
Redis 是 projection / cache / bus。
Caster 热路径不访问 PostgreSQL。
旧 HTTP API、旧 Redis key、旧 protobuf 不作为 v2 兼容输入。
```

禁止：

```text
把 Redis 中的配置、账号或运行态当成长期唯一事实。
让 Web 直接写 PostgreSQL 或 Redis。
让 Agent 直接修改业务表。
让 Caster 在连接热路径查询 PG 或同步调用 AdminService。
把旧 proto message 当作 v2 API schema。
```

## 2. API 命名空间

统一前缀：

```text
/api/v1
```

命名空间：

| Namespace | 调用方 | 用途 |
| --- | --- | --- |
| `/auth` | Web | Web 登录、登出、会话。 |
| `/me` | customer/user | 当前登录用户自助能力。 |
| `/supplier` | supplier | 供应商自助能力。 |
| `/admin` | admin | 全局管理能力。 |
| `/control` | admin / operator | Host、Runtime、Worker、Config、Audit 控制面。 |
| `/agents` | Agent | Agent 注册、心跳、desired polling、事件和指标。 |
| `/events` | Web | SSE 控制面事件流。 |
| `/runtime-local` | Agent -> local Caster | 本机 Runtime health、metrics、reload、drain。 |

`/runtime-local` 不应暴露给公网 Web 入口。

## 3. 通用 HTTP 规范

### 3.1 Envelope

成功响应：

```json
{
  "request_id": "req_01HXYZ",
  "data": {},
  "meta": {}
}
```

错误响应：

```json
{
  "request_id": "req_01HXYZ",
  "error": {
    "code": "version_conflict",
    "message": "desired state version conflict",
    "details": {}
  }
}
```

分页响应：

```json
{
  "request_id": "req_01HXYZ",
  "data": [],
  "page": {
    "limit": 50,
    "offset": 0,
    "total": 120
  }
}
```

### 3.2 时间、ID 和枚举

```text
时间使用 RFC3339 UTC 字符串。
ID 使用带领域前缀的稳定字符串，例如 acc_、aacc_、host_、ag_、rt_、cfg_。
JSON 字段使用 snake_case。
枚举使用 lower_snake_case 字符串。
金额使用整数分或 decimal 字符串，不使用二进制浮点作为持久契约。
```

### 3.3 HTTP 状态码

| HTTP | 语义 |
| --- | --- |
| 200 | 查询或同步动作成功。 |
| 201 | 资源创建成功。 |
| 202 | 意图已接受，异步执行中。 |
| 400 | 参数格式错误。 |
| 401 | 未认证。 |
| 403 | 权限不足。 |
| 404 | 资源不存在。 |
| 409 | 版本、状态、唯一键或幂等冲突。 |
| 422 | 业务规则不满足。 |
| 429 | 限流。 |
| 503 | 依赖不可用。 |

## 4. Core HTTP API

### 4.1 Health

```text
GET /api/v1/health
```

响应：

```json
{
  "request_id": "req_01HXYZ",
  "data": {
    "service": "navcaster-admin",
    "status": "ok",
    "version": "v2.0.0",
    "postgres": "ok",
    "redis": "ok",
    "time": "2026-06-27T10:00:00Z"
  }
}
```

### 4.2 Auth

```text
POST /api/v1/auth/login
POST /api/v1/auth/logout
GET  /api/v1/auth/session
```

登录响应：

```json
{
  "request_id": "req_01HXYZ",
  "data": {
    "token": "opaque_token",
    "account_id": "acc_01HXYZ",
    "username": "alice",
    "role": "admin",
    "display_name": "Alice",
    "expires_at": "2026-06-28T10:00:00Z"
  }
}
```

第一版 token 可以是 AdminService 管理的 opaque token。是否放入 Redis token store 由后续实现任务决定，但不能复用旧 HTTP token 语义作为 v2 兼容承诺。

### 4.3 Agent API

```text
POST /api/v1/agents/register
POST /api/v1/agents/heartbeat
GET  /api/v1/agents/{agent_id}/desired-state?since_version=42
POST /api/v1/agents/{agent_id}/runtime-events
POST /api/v1/agents/{agent_id}/runtime-metrics
POST /api/v1/agents/{agent_id}/actual-state
```

Register request：

```json
{
  "bootstrap_token": "boot_xxx",
  "hostname": "caster-node-01",
  "machine_id": "machine_xxx",
  "os": "windows",
  "arch": "amd64",
  "agent_version": "v2.0.0"
}
```

Register response：

```json
{
  "request_id": "req_01HXYZ",
  "data": {
    "agent_id": "ag_01HXYZ",
    "host_id": "host_01HXYZ",
    "agent_secret": "secret_xxx",
    "heartbeat_interval_ms": 5000
  }
}
```

Heartbeat request：

```json
{
  "agent_id": "ag_01HXYZ",
  "host_id": "host_01HXYZ",
  "sequence": 1001,
  "resources": {
    "cpu_usage_pct": 35.2,
    "memory_used_bytes": 8589934592,
    "memory_total_bytes": 34359738368,
    "disk_used_bytes": 100000000000,
    "disk_total_bytes": 500000000000,
    "network_rx_bps": 120000,
    "network_tx_bps": 240000
  },
  "runtime_summaries": [
    {
      "runtime_id": "rt_01HXYZ",
      "actual_state": "running",
      "process_id": 12345,
      "observed_desired_version": 42
    }
  ]
}
```

Desired-state response：

```json
{
  "request_id": "req_01HXYZ",
  "data": {
    "version": 43,
    "runtimes": [
      {
        "runtime_id": "rt_01HXYZ",
        "host_id": "host_01HXYZ",
        "desired_state": "running",
        "config_version": 17,
        "listen_port": 4202,
        "worker_count": 4,
        "restart_policy": "on_failure",
        "draining": false,
        "version": 43
      }
    ]
  }
}
```

### 4.4 Control API

```text
GET  /api/v1/control/hosts
GET  /api/v1/control/hosts/{host_id}
POST /api/v1/control/hosts/{host_id}/actions/disable
POST /api/v1/control/hosts/{host_id}/actions/enable

GET  /api/v1/control/runtimes
POST /api/v1/control/runtimes
GET  /api/v1/control/runtimes/{runtime_id}
PUT  /api/v1/control/runtimes/{runtime_id}/desired-state
POST /api/v1/control/runtimes/{runtime_id}/actions/start
POST /api/v1/control/runtimes/{runtime_id}/actions/stop
POST /api/v1/control/runtimes/{runtime_id}/actions/restart
POST /api/v1/control/runtimes/{runtime_id}/actions/drain
POST /api/v1/control/runtimes/{runtime_id}/actions/undrain

GET  /api/v1/control/config-versions
POST /api/v1/control/config-versions
GET  /api/v1/control/config-versions/{config_version}
POST /api/v1/control/config-versions/{config_version}/publish
POST /api/v1/control/config-versions/{config_version}/rollback

GET  /api/v1/control/audit
GET  /api/v1/control/events
```

Create Runtime request：

```json
{
  "host_id": "host_01HXYZ",
  "name": "caster-a",
  "listen_port": 4202,
  "worker_count": 4,
  "max_worker_count": 16,
  "config_version": 17,
  "restart_policy": "on_failure",
  "start_immediately": true
}
```

Action response：

```json
{
  "request_id": "req_01HXYZ",
  "data": {
    "intent_id": "intent_01HXYZ",
    "status": "accepted",
    "runtime_id": "rt_01HXYZ",
    "desired_version": 44
  }
}
```

### 4.5 Admin / Me / Supplier API

v2 保留三类角色入口：

```text
/api/v1/admin/*
/api/v1/me/*
/api/v1/supplier/*
```

边界：

```text
me 和 supplier API 的 account_id 必须从登录 subject 推导。
me 和 supplier API 不接受客户端传入任意 account_id。
admin API 必须显式目标 ID。
admin 写操作必须写 operation_audit_logs。
Caster 接入认证使用 AccessAccount，不使用 Web token。
```

核心资源：

```text
accounts
access_accounts
mount_point_groups
mount_points
station_records
online_sessions
usage_facts
billing_cycles
supplier_supply_usage
operation_audit_logs
```

## 5. SSE 事件

```text
GET /api/v1/events/stream
```

事件 envelope：

```json
{
  "event_id": "evt_01HXYZ",
  "type": "runtime.actual_state_changed",
  "resource_type": "runtime",
  "resource_id": "rt_01HXYZ",
  "version": 1001,
  "occurred_at": "2026-06-27T10:00:05Z",
  "payload": {}
}
```

第一批事件类型：

```text
host.status_changed
agent.heartbeat_lost
agent.reconnected
runtime.desired_state_changed
runtime.actual_state_changed
runtime.event_created
runtime.metrics_updated
worker.metrics_updated
config.version_published
config.release_failed
audit.created
```

SSE 只用于控制面展示，不承载 NTRIP 数据流。

## 6. PostgreSQL 表域

PostgreSQL 表使用 lower_snake_case。以下为 v2 第一批表域，不等于最终 migration 的完整字段清单。

### 6.1 Identity

```text
accounts
account_sessions
roles
permissions
account_role_bindings
```

### 6.2 Access

```text
access_accounts
access_account_groups
account_allowed_mount_point_groups
mount_point_groups
mount_point_group_members
```

### 6.3 Assets

```text
mount_points
station_records
station_events
source_records
alias_rules
suppliers
```

### 6.4 Control

```text
hosts
agents
runtimes
runtime_desired_states
runtime_actual_snapshots
runtime_events
config_versions
config_releases
control_intents
```

### 6.5 Billing

```text
usage_facts
supply_duration_facts
billing_cycles
invoices
settlements
balance_ledger_entries
```

### 6.6 Audit

```text
operation_audit_logs
login_audit_logs
security_audit_logs
```

## 7. PostgreSQL 关键约束

建议约束：

```text
账号、接入账号、挂载点和配置版本都使用不可变 ID。
username 等安全凭证名删除后默认不复用，可用 tombstone 表或 deleted_at 部分索引。
金额和余额使用整数分或 decimal。
用量事实只追加，不承担主状态职责。
operation_audit_logs 只追加。
runtime_desired_states 以 runtime_id 维持当前版本，历史进入 runtime_events / control_intents / audit。
config_versions 发布后不可原地修改，只能创建新版本或 rollback release。
```

## 8. Redis key registry

Redis key 使用 `v2:` 前缀，避免和旧 key 混淆。

### 8.1 Auth projection

| Key | Type | 说明 |
| --- | --- | --- |
| `v2:auth:access-account:<username>` | STRING JSON | AccessAccountAuthIndex。 |
| `v2:auth:policy:<access_account_id>` | STRING JSON | 访问策略投影。 |
| `v2:auth:version` | STRING | 鉴权投影版本。 |

AccessAccountAuthIndex：

```json
{
  "access_account_id": "aacc_01HXYZ",
  "username": "rover01",
  "password_hash": "hash",
  "password_algo": "argon2id",
  "kind": "user_client",
  "status": "active",
  "owner_account_id": "acc_01HXYZ",
  "owner_status": "active",
  "mount_point_group_id": "mpg_01HXYZ",
  "concurrency_limit": 10,
  "expires_at": null,
  "version": 18
}
```

### 8.2 Config projection

| Key | Type | 说明 |
| --- | --- | --- |
| `v2:config:runtime:<runtime_id>` | STRING JSON | Runtime 当前配置投影。 |
| `v2:config:version` | STRING | 全局配置投影版本。 |
| `v2:control:config` | PUB/SUB | 配置变更通知。 |

### 8.3 Runtime actual state

| Key | Type | TTL | 说明 |
| --- | --- | --- | --- |
| `v2:agent:heartbeat:<agent_id>` | STRING JSON | 是 | Agent 心跳。 |
| `v2:runtime:actual:<runtime_id>` | STRING JSON | 是 | Runtime actual snapshot。 |
| `v2:runtime:worker-stat:<runtime_id>` | HASH | 是 | worker_id -> WorkerStat JSON。 |
| `v2:runtime:mount-owner:<runtime_id>` | HASH | 是 | mount -> worker_id。 |

### 8.4 Online sessions

| Key | Type | TTL | 说明 |
| --- | --- | --- | --- |
| `v2:session:access-account:<access_account_id>` | HASH | 是 | connect_key -> OnlineSession JSON。 |
| `v2:session:account:<account_id>` | HASH | 是 | connect_key -> OnlineSession JSON。 |
| `v2:session:mount:<mount>` | HASH | 是 | connect_key -> summary JSON。 |

### 8.5 Data bus

| Key / Channel | Type | 说明 |
| --- | --- | --- |
| `stream:mount:<mount>` | Pub/Sub | mount 数据流。 |
| `stream:runtime:<runtime_id>` | Pub/Sub | runtime 内控制或观测事件。 |
| `v2:control:kick` | Pub/Sub | 踢线和策略变更通知。 |
| `v2:control:config` | Pub/Sub | 配置投影变化。 |

说明：

```text
第一版使用普通 Pub/Sub。
当 Redis 数据面成为瓶颈时再评估 Sharded Pub/Sub。
单个超级热门 mount 的单频道瓶颈不能仅靠 Redis Cluster 自动解决。
```

`stream:mount:<mount>` 的最小 Caster payload 为二进制 envelope：

```text
magic: NCV2BUS1
fields: origin_runtime_id, mount, payload_length
body: raw NTRIP/RTCM/NMEA bytes
```

订阅端必须忽略 `origin_runtime_id` 等于本 runtime 的消息。该 channel 不兼容旧
`MPT:<mount>` channel，也不写 legacy data key。

## 9. PG -> Redis 投影

投影流程：

```text
AdminService transaction:
  write PostgreSQL source row
  write outbox event or bump projection version
  write audit log

Projection worker:
  read changed rows / outbox
  build projection JSON
  write Redis key with version/checksum
  publish v2:control:* notification

Caster Runtime:
  receive notification or poll version
  update local cache
  continue hot path without PG
```

投影要求：

```text
幂等。
可从 PostgreSQL 全量重建。
带 version 或 checksum。
失败可重试。
不得让部分 Redis 写入成为唯一事实。
```

## 10. Actual state 汇聚

Runtime actual state 来源：

```text
Agent heartbeat / actual-state API。
Caster Runtime Redis TTL snapshot。
Caster Runtime local metrics。
Runtime events。
```

AdminService 聚合策略：

```text
Agent 上报是本机进程事实的主输入。
Redis TTL snapshot 是 Runtime 快速展示和失联判断输入。
PG runtime_actual_snapshots 可保存低频历史快照。
Web 展示需标注 updated_at 和 stale 状态。
```

## 11. Caster 接入认证契约

连接建立时：

```text
1. Acceptor 解析 NTRIP 方法、mount、Basic Auth 和初始 GGA。
2. Worker 通过本地 auth cache 查 username。
3. cache miss 时读取 Redis v2:auth:access-account:<username>。
4. 校验 access account、owner account、密码、状态、过期、IP、分组权限。
5. 校验并发、余额或订阅快照。
6. 建立 OnlineSession。
7. 写 Redis session TTL。
```

禁止：

```text
登录热路径查询 PostgreSQL。
登录热路径同步调用 AdminService。
登录热路径读取旧 ACT:* 作为 v2 必需行为。
```

## 12. IDL / protobuf

v2 不沿用旧 protobuf：

```text
不复用旧 .proto 文件。
不保留旧字段编号。
不保留旧 package。
不把旧 message 作为 HTTP JSON 契约。
不把 protobuf 包装到 RTCM 数据帧。
```

如果后续需要 IDL：

```text
放在 api/idl 或 caster/idl。
package 从 v2 重新命名。
字段编号从 v2 重新分配。
用途限定为内部稳定结构或高频跨进程结构。
Web/Admin/Agent 第一版仍以 HTTP/JSON 为准。
```

## 13. 后续验证要求

后续实现任务至少覆盖：

```text
OpenAPI 或等价 API contract check。
PostgreSQL migration smoke。
Redis key registry 单元测试。
PG -> Redis projection rebuild 测试。
AdminService 写 PG 后 Redis 投影更新。
Caster auth cache miss 只访问 Redis，不访问 PG。
Redis 投影丢失后可从 PG 重建。
SSE 事件 envelope 版本和断线重连语义。
me/supplier API 不接受任意 account_id 越权。
admin API 写操作写 operation_audit_logs。
```
